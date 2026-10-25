/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Never get the panel banned by Home Assistant: HA's http.ban counts every rejected token as a
 * failed login and bans the address after a few. The first rejection from any path (the
 * WebSocket's auth_invalid, an HTTP 401 or 403, a WebSocket handshake 401/403) blocks every
 * further request that carries the token until the panel restarts, which is also how a changed
 * token or address in Settings takes effect. There is no timed retry: each would be one more
 * failed login.
 *
 * One exception: a 401 on POST /api/template while the WebSocket is authenticated means the
 * token works but its user is no administrator. That only turns the template entity discovery
 * off; the rest of the client keeps running.
 *
 * Pure C with no locking: each state is a single word written once per transition, and a reader
 * that sees it a moment late only lets one more request through.
 */
#pragma once

#include <stdbool.h>

typedef enum {
    HA_AUTH_OK = 0,
    HA_AUTH_TOKEN_REJECTED, /* auth_invalid or HTTP 401: wrong, expired or revoked token */
    HA_AUTH_FORBIDDEN,      /* HTTP 403: most likely an IP ban in Home Assistant */
} ha_auth_block_t;

/* Back to the start state (boot, and host tests). */
void ha_auth_state_reset(void);
/* The WebSocket's auth_invalid message. True when this blocks the client now (log it once). */
bool ha_auth_state_report_ws_auth_invalid(void);
/* An HTTP status from Home Assistant, including a WebSocket handshake's; other statuses than
 * 401/403 are ignored. `template_request` with `ws_authenticated` marks the not-admin case.
 * True when this changes the state (log it once). */
bool ha_auth_state_report_http(int status, bool template_request, bool ws_authenticated);
/* The status of an image fetch. Only a request that carried the panel's token can tell it was rejected: a
 * media URL with its own ?token= (a camera_proxy URL expires every few minutes, and after a Home Assistant
 * restart) or a foreign host answers 401/403 about that URL, not about the panel. False for those. */
bool ha_auth_state_report_image_fetch(int status, bool panel_token_sent);
/* True while no request carrying the token may be sent. */
bool ha_auth_state_blocked(void);
ha_auth_block_t ha_auth_state_reason(void);
/* True when the template entity discovery must not be requested (blocked, or not admin). */
bool ha_auth_state_template_disabled(void);
/* Short English id for logs and the device API: "ok", "token_rejected", "forbidden", and
 * "not_admin" while only the template discovery is off. */
const char *ha_auth_state_name(void);
