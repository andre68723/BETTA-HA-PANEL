/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Transport-independent compaction of Home Assistant attributes and weather forecasts.
 * The caller owns every returned cJSON value and passes a writable output buffer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

cJSON *ha_client_find_compact_weather_forecast(cJSON *node, const char *entity_id);
cJSON *ha_client_extract_compact_forecast_from_attrs_json(const char *attrs_json);
bool ha_client_weather_attrs_has_forecast_json(const char *attrs_json);
/* Takes ownership of forecast even when the merge fails. */
bool ha_client_append_compact_forecast_to_attrs_json(char *attrs_json, size_t attrs_json_size, cJSON *forecast);

bool ha_client_serialize_weather_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_climate_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_media_player_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_sonos_favorites_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_attrs_is_weather_report(cJSON *attrs);
bool ha_client_serialize_weather_report_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_vacuum_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_light_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_cover_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_fan_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_alarm_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
bool ha_client_serialize_image_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);

/* A price list in `data` (ha_price_series.h) and its compact form {"unit_of_measurement":...,"pf":{...}}. */
bool ha_client_attrs_is_price_series(cJSON *attrs);
bool ha_client_serialize_price_series_compact(cJSON *src_attrs, char *out_json, size_t out_json_size);
