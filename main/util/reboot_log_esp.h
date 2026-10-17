/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The ESP side of the reboot history (util/reboot_log.h): the RTC breadcrumb, the NVS ring and the refresh timer.
 */
#pragma once

#include "cJSON.h"
#include "util/reboot_log.h"

/* Once per boot, early in app_main after NVS is up: turns the previous run's breadcrumb and this boot's reset reason into a
 * history entry and starts the breadcrumb for this run. */
void reboot_log_init(void);
/* Call just before a deliberate esp_restart() (any task): the next boot reports why. */
void reboot_log_mark(reboot_cause_t cause);
/* Adds "boot_seq" and "entries" (newest first) to `root` (GET /api/reboot-log). */
void reboot_log_add_json(cJSON *root);
/* Label of the newest history entry (how the previous run ended), "" before the log is initialised. */
const char *reboot_log_last_label(void);
/* The deliberate-restart cause of the previous run (a reboot_cause_t), REBOOT_CAUSE_NONE after a crash, a power cut or an
 * untagged restart, and before the log is initialised. */
reboot_cause_t reboot_log_last_cause(void);
/* Whether the previous run ended in a failure (reboot_entry_is_failure); false before the log is initialised. */
bool reboot_log_last_run_failed(void);
/* Latest retained panic breadcrumb, for importing a raw dump which has no timestamp of its own. */
bool reboot_log_latest_panic(reboot_entry_t *out);
