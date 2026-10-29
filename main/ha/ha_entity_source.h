/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

/* Discovery-only metadata: no full state/forecast payloads in the result cache. */
typedef struct {
    uint32_t flags;
    uint32_t features;
    bool features_known;
    bool dimming;
} ha_entity_search_meta_t;
char *ha_entity_source_template(const char *domain, const char *search_literal, unsigned offset, unsigned page_size);
void ha_entity_search_parse(const cJSON *entry, ha_entity_search_meta_t *out);
void ha_entity_search_json(cJSON *entry, const ha_entity_search_meta_t *meta);

#define HA_DISCOVERY_SOURCE_SETUP \
    "{%% set quotes = integration_entities('easy_stock') %%}" \
    "{%% set sonos = integration_entities('sonos') %%}" \
    "{%% set printers = integration_entities('bambu_lab') %%}"

/* Keep 2026.10 display names while the legacy attribute exists. Without it, compose the
 * registry entity/device names (entity_name is available since HA 2026.4). A primary
 * entity may have an empty entity name; never duplicate an existing device prefix. */
#define HA_DISCOVERY_DISPLAY_NAME \
    "{%% set name = a.get('friendly_name') or '' %%}" \
    "{%% if not name %%}" \
    "{%% set name = entity_name(s.entity_id) or '' %%}" \
    "{%% set dn = device_name(s.entity_id) or '' %%}" \
    "{%% if dn and name != dn and not name.startswith(dn ~ ' ') %%}" \
    "{%% set name = (dn ~ ' ' ~ name)|trim %%}" \
    "{%% endif %%}{%% endif %%}" \
    "{%% set name = name or s.entity_id %%}"
/* Small source metadata for discovery; never transfer complete forecast lists.
 * `a` is the state's attributes. Full price parsing remains the firmware's
 * authority when the chosen entity is subscribed. */
#define HA_DISCOVERY_SOURCE_FIELDS \
    ", u=(a.get('unit_of_measurement','')), sc=(a.get('state_class','')), " \
    "ps=(((a.get('data') or [])|count > 1 and a.data[0] is mapping and " \
    "('price_per_kwh' in a.data[0] or 'da_cons' in a.data[0]) and " \
    "('start_time' in a.data[0] or 'time_ts' in a.data[0])) or " \
    "(((a.get('raw_today') or [])|count + (a.get('raw_tomorrow') or [])|count) > 1 and " \
    "a.get('currency','EUR') == 'EUR' and a.get('unit_of_measurement','') in ['EUR/kWh','€/kWh','EUR/MWh','€/MWh','EUR/Wh','€/Wh'," \
    "'c/kWh','ct/kWh','¢/kWh','c/MWh','ct/MWh','¢/MWh','c/Wh','ct/Wh','¢/Wh'])), " \
    "sf=(a.get('detailedForecast') is sequence and a.get('detailedForecast') is not string), " \
    "xf=((1 if is_number(s.state) else 0) + " \
    "(2 if a.get('data') is string and a.data|length > 400 and 'Wetter' in a.data else 0) + " \
    "(4 if a.get('items') is mapping else 0) + (8 if s.entity_id in quotes else 0) + " \
    "(16 if s.entity_id in sonos else 0) + (32 if s.entity_id in printers else 0) + " \
    "(64 if a.get('current_temperature') is number else 0) + " \
    "(128 if s.entity_id in printers and 'running' in (a.get('options') or []) and 'pause' in a.options else 0)), " \
    "ft=a.get('supported_features'), dm=((a.supported_color_modes|reject('in',['onoff','unknown'])|list|count > 0) " \
    "if a.get('supported_color_modes') is sequence and a.supported_color_modes is not string else " \
    "((a.get('color_mode','') not in ['','onoff','unknown']) or " \
    "((a.get('supported_features',0)|bitwise_and(209)) > 0) or a.get('brightness') is number or a.get('brightness_pct') is number))"
