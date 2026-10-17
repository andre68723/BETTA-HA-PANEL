/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Network watchdog. The task watchdog only sees blocked tasks; on the paneljc the screen stayed alive while the ESP32-C6 link was
 * dead for minutes past every recovery step in wifi_mgr.c. This decides, from a few facts gathered every half minute, when the panel
 * should give up and restart (which also resets the C6). It is deliberately slow and bounded, so a router outage cannot make it
 * reboot forever:
 *   - no Wi-Fi link for NET_WD_LINK_DOWN_S while a network is configured (and not in setup AP mode), or
 *   - a link that looks up but the gateway stopped answering pings for NET_WD_PING_DEAD_S although it answered before, with no
 *     Home Assistant connection to prove the traffic flows,
 *   - the device's own HTTP server (probed over loopback, which does not touch the C6) not answering for NET_WD_HTTP_DEAD_S while the
 *     rest looks alive: a hang seen on the paneljc after a layout change, pingable but with every request timing out,
 *   - never within NET_WD_BOOT_GRACE_S of boot, and at most NET_WD_MAX_RESTARTS times in a row (a restart counts as "in a row" until the
 *     network has been alive for NET_WD_CLEAR_AFTER_S).
 * Pure (host tested); util/net_watchdog_esp.c gathers the inputs and restarts.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NET_WD_BOOT_GRACE_S 300
#define NET_WD_LINK_DOWN_S 600
#define NET_WD_PING_DEAD_S 600
#define NET_WD_CLEAR_AFTER_S 600
#define NET_WD_HTTP_DEAD_S 300
#define NET_WD_MAX_RESTARTS 3

typedef struct {
    int64_t link_down_since; /* uptime seconds the link went down, -1 while it is up */
    int64_t ping_fail_since; /* uptime seconds the gateway pings began failing, -1 while they work or are unknown */
    int64_t alive_since;     /* uptime seconds the network has been alive without a break, -1 when not alive */
    int64_t http_fail_since; /* uptime seconds the HTTP self-probe began failing, -1 while it works or is unknown */
} net_wd_state_t;

typedef struct {
    bool wifi_expected;     /* a network is configured and the panel is not in setup AP mode */
    bool link_up;           /* Wi-Fi connected with an IP address */
    bool ha_connected;      /* the Home Assistant WebSocket is connected (proof that traffic flows) */
    int ping;               /* last gateway ping: 1 answered, 0 timed out, -1 none yet */
    bool ping_ever_ok;      /* the gateway answered at least once since boot (some routers never do) */
    int http;               /* last HTTP self-probe: 1 answered, 0 failed, -1 none yet */
    uint32_t restarts_in_a_row;
} net_wd_input_t;

typedef enum {
    NET_WD_OK = 0,
    NET_WD_RESTART_LINK,      /* no link for too long */
    NET_WD_RESTART_TRANSPORT, /* link up, gateway silent, no Home Assistant traffic */
    NET_WD_RESTART_HTTP,      /* the panel's own web server stopped answering */
    NET_WD_SUPPRESSED,        /* a restart is due but the limit of restarts in a row is reached */
} net_wd_decision_t;

void net_wd_init(net_wd_state_t *state);
/* One evaluation at `now_s` (seconds since boot). */
net_wd_decision_t net_wd_step(net_wd_state_t *state, const net_wd_input_t *in, int64_t now_s);
/* True once the network has been alive for NET_WD_CLEAR_AFTER_S: the counter of restarts in a row may be cleared. */
bool net_wd_may_clear_restarts(const net_wd_state_t *state, int64_t now_s);
