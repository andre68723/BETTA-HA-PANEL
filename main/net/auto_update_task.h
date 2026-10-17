/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The optional automatic firmware update ("Install updates automatically", off by default). Once a minute a task asks the pure decision
 * (util/auto_update.c) whether the panel may install the release the update check found, and if so starts the same install as the
 * editor's update button. It remembers the attempt in NVS (util/auto_update_state.c) so that after the restart it knows whether the
 * panel came up on the new firmware or the bootloader returned to the old one; a rolled-back version is not tried again and the panel
 * shows a notice.
 */
#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "sdkconfig.h"

/* Once after boot, after the reboot log and the update check: resolves the attempt a restart interrupted and starts the task. */
void auto_update_task_start(bool enabled);
/* The setting changed without a restart. */
void auto_update_task_configure(bool enabled);
#if CONFIG_APP_AUTO_UPDATE_TEST_HOOK
/* Test builds only. `force` (when >= 0): 1 treats the panel as quiet and inside its window at its start minute and ignores "tried today" for
 * the next install, 0 switches that off. `reset`: forget the stored state. `attempt` (not NULL): store an install attempt of that tag as if
 * the task had just started one, for rollback tests. Wakes the task. */
void auto_update_test_hook(int force, bool reset, const char *attempt);
#endif
/* Adds the "auto_update" object to /api/ota/status: the setting, why the panel waits (or "go"), the last result and the blocked version. */
void auto_update_add_status_json(cJSON *root);
