/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "ui/theme/ui_typography.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "ha/ha_datetime_value.h"

/* Shared weather/solar grid. Callers use the same outer x/width and 14 px
 * padding to align dates. Rest is a summary, never a dated slot. */
static inline int ui_day_grid_count(lv_coord_t width, int requested, int maximum)
{
    if (requested > 0) return requested < maximum ? requested : maximum;
    lv_point_t high, low;
    lv_text_get_size(&high, "-12°", ui_font_role(UI_TEXT_VALUE), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&low, "-18°", ui_font_role(UI_TEXT_VALUE), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int count = (width + 6) / (high.x + low.x + 20 + 6);
    return count < 1 ? 1 : (count > maximum ? maximum : count);
}

static inline lv_coord_t ui_day_grid_width(lv_coord_t width, int count)
{
    return (width - 6 * (count - 1)) / count;
}

static inline lv_coord_t ui_day_grid_x(lv_coord_t width, int count, int index)
{
    return index * (ui_day_grid_width(width, count) + 6);
}

/* Local calendar arithmetic survives DST transitions; adding 86400 seconds does not. */
static inline void ui_day_grid_date(int offset, char date[11])
{
    time_t now = time(NULL); struct tm local = {0};
    if (now < 1600000000 || !localtime_r(&now, &local)) { date[0] = '\0'; return; }
    local.tm_mday += offset; local.tm_hour = 12; local.tm_isdst = -1;
    mktime(&local); strftime(date, 11, "%Y-%m-%d", &local);
}

static inline void ui_day_grid_local_date(const char *source, char date[11])
{
    ha_datetime_value_t local;
    if (source && strlen(source) > 10 && ha_datetime_parse("datetime.forecast", source, "{}", &local))
        snprintf(date, 11, "%04d-%02d-%02d", local.year, local.month, local.day);
    else snprintf(date, 11, "%.10s", source ? source : "");
}
