/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <stdio.h>
#include "ui/widgets/weather_tile_internal.h"

#if APP_UI_WEATHER_ICON_DEBUG
static const char *TAG = "w_weather_tile";
#endif

static bool weather_render_columns(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values)
{
    if (!weather_columns_wanted(card) || values == NULL) {
        weather_columns_hide(ctx);
        return false;
    }
    weather_columns_build(card, ctx);
    ctx->last_values = *values;
    lv_obj_update_layout(card);
    const lv_coord_t cw = lv_obj_get_content_width(card);
    const lv_coord_t ch = lv_obj_get_content_height(card);

    const lv_font_t *day_font = ui_font_role(UI_TEXT_LABEL);
    const lv_font_t *value_font = ui_font_role(UI_TEXT_VALUE);
    const lv_font_t *low_font = ui_font_role(UI_TEXT_VALUE);
    const lv_font_t *rain_font = ui_font_role(UI_TEXT_META);
    const lv_coord_t day_h = day_font->line_height, value_h = value_font->line_height;
    const lv_coord_t low_h = low_font->line_height, rain_h = rain_font->line_height;
#if UI_TEXT_ADAPTIVE
    const bool readable = true;
#else
    const bool readable = false;
#endif
    /* Old row view and its header icon stay out of the way. */
    for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
        if (ctx->rows[i].container != NULL) {
            lv_obj_set_hidden(ctx->rows[i].container, true);
        }
    }
    weather_hide_lottie(ctx);
    lv_obj_set_hidden(ctx->condition_label, true);

    /* Header: big icon left, temperature and condition right of it. */
    const bool night = weather_is_night_now();
    const lv_coord_t head_y = ctx->hide_name ? 8 : (readable ? 52 : 36); /* leave the single-line title clear of a scaled icon’s native box */
    /* The large header icon only when the forecast cards below still get room for a
     * full-size day icon; on a short tile it pushed their temperatures out of the card. */
    const lv_coord_t card_fixed_h = day_h + 2 + 2 +
                                    value_h + 12;
    const bool large = !readable && cw >= 260 &&
                       ch - (head_y + WEATHER_ICON_LARGE_PX + 10) >= card_fixed_h + WEATHER_ICON_SMALL_PX;
    /* A short tile (down to 200 px) gets a one-line header with a smaller icon, so the day cards
     * below keep their icons and temperatures instead of being left out. */
    const bool short_head = !large && ch - (head_y + WEATHER_ICON_SMALL_PX + 10) < card_fixed_h + 24;
    const lv_coord_t icon_px = readable && cw >= 400 ? 32 : (large ? WEATHER_ICON_LARGE_PX : (short_head ? 40 : WEATHER_ICON_SMALL_PX));
    const lv_image_dsc_t *main_dsc =
        weather_icon_image(weather_icon_from_condition(weather_effective_condition_key(ctx, values), night), large);
    if (main_dsc != NULL) {
        weather_set_image_if_changed(ctx->col_main_icon, main_dsc);
        /* Scaled around its center, so the smaller icon is shifted back to the header's corner. */
        const lv_coord_t native = large ? WEATHER_ICON_LARGE_PX : WEATHER_ICON_SMALL_PX;
        lv_image_set_scale(ctx->col_main_icon, (uint32_t)(256 * icon_px / native));
        lv_obj_set_pos(ctx->col_main_icon, -(native - icon_px) / 2, head_y - (native - icon_px) / 2);
        lv_obj_set_hidden(ctx->col_main_icon, false);
    } else {
        lv_obj_set_hidden(ctx->col_main_icon, true);
    }
    const lv_coord_t text_x = main_dsc != NULL ? icon_px + 6 : 0;
    const lv_coord_t text_w = cw - text_x;

    char temp_text[32] = "--";
    if (values->has_temp) {
        snprintf(temp_text, sizeof(temp_text), "%.1f %s", (double)values->temp, values->unit);
    }
    lv_label_set_text(ctx->temp_label, temp_text);
    const lv_font_t *temp_font = readable ? ui_font_role(UI_TEXT_VALUE) : (short_head ? ui_font_role(UI_TEXT_META) : WEATHER_3DAY_TEMP_FONT);
    lv_obj_set_style_text_font(ctx->temp_label, temp_font, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->temp_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_long_mode(ctx->temp_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ctx->temp_label, text_w);

    const char *display_condition = ctx->last_condition_text[0] != '\0' ? ctx->last_condition_text : values->condition;
    char meta_text[64];
    const bool inline_head = readable && cw >= 400;
    if (values->humidity >= 0) {
        snprintf(meta_text, sizeof(meta_text), (short_head || readable) ? "%s | %d %%" : "%s\n%d %%", display_condition,
                 values->humidity);
    } else {
        snprintf(meta_text, sizeof(meta_text), "%s", display_condition);
    }
    lv_label_set_text(ctx->meta_label, meta_text);
    const lv_font_t *meta_font = readable ? ui_font_role(UI_TEXT_META) : (short_head ? ui_font_role(UI_TEXT_META) : WEATHER_3DAY_META_FONT);
    lv_obj_set_style_text_font(ctx->meta_label, meta_font, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->meta_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    const bool one_line_meta = short_head || readable; /* one line: a longer text ends in dots, it never grows into the next row */
    lv_label_set_long_mode(ctx->meta_label, one_line_meta ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->meta_label, text_w);
    if (one_line_meta) {
        lv_obj_set_height(ctx->meta_label, lv_font_get_line_height(meta_font));
    } else {
        lv_obj_set_height(ctx->meta_label, LV_SIZE_CONTENT);
    }

    const lv_coord_t temp_h = lv_font_get_line_height(temp_font);
    const lv_coord_t meta_h = (short_head || readable ? 1 : 2) * lv_font_get_line_height(meta_font);
    const lv_coord_t line_gap = short_head ? 0 : 4;
    lv_coord_t block_y = head_y + (icon_px - (temp_h + line_gap + meta_h)) / 2;
    if (block_y < head_y) {
        block_y = head_y;
    }
    lv_obj_set_pos(ctx->temp_label, text_x, block_y);
    /* the temperature's own width places what follows it (it grows with the text size) */
    lv_point_t temp_size;
    lv_text_get_size(&temp_size, temp_text, temp_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_coord_t temp_block_w = temp_size.x + 44 > 150 ? temp_size.x + 44 : 150;
    lv_obj_set_pos(ctx->meta_label, inline_head ? text_x + temp_block_w : text_x, inline_head ? block_y + (temp_h - meta_h) / 2 : block_y + temp_h + line_gap);
    if (inline_head) lv_obj_set_width(ctx->meta_label, text_w - temp_block_w);
    ui_text_record(ctx->temp_label, UI_TEXT_VALUE, true);
    ui_text_record(ctx->meta_label, UI_TEXT_META, true);

    /* Report summary in the free header space right of the temperature block. */
    ctx->report_x = text_x + (temp_block_w > 200 ? temp_block_w : 200) + 12;
    ctx->report_w = readable ? 0 : cw - ctx->report_x;
    ctx->report_y = head_y;
    ctx->report_h = icon_px;
    weather_report_update_label(ctx);

    /* Forecast cards. */
    weather_3day_row_t *rows = ctx->render_rows;
    memset(rows, 0, sizeof(ctx->render_rows));
    weather_build_3day_rows(values, rows);
    weather_copy_text(rows[0].day, sizeof(rows[0].day), ui_i18n_get("weather.today", "Today"));

    const lv_coord_t gap = 6;
    int count = (int)((cw + gap) / (54 + gap));
    if (count > WEATHER_COLS_MAX) {
        count = WEATHER_COLS_MAX;
    }
    if (count > WEATHER_3DAY_ROWS) {
        count = WEATHER_3DAY_ROWS;
    }
    if (count < 3) {
        count = 3;
    }
    if (ctx->forecast_days >= 0) count = ui_day_grid_count(cw, ctx->forecast_days, WEATHER_COLS_MAX);
    if (ctx->forecast_days >= 0) {
        weather_3day_row_t dated[WEATHER_3DAY_ROWS]; memcpy(dated, rows, sizeof(dated));
        for (int i = 0; i < count; i++) {
            char date[11]; ui_day_grid_date(i, date);
            if (!date[0]) break;
            memset(&rows[i], 0, sizeof(rows[i])); rows[i].valid = true;
            rows[i].precip_mm = -1; rows[i].precip_prob = -1;
            weather_copy_text(rows[i].date, sizeof(rows[i].date), date);
            weather_day_from_datetime(date, rows[i].day, sizeof(rows[i].day));
            if (!i) weather_copy_text(rows[i].day, sizeof(rows[i].day), ui_i18n_get("weather.today", "Today"));
            for (int j = 0; j < WEATHER_3DAY_ROWS; j++)
                if (dated[j].valid && !strcmp(dated[j].date, date)) { rows[i] = dated[j]; break; }
        }
    }
    /* Only as many cards as there are forecast days, so the row stays centered. */
    int valid_days = 0;
    while (valid_days < count && rows[valid_days].valid) {
        valid_days++;
    }
    if (valid_days > 0 && valid_days < count) {
        count = valid_days;
    }
    lv_coord_t col_w = (cw - gap * (count - 1)) / count;
    if (!readable && col_w > 96) {
        col_w = 96;
    }
    const lv_coord_t total_w = col_w * count + gap * (count - 1);
    const lv_coord_t x0 = (cw - total_w) / 2;
    /* Below both the icon and the temperature/condition block next to it. */
    const lv_coord_t text_bottom = block_y + temp_h + (inline_head ? 0 : line_gap + meta_h);
    const lv_coord_t free_y = (text_bottom > head_y + icon_px ? text_bottom : head_y + icon_px) + 10;
    const lv_coord_t free_h = ch - free_y;
    /* Cards are as tall as their content needs (plus some air), centered in the free area. */
    bool any_precip = false;
    if (ctx->show_precip) {
        for (int i = 0; i < count; i++) {
            if (rows[i].valid && (rows[i].precip_prob >= 0 || rows[i].precip_mm >= 0.0f)) {
                any_precip = true;
            }
        }
    }
    const lv_coord_t col_h_max = readable ? day_h + 52 + value_h + low_h + (any_precip ? 2 * rain_h + 6 : 0) + 20 : (any_precip ? 236 : 190);
    const lv_coord_t col_h = free_h < col_h_max ? free_h : col_h_max;
    const lv_coord_t y0 = free_y + (free_h - col_h) / 2;

    for (int i = 0; i < WEATHER_COLS_MAX; i++) {
        weather_col_widgets_t *col = &ctx->cols[i];
        const weather_3day_row_t *row = (i < count) ? &rows[i] : NULL;
        if (row == NULL || !row->valid || col_h < 70) {
            lv_obj_set_hidden(col->box, true);
            continue;
        }
        lv_obj_set_hidden(col->box, false);
        lv_obj_set_pos(col->box, x0 + i * (col_w + gap), y0);
        lv_obj_set_size(col->box, col_w, col_h);

        const lv_coord_t fixed_h = day_h + value_h + 16;
        const bool show_lo = readable ? col_h >= fixed_h + 24 + low_h : col_h >= 150;
        const int rain_room = col_h - fixed_h - 24 - (show_lo ? low_h : 0) - 6;
        const int rain_max = !any_precip || !show_lo ? 0 : (readable ? (rain_room >= 2 * rain_h ? 2 : (rain_room >= rain_h ? 1 : 0)) : (col_h >= 210 ? 2 : (col_h >= 180 ? 1 : 0)));
        /* The day icon takes what the day name and the high leave, up to its full size; below
         * 24 px it is left out rather than drawn over the temperatures. */
        lv_coord_t icon_size = col_h - (readable ? fixed_h : card_fixed_h) -
                               (show_lo ? low_h : 0) -
                               (rain_max > 0 ? 6 + rain_max * rain_h : 0);
        if (icon_size > WEATHER_ICON_SMALL_PX) {
            icon_size = WEATHER_ICON_SMALL_PX;
        }
        if (icon_size < 24) {
            icon_size = 0;
        }
        lv_coord_t content_h = day_h + 2 + icon_size + 2 + value_h;
        if (show_lo) {
            content_h += low_h;
        }
        if (rain_max > 0) {
            content_h += 6 + rain_max * rain_h;
        }
        lv_coord_t y = (col_h - content_h) / 2;
        if (y < 6) {
            y = 6;
        }
        ui_text_record(col->day, UI_TEXT_LABEL, true);
        lv_point_t day_size; lv_text_get_size(&day_size, row->day, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        char short_day[12]; const char *shown_day = row->day;
        if (readable && day_size.x > col_w && row->date[0]) {
            weather_day_from_datetime(row->date, short_day, sizeof(short_day)); shown_day = short_day;
        }
        const lv_font_t *fit_day = readable ? ui_text_fit(shown_day, day_font, col_w, day_h) : day_font;
        lv_obj_set_style_text_font(col->day, fit_day, LV_PART_MAIN);
        ui_text_compact(col->day, fit_day == ui_font_role(UI_TEXT_META));
        ui_text_record(col->hi, UI_TEXT_VALUE, true); ui_text_record(col->lo, UI_TEXT_VALUE, true);
        ui_text_record(col->rain, UI_TEXT_META, true);
        lv_obj_set_style_text_font(col->hi, value_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(col->lo, low_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(col->rain, rain_font, LV_PART_MAIN);
        lv_label_set_text(col->day, shown_day);
        lv_obj_set_width(col->day, col_w);
        lv_obj_set_pos(col->day, 0, y);
        y += day_h + 2;

        const weather_icon_id_t id = weather_icon_from_condition(row->condition_key, (i == 0) ? night : false);
        const lv_image_dsc_t *dsc = weather_icon_image(id, false);
        if (dsc != NULL && icon_size > 0) {
            weather_set_image_if_changed(col->icon, dsc);
            lv_image_set_scale(col->icon, (uint32_t)(256 * icon_size / WEATHER_ICON_SMALL_PX));
            lv_obj_set_pos(col->icon, (col_w - WEATHER_ICON_SMALL_PX) / 2, y - (WEATHER_ICON_SMALL_PX - icon_size) / 2);
            lv_obj_set_hidden(col->icon, false);
        } else {
            lv_obj_set_hidden(col->icon, true);
        }
        y += icon_size + 2;

        weather_set_col_temp(col->hi, row->has_high, row->high_temp);
        lv_obj_set_width(col->hi, col_w);
        lv_obj_set_pos(col->hi, 0, y);
        y += value_h;
        if (show_lo) {
            weather_set_col_temp(col->lo, row->has_low, row->low_temp);
            lv_obj_set_width(col->lo, col_w);
            lv_obj_set_pos(col->lo, 0, y);
            lv_obj_set_hidden(col->lo, false);
            y += low_h + 6;
        } else {
            lv_obj_set_hidden(col->lo, true);
        }
        lv_obj_set_width(col->rain, col_w);
        if (rain_max > 0 && weather_set_col_rain(col->rain, row, values->precip_unit, rain_max, col_w) > 0) {
            lv_obj_set_pos(col->rain, 0, y);
            lv_obj_set_hidden(col->rain, false);
        } else {
            lv_obj_set_hidden(col->rain, true);
        }
    }
    return true;
}

static void weather_render_3day(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values, bool available)
{
    if (card == NULL || ctx == NULL) {
        return;
    }
    if (available && values != NULL && weather_render_columns(card, ctx, values)) {
        return;
    }
    weather_columns_hide(ctx);

    lv_coord_t card_w = lv_obj_get_width(card);
    lv_coord_t icon_x = 20;
    lv_coord_t icon_y = ctx->hide_name ? 0 : 20;

    lv_obj_set_style_text_font(ctx->temp_label, WEATHER_TEMP_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->meta_label, WEATHER_3DAY_SUBMETA_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->temp_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->meta_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(ctx->temp_label, card_w - 32);
    lv_obj_set_width(ctx->meta_label, card_w - 32);
    lv_obj_set_pos(ctx->temp_label, 16, 58 - (ctx->hide_name ? 28 : 0));
    lv_obj_set_pos(ctx->meta_label, 16, 102 - (ctx->hide_name ? 28 : 0));

    for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
        weather_3day_row_widgets_t *row = &ctx->rows[i];
        if (row->container == NULL) {
            continue;
        }

        lv_obj_set_style_text_font(row->day_label, WEATHER_3DAY_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->day_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(row->day_label, LV_LABEL_LONG_CLIP);

        lv_obj_set_style_text_font(row->icon_label, WEATHER_3DAY_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->icon_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(row->icon_label, LV_LABEL_LONG_CLIP);

        lv_obj_set_style_text_font(row->low_label, WEATHER_3DAY_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->low_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->low_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

        lv_obj_set_style_text_font(row->high_label, WEATHER_3DAY_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->high_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->high_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

        lv_obj_set_style_bg_color(row->bar_track, lv_color_hex(WEATHER_3DAY_TRACK_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->bar_track, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->bar_track, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row->bar_track, ui_style_track_radius(), LV_PART_MAIN);

        lv_obj_set_style_bg_color(row->bar_fill, lv_color_hex(WEATHER_3DAY_FILL_COLD), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(row->bar_fill, lv_color_hex(WEATHER_3DAY_FILL_HOT), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(row->bar_fill, LV_GRAD_DIR_HOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->bar_fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->bar_fill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row->bar_fill, ui_style_track_radius(), LV_PART_MAIN);

        lv_obj_set_style_bg_color(row->bar_marker, lv_color_hex(0xD5DEE3), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->bar_marker, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->bar_marker, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->bar_marker, lv_color_hex(0x4E5D68), LV_PART_MAIN);
        lv_obj_set_style_radius(row->bar_marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    }
    if (!available || values == NULL) {
        lv_obj_set_hidden(ctx->condition_label, false);
        lv_obj_set_style_text_font(ctx->condition_label, WEATHER_3DAY_TEMP_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->condition_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_size(ctx->condition_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_LEFT, icon_x, icon_y + 8);
        lv_label_set_text(ctx->condition_label, "--");

        lv_label_set_text(ctx->temp_label, "--");
        lv_label_set_text(ctx->meta_label, ui_i18n_get("weather.unavailable", "Unavailable"));
        weather_set_3day_rows_layout(card, ctx, WEATHER_3DAY_ROWS_TOP - (ctx->hide_name ? 28 : 0));
        for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
            weather_set_3day_row_values(&ctx->rows[i], NULL, "C", 0.0f, 1.0f);
        }
        weather_hide_lottie(ctx);
        return;
    }

    const char *display_condition = (ctx->last_condition_text[0] != '\0') ? ctx->last_condition_text : values->condition;

    uint32_t icon_cp = ctx->last_icon_cp;
    const lv_font_t *icon_font = NULL;
    bool icon_mode = false;
    if (icon_cp != 0U) {
        icon_font = weather_pick_render_icon_font(card, ctx, icon_cp, ctx->last_icon_font);
    }
    if (icon_cp != 0U && icon_font != NULL) {
        char icon_utf8[5] = {0};
        if (weather_icon_utf8_from_codepoint(icon_cp, icon_utf8)) {
            icon_mode = true;
            lv_label_set_long_mode(ctx->condition_label, LV_LABEL_LONG_CLIP);
            lv_obj_set_size(ctx->condition_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(ctx->condition_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_set_style_text_font(ctx->condition_label, icon_font, LV_PART_MAIN);
            lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_WEATHER_ICON), LV_PART_MAIN);
            lv_obj_set_style_text_opa(ctx->condition_label, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_LEFT, icon_x, icon_y);
            lv_label_set_text(ctx->condition_label, icon_utf8);
            ctx->last_icon_cp = icon_cp;
            ctx->last_icon_font = icon_font;
        }
    }

    if (!icon_mode) {
        lv_label_set_long_mode(ctx->condition_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ctx->condition_label, card_w - icon_x - 28);
        lv_obj_set_style_text_align(ctx->condition_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->condition_label, WEATHER_3DAY_META_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_LEFT, icon_x, icon_y + 16);
        lv_label_set_text(ctx->condition_label, display_condition);
    }

    const char *visual_condition_key = weather_effective_condition_key(ctx, values);
    bool lottie_mode = weather_show_lottie(card, ctx, visual_condition_key, icon_x - 4, icon_y - 8, 0);
    if (lottie_mode) {
        lv_obj_set_hidden(ctx->condition_label, true);
    } else {
        lv_obj_set_hidden(ctx->condition_label, false);
    }

    char temp_text[32] = {0};
    if (values->has_temp) {
        snprintf(temp_text, sizeof(temp_text), "%.1f %s", (double)values->temp, values->unit);
    } else {
        snprintf(temp_text, sizeof(temp_text), "--");
    }
    lv_label_set_text(ctx->temp_label, temp_text);

    char meta_text[64] = {0};
    if (values->humidity >= 0) {
        snprintf(meta_text, sizeof(meta_text), "%s | %d%%", display_condition, values->humidity);
    } else {
        snprintf(meta_text, sizeof(meta_text), "%s", display_condition);
    }
    lv_label_set_text(ctx->meta_label, meta_text);

    weather_set_3day_rows_layout(card, ctx, WEATHER_3DAY_ROWS_TOP - (ctx->hide_name ? 28 : 0));

    weather_3day_row_t *rows = ctx->render_rows;
    memset(rows, 0, sizeof(ctx->render_rows));
    weather_build_3day_rows(values, rows);
    float range_min = 0.0f;
    float range_max = 1.0f;
    weather_compute_3day_range(rows, &range_min, &range_max);
    for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
        weather_set_3day_row_values(&ctx->rows[i], &rows[i], values->unit, range_min, range_max);
    }
}

void weather_render(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values, bool available)
{
    if (card == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);

    if (ctx->show_forecast) {
        weather_render_3day(card, ctx, values, available);
        return;
    }

    weather_now_hide(&ctx->now_view);
    if (available && values != NULL) {
        weather_now_data_t now = {
            .hide_name = ctx->hide_name, .has_temp = values->has_temp, .temp = values->temp, .unit = values->unit,
            .condition = ctx->last_condition_text[0] ? ctx->last_condition_text : values->condition,
            .condition_key = weather_effective_condition_key(ctx, values), .night = weather_is_night_now(),
            .humidity = values->humidity, .has_low = values->today_has_low, .low = values->today_low_temp,
            .precip = values->today_precip_mm, .precip_prob = values->today_precip_prob,
            .precip_unit = values->precip_unit
        };
        if (weather_now_render(card, ctx->condition_label, ctx->temp_label, ctx->meta_label,
                               &ctx->now_view, &now)) {
            weather_hide_lottie(ctx);
            return;
        }
    }

    lv_obj_set_hidden(ctx->meta_label, false);
    lv_obj_set_width(ctx->temp_label, lv_obj_get_content_width(card));
    lv_obj_set_width(ctx->meta_label, lv_obj_get_content_width(card));
    const lv_font_t *temp_font = weather_pick_temp_font(card);
    const lv_font_t *meta_font = weather_pick_meta_font(card);
    lv_obj_set_style_text_font(ctx->temp_label, temp_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->meta_label, meta_font, LV_PART_MAIN);

    if (!available || values == NULL) {
        lv_coord_t text_width = weather_condition_text_width(card);
        if (text_width > 0) {
            lv_obj_set_width(ctx->condition_label, text_width);
        }
        lv_obj_set_style_text_font(ctx->condition_label, WEATHER_CONDITION_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_set_hidden(ctx->condition_label, false);
#if APP_UI_TILE_LAYOUT_TUNED
        lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_MID, 0, 36 - (ctx->hide_name ? 28 : 0));
        lv_obj_align(ctx->temp_label, LV_ALIGN_TOP_MID, 0, 76 - (ctx->hide_name ? 28 : 0));
        lv_obj_align(ctx->meta_label, LV_ALIGN_TOP_MID, 0, 130 - (ctx->hide_name ? 28 : 0));
#else
        lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_MID, 0, 34 - (ctx->hide_name ? 28 : 0));
        lv_obj_align(ctx->temp_label, LV_ALIGN_TOP_MID, 0, 72 - (ctx->hide_name ? 28 : 0));
        lv_obj_align(ctx->meta_label, LV_ALIGN_TOP_MID, 0, 124 - (ctx->hide_name ? 28 : 0));
#endif
        lv_label_set_text(ctx->condition_label, ui_i18n_get("weather.unavailable", "Unavailable"));
        lv_label_set_text(ctx->temp_label, "--");
        lv_label_set_text(ctx->meta_label, "");
        weather_hide_lottie(ctx);
        return;
    }

    const char *display_condition = values->condition;
    if (ctx->last_condition_text[0] != '\0') {
        display_condition = ctx->last_condition_text;
    }

    uint32_t icon_cp = ctx->last_icon_cp;
    const lv_font_t *icon_font = NULL;
    bool icon_mode = false;
    if (icon_cp != 0U) {
        icon_font = weather_pick_render_icon_font(card, ctx, icon_cp, ctx->last_icon_font);
    }

    if (icon_cp != 0U && icon_font != NULL) {
        icon_mode = true;
        char icon_utf8[5] = {0};
        if (!weather_icon_utf8_from_codepoint(icon_cp, icon_utf8)) {
            icon_mode = false;
        }

        /* Icon rendering: avoid wrapping and width constraints intended for text. */
        lv_label_set_long_mode(ctx->condition_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(ctx->condition_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(ctx->condition_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->condition_label, icon_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_WEATHER_ICON), LV_PART_MAIN);
        lv_obj_set_style_text_opa(ctx->condition_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(ctx->condition_label, icon_utf8);
        lv_obj_set_hidden(ctx->condition_label, false);
        lv_obj_invalidate(ctx->condition_label);
#if APP_UI_WEATHER_ICON_DEBUG
        lv_coord_t lw = lv_obj_get_width(ctx->condition_label);
        lv_coord_t lh = lv_obj_get_height(ctx->condition_label);
        ESP_LOGI(TAG, "icon font lh=%d label w=%d h=%d text='%s' cp=0x%lX",
            (icon_font != NULL) ? (int)icon_font->line_height : -1, (int)lw, (int)lh, icon_utf8, (unsigned long)icon_cp);
#endif
        lv_obj_move_foreground(ctx->condition_label);
        lv_obj_update_layout(ctx->condition_label);
        lv_obj_update_layout(card);
    }

    if (icon_mode) {
        ctx->last_icon_cp = icon_cp;
        ctx->last_icon_font = icon_font;
        if (ctx->last_condition_text[0] != '\0') {
            display_condition = ctx->last_condition_text;
        }
    }

    if (!icon_mode) {
        lv_coord_t text_width = weather_condition_text_width(card);
        lv_label_set_long_mode(ctx->condition_label, LV_LABEL_LONG_WRAP);
        if (text_width > 0) {
            lv_obj_set_width(ctx->condition_label, text_width);
        }
        lv_obj_set_height(ctx->condition_label, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(ctx->condition_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->condition_label, WEATHER_CONDITION_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_label_set_text(ctx->condition_label, display_condition);
    }

    const char *visual_condition_key = weather_effective_condition_key(ctx, values);
    bool lottie_candidate = weather_has_lottie_for_key(visual_condition_key);
    lv_coord_t lottie_size =
        lottie_candidate ? weather_pick_lottie_size_main_adaptive(card, ctx, temp_font, meta_font) : 0;
    bool visual_icon_mode = icon_mode || lottie_candidate;

    char temp_text[32] = {0};
    if (values->has_temp) {
        snprintf(temp_text, sizeof(temp_text), "%.1f %s", (double)values->temp, values->unit);
    } else {
        snprintf(temp_text, sizeof(temp_text), "--");
    }
    lv_label_set_text(ctx->temp_label, temp_text);

    if (values->humidity >= 0) {
        char meta_text[64] = {0};
        if (visual_icon_mode) {
            snprintf(meta_text, sizeof(meta_text), "%.36s | %d%%", display_condition, values->humidity);
        } else {
            snprintf(meta_text, sizeof(meta_text), ui_i18n_get("weather.humidity_format", "Humidity %d%%"), values->humidity);
        }
        lv_label_set_text(ctx->meta_label, meta_text);
    } else {
        lv_label_set_text(ctx->meta_label, visual_icon_mode ? display_condition : "");
    }

    lv_coord_t condition_y = 0;
    lv_coord_t temp_y = 0;
    lv_coord_t meta_y = 0;
    weather_compute_main_layout(
        card, ctx, visual_icon_mode, icon_font, temp_font, meta_font, lottie_candidate ? lottie_size : 0, &condition_y,
        &temp_y, &meta_y);
    lv_obj_align(ctx->condition_label, LV_ALIGN_TOP_MID, 0, condition_y);
    lv_obj_align(ctx->temp_label, LV_ALIGN_TOP_MID, 0, temp_y);
    lv_obj_align(ctx->meta_label, LV_ALIGN_TOP_MID, 0, meta_y);

    bool lottie_mode = false;
    if (lottie_candidate && lottie_size > 0) {
        lv_coord_t content_w = lv_obj_get_content_width(card);
        if (content_w <= 0) {
            content_w = lv_obj_get_width(card);
        }
        lv_coord_t lottie_x = (content_w - lottie_size) / 2;
        if (lottie_x < 0) {
            lottie_x = 0;
        }
        lv_coord_t min_dim = weather_card_min_dim(card);
        lv_coord_t gap_condition_temp = (min_dim >= 300) ? 14 : 10;
        lv_coord_t slot_top = weather_content_top_y(ctx);
        lv_coord_t slot_bottom = temp_y - gap_condition_temp;
        if (slot_bottom <= slot_top) {
            slot_bottom = slot_top + lottie_size;
        }
        lv_coord_t lottie_y = slot_top;
        lv_coord_t slot_h = slot_bottom - slot_top;
        if (slot_h > lottie_size) {
            lottie_y = slot_top + (slot_h - lottie_size) / 2;
        }
        lv_coord_t content_bottom = weather_content_bottom_y(card, ctx);
        if ((lottie_y + lottie_size) > content_bottom) {
            lottie_y = content_bottom - lottie_size;
        }
        if (lottie_y < 0) {
            lottie_y = 0;
        }
        lottie_mode = weather_show_lottie(card, ctx, visual_condition_key, lottie_x, lottie_y, lottie_size);
    } else {
        weather_hide_lottie(ctx);
    }

    if (lottie_mode) {
        lv_obj_set_hidden(ctx->condition_label, true);
    } else {
        lv_obj_set_hidden(ctx->condition_label, false);
        if (!lottie_candidate) {
            weather_hide_lottie(ctx);
        }
    }

}
