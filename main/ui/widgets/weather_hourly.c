/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ui/widgets/weather_tile_internal.h"
#include "drivers/display_init.h"
#include "esp_timer.h"
#include "ui/text_font.h"
#include "ui/ui_memory.h"

#define HOURLY_REFRESH_MS (15LL * 60 * 1000)
#define HOURLY_RETRY_MS 30000
#define HOURLY_TIMEOUT_MS 20000

/* UI-owned registrations. Replies carry an integer token, never a widget pointer: removal,
 * recreation and timeouts cannot make a late WS reply touch a new widget at the same address. */
static w_weather_tile_ctx_t *s_registered;
static uint32_t s_token;

typedef struct {
    uint32_t token;
    char entity[APP_MAX_ENTITY_ID_LEN];
    ha_weather_hourly_t data;
} hourly_reply_t;

static void rerender(w_weather_tile_ctx_t *ctx)
{
    ui_fonts_tile_begin("weather_tile", lv_obj_get_width(ctx->card), lv_obj_get_height(ctx->card));
    weather_hourly_render(ctx->card, ctx, ctx->available ? &ctx->last_values : NULL);
    ctx->hourly_view_hour = (int64_t)time(NULL) / 3600;
    ui_fonts_tile_end();
}

static void apply_reply(void *user)
{
    hourly_reply_t *reply = user;
    for (w_weather_tile_ctx_t *ctx = s_registered; ctx; ctx = ctx->hourly_next) {
        if (ctx->hourly_token != reply->token || strcmp(ctx->entity_id, reply->entity)) continue;
        ctx->hourly_inflight = false;
        ctx->hourly = reply->data;
        ctx->hourly_ok_ms = esp_timer_get_time() / 1000;
        rerender(ctx);
        break;
    }
    free(reply);
}

static hourly_reply_t *parse_reply(uint32_t token, const cJSON *result)
{
    hourly_reply_t *reply = ui_calloc_prefer_psram(1, sizeof(*reply));
    if (!reply) return NULL;
    reply->token = token;
    const cJSON *response = cJSON_GetObjectItemCaseSensitive(result, "response");
    const cJSON *entity = cJSON_IsObject(response) ? response->child : NULL;
    if (entity && entity->string) {
        snprintf(reply->entity, sizeof(reply->entity), "%s", entity->string);
        ha_weather_hourly_parse(result, reply->entity, (int64_t)time(NULL), &reply->data);
    }
    return reply;
}

#ifndef BETTA_SIMULATOR
static void response_cb(bool success, cJSON *result, void *user)
{
    /* Failed sends can invoke this synchronously and also return an error. There is no
     * caller-owned allocation to free twice; the UI timer expires an unanswered token. */
    if (!success) return;
    hourly_reply_t *reply = parse_reply((uint32_t)(uintptr_t)user, result);
    if (!reply) return;
    bool queued = false;
    if (display_lock(2000)) {
        queued = lv_async_call(apply_reply, reply) == LV_RESULT_OK;
        display_unlock();
    }
    if (!queued) free(reply);
}
#else
void weather_hourly_simulator_reply(uint32_t token, const cJSON *result)
{
    hourly_reply_t *reply = parse_reply(token, result);
    if (reply) apply_reply(reply);
}
#endif

static void tick(lv_timer_t *timer)
{
    w_weather_tile_ctx_t *ctx = lv_timer_get_user_data(timer);
    if (!ctx->available || !lv_obj_is_visible(ctx->card)) return;
    const int64_t now = esp_timer_get_time() / 1000;
    if (ctx->hourly_inflight && now - ctx->hourly_attempt_ms >= HOURLY_TIMEOUT_MS) {
        ctx->hourly_inflight = false;
        ctx->hourly_token = ++s_token;
        ctx->hourly_retry_after_ms = now + HOURLY_REFRESH_MS;
        rerender(ctx);
    }
    if (ctx->hourly_view_hour != (int64_t)time(NULL) / 3600) rerender(ctx);
#ifndef BETTA_SIMULATOR
    if (now < ctx->hourly_retry_after_ms || ctx->hourly_inflight || (ctx->hourly_attempt_ms && now - ctx->hourly_attempt_ms < HOURLY_RETRY_MS) ||
        (ctx->hourly_ok_ms && now - ctx->hourly_ok_ms < HOURLY_REFRESH_MS) ||
        !ha_client_is_connected() || !ha_client_is_initial_sync_done() || ha_client_heavy_gate_is_busy()) return;
    ctx->hourly_attempt_ms = now;
    ctx->hourly_token = ++s_token;
    ctx->hourly_inflight = true;
    if (ha_client_call_service_with_response("weather", "get_forecasts", ctx->entity_id, "{\"type\":\"hourly\"}",
            response_cb, (void *)(uintptr_t)ctx->hourly_token) != ESP_OK) ctx->hourly_inflight = false;
    rerender(ctx);
#endif
}

