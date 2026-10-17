/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * What the automatic update remembers across the restart it causes: which version it installed, whether the panel then
 * came up on it or fell back to the previous firmware, and which version must not be tried again. Pure (host tested); the
 * NVS blob and the scheduler are in net/auto_update_task.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUTO_UPDATE_TAG_LEN 32

typedef enum {
    AUTO_UPDATE_RESULT_NONE = 0,
    AUTO_UPDATE_RESULT_INSTALLED,   /* the panel came up on the version it installed */
    AUTO_UPDATE_RESULT_ROLLED_BACK, /* the new image failed and the bootloader returned to the previous one */
    AUTO_UPDATE_RESULT_FAILED       /* the install did not finish (download or write error, power cut, ...) */
} auto_update_result_t;

typedef struct {
    uint32_t version; /* layout of this struct, AUTO_UPDATE_STATE_VERSION */
    char attempt_tag[AUTO_UPDATE_TAG_LEN]; /* installing this release; empty when no install is under way */
    int64_t attempt_epoch;
    int32_t last_attempt_day;               /* local day number of the last attempt, at most one per day */
    char blocked_tag[AUTO_UPDATE_TAG_LEN];  /* rolled back; never tried again */
    uint8_t result;                         /* auto_update_result_t of the last attempt */
    char result_tag[AUTO_UPDATE_TAG_LEN];
    int64_t result_epoch;
    uint8_t notice_pending;                 /* the rollback has not been shown on the panel yet */
} auto_update_state_t;

#define AUTO_UPDATE_STATE_VERSION 1u

void auto_update_state_init(auto_update_state_t *state);
/* An install of `tag` starts now (`day` is the local day number, `epoch` the wall time or 0 when unknown). */
void auto_update_state_begin(auto_update_state_t *state, const char *tag, int64_t epoch, int32_t day);
/* The install failed before the restart: no rollback is involved, the version may be tried again tomorrow. */
void auto_update_state_failed(auto_update_state_t *state, int64_t epoch);
/* After a restart: decide what the attempt came to. `previous_run_failed` is reboot_entry_is_failure of the run that ended,
 * `running_version` the firmware that is running now. True when the state changed (save it). No attempt: nothing happens.
 * - running the attempted version: installed;
 * - a different version after a failure: rolled back, the version is blocked and the notice is pending;
 * - a different version without a failure (a power cut during the install): failed, not blocked. */
bool auto_update_state_resolve_boot(auto_update_state_t *state, const char *running_version, bool previous_run_failed, int64_t epoch);
/* The rollback notice was shown on the panel. */
void auto_update_state_ack_notice(auto_update_state_t *state);

const char *auto_update_result_name(auto_update_result_t result);
