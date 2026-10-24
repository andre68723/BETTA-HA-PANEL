/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_evcc_lookup.h"
#include "layout/evcc_slots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define LOCK() taskENTER_CRITICAL(&s_lock)
#define UNLOCK() taskEXIT_CRITICAL(&s_lock)
#define ALLOC(n, size) heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define ALLOC(n, size) calloc(n, size)
#endif

#define MAX_INSTANCES 4
#define MAX_RELATED 512
enum { IDLE, ENTRIES, RELATED, GET, READY, ERROR };
typedef struct { char id[40], title[64]; } instance_t;
typedef struct {
    char instance[40], instance_name[64], stem[96];
    char entity[EVCC_ROLE_COUNT][APP_MAX_ENTITY_ID_LEN];
    bool conflict[EVCC_ROLE_COUNT];
    bool target_number;
} loadpoint_t;
static instance_t s_instances[MAX_INSTANCES];
static loadpoint_t *s_points;
/* The site power sensors of each instance, for the wallbox's energy-flow header (PSRAM like the loadpoints). */
static char (*s_sites)[EVCC_SITE_COUNT][APP_MAX_ENTITY_ID_LEN];
static char (*s_related)[APP_MAX_ENTITY_ID_LEN];
static size_t s_instances_count, s_current, s_related_count, s_next, s_count;
static int s_phase;
static bool s_restart, s_send;
static uint32_t s_inflight;
static int64_t s_since;

static void phase(int value) { LOCK(); s_phase = value; UNLOCK(); }
static void fail(void) { s_inflight = 0; s_send = false; phase(ERROR); }
void ha_evcc_lookup_request(void) { LOCK(); s_restart = true; UNLOCK(); }
/* Only the HA task mutates the state machine and buffers. A restart invalidates
 * the old message id; replies from an earlier generation are not consumed. */
