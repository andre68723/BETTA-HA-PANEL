/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Which settings changes need a restart. Wi-Fi, Home Assistant, MQTT, the NTP server and the device
 * room are read once when their services start at boot; everything else (display, night mode,
 * camera, screen protection, time zone, language, appearance, top bar, update check) the settings
 * API applies at once. PUT /api/settings with "reboot": "auto" restarts only when this says so.
 */
#pragma once

#include <stdbool.h>

#include "settings/runtime_settings.h"

/* True when a setting that only takes effect at boot differs between `before` and `after`. */
bool settings_need_restart(const runtime_settings_t *before, const runtime_settings_t *after);
