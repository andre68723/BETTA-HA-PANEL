/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_printer_lookup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "layout/bambu_printer_slots.h"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define PL_LOCK() taskENTER_CRITICAL(&s_lock)
#define PL_UNLOCK() taskEXIT_CRITICAL(&s_lock)
#define PL_CALLOC(n, size) heap_caps_calloc((n), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define PL_LOCK() ((void)0)
#define PL_UNLOCK() ((void)0)
#define PL_CALLOC(n, size) calloc((n), (size))
#endif

#define PL_MAX_BINDINGS 11
#define PL_MAX_RELATED 160 /* entities of one printer (a printer with an AMS has about 70) */
#define PL_ID_LEN 40
#define PL_TITLE_LEN 64

typedef enum {
    PL_IDLE = 0,
    PL_ENTRIES,  /* config_entries/get */
    PL_RELATED,  /* search/related of entry s_current */
    PL_GET,      /* get_entries batches of entry s_current */
    PL_READY,
    PL_ERROR,
} pl_phase_t;

typedef struct {
    char id[PL_ID_LEN];
    char title[PL_TITLE_LEN];
    char entities[PL_MAX_BINDINGS][APP_MAX_ENTITY_ID_LEN];
} pl_printer_t;

/* Shared snapshot, under the lock. */
static pl_printer_t s_printers[HA_PRINTER_LOOKUP_MAX_PRINTERS];
static size_t s_printer_count;
static pl_phase_t s_phase;

/* HA task only. */
static bool s_send_due;        /* the next step is ready to be sent */
static uint32_t s_inflight_id;
static int64_t s_inflight_since_ms;
static size_t s_current;       /* the printer being resolved */
static char (*s_related)[APP_MAX_ENTITY_ID_LEN];
static size_t s_related_count;
static size_t s_related_next;

static bool pl_ensure_buffer(void)
{
    if (s_related == NULL) {
        s_related = PL_CALLOC(PL_MAX_RELATED, APP_MAX_ENTITY_ID_LEN);
    }
    return s_related != NULL;
}

static void pl_set_phase(pl_phase_t phase)
{
    PL_LOCK();
    s_phase = phase;
    PL_UNLOCK();
}

static void pl_fail(void)
{
    s_send_due = false;
    s_inflight_id = 0;
    pl_set_phase(PL_ERROR);
}

void ha_printer_lookup_request(void)
{
    PL_LOCK();
    s_printer_count = 0;
    memset(s_printers, 0, sizeof(s_printers));
    s_phase = PL_ENTRIES;
    PL_UNLOCK();
    s_current = 0;
    s_related_count = 0;
    s_related_next = 0;
    s_inflight_id = 0;
    s_send_due = true;
}

void ha_printer_lookup_cancel(void)
{
    if (s_phase == PL_ENTRIES || s_phase == PL_RELATED || s_phase == PL_GET) {
        pl_fail();
    }
}

bool ha_printer_lookup_is_due(int64_t now_ms)
{
    if (s_inflight_id != 0) {
        if (now_ms - s_inflight_since_ms >= HA_PRINTER_LOOKUP_TIMEOUT_MS) {
            pl_fail(); /* never answered */
        }
        return false;
    }
    return s_send_due && (s_phase == PL_ENTRIES || s_phase == PL_RELATED || s_phase == PL_GET);
}

static cJSON *pl_request(uint32_t msg_id, const char *type)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "id", (double)msg_id);
    cJSON_AddStringToObject(root, "type", type);
    return root;
}

