/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/net_watchdog.h"

void net_wd_init(net_wd_state_t *state)
{
    state->link_down_since = -1;
    state->ping_fail_since = -1;
    state->alive_since = -1;
    state->http_fail_since = -1;
}

net_wd_decision_t net_wd_step(net_wd_state_t *st, const net_wd_input_t *in, int64_t now_s)
{
    /* HTTP self-probe: independent of the link, the web server must answer on loopback (also in setup AP mode) */
    if (in->http == 0) {
        if (st->http_fail_since < 0) st->http_fail_since = now_s;
    } else {
        st->http_fail_since = -1;
    }
    const bool http_dead = st->http_fail_since >= 0 && now_s - st->http_fail_since >= NET_WD_HTTP_DEAD_S;
    if (!in->wifi_expected) {
        const int64_t keep = st->http_fail_since;
        net_wd_init(st);
        st->http_fail_since = keep;
        if (!http_dead || now_s < NET_WD_BOOT_GRACE_S) return NET_WD_OK;
        return in->restarts_in_a_row >= NET_WD_MAX_RESTARTS ? NET_WD_SUPPRESSED : NET_WD_RESTART_HTTP;
    }
    /* link timer */
    if (in->link_up) {
        st->link_down_since = -1;
    } else if (st->link_down_since < 0) {
        st->link_down_since = now_s;
    }
    /* gateway timer: only a ping that failed after the gateway answered once, and only while a link is reported */
    const bool ping_failing = in->link_up && in->ping == 0 && in->ping_ever_ok;
    if (ping_failing) {
        if (st->ping_fail_since < 0) st->ping_fail_since = now_s;
    } else {
        st->ping_fail_since = -1;
    }
    /* "alive": a link and no silent gateway */
    const bool alive = in->link_up && !ping_failing;
    if (alive) {
        if (st->alive_since < 0) st->alive_since = now_s;
    } else {
        st->alive_since = -1;
    }

    net_wd_decision_t due = NET_WD_OK;
    if (st->link_down_since >= 0 && now_s - st->link_down_since >= NET_WD_LINK_DOWN_S) {
        due = NET_WD_RESTART_LINK;
    } else if (st->ping_fail_since >= 0 && now_s - st->ping_fail_since >= NET_WD_PING_DEAD_S && !in->ha_connected) {
        due = NET_WD_RESTART_TRANSPORT;
    } else if (http_dead) {
        due = NET_WD_RESTART_HTTP;
    }
    if (due == NET_WD_OK || now_s < NET_WD_BOOT_GRACE_S) return NET_WD_OK;
    return in->restarts_in_a_row >= NET_WD_MAX_RESTARTS ? NET_WD_SUPPRESSED : due;
}

bool net_wd_may_clear_restarts(const net_wd_state_t *state, int64_t now_s)
{
    return state->alive_since >= 0 && now_s - state->alive_since >= NET_WD_CLEAR_AFTER_S;
}
