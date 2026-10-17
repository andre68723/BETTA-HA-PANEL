/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "cJSON.h"

/* Starts the watchdog task (util/ui_watchdog.h). Call after the UI runtime is up. */
void ui_watchdog_start(void);
/* The ui_runtime loop ran (any task may call it; it only stores a timestamp). */
void ui_watchdog_beat_runtime(void);
/* Creates the LVGL timer that proves the LVGL task still runs. Call with the display lock held. */
void ui_watchdog_attach_lvgl(void);
/* Adds the "ui_watchdog" object to /api/version. */
void ui_watchdog_add_json(cJSON *root);
