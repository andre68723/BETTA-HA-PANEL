/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"
#include "drivers/display_init.h"

/* ---- Day detail popup ------------------------------------------------- */

/* Left open, a popup blocks the layout underneath and the touch that would dismiss it
 * may never come (the panel is a wall fixture, not a phone in a pocket). */
#define WEATHER_POPUP_AUTO_CLOSE_MS 60000

static void weather_popup_close(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->popup_close_timer != NULL) {
        lv_timer_del(ctx->popup_close_timer);
        ctx->popup_close_timer = NULL;
    }
    if (ctx->popup != NULL) {
        lv_obj_t *popup = ctx->popup;
        ctx->popup = NULL;
        lv_obj_del(popup);
    }
}

static void weather_popup_auto_close_cb(lv_timer_t *timer)
{
    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx != NULL) {
        ctx->popup_close_timer = NULL; /* the timer deletes itself; do not lv_timer_del it again */
        weather_popup_close(ctx);
    }
}

static void weather_popup_arm_auto_close(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->popup_close_timer = lv_timer_create(weather_popup_auto_close_cb, WEATHER_POPUP_AUTO_CLOSE_MS, ctx);
    if (ctx->popup_close_timer != NULL) {
        lv_timer_set_repeat_count(ctx->popup_close_timer, 1);
    }
}

static void weather_popup_deleted_cb(lv_event_t *event)
{
    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx != NULL) {
        ctx->popup = NULL;
        if (ctx->popup_close_timer != NULL) {
            lv_timer_del(ctx->popup_close_timer);
            ctx->popup_close_timer = NULL;
        }
    }
}

static void weather_popup_backdrop_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
        weather_popup_close((w_weather_tile_ctx_t *)lv_event_get_user_data(event));
    }
}

static void weather_popup_close_cb(lv_event_t *event)
{
    weather_popup_close((w_weather_tile_ctx_t *)lv_event_get_user_data(event));
}

/* Width of a detail row in the day popup: full width, or half on wide screens (two columns). */
static int s_weather_popup_row_w = 0;

static void weather_popup_add_row(lv_obj_t *list, const char *label, const char *value)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    if (s_weather_popup_row_w > 0) {
        lv_obj_set_width(row, s_weather_popup_row_w);
    } else {
        lv_obj_set_width(row, LV_PCT(100));
    }
    lv_obj_set_height(row, ui_font_role(UI_TEXT_VALUE)->line_height + 12);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, false);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(row, LV_OPA_50, LV_PART_MAIN);

    lv_obj_t *l = ui_popup_label_create(row, label, UI_POPUP_LABEL_CAPTION);
    lv_label_set_text(l, label);
    ui_text_set(l, UI_TEXT_LABEL, true);
    lv_obj_set_style_text_color(l, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *v = ui_popup_label_create(row, value, UI_POPUP_LABEL_VALUE);
    lv_label_set_text(v, value);
    ui_text_set(v, UI_TEXT_VALUE, true);
    lv_obj_set_style_text_color(v, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -2, 0);
}

static const char *weather_compass_point(int bearing, char *buf, size_t buf_size)
{
    /* "weather.compass" holds the eight points, comma separated, starting north. */
    const char *list = ui_i18n_get("weather.compass", "N,NE,E,SE,S,SW,W,NW");
    int idx = (((bearing % 360) + 360) % 360 + 22) / 45 % 8;
    const char *p = list;
    for (int i = 0; i < idx && p != NULL; i++) {
        p = strchr(p, ',');
        if (p != NULL) {
            p++;
        }
    }
    if (p == NULL) {
        buf[0] = '\0';
        return buf;
    }
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ',' && n + 1 < buf_size) {
        buf[n] = p[n];
        n++;
    }
    buf[n] = '\0';
    return buf;
}

/* Extra details for one day, fetched with weather.get_forecasts when the popup
 * opens (the panel's compact forecast has no room for them). */
