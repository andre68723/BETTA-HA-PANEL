/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "esp_err.h"
typedef enum { API_OTA_PHASE_IDLE, API_OTA_PHASE_FAILED, API_OTA_PHASE_RUNNING, API_OTA_PHASE_REBOOTING } api_ota_phase_t;
api_ota_phase_t api_ota_phase(void);
esp_err_t api_ota_start_url(const char *url);
