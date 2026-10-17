/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Fills the energy timeline widgets from the Home Assistant Recorder. Each widget registers its statistic ids;
 * a background task requests the last 24 hours (recorder/statistics_during_period, five-minute change) one
 * statistic at a time through the heavy WebSocket gate, and refreshes every 15 minutes. The widget only reads
 * copies of the snapshot from the UI task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ha/energy_timeline_model.h"

typedef enum {
    HA_ET_LOADING = 0,     /* nothing received yet */
    HA_ET_READY,
    HA_ET_STALE,           /* the data is older than 40 minutes (Home Assistant unreachable or requests failing) */
    HA_ET_NO_STATISTICS    /* Home Assistant answered, but has no statistics for the grid import id */
} ha_energy_timeline_state_t;

typedef struct {
    ha_energy_timeline_state_t state;
    uint32_t version;         /* changes whenever the data does */
    int64_t updated_epoch;    /* UTC seconds of the last complete fetch, 0 if none */
    et_series_t series;
} ha_energy_timeline_snapshot_t;

/* Statistic ids by channel; an empty or NULL id leaves the channel unconfigured. Grid import is required. */
typedef struct {
    const char *stat_id[ET_CH_COUNT];
} ha_energy_timeline_config_t;

#define HA_ET_MAX_WIDGETS 4

esp_err_t ha_energy_timeline_register(const char *widget_id, const ha_energy_timeline_config_t *config);
void ha_energy_timeline_unregister(const char *widget_id);
/* Copies the current snapshot of a registered widget. False when the id is not registered. */
bool ha_energy_timeline_get(const char *widget_id, ha_energy_timeline_snapshot_t *out);
