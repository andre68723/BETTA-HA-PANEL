/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

static int weather_3day_visible_rows(lv_obj_t *card, lv_coord_t rows_top, int max_rows)
{
    if (card == NULL) {
        return 2;
    }
    lv_coord_t card_h = lv_obj_get_height(card);
    if (card_h <= 0) {
        card_h = lv_obj_get_style_height(card, LV_PART_MAIN);
    }
    if (rows_top < WEATHER_3DAY_ROWS_TOP) {
        rows_top = WEATHER_3DAY_ROWS_TOP;
    }
    lv_coord_t rows_bottom = card_h - WEATHER_3DAY_ROWS_BOTTOM_PAD;
    lv_coord_t available_h = rows_bottom - rows_top;
    if (available_h <= WEATHER_3DAY_ROW_HEIGHT) {
        /* Always keep at least the "Now" row plus one forecast day so the
         * tile still conveys a forecast idea even on a very short card. */
        return 2;
    }
    int fit = (int)((available_h + WEATHER_3DAY_ROW_GAP) / (WEATHER_3DAY_ROW_HEIGHT + WEATHER_3DAY_ROW_GAP));
    if (fit < 2) {
        fit = 2;
    }
    if (fit > max_rows) {
        fit = max_rows;
    }
    return fit;
}

void weather_set_3day_rows_layout(lv_obj_t *card, w_weather_tile_ctx_t *ctx, lv_coord_t requested_rows_top)
{
    if (card == NULL || ctx == NULL) {
        return;
    }

    lv_coord_t card_w = lv_obj_get_width(card);
    lv_coord_t card_h = lv_obj_get_height(card);
    lv_coord_t left = 16;
    lv_coord_t right = 16;
    lv_coord_t content_w = card_w - left - right;
    if (content_w < 120) {
        content_w = 120;
    }

    /* Fixed row metrics -- visible count adapts, spacing does not. */
    const lv_coord_t row_h = WEATHER_3DAY_ROW_HEIGHT;
    const lv_coord_t row_gap = WEATHER_3DAY_ROW_GAP;
    lv_coord_t rows_top = requested_rows_top;
    if (rows_top < WEATHER_3DAY_ROWS_TOP) {
        rows_top = WEATHER_3DAY_ROWS_TOP;
    }
    /* Layouts saved before "Visible days" keep their six rows. */
    const int visible = weather_3day_visible_rows(card, rows_top, ctx->forecast_days < 0 ? WEATHER_COLS_LEGACY_MAX : WEATHER_3DAY_ROWS);
    lv_coord_t rows_bottom = card_h - WEATHER_3DAY_ROWS_BOTTOM_PAD;
    lv_coord_t used_h = (lv_coord_t)(visible * row_h + (visible - 1) * row_gap);
    if (used_h > (rows_bottom - rows_top)) {
        /* Shouldn't happen because weather_3day_visible_rows sized to fit,
         * but guard anyway so we never overflow the card. */
        rows_top = rows_bottom - used_h;
        if (rows_top < WEATHER_3DAY_ROWS_TOP) {
            rows_top = WEATHER_3DAY_ROWS_TOP;
        }
    }

    for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
        weather_3day_row_widgets_t *row = &ctx->rows[i];
        if (row->container == NULL) {
            continue;
        }

        if (i >= visible) {
            lv_obj_set_hidden(row->container, true);
            continue;
        }
        lv_obj_set_hidden(row->container, false);

        lv_coord_t y = rows_top + i * (row_h + row_gap);
        lv_obj_set_pos(row->container, left, y);
        lv_obj_set_size(row->container, content_w, row_h);

        lv_coord_t day_w = 50;
        lv_coord_t icon_w = 28;
        lv_coord_t low_w = 46;
        lv_coord_t high_w = 46;
        lv_coord_t gap = 4;
        lv_coord_t bar_w = content_w - (day_w + icon_w + low_w + high_w + gap * 4);
        if (bar_w < 56) {
            day_w = 42;
            icon_w = 24;
            low_w = 42;
            high_w = 42;
            bar_w = content_w - (day_w + icon_w + low_w + high_w + gap * 4);
            if (bar_w < 40) {
                bar_w = 40;
            }
        }

        lv_coord_t x = 0;
        lv_obj_set_pos(row->day_label, x, 0);
        lv_obj_set_size(row->day_label, day_w, row_h);
        x += day_w + gap;

        lv_obj_set_pos(row->icon_label, x, 0);
        lv_obj_set_size(row->icon_label, icon_w, row_h);
        x += icon_w + gap;

        lv_obj_set_pos(row->low_label, x, 0);
        lv_obj_set_size(row->low_label, low_w, row_h);
        x += low_w + gap;

        lv_coord_t track_h = (row_h >= 24) ? 16 : 14;
        const lv_font_t *text_font = lv_obj_get_style_text_font(row->low_label, LV_PART_MAIN);
        if (text_font == NULL) {
            text_font = WEATHER_3DAY_META_FONT;
        }
        lv_coord_t text_h = (text_font != NULL) ? (lv_coord_t)lv_font_get_line_height(text_font) : row_h;
        lv_coord_t track_y = (text_h - track_h) / 2;
        if (track_y < 0) {
            track_y = 0;
        }
        lv_coord_t max_track_y = row_h - track_h;
        if (track_y > max_track_y) {
            track_y = max_track_y;
        }
        lv_obj_set_pos(row->bar_track, x, track_y);
        lv_obj_set_size(row->bar_track, bar_w, track_h);
        x += bar_w + gap;

        lv_obj_set_pos(row->high_label, x, 0);
        lv_obj_set_size(row->high_label, high_w, row_h);
    }
}

