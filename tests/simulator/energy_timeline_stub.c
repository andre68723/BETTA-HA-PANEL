/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Simulator replacement for ha_energy_timeline.c: no Home Assistant, a fixed synthetic day. Which meters exist
 * follows the widget's configuration; a widget id containing "loading", "stale" or "nostats" shows that state.
 */
#include "ha/ha_energy_timeline.h"

#include <math.h>
#include <string.h>

#include "app_config.h"

#define STUB_MAX 8
static struct {
    bool used;
    char id[APP_MAX_WIDGET_ID_LEN];
    bool configured[ET_CH_COUNT];
} s_widgets[STUB_MAX];

esp_err_t ha_energy_timeline_register(const char *widget_id, const ha_energy_timeline_config_t *config)
{
    if (widget_id == NULL || config == NULL || config->stat_id[ET_CH_GRID_IMPORT] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < STUB_MAX; i++) {
        if (!s_widgets[i].used) {
            s_widgets[i].used = true;
            strlcpy(s_widgets[i].id, widget_id, sizeof(s_widgets[i].id));
            for (int c = 0; c < ET_CH_COUNT; c++) {
                s_widgets[i].configured[c] = config->stat_id[c] != NULL && config->stat_id[c][0] != '\0';
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void ha_energy_timeline_unregister(const char *widget_id)
{
    for (int i = 0; i < STUB_MAX; i++) {
        if (s_widgets[i].used && strcmp(s_widgets[i].id, widget_id) == 0) s_widgets[i].used = false;
    }
}

bool ha_energy_timeline_get(const char *widget_id, ha_energy_timeline_snapshot_t *out)
{
    for (int i = 0; i < STUB_MAX; i++) {
        if (!s_widgets[i].used || strcmp(s_widgets[i].id, widget_id) != 0) continue;
        memset(out, 0, sizeof(*out));
        out->version = 1;
        const int64_t now = 1780056000 + 7 * 300; /* a fixed evening, so screenshots do not change from run to run */
        out->updated_epoch = now;
        out->state = strstr(widget_id, "loading") ? HA_ET_LOADING
                   : strstr(widget_id, "stale") ? HA_ET_STALE
                   : strstr(widget_id, "nostats") ? HA_ET_NO_STATISTICS : HA_ET_READY;
        et_series_init(&out->series, now, s_widgets[i].configured);
        if (out->state == HA_ET_LOADING) return true;
        for (size_t slot = 0; slot < ET_SLOTS; slot++) {
            if (slot >= 40 && slot < 44) continue; /* a Recorder gap: no bars, never zero bars */
            const int64_t at = out->series.window_start + (int64_t)slot * ET_SLOT_SECONDS;
            const double hour = fmod((double)at / 3600.0 + 2.0, 24.0); /* local hour */
            double sun = (hour > 6.0 && hour < 20.0) ? sin((hour - 6.0) / 14.0 * M_PI) : 0.0;
            sun = sun * sun * 1.15;
            double home = 0.14 + 0.05 * sin(hour) + ((hour > 6.5 && hour < 8.5) ? 0.22 : 0.0) +
                          ((hour > 11.5 && hour < 13.5) ? 0.30 : 0.0) + ((hour > 17.5 && hour < 21.5) ? 0.45 : 0.0);
            if (!s_widgets[i].configured[ET_CH_SOLAR]) sun = 0.0;
            const bool battery = s_widgets[i].configured[ET_CH_BATTERY_CHARGE] && s_widgets[i].configured[ET_CH_BATTERY_DISCHARGE];
            const double direct = sun < home ? sun : home;
            double surplus = sun - direct, deficit = home - direct;
            const double charge = battery ? (surplus < 0.45 ? surplus : 0.45) : 0.0;
            surplus -= charge;
            const double discharge = battery && deficit > 0.0 ? (deficit < 0.5 ? deficit : 0.5) : 0.0;
            deficit -= discharge;
            const double to_grid = s_widgets[i].configured[ET_CH_GRID_EXPORT] ? surplus : 0.0;
            const double from_grid = deficit;
            for (int part = 0; part < ET_PARTS_PER_SLOT; part++) {
                if (slot == ET_SLOTS - 1 && part > 0) break; /* the running slot is partial */
                const int64_t start = at + part * ET_PART_SECONDS;
                et_add_part(&out->series, ET_CH_GRID_IMPORT, start, from_grid / 3.0);
                if (s_widgets[i].configured[ET_CH_GRID_EXPORT]) et_add_part(&out->series, ET_CH_GRID_EXPORT, start, to_grid / 3.0);
                if (s_widgets[i].configured[ET_CH_SOLAR]) et_add_part(&out->series, ET_CH_SOLAR, start, sun / 3.0);
                if (battery) {
                    et_add_part(&out->series, ET_CH_BATTERY_CHARGE, start, charge / 3.0);
                    et_add_part(&out->series, ET_CH_BATTERY_DISCHARGE, start, discharge / 3.0);
                }
            }
        }
        return true;
    }
    return false;
}
