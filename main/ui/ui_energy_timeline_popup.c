/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_energy_timeline_popup.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_popup.h"

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *body;
    lv_coord_t y;
    lv_coord_t width;
} etl_popup_t;

static etl_popup_t s_popup;

static void popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_popup.overlay != NULL) {
        lv_obj_delete(s_popup.overlay);
        s_popup.overlay = NULL;
    }
}

static void popup_deleted_cb(lv_event_t *e)
{
    (void)e;
    s_popup.overlay = NULL;
}

static lv_obj_t *popup_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                             lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_size(label, w, lv_font_get_line_height(font));
    lv_obj_set_pos(label, x, y);
    return label;
}

static void heading(const char *text)
{
    s_popup.y += 8;
    popup_label(s_popup.body, text, ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_PRIMARY, 0, s_popup.y, s_popup.width,
                LV_TEXT_ALIGN_LEFT);
    s_popup.y += lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 4;
}

/* label left (with a colour dot), value right, an optional muted line below (a statistic id). */
static void row(uint32_t dot_color, const char *label, float kwh, const char *unit, const char *detail)
{
    const lv_coord_t line = lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 4;
    if (dot_color != 0) {
        lv_obj_t *dot = lv_obj_create(s_popup.body);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_pos(dot, 0, s_popup.y + line / 2 - 5);
        lv_obj_set_style_bg_color(dot, lv_color_hex(dot_color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    }
    char value[32];
    snprintf(value, sizeof(value), kwh >= 100.0f ? "%.0f %s" : "%.2f %s", (double)kwh, unit);
    popup_label(s_popup.body, label, ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_PRIMARY, 18, s_popup.y, s_popup.width * 60 / 100,
                LV_TEXT_ALIGN_LEFT);
    popup_label(s_popup.body, value, ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_PRIMARY, s_popup.width * 60 / 100, s_popup.y,
                s_popup.width * 40 / 100, LV_TEXT_ALIGN_RIGHT);
    s_popup.y += line;
    if (detail != NULL && detail[0] != '\0') {
        popup_label(s_popup.body, detail, ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_MUTED, 18, s_popup.y, s_popup.width - 18,
                    LV_TEXT_ALIGN_LEFT);
        s_popup.y += line;
    }
}

static void note(const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(s_popup.body);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_width(label, s_popup.width);
    lv_obj_set_pos(label, 0, s_popup.y);
    lv_obj_update_layout(label);
    s_popup.y += lv_obj_get_height(label) + 6;
}

void ui_energy_timeline_popup_open(const ui_energy_timeline_popup_info_t *info, const ha_energy_timeline_snapshot_t *snapshot)
{
    if (info == NULL || snapshot == NULL) {
        return;
    }
    if (s_popup.overlay != NULL) {
        lv_obj_delete(s_popup.overlay);
        s_popup.overlay = NULL;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    lv_coord_t sw = lv_obj_get_width(screen), sh = lv_obj_get_height(screen);
    if (sw < 100 || sh < 100) {
        sw = 720;
        sh = 720;
    }
    s_popup.overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_popup.overlay, sw, sh);
    lv_obj_center(s_popup.overlay);
    lv_obj_set_scrollable(s_popup.overlay, false);
    lv_obj_set_style_bg_color(s_popup.overlay, lv_color_hex(0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_popup.overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_popup.overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_popup.overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_popup.overlay, popup_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(s_popup.overlay, popup_close_cb, LV_EVENT_CLICKED, NULL);

    lv_coord_t cw = sw - 24 > 640 ? 640 : sw - 24;
    lv_coord_t ch = sh - 48 > 560 ? 560 : sh - 48;
    lv_obj_t *card = lv_obj_create(s_popup.overlay);
    lv_obj_set_size(card, cw, ch);
    lv_obj_center(card);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_event_bubble(card, false);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, ui_style_radius(18), LV_PART_MAIN);
    ui_popup_style_frame(card);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    char title_buf[APP_MAX_NAME_LEN * 2];
    popup_label(card, ui_style_title(title_buf, sizeof(title_buf), info->title[0] ? info->title : "Energy"),
                ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_PRIMARY, 0, 0, cw - 100, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), popup_close_cb, NULL);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_popup.body = lv_obj_create(card);
    lv_obj_remove_style_all(s_popup.body);
    lv_obj_set_pos(s_popup.body, 0, 52);
    lv_obj_set_size(s_popup.body, cw - 28, ch - 80);
    lv_obj_set_scroll_dir(s_popup.body, LV_DIR_VER);
    s_popup.width = cw - 28 - 12;
    s_popup.y = 0;

    const et_series_t *series = &snapshot->series;
    et_totals_t t;
    et_totals(series, &t);
    char buf[96];

    /* Status: when, and how complete. */
    if (snapshot->state == HA_ET_LOADING) {
        note(ui_i18n_get("energytl.loading", "Loading ..."), APP_UI_COLOR_TEXT_MUTED);
    } else if (snapshot->state == HA_ET_NO_STATISTICS) {
        note(ui_i18n_get("energytl.nostats_hint", "Home Assistant has no long-term statistics for the grid import counter. "
                                                  "It needs a state_class of total or total_increasing."),
             ETL_COLOR_EXPORT);
    } else {
        struct tm local;
        time_t at = (time_t)snapshot->updated_epoch;
        char clock[8] = "--:--";
        if (localtime_r(&at, &local)) strftime(clock, sizeof(clock), "%H:%M", &local);
        snprintf(buf, sizeof(buf), ui_i18n_get("energytl.updated", "Updated %s"), clock);
        note(buf, snapshot->state == HA_ET_STALE ? ETL_COLOR_EXPORT : APP_UI_COLOR_TEXT_MUTED);
        if (t.slots_partial > 0) {
            snprintf(buf, sizeof(buf), ui_i18n_get("energytl.partial", "%d of %d slots are incomplete"),
                     (int)t.slots_partial, (int)ET_SLOTS);
            note(buf, APP_UI_COLOR_TEXT_MUTED);
        }
    }

    if (snapshot->state == HA_ET_READY || snapshot->state == HA_ET_STALE) {
        heading(ui_i18n_get("energytl.measured", "Measured, last 24 hours"));
        const etl_palette_t palette = info->palette.initialized ? info->palette : etl_palette_defaults();
        row(palette.value[ETL_GRID], ui_i18n_get("energytl.import", "Grid import"), t.import_kwh, "kWh", info->stat_id[ET_CH_GRID_IMPORT]);
        if (series->configured[ET_CH_GRID_EXPORT])
            row(palette.value[ETL_EXPORT], ui_i18n_get("energytl.export_meter", "Grid export"), t.export_kwh, "kWh",
                info->stat_id[ET_CH_GRID_EXPORT]);
        if (series->configured[ET_CH_SOLAR])
            row(palette.value[ETL_SOLAR], ui_i18n_get("energytl.solar_meter", "Solar production"), t.solar_kwh, "kWh",
                info->stat_id[ET_CH_SOLAR]);
        if (t.battery) {
            row(palette.value[ETL_BATTERY_IN], ui_i18n_get("energytl.charge", "Battery charge"), t.charge_kwh, "kWh",
                info->stat_id[ET_CH_BATTERY_CHARGE]);
            row(palette.value[ETL_BATTERY_OUT], ui_i18n_get("energytl.discharge", "Battery discharge"), t.discharge_kwh, "kWh",
                info->stat_id[ET_CH_BATTERY_DISCHARGE]);
        }
        if (info->has_soc) {
            snprintf(buf, sizeof(buf), "%.0f %%", (double)info->soc_percent);
            popup_label(s_popup.body, ui_i18n_get("energytl.soc", "Battery level"), ui_font_role(UI_TEXT_META),
                        APP_UI_COLOR_TEXT_PRIMARY, 18, s_popup.y, s_popup.width * 60 / 100, LV_TEXT_ALIGN_LEFT);
            popup_label(s_popup.body, buf, ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_PRIMARY, s_popup.width * 60 / 100,
                        s_popup.y, s_popup.width * 40 / 100, LV_TEXT_ALIGN_RIGHT);
            s_popup.y += lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 4;
        }
        if (info->has_forecast) {
            row(palette.value[ETL_FORECAST], ui_i18n_get("energytl.forecast", "Solar forecast"), info->forecast_kwh, "kWh", info->forecast_id);
        }

        heading(t.estimated ? ui_i18n_get("energytl.mix_estimated", "Mix (estimated)")
                            : ui_i18n_get("energytl.mix", "Mix"));
        row(0, ui_i18n_get("energytl.home", "Home use"), t.home_kwh, "kWh", NULL);
        row(palette.value[ETL_SOLAR], ui_i18n_get("energytl.home_from_solar", "  from solar"), t.home_from_solar, "kWh", NULL);
        if (t.battery)
            row(palette.value[ETL_BATTERY_OUT], ui_i18n_get("energytl.home_from_battery", "  from battery"), t.home_from_battery,
                "kWh", NULL);
        row(palette.value[ETL_GRID], ui_i18n_get("energytl.home_from_grid", "  from grid"), t.home_from_grid, "kWh", NULL);
        if (series->configured[ET_CH_GRID_EXPORT]) {
            row(0, ui_i18n_get("energytl.export", "Feed-in"), t.export_kwh, "kWh", NULL);
            row(palette.value[ETL_SOLAR], ui_i18n_get("energytl.export_from_solar", "  from solar"), t.export_from_solar, "kWh", NULL);
            if (t.battery)
                row(palette.value[ETL_BATTERY_OUT], ui_i18n_get("energytl.export_from_battery", "  from battery"),
                    t.export_from_battery, "kWh", NULL);
        }
        if (t.battery) {
            row(0, ui_i18n_get("energytl.charge", "Battery charge"), t.charge_kwh, "kWh", NULL);
            row(palette.value[ETL_SOLAR], ui_i18n_get("energytl.charge_from_solar", "  from solar"), t.battery_from_solar, "kWh", NULL);
            row(palette.value[ETL_GRID], ui_i18n_get("energytl.charge_from_grid", "  from grid"), t.battery_from_grid, "kWh", NULL);
        }
        if (t.estimated) {
            s_popup.y += 6;
            note(ui_i18n_get("energytl.estimate_note",
                             "Estimated: aggregate meters cannot tell whether grid energy charged the battery or "
                             "served the home. Solar serves the home first, then the battery, then the grid; the "
                             "battery serves what the home still needs. The measured values above are exact."),
                 APP_UI_COLOR_TEXT_MUTED);
        }
    }
    lv_obj_update_layout(s_popup.body);
}
