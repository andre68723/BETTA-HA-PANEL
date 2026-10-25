/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Energy price forecast widget: what Home Assistant's price list becomes on the tile, the Today/Tomorrow buttons, the states without prices and the
 * tile's own timer. The list is built like the real EPEX Spot attribute (public market data of 2026-10-17) around the simulator's local today. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ha/ha_attr_serialize.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_widget_factory.h"

esp_err_t w_energy_price_forecast_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_energy_price_forecast_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_energy_price_forecast_mark_unavailable(ui_widget_instance_t *);

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "price line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static const double TODAY[24] = {0.20038, 0.18343, 0.16618, 0.1524, 0.14999, 0.15302, 0.15022, 0.16521, 0.18674, 0.17057, 0.16114, 0.14435,
                                 0.12601, 0.12019, 0.12566, 0.14447, 0.16694, 0.19895, 0.21843, 0.23493, 0.20066, 0.1878, 0.17073, 0.16003};
static const double TOMORROW[24] = {0.149, 0.14663, 0.14779, 0.14263, 0.14735, 0.15199, 0.14114, 0.15642, 0.18276, 0.20084, 0.19449, 0.1901,
                                    0.18337, 0.16312, 0.16192, 0.16977, 0.19086, 0.21263, 0.22627, 0.2206, 0.19828, 0.17955, 0.17661, 0.17101};

static void advance(unsigned ms) { (void)ms; lv_obj_update_layout(lv_screen_active()); lv_timer_handler(); lv_timer_handler(); }

/* The first label under root whose text starts with prefix. */
static lv_obj_t *find_prefix(lv_obj_t *root, const char *prefix)
{
    if (lv_obj_check_type(root, &lv_label_class) && !lv_obj_is_hidden(root)) {
        const char *text = lv_label_get_text(root);
        if (text != NULL && strncmp(text, prefix, strlen(prefix)) == 0) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_prefix(lv_obj_get_child(root, i), prefix);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *find_chart(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_chart_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *chart = find_chart(lv_obj_get_child(root, i));
        if (chart != NULL) return chart;
    }
    return NULL;
}

static void apply_attrs(ui_widget_instance_t *widget, const char *entity, const char *state, cJSON *attrs)
{
    ha_state_t s = {0};
    snprintf(s.entity_id, sizeof(s.entity_id), "%s", entity);
    snprintf(s.state, sizeof(s.state), "%s", state);
    if (attrs != NULL && ha_client_attrs_is_price_series(attrs)) {
        CHECK(ha_client_serialize_price_series_compact(attrs, s.attributes_json, sizeof(s.attributes_json)));
    } else {
        snprintf(s.attributes_json, sizeof(s.attributes_json), "{}");
    }
    sim_host_state_add(&s);
    w_energy_price_forecast_apply_state(widget, &s);
    advance(50);
}

static time_t day_start(time_t now, int offset)
{
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_mday += offset;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static cJSON *list_attrs(int days)
{
    const time_t midnight = day_start(time(NULL), 0), tomorrow = day_start(midnight, 1), end = day_start(midnight, days);
    cJSON *attrs = cJSON_CreateObject();
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "EUR/kWh");
    cJSON *data = cJSON_AddArrayToObject(attrs, "data");
    for (time_t start = midnight; start < end; start += 3600) {
        struct tm lt;
        localtime_r(&start, &lt);
        char text[40];
        size_t n = strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &lt);
        const long off = lt.tm_gmtoff;
        snprintf(text + n, sizeof(text) - n, "%c%02ld:%02ld", off < 0 ? '-' : '+', labs(off) / 3600, (labs(off) % 3600) / 60);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "start_time", text);
        const int hour = (int)((start - (start < tomorrow ? midnight : tomorrow)) / 3600);
        cJSON_AddNumberToObject(item, "price_per_kwh", (start < tomorrow ? TODAY : TOMORROW)[hour % 24]);
        cJSON_AddItemToArray(data, item);
    }
    return attrs;
}

/* Each case gets a screen of its own: tiles created next to each other on one screen were laid out with no size here. */
static void fresh_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_screen_load(screen);
    advance(20);
}

