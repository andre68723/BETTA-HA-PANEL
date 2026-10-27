/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_layout_entities.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "layout/bambu_printer_slots.h"
#include "layout/evcc_slots.h"

bool ha_layout_entities_contains(const char *entity_ids, size_t count, const char *entity_id)
{
    if (entity_ids == NULL || entity_id == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const char *entry = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        if (strncmp(entry, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

void ha_layout_entities_add(ha_layout_entity_set_t *set, const char *entity_id)
{
    if (set == NULL || set->ids == NULL || entity_id == NULL || entity_id[0] == '\0') {
        return;
    }
    if (ha_layout_entities_contains(set->ids, set->count, entity_id)) {
        return;
    }
    if (set->count >= set->capacity) {
        if (set->on_overflow != NULL) {
            set->on_overflow(set->capacity, entity_id);
        }
        return;
    }
    char *dst = set->ids + (set->count * APP_MAX_ENTITY_ID_LEN);
    size_t len = strnlen(entity_id, APP_MAX_ENTITY_ID_LEN - 1U);
    memcpy(dst, entity_id, len);
    dst[len] = '\0';
    set->count++;
}

static void ha_layout_collect_field(cJSON *obj, const char *key, ha_layout_entity_set_t *set)
{
    if (obj == NULL || key == NULL) {
        return;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        ha_layout_entities_add(set, item->valuestring);
    }
}

static bool ha_layout_energy_has_manual_entities(cJSON *energy)
{
    if (!cJSON_IsObject(energy)) {
        return false;
    }
    static const char *energy_keys[] = {
        "home_power_entity_id",
        "solar_power_entity_id",
        "grid_power_entity_id",
        "grid_import_power_entity_id",
        "grid_export_power_entity_id",
        "battery_power_entity_id",
        "battery_charge_power_entity_id",
        "battery_discharge_power_entity_id",
        "battery_soc_entity_id",
        "gas_entity_id",
        "water_entity_id",
    };
    for (size_t i = 0; i < (sizeof(energy_keys) / sizeof(energy_keys[0])); i++) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(energy, energy_keys[i]);
        if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
            return true;
        }
    }
    return false;
}

static bool ha_layout_energy_page_uses_manual_live(cJSON *energy)
{
    if (!cJSON_IsObject(energy)) {
        return false;
    }
    cJSON *source = cJSON_GetObjectItemCaseSensitive(energy, "source");
    if (cJSON_IsString(source) && source->valuestring != NULL && source->valuestring[0] != '\0') {
        return strcmp(source->valuestring, "manual_live") == 0;
    }
    return ha_layout_energy_has_manual_entities(energy);
}

void ha_layout_entities_collect(cJSON *root, ha_layout_entity_set_t *set)
{
    if (set == NULL) {
        return;
    }
    set->count = 0;
    set->need_weather_forecast = false;
    if (set->ids == NULL || set->capacity == 0) {
        return;
    }
    {
        /* Alert entities (a doorbell): the panel has to see their changes even without a tile. */
        cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts");
        cJSON *alert = NULL;
        cJSON_ArrayForEach(alert, alerts) {
            ha_layout_collect_field(alert, "entity_id", set);
            ha_layout_collect_field(alert, "camera_entity_id", set);
        }
    }
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (cJSON_IsArray(pages)) {
        int page_count = cJSON_GetArraySize(pages);
        for (int p = 0; p < page_count; p++) {
            cJSON *page = cJSON_GetArrayItem(pages, p);
            cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
            if (cJSON_IsString(page_type) && page_type->valuestring != NULL &&
                strcmp(page_type->valuestring, "house_dashboard") == 0) {
                /* House page: the rooms carry the entities (a main sensor plus room_entities). */
                cJSON *house = cJSON_GetObjectItemCaseSensitive(page, "house");
                cJSON *rooms = cJSON_IsObject(house) ? cJSON_GetObjectItemCaseSensitive(house, "rooms") : NULL;
                cJSON *room = NULL;
                cJSON_ArrayForEach(room, rooms) {
                    ha_layout_collect_field(room, "entity_id", set);
                    cJSON *extras = cJSON_GetObjectItemCaseSensitive(room, "room_entities");
                    cJSON *extra = NULL;
                    cJSON_ArrayForEach(extra, extras) {
                        if (cJSON_IsString(extra) && extra->valuestring != NULL) {
                            ha_layout_entities_add(set, extra->valuestring);
                        }
                    }
                }
                continue;
            }
            if (cJSON_IsString(page_type) && page_type->valuestring != NULL &&
                strcmp(page_type->valuestring, "energy_dashboard") == 0) {
                cJSON *energy = cJSON_GetObjectItemCaseSensitive(page, "energy");
                static const char *energy_keys[] = {
                    "home_power_entity_id",
                    "solar_power_entity_id",
                    "grid_power_entity_id",
                    "grid_import_power_entity_id",
                    "grid_export_power_entity_id",
                    "battery_power_entity_id",
                    "battery_charge_power_entity_id",
                    "battery_discharge_power_entity_id",
                    "battery_soc_entity_id",
                    "gas_entity_id",
                    "water_entity_id",
                };
                if (cJSON_IsObject(energy)) {
                    if (ha_layout_energy_page_uses_manual_live(energy)) {
                        for (size_t i = 0; i < (sizeof(energy_keys) / sizeof(energy_keys[0])); i++) {
                            ha_layout_collect_field(energy, energy_keys[i], set);
                        }
                    } else {
                        /* Home Assistant energy source: battery state of charge and live solar power are shown next to the nodes. */
                        ha_layout_collect_field(energy, "battery_soc_entity_id", set);
                        ha_layout_collect_field(energy, "solar_power_entity_id", set);
                    }
                }
                continue;
            }

            cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
            if (!cJSON_IsArray(widgets)) {
                continue;
            }

            int widget_count = cJSON_GetArraySize(widgets);
            for (int w = 0; w < widget_count; w++) {
                cJSON *widget = cJSON_GetArrayItem(widgets, w);
                if (!cJSON_IsObject(widget)) {
                    continue;
                }
                cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
                if (cJSON_IsString(type) && type->valuestring != NULL &&
                    (strcmp(type->valuestring, "weather_3day") == 0 ||
                     strcmp(type->valuestring, "weather_tile") == 0)) {
                    set->need_weather_forecast = true;
                }
                ha_layout_collect_field(widget, "entity_id", set);
                ha_layout_collect_field(widget, "secondary_entity_id", set);
                ha_layout_collect_field(widget, "tertiary_entity_id", set);
                ha_layout_collect_field(widget, "forecast_today_entity_id", set);
                ha_layout_collect_field(widget, "forecast_tomorrow_entity_id", set);
                ha_layout_collect_field(widget, "forecast_day_3_entity_id", set);
                ha_layout_collect_field(widget, "forecast_day_4_entity_id", set);
                ha_layout_collect_field(widget, "forecast_day_5_entity_id", set);
                ha_layout_collect_field(widget, "forecast_day_6_entity_id", set);
                ha_layout_collect_field(widget, "forecast_day_7_entity_id", set);
                ha_layout_collect_field(widget, "precond_entity_id", set);
                ha_layout_collect_field(widget, "battery_soc_entity_id", set); /* energy timeline: live state of charge */
                ha_layout_collect_field(widget, "lock_entity_id", set);
                ha_layout_collect_field(widget, "interior_temp_entity_id", set);
                ha_layout_collect_field(widget, "battery_entity_id", set); /* vacuum tile: optional charge sensor */
                ha_layout_collect_field(widget, "dividends_entity_id", set); /* portfolio tile: optional dividends sensor */
                ha_layout_collect_field(widget, "return_entity_id", set); /* portfolio tile: optional unrealised return sensor */
                if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "bambu_printer") == 0) {
                    for (size_t b = 0; b < k_bambu_printer_binding_count; b++) {
                        ha_layout_collect_field(widget, k_bambu_printer_bindings[b].key, set);
                    }
                }
                if (cJSON_IsString(type) && !strcmp(type->valuestring, "wallbox_evcc")) {
                    for (size_t b = 0; b < EVCC_ROLE_COUNT; b++) ha_layout_collect_field(widget, k_evcc_bindings[b].key, set);
                    for (size_t b = 0; b < EVCC_SITE_COUNT; b++) ha_layout_collect_field(widget, k_evcc_site_bindings[b].key, set);
                }
                /* the room card's list and the Stocks & ETFs quotes after the first */
                static const char *const lists[] = {"room_entities", "stock_entities", "sensor_entities"};
                for (size_t l = 0; l < sizeof(lists) / sizeof(lists[0]); l++) {
                    cJSON *list_item = NULL;
                    cJSON_ArrayForEach(list_item, cJSON_GetObjectItemCaseSensitive(widget, lists[l])) {
                        if (cJSON_IsString(list_item) && list_item->valuestring != NULL) {
                            ha_layout_entities_add(set, list_item->valuestring);
                        }
                    }
                }
            }
        }
    }
}

