/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

static float weather_clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static bool weather_unit_is_fahrenheit(const char *unit)
{
    if (unit == NULL || unit[0] == '\0') {
        return false;
    }

    for (size_t i = 0; unit[i] != '\0'; i++) {
        char c = unit[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        if (c == 'F') {
            return true;
        }
    }
    return false;
}

static float weather_temp_from_celsius(float celsius, const char *unit)
{
    if (weather_unit_is_fahrenheit(unit)) {
        return (celsius * 9.0f / 5.0f) + 32.0f;
    }
    return celsius;
}

static float weather_3day_scale_min(const char *unit)
{
    return weather_temp_from_celsius(WEATHER_3DAY_SCALE_MIN_C, unit);
}

static float weather_3day_scale_mix(const char *unit)
{
    return weather_temp_from_celsius(WEATHER_3DAY_SCALE_MIX_C, unit);
}

static float weather_3day_scale_warm(const char *unit)
{
    return weather_temp_from_celsius(WEATHER_3DAY_SCALE_WARM_C, unit);
}

static float weather_3day_scale_max(const char *unit)
{
    return weather_temp_from_celsius(WEATHER_3DAY_SCALE_MAX_C, unit);
}

static lv_color_t weather_color_lerp(lv_color_t a, lv_color_t b, float t)
{
    t = weather_clampf(t, 0.0f, 1.0f);
    uint8_t r = (uint8_t)((float)a.red + ((float)b.red - (float)a.red) * t + 0.5f);
    uint8_t g = (uint8_t)((float)a.green + ((float)b.green - (float)a.green) * t + 0.5f);
    uint8_t bl = (uint8_t)((float)a.blue + ((float)b.blue - (float)a.blue) * t + 0.5f);
    return lv_color_make(r, g, bl);
}

lv_color_t weather_3day_temp_color(float value, const char *unit)
{
    float t0 = weather_3day_scale_min(unit);
    float t1 = weather_3day_scale_mix(unit);
    float t2 = weather_3day_scale_warm(unit);
    float t3 = weather_3day_scale_max(unit);

    lv_color_t c0 = lv_color_hex(WEATHER_3DAY_FILL_COLD);
    lv_color_t c1 = lv_color_hex(WEATHER_3DAY_FILL_MIXED);
    lv_color_t c2 = lv_color_hex(WEATHER_3DAY_FILL_WARM);
    lv_color_t c3 = lv_color_hex(WEATHER_3DAY_FILL_HOT);

    value = weather_clampf(value, t0, t3);

    if (value <= t1) {
        float denom = (t1 - t0);
        float ratio = (denom > 0.001f) ? ((value - t0) / denom) : 0.0f;
        return weather_color_lerp(c0, c1, ratio);
    }
    if (value <= t2) {
        float denom = (t2 - t1);
        float ratio = (denom > 0.001f) ? ((value - t1) / denom) : 0.0f;
        return weather_color_lerp(c1, c2, ratio);
    }

    float denom = (t3 - t2);
    float ratio = (denom > 0.001f) ? ((value - t2) / denom) : 0.0f;
    return weather_color_lerp(c2, c3, ratio);
}

float weather_normalize_temp(float value, float range_min, float range_max)
{
    float span = range_max - range_min;
    if (span < 0.001f) {
        return 0.5f;
    }
    float norm = (value - range_min) / span;
    return weather_clampf(norm, 0.0f, 1.0f);
}

void weather_format_temp(char *dst, size_t dst_size, float temp, const char *unit)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    const char *temp_unit = (unit != NULL && unit[0] != '\0') ? unit : "C";
    snprintf(dst, dst_size, "%.0f%s", (double)temp, temp_unit);
}

void weather_set_row_icon(lv_obj_t *label, const char *condition_key)
{
    if (label == NULL) {
        return;
    }

    uint32_t cp = weather_icon_codepoint_for_key(condition_key);
    const lv_font_t *font = mdi_font_weather_20();
    if (font == NULL) {
        font = mdi_font_weather_small();
    }
    if (!weather_font_has_codepoint(font, cp)) {
        font = weather_find_icon_font_for_cp(cp);
    }

    char utf8[5] = {0};
    if (cp != 0U && font != NULL && weather_icon_utf8_from_codepoint(cp, utf8)) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_transform_zoom(label, 256, LV_PART_MAIN);
        lv_label_set_text(label, utf8);
        return;
    }

    lv_obj_set_style_text_font(label, WEATHER_3DAY_META_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_transform_zoom(label, 256, LV_PART_MAIN);
    lv_label_set_text(label, "-");
}

