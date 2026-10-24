/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Statistics requests of the history popup: a metadata check (does this entity offer a 7-day view?) and the
 * 168-hour hourly-mean query. One worker runs them one after the other over the shared WebSocket helper.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ha/ha_history_stats.h"

typedef struct {
    esp_err_t status;              /* ESP_OK: Home Assistant answered; anything else: no answer, offer nothing */
    ha_stats_eligibility_t result;
} ha_history_check_t;

typedef struct {
    esp_err_t status;              /* ESP_OK, ESP_ERR_TIMEOUT, ESP_FAIL (transport/answer error), ESP_ERR_INVALID_RESPONSE */
    ha_stats_outcome_t outcome;
    ha_stats_window_t window;
    ha_stats_series_t series;
} ha_history_week_t;

typedef struct {
    esp_err_t status;              /* ESP_OK, ESP_ERR_TIMEOUT, ESP_FAIL, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_STATE (no clock) */
    ha_stats_outcome_t outcome;
    ha_stats_window_t window;
    ha_range_series_t series;
} ha_history_range_t;

#define HA_DAYS_MAX_SOURCES 4
#define HA_DAYS_COUNT 7

typedef struct {
    esp_err_t status;              /* ESP_OK: at least one source answered */
    ha_stats_outcome_t outcome;    /* EMPTY: answered, but no usable hour in the window */
    uint8_t sources_total;         /* sources asked for */
    uint8_t sources_eligible;      /* sources whose metadata says "cumulative counter in a volume/energy unit" */
    uint8_t sources_ok;            /* sources that delivered rows */
    char unit[HA_STATS_UNIT_MAX];  /* unit of the sums: the statistics unit of the first eligible source */
    ha_stats_window_t window;
    ha_stats_series_t series;      /* hourly changes, summed over the sources */
    ha_stats_days_t days;          /* the last seven local calendar days, today last */
} ha_history_days_t;

typedef void (*ha_history_days_cb_t)(uint32_t generation, ha_history_days_t *days);
typedef void (*ha_history_range_cb_t)(uint32_t generation, ha_history_range_t *range);

/* Completion runs on the LVGL task. The week callback owns and frees the result with ha_history_week_free(). The
 * generation ID lets a closed or replaced popup ignore an older answer without keeping a widget pointer. */
typedef void (*ha_history_check_cb_t)(uint32_t generation, const ha_history_check_t *check);
typedef void (*ha_history_week_cb_t)(uint32_t generation, ha_history_week_t *week);

/* display_unit is the unit the popup shows; state_class comes from the entity's attributes. Answers come from a
 * short-lived cache when possible. */
esp_err_t ha_history_check_week(const char *entity_id, const char *display_unit, ha_stats_state_class_t state_class,
                                uint32_t generation, ha_history_check_cb_t callback);
/* convert/convert_class come from a positive check: HA converts to display_unit in that unit class. */
esp_err_t ha_history_request_week(const char *entity_id, const char *display_unit, bool convert,
                                  const char *convert_class, uint32_t generation, ha_history_week_cb_t callback);
void ha_history_week_free(ha_history_week_t *week);

/* Daily consumption of up to HA_DAYS_MAX_SOURCES cumulative counters (statistic IDs, e.g. the Water or Gas source of
 * the Energy dashboard): one metadata call for all, then the hourly change of every eligible source, summed into the
 * last seven local days. The callback owns and frees the result with ha_history_days_free(). */
esp_err_t ha_history_request_days(const char *const *ids, size_t count, uint32_t generation, ha_history_days_cb_t callback);
void ha_history_days_free(ha_history_days_t *days);

/* Daily means of the last 30 days or weekly means of the last 12 months of one measurement entity (a quote), from the Recorder
 * statistics: no metadata check, an entity without statistics answers with outcome EMPTY. The callback owns and frees the
 * result with ha_history_range_free(). */
esp_err_t ha_history_request_range(const char *entity_id, ha_range_kind_t kind, uint32_t generation, ha_history_range_cb_t callback);
void ha_history_range_free(ha_history_range_t *range);
