/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/* HA's evcc integration translates select option IDs independently of state
 * attributes. Keep that display-only mapping; commands still use raw IDs. */
void ha_evcc_vehicle_names_request(void);
bool ha_evcc_vehicle_name(const char *id, char *out, size_t capacity);
bool ha_evcc_vehicle_names_is_due(int64_t now_ms);
cJSON *ha_evcc_vehicle_names_next_request(uint32_t token, int64_t now_ms);
void ha_evcc_vehicle_names_result(uint32_t token, bool success, const cJSON *result);
void ha_evcc_vehicle_names_reply(bool success, cJSON *result, void *user);
void ha_evcc_vehicle_names_cancel(void);