typedef struct weather_day_detail {
    struct weather_day_detail *next;
    uint32_t token;
    lv_obj_t *list;   /* validity and token checked on the UI task */
    bool skip_current; /* today: pressure/humidity/wind/visibility already shown */
    char date[11];
    float humidity, pressure, wind_speed, wind_gust, uv, clouds, sun_h, rain_h, visibility_km, dew;
    int wind_bearing;
    char pressure_unit[8];
    char wind_unit[8];
    char temp_unit[8];
} weather_day_detail_t;

static weather_day_detail_t *s_pending_details;
static uint32_t s_detail_token;
#ifndef BETTA_SIMULATOR
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_detail_lock = portMUX_INITIALIZER_UNLOCKED;
#define WEATHER_DETAILS_LOCK() portENTER_CRITICAL(&s_detail_lock)
#define WEATHER_DETAILS_UNLOCK() portEXIT_CRITICAL(&s_detail_lock)
#else
#define WEATHER_DETAILS_LOCK() ((void)0)
#define WEATHER_DETAILS_UNLOCK() ((void)0)
#endif

/* Both a synchronous send error and the WS callback may finish a request. Integer
 * handles let exactly one take ownership, without retaining a freed callback pointer. */
static weather_day_detail_t *weather_detail_take(uint32_t token)
{
    WEATHER_DETAILS_LOCK();
    weather_day_detail_t **link = &s_pending_details;
    while (*link && (*link)->token != token) link = &(*link)->next;
    weather_day_detail_t *detail = *link;
    if (detail) *link = detail->next;
    WEATHER_DETAILS_UNLOCK();
    return detail;
}


static bool weather_detail_num(cJSON *item, const char *key, float *out)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(item, key);
    if (cJSON_IsNumber(n)) {
        *out = (float)n->valuedouble;
        return true;
    }
    return false;
}

static void weather_detail_apply_cb(void *data)
{
    weather_day_detail_t *d = (weather_day_detail_t *)data;
    if (d == NULL) {
        return;
    }
    if (d->list != NULL && lv_obj_is_valid(d->list) &&
        lv_obj_get_user_data(d->list) == (void *)(uintptr_t)d->token) {
        char text[48];
        if (!d->skip_current) {
            if (!isnan(d->humidity)) {
                snprintf(text, sizeof(text), "%.0f %%", (double)d->humidity);
                weather_popup_add_row(d->list, ui_i18n_get("weather.humidity", "Humidity"), text);
            }
            if (!isnan(d->pressure)) {
                snprintf(text, sizeof(text), "%.1f %s", (double)d->pressure, d->pressure_unit);
                weather_popup_add_row(d->list, ui_i18n_get("weather.pressure", "Pressure"), text);
            }
            if (!isnan(d->wind_speed)) {
                char point[8] = "";
                if (d->wind_bearing >= 0) {
                    weather_compass_point(d->wind_bearing, point, sizeof(point));
                }
                snprintf(text, sizeof(text), "%.1f %s%s%s%s", (double)d->wind_speed, d->wind_unit,
                         point[0] != '\0' ? " (" : "", point, point[0] != '\0' ? ")" : "");
                weather_popup_add_row(d->list, ui_i18n_get("weather.wind", "Wind"), text);
            }
        }
        if (!isnan(d->wind_gust)) {
            snprintf(text, sizeof(text), "%.1f %s", (double)d->wind_gust, d->wind_unit);
            weather_popup_add_row(d->list, ui_i18n_get("weather.wind_gust", "Wind gusts"), text);
        }
        if (!isnan(d->uv)) {
            snprintf(text, sizeof(text), "%.0f", (double)d->uv);
            weather_popup_add_row(d->list, ui_i18n_get("weather.uv_index", "UV index"), text);
        }
        if (!isnan(d->clouds)) {
            snprintf(text, sizeof(text), "%.0f %%", (double)d->clouds);
            weather_popup_add_row(d->list, ui_i18n_get("weather.cloud_cover", "Cloud cover"), text);
        }
        if (!isnan(d->sun_h)) {
            snprintf(text, sizeof(text), "%.1f h", (double)d->sun_h);
            weather_popup_add_row(d->list, ui_i18n_get("weather.sunshine", "Sunshine"), text);
        }
        if (!isnan(d->rain_h) && d->rain_h > 0.0f) {
            snprintf(text, sizeof(text), "%.1f h", (double)d->rain_h);
            weather_popup_add_row(d->list, ui_i18n_get("weather.rain_duration", "Rain duration"), text);
        }
        if (!d->skip_current && !isnan(d->visibility_km)) {
            snprintf(text, sizeof(text), "%.1f km", (double)d->visibility_km);
            weather_popup_add_row(d->list, ui_i18n_get("weather.visibility", "Visibility"), text);
        }
        if (!isnan(d->dew)) {
            snprintf(text, sizeof(text), "%.1f %s", (double)d->dew, d->temp_unit);
            weather_popup_add_row(d->list, ui_i18n_get("weather.dew_point", "Dew point"), text);
        }
    }
    heap_caps_free(d);
}