static void calendar_cases(void)
{
    /* Independent Berlin epochs/prices: normal day, spring jump, both autumn 02 hours and after the rollback. */
    static const struct {
        time_t now, midnight, tomorrow;
        int hours;
        const char *price, *slot, *tomorrow_iso;
    } cases[] = {
        {1792058400, 1792015200, 1792101600, 24, "12,6", "12:00-13:00", "2026-10-16T00:00:00+02:00"},
        {1774747800, 1774738800, 1774821600, 23, "16,6", "03:00-04:00", "2026-03-30T00:00:00+02:00"},
        {1792888200, 1792879200, 1792969200, 25, "16,6", "02:00-02:00", "2026-10-26T00:00:00+01:00"},
        {1792891800, 1792879200, 1792969200, 25, "15,2", "02:00-03:00", "2026-10-26T00:00:00+01:00"},
        {1792899000, 1792879200, 1792969200, 25, "15,3", "04:00-05:00", "2026-10-26T00:00:00+01:00"},
    };
    const time_t saved = sim_clock_fixed() ? time(NULL) : 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sim_clock_set(cases[i].now);
        CHECK(day_start(time(NULL), 0) == cases[i].midnight && day_start(time(NULL), 1) == cases[i].tomorrow);
        fresh_screen();
        ui_widget_def_t def = {.w = 700, .h = 400, .price_selector = true, .price_cheap_times = true};
        strcpy(def.type, "energy_price_forecast");
        strcpy(def.entity_id, "sensor.price");
        strcpy(def.title, "Price");
        ui_widget_instance_t widget = {0};
        CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
        lv_obj_update_layout(widget.obj); /* fixed virtual time does not wait for a display refresh */
        cJSON *attrs = list_attrs(2);
        cJSON *data = cJSON_GetObjectItemCaseSensitive(attrs, "data");
        CHECK(cJSON_GetArraySize(data) == cases[i].hours + 24);
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(data, cases[i].hours), "start_time")->valuestring,
                     cases[i].tomorrow_iso) == 0);
        apply_attrs(&widget, "sensor.price", "0.2", attrs);
        CHECK(find_prefix(widget.obj, cases[i].price) != NULL && find_prefix(widget.obj, cases[i].slot) != NULL);
        CHECK(find_prefix(widget.obj, "min 12,0  max 23,5") != NULL);
        lv_obj_send_event(find_prefix(widget.obj, "Tomorrow"), LV_EVENT_CLICKED, NULL);
        advance(50);
        CHECK(find_prefix(widget.obj, "00:00-01:00") != NULL && find_prefix(widget.obj, "14,9") != NULL);
        CHECK(find_prefix(widget.obj, "min 14,1  max 22,6") != NULL);
        cJSON_Delete(attrs);
        lv_obj_delete(widget.obj);
    }
    sim_clock_set(saved);
    fresh_screen();
}

