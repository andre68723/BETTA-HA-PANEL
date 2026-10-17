/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "simulator_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui/ui_pages.h"
#include "ui/widgets/weather_condition_icon.h"

static const char *field(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
}

extern bool g_sim_mqtt;

void simulator_frame_apply(const cJSON *frame)
{
    g_sim_mqtt = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "mqtt"));
    const cJSON *left_items = cJSON_GetObjectItemCaseSensitive(frame, "left_items");
    if (cJSON_IsArray(left_items)) {
        const cJSON *first = cJSON_GetArrayItem(left_items, 0);
        const cJSON *second = cJSON_GetArrayItem(left_items, 1);
        const cJSON *third = cJSON_GetArrayItem(left_items, 2);
        ui_pages_set_topbar_left_slots(cJSON_IsString(first) ? first->valuestring : "",
                                      cJSON_IsString(second) ? second->valuestring : "",
                                      cJSON_IsString(third) ? third->valuestring : "");
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int day, month, year, hour, minute;
    if (sscanf(field(frame, "date"), "%d%*[.-]%d%*[.-]%d", &day, &month, &year) == 3) {
        tm_now.tm_mday = day;
        tm_now.tm_mon = month - 1;
        tm_now.tm_year = year - 1900;
    }
    if (sscanf(field(frame, "time"), "%d:%d", &hour, &minute) == 2) {
        tm_now.tm_hour = hour;
        tm_now.tm_min = minute;
    }
    ui_pages_set_topbar_datetime(&tm_now);
    ui_pages_set_topbar_status(true, false, true, true);
    ui_pages_set_crash_warning(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(frame, "crash_warning")));

    const char *weather = field(frame, "weather_temperature");
    if (weather[0] != '\0') {
        uint32_t icon = weather_icon_codepoint_for_key(field(frame, "weather_condition"));
        ui_pages_set_topbar_weather(true, strtof(weather, NULL), "°C", icon);
    }
    const cJSON *stocks = cJSON_GetObjectItemCaseSensitive(frame, "stocks");
    ui_pages_stock_item_t items[4];
    size_t count = 0;
    const cJSON *stock = NULL;
    cJSON_ArrayForEach(stock, stocks) {
        if (count >= sizeof(items) / sizeof(items[0])) break;
        memset(&items[count], 0, sizeof(items[count]));
        snprintf(items[count].symbol, sizeof(items[count].symbol), "%s", field(stock, "symbol"));
        snprintf(items[count].value, sizeof(items[count].value), "%s", field(stock, "value"));
        items[count].value_is_euro = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(stock, "euro"));
        count++;
    }
    if (count > 0) {
        /* Fit the quote against the settled clock width, as on a running panel. */
        lv_obj_update_layout(lv_screen_active());
        ui_pages_set_topbar_stocks(items, count);
    }
}
