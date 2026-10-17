/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/auto_update_state.h"

#include <stdio.h>
#include <string.h>

static void copy_tag(char out[AUTO_UPDATE_TAG_LEN], const char *tag)
{
    snprintf(out, AUTO_UPDATE_TAG_LEN, "%s", tag != NULL ? tag : "");
}

void auto_update_state_init(auto_update_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->version = AUTO_UPDATE_STATE_VERSION;
}

void auto_update_state_begin(auto_update_state_t *state, const char *tag, int64_t epoch, int32_t day)
{
    copy_tag(state->attempt_tag, tag);
    state->attempt_epoch = epoch;
    state->last_attempt_day = day;
}

void auto_update_state_failed(auto_update_state_t *state, int64_t epoch)
{
    copy_tag(state->result_tag, state->attempt_tag);
    state->attempt_tag[0] = '\0';
    state->result = AUTO_UPDATE_RESULT_FAILED;
    state->result_epoch = epoch;
}

bool auto_update_state_resolve_boot(auto_update_state_t *state, const char *running_version, bool previous_run_failed, int64_t epoch)
{
    if (state->attempt_tag[0] == '\0') {
        return false;
    }
    copy_tag(state->result_tag, state->attempt_tag);
    state->result_epoch = epoch;
    if (running_version != NULL && strcmp(running_version, state->attempt_tag) == 0) {
        state->result = AUTO_UPDATE_RESULT_INSTALLED;
    } else if (previous_run_failed) {
        state->result = AUTO_UPDATE_RESULT_ROLLED_BACK;
        copy_tag(state->blocked_tag, state->attempt_tag);
        state->notice_pending = 1;
    } else {
        state->result = AUTO_UPDATE_RESULT_FAILED;
    }
    state->attempt_tag[0] = '\0';
    return true;
}

void auto_update_state_ack_notice(auto_update_state_t *state)
{
    state->notice_pending = 0;
}

const char *auto_update_result_name(auto_update_result_t result)
{
    switch (result) {
    case AUTO_UPDATE_RESULT_INSTALLED: return "installed";
    case AUTO_UPDATE_RESULT_ROLLED_BACK: return "rolled_back";
    case AUTO_UPDATE_RESULT_FAILED: return "failed";
    default: return "";
    }
}
