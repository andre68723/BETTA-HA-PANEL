/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_display_precision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ha/ha_layout_entities.h"

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

/* Registry lookups also determine quote providers: cover every entity the transport can subscribe to. */
#define DP_CAPACITY HA_LAYOUT_ENTITY_CAPACITY
/* Precision beyond this is noise on a panel (and keeps the output short). */
#define DP_MAX_DIGITS 6

/* The integration of an entity, as far as a reader needs it (ha_display_precision_platform()). */
typedef enum { DP_PLATFORM_PENDING = 0, DP_PLATFORM_FAILED, DP_PLATFORM_OTHER, DP_PLATFORM_EASY_STOCK } dp_platform_t;

/* Read by every task, under the lock: hashes of the entity ids, their precision and integration. */
static uint32_t *s_hash;
static int8_t *s_digits;
static uint8_t *s_platform;
static uint8_t *s_keep; /* HA-task scratch for pruning; keep it off the task stack. */
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
    if (s_keep == NULL) s_keep = DP_CALLOC(DP_CAPACITY, sizeof(*s_keep));
    if (s_hash == NULL) {
        uint32_t *hash = DP_CALLOC(DP_CAPACITY, sizeof(*hash));
        int8_t *digits = DP_CALLOC(DP_CAPACITY, sizeof(*digits));
        uint8_t *platform = DP_CALLOC(DP_CAPACITY, sizeof(*platform));
        if (hash == NULL || digits == NULL || platform == NULL) {
            free(hash);
            free(digits);
            free(platform);
            return false;
        }
        DP_LOCK();
        s_hash = hash;
        s_digits = digits;
        s_platform = platform;
        DP_UNLOCK();
    }
    return s_wanted != NULL && s_keep != NULL;
}

/* Returns whether the stored precision or integration changed. A failed lookup keeps the precision known before. */
static bool dp_store(const char *entity_id, int digits, dp_platform_t platform)
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
        s_platform[i] = DP_PLATFORM_PENDING;
        s_known++;
    }
    if (i < s_known && platform != DP_PLATFORM_FAILED && s_digits[i] != digits) {
        s_digits[i] = (int8_t)digits;
        changed = true;
    }
    if (i < s_known && s_platform[i] != platform) {
        s_platform[i] = (uint8_t)platform;
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
    if (!cJSON_IsNumber(digits) || !isfinite(digits->valuedouble) || digits->valuedouble < 0) {
        return HA_DISPLAY_PRECISION_UNKNOWN;
    }
    return digits->valuedouble > DP_MAX_DIGITS ? DP_MAX_DIGITS : (int)digits->valuedouble;
}

static dp_platform_t dp_entry_platform(const cJSON *entry)
{
    const cJSON *platform = cJSON_GetObjectItemCaseSensitive(entry, "platform");
    if (!cJSON_IsString(platform) || platform->valuestring == NULL) return DP_PLATFORM_OTHER;
    if (strcmp(platform->valuestring, "easy_stock") == 0) return DP_PLATFORM_EASY_STOCK;
    return DP_PLATFORM_OTHER;
}

/* The entities first .. end-1 of the wanted list got no answer: their integration is unknown for good (until the next
 * lookup), so a reader can say so instead of waiting. */
static void dp_mark_failed(size_t first, size_t end, ha_display_precision_changed_fn changed)
{
    for (size_t i = first; i < end && i < s_wanted_count; i++) {
        const char *id = s_wanted + i * APP_MAX_ENTITY_ID_LEN;
        if (dp_store(id, HA_DISPLAY_PRECISION_UNKNOWN, DP_PLATFORM_FAILED) && changed != NULL) {
            changed(id);
        }
    }
}

bool ha_display_precision_set_wanted(const char *entity_ids, size_t count)
{
    if (count > 0 && (entity_ids == NULL || !dp_ensure_buffers())) return false;
    s_wanted_count = 0;
    s_next = 0;
    s_inflight_id = 0;
    for (size_t i = 0; i < count && s_wanted_count < DP_CAPACITY; i++) {
        const char *id = entity_ids + i * APP_MAX_ENTITY_ID_LEN;
        if (dp_is_numeric_domain(id)) {
            snprintf(s_wanted + s_wanted_count * APP_MAX_ENTITY_ID_LEN, APP_MAX_ENTITY_ID_LEN, "%s", id);
            s_wanted_count++;
        }
    }
    /* Layout edits must not fill the cache with entities no longer used. Quote-provider lookups rely
     * on this cache too: after 256 different entities a new quote otherwise stays pending until reboot.
     * This function and dp_store() have the same HA-task owner, so only the final move needs the read lock. */
    if (s_keep != NULL) memset(s_keep, 0, DP_CAPACITY * sizeof(*s_keep));
    for (size_t i = 0; i < s_wanted_count; i++) {
        const uint32_t h = dp_hash(s_wanted + i * APP_MAX_ENTITY_ID_LEN);
        for (size_t j = 0; j < s_known; j++) if (s_hash[j] == h) s_keep[j] = true;
    }
    DP_LOCK();
    size_t kept = 0;
    for (size_t i = 0; i < s_known; i++) {
        if (s_keep == NULL || !s_keep[i]) continue;
        s_hash[kept] = s_hash[i];
        s_digits[kept] = s_digits[i];
        s_platform[kept++] = s_platform[i];
    }
    s_known = kept;
    DP_UNLOCK();
    return true;
}

bool ha_display_precision_is_due(int64_t now_ms)
{
    if (s_inflight_id != 0) {
        if (now_ms - s_inflight_since_ms < HA_DISPLAY_PRECISION_TIMEOUT_MS) {
            return false;
        }
        s_inflight_id = 0; /* never answered: skip that batch rather than ask forever */
        dp_mark_failed(s_inflight_first, s_next, NULL); /* readers notice on their next state update */
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
        dp_mark_failed(s_inflight_first, s_wanted_count, changed);
        s_next = s_wanted_count; /* a Home Assistant without get_entries: stop asking */
        return true;
    }
    for (size_t i = s_inflight_first; i < s_next; i++) {
        const char *id = s_wanted + i * APP_MAX_ENTITY_ID_LEN;
        const cJSON *entry = cJSON_GetObjectItemCaseSensitive(result, id);
        /* an entity without a registry entry (no unique ID) is no integration's quote */
        int digits = cJSON_IsObject(entry) ? dp_entry_digits(entry) : HA_DISPLAY_PRECISION_UNKNOWN;
        dp_platform_t platform = cJSON_IsObject(entry) ? dp_entry_platform(entry) : DP_PLATFORM_OTHER;
        if (dp_store(id, digits, platform) && changed != NULL) {
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

const char *ha_display_precision_platform(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return NULL;
    }
    uint32_t h = dp_hash(entity_id);
    dp_platform_t platform = DP_PLATFORM_PENDING;
    DP_LOCK();
    for (size_t i = 0; i < s_known; i++) {
        if (s_hash[i] == h) {
            platform = (dp_platform_t)s_platform[i];
            break;
        }
    }
    DP_UNLOCK();
    switch (platform) {
    case DP_PLATFORM_FAILED: return "";
    case DP_PLATFORM_OTHER: return "other";
    case DP_PLATFORM_EASY_STOCK: return "easy_stock";
    default: return NULL;
    }
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
