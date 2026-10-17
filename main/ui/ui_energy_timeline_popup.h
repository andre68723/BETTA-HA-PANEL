/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Details popup of the energy timeline: the measured energy of every counter with its statistic id, the estimated
 * mix and how it is derived, the battery state and the forecast source. Opened by a tap on the widget.
 */
#pragma once

#include <stdbool.h>

#include "app_config.h"
#include "ha/ha_energy_timeline.h"
#include "layout/energy_timeline_colors.h"


typedef struct {
    etl_palette_t palette;
    char title[APP_MAX_NAME_LEN];
    char stat_id[ET_CH_COUNT][APP_MAX_ENTITY_ID_LEN];
    char soc_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_id[APP_MAX_ENTITY_ID_LEN];
    bool has_soc;
    float soc_percent;
    bool has_forecast;
    float forecast_kwh;        /* forecast production over the slots the forecast covers in the window */
} ui_energy_timeline_popup_info_t;

void ui_energy_timeline_popup_open(const ui_energy_timeline_popup_info_t *info, const ha_energy_timeline_snapshot_t *snapshot);
