/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ha/ha_history_reduce.h"

/* Completion runs on the LVGL task. The callback owns and frees snapshot. A generation ID
 * lets a closed popup ignore an older request without retaining a widget pointer. */
typedef void (*ha_history_cb_t)(uint32_t generation, ha_history_snapshot_t *snapshot, esp_err_t status);

esp_err_t ha_history_request(const char *entity_id, bool numeric, uint32_t generation, ha_history_cb_t callback);
/* Quote view only: local midnight to now, including 23/25-hour days. */
esp_err_t ha_history_request_today(const char *entity_id, uint32_t generation, ha_history_cb_t callback);
void ha_history_snapshot_free(ha_history_snapshot_t *snapshot);