int sim_price_regression(void)
{
    sim_host_set_online(true);
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    calendar_cases();
    ui_widget_def_t def = {.w = 700, .h = 400, .price_selector = true, .price_cheap_times = true};
    strcpy(def.type, "energy_price_forecast");
    strcpy(def.entity_id, "sensor.price");
    strcpy(def.title, "Price");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    advance(50);

    /* nothing received yet: no prices, no bars */
    CHECK(find_prefix(widget.obj, "No prices") != NULL);

    /* today and tomorrow: the price of the running hour, the day buttons, minimum and maximum, a cheap window */
    cJSON *attrs = list_attrs(2);
    apply_attrs(&widget, "sensor.price", "0.2", attrs);
    CHECK(find_prefix(widget.obj, "No prices") == NULL);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char want[16];
    const int elapsed_hour = (int)((now - day_start(now, 0)) / 3600);
    snprintf(want, sizeof(want), "%.1f", TODAY[elapsed_hour % 24] * 100.0);
    for (char *p = want; *p; p++) if (*p == '.') *p = ',';
    lv_obj_t *price = find_prefix(widget.obj, want);
    CHECK(price != NULL);
    char slot[16];
    time_t next_hour = now + 3600;
    struct tm end_tm;
    localtime_r(&next_hour, &end_tm);
    snprintf(slot, sizeof(slot), "%02d:00-%02d:00", tm.tm_hour, end_tm.tm_hour);
    CHECK(find_prefix(widget.obj, slot) != NULL);
    CHECK(find_prefix(widget.obj, "min 12,0") != NULL);
    lv_obj_t *tomorrow = find_prefix(widget.obj, "Tomorrow");
    lv_obj_t *today = find_prefix(widget.obj, "Today");
    CHECK(tomorrow != NULL && today != NULL);
    const lv_color_t selected_day = lv_obj_get_style_bg_color(today, LV_PART_MAIN);
    const lv_color_t other_day = lv_obj_get_style_bg_color(tomorrow, LV_PART_MAIN);
    CHECK(selected_day.red != other_day.red || selected_day.green != other_day.green || selected_day.blue != other_day.blue);
    CHECK(find_prefix(widget.obj, "Cheap") != NULL || find_prefix(widget.obj, "No cheap window left") != NULL);

    /* tomorrow: like the card, the header names the day's first interval (no running one), the range is tomorrow's */
    lv_obj_send_event(tomorrow, LV_EVENT_CLICKED, NULL);
    advance(50);
    const lv_color_t selected_tomorrow = lv_obj_get_style_bg_color(tomorrow, LV_PART_MAIN);
    CHECK(selected_tomorrow.red == selected_day.red && selected_tomorrow.green == selected_day.green && selected_tomorrow.blue == selected_day.blue);
    CHECK(find_prefix(widget.obj, "00:00-01:00") != NULL && find_prefix(widget.obj, "14,9") != NULL); /* 0.149 */
    CHECK(find_prefix(widget.obj, "min 14,1") != NULL); /* 0.14114 */
    lv_obj_send_event(find_prefix(widget.obj, "Today"), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(widget.obj, slot) != NULL && find_prefix(widget.obj, "min 12,0") != NULL);

    /* only today published: tomorrow says so, then today again */
    cJSON_Delete(attrs);
    attrs = list_attrs(1);
    apply_attrs(&widget, "sensor.price", "0.2", attrs);
    lv_obj_send_event(find_prefix(widget.obj, "Tomorrow"), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(widget.obj, "Tomorrow not published yet") != NULL);
    CHECK(find_prefix(widget.obj, "00:00-01:00") == NULL);
    lv_obj_send_event(find_prefix(widget.obj, "Today"), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(widget.obj, "Tomorrow not published yet") == NULL && find_prefix(widget.obj, slot) != NULL);
    cJSON_Delete(attrs);

    /* a state without a usable list, then an unavailable entity */
    apply_attrs(&widget, "sensor.price", "0.2", NULL);
    CHECK(find_prefix(widget.obj, "No prices") != NULL);
    attrs = list_attrs(2);
    apply_attrs(&widget, "sensor.price", "0.2", attrs);
    CHECK(find_prefix(widget.obj, "No prices") == NULL);
    w_energy_price_forecast_mark_unavailable(&widget);
    advance(50);
    CHECK(find_prefix(widget.obj, "No prices") != NULL);
    cJSON_Delete(attrs);

    /* a narrow tile has one button that names the day and switches it */
    ui_widget_def_t narrow_def = {.w = 300, .h = 220, .price_selector = true};
    strcpy(narrow_def.type, "energy_price_forecast");
    strcpy(narrow_def.entity_id, "sensor.price");
    strcpy(narrow_def.title, "Narrow");
    fresh_screen();
    ui_widget_instance_t narrow = {0};
    CHECK(ui_widget_factory_create(&narrow_def, lv_screen_active(), &narrow) == ESP_OK);
    lv_obj_update_layout(narrow.obj);
    advance(50);
    attrs = list_attrs(2);
    apply_attrs(&narrow, "sensor.price", "0.2", attrs);
    CHECK(find_prefix(narrow.obj, "Today") != NULL && find_prefix(narrow.obj, "Tomorrow") == NULL);
    lv_obj_send_event(find_prefix(narrow.obj, "Today"), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(narrow.obj, "Tomorrow") != NULL && find_prefix(narrow.obj, "00:00-01:00") != NULL);
    lv_obj_send_event(find_prefix(narrow.obj, "Tomorrow"), LV_EVENT_CLICKED, NULL);
    advance(50);
    /* just after midnight today runs the slot that tomorrow starts with, so the text cannot tell the days apart then */
    CHECK(find_prefix(narrow.obj, "Today") != NULL && (find_prefix(narrow.obj, "00:00-01:00") == NULL || strcmp(slot, "00:00-01:00") == 0));
    cJSON_Delete(attrs);

    /* the options default to off, as in the card: no day buttons, today only, no cheap line, the range still shown */
    ui_widget_def_t plain_def = {.w = 700, .h = 400};
    strcpy(plain_def.type, "energy_price_forecast");
    strcpy(plain_def.entity_id, "sensor.price");
    strcpy(plain_def.title, "Plain");
    fresh_screen();
    ui_widget_instance_t plain = {0};
    CHECK(ui_widget_factory_create(&plain_def, lv_screen_active(), &plain) == ESP_OK);
    lv_obj_update_layout(plain.obj);
    advance(50);
    attrs = list_attrs(2);
    apply_attrs(&plain, "sensor.price", "0.2", attrs);
    CHECK(find_prefix(plain.obj, "Today") == NULL && find_prefix(plain.obj, "Tomorrow") == NULL);
    CHECK(find_prefix(plain.obj, "Cheap") == NULL && find_prefix(plain.obj, "No cheap window left") == NULL);
    /* without the day buttons the view is the 24 hours around now: the intervals from 11 hours before to 12 hours after the running one */
    double lo = 1e9, hi = -1e9;
    const int today_hours = (int)((day_start(now, 1) - day_start(now, 0)) / 3600);
    const int total_hours = (int)((day_start(now, 2) - day_start(now, 0)) / 3600);
    for (int idx = elapsed_hour - 11; idx <= elapsed_hour + 12; idx++) {
        if (idx < 0 || idx >= total_hours) continue;
        const double v = idx < today_hours ? TODAY[idx % 24] : TOMORROW[(idx - today_hours) % 24];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    char range[32];
    snprintf(range, sizeof(range), "min %.1f  max %.1f", lo * 100.0, hi * 100.0);
    for (char *p = range; *p; p++) if (*p == '.') *p = ',';
    CHECK(find_prefix(plain.obj, range) != NULL);
    CHECK(find_prefix(plain.obj, slot) != NULL);

    /* an average sensor in secondary_entity_id: a usable state is taken, a broken one falls back to the day's average, neither disturbs the tile */
    ui_widget_def_t avg_def = {.w = 700, .h = 400};
    strcpy(avg_def.type, "energy_price_forecast");
    strcpy(avg_def.entity_id, "sensor.price");
    strcpy(avg_def.secondary_entity_id, "sensor.price_avg");
    strcpy(avg_def.title, "Average");
    avg_def.price_average = 0.25f;
    fresh_screen();
    ui_widget_instance_t avg = {0};
    CHECK(ui_widget_factory_create(&avg_def, lv_screen_active(), &avg) == ESP_OK);
    lv_obj_update_layout(avg.obj);
    advance(50);
    apply_attrs(&avg, "sensor.price", "0.2", attrs);
    apply_attrs(&avg, "sensor.price_avg", "0.163377", NULL);
    CHECK(find_prefix(avg.obj, slot) != NULL);
    apply_attrs(&avg, "sensor.price_avg", "unavailable", NULL);
    apply_attrs(&avg, "sensor.price_avg", "16.3", NULL); /* cent instead of EUR: not used */
    CHECK(find_prefix(avg.obj, slot) != NULL && find_prefix(avg.obj, range) != NULL);
    cJSON_Delete(attrs);

    /* the stock chip's popup: 30 days of daily means, a tap on the second button shows 12 months of weekly means */
    fresh_screen();
    ui_history_popup_open_range("sensor.quote", "SAP.DE", "199.22 EUR");
    lv_obj_update_layout(lv_layer_top());
    advance(50);
    lv_obj_t *top = lv_layer_top();
    CHECK(find_prefix(top, "SAP.DE") != NULL && find_prefix(top, "30 d") != NULL && find_prefix(top, "12 m") != NULL);
    CHECK(find_prefix(top, "Daily means") != NULL && find_prefix(top, "199.22 EUR") != NULL);
    lv_obj_t *year = find_prefix(top, "12 m");
    lv_obj_send_event(lv_obj_get_parent(year), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(top, "Weekly means") != NULL && find_prefix(top, "Daily means") == NULL);
    lv_obj_send_event(lv_obj_get_parent(find_prefix(top, "30 d")), LV_EVENT_CLICKED, NULL);
    advance(50);
    CHECK(find_prefix(top, "Daily means") != NULL);

    /* A parsed sparse month is drawn at elapsed-time X coordinates and leaves a real hole in the line. */
    const int64_t begin = 1792188000LL;
    ha_stats_window_t window = {.start_epoch = begin, .end_epoch = ha_stats_local_day_start(begin, 30)};
    cJSON *reply = cJSON_CreateObject();
    cJSON *rows = cJSON_AddArrayToObject(reply, "sensor.quote");
    const int offsets[] = {0, 1, 29};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        cJSON *row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "start", (double)ha_stats_local_day_start(begin, offsets[i]) * 1000.0);
        cJSON_AddNumberToObject(row, "mean", 100 + i);
        cJSON_AddItemToArray(rows, row);
    }
    ha_range_series_t range_series;
    CHECK(ha_stats_parse_range(reply, "sensor.quote", &window, HA_RANGE_30_DAYS, &range_series) == HA_STATS_OUTCOME_OK);
    cJSON_Delete(reply);
    ui_history_popup_simulator_set_range(&range_series);
    advance(50);
    lv_obj_t *chart = find_chart(top);
    CHECK(chart != NULL && lv_chart_get_type(chart) == LV_CHART_TYPE_SCATTER);
    CHECK(lv_chart_get_point_count(chart) == 30);
    lv_chart_series_t *line = lv_chart_get_series_next(chart, NULL);
    int32_t *xs = lv_chart_get_series_x_array(chart, line);
    int32_t *ys = lv_chart_get_series_y_array(chart, line);
    CHECK(xs[0] == 0 && xs[1] == (ha_stats_local_day_start(begin, 1) - begin) / 60);
    CHECK(xs[29] == (ha_stats_local_day_start(begin, 29) - begin) / 60 && xs[1] * 2 < xs[29]);
    CHECK(ys[0] != LV_CHART_POINT_NONE && ys[1] != LV_CHART_POINT_NONE && ys[29] != LV_CHART_POINT_NONE);
    CHECK(ys[2] == LV_CHART_POINT_NONE && ys[28] == LV_CHART_POINT_NONE);

    /* A year must fit LVGL's int32 coordinate mapping, including the multiplication by the chart width. */
    reply = cJSON_CreateObject();
    rows = cJSON_AddArrayToObject(reply, "sensor.quote");
    window.end_epoch = ha_stats_local_day_start(begin, 365);
    for (int i = 0; i < 3; i++) {
        cJSON *row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "start", (double)ha_stats_local_day_start(begin, i * 26 * 7) * 1000.0);
        cJSON_AddNumberToObject(row, "mean", 100 + i);
        cJSON_AddItemToArray(rows, row);
    }
    CHECK(ha_stats_parse_range(reply, "sensor.quote", &window, HA_RANGE_12_MONTHS, &range_series) == HA_STATS_OUTCOME_OK);
    cJSON_Delete(reply);
    CHECK(range_series.count == 53);
    ui_history_popup_simulator_set_range(&range_series);
    advance(50);
    chart = find_chart(top);
    line = lv_chart_get_series_next(chart, NULL);
    lv_obj_update_layout(chart);
    lv_point_t first_point, middle_point, last_point;
    lv_chart_get_point_pos_by_id(chart, line, 0, &first_point);
    lv_chart_get_point_pos_by_id(chart, line, 26, &middle_point);
    lv_chart_get_point_pos_by_id(chart, line, 52, &last_point);
    CHECK(middle_point.x > first_point.x && middle_point.x < last_point.x);
    CHECK(abs(2 * (middle_point.x - first_point.x) - (last_point.x - first_point.x)) <= 2);

    printf("price forecast: %u checks passed\n", checks);
    return 0;
}
