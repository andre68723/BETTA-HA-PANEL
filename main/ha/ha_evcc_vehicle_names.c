/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_evcc_vehicle_names.h"
#include "ha/ha_select_options.h"
#include "app_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define LOCK() taskENTER_CRITICAL(&s_lock)
#define UNLOCK() taskEXIT_CRITICAL(&s_lock)
#define ALLOC() heap_caps_calloc(1, sizeof(names_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define ALLOC() calloc(1, sizeof(names_t))
#endif
typedef struct { char id[HA_SELECT_OPTION_LEN], name[APP_MAX_NAME_LEN]; } name_t;
typedef struct { size_t count; name_t item[HA_SELECT_MAX_OPTIONS]; } names_t;
static names_t *s_names;
static bool s_wanted;
static uint32_t s_token;
static int64_t s_next_ms, s_deadline_ms;

void ha_evcc_vehicle_names_request(void)
{
    LOCK(); s_wanted = true; UNLOCK();
}
bool ha_evcc_vehicle_name(const char *id, char *out, size_t capacity)
{
    if (!id || !out || !capacity) return false;
    bool found = false;
    LOCK();
    if (s_names) for (size_t i = 0; i < s_names->count; i++) if (!strcmp(s_names->item[i].id, id)) {
        snprintf(out, capacity, "%s", s_names->item[i].name); found = true; break;
    }
    UNLOCK(); return found;
}
bool ha_evcc_vehicle_names_is_due(int64_t now_ms)
{
    LOCK();
    if (s_token && now_ms >= s_deadline_ms) s_token = 0;
    bool due = s_wanted && !s_token && now_ms >= s_next_ms;
    UNLOCK(); return due;
}
cJSON *ha_evcc_vehicle_names_next_request(uint32_t token, int64_t now_ms)
{
    if (!token || !ha_evcc_vehicle_names_is_due(now_ms)) return NULL;
    cJSON *cmd = cJSON_CreateObject();
    if (!cmd) return NULL;
    cJSON_AddStringToObject(cmd, "type", "frontend/get_translations");
    cJSON_AddStringToObject(cmd, "language", "en");
    cJSON_AddStringToObject(cmd, "category", "entity");
    cJSON_AddStringToObject(cmd, "integration", "evcc_intg");
    LOCK(); s_token = token; s_deadline_ms = now_ms + 20000; s_next_ms = now_ms + 300000; UNLOCK();
    return cmd;
}
void ha_evcc_vehicle_names_result(uint32_t token, bool success, const cJSON *result)
{
    LOCK(); bool valid = token && token == s_token; UNLOCK();
    if (!valid) return;
    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(result, "resources");
    names_t *fresh = success && cJSON_IsObject(resources) ? ALLOC() : NULL;
    if (fresh) {
        static const char prefix[] = "component.evcc_intg.entity.select.vehiclename.state.";
        const cJSON *entry;
        cJSON_ArrayForEach(entry, resources) {
            const char *name = cJSON_GetStringValue(entry);
            if (!entry->string || strncmp(entry->string, prefix, sizeof(prefix)-1) || !name || !*name || strlen(name) >= APP_MAX_NAME_LEN) continue;
            const char *id = entry->string + sizeof(prefix)-1;
            if (!*id || !strcmp(id, "null") || strlen(id) >= HA_SELECT_OPTION_LEN || fresh->count == HA_SELECT_MAX_OPTIONS) continue;
            /* Ignore the integration's untranslated placeholder slots; they must
             * not crowd out the configured ev1/ev4 entries later in the object. */
            if (!strncmp(id, "vehicle_", 8) && !strncmp(name, "Vehicle ", 8)) continue;
            strcpy(fresh->item[fresh->count].id, id); strcpy(fresh->item[fresh->count++].name, name);
        }
    }
    LOCK();
    if (token == s_token) {
        s_token = 0;
        if (fresh) { names_t *old = s_names; s_names = fresh; fresh = old; }
    }
    UNLOCK(); free(fresh);
}
void ha_evcc_vehicle_names_cancel(void)
{
    LOCK(); s_token = 0; s_next_ms = 0; UNLOCK();
}
void ha_evcc_vehicle_names_reply(bool success, cJSON *result, void *user)
{
    ha_evcc_vehicle_names_result((uint32_t)(uintptr_t)user, success, result);
}
