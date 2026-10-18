/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Asks the Parqet Companion integration for the figures of one interval (WebSocket command parqet/get_performance, see parqet_performance.h)
 * for the parqet_portfolio widget. One request runs at a time over the shared WebSocket helper; the others wait. An answer is kept for five
 * minutes, so several tiles of one portfolio and a page revisit cost one request. All functions run on the LVGL task.
 */
#pragma once

#include "esp_err.h"
#include "ha/parqet_performance.h"

/* status ESP_OK: perf is valid for the duration of the call. ESP_FAIL: Home Assistant refused, ESP_ERR_TIMEOUT: no answer in time (a reply above the
 * panel's 128 KB WebSocket buffer, such as the interval Max of a large portfolio, ends here), ESP_ERR_INVALID_RESPONSE: the reply had no figures. */
typedef void (*parqet_perf_cb_t)(void *user, esp_err_t status, const parqet_perf_t *perf);

/* interval is one of "1d", "1w", "1m", "ytd" (the integration also knows "1y", "max" ...). A cached answer calls cb before returning. Only one
 * request per `user` and interval waits at a time. */
esp_err_t parqet_request_perf(const char *entry_id, const char *portfolio_id, const char *interval, void *user, parqet_perf_cb_t cb);
/* Forget everything `user` waits for; its callback is not called any more. Call before the object behind `user` goes away. */
void parqet_request_cancel(void *user);