/* Runs on the HA WS RX task: pick the day out of the response, hand it to LVGL. */
static void weather_detail_response_cb(bool success, cJSON *result, void *user)
{
    weather_day_detail_t *d = weather_detail_take((uint32_t)(uintptr_t)user);
    if (d == NULL) return;
    if (!success) { heap_caps_free(d); return; }
    bool found = false;
    if (success && result != NULL) {
        cJSON *response = cJSON_GetObjectItemCaseSensitive(result, "response");
        cJSON *entity_obj = cJSON_IsObject(response) ? response->child : NULL;
        cJSON *forecast = cJSON_IsObject(entity_obj) ? cJSON_GetObjectItemCaseSensitive(entity_obj, "forecast") : NULL;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, forecast) {
            cJSON *dt = cJSON_GetObjectItemCaseSensitive(item, "datetime");
            if (!cJSON_IsString(dt) || dt->valuestring == NULL) {
                continue;
            }
            /* Empty date (today without a forecast entry): take the first item. */
            if (d->date[0] != '\0' && strncmp(dt->valuestring, d->date, 10) != 0) {
                continue;
            }
            float v = 0.0f;
            if (weather_detail_num(item, "humidity", &v)) d->humidity = v;
            if (weather_detail_num(item, "pressure", &v)) d->pressure = v;
            if (weather_detail_num(item, "wind_speed", &v)) d->wind_speed = v;
            if (weather_detail_num(item, "wind_gust_speed", &v)) d->wind_gust = v;
            if (weather_detail_num(item, "wind_bearing", &v)) d->wind_bearing = (int)(v + 0.5f);
            if (weather_detail_num(item, "uv_index", &v)) d->uv = v;
            if (weather_detail_num(item, "cloud_coverage", &v)) d->clouds = v;
            if (weather_detail_num(item, "sun_duration", &v)) d->sun_h = v / 3600.0f;
            if (weather_detail_num(item, "precipitation_duration", &v)) d->rain_h = v / 3600.0f;
            if (weather_detail_num(item, "visibility", &v)) d->visibility_km = v / 1000.0f; /* DWD: metres */
            if (weather_detail_num(item, "dew_point", &v)) d->dew = v;
            found = true;
            break;
        }
    }
    (void)found;
    bool queued = false;
    if (display_lock(2000)) {
        queued = lv_async_call(weather_detail_apply_cb, d) == LV_RESULT_OK;
        display_unlock();
    }
    if (!queued) heap_caps_free(d);
}

static void weather_request_day_details(w_weather_tile_ctx_t *ctx, lv_obj_t *list, int index)
{
    if (ctx->entity_id[0] == '\0' || !ha_client_is_connected()) {
        return;
    }
    weather_day_detail_t *d = (weather_day_detail_t *)heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (d == NULL) {
        return;
    }
    d->list = list;
    d->skip_current = (index == 0);
    strlcpy(d->date, ctx->render_rows[index].date, sizeof(d->date));
    d->humidity = d->pressure = d->wind_speed = d->wind_gust = d->uv = d->clouds = NAN;
    d->sun_h = d->rain_h = d->visibility_km = d->dew = NAN;
    d->wind_bearing = -1;
    strlcpy(d->pressure_unit, ctx->last_values.pressure_unit, sizeof(d->pressure_unit));
    strlcpy(d->wind_unit, ctx->last_values.wind_unit[0] ? ctx->last_values.wind_unit : "km/h", sizeof(d->wind_unit));
    strlcpy(d->temp_unit, ctx->last_values.unit, sizeof(d->temp_unit));
    if (d->pressure_unit[0] == '\0') {
        snprintf(d->pressure_unit, sizeof(d->pressure_unit), "hPa");
    }
    WEATHER_DETAILS_LOCK();
    d->token = ++s_detail_token;
    d->next = s_pending_details;
    s_pending_details = d;
    const uint32_t token = d->token;
    WEATHER_DETAILS_UNLOCK();
    lv_obj_set_user_data(list, (void *)(uintptr_t)token);
    if (ha_client_call_service_with_response("weather", "get_forecasts", ctx->entity_id, "{\"type\":\"daily\"}",
            weather_detail_response_cb, (void *)(uintptr_t)token) != ESP_OK) {
        heap_caps_free(weather_detail_take(token));
    }
}