void weather_set_3day_row_values(weather_3day_row_widgets_t *widgets, const weather_3day_row_t *row,
    const char *unit, float range_min, float range_max)
{
    if (widgets == NULL || widgets->container == NULL) {
        return;
    }

    if (row == NULL || !row->valid) {
        lv_obj_set_hidden(widgets->container, true);
        lv_label_set_text(widgets->day_label, "--");
        lv_label_set_text(widgets->low_label, "--");
        lv_label_set_text(widgets->high_label, "--");
        weather_set_row_icon(widgets->icon_label, NULL);
        lv_obj_set_hidden(widgets->bar_fill, true);
        lv_obj_set_hidden(widgets->bar_marker, true);
        return;
    }

    lv_label_set_text(widgets->day_label, row->day[0] != '\0' ? row->day : "--");
    weather_set_row_icon(widgets->icon_label, row->condition_key);

    char low_text[24] = {0};
    if (row->has_low) {
        weather_format_temp(low_text, sizeof(low_text), row->low_temp, unit);
    } else {
        weather_copy_text(low_text, sizeof(low_text), "--");
    }
    lv_label_set_text(widgets->low_label, low_text);

    char high_text[24] = {0};
    if (row->has_high) {
        weather_format_temp(high_text, sizeof(high_text), row->high_temp, unit);
    } else {
        weather_copy_text(high_text, sizeof(high_text), "--");
    }
    lv_label_set_text(widgets->high_label, high_text);

    lv_obj_update_layout(widgets->bar_track);
    lv_coord_t track_w = lv_obj_get_width(widgets->bar_track);
    lv_coord_t track_h = lv_obj_get_height(widgets->bar_track);
    if (track_w <= 0 || track_h <= 0) {
        lv_obj_set_hidden(widgets->bar_fill, true);
        lv_obj_set_hidden(widgets->bar_marker, true);
        return;
    }

    if (row->has_low && row->has_high) {
        float low_temp = row->low_temp;
        float high_temp = row->high_temp;
        if (low_temp > high_temp) {
            float tmp = low_temp;
            low_temp = high_temp;
            high_temp = tmp;
        }

        float start_norm = weather_normalize_temp(low_temp, range_min, range_max);
        float end_norm = weather_normalize_temp(high_temp, range_min, range_max);
        if (end_norm < start_norm) {
            float tmp = end_norm;
            end_norm = start_norm;
            start_norm = tmp;
        }

        lv_coord_t fill_x = (lv_coord_t)(start_norm * (float)track_w + 0.5f);
        lv_coord_t fill_end = (lv_coord_t)(end_norm * (float)track_w + 0.5f);
        if (fill_end <= fill_x) {
            fill_end = fill_x + 2;
        }
        if (fill_x < 0) {
            fill_x = 0;
        }
        if (fill_end > track_w) {
            fill_end = track_w;
        }
        if (fill_end <= fill_x) {
            fill_x = 0;
            fill_end = track_w;
        }

        lv_color_t start_color = weather_3day_temp_color(low_temp, unit);
        lv_color_t end_color = weather_3day_temp_color(high_temp, unit);
        lv_obj_set_style_bg_color(widgets->bar_fill, start_color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(widgets->bar_fill, end_color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(widgets->bar_fill, LV_GRAD_DIR_HOR, LV_PART_MAIN);

        lv_obj_set_pos(widgets->bar_fill, fill_x, 0);
        lv_obj_set_size(widgets->bar_fill, fill_end - fill_x, track_h);
        lv_obj_set_hidden(widgets->bar_fill, false);
    } else {
        lv_obj_set_hidden(widgets->bar_fill, true);
    }

    if (row->has_point) {
        float point_norm = weather_normalize_temp(row->point_temp, range_min, range_max);
        lv_coord_t marker_size = track_h;
        lv_coord_t marker_x = (lv_coord_t)(point_norm * (float)track_w + 0.5f) - marker_size / 2;
        if (marker_x < 0) {
            marker_x = 0;
        }
        if ((marker_x + marker_size) > track_w) {
            marker_x = track_w - marker_size;
        }
        lv_coord_t marker_y = (track_h - marker_size) / 2;
        lv_obj_set_pos(widgets->bar_marker, marker_x, marker_y);
        lv_obj_set_size(widgets->bar_marker, marker_size, marker_size);
        lv_obj_set_hidden(widgets->bar_marker, false);
    } else {
        lv_obj_set_hidden(widgets->bar_marker, true);
    }
}

lv_coord_t weather_content_top_y(const w_weather_tile_ctx_t *ctx)
{
    if (ctx->hide_name) return 4;
    return 34;
}

lv_coord_t weather_content_bottom_y(lv_obj_t *card, const w_weather_tile_ctx_t *ctx)
{
    if (card == NULL) {
        return 0;
    }

    lv_coord_t card_h = lv_obj_get_height(card);
    lv_coord_t bottom = card_h - 16;

    if (ctx != NULL && ctx->show_forecast) {
        lv_coord_t row_y =
            card_h - 102;
        if (row_y < 152) {
            row_y = 152;
        }
        bottom = row_y - 8;
    }

    return (bottom > 0) ? bottom : 0;
}

static lv_coord_t weather_label_height_or_font(lv_obj_t *label, const lv_font_t *fallback_font)
{
    if (label == NULL) {
        return (fallback_font != NULL) ? fallback_font->line_height : 16;
    }

    lv_coord_t h = lv_obj_get_height(label);
    if (h > 0) {
        return h;
    }

    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (font != NULL) {
        return font->line_height;
    }
    if (fallback_font != NULL) {
        return fallback_font->line_height;
    }
    return 16;
}

void weather_compute_main_layout(lv_obj_t *card, w_weather_tile_ctx_t *ctx, bool icon_mode,
    const lv_font_t *icon_font, const lv_font_t *temp_font, const lv_font_t *meta_font,
    lv_coord_t condition_height_override, lv_coord_t *out_condition_y, lv_coord_t *out_temp_y, lv_coord_t *out_meta_y)
{
    if (card == NULL || ctx == NULL || out_condition_y == NULL || out_temp_y == NULL || out_meta_y == NULL) {
        return;
    }

    lv_obj_update_layout(card);

    lv_coord_t top = weather_content_top_y(ctx);
    lv_coord_t bottom = weather_content_bottom_y(card, ctx);
    if (bottom <= top) {
        bottom = top + 1;
    }

    lv_coord_t h_condition = condition_height_override > 0
                                 ? condition_height_override
                                 : weather_label_height_or_font(ctx->condition_label, icon_mode ? icon_font : NULL);
    lv_coord_t h_temp = weather_label_height_or_font(ctx->temp_label, temp_font);
    lv_coord_t h_meta = weather_label_height_or_font(ctx->meta_label, meta_font);

    lv_coord_t gap_condition_temp = icon_mode ? 10 : 8;
    lv_coord_t gap_temp_meta = 8;
    lv_coord_t min_dim = weather_card_min_dim(card);
    if (min_dim >= 300) {
        gap_condition_temp = icon_mode ? 14 : 12;
        gap_temp_meta = 12;
    }
    lv_coord_t content_h = h_condition + gap_condition_temp + h_temp + gap_temp_meta + h_meta;
    lv_coord_t avail_h = bottom - top;

    lv_coord_t y0 = top;
    if (avail_h > content_h) {
        y0 = top + (avail_h - content_h) / 2;
        /* Slight downward bias on larger cards to avoid too much empty space below content. */
        if (min_dim >= 300 && ctx->show_forecast == false) {
            lv_coord_t bias = (avail_h - content_h) / 8;
            y0 += bias;
        }
    }

    lv_coord_t condition_y = y0;
    lv_coord_t temp_y = condition_y + h_condition + gap_condition_temp;
    lv_coord_t meta_y = temp_y + h_temp + gap_temp_meta;

    if ((meta_y + h_meta) > bottom) {
        lv_coord_t overflow = (meta_y + h_meta) - bottom;
        if (overflow > 0) {
            meta_y -= overflow;
            if (meta_y < (temp_y + 4)) {
                meta_y = temp_y + 4;
            }
        }
    }

    if (icon_mode && min_dim >= 300 && !ctx->show_forecast) {
        /* Lift icon-centric layout a bit on large cards so the icon is less "low" visually. */
        lv_coord_t lift = 10;
        if (condition_y > (top + 2)) {
            condition_y -= lift;
            temp_y -= (lift / 2);
            meta_y -= (lift / 2);
            if (condition_y < top) {
                condition_y = top;
            }
            if (temp_y < (condition_y + 4)) {
                temp_y = condition_y + 4;
            }
            if (meta_y < (temp_y + 4)) {
                meta_y = temp_y + 4;
            }
        }
    }

    *out_condition_y = condition_y;
    *out_temp_y = temp_y;
    *out_meta_y = meta_y;
}

lv_coord_t weather_pick_lottie_size_main_adaptive(
    lv_obj_t *card, w_weather_tile_ctx_t *ctx, const lv_font_t *temp_font, const lv_font_t *meta_font)
{
    lv_coord_t fallback = weather_pick_lottie_size(card, ctx);
    if (card == NULL || ctx == NULL) {
        return fallback;
    }

    lv_coord_t top = weather_content_top_y(ctx);
    lv_coord_t bottom = weather_content_bottom_y(card, ctx);
    if (bottom <= top) {
        return fallback;
    }

    lv_obj_update_layout(card);
    lv_coord_t min_dim = weather_card_min_dim(card);
    lv_coord_t h_temp = weather_label_height_or_font(ctx->temp_label, temp_font);
    lv_coord_t h_meta = weather_label_height_or_font(ctx->meta_label, meta_font);
    lv_coord_t gap_condition_temp = (min_dim >= 300) ? 14 : 10;
    lv_coord_t gap_temp_meta = (min_dim >= 300) ? 12 : 8;

    lv_coord_t available_h = bottom - top - h_temp - h_meta - gap_condition_temp - gap_temp_meta;
    lv_coord_t available_w = lv_obj_get_width(card) - 36;
    lv_coord_t max_size = (available_h < available_w) ? available_h : available_w;
    if (max_size <= 0) {
        return fallback;
    }

    lv_coord_t size = max_size - 4;
    if (size < 40) {
        size = max_size;
    }
    if (size > (fallback + 28)) {
        size = fallback + 28;
    }
    if (size > max_size) {
        size = max_size;
    }
    if (size < 40) {
        size = 40;
    }
    return weather_cap_lottie_size(size);
}

/* Night from the local clock (the tile has no sun entity): used to pick the
 * night variant of the clear and partly cloudy icons for the current row. */
bool weather_is_night_now(void)
{
    time_t now = time(NULL);
    if (now < 1700000000) { /* clock not set yet */
        return false;
    }
    struct tm tm_now = {0};
    if (localtime_r(&now, &tm_now) == NULL) {
        return false;
    }
    return tm_now.tm_hour < 6 || tm_now.tm_hour >= 21;
}

/* The horizontal view needs the PNG icon set and a tile of some size;
 * small tiles keep the compact rows. */
bool weather_columns_wanted(lv_obj_t *card)
{
#if APP_HAVE_WEATHER_PNG_ICONS
    if (card == NULL) {
        return false;
    }
    lv_obj_update_layout(card); /* the size reads 0 until the first layout pass */
    /* Down to the P4 minimum height: the day cards size themselves from the tile, while the
     * compact row view below uses positions for a large tile and overlapped on a short one. */
    return lv_obj_get_width(card) >= 250 && lv_obj_get_height(card) >= 200;
#else
    (void)card;
    return false;
#endif
}

static lv_obj_t *weather_col_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "--");
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void weather_columns_build(lv_obj_t *card, w_weather_tile_ctx_t *ctx)
{
    if (ctx->cols_built) {
        return;
    }
    ctx->report_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->report_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->report_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->report_label, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(ctx->report_label, true);
    lv_obj_set_clickable(ctx->report_label, true);
    lv_obj_add_event_cb(ctx->report_label, weather_report_clicked_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_set_clickable(ctx->temp_label, true);
    lv_obj_add_event_cb(ctx->temp_label, weather_report_clicked_cb, LV_EVENT_CLICKED, ctx);
    ctx->col_main_icon = lv_image_create(card);
    lv_obj_set_clickable(ctx->col_main_icon, false);
    lv_obj_set_hidden(ctx->col_main_icon, true);
    for (int i = 0; i < WEATHER_COLS_MAX; i++) {
        weather_col_widgets_t *col = &ctx->cols[i];
        col->box = lv_obj_create(card);
        lv_obj_set_scrollable(col->box, false);
        lv_obj_set_clickable(col->box, true);
        lv_obj_set_user_data(col->box, (void *)(intptr_t)i);
        lv_obj_add_event_cb(col->box, weather_col_clicked_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_set_style_border_width(col->box, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col->box, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(col->box, ui_style_radius(14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(col->box, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(col->box,
            lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 22),
            LV_PART_MAIN);
        col->day = weather_col_label(col->box, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_SOFT));
        col->icon = lv_image_create(col->box);
        lv_obj_set_clickable(col->icon, false);
        col->hi = weather_col_label(col->box, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
        col->lo = weather_col_label(col->box, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED));
        col->rain = weather_col_label(col->box, ui_font_role(UI_TEXT_META), lv_color_hex(0x5AB4F0));
        lv_obj_set_hidden(col->box, true);
    }
    ctx->cols_built = true;
}

void weather_columns_hide(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->cols_built) {
        return;
    }
    lv_obj_set_hidden(ctx->col_main_icon, true);
    lv_obj_set_hidden(ctx->report_label, true);
    for (int i = 0; i < WEATHER_COLS_MAX; i++) {
        lv_obj_set_hidden(ctx->cols[i].box, true);
    }
}

void weather_set_image_if_changed(lv_obj_t *img, const lv_image_dsc_t *dsc)
{
    if (img != NULL && dsc != NULL && lv_image_get_src(img) != (const void *)dsc) {
        lv_image_set_src(img, dsc);
    }
}

/* Rain chance and amount as up to two lines; returns the line count (0 = nothing to show). */
int weather_set_col_rain(lv_obj_t *label, const weather_3day_row_t *row, const char *unit, int max_lines, lv_coord_t width)
{
    (void)width;
    char text[40] = "";
    int lines = 0;
    size_t len = 0;
    if (row->precip_prob >= 0 && max_lines > 0) {
        len += (size_t)snprintf(text + len, sizeof(text) - len, "%d %%", row->precip_prob);
        lines++;
    }
    if (row->precip_mm >= 0.0f && (lines < max_lines)) {
        /* Wider reviewed columns retain a fractional amount, e.g. 0.2 mm. */
        int precision = 0;
#if UI_TEXT_ADAPTIVE
        if (width >= 100 && fabsf(row->precip_mm - roundf(row->precip_mm)) > 0.01f) precision = 1;
#endif
        snprintf(text + len, sizeof(text) - len, "%s%.*f %s", lines > 0 ? "\n" : "",
                 precision, (double)row->precip_mm, unit);
        lines++;
    }
    lv_label_set_text(label, text);
    return lines;
}

void weather_set_col_temp(lv_obj_t *label, bool has, float value)
{
    char text[16];
    if (has) {
        snprintf(text, sizeof(text), "%.0f\xC2\xB0", (double)value);
    } else {
        snprintf(text, sizeof(text), "--");
    }
    lv_label_set_text(label, text);
}