void weather_hourly_start(w_weather_tile_ctx_t *ctx)
{
    ctx->hourly_token = ++s_token;
    ctx->hourly_next = s_registered;
    s_registered = ctx;
    ctx->hourly_timer = lv_timer_create(tick, 2000, ctx);
}

void weather_hourly_stop(w_weather_tile_ctx_t *ctx)
{
    if (!ctx->show_hourly) return;
    if (ctx->hourly_timer) lv_timer_delete(ctx->hourly_timer);
    w_weather_tile_ctx_t **link = &s_registered;
    while (*link && *link != ctx) link = &(*link)->hourly_next;
    if (*link) *link = ctx->hourly_next;
}

static void text(lv_obj_t *label, ui_text_role_t role, int width, int x, int y, const char *value)
{
    ui_text_set(label, role, true);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
    const lv_font_t *font = ui_text_fit(value, ui_font_role(role), width, lv_font_get_line_height(ui_font_role(role)));
    lv_obj_set_style_text_font(label, font, 0);
    ui_text_compact(label, font != ui_font_role(role));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_size(label, width, lv_font_get_line_height(ui_font_role(role)));
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    lv_label_set_text(label, value);
    lv_obj_set_hidden(label, false);
}

bool weather_hourly_render(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values)
{
    lv_obj_update_layout(card);
    const int width = lv_obj_get_content_width(card), height = lv_obj_get_content_height(card);
    const int meta_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int value_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
    const int y = ctx->hide_name ? 0 : meta_h + 6;
    const bool large = width >= 400 && height >= y + 112 + meta_h * 2 + 72;
    const int icon_px = large ? 112 : 64;
    const int head_h = icon_px > value_h + meta_h + 4 ? icon_px : value_h + meta_h + 4;
    if (!ctx->hourly_strip) {
        ctx->hourly_icon = lv_image_create(card);
        ctx->hourly_strip = lv_obj_create(card);
        lv_obj_remove_style_all(ctx->hourly_strip);
        lv_obj_set_scroll_dir(ctx->hourly_strip, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(ctx->hourly_strip, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_border_side(ctx->hourly_strip, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(ctx->hourly_strip, 1, 0);
        lv_obj_set_style_border_color(ctx->hourly_strip, lv_color_hex(APP_UI_COLOR_CARD_BORDER), 0);
        lv_obj_set_style_pad_top(ctx->hourly_strip, 8, 0);
        lv_obj_set_event_bubble(ctx->hourly_strip, true);
        ctx->hourly_status = lv_label_create(ctx->hourly_strip);
    }
    lv_obj_set_pos(ctx->hourly_strip, 0, y + head_h + 8);
    const int strip_h = height - y - head_h - 8;
    lv_obj_set_size(ctx->hourly_strip, width, strip_h > 1 ? strip_h : 1);
    const lv_image_dsc_t *image = values ? weather_icon_image(weather_icon_from_condition(
        weather_effective_condition_key(ctx, values), weather_is_night_now()), large) : NULL;
    lv_obj_set_hidden(ctx->hourly_icon, image == NULL);
    if (image) {
        lv_image_set_src(ctx->hourly_icon, image);
        lv_obj_set_pos(ctx->hourly_icon, 0, y);
    }
    const int x = icon_px + 12;
    char value[48] = "--";
    if (values && values->has_temp) snprintf(value, sizeof(value), "%.1f %s", (double)values->temp, values->unit);
    text(ctx->temp_label, UI_TEXT_VALUE, width - x, x, y, value);
    text(ctx->condition_label, UI_TEXT_META, width - x, x, y + value_h + 4,
         values ? values->condition : ui_i18n_get("weather.unavailable", "Weather unavailable"));
    ui_text_record(ctx->condition_label, UI_TEXT_META, false);
    lv_obj_set_style_text_color(ctx->condition_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), 0);
    lv_obj_set_hidden(ctx->meta_label, true);
    const int hour_icon = strip_h >= meta_h * 2 + 64 ? 48 : 24;
    const int col_w = meta_h * 3 > 76 ? meta_h * 3 : 76;
    const int col_h = meta_h * 2 + hour_icon + 8;
    unsigned visible = 0;
    const int64_t now = (int64_t)time(NULL);
    for (unsigned i = 0; i < HA_WEATHER_HOURS_MAX; i++) {
        if (!values || i >= ctx->hourly.count || ctx->hourly.hour[i].at < now) {
            if (ctx->hours[i].box) lv_obj_set_hidden(ctx->hours[i].box, true);
            continue;
        }
        if (!ctx->hours[i].box) {
            lv_obj_t *box = lv_obj_create(ctx->hourly_strip);
            lv_obj_remove_style_all(box);
            lv_obj_set_scrollable(box, false);
            lv_obj_set_clickable(box, false);
            ctx->hours[i].box = box;
            ctx->hours[i].time = lv_label_create(box);
            ctx->hours[i].icon = lv_image_create(box);
            ctx->hours[i].temp = lv_label_create(box);
        }
        const ha_weather_hour_t *hour = &ctx->hourly.hour[i];
        lv_obj_set_hidden(ctx->hours[i].box, false);
        lv_obj_set_pos(ctx->hours[i].box, visible++ * (col_w + 8), 0);
        lv_obj_set_size(ctx->hours[i].box, col_w, col_h);
        const time_t at = (time_t)hour->at;
        struct tm local;
        localtime_r(&at, &local);
        snprintf(value, sizeof(value), "%02d:%02d", local.tm_hour, local.tm_min);
        text(ctx->hours[i].time, UI_TEXT_META, col_w, 0, 0, value);
        lv_obj_set_style_text_align(ctx->hours[i].time, LV_TEXT_ALIGN_CENTER, 0);
        const bool night = !strcmp(hour->condition, "clear-night");
        const lv_image_dsc_t *icon = weather_icon_image(weather_icon_from_condition(hour->condition, night), false);
        lv_obj_set_hidden(ctx->hours[i].icon, icon == NULL);
        if (icon) {
            lv_image_set_src(ctx->hours[i].icon, icon);
            lv_image_set_scale(ctx->hours[i].icon, 256 * hour_icon / WEATHER_ICON_SMALL_PX);
            lv_obj_set_pos(ctx->hours[i].icon, (col_w - WEATHER_ICON_SMALL_PX) / 2,
                           meta_h + 4 - (WEATHER_ICON_SMALL_PX - hour_icon) / 2);
        }
        snprintf(value, sizeof(value), "%.0f%s", (double)hour->temperature, values->unit);
        text(ctx->hours[i].temp, UI_TEXT_META, col_w, 0, meta_h + hour_icon + 8, value);
        lv_obj_set_style_text_align(ctx->hours[i].temp, LV_TEXT_ALIGN_CENTER, 0);
    }
    text(ctx->hourly_status, UI_TEXT_META, width, 0, 0, ui_i18n_get(ctx->hourly_inflight ?
        "weather.hourly_loading" : "weather.hourly_empty", ctx->hourly_inflight ? "Loading hourly forecast..." : "No hourly forecast"));
    ui_text_record(ctx->hourly_status, UI_TEXT_META, false);
    lv_label_set_long_mode(ctx->hourly_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(ctx->hourly_status, LV_SIZE_CONTENT);
    lv_obj_set_hidden(ctx->hourly_status, visible > 0);
    return true;
}