/* ---- Weather report (DWD text) ------------------------------------------ */

static int weather_weekday_from_date(const char *date)
{
    int y = 0, m = 0, d = 0;
    if (date == NULL || sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) {
        return -1;
    }
    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    return t.tm_wday;
}

static const char *weather_report_text_for_row(const w_weather_tile_ctx_t *ctx, int index)
{
    int wd = -1;
    if (index == 0) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        wd = lt.tm_wday;
    } else {
        wd = weather_weekday_from_date(ctx->render_rows[index].date);
    }
    return (wd >= 0 && wd <= 6) ? ctx->report_day[wd] : "";
}

/* Shows the summary in the free header space (or hides it if it does not fit). */
void weather_report_update_label(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->report_label == NULL) {
        return;
    }
    if (ctx->report_summary[0] == '\0' || ctx->report_w < 150 || ctx->report_h < 40) {
        lv_obj_set_hidden(ctx->report_label, true);
        return;
    }
    lv_label_set_text(ctx->report_label, ctx->report_summary);
    lv_obj_set_size(ctx->report_label, ctx->report_w, ctx->report_h);
    lv_obj_set_pos(ctx->report_label, ctx->report_x, ctx->report_y);
    lv_obj_set_hidden(ctx->report_label, false);
}

void weather_report_parse_state(w_weather_tile_ctx_t *ctx, const ha_state_t *state)
{
    ctx->report_summary[0] = '\0';
    memset(ctx->report_day, 0, sizeof(ctx->report_day));
    if (!strcmp(state->state, "unknown") || !strcmp(state->state, "unavailable")) return;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs == NULL) {
        return;
    }
    cJSON *sum = cJSON_GetObjectItemCaseSensitive(attrs, "s");
    if (cJSON_IsString(sum) && sum->valuestring != NULL) {
        strlcpy(ctx->report_summary, sum->valuestring, sizeof(ctx->report_summary));
    }
    cJSON *days = cJSON_GetObjectItemCaseSensitive(attrs, "d");
    for (int w = 0; w < 7; w++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", w);
        cJSON *item = cJSON_IsObject(days) ? cJSON_GetObjectItemCaseSensitive(days, key) : NULL;
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            strlcpy(ctx->report_day[w], item->valuestring, sizeof(ctx->report_day[w]));
        }
    }
    cJSON_Delete(attrs);
}

static void weather_popup_add_text(lv_obj_t *list, const char *text, lv_color_t color)
{
    lv_obj_t *label = ui_popup_label_create(list, text, UI_POPUP_LABEL_HINT);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label, 8, LV_PART_MAIN);
}

/* Tap on the temperature or the summary: the report summary and today's text. */
static void weather_show_report_popup(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup != NULL || ctx->report_summary[0] == '\0') {
        return;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen);
    int screen_h = lv_obj_get_height(screen);
    if (screen_w <= 0) screen_w = 720;
    if (screen_h <= 0) screen_h = 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, weather_popup_backdrop_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(overlay, weather_popup_deleted_cb, LV_EVENT_DELETE, ctx);
    ctx->popup = overlay;

    int card_w = screen_w - 48;
    if (card_w > 520) card_w = 520;
    if (card_w < 260) card_w = 260;
    int card_h = 330;
    if (card_h > screen_h - 48) card_h = screen_h - 48;

    lv_obj_t *card = ui_popup_card_create(overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);

    #if UI_TEXT_ADAPTIVE
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
#else
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
#endif

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ui_i18n_get("weather.report_title", "Weather report"));
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 6);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), weather_popup_close_cb, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, card_w - 32, card_h - 32 - 56);
    lv_obj_set_pos(list, 0, 56);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    weather_popup_add_text(list, ctx->report_summary, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
    const char *today = weather_report_text_for_row(ctx, 0);
    if (today[0] != '\0') {
        weather_popup_add_text(list, today, lv_color_hex(APP_UI_COLOR_TEXT_MUTED));
    }
    weather_popup_arm_auto_close(ctx);
}