bool ha_layout_needs_ha_energy(cJSON *root)
{
    bool needs_ha_energy = false;
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (cJSON_IsArray(pages)) {
        int page_count = cJSON_GetArraySize(pages);
        for (int p = 0; p < page_count; p++) {
            cJSON *page = cJSON_GetArrayItem(pages, p);
            cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
            if (!cJSON_IsString(page_type) || page_type->valuestring == NULL ||
                strcmp(page_type->valuestring, "energy_dashboard") != 0) {
                continue;
            }

            cJSON *energy = cJSON_GetObjectItemCaseSensitive(page, "energy");
            if (!ha_layout_energy_page_uses_manual_live(energy)) {
                needs_ha_energy = true;
                break;
            }
        }
    }
    return needs_ha_energy;
}

static int ha_layout_entity_id_sort_cmp(const void *lhs, const void *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }
    return strncmp((const char *)lhs, (const char *)rhs, APP_MAX_ENTITY_ID_LEN);
}

uint32_t ha_layout_entities_signature(char *entity_ids, size_t entity_count)
{
    if (entity_ids == NULL || entity_count == 0) {
        return 0;
    }

    qsort(entity_ids, entity_count, APP_MAX_ENTITY_ID_LEN, ha_layout_entity_id_sort_cmp);

    uint32_t hash = 2166136261u; /* FNV-1a 32-bit */
    for (size_t i = 0; i < entity_count; i++) {
        const char *entry = entity_ids + (i * APP_MAX_ENTITY_ID_LEN);
        for (size_t j = 0; j < APP_MAX_ENTITY_ID_LEN; j++) {
            uint8_t ch = (uint8_t)entry[j];
            if (ch == '\0') {
                break;
            }
            hash ^= ch;
            hash *= 16777619u;
        }
        hash ^= 0xFFu; /* delimiter to avoid concatenation ambiguity */
        hash *= 16777619u;
    }
    return hash;
}