static void start(void)
{
    if (!s_points) s_points = ALLOC(HA_EVCC_LOOKUP_MAX_LOADPOINTS, sizeof(*s_points));
    if (!s_sites) s_sites = ALLOC(MAX_INSTANCES, sizeof(*s_sites));
    if (!s_related) s_related = ALLOC(MAX_RELATED, sizeof(*s_related));
    LOCK();
    s_count = 0;
    if (s_points) memset(s_points, 0, HA_EVCC_LOOKUP_MAX_LOADPOINTS * sizeof(*s_points));
    if (s_sites) memset(s_sites, 0, MAX_INSTANCES * sizeof(*s_sites));
    UNLOCK();
    s_instances_count = s_current = s_related_count = s_next = 0;
    s_inflight = 0;
    if (!s_points || !s_related || !s_sites) { fail(); return; }
    s_send = true; phase(ENTRIES);
}
void ha_evcc_lookup_cancel(void)
{
    LOCK(); s_restart = false; UNLOCK();
    if (s_phase == ENTRIES || s_phase == RELATED || s_phase == GET) fail();
}
bool ha_evcc_lookup_is_due(int64_t now)
{
    LOCK(); bool restart = s_restart; s_restart = false; UNLOCK();
    if (restart) start();
    if (s_inflight && now - s_since >= HA_EVCC_LOOKUP_TIMEOUT_MS) fail();
    return s_send && !s_inflight;
}
cJSON *ha_evcc_lookup_next_request(uint32_t id, int64_t now)
{
    if (!id || !ha_evcc_lookup_is_due(now)) return NULL;
    const char *type = s_phase == ENTRIES ? "config_entries/get" : s_phase == RELATED ?
        "search/related" : "config/entity_registry/get_entries";
    cJSON *root = cJSON_CreateObject();
    if (!root) { fail(); return NULL; }
    cJSON_AddNumberToObject(root, "id", id); cJSON_AddStringToObject(root, "type", type);
    if (s_phase == ENTRIES) cJSON_AddStringToObject(root, "domain", "evcc_intg");
    else if (s_phase == RELATED) {
        cJSON_AddStringToObject(root, "item_type", "config_entry");
        cJSON_AddStringToObject(root, "item_id", s_instances[s_current].id);
    } else {
        cJSON *ids = cJSON_AddArrayToObject(root, "entity_ids");
        if (!ids) { cJSON_Delete(root); fail(); return NULL; }
        size_t end = s_next + HA_EVCC_LOOKUP_BATCH;
        if (end > s_related_count) end = s_related_count;
        for (size_t i = s_next; i < end; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(s_related[i]));
    }
    s_inflight = id; s_since = now; s_send = false; return root;
}
void ha_evcc_lookup_send_failed(void) { fail(); }
static const char *string(const cJSON *root, const char *key)
{
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, key));
}
static void next_instance(void)
{
    s_current++; s_next = s_related_count = 0;
    if (s_current < s_instances_count) { phase(RELATED); s_send = true; }
    else { phase(READY); s_send = false; }
}
static bool store(const char *id, const cJSON *entry)
{
    const char *platform = string(entry, "platform"), *instance = string(entry, "config_entry_id");
    if (!platform || strcmp(platform, "evcc_intg") || !instance || strcmp(instance, s_instances[s_current].id)) return true;
    /* Disabled entities have no state and must not become writable bindings. */
    const cJSON *disabled = cJSON_GetObjectItemCaseSensitive(entry, "disabled_by");
    if (disabled && !cJSON_IsNull(disabled)) return true;
    char stem[96]; size_t role;
    if (evcc_registry_site_role(id, string(entry, "unique_id"), &role)) {
        LOCK();
        if (!s_sites[s_current][role][0]) snprintf(s_sites[s_current][role], APP_MAX_ENTITY_ID_LEN, "%s", id);
        UNLOCK();
        return true;
    }
    if (!evcc_registry_role(id, string(entry, "unique_id"), stem, sizeof(stem), &role)) return true;
    LOCK();
    size_t n;
    for (n = 0; n < s_count; n++) if (!strcmp(s_points[n].instance, instance) && !strcmp(s_points[n].stem, stem)) break;
    if (n == s_count) {
        if (s_count == HA_EVCC_LOOKUP_MAX_LOADPOINTS) { UNLOCK(); return false; }
        s_count++;
        snprintf(s_points[n].instance, sizeof(s_points[n].instance), "%s", instance);
        snprintf(s_points[n].instance_name, sizeof(s_points[n].instance_name), "%s", s_instances[s_current].title);
        strcpy(s_points[n].stem, stem);
    }
    loadpoint_t *lp = &s_points[n];
    char *old = lp->entity[role];
    /* The arrival/planning select may share the unique_id with the real default
     * number limit. A number always wins, independently of registry/batch order. */
    bool incoming_number = role == EVCC_TARGET && !strncmp(id, "number.", 7);
    bool old_number = role == EVCC_TARGET && lp->target_number;
    if (incoming_number && !old_number) { strcpy(old, id); lp->conflict[role] = false; lp->target_number = true; }
    else if (!old_number || incoming_number) {
        if (*old && strcmp(old, id)) { old[0] = 0; lp->conflict[role] = true; }
        else if (!lp->conflict[role]) strcpy(old, id);
    }
    UNLOCK(); return true;
}
bool ha_evcc_lookup_handle_result(uint32_t id, bool success, const cJSON *result)
{
    if (!id || id != s_inflight) return false;
    s_inflight = 0;
    if (!success) { fail(); return true; }
    if (s_phase == ENTRIES) {
        if (!cJSON_IsArray(result)) { fail(); return true; }
        const cJSON *item;
        cJSON_ArrayForEach(item, result) {
            const char *entry = string(item, "entry_id"), *name = string(item, "title"), *domain = string(item, "domain");
            if (!domain || strcmp(domain, "evcc_intg")) continue;
            if (!entry || !*entry || strlen(entry) >= sizeof(s_instances[0].id) || s_instances_count == MAX_INSTANCES) {
                fail(); return true;
            }
            instance_t *out = &s_instances[s_instances_count++];
            strcpy(out->id, entry); snprintf(out->title, sizeof(out->title), "%s", name ? name : entry);
        }
        if (!s_instances_count) phase(READY); else { phase(RELATED); s_send = true; }
    } else if (s_phase == RELATED) {
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(result, "entity");
        if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > MAX_RELATED) { fail(); return true; }
        const cJSON *item;
        cJSON_ArrayForEach(item, list) {
            const char *name = cJSON_GetStringValue(item);
            if (!name || !strchr(name, '.') || strlen(name) >= APP_MAX_ENTITY_ID_LEN) { fail(); return true; }
            strcpy(s_related[s_related_count++], name);
        }
        if (!s_related_count) next_instance(); else { phase(GET); s_send = true; }
    } else if (s_phase == GET) {
        if (!cJSON_IsObject(result)) { fail(); return true; }
        size_t end = s_next + HA_EVCC_LOOKUP_BATCH;
        if (end > s_related_count) end = s_related_count;
        for (size_t i = s_next; i < end; i++) {
            const cJSON *entry = cJSON_GetObjectItemCaseSensitive(result, s_related[i]);
            if (!store(s_related[i], entry)) { fail(); return true; }
        }
        s_next = end;
        if (s_next < s_related_count) s_send = true; else next_instance();
    }
    return true;
}
char *ha_evcc_lookup_json(void)
{
    loadpoint_t *snapshot = ALLOC(HA_EVCC_LOOKUP_MAX_LOADPOINTS, sizeof(*snapshot));
    char (*sites)[EVCC_SITE_COUNT][APP_MAX_ENTITY_ID_LEN] = ALLOC(MAX_INSTANCES, sizeof(*sites));
    if (!snapshot || !sites) { free(snapshot); free(sites); return NULL; }
    instance_t instances[MAX_INSTANCES];
    LOCK();
    int state = s_restart ? ENTRIES : s_phase; size_t count = s_count, instance_count = s_instances_count;
    if (s_points) memcpy(snapshot, s_points, count * sizeof(*snapshot));
    if (s_sites) memcpy(sites, s_sites, MAX_INSTANCES * sizeof(*sites));
    memcpy(instances, s_instances, sizeof(instances));
    UNLOCK();
    cJSON *root = cJSON_CreateObject();
    if (!root) { free(snapshot); free(sites); return NULL; }
    cJSON_AddStringToObject(root, "status", state == READY ? "ready" : state == ERROR ? "error" : state == IDLE ? "idle" : "pending");
    cJSON *list = cJSON_AddArrayToObject(root, "loadpoints");
    for (size_t i = 0; state == READY && i < count && list; i++) {
        loadpoint_t *lp = &snapshot[i];
        /* Battery/site controls can end in mode too, but have no charge_power.
         * A disabled charger can consist only of disabled_in_config. */
        if (!lp->entity[EVCC_POWER][0] && !lp->entity[EVCC_DISABLED][0]) continue;
        cJSON *item = cJSON_CreateObject();
        char id[140]; snprintf(id, sizeof(id), "%s/%s", lp->instance, lp->stem);
        cJSON_AddStringToObject(item, "id", id); cJSON_AddStringToObject(item, "name", lp->stem);
        cJSON_AddStringToObject(item, "instance", lp->instance_name);
        cJSON *bindings = cJSON_AddObjectToObject(item, "entities");
        for (size_t j = 0; j < EVCC_ROLE_COUNT; j++) if (lp->entity[j][0]) cJSON_AddStringToObject(bindings, k_evcc_bindings[j].key, lp->entity[j]);
        /* the instance's site sensors, for the energy-flow header */
        for (size_t k = 0; k < instance_count; k++) {
            if (strcmp(instances[k].id, lp->instance)) continue;
            for (size_t j = 0; j < EVCC_SITE_COUNT; j++)
                if (sites[k][j][0]) cJSON_AddStringToObject(bindings, k_evcc_site_bindings[j].key, sites[k][j]);
        }
        cJSON_AddItemToArray(list, item);
    }
    char *out = cJSON_PrintUnformatted(root); cJSON_Delete(root); free(snapshot); free(sites); return out;
}
