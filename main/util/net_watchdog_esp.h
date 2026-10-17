/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "cJSON.h"

/* Starts the watchdog task (util/net_watchdog.h) at the end of app_main. `ha_expected`: Home Assistant is configured. */
void net_watchdog_start(bool wifi_configured, bool ha_expected);
/* Adds the "net_watchdog" object to /api/version. */
void net_watchdog_add_json(cJSON *root);
