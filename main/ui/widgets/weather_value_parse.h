/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

bool weather_parse_float_token(const char *text, float *out);
bool weather_parse_int_token(const char *text, int *out);
bool weather_json_item_to_float(cJSON *item, float *out);
bool weather_json_item_to_int(cJSON *item, int *out);
bool weather_extract_raw_number_attr(const char *json, const char *key, float *out);
bool weather_extract_raw_int_attr(const char *json, const char *key, int *out);
bool weather_extract_raw_string_attr(const char *json, const char *key, char *out, size_t out_size);
