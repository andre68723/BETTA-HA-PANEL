/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_display_precision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define DP_LOCK() taskENTER_CRITICAL(&s_lock)
#define DP_UNLOCK() taskEXIT_CRITICAL(&s_lock)
#define DP_CALLOC(n, size) heap_caps_calloc((n), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define DP_LOCK() ((void)0)
#define DP_UNLOCK() ((void)0)
#define DP_CALLOC(n, size) calloc((n), (size))
#endif

/* Entities looked up and remembered; a layout rarely has more than a few dozen numeric ones. */
#define DP_CAPACITY 256
/* Precision beyond this is noise on a panel (and keeps the output short). */
#define DP_MAX_DIGITS 6

/* Read by every task, under the lock: hashes of the entity ids and their precision. */
static uint32_t *s_hash;
static int8_t *s_digits;
static size_t s_known;

/* HA task only: the entities still to ask for and the request in flight. */
static char *s_wanted;
static size_t s_wanted_count;
static size_t s_next;
static uint32_t s_inflight_id;
static size_t s_inflight_first;
static int64_t s_inflight_since_ms;

static uint32_t dp_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s != '\0'; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

static bool dp_is_numeric_domain(const char *entity_id)
{
    return strncmp(entity_id, "sensor.", 7) == 0 || strncmp(entity_id, "number.", 7) == 0 ||
           strncmp(entity_id, "input_number.", 13) == 0;
}

static bool dp_ensure_buffers(void)
{
    if (s_wanted == NULL) {
        s_wanted = DP_CALLOC(DP_CAPACITY, APP_MAX_ENTITY_ID_LEN);
    }
    if (s_hash == NULL) {
        uint32_t *hash = DP_CALLOC(DP_CAPACITY, sizeof(*hash));
        int8_t *digits = DP_CALLOC(DP_CAPACITY, sizeof(*digits));
        if (hash == NULL || digits == NULL) {
            free(hash);
            free(digits);
            return false;
        }
        DP_LOCK();
        s_hash = hash;
        s_digits = digits;
        DP_UNLOCK();
    }
    return s_wanted != NULL;
}

/* Returns whether the stored precision changed. */
static bool dp_store(const char *entity_id, int digits)
{
    uint32_t h = dp_hash(entity_id);
    bool changed = false;
    DP_LOCK();
    size_t i = 0;
    while (i < s_known && s_hash[i] != h) {
        i++;
    }
    if (i == s_known && s_known < DP_CAPACITY) {
        s_hash[i] = h;
        s_digits[i] = HA_DISPLAY_PRECISION_UNKNOWN;
        s_known++;
    }
    if (i < s_known && s_digits[i] != digits) {
        s_digits[i] = (int8_t)digits;
        changed = true;
    }
    DP_UNLOCK();
    return changed;
}

static int dp_entry_digits(const cJSON *entry)
{
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(entry, "options");
    const cJSON *sensor = cJSON_GetObjectItemCaseSensitive(options, "sensor");
    const cJSON *digits = cJSON_GetObjectItemCaseSensitive(sensor, "display_precision");
    if (!cJSON_IsNumber(digits)) {
        digits = cJSON_GetObjectItemCaseSensitive(sensor, "suggested_display_precision");
    }
    if (!cJSON_IsNumber(digits) || digits->valuedouble < 0) {
        return HA_DISPLAY_PRECISION_UNKNOWN;
    }
    return digits->valuedouble > DP_MAX_DIGITS ? DP_MAX_DIGITS : (int)digits->valuedouble;
}

void ha_display_precision_set_wanted(const char *entity_ids, size_t count)
{
    s_wanted_count = 0;
    s_next = 0;
    s_inflight_id = 0;
    if (entity_ids == NULL || !dp_ensure_buffers()) {
        return;
    }
    for (size_t i = 0; i < count && s_wanted_count < DP_CAPACITY; i++) {
        const char *id = entity_ids + i * APP_MAX_ENTITY_ID_LEN;
        if (dp_is_numeric_domain(id)) {
            snprintf(s_wanted + s_wanted_count * APP_MAX_ENTITY_ID_LEN, APP_MAX_ENTITY_ID_LEN, "%s", id);
            s_wanted_count++;
        }
    }
}

