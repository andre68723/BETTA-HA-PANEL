/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_entity_source.h"
#include "ha/ha_discovery_domains.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

char *ha_entity_source_template(const char *domain, const char *search_literal, unsigned offset, unsigned page_size)
{
    const size_t capacity = 4096;
#ifdef ESP_PLATFORM
    char *template_text = heap_caps_calloc(1, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    char *template_text = calloc(1, capacity);
#endif
    if (template_text == NULL) return NULL;
    int template_len = 0;
    char domains_jinja[384] = "[]"; /* the "any" and "room" domain lists, from ha_discovery_domains.c */
    (void)ha_discovery_domains_jinja(domain, domains_jinja, sizeof(domains_jinja));
    if (strcmp(domain, "any") == 0) {
        /* Add from an entity: every entity a tile can show, matched on id, name and area, with its
         * device class so the editor can suggest the right tile. */
        template_len = snprintf(template_text, capacity,
            "{%% set offset = %u %%}"
            "{%% set limit = %u %%}"
            "{%% set q = (%s|lower) %%}"
            "{%% set doms = %s %%}"
            HA_DISCOVERY_SOURCE_SETUP
            "{%% set ns = namespace(items=[], total=0) %%}"
            "{%% for s in states|sort(attribute='entity_id') %%}"
            "{%% set a = s.attributes %%}"
            "{%% if s.domain in doms %%}"
            HA_DISCOVERY_DISPLAY_NAME
            "{%% set room = area_name(s.entity_id) or '' %%}"
            "{%% set hay = (s.entity_id ~ ' ' ~ name ~ ' ' ~ room)|lower %%}"
            "{%% if q == '' or q in hay %%}"
            "{%% set idx = ns.total %%}"
            "{%% set ns.total = ns.total + 1 %%}"
            "{%% if idx >= offset and idx < offset + limit %%}"
            "{%% set ns.items = ns.items + [dict(id=s.entity_id, name=name, room=room, "
            "dc=(s.attributes.device_class|default('', true))" HA_DISCOVERY_SOURCE_FIELDS ")] %%}"
            "{%% endif %%}"
            "{%% endif %%}"
            "{%% endif %%}"
            "{%% endfor %%}"
            "{{ {'total': ns.total, 'items': ns.items}|to_json }}",
            (unsigned)offset, (unsigned)page_size, search_literal, domains_jinja);
    } else if (strcmp(domain, "room") == 0) {
        /* Room cards (and the editor's per-room widget import): everything that belongs to an
         * area and can be shown for a room (temperature and humidity sensors, window/door/motion
         * sensors, lights, covers, fans, climate, media players, locks, switches, vacuums), with
         * its device class. */
        template_len = snprintf(template_text, capacity,
            "{%% set offset = %u %%}"
            "{%% set limit = %u %%}"
            "{%% set q = (%s|lower) %%}"
            "{%% set sc = ['temperature','humidity','battery'] %%}"
            "{%% set bc = ['window','door','opening','garage_door','motion','occupancy','presence','moving','battery'] %%}"
            HA_DISCOVERY_SOURCE_SETUP
            "{%% set ns = namespace(items=[], total=0) %%}"
            "{%% for s in states|sort(attribute='entity_id') %%}"
            "{%% set a = s.attributes %%}"
            "{%% set d = s.domain %%}"
            "{%% set dc = s.attributes.device_class|default('', true) %%}"
            "{%% if (d == 'sensor' and dc in sc) or (d == 'binary_sensor' and dc in bc) or (d not in ['sensor','binary_sensor'] and d in %s) %%}"
            HA_DISCOVERY_DISPLAY_NAME
            "{%% set room = area_name(s.entity_id) or '' %%}"
            "{%% if room != '' and (q == '' or q in room|lower) %%}"
            "{%% set idx = ns.total %%}"
            "{%% set ns.total = ns.total + 1 %%}"
            "{%% if idx >= offset and idx < offset + limit %%}"
            "{%% set ns.items = ns.items + [dict(id=s.entity_id, name=name, room=room, dc=dc, fl=(floor_name(s.entity_id) or '')" HA_DISCOVERY_SOURCE_FIELDS ")] %%}"
            "{%% endif %%}"
            "{%% endif %%}"
            "{%% endif %%}"
            "{%% endfor %%}"
            "{{ {'total': ns.total, 'items': ns.items}|to_json }}",
            (unsigned)offset, (unsigned)page_size, search_literal, domains_jinja);
    } else {
        template_len = snprintf(template_text, capacity,
        "{%% set offset = %u %%}"
        "{%% set limit = %u %%}"
        "{%% set q = (%s|lower) %%}"
        HA_DISCOVERY_SOURCE_SETUP
        "{%% set ns = namespace(items=[], total=0) %%}"
        "{%% for s in states.%s|sort(attribute='entity_id') %%}"
        "{%% set a = s.attributes %%}"
        HA_DISCOVERY_DISPLAY_NAME
        "{%% set room = area_name(s.entity_id) or '' %%}"
        "{%% set hay = (s.entity_id ~ ' ' ~ name ~ ' ' ~ room)|lower %%}"
        "{%% if q == '' or q in hay %%}"
        "{%% set idx = ns.total %%}"
        "{%% set ns.total = ns.total + 1 %%}"
        "{%% if idx >= offset and idx < offset + limit %%}"
        "{%% set ns.items = ns.items + [dict(id=s.entity_id, name=name, room=room, icon=(s.attributes.icon|default('', true)), "
        "dc=(s.attributes.device_class|default('', true))" HA_DISCOVERY_SOURCE_FIELDS ")] %%}"
        "{%% endif %%}"
        "{%% endif %%}"
        "{%% endfor %%}"
        "{{ {'total': ns.total, 'items': ns.items}|to_json }}",
        (unsigned)offset, (unsigned)page_size, search_literal, domain);
    }
    if (template_len <= 0 || (size_t)template_len >= capacity) {
        free(template_text);
        return NULL;
    }
    return template_text;
}

void ha_entity_search_parse(const cJSON *entry, ha_entity_search_meta_t *out)
{
    const cJSON *flags = cJSON_GetObjectItemCaseSensitive(entry, "xf");
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(entry, "ft");
    out->flags = cJSON_IsNumber(flags) && flags->valuedouble >= 0 && flags->valuedouble <= UINT32_MAX ? (uint32_t)flags->valuedouble : 0;
    out->features_known = cJSON_IsNumber(features) && features->valuedouble >= 0 && features->valuedouble <= UINT32_MAX;
    out->features = out->features_known ? (uint32_t)features->valuedouble : 0;
    out->dimming = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "dm"));
}

void ha_entity_search_json(cJSON *entry, const ha_entity_search_meta_t *meta)
{
    cJSON_AddNumberToObject(entry, "search_flags", meta->flags);
    if (meta->features_known) cJSON_AddNumberToObject(entry, "supported_features", meta->features);
    cJSON *capabilities = cJSON_AddObjectToObject(entry, "capabilities");
    if (capabilities != NULL) cJSON_AddBoolToObject(capabilities, "dimming", meta->dimming);
}
