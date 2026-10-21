/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The daily closes of an Easy Stock symbol from the integration's own endpoint, GET /api/easy_stock/history?symbol=...
 * (an authenticated Home Assistant request, not a WebSocket command; the endpoint belongs to the integration, tested with
 * v0.5.1). One low-priority worker with its stack in PSRAM, started on the first request; the reply is read into PSRAM and
 * refused above HA_QUOTE_HISTORY_MAX_BYTES, whatever Content-Length says. One request waits at a time: a newer one
 * replaces it (ui/quote_history_feed.c asks again for a lost answer).
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "ha/ha_quote_series.h"

typedef struct {
    esp_err_t status;                    /* ESP_OK: Home Assistant answered 200; ESP_ERR_NOT_FOUND: 404 (no such symbol or
                                            integration); ESP_ERR_INVALID_SIZE: oversized; anything else: no answer */
    ha_quote_history_outcome_t outcome;  /* with ESP_OK */
    ha_quote_daily_t daily;
} ha_easy_stock_history_t;

/* Completion runs on the LVGL task; the callback owns the result and frees it with ha_easy_stock_free(). */
typedef void (*ha_easy_stock_cb_t)(uint32_t generation, ha_easy_stock_history_t *history);

esp_err_t ha_easy_stock_request(const char *symbol, uint32_t generation, ha_easy_stock_cb_t callback);
void ha_easy_stock_free(ha_easy_stock_history_t *history);
