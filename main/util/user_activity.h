/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * When somebody last used the panel: a touch, or a request to the editor/API (not the status polling of /api/version).
 * The automatic update waits until nobody has for a while (util/auto_update.h). Timestamps are seconds since boot.
 */
#pragma once

#include <stdint.h>

/* Record an interaction at `now_s`. Safe from any task. */
void user_activity_note(uint32_t now_s);

/* Seconds since the last recorded interaction; without any since boot, the time since boot. */
int64_t user_activity_idle_s(uint32_t now_s);