void weather_report_clicked_cb(lv_event_t *event)
{
    weather_show_report_popup((w_weather_tile_ctx_t *)lv_event_get_user_data(event));
}

/* "Sonntag, 20.09." (today: "Heute, Samstag, 19.09."). */
static void weather_day_full_title(const w_weather_tile_ctx_t *ctx, int index, char *out, size_t out_size)
{
    const weather_3day_row_t *row = &ctx->render_rows[index];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int wd = lt.tm_wday;
    int day = lt.tm_mday;
    int month = lt.tm_mon + 1;
    if (index > 0) {
        int y = 0, m = 0, d = 0;
        if (sscanf(row->date, "%d-%d-%d", &y, &m, &d) == 3) {
            wd = weather_weekday_from_date(row->date);
            day = d;
            month = m;
        }
    }
    char key[24];
    snprintf(key, sizeof(key), "weather.day_full_%d", (wd >= 0 && wd <= 6) ? wd : 0);
    const char *name = ui_i18n_get(key, row->day);
    if (index == 0) {
        snprintf(out, out_size, "%s, %s, %02d.%02d.", ui_i18n_get("weather.today", "Today"), name, day, month);
    } else {
        snprintf(out, out_size, "%s, %02d.%02d.", name, day, month);
    }
}

static void weather_show_day_popup(w_weather_tile_ctx_t *ctx, int index)
{
    if (ctx == NULL || ctx->popup != NULL || index < 0 || index >= WEATHER_3DAY_ROWS) {
        return;
    }
    const weather_3day_row_t *row = &ctx->render_rows[index];
    if (!row->valid) {
        return;
    }
    const weather_values_t *v = &ctx->last_values;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen);
    int screen_h = lv_obj_get_height(screen);
    if (screen_w <= 0) screen_w = 720;
    if (screen_h <= 0) screen_h = 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, weather_popup_backdrop_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(overlay, weather_popup_deleted_cb, LV_EVENT_DELETE, ctx);
    ctx->popup = overlay;

    /* Wide screens (10"): a wider card with the detail rows in two columns. */
    const bool wide = screen_w >= 1000;
    int card_w = screen_w - 48;
    if (card_w > (wide ? 800 : 560)) card_w = wide ? 800 : 560;
    if (card_w < 260) card_w = 260;
    int card_h = screen_h - 48;
    if (card_h > (wide ? 560 : 560)) card_h = 560;

    lv_obj_t *card = ui_popup_card_create(overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);

    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);

    const int inner_w = card_w - 32;
    const int column_gap = 28;
    s_weather_popup_row_w = wide ? (inner_w - column_gap) / 2 : 0;

    char title_text[64];
    weather_day_full_title(ctx, index, title_text, sizeof(title_text));
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 6);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), weather_popup_close_cb, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Header: large icon, condition, high / low. */
    const int head_y = 52;
    const int icon_px = WEATHER_ICON_LARGE_PX;
    const weather_icon_id_t id = weather_icon_from_condition(row->condition_key, index == 0 ? weather_is_night_now() : false);
    const lv_image_dsc_t *dsc = weather_icon_image(id, true);
    int text_x = 0;
    if (dsc != NULL) {
        lv_obj_t *img = lv_image_create(card);
        lv_image_set_src(img, dsc);
        lv_obj_set_clickable(img, false);
        lv_obj_set_pos(img, 0, head_y);
        text_x = icon_px + 8;
    }
    lv_obj_t *cond = lv_label_create(card);
    lv_label_set_text(cond, row->condition_text[0] != '\0' ? row->condition_text : "--");
    lv_obj_set_style_text_font(cond, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(cond, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(cond, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cond, inner_w - text_x);
    lv_obj_set_pos(cond, text_x, head_y + 8);

    char temps[48];
    if (row->has_high && row->has_low) {
        snprintf(temps, sizeof(temps), "%.0f\xC2\xB0 / %.0f\xC2\xB0", (double)row->high_temp, (double)row->low_temp);
    } else if (row->has_high) {
        snprintf(temps, sizeof(temps), "%.0f\xC2\xB0", (double)row->high_temp);
    } else {
        snprintf(temps, sizeof(temps), "--");
    }
    lv_obj_t *temp = lv_label_create(card);
    lv_label_set_text(temp, temps);
    lv_obj_set_style_text_font(temp, WEATHER_3DAY_TEMP_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(temp, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_pos(temp, text_x, head_y + 8 + lv_font_get_line_height(ui_font_role(UI_TEXT_META)) * 2);

    /* Detail rows: only what is known. */
    const int list_y = head_y + icon_px + 12;
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, inner_w, card_h - 32 - list_y);
    lv_obj_set_pos(list, 0, list_y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, wide ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(list, column_gap, LV_PART_MAIN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    if (!ctx->show_forecast && ctx->report_summary[0]) {
        weather_popup_add_text(list, ctx->report_summary, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
    }
    const char *report_text = weather_report_text_for_row(ctx, index);
    if (report_text[0] != '\0') {
        weather_popup_add_text(list, report_text, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY));
    }

    char text[48];
    if (row->precip_prob >= 0) {
        snprintf(text, sizeof(text), "%d %%", row->precip_prob);
        weather_popup_add_row(list, ui_i18n_get("weather.rain_chance", "Chance of rain"), text);
    }
    if (row->precip_mm >= 0.0f) {
        snprintf(text, sizeof(text), "%.1f %s", (double)row->precip_mm, v->precip_unit);
        weather_popup_add_row(list, ui_i18n_get("weather.precipitation", "Precipitation"), text);
    }
    if (index == 0) {
        /* Current conditions belong to today only. */
        if (v->has_pressure) {
            snprintf(text, sizeof(text), "%.1f %s", (double)v->pressure, v->pressure_unit);
            weather_popup_add_row(list, ui_i18n_get("weather.pressure", "Pressure"), text);
        }
        if (v->humidity >= 0) {
            snprintf(text, sizeof(text), "%d %%", v->humidity);
            weather_popup_add_row(list, ui_i18n_get("weather.humidity", "Humidity"), text);
        }
        if (v->has_wind) {
            char point[8] = "";
            if (v->wind_bearing >= 0) {
                weather_compass_point(v->wind_bearing, point, sizeof(point));
            }
            snprintf(text, sizeof(text), "%.1f %s%s%s%s", (double)v->wind_speed, v->wind_unit,
                     point[0] != '\0' ? " (" : "", point, point[0] != '\0' ? ")" : "");
            weather_popup_add_row(list, ui_i18n_get("weather.wind", "Wind"), text);
        }
        if (v->has_visibility) {
            snprintf(text, sizeof(text), "%.1f %s", (double)v->visibility, v->visibility_unit);
            weather_popup_add_row(list, ui_i18n_get("weather.visibility", "Visibility"), text);
        }
    }
    weather_request_day_details(ctx, list, index);
    weather_popup_arm_auto_close(ctx);
}

void weather_col_clicked_cb(lv_event_t *event)
{
    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *box = lv_event_get_target(event);
    if (ctx != NULL && box != NULL) {
        weather_show_day_popup(ctx, (int)(intptr_t)lv_obj_get_user_data(box));
    }
}

/* Horizontal view: the current condition as a large colored icon with the
 * temperature beside it, and below it one card per day (today first) with the
 * weekday, a colored icon and the high and low. Returns false if the tile is
 * not suited (then the caller renders the compact rows). */

void weather_now_clicked_cb(lv_event_t *event)
{
    w_weather_tile_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx != NULL && ctx->available) weather_show_day_popup(ctx, 0);
}