cJSON *ha_printer_lookup_next_request(uint32_t msg_id, int64_t now_ms)
{
    if (msg_id == 0 || !ha_printer_lookup_is_due(now_ms)) {
        return NULL;
    }
    cJSON *root = NULL;
    switch (s_phase) {
    case PL_ENTRIES:
        root = pl_request(msg_id, "config_entries/get");
        if (root != NULL) cJSON_AddStringToObject(root, "domain", "bambu_lab");
        break;
    case PL_RELATED:
        root = pl_request(msg_id, "search/related");
        if (root != NULL) {
            cJSON_AddStringToObject(root, "item_type", "config_entry");
            cJSON_AddStringToObject(root, "item_id", s_printers[s_current].id);
        }
        break;
    case PL_GET: {
        root = pl_request(msg_id, "config/entity_registry/get_entries");
        cJSON *ids = root != NULL ? cJSON_AddArrayToObject(root, "entity_ids") : NULL;
        if (ids == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        size_t end = s_related_next + HA_PRINTER_LOOKUP_BATCH;
        if (end > s_related_count) end = s_related_count;
        for (size_t i = s_related_next; i < end; i++) {
            cJSON_AddItemToArray(ids, cJSON_CreateString(s_related[i]));
        }
        break;
    }
    default:
        return NULL;
    }
    if (root == NULL) {
        return NULL;
    }
    s_inflight_id = msg_id;
    s_inflight_since_ms = now_ms;
    s_send_due = false;
    return root;
}

void ha_printer_lookup_send_failed(void)
{
    if (s_inflight_id != 0) {
        s_inflight_id = 0;
        s_send_due = true; /* ask again at the next opportunity */
    }
}

/* Moves on to the next printer, or finishes. */
static void pl_next_printer(void)
{
    s_current++;
    s_related_count = 0;
    s_related_next = 0;
    PL_LOCK();
    const size_t count = s_printer_count;
    PL_UNLOCK();
    if (s_current < count) {
        pl_set_phase(PL_RELATED);
        s_send_due = true;
    } else {
        s_send_due = false;
        pl_set_phase(PL_READY);
    }
}

static bool pl_has_entity(const char *entity_id)
{
    const char *dot = strchr(entity_id, '.');
    return dot != NULL && dot != entity_id;
}

/* Stores the entities of a get_entries answer that one binding names, by translation_key and domain. */
static void pl_store_entries(const cJSON *result, size_t first, size_t last)
{
    for (size_t i = first; i < last; i++) {
        const char *id = s_related[i];
        const cJSON *entry = cJSON_GetObjectItemCaseSensitive(result, id);
        const cJSON *key = cJSON_IsObject(entry) ? cJSON_GetObjectItemCaseSensitive(entry, "translation_key") : NULL;
        if (!cJSON_IsString(key) || key->valuestring == NULL) {
            continue;
        }
        for (size_t b = 0; b < k_bambu_printer_binding_count && b < PL_MAX_BINDINGS; b++) {
            const bambu_printer_binding_t *binding = &k_bambu_printer_bindings[b];
            const size_t dlen = strlen(binding->domain);
            if (strcmp(key->valuestring, binding->translation_key) == 0 && strncmp(id, binding->domain, dlen) == 0 &&
                id[dlen] == '.') {
                PL_LOCK();
                if (s_printers[s_current].entities[b][0] == '\0') {
                    snprintf(s_printers[s_current].entities[b], APP_MAX_ENTITY_ID_LEN, "%s", id);
                }
                PL_UNLOCK();
            }
        }
    }
}

bool ha_printer_lookup_handle_result(uint32_t msg_id, bool success, const cJSON *result)
{
    if (msg_id == 0 || msg_id != s_inflight_id) {
        return false;
    }
    s_inflight_id = 0;
    if (!success) {
        pl_fail();
        return true;
    }
    switch (s_phase) {
    case PL_ENTRIES: {
        size_t count = 0;
        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, result)
        {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "entry_id");
            const cJSON *title = cJSON_GetObjectItemCaseSensitive(entry, "title");
            const cJSON *domain = cJSON_GetObjectItemCaseSensitive(entry, "domain");
            if (!cJSON_IsString(id) || id->valuestring == NULL || strlen(id->valuestring) >= PL_ID_LEN ||
                (cJSON_IsString(domain) && strcmp(domain->valuestring, "bambu_lab") != 0) ||
                count >= HA_PRINTER_LOOKUP_MAX_PRINTERS) {
                continue;
            }
            PL_LOCK();
            snprintf(s_printers[count].id, PL_ID_LEN, "%s", id->valuestring);
            snprintf(s_printers[count].title, PL_TITLE_LEN, "%s",
                     cJSON_IsString(title) && title->valuestring != NULL ? title->valuestring : id->valuestring);
            PL_UNLOCK();
            count++;
        }
        PL_LOCK();
        s_printer_count = count;
        PL_UNLOCK();
        s_current = 0;
        if (count == 0) {
            pl_set_phase(PL_READY); /* no Bambu Lab integration, or no printer */
        } else {
            pl_set_phase(PL_RELATED);
            s_send_due = true;
        }
        return true;
    }
    case PL_RELATED: {
        if (!pl_ensure_buffer()) {
            pl_fail();
            return true;
        }
        s_related_count = 0;
        const cJSON *entities = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "entity") : NULL;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, entities)
        {
            if (cJSON_IsString(item) && item->valuestring != NULL && pl_has_entity(item->valuestring) &&
                strlen(item->valuestring) < APP_MAX_ENTITY_ID_LEN && s_related_count < PL_MAX_RELATED) {
                snprintf(s_related[s_related_count++], APP_MAX_ENTITY_ID_LEN, "%s", item->valuestring);
            }
        }
        s_related_next = 0;
        if (s_related_count == 0) {
            pl_next_printer();
        } else {
            pl_set_phase(PL_GET);
            s_send_due = true;
        }
        return true;
    }
    case PL_GET: {
        size_t end = s_related_next + HA_PRINTER_LOOKUP_BATCH;
        if (end > s_related_count) end = s_related_count;
        if (cJSON_IsObject(result)) {
            pl_store_entries(result, s_related_next, end);
        }
        s_related_next = end;
        if (s_related_next < s_related_count) {
            s_send_due = true;
        } else {
            pl_next_printer();
        }
        return true;
    }
    default:
        return true;
    }
}

char *ha_printer_lookup_json(void)
{
    pl_printer_t *snapshot = PL_CALLOC(HA_PRINTER_LOOKUP_MAX_PRINTERS, sizeof(*snapshot));
    if (snapshot == NULL) {
        return NULL;
    }
    PL_LOCK();
    const pl_phase_t phase = s_phase;
    const size_t count = s_printer_count;
    memcpy(snapshot, s_printers, sizeof(s_printers));
    PL_UNLOCK();

    cJSON *root = cJSON_CreateObject();
    const char *status = phase == PL_READY ? "ready" : phase == PL_ERROR ? "error" : phase == PL_IDLE ? "idle" : "pending";
    cJSON_AddStringToObject(root, "status", status);
    cJSON *printers = cJSON_AddArrayToObject(root, "printers");
    for (size_t i = 0; phase == PL_READY && i < count && printers != NULL; i++) {
        cJSON *printer = cJSON_CreateObject();
        cJSON_AddStringToObject(printer, "id", snapshot[i].id);
        cJSON_AddStringToObject(printer, "name", snapshot[i].title);
        cJSON *entities = cJSON_AddObjectToObject(printer, "entities");
        for (size_t b = 0; b < k_bambu_printer_binding_count && b < PL_MAX_BINDINGS; b++) {
            if (snapshot[i].entities[b][0] != '\0') {
                cJSON_AddStringToObject(entities, k_bambu_printer_bindings[b].key, snapshot[i].entities[b]);
            }
        }
        cJSON_AddItemToArray(printers, printer);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(snapshot);
    return out;
}
