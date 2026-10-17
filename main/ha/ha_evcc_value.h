/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "app_config.h"
#include "ha/ha_select_options.h"

bool evcc_number(const char *raw, float *value);
bool evcc_soc(const char *raw, float *value);
bool evcc_select_parse(const char *attributes, ha_select_options_t *options);
bool evcc_number_bounds(const char *attributes, float *min, float *max, float *step);
bool evcc_target_valid(float value, float min, float max, float step);
bool evcc_number_payload(const char *entity, float value, char *out, size_t capacity);
/* Keep complete options or no writable list. Never preserve the large nested vehicle object. */
bool ha_evcc_control_attrs(const cJSON *attributes, bool number, char *out, size_t capacity);