bool ha_display_precision_is_due(int64_t now_ms)
{
    if (s_inflight_id != 0) {
        if (now_ms - s_inflight_since_ms < HA_DISPLAY_PRECISION_TIMEOUT_MS) {
            return false;
        }
        s_inflight_id = 0; /* never answered: skip that batch rather than ask forever */
    }
    return s_next < s_wanted_count;
}

cJSON *ha_display_precision_next_request(uint32_t msg_id, int64_t now_ms)
{
    if (msg_id == 0 || !ha_display_precision_is_due(now_ms)) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *ids = cJSON_AddArrayToObject(root, "entity_ids");
    if (ids == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddNumberToObject(root, "id", (double)msg_id);
    cJSON_AddStringToObject(root, "type", "config/entity_registry/get_entries");
    size_t end = s_next + HA_DISPLAY_PRECISION_BATCH;
    if (end > s_wanted_count) {
        end = s_wanted_count;
    }
    for (size_t i = s_next; i < end; i++) {
        cJSON_AddItemToArray(ids, cJSON_CreateString(s_wanted + i * APP_MAX_ENTITY_ID_LEN));
    }
    s_inflight_id = msg_id;
    s_inflight_first = s_next;
    s_inflight_since_ms = now_ms;
    s_next = end;
    return root;
}

void ha_display_precision_cancel(void)
{
    if (s_inflight_id != 0) {
        s_next = s_inflight_first;
        s_inflight_id = 0;
    }
}

bool ha_display_precision_handle_result(
    uint32_t msg_id, bool success, const cJSON *result, ha_display_precision_changed_fn changed)
{
    if (msg_id == 0 || msg_id != s_inflight_id) {
        return false;
    }
    s_inflight_id = 0;
    if (!success || !cJSON_IsObject(result)) {
        s_next = s_wanted_count; /* a Home Assistant without get_entries: stop asking */
        return true;
    }
    for (size_t i = s_inflight_first; i < s_next; i++) {
        const char *id = s_wanted + i * APP_MAX_ENTITY_ID_LEN;
        const cJSON *entry = cJSON_GetObjectItemCaseSensitive(result, id);
        int digits = cJSON_IsObject(entry) ? dp_entry_digits(entry) : HA_DISPLAY_PRECISION_UNKNOWN;
        if (dp_store(id, digits) && changed != NULL) {
            changed(id);
        }
    }
    return true;
}

int ha_display_precision_get(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return HA_DISPLAY_PRECISION_UNKNOWN;
    }
    uint32_t h = dp_hash(entity_id);
    int digits = HA_DISPLAY_PRECISION_UNKNOWN;
    DP_LOCK();
    for (size_t i = 0; i < s_known; i++) {
        if (s_hash[i] == h) {
            digits = s_digits[i];
            break;
        }
    }
    DP_UNLOCK();
    return digits;
}

bool ha_display_precision_format(const char *entity_id, const char *state, char *out, size_t out_len)
{
    if (state == NULL || state[0] == '\0' || out == NULL || out_len == 0) {
        return false;
    }
    int digits = ha_display_precision_get(entity_id);
    if (digits < 0) {
        return false;
    }
    char *end = NULL;
    double value = strtod(state, &end);
    if (end == state || *end != '\0' || !isfinite(value) || state[0] == ' ') {
        return false;
    }
    char text[48];
    int n = snprintf(text, sizeof(text), "%.*f", digits, value);
    if (n <= 0 || (size_t)n >= sizeof(text) || (size_t)n >= out_len) {
        return false;
    }
    /* -0.004 at two digits is "0.00", not "-0.00". */
    const char *shown = text;
    if (text[0] == '-' && strspn(text + 1, "0.") == strlen(text + 1)) {
        shown = text + 1;
    }
    snprintf(out, out_len, "%s", shown);
    return true;
}
