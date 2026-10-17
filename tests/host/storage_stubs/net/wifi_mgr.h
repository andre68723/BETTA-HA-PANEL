/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
bool wifi_mgr_is_connected(void);
esp_err_t wifi_mgr_try_get_sta_rssi(int8_t *);
