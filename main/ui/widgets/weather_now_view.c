/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_now_view.h"
#include "ui/assets/weather/weather_icons.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_i18n.h"
#include <stdio.h>

void weather_now_hide(weather_now_view_t *view)
{
    if (view->icon) lv_obj_set_hidden(view->icon, true);
    if (view->low) lv_obj_set_hidden(view->low, true);
    if (view->precip) lv_obj_set_hidden(view->precip, true);
}

static void label(lv_obj_t *obj, int width, const lv_font_t *font, const char *text, int x, int y)
{
    lv_obj_set_hidden(obj, false);
    lv_obj_set_size(obj, width, lv_font_get_line_height(font)); /* one line: a text that does not fit ends in dots, it never wraps into the next row */
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(obj, text);
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, x, y);
}

bool weather_now_render(lv_obj_t *card, lv_obj_t *condition, lv_obj_t *temp, lv_obj_t *meta,
                        weather_now_view_t *view, const weather_now_data_t *data)
{
    lv_obj_update_layout(card);
    int width = lv_obj_get_content_width(card), height = lv_obj_get_content_height(card);
    bool large = width >= 228 && height >= 280;
    const lv_image_dsc_t *image = weather_icon_image(
        weather_icon_from_condition(data->condition_key, data->night), large);
    if (!image) return false;

    if (!view->icon) {
        view->icon = lv_image_create(card);
        view->low = lv_label_create(card);
        view->precip = lv_label_create(card);
        lv_obj_set_style_text_color(view->low, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), 0);
        lv_obj_set_style_text_color(view->precip, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), 0);
    }
    weather_now_hide(view); /* newly created optional labels must not retain LVGL's default Text */
    int icon_size = large ? WEATHER_ICON_LARGE_PX : WEATHER_ICON_SMALL_PX;
    bool short_tile = height < 190;
    /* below the one-line name, also when larger text makes its line taller */
    const int below_name = lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 2;
    int icon_y = data->hide_name ? 0 : (below_name > 28 ? below_name : 28);
    lv_obj_set_hidden(view->icon, false);
    lv_image_set_src(view->icon, image);
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, short_tile ? 0 : (width - icon_size) / 2, icon_y);
    char text[96];
    if (data->has_temp) snprintf(text, sizeof(text), "%.1f %s", (double)data->temp, data->unit);
    else snprintf(text, sizeof(text), "--");
    int temp_y = short_tile ? icon_y + 14 : icon_y + icon_size + 4;
    /* rows follow the line heights of the fonts in use, so larger text pushes the rows down instead of overlapping them */
    const lv_font_t *temp_font = large ? ui_font_role(UI_TEXT_KEY) : ui_font_role(UI_TEXT_VALUE);
    const int temp_h = (int)lv_font_get_line_height(temp_font);
    const int meta_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    label(temp, short_tile ? width - icon_size : width, temp_font, text, short_tile ? icon_size : 0, temp_y);
    int meta_y = short_tile ? icon_y + icon_size + 4 : temp_y + temp_h + (large ? 11 : 8);
    lv_obj_set_style_text_color(condition, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), 0);
    label(condition, width, ui_font_role(UI_TEXT_META), data->condition, 0, meta_y);
    lv_obj_set_hidden(meta, true);
    int extra_y = meta_y + meta_h + 1;
    if (height >= extra_y + meta_h - 3) {
        bool rain = data->precip >= 0 || data->precip_prob >= 0;
        int extra_width = data->has_low && rain ? width / 2 : width;
        if (data->has_low) {
            snprintf(text, sizeof(text), "%s %.0f%s", ui_i18n_get("weather.low_short", "Low"),
                     (double)data->low, data->unit);
            label(view->low, extra_width, ui_font_role(UI_TEXT_META), text, 0, extra_y);
        }
        if (rain) {
            if (data->precip >= 0)
                snprintf(text, sizeof(text), "%.1f %s", (double)data->precip,
                         data->precip_unit[0] ? data->precip_unit : "mm");
            else snprintf(text, sizeof(text), "%s %d%%", ui_i18n_get("weather.rain_short", "Rain"), data->precip_prob);
            label(view->precip, extra_width, ui_font_role(UI_TEXT_META), text, data->has_low ? width - extra_width : 0, extra_y);
        }
        if (data->humidity >= 0 && height >= extra_y + 2 * meta_h + 2) {
            snprintf(text, sizeof(text), ui_i18n_get("weather.humidity_format", "Humidity %d%%"), data->humidity);
            label(meta, width, ui_font_role(UI_TEXT_META), text, 0, extra_y + meta_h + 3);
        }
    }
    return true;
}