void weather_build_3day_rows(const weather_values_t *values, weather_3day_row_t rows[WEATHER_3DAY_ROWS])
{
    if (values == NULL || rows == NULL) {
        return;
    }

    memset(rows, 0, sizeof(weather_3day_row_t) * WEATHER_3DAY_ROWS);

    weather_3day_row_t *current = &rows[0];
    current->valid = true;
    weather_copy_text(current->day, sizeof(current->day), ui_i18n_get("weather.now", "Now"));
    /* "Now" must reflect the live weather entity state, not today's forecast summary. */
    weather_copy_text(current->condition_key, sizeof(current->condition_key),
        values->condition_key[0] != '\0' ? values->condition_key : values->today_condition_key);

    if (values->today_has_low) {
        current->has_low = true;
        current->low_temp = values->today_low_temp;
    }
    if (values->today_has_high) {
        current->has_high = true;
        current->high_temp = values->today_high_temp;
    }

    if (!current->has_low && values->has_temp) {
        current->has_low = true;
        current->low_temp = values->temp;
    }
    if (!current->has_high && values->has_temp) {
        current->has_high = true;
        current->high_temp = values->temp;
    }

    if (current->has_low && !current->has_high) {
        current->has_high = true;
        current->high_temp = current->low_temp;
    } else if (!current->has_low && current->has_high) {
        current->has_low = true;
        current->low_temp = current->high_temp;
    }

    if (values->has_temp) {
        current->has_point = true;
        current->point_temp = values->temp;
    }
    weather_copy_text(current->condition_text, sizeof(current->condition_text), values->condition);
    weather_copy_text(current->date, sizeof(current->date), values->today_date);
    current->precip_mm = values->today_precip_mm;
    current->precip_prob = values->today_precip_prob;

    for (int i = 0; i < WEATHER_3DAY_MAX_FORECAST; i++) {
        const weather_forecast_t *src = &values->forecast[i];
        weather_3day_row_t *dst = &rows[i + 1];
        if (!src->valid) {
            continue;
        }

        dst->valid = true;
        weather_copy_text(dst->condition_text, sizeof(dst->condition_text), src->condition);
        weather_copy_text(dst->date, sizeof(dst->date), src->date);
        dst->precip_mm = src->precip_mm;
        dst->precip_prob = src->precip_prob;
        dst->has_low = src->has_low;
        dst->has_high = src->has_high;
        dst->low_temp = src->low_temp;
        dst->high_temp = src->high_temp;
        weather_copy_text(dst->day, sizeof(dst->day), src->day);
        weather_copy_text(dst->condition_key, sizeof(dst->condition_key),
            src->condition_key[0] != '\0' ? src->condition_key : values->condition_key);

        if (dst->has_low && !dst->has_high) {
            dst->has_high = true;
            dst->high_temp = dst->low_temp;
        } else if (!dst->has_low && dst->has_high) {
            dst->has_low = true;
            dst->low_temp = dst->high_temp;
        }
    }
}

void weather_compute_3day_range(
    const weather_3day_row_t rows[WEATHER_3DAY_ROWS], float *out_min, float *out_max)
{
    if (rows == NULL || out_min == NULL || out_max == NULL) {
        return;
    }

    bool has_any = false;
    float min_temp = 0.0f;
    float max_temp = 0.0f;

    for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
        const weather_3day_row_t *row = &rows[i];
        if (!row->valid) {
            continue;
        }

        if (row->has_low) {
            if (!has_any || row->low_temp < min_temp) {
                min_temp = row->low_temp;
            }
            if (!has_any || row->low_temp > max_temp) {
                max_temp = row->low_temp;
            }
            has_any = true;
        }
        if (row->has_high) {
            if (!has_any || row->high_temp < min_temp) {
                min_temp = row->high_temp;
            }
            if (!has_any || row->high_temp > max_temp) {
                max_temp = row->high_temp;
            }
            has_any = true;
        }
        if (row->has_point) {
            if (!has_any || row->point_temp < min_temp) {
                min_temp = row->point_temp;
            }
            if (!has_any || row->point_temp > max_temp) {
                max_temp = row->point_temp;
            }
            has_any = true;
        }
    }

    if (!has_any) {
        *out_min = 0.0f;
        *out_max = 1.0f;
        return;
    }

    float span = max_temp - min_temp;
    if (span < 1.0f) {
        float mid = (max_temp + min_temp) * 0.5f;
        min_temp = mid - 0.5f;
        max_temp = mid + 0.5f;
    }

    *out_min = min_temp;
    *out_max = max_temp;
}
