/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_auth_state.h"

static volatile ha_auth_block_t s_block = HA_AUTH_OK;
static volatile bool s_not_admin = false;
static bool s_retry_requested;
static int64_t s_retry_after_ms;

void ha_auth_state_reset(void)
{
    s_block = HA_AUTH_OK;
    s_not_admin = false;
    s_retry_requested = false;
    s_retry_after_ms = 0;
}

bool ha_auth_state_retry_available(int64_t now_ms)
{
    return !s_retry_requested && now_ms >= s_retry_after_ms;
}

bool ha_auth_state_request_retry(int64_t now_ms)
{
    if (!ha_auth_state_retry_available(now_ms)) return false;
    s_retry_after_ms = now_ms + 15000;
    s_retry_requested = true;
    return true;
}

bool ha_auth_state_take_retry(void)
{
    bool requested = s_retry_requested;
    s_retry_requested = false;
    return requested;
}

void ha_auth_state_begin_retry(void)
{
    s_block = HA_AUTH_OK;
    s_not_admin = false;
}

static bool block(ha_auth_block_t reason)
{
    if (s_block != HA_AUTH_OK) {
        return false; /* the first reason stays */
    }
    s_block = reason;
    return true;
}

bool ha_auth_state_report_ws_auth_invalid(void)
{
    return block(HA_AUTH_TOKEN_REJECTED);
}

bool ha_auth_state_report_http(int status, bool template_request, bool ws_authenticated)
{
    if (status == 401 && template_request && ws_authenticated) {
        const bool changed = !s_not_admin;
        s_not_admin = true;
        return changed;
    }
    if (status == 401) {
        return block(HA_AUTH_TOKEN_REJECTED);
    }
    if (status == 403) {
        return block(HA_AUTH_FORBIDDEN);
    }
    return false;
}

bool ha_auth_state_report_image_fetch(int status, bool panel_token_sent)
{
    return panel_token_sent && ha_auth_state_report_http(status, false, true);
}

bool ha_auth_state_blocked(void)
{
    return s_block != HA_AUTH_OK;
}

ha_auth_block_t ha_auth_state_reason(void)
{
    return s_block;
}

bool ha_auth_state_template_disabled(void)
{
    return s_block != HA_AUTH_OK || s_not_admin;
}

const char *ha_auth_state_name(void)
{
    switch (s_block) {
    case HA_AUTH_TOKEN_REJECTED: return "token_rejected";
    case HA_AUTH_FORBIDDEN: return "forbidden";
    case HA_AUTH_OK: break;
    }
    return s_not_admin ? "not_admin" : "ok";
}
