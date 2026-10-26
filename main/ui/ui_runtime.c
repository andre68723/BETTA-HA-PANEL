/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_alarm_state.h"
#include "ui/display_geometry.h"
#include "layout/layout_orientation.h"
#include "drivers/display_orientation.h"
#include "ui/ui_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_events.h"
#include "drivers/display_init.h"
#include "ha/ha_client.h"
#include "ha/ha_model.h"
#include "ha/ha_display_precision.h"
#include "ha/ha_quote.h"
#include "layout/layout_store.h"
#include "net/wifi_mgr.h"
#include "util/coredump_archive.h"
#include "settings/runtime_settings.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_energy_page.h"
#include "ui/ui_house_page.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/notification_text.h"
#include "ui/ui_notification_popup.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_pages.h"
#include "ui/ui_display_protection.h"
#include "ui/ui_widget_factory.h"
#include "layout/evcc_slots.h"
#include "layout/widget_types.h"
#include "util/boot_guard.h"
#include "util/notification_parse.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/theme_store.h"
#include "ui/graph_history_store.h"
#include "util/log_tags.h"
#include "util/ui_watchdog_esp.h"

static void ui_runtime_copy_json_string(cJSON *obj, const char *key, char *dst, size_t dst_size);
static void ui_runtime_copy_text_field(cJSON *obj, const char *key, char *dst, size_t dst_size);

#define UI_MODEL_RECONCILE_INTERVAL_MS 1000

static ui_widget_instance_t *s_widgets = NULL;
static size_t s_widget_count = 0;
static ui_energy_page_instance_t *s_energy_pages = NULL;
static size_t s_energy_page_count = 0;
static bool s_pending_protection_reload = false;

static void ui_runtime_update_widget_visibility(const char *page_id, bool refresh_visible_widgets);
static void ui_runtime_apply_widget_current_state(ui_widget_instance_t *widget, bool mark_unavailable_if_missing);
static void ui_runtime_load_alerts(cJSON *root);
static void ui_runtime_check_alerts(const char *entity_id);

/* Invoked by ui_pages whenever the active page changes.
 * If the newly shown page is an energy dashboard, ask the HA client to
 * refresh the statistics immediately so the user sees fresh kWh values
 * instead of waiting for the next HA_ENERGY_SYNC_INTERVAL_MS tick. */
/* The refresh of the newly shown page (energy statistics, re-applying every
 * widget's state) takes 60 to 110 ms on the 10". It runs from an async call so
 * the page is drawn first; only what actually changed is repainted afterwards. */
static char s_pending_refresh_page[APP_MAX_PAGE_ID_LEN];
static bool s_refresh_scheduled = false;

/* Widgets are re-applied in slices of about 8 ms so a page with many tiles (the house page) never
 * blocks touch and drawing for its whole refresh. A page change restarts the walk. */
#define UI_REFRESH_SLICE_US 8000
/* Bounded wait for the display lock: a blocked UI task would starve the event queue; callers retry or defer. */
#define UI_RUNTIME_LOCK_MS 5000
static size_t s_refresh_next = 0;
static bool s_refresh_energy_done = false;
static int64_t s_refresh_start_us = 0;

static void ui_runtime_deferred_refresh_cb(void *arg)
{
    (void)arg;
    s_refresh_scheduled = false;
    const char *page_id = s_pending_refresh_page;
    if (page_id[0] == '\0') {
        return;
    }
    const int64_t slice_start_us = esp_timer_get_time();
    if (s_refresh_next == 0 && !s_refresh_energy_done) {
        s_refresh_start_us = slice_start_us;
        for (size_t i = 0; i < s_energy_page_count; i++) {
            if (strncmp(s_energy_pages[i].page_id, page_id, APP_MAX_PAGE_ID_LEN) == 0) {
                (void)ha_client_request_energy_refresh();
                (void)ui_energy_page_apply_all_states(&s_energy_pages[i]);
                break;
            }
        }
        s_refresh_energy_done = true;
    }
    while (s_refresh_next < s_widget_count) {
        ui_widget_instance_t *widget = &s_widgets[s_refresh_next++];
        if (strncmp(widget->page_id, page_id, APP_MAX_PAGE_ID_LEN) == 0 &&
            ui_widget_factory_needs_refresh_on_show(widget->type)) {
            ui_runtime_apply_widget_current_state(widget, false);
        }
        if (esp_timer_get_time() - slice_start_us > UI_REFRESH_SLICE_US) {
            s_refresh_scheduled = true;
            if (lv_async_call(ui_runtime_deferred_refresh_cb, NULL) != LV_RESULT_OK) {
                s_refresh_scheduled = false;
            }
            return;
        }
    }
    s_refresh_next = 0;
    s_refresh_energy_done = false;
    ESP_LOGI(TAG_UI, "page %s shown, refresh %d ms", page_id, (int)((esp_timer_get_time() - s_refresh_start_us) / 1000));
}

static void ui_runtime_on_page_shown(const char *page_id, uint16_t index)
{
    (void)index;
    if (page_id == NULL || page_id[0] == '\0') {
        return;
    }
    strlcpy(s_pending_refresh_page, page_id, sizeof(s_pending_refresh_page));
    s_refresh_next = 0; /* restart the sliced refresh for the new page */
    s_refresh_energy_done = false;
    /* Cheap part now: visibility flags only. */
    ui_runtime_update_widget_visibility(page_id, false);
    if (!s_refresh_scheduled) {
        s_refresh_scheduled = true;
        if (lv_async_call(ui_runtime_deferred_refresh_cb, NULL) != LV_RESULT_OK) {
            s_refresh_scheduled = false;
            ui_runtime_deferred_refresh_cb(NULL);
        }
    }
}
static TaskHandle_t s_ui_task = NULL;
static ha_state_t s_state_scratch;
static bool s_initialized = false;
static int64_t s_last_topbar_refresh_ms = 0;
static int64_t s_last_model_reconcile_ms = 0;
static uint32_t s_last_model_revision = 0;
static bool s_model_reconcile_pending = false;
static bool s_pending_state_reconcile = false;
static bool s_pending_topbar_refresh = false;
static char s_topbar_left_slots[APP_TOPBAR_LEFT_SLOTS][APP_MAX_UI_OPTION_LEN] = { "date", "", "" };
static bool s_topbar_weather_enabled = false;
static char s_topbar_weather_entity_id[APP_MAX_ENTITY_ID_LEN] = {0};
static char s_topbar_stock_entity_ids[3][APP_MAX_ENTITY_ID_LEN] = {{0}};
/* Notification deferred by display-lock contention; owned by the UI task. */
static app_notification_t *s_pending_notification = NULL;
static uint32_t s_deferred_event_count = 0;
static int64_t s_deferred_event_log_ms = 0;

static esp_err_t ui_runtime_alloc_buffers(void)
{
    if (s_widgets == NULL) {
        s_widgets = ui_calloc_prefer_psram(APP_MAX_WIDGETS_TOTAL, sizeof(*s_widgets));
        if (s_widgets == NULL) {
            ESP_LOGE(TAG_UI, "Failed to allocate widget runtime buffer (%u slots)",
                     (unsigned)APP_MAX_WIDGETS_TOTAL);
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_energy_pages == NULL) {
        s_energy_pages = ui_calloc_prefer_psram(APP_MAX_PAGES, sizeof(*s_energy_pages));
        if (s_energy_pages == NULL) {
            ESP_LOGE(TAG_UI, "Failed to allocate energy page runtime buffer (%u pages)", (unsigned)APP_MAX_PAGES);
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

typedef struct {
    bool valid;
    int minute;
    int hour;
    int day;
    int month;
    int year;
    bool wifi_connected;
    bool wifi_setup_ap_active;
    bool ha_connected;
    bool ha_initial_sync_done;
} ui_topbar_cache_t;
static ui_topbar_cache_t s_topbar_cache = {0};
#if APP_UI_TEST_WEATHER_ICON_OVERLAY
static lv_obj_t *s_weather_icon_overlay = NULL;
#endif

static bool ui_runtime_weather_has_token(const char *key, const char *token)
{
    return key != NULL && token != NULL && strstr(key, token) != NULL;
}

static void ui_runtime_weather_normalize_condition_key(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; i++) {
        char c = src[i];
        if (c == ' ' || c == '_') {
            c = '-';
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        dst[out++] = c;
    }
    dst[out] = '\0';
}

uint32_t ui_runtime_weather_icon_codepoint_for_key(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return 0U;
    }
    if (strcmp(key, "clear-night") == 0) return 0xF0594U;
    if (strcmp(key, "partlycloudy") == 0 || strcmp(key, "partly-cloudy") == 0) return 0xF0595U;
    if (ui_runtime_weather_has_token(key, "tornado")) return 0xF0F38U;
    if (ui_runtime_weather_has_token(key, "hurricane")) return 0xF0898U;
    if (ui_runtime_weather_has_token(key, "lightning") && ui_runtime_weather_has_token(key, "rain")) return 0xF067EU;
    if (ui_runtime_weather_has_token(key, "partly") && ui_runtime_weather_has_token(key, "lightning")) return 0xF0F32U;
    if (ui_runtime_weather_has_token(key, "lightning")) return 0xF0593U;
    if (ui_runtime_weather_has_token(key, "hail")) return 0xF0592U;
    if (ui_runtime_weather_has_token(key, "fog") || ui_runtime_weather_has_token(key, "hazy") ||
        ui_runtime_weather_has_token(key, "mist")) return 0xF0591U;
    if (ui_runtime_weather_has_token(key, "partly") && ui_runtime_weather_has_token(key, "snow") &&
        ui_runtime_weather_has_token(key, "rain")) return 0xF0F35U;
    if (ui_runtime_weather_has_token(key, "partly") && ui_runtime_weather_has_token(key, "snow")) return 0xF0F34U;
    if (ui_runtime_weather_has_token(key, "snow") && ui_runtime_weather_has_token(key, "rain")) return 0xF067FU;
    if (ui_runtime_weather_has_token(key, "snow") && ui_runtime_weather_has_token(key, "heavy")) return 0xF0F36U;
    if (ui_runtime_weather_has_token(key, "snow")) return 0xF0598U;
    if (ui_runtime_weather_has_token(key, "partly") && ui_runtime_weather_has_token(key, "rain")) return 0xF0F33U;
    if (ui_runtime_weather_has_token(key, "pouring")) return 0xF0596U;
    if (ui_runtime_weather_has_token(key, "rain")) return 0xF0597U;
    if (ui_runtime_weather_has_token(key, "night") && ui_runtime_weather_has_token(key, "partly")) return 0xF0F31U;
    if (ui_runtime_weather_has_token(key, "night")) return 0xF0594U;
    if (ui_runtime_weather_has_token(key, "sunset")) return 0xF059AU;
    if (ui_runtime_weather_has_token(key, "sunny") || ui_runtime_weather_has_token(key, "clear")) return 0xF0599U;
    if (ui_runtime_weather_has_token(key, "wind") && ui_runtime_weather_has_token(key, "variant")) return 0xF059EU;
    if (ui_runtime_weather_has_token(key, "wind")) return 0xF059DU;
    if (ui_runtime_weather_has_token(key, "partly")) return 0xF0595U;
    if (ui_runtime_weather_has_token(key, "cloud")) return 0xF0590U;
    return 0U;
}

static bool ui_runtime_json_item_to_float(cJSON *item, float *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (float)item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end = NULL;
        float value = strtof(item->valuestring, &end);
        if (end != item->valuestring) {
            *out = value;
            return true;
        }
    }
    return false;
}

static void ui_runtime_load_topbar_settings(void)
{
    runtime_settings_t settings = {0};
    if (runtime_settings_load(&settings) != ESP_OK) {
        runtime_settings_set_defaults(&settings);
    }
    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        snprintf(
            s_topbar_left_slots[i],
            sizeof(s_topbar_left_slots[i]),
            "%s",
            settings.topbar_left_slots[i]);
    }
    s_topbar_weather_enabled =
        settings.topbar_weather_enabled && settings.topbar_weather_entity_id[0] != '\0';
    snprintf(
        s_topbar_weather_entity_id,
        sizeof(s_topbar_weather_entity_id),
        "%s",
        settings.topbar_weather_entity_id);
    /* Only the first stock entity is used by the top bar; the others stay empty. */
    strlcpy(s_topbar_stock_entity_ids[0], settings.topbar_stock_entity_id, sizeof(s_topbar_stock_entity_ids[0]));
    for (size_t i = 1; i < 3U; i++) {
        s_topbar_stock_entity_ids[i][0] = '\0';
    }
    ui_pages_set_topbar_left_slots(s_topbar_left_slots[0], s_topbar_left_slots[1], s_topbar_left_slots[2]);
}

static void ui_runtime_apply_topbar_weather_state(const ha_state_t *state)
{
    if (!s_topbar_weather_enabled || state == NULL ||
        strncmp(state->entity_id, s_topbar_weather_entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }

    float temperature = 0.0f;
    bool has_temperature = false;
    char unit[12] = {0};
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        has_temperature =
            ui_runtime_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "temperature"), &temperature) ||
            ui_runtime_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "current_temperature"), &temperature) ||
            ui_runtime_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "native_temperature"), &temperature);
        cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "temperature_unit");
        if (!cJSON_IsString(unit_item) || unit_item->valuestring == NULL || unit_item->valuestring[0] == '\0') {
            unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "native_temperature_unit");
        }
        if (cJSON_IsString(unit_item) && unit_item->valuestring != NULL) {
            snprintf(unit, sizeof(unit), "%s", unit_item->valuestring);
        }
        cJSON_Delete(attrs);
    }

    char condition_key[32] = {0};
    ui_runtime_weather_normalize_condition_key(state->state, condition_key, sizeof(condition_key));
    uint32_t icon = ui_runtime_weather_icon_codepoint_for_key(condition_key);
    ui_pages_set_topbar_weather(has_temperature, temperature, unit, icon);
}

static void ui_runtime_stock_symbol_from_entity(const char *entity_id, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    const char *src = entity_id != NULL ? entity_id : "";
    const char *prefix = "sensor.yahoofinance_";
    if (strncmp(src, prefix, strlen(prefix)) == 0) {
        src += strlen(prefix);
    } else if (strncmp(src, "sensor.", 7) == 0) {
        src += 7;
    }

    char tmp[24] = {0};
    size_t out_pos = 0;
    for (size_t i = 0; src[i] != '\0' && out_pos + 1 < sizeof(tmp); i++) {
        char c = src[i];
        if (c == ' ') {
            break;
        }
        if (c == '_' && src[i + 1] != '\0' && src[i + 2] != '\0' && src[i + 3] == '\0') {
            c = '.';
        } else if (c == '_') {
            c = ' ';
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c + ('A' - 'a'));
        }
        tmp[out_pos++] = c;
    }
    tmp[out_pos] = '\0';
    /* strlcpy truncates like snprintf but does not trigger -Wformat-truncation
     * when the caller's buffer is smaller than tmp. */
    strlcpy(out, tmp[0] != '\0' ? tmp : "STK", out_len);
}

static bool ui_runtime_is_topbar_stock_entity(const char *entity_id)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < 3U; i++) {
        if (s_topbar_stock_entity_ids[i][0] != '\0' &&
            strncmp(entity_id, s_topbar_stock_entity_ids[i], APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static bool ui_runtime_stock_currency_is_euro(const char *currency)
{
    return currency != NULL && (strcmp(currency, "\xE2\x82\xAC") == 0 || strcmp(currency, "EUR") == 0);
}

static void ui_runtime_refresh_topbar_stocks(void)
{
    ui_pages_stock_item_t items[3] = {0};
    size_t count = 0;
    ha_state_t state = {0};

    for (size_t i = 0; i < 3U; i++) {
        const char *entity_id = s_topbar_stock_entity_ids[i];
        if (entity_id[0] == '\0') {
            continue;
        }
        memset(&state, 0, sizeof(state));
        if (!ha_model_get_state(entity_id, &state) ||
            state.state[0] == '\0' ||
            strcmp(state.state, "unknown") == 0 ||
            strcmp(state.state, "unavailable") == 0) {
            continue;
        }

        char unit[8] = {0};
        char currency_symbol[8] = {0};
        float price = 0.0f;
        bool has_price = false;
        ui_runtime_stock_symbol_from_entity(entity_id, items[count].symbol, sizeof(items[count].symbol));

        /* A Yahoo Finance or Easy Stock sensor (by its registry entry) is read like the Stocks & ETFs widget reads it: the
         * state is the price. Any other sensor keeps the reading below. */
        ha_quote_t quote;
        if (ha_quote_parse(ha_quote_provider_from_platform(ha_display_precision_platform(entity_id)), state.state,
                           state.attributes_json, &quote)) {
            if (!quote.available) {
                continue;
            }
            if (quote.symbol[0] != '\0') {
                ui_runtime_stock_symbol_from_entity(quote.symbol, items[count].symbol, sizeof(items[count].symbol));
            }
            const bool euro = strcmp(quote.currency, "EUR") == 0;
            snprintf(items[count].value, sizeof(items[count].value), "%.2f%s", quote.price,
                     euro ? "" : ha_quote_currency_sign(quote.currency));
            items[count].value_is_euro = euro;
            count++;
            continue;
        }

        cJSON *attrs = cJSON_Parse(state.attributes_json);
        if (attrs != NULL) {
            cJSON *symbol_item = cJSON_GetObjectItemCaseSensitive(attrs, "symbol");
            if (cJSON_IsString(symbol_item) && symbol_item->valuestring != NULL && symbol_item->valuestring[0] != '\0') {
                ui_runtime_stock_symbol_from_entity(symbol_item->valuestring, items[count].symbol, sizeof(items[count].symbol));
            }

            cJSON *price_item = cJSON_GetObjectItemCaseSensitive(attrs, "regularMarketPrice");
            has_price = ui_runtime_json_item_to_float(price_item, &price);

            cJSON *currency_item = cJSON_GetObjectItemCaseSensitive(attrs, "currencySymbol");
            if (cJSON_IsString(currency_item) && currency_item->valuestring != NULL) {
                snprintf(currency_symbol, sizeof(currency_symbol), "%s", currency_item->valuestring);
            }

            cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
            if (cJSON_IsString(unit_item) && unit_item->valuestring != NULL) {
                snprintf(unit, sizeof(unit), "%s", unit_item->valuestring);
            }
            cJSON_Delete(attrs);
        }
        bool is_euro = ui_runtime_stock_currency_is_euro(currency_symbol) ||
                       (currency_symbol[0] == '\0' && ui_runtime_stock_currency_is_euro(unit));

        char *end = NULL;
        float numeric = has_price ? price : strtof(state.state, &end);
        if (has_price || end != state.state) {
            const char *suffix = "";
            if (!is_euro) {
                suffix = currency_symbol[0] != '\0' ? currency_symbol : unit;
            }
            snprintf(items[count].value, sizeof(items[count].value), "%.2f%s", (double)numeric, suffix);
            items[count].value_is_euro = is_euro;
        } else {
            snprintf(items[count].value, sizeof(items[count].value), "%.12s%s", state.state, unit);
        }
        count++;
    }

    ui_pages_set_topbar_stocks(items, count);
}

/* A tap on the stock chip: the quote's history of the last 30 days or 12 months, from the Recorder statistics. */
static void ui_runtime_stock_chip_tap(void)
{
    const char *entity_id = s_topbar_stock_entity_ids[0];
    if (entity_id[0] == '\0') return;
    ha_state_t *state = calloc(1, sizeof(*state));
    if (state == NULL || !ha_model_get_state(entity_id, state)) {
        free(state);
        return;
    }
    char title[APP_MAX_NAME_LEN];
    char current[40];
    ui_runtime_stock_symbol_from_entity(entity_id, title, sizeof(title));
    snprintf(current, sizeof(current), "%.20s", state->state);
    ha_quote_t quote;
    const bool is_quote = ha_quote_parse(ha_quote_provider_from_platform(ha_display_precision_platform(entity_id)),
                                         state->state, state->attributes_json, &quote);
    if (is_quote && quote.available) {
        if (quote.symbol[0] != '\0') ui_runtime_stock_symbol_from_entity(quote.symbol, title, sizeof(title));
        snprintf(current, sizeof(current), "%.2f %s", quote.price, ha_quote_currency_sign(quote.currency));
    }
    cJSON *attrs = !is_quote ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        const cJSON *symbol = cJSON_GetObjectItemCaseSensitive(attrs, "symbol");
        if (cJSON_IsString(symbol) && symbol->valuestring != NULL && symbol->valuestring[0] != '\0') {
            ui_runtime_stock_symbol_from_entity(symbol->valuestring, title, sizeof(title));
        }
        const cJSON *price = cJSON_GetObjectItemCaseSensitive(attrs, "regularMarketPrice");
        float value = 0.0f;
        if (ui_runtime_json_item_to_float((cJSON *)price, &value)) snprintf(current, sizeof(current), "%.2f", (double)value);
        const cJSON *symbol_char = cJSON_GetObjectItemCaseSensitive(attrs, "currencySymbol");
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
        const char *suffix = cJSON_IsString(symbol_char) && symbol_char->valuestring[0] != '\0' ? symbol_char->valuestring
                             : cJSON_IsString(unit) ? unit->valuestring : "";
        if (suffix[0] != '\0') {
            const size_t used = strlen(current);
            snprintf(current + used, sizeof(current) - used, " %.8s", suffix);
        }
        cJSON_Delete(attrs);
    }
    free(state);
    ui_history_popup_open_range(entity_id, title, current);
}

static void ui_runtime_clamp_widget_rect(ui_widget_def_t *def)
{
    if (def == NULL) {
        return;
    }

    widget_size_limits_t limits = layout_orientation_widget_limits(def->type, def->style_variant, display_geometry_portrait());

    if (def->w < limits.min_w) {
        def->w = limits.min_w;
    }
    if (def->h < limits.min_h) {
        def->h = limits.min_h;
    }
    if (def->w > limits.max_w) {
        def->w = limits.max_w;
    }
    if (def->h > limits.max_h) {
        def->h = limits.max_h;
    }

    if (def->x < 0) {
        def->x = 0;
    }
    if (def->y < 0) {
        def->y = 0;
    }

    if (def->x + def->w > APP_VIEW_CONTENT_WIDTH) {
        def->x = APP_VIEW_CONTENT_WIDTH - def->w;
    }
    if (!display_geometry_portrait() && def->y + def->h > APP_VIEW_CONTENT_HEIGHT) {
        def->y = APP_VIEW_CONTENT_HEIGHT - def->h;
    }

    if (def->x < 0) {
        def->x = 0;
    }
    if (def->y < 0) {
        def->y = 0;
    }
}

static void ui_runtime_refresh_topbar(void)
{
    ui_pages_set_crash_warning(coredump_archive_count() > 0);
    time_t now = time(NULL);
    struct tm info = {0};
    localtime_r(&now, &info);

    bool wifi_connected = wifi_mgr_is_connected();
    bool wifi_setup_ap_active = wifi_mgr_is_setup_ap_active();
    bool ha_connected = ha_client_is_connected();
    bool ha_initial_sync_done = ha_client_is_initial_sync_done();

    bool datetime_changed = !s_topbar_cache.valid || info.tm_min != s_topbar_cache.minute ||
                            info.tm_hour != s_topbar_cache.hour || info.tm_mday != s_topbar_cache.day ||
                            info.tm_mon != s_topbar_cache.month || info.tm_year != s_topbar_cache.year;
    bool status_changed = !s_topbar_cache.valid || wifi_connected != s_topbar_cache.wifi_connected ||
                          wifi_setup_ap_active != s_topbar_cache.wifi_setup_ap_active ||
                          ha_connected != s_topbar_cache.ha_connected ||
                          ha_initial_sync_done != s_topbar_cache.ha_initial_sync_done;

    if (datetime_changed) {
        ui_pages_set_topbar_datetime(&info);
    }
    if (status_changed) {
        ui_pages_set_topbar_status(wifi_connected, wifi_setup_ap_active, ha_connected, ha_initial_sync_done);
    }

    s_topbar_cache.valid = true;
    s_topbar_cache.minute = info.tm_min;
    s_topbar_cache.hour = info.tm_hour;
    s_topbar_cache.day = info.tm_mday;
    s_topbar_cache.month = info.tm_mon;
    s_topbar_cache.year = info.tm_year;
    s_topbar_cache.wifi_connected = wifi_connected;
    s_topbar_cache.wifi_setup_ap_active = wifi_setup_ap_active;
    s_topbar_cache.ha_connected = ha_connected;
    s_topbar_cache.ha_initial_sync_done = ha_initial_sync_done;
}

static void ui_runtime_show_weather_icon_overlay(void)
{
#if APP_UI_TEST_WEATHER_ICON_OVERLAY
    if (s_weather_icon_overlay != NULL) {
        lv_obj_move_foreground(s_weather_icon_overlay);
        return;
    }

    const lv_font_t *font = mdi_font_weather();
    if (font == NULL) {
        font = mdi_font_large();
    }

    s_weather_icon_overlay = lv_label_create(lv_layer_top());
    lv_obj_set_ignore_layout(s_weather_icon_overlay, true);
    lv_obj_set_style_text_color(s_weather_icon_overlay, lv_color_hex(0x2FE3E3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_weather_icon_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    if (font != NULL) {
        lv_obj_set_style_text_font(s_weather_icon_overlay, font, LV_PART_MAIN);
    }

    /* Rainy icon U+F0597 rendered directly from weather font on top layer. */
    lv_label_set_text(s_weather_icon_overlay, "\xF3\xB0\x96\x97");
    lv_obj_align(s_weather_icon_overlay, LV_ALIGN_CENTER, 0, -20);
    lv_obj_move_foreground(s_weather_icon_overlay);

    ESP_LOGI(TAG_UI, "Weather icon overlay test enabled (font=%s)",
        (mdi_font_weather() != NULL) ? "72/56" : "none");
#endif
}

/* One widget definition on the heap (PSRAM) instead of two on the stack: it is ~2.7 KB and the layout
 * is built on the main task, whose stack is small and already carries the LVGL event chain. */
static ui_widget_def_t *ui_runtime_def_scratch(void)
{
    static ui_widget_def_t *s_def = NULL;
    if (s_def == NULL) {
        s_def = ui_calloc_prefer_psram(1, sizeof(*s_def));
    }
    if (s_def != NULL) {
        memset(s_def, 0, sizeof(*s_def));
    }
    return s_def;
}

/* Every entity a widget shows, once each, primary first: primary, secondary, tertiary, the extras
 * (room card list, car tile) and the forecast slots (solar forecast). One list for routing, the
 * initial state and the refresh, so a new entity slot is added in one place. */
#define UI_RUNTIME_MAX_WIDGET_ENTITIES (3 + UI_WIDGET_EXTRA_ENTITIES + 7)

static size_t ui_runtime_widget_entity_ids(const ui_widget_instance_t *w, const char **out, size_t max_out)
{
    const char *candidates[UI_RUNTIME_MAX_WIDGET_ENTITIES] = {
        w->entity_id, w->secondary_entity_id, w->tertiary_entity_id,
    };
    size_t n = 3;
    for (int i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) {
        candidates[n++] = w->extra_entity_id[i];
    }
    candidates[n++] = w->forecast_today_entity_id;
    candidates[n++] = w->forecast_tomorrow_entity_id;
    candidates[n++] = w->forecast_day_3_entity_id;
    candidates[n++] = w->forecast_day_4_entity_id;
    candidates[n++] = w->forecast_day_5_entity_id;
    candidates[n++] = w->forecast_day_6_entity_id;
    candidates[n++] = w->forecast_day_7_entity_id;
    size_t count = 0;
    for (size_t i = 0; i < n && count < max_out; i++) {
        if (candidates[i][0] == '\0') {
            continue;
        }
        bool seen = false;
        for (size_t j = 0; j < count; j++) {
            if (strncmp(out[j], candidates[i], APP_MAX_ENTITY_ID_LEN) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            out[count++] = candidates[i];
        }
    }
    return count;
}

static bool ui_runtime_widget_uses_entity(const ui_widget_instance_t *w, const char *entity_id)
{
    const char *ids[UI_RUNTIME_MAX_WIDGET_ENTITIES];
    const size_t n = ui_runtime_widget_entity_ids(w, ids, UI_RUNTIME_MAX_WIDGET_ENTITIES);
    for (size_t i = 0; i < n; i++) {
        if (strncmp(ids[i], entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static void ui_runtime_apply_entity_state_ex(const char *entity_id, bool mark_unavailable_if_missing)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return;
    }

    memset(&s_state_scratch, 0, sizeof(s_state_scratch));
    bool found = ha_model_get_state(entity_id, &s_state_scratch);
    for (size_t i = 0; i < s_widget_count; i++) {
        const bool is_primary = (strncmp(entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        if (!is_primary && !ui_runtime_widget_uses_entity(&s_widgets[i], entity_id)) {
            continue;
        }
        if (found) {
            ui_widget_factory_apply_state(&s_widgets[i], &s_state_scratch);
        } else if (is_primary && mark_unavailable_if_missing) {
            ui_widget_factory_mark_unavailable(&s_widgets[i]);
        }
    }

    if (found) {
        ui_runtime_apply_topbar_weather_state(&s_state_scratch);
        for (size_t i = 0; i < s_energy_page_count; i++) {
            (void)ui_energy_page_apply_state(&s_energy_pages[i], &s_state_scratch);
        }
    } else if (s_topbar_weather_enabled &&
               strncmp(entity_id, s_topbar_weather_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        ui_pages_set_topbar_weather(false, 0.0f, NULL, 0U);
    }

    if (ui_runtime_is_topbar_stock_entity(entity_id)) {
        ui_runtime_refresh_topbar_stocks();
    }
}

static void ui_runtime_apply_entity_state(const char *entity_id)
{
    ui_runtime_apply_entity_state_ex(entity_id, true);
}

static void ui_runtime_apply_widget_current_state(ui_widget_instance_t *widget, bool mark_unavailable_if_missing)
{
    if (widget == NULL) {
        return;
    }

    if (widget->entity_id[0] != '\0') {
        memset(&s_state_scratch, 0, sizeof(s_state_scratch));
        bool found = ha_model_get_state(widget->entity_id, &s_state_scratch);
        if (found) {
            ui_widget_factory_apply_state(widget, &s_state_scratch);
        } else if (mark_unavailable_if_missing) {
            ui_widget_factory_mark_unavailable(widget);
        }
    }

    const char *ids[UI_RUNTIME_MAX_WIDGET_ENTITIES];
    const size_t n = ui_runtime_widget_entity_ids(widget, ids, UI_RUNTIME_MAX_WIDGET_ENTITIES);
    for (size_t i = 0; i < n; i++) {
        if (strncmp(ids[i], widget->entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            continue; /* the primary entity was applied above */
        }
        memset(&s_state_scratch, 0, sizeof(s_state_scratch));
        if (ha_model_get_state(ids[i], &s_state_scratch)) {
            ui_widget_factory_apply_state(widget, &s_state_scratch);
        }
    }
}

static void ui_runtime_update_widget_visibility(const char *page_id, bool refresh_visible_widgets)
{
    if (page_id == NULL || page_id[0] == '\0') {
        return;
    }

    for (size_t i = 0; i < s_widget_count; i++) {
        bool visible = (strncmp(s_widgets[i].page_id, page_id, APP_MAX_PAGE_ID_LEN) == 0);
        ui_widget_factory_set_visible(&s_widgets[i], visible);
        if (visible && refresh_visible_widgets && ui_widget_factory_needs_refresh_on_show(s_widgets[i].type)) {
            ui_runtime_apply_widget_current_state(&s_widgets[i], false);
        }
    }
}

static void ui_runtime_apply_all_states(void)
{
    /* One pass over the widgets: applying per entity would visit every widget that shares it again (quadratic). */
    for (size_t i = 0; i < s_widget_count; i++) {
        ui_runtime_apply_widget_current_state(&s_widgets[i], true);
    }
    for (size_t i = 0; i < s_energy_page_count; i++) {
        ui_energy_page_apply_all_states(&s_energy_pages[i]);
    }
    if (s_topbar_weather_enabled) {
        ui_runtime_apply_entity_state(s_topbar_weather_entity_id);
    } else {
        ui_pages_set_topbar_weather(false, 0.0f, NULL, 0U);
    }
    ui_runtime_refresh_topbar_stocks();
}

static void ui_runtime_apply_all_states_preserve_missing(void)
{
    /* One pass over the widgets: applying per entity would visit every widget that shares it again (quadratic). */
    for (size_t i = 0; i < s_widget_count; i++) {
        ui_runtime_apply_widget_current_state(&s_widgets[i], false);
    }
    for (size_t i = 0; i < s_energy_page_count; i++) {
        ui_energy_page_apply_all_states(&s_energy_pages[i]);
    }
    if (s_topbar_weather_enabled) {
        ui_runtime_apply_entity_state_ex(s_topbar_weather_entity_id, false);
    } else {
        ui_pages_set_topbar_weather(false, 0.0f, NULL, 0U);
    }
    ui_runtime_refresh_topbar_stocks();
}

static bool ui_runtime_widget_from_json(cJSON *widget_json, ui_widget_def_t *out)
{
    cJSON *id = cJSON_GetObjectItemCaseSensitive(widget_json, "id");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(widget_json, "type");
    cJSON *title = cJSON_GetObjectItemCaseSensitive(widget_json, "title");
    cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "entity_id");
    cJSON *secondary_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "secondary_entity_id");
    cJSON *tertiary_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "tertiary_entity_id");
    cJSON *forecast_today_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_today_entity_id");
    cJSON *forecast_tomorrow_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_tomorrow_entity_id");
    cJSON *forecast_day_3_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_day_3_entity_id");
    cJSON *forecast_day_4_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_day_4_entity_id");
    cJSON *forecast_day_5_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_day_5_entity_id");
    cJSON *forecast_day_6_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_day_6_entity_id");
    cJSON *forecast_day_7_entity_id = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_day_7_entity_id");
    cJSON *solar_forecast_bar_max_kwh = cJSON_GetObjectItemCaseSensitive(widget_json, "solar_forecast_bar_max_kwh");
    cJSON *solar_forecast_bar_orientation = cJSON_GetObjectItemCaseSensitive(widget_json, "solar_forecast_bar_orientation");
    cJSON *sensor_show_age = cJSON_GetObjectItemCaseSensitive(widget_json, "sensor_show_age");
    cJSON *lock_confirm = cJSON_GetObjectItemCaseSensitive(widget_json, "lock_confirm");
    cJSON *price_selector = cJSON_GetObjectItemCaseSensitive(widget_json, "price_selector");
    cJSON *price_cheap_times = cJSON_GetObjectItemCaseSensitive(widget_json, "price_cheap_times");
    cJSON *price_line = cJSON_GetObjectItemCaseSensitive(widget_json, "price_line");
    cJSON *price_average = cJSON_GetObjectItemCaseSensitive(widget_json, "price_average");
    cJSON *parqet_hide_amounts = cJSON_GetObjectItemCaseSensitive(widget_json, "parqet_hide_amounts");
    cJSON *history_popup = cJSON_GetObjectItemCaseSensitive(widget_json, "history_popup");
    cJSON *gauge_min = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_min");
    cJSON *gauge_max = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_max");
    cJSON *gauge_bar_style = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_bar_style");
    cJSON *gauge_segment_count = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_segment_count");
    cJSON *gauge_color_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_color_mode");
    cJSON *gauge_color_low = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_color_low");
    cJSON *gauge_color_high = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_color_high");
    cJSON *gauge_target_color = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_target_color");
    cJSON *gauge_target_low = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_target_low");
    cJSON *gauge_target_high = cJSON_GetObjectItemCaseSensitive(widget_json, "gauge_target_high");
    cJSON *slider_direction = cJSON_GetObjectItemCaseSensitive(widget_json, "slider_direction");
    cJSON *slider_accent_color = cJSON_GetObjectItemCaseSensitive(widget_json, "slider_accent_color");
    cJSON *button_accent_color = cJSON_GetObjectItemCaseSensitive(widget_json, "button_accent_color");
    cJSON *button_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "button_mode");
    cJSON *graph_line_color = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_line_color");
    cJSON *graph_point_count = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_point_count");
    cJSON *graph_time_window_min = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_time_window_min");
    cJSON *graph_display_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_display_mode");
    cJSON *graph_bar_bucket_min = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_bar_bucket_min");
    cJSON *graph_ha_history = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_ha_history");
    cJSON *style_variant = cJSON_GetObjectItemCaseSensitive(widget_json, "style_variant");
    cJSON *arc_opening = cJSON_GetObjectItemCaseSensitive(widget_json, "arc_opening");
    cJSON *default_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "default_mode");
    cJSON *display_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "display_mode");
    cJSON *rect = cJSON_GetObjectItemCaseSensitive(widget_json, "rect");
    if (!cJSON_IsString(id) || !cJSON_IsString(type) || !cJSON_IsObject(rect)) {
        return false;
    }

    const bool requires_entity = (strcmp(type->valuestring, "empty_tile") != 0);
    if (requires_entity && !cJSON_IsString(entity_id)) {
        return false;
    }

    cJSON *x = cJSON_GetObjectItemCaseSensitive(rect, "x");
    cJSON *y = cJSON_GetObjectItemCaseSensitive(rect, "y");
    cJSON *w = cJSON_GetObjectItemCaseSensitive(rect, "w");
    cJSON *h = cJSON_GetObjectItemCaseSensitive(rect, "h");
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsNumber(w) || !cJSON_IsNumber(h)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    cJSON *forecast_days = cJSON_GetObjectItemCaseSensitive(widget_json, "forecast_days");
    out->forecast_days = cJSON_IsNumber(forecast_days) ? forecast_days->valueint : -1;
    out->align_forecast = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget_json, "align_forecast"));
    out->weather_hourly = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget_json, "weather_hourly"));
    out->show_precipitation = true;
    cJSON *show_precip = cJSON_GetObjectItemCaseSensitive(widget_json, "show_precipitation");
    if (cJSON_IsBool(show_precip)) {
        out->show_precipitation = cJSON_IsTrue(show_precip);
    }
    out->room_controls = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget_json, "room_controls"));
    out->hide_name = !widget_show_name(widget_json);
    out->lock_confirm = cJSON_IsTrue(lock_confirm);
    out->price_selector = cJSON_IsTrue(price_selector); /* off unless the layout says true: the running 24 hours */
    out->price_cheap_times = cJSON_IsTrue(price_cheap_times);
    out->price_line = cJSON_IsTrue(price_line);
    out->parqet_hide_amounts = cJSON_IsTrue(parqet_hide_amounts);
    out->parqet_selector = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(widget_json, "parqet_selector"));
    ui_runtime_copy_json_string(widget_json, "parqet_view", out->parqet_view, sizeof(out->parqet_view));
    ui_runtime_copy_json_string(widget_json, "parqet_interval", out->parqet_interval, sizeof(out->parqet_interval));
    out->price_average = cJSON_IsNumber(price_average) && price_average->valuedouble > 0.0 ? (float)price_average->valuedouble : 0.0f;
    out->camera_live = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget_json, "camera_live"));
    out->alarm_actions = ha_alarm_action_mask_parse(cJSON_GetObjectItemCaseSensitive(widget_json, "alarm_actions"));
    out->history_popup = cJSON_IsTrue(history_popup);
    ui_runtime_copy_json_string(widget_json, "sensor_format", out->sensor_format, sizeof(out->sensor_format));
    ui_runtime_copy_json_string(widget_json, "sensor_unit", out->sensor_unit, sizeof(out->sensor_unit));
    out->sensor_show_age = true;
    if (cJSON_IsBool(sensor_show_age)) {
        out->sensor_show_age = cJSON_IsTrue(sensor_show_age);
    }
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    snprintf(out->type, sizeof(out->type), "%s", type->valuestring);
    if (strcmp(out->type, "energy_timeline") == 0) out->energy_palette = etl_palette_from_json(widget_json);
    if (strcmp(out->type, "roborock_tile") == 0) {
        snprintf(out->type, sizeof(out->type), "%s", "vacuum_tile"); /* legacy name */
    }
    snprintf(out->title, sizeof(out->title), "%s", cJSON_IsString(title) ? title->valuestring : id->valuestring);
    if (cJSON_IsString(entity_id) && entity_id->valuestring != NULL) {
        snprintf(out->entity_id, sizeof(out->entity_id), "%s", entity_id->valuestring);
    }
    if (cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL) {
        snprintf(out->secondary_entity_id, sizeof(out->secondary_entity_id), "%s", secondary_entity_id->valuestring);
    }
    if (cJSON_IsString(tertiary_entity_id) && tertiary_entity_id->valuestring != NULL) {
        snprintf(out->tertiary_entity_id, sizeof(out->tertiary_entity_id), "%s", tertiary_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_today_entity_id) && forecast_today_entity_id->valuestring != NULL) {
        snprintf(out->forecast_today_entity_id, sizeof(out->forecast_today_entity_id), "%s", forecast_today_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_tomorrow_entity_id) && forecast_tomorrow_entity_id->valuestring != NULL) {
        snprintf(out->forecast_tomorrow_entity_id, sizeof(out->forecast_tomorrow_entity_id), "%s", forecast_tomorrow_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_day_3_entity_id) && forecast_day_3_entity_id->valuestring != NULL) {
        snprintf(out->forecast_day_3_entity_id, sizeof(out->forecast_day_3_entity_id), "%s", forecast_day_3_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_day_4_entity_id) && forecast_day_4_entity_id->valuestring != NULL) {
        snprintf(out->forecast_day_4_entity_id, sizeof(out->forecast_day_4_entity_id), "%s", forecast_day_4_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_day_5_entity_id) && forecast_day_5_entity_id->valuestring != NULL) {
        snprintf(out->forecast_day_5_entity_id, sizeof(out->forecast_day_5_entity_id), "%s", forecast_day_5_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_day_6_entity_id) && forecast_day_6_entity_id->valuestring != NULL) {
        snprintf(out->forecast_day_6_entity_id, sizeof(out->forecast_day_6_entity_id), "%s", forecast_day_6_entity_id->valuestring);
    }
    if (cJSON_IsString(forecast_day_7_entity_id) && forecast_day_7_entity_id->valuestring != NULL) {
        snprintf(out->forecast_day_7_entity_id, sizeof(out->forecast_day_7_entity_id), "%s", forecast_day_7_entity_id->valuestring);
    }
    if (strcmp(out->type, "car_widget") == 0) {
        /* Optional extras of the car tile: pre-conditioning, lock / door status, interior temperature
         * (extra_entity_id[UI_CAR_EXTRA_*], see ui_widget_factory.h). */
        ui_runtime_copy_json_string(widget_json, "precond_entity_id", out->extra_entity_id[UI_CAR_EXTRA_PRECOND],
            sizeof(out->extra_entity_id[UI_CAR_EXTRA_PRECOND]));
        ui_runtime_copy_json_string(widget_json, "lock_entity_id", out->extra_entity_id[UI_CAR_EXTRA_LOCK],
            sizeof(out->extra_entity_id[UI_CAR_EXTRA_LOCK]));
        ui_runtime_copy_json_string(widget_json, "interior_temp_entity_id", out->extra_entity_id[UI_CAR_EXTRA_TEMP],
            sizeof(out->extra_entity_id[UI_CAR_EXTRA_TEMP]));
    }
    if (!strcmp(out->type, "wallbox_evcc")) {
        out->entity_id[0] = out->secondary_entity_id[0] = out->tertiary_entity_id[0] = 0;
        memset(out->extra_entity_id, 0, sizeof(out->extra_entity_id));
        evcc_slots_from_json(widget_json, out->entity_id, out->secondary_entity_id, out->tertiary_entity_id, out->extra_entity_id, UI_WIDGET_EXTRA_ENTITIES);
    }
    if (strcmp(out->type, "bambu_printer") == 0) {
        /* Named bindings only: generic slots a hand-edited layout might carry must not leak into the widget. */
        out->entity_id[0] = out->secondary_entity_id[0] = out->tertiary_entity_id[0] = '\0';
        memset(out->extra_entity_id, 0, sizeof(out->extra_entity_id));
        bambu_printer_slots_from_json(widget_json, out->entity_id, out->secondary_entity_id, out->tertiary_entity_id,
                                      out->extra_entity_id, UI_WIDGET_EXTRA_ENTITIES);
    }
    if (strcmp(out->type, "vacuum_tile") == 0 || strcmp(out->type, "roborock_tile") == 0) {
        ui_runtime_copy_json_string(widget_json, "battery_entity_id", out->extra_entity_id[UI_VACUUM_EXTRA_BATTERY],
            sizeof(out->extra_entity_id[UI_VACUUM_EXTRA_BATTERY]));
    }
    if (strcmp(out->type, "parqet_portfolio") == 0) {
        ui_runtime_copy_json_string(widget_json, "dividends_entity_id", out->extra_entity_id[UI_PARQET_EXTRA_DIVIDENDS],
            sizeof(out->extra_entity_id[UI_PARQET_EXTRA_DIVIDENDS]));
        ui_runtime_copy_json_string(widget_json, "return_entity_id", out->extra_entity_id[UI_PARQET_EXTRA_RETURN],
            sizeof(out->extra_entity_id[UI_PARQET_EXTRA_RETURN]));
    }
    if (strcmp(out->type, "stock_quotes") == 0) {
        cJSON *more = cJSON_GetObjectItemCaseSensitive(widget_json, "stock_entities");
        for (int i = 0; i < UI_STOCK_MAX_QUOTES - 1; i++) {
            const cJSON *item = cJSON_GetArrayItem(more, i);
            if (cJSON_IsString(item) && item->valuestring != NULL) {
                snprintf(out->extra_entity_id[i], APP_MAX_ENTITY_ID_LEN, "%s", item->valuestring);
            }
        }
        ui_runtime_copy_json_string(widget_json, "stock_view", out->display_mode, sizeof(out->display_mode));
        ui_runtime_copy_json_string(widget_json, "stock_period", out->stock_period, sizeof(out->stock_period));
        ui_runtime_copy_json_string(widget_json, "stock_delta", out->stock_delta, sizeof(out->stock_delta));
        out->stock_ticker = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(widget_json, "stock_ticker"));
        out->stock_selector = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(widget_json, "stock_selector"));
    }
    if (strcmp(out->type, "energy_timeline") == 0) {
        ui_runtime_copy_json_string(widget_json, "battery_charge_entity_id",
            out->extra_entity_id[UI_ETL_EXTRA_BATTERY_CHARGE], sizeof(out->extra_entity_id[UI_ETL_EXTRA_BATTERY_CHARGE]));
        ui_runtime_copy_json_string(widget_json, "battery_discharge_entity_id",
            out->extra_entity_id[UI_ETL_EXTRA_BATTERY_DISCHARGE], sizeof(out->extra_entity_id[UI_ETL_EXTRA_BATTERY_DISCHARGE]));
        ui_runtime_copy_json_string(widget_json, "battery_soc_entity_id",
            out->extra_entity_id[UI_ETL_EXTRA_BATTERY_SOC], sizeof(out->extra_entity_id[UI_ETL_EXTRA_BATTERY_SOC]));
    }
    if (strcmp(out->type, "room_tile") == 0) {
        /* The extra field (room_extra_mode / room_extra_entity_id) reuses display_mode and secondary_entity_id, which a
         * room card does not otherwise use; the entity is always one the card already has. */
        ui_runtime_copy_json_string(widget_json, "room_extra_mode", out->display_mode, sizeof(out->display_mode));
        ui_runtime_copy_json_string(widget_json, "room_extra_entity_id", out->secondary_entity_id,
            sizeof(out->secondary_entity_id));
        /* Room card: up to 7 more entities after entity_id. */
        cJSON *room_entities = cJSON_GetObjectItemCaseSensitive(widget_json, "room_entities");
        int n = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, room_entities) {
            if (n < UI_WIDGET_EXTRA_ENTITIES && cJSON_IsString(item) && item->valuestring != NULL) {
                snprintf(out->extra_entity_id[n], APP_MAX_ENTITY_ID_LEN, "%s", item->valuestring);
                n++;
            }
        }
    }
    if (cJSON_IsNumber(solar_forecast_bar_max_kwh)) {
        out->solar_forecast_bar_max_kwh = (float)solar_forecast_bar_max_kwh->valuedouble;
    }
    out->gauge_min = cJSON_IsNumber(gauge_min) ? (float)gauge_min->valuedouble : 0.0f;
    out->gauge_max = cJSON_IsNumber(gauge_max) ? (float)gauge_max->valuedouble : 100.0f;
    snprintf(out->gauge_bar_style, sizeof(out->gauge_bar_style), "%s",
        (cJSON_IsString(gauge_bar_style) && gauge_bar_style->valuestring != NULL) ? gauge_bar_style->valuestring : "");
    snprintf(out->gauge_color_mode, sizeof(out->gauge_color_mode), "%s",
        (cJSON_IsString(gauge_color_mode) && gauge_color_mode->valuestring != NULL) ? gauge_color_mode->valuestring : "");
    snprintf(out->gauge_color_low, sizeof(out->gauge_color_low), "%s",
        (cJSON_IsString(gauge_color_low) && gauge_color_low->valuestring != NULL) ? gauge_color_low->valuestring : "");
    snprintf(out->gauge_color_high, sizeof(out->gauge_color_high), "%s",
        (cJSON_IsString(gauge_color_high) && gauge_color_high->valuestring != NULL) ? gauge_color_high->valuestring : "");
    snprintf(out->gauge_target_color, sizeof(out->gauge_target_color), "%s",
        (cJSON_IsString(gauge_target_color) && gauge_target_color->valuestring != NULL) ? gauge_target_color->valuestring : "");
    out->gauge_target_low = cJSON_IsNumber(gauge_target_low) ? (float)gauge_target_low->valuedouble : out->gauge_min;
    out->gauge_target_high = cJSON_IsNumber(gauge_target_high) ? (float)gauge_target_high->valuedouble : out->gauge_max;
    if (cJSON_IsString(solar_forecast_bar_orientation) && solar_forecast_bar_orientation->valuestring != NULL) {
        snprintf(out->solar_forecast_bar_orientation, sizeof(out->solar_forecast_bar_orientation), "%s",
            solar_forecast_bar_orientation->valuestring);
    }
    if (cJSON_IsString(slider_direction) && slider_direction->valuestring != NULL) {
        snprintf(out->slider_direction, sizeof(out->slider_direction), "%s", slider_direction->valuestring);
    }
    if (cJSON_IsString(slider_accent_color) && slider_accent_color->valuestring != NULL) {
        snprintf(out->slider_accent_color, sizeof(out->slider_accent_color), "%s", slider_accent_color->valuestring);
    }
    if (cJSON_IsString(button_accent_color) && button_accent_color->valuestring != NULL) {
        snprintf(out->button_accent_color, sizeof(out->button_accent_color), "%s", button_accent_color->valuestring);
    }
    if (cJSON_IsString(button_mode) && button_mode->valuestring != NULL) {
        snprintf(out->button_mode, sizeof(out->button_mode), "%s", button_mode->valuestring);
    }
    if (cJSON_IsString(graph_line_color) && graph_line_color->valuestring != NULL) {
        snprintf(out->graph_line_color, sizeof(out->graph_line_color), "%s", graph_line_color->valuestring);
    }
    if (cJSON_IsNumber(graph_point_count)) {
        out->graph_point_count = graph_point_count->valueint;
    }
    if (cJSON_IsNumber(gauge_segment_count)) {
        out->gauge_segment_count = gauge_segment_count->valueint;
    }
    if (cJSON_IsNumber(graph_time_window_min)) {
        out->graph_time_window_min = graph_time_window_min->valueint;
    }
    if (cJSON_IsString(graph_display_mode) && graph_display_mode->valuestring != NULL) {
        snprintf(out->graph_display_mode, sizeof(out->graph_display_mode), "%s", graph_display_mode->valuestring);
    }
    if (cJSON_IsNumber(graph_bar_bucket_min)) {
        out->graph_bar_bucket_min = graph_bar_bucket_min->valueint;
    }
    out->graph_ha_history = cJSON_IsTrue(graph_ha_history);
    if (cJSON_IsString(style_variant) && style_variant->valuestring != NULL) {
        snprintf(out->style_variant, sizeof(out->style_variant), "%s", style_variant->valuestring);
    }
    if (cJSON_IsString(default_mode) && default_mode->valuestring != NULL) {
        snprintf(out->default_mode, sizeof(out->default_mode), "%s", default_mode->valuestring);
    }
    if (cJSON_IsString(display_mode) && display_mode->valuestring != NULL) {
        snprintf(out->display_mode, sizeof(out->display_mode), "%s", display_mode->valuestring);
    }
    /* The wallbox header lives in fields the wallbox does not use otherwise (layout/evcc_slots.h). */
    if (!strcmp(out->type, "wallbox_evcc")) {
        char *const site[EVCC_SITE_COUNT] = {out->forecast_today_entity_id, out->forecast_tomorrow_entity_id,
                                             out->forecast_day_3_entity_id, out->forecast_day_4_entity_id};
        out->forecast_day_5_entity_id[0] = '\0';
        out->forecast_day_6_entity_id[0] = '\0';
        out->forecast_day_7_entity_id[0] = '\0';
        evcc_header_from_json(widget_json, out->style_variant, sizeof(out->style_variant), &out->gauge_max, site);
    }
    /* A binary tile in the "door" look is drawn by the door / window contact renderer. */
    if (strcmp(out->type, "binary_tile") == 0 && strcmp(out->display_mode, "door") == 0) {
        snprintf(out->type, sizeof(out->type), "%s", "contact_tile");
    }
    if (cJSON_IsString(arc_opening) && arc_opening->valuestring != NULL) {
        snprintf(out->arc_opening, sizeof(out->arc_opening), "%s", arc_opening->valuestring);
    }
    out->x = x->valueint;
    out->y = y->valueint;
    out->w = w->valueint;
    out->h = h->valueint;
    ui_runtime_clamp_widget_rect(out);
    return true;
}

static void ui_runtime_copy_json_string(cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    cJSON *item = (obj != NULL) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

/* Display text of an alert: cut at a codepoint or emoji unit boundary, as the REST/MQTT notifications are. */
static void ui_runtime_copy_text_field(cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    cJSON *item = (obj != NULL) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        notif_text_copy(dst, dst_size, item->valuestring);
    }
}

static bool ui_runtime_is_background_widget_type(const char *type)
{
    return type != NULL && strcmp(type, "empty_tile") == 0;
}

/* House page (ui_house_page.c): every room becomes a room_tile registered like a widget of a normal
 * page, so subscriptions, state updates and the popup work the same. */
static bool ui_runtime_house_room_cb(const ui_widget_def_t *def, lv_obj_t *parent, void *user)
{
    const char *page_id = (const char *)user;
    if (s_widget_count >= APP_MAX_WIDGETS_TOTAL ||
        ui_widget_factory_create(def, parent, &s_widgets[s_widget_count]) != ESP_OK) {
        return false;
    }
    snprintf(s_widgets[s_widget_count].page_id, sizeof(s_widgets[s_widget_count].page_id), "%s", page_id);
    s_widget_count++;
    return true;
}

/* How long each widget took to create, for the build profile line: the cost per widget should not grow with the widget count. */
static uint32_t s_create_us[APP_MAX_WIDGETS_TOTAL];

static void ui_runtime_log_build_profile(int64_t built_us, int64_t shown_us, int64_t states_us, int64_t total_us)
{
    const size_t n = s_widget_count;
    if (n < 6) {
        return;
    }
    const size_t third = n / 3;
    uint64_t sum[3] = {0, 0, 0};
    for (size_t i = 0; i < n; i++) {
        sum[i < third ? 0 : (i < 2 * third ? 1 : 2)] += s_create_us[i];
    }
    ESP_LOGI(TAG_UI, "layout build: %u widgets, create %lld ms, page show %lld ms, states %lld ms, total %lld ms; per widget by thirds: %llu / %llu / %llu us",
             (unsigned)n, (long long)(built_us / 1000), (long long)(shown_us / 1000), (long long)(states_us / 1000), (long long)(total_us / 1000), (unsigned long long)(sum[0] / third),
             (unsigned long long)(sum[1] / third), (unsigned long long)(sum[2] / (n - 2 * third)));
}

static esp_err_t ui_runtime_apply_layout(const char *layout_json, const runtime_settings_t *live, const theme_entry_t *palette)
{
    if (!s_initialized || layout_json == NULL || s_widgets == NULL || s_energy_pages == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_Parse(layout_json);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    if (!layout_orientation_project(root, display_geometry_portrait())) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
#endif
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsArray(pages)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!display_lock(UI_RUNTIME_LOCK_MS)) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }

    if (live != NULL && palette != NULL) {
        /* Reload custom translations too, even when the language code did not change. */
        esp_err_t language_err = ui_i18n_init(live->ui_language);
        if (language_err != ESP_OK) { display_unlock(); cJSON_Delete(root); return language_err; }
        theme_palette_set_active(&palette->palette, palette->id, palette->name);
        ui_style_set(ui_style_from_name(live->ui_style));
        ui_fonts_set_user_step(live->ui_text_step);
        theme_default_rebuild_styles();
    }
    ui_runtime_load_alerts(root);
    ui_runtime_check_alerts(NULL);

    /* A rebuild after a layout save returns to the page on screen, not the first page. */
    char shown_page[APP_MAX_PAGE_ID_LEN];
    snprintf(shown_page, sizeof(shown_page), "%s", ui_pages_current_id());

    s_topbar_cache.valid = false;
    ui_pages_reset();
    ui_runtime_load_topbar_settings();

    esp_err_t build_err = ESP_OK;
    const int64_t build_start_us = esp_timer_get_time();
    memset(s_widgets, 0, APP_MAX_WIDGETS_TOTAL * sizeof(*s_widgets));
    s_widget_count = 0;
    memset(s_energy_pages, 0, APP_MAX_PAGES * sizeof(*s_energy_pages));
    s_energy_page_count = 0;

    int page_count = cJSON_GetArraySize(pages);
    for (int p = 0; p < page_count; p++) {
        cJSON *page = cJSON_GetArrayItem(pages, p);
        cJSON *page_id = cJSON_GetObjectItemCaseSensitive(page, "id");
        cJSON *page_title = cJSON_GetObjectItemCaseSensitive(page, "title");
        cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
        cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
        if (!cJSON_IsString(page_id)) {
            continue;
        }

        lv_obj_t *page_container = ui_pages_add(
            page_id->valuestring, cJSON_IsString(page_title) ? page_title->valuestring : page_id->valuestring);
        if (page_container == NULL) {
            build_err = ESP_ERR_NO_MEM;
            continue;
        }
        {
            /* Nav icon: the page's own choice, else house / energy pages get theirs. */
            cJSON *page_icon = cJSON_GetObjectItemCaseSensitive(page, "icon");
            const char *type_str = cJSON_IsString(page_type) && page_type->valuestring != NULL ? page_type->valuestring : "";
            const char *icon = (cJSON_IsString(page_icon) && page_icon->valuestring != NULL) ? page_icon->valuestring : "";
            if (icon[0] == '\0') {
                icon = strcmp(type_str, "house_dashboard") == 0 ? "house"
                       : (strcmp(type_str, "energy_dashboard") == 0 ? "energy" : "");
            }
            ui_pages_set_icon(page_id->valuestring, icon);
        }

        if (cJSON_IsString(page_type) && page_type->valuestring != NULL &&
            strcmp(page_type->valuestring, "house_dashboard") == 0) {
            ui_house_page_build(page, page_container, ui_runtime_house_room_cb, (void *)page_id->valuestring);
            continue;
        }
        bool is_energy_page = cJSON_IsString(page_type) && page_type->valuestring != NULL &&
                              strcmp(page_type->valuestring, "energy_dashboard") == 0;
        if (is_energy_page) {
            if (s_energy_page_count >= APP_MAX_PAGES) {
                continue;
            }
            ui_energy_page_config_t energy_config = {0};
            ui_energy_page_config_from_json(page,
                page_id->valuestring,
                cJSON_IsString(page_title) ? page_title->valuestring : page_id->valuestring,
                &energy_config);
            esp_err_t err =
                ui_energy_page_create(&energy_config, page_container, &s_energy_pages[s_energy_page_count]);
            if (err == ESP_OK) {
                s_energy_page_count++;
            } else if (err == ESP_ERR_NO_MEM) build_err = err;
            continue;
        }

        if (!cJSON_IsArray(widgets)) {
            continue;
        }

        int widget_count = cJSON_GetArraySize(widgets);
        for (int pass = 0; pass < 2; pass++) {
            const bool background_pass = (pass == 0);
            for (int w = 0; w < widget_count; w++) {
                if (s_widget_count >= APP_MAX_WIDGETS_TOTAL) {
                    break;
                }
                ui_widget_def_t *def = ui_runtime_def_scratch();
                if (def == NULL) {
                    build_err = ESP_ERR_NO_MEM;
                    break;
                }
                if (!ui_runtime_widget_from_json(cJSON_GetArrayItem(widgets, w), def)) {
                    continue;
                }
                bool is_background = ui_runtime_is_background_widget_type(def->type);
                if (is_background != background_pass) {
                    continue;
                }
                const int64_t create_start_us = esp_timer_get_time();
                esp_err_t err = ui_widget_factory_create(def, page_container, &s_widgets[s_widget_count]);
                if (err == ESP_ERR_NO_MEM) build_err = err;
                if (s_widget_count < APP_MAX_WIDGETS_TOTAL) s_create_us[s_widget_count] = (uint32_t)(esp_timer_get_time() - create_start_us);
                if (err == ESP_OK) {
                    snprintf(s_widgets[s_widget_count].page_id, sizeof(s_widgets[s_widget_count].page_id),
                             "%s", page_id->valuestring);
                    s_widget_count++;
                }
            }
        }
    }

    cJSON_Delete(root);
    const int64_t built_us = esp_timer_get_time() - build_start_us;

    const int64_t show_start_us = esp_timer_get_time();
    if (ui_pages_count() > 0 && (shown_page[0] == '\0' || !ui_pages_show(shown_page))) {
        ui_pages_show_index(0);
    }
    const int64_t shown_us = esp_timer_get_time() - show_start_us;
    const int64_t states_start_us = esp_timer_get_time();
    ui_runtime_apply_all_states();
    const int64_t states_us = esp_timer_get_time() - states_start_us;
    ui_runtime_refresh_topbar();
    display_unlock();
    ui_runtime_log_build_profile(built_us, shown_us, states_us, esp_timer_get_time() - build_start_us);
    ESP_LOGI(TAG_UI, "Layout loaded: %u widgets", (unsigned)s_widget_count);
    return build_err;
}

static uint32_t s_applied_layout_revision;
static bool s_safe_layout_reload;

esp_err_t ui_runtime_load_layout(const char *layout_json)
{
    return ui_runtime_apply_layout(layout_json, NULL, NULL);
}

esp_err_t ui_runtime_reload_layout(void)
{
    const uint32_t revision = app_events_layout_revision();
    /* Prepare files before taking the display lock; retain the current UI on errors. */
    runtime_settings_t *live = calloc(1, sizeof(*live));
    if (live == NULL) return ESP_ERR_NO_MEM;
    theme_entry_t palette;
    esp_err_t err = runtime_settings_load(live);
    if (err == ESP_OK) err = theme_store_load_active(&palette);
    char *json = NULL;
    if (err == ESP_OK) {
        if (boot_guard_take_safe_layout()) s_safe_layout_reload = true;
        if (s_safe_layout_reload) {
            json = strdup(layout_store_default_json());
            if (json == NULL) err = ESP_ERR_NO_MEM;
        } else {
            err = layout_store_load(&json);
        }
    }
    if (err == ESP_OK) err = ui_runtime_apply_layout(json, live, &palette);
    if (err == ESP_OK) {
        s_safe_layout_reload = false;
        s_applied_layout_revision = revision;
        w_graph_request_history_cleanup();
    }
    free(json);
    free(live);
    return err;
}

/* ---- Alerts: "when this entity fires, show a notification card" (for example a doorbell) ----
 * Configured in the layout as "alerts": [{entity_id, title, message, timeout_sec, action_entity_id, action_label}].
 * An event.* entity fires when its state (the timestamp of the last event) changes; any other entity when it turns on.
 * The state present when the layout is loaded is only remembered, so a restart never rings. */
#define UI_MAX_ALERTS 4

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_NOTIFICATION_MAX_TITLE_LEN];
    char message[APP_NOTIFICATION_MAX_MESSAGE_LEN];
    char action_entity_id[APP_MAX_ENTITY_ID_LEN];
    char action_label[APP_NOTIFICATION_MAX_ACTION_LABEL_LEN];
    char camera_entity_id[APP_MAX_ENTITY_ID_LEN];
    uint32_t timeout_sec;
    chime_kind_t sound;
    char last_state[APP_MAX_STATE_LEN];
    bool primed;
} ui_alert_t;

static ui_alert_t s_alerts[UI_MAX_ALERTS];
static size_t s_alert_count = 0;

static void ui_runtime_load_alerts(cJSON *root)
{
    memset(s_alerts, 0, sizeof(s_alerts));
    s_alert_count = 0;
    cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts");
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, alerts) {
        if (s_alert_count >= UI_MAX_ALERTS || !cJSON_IsObject(item)) {
            break;
        }
        ui_alert_t *alert = &s_alerts[s_alert_count];
        ui_runtime_copy_json_string(item, "entity_id", alert->entity_id, sizeof(alert->entity_id));
        if (alert->entity_id[0] == '\0') {
            continue;
        }
        ui_runtime_copy_text_field(item, "title", alert->title, sizeof(alert->title));
        ui_runtime_copy_text_field(item, "message", alert->message, sizeof(alert->message));
        ui_runtime_copy_json_string(item, "action_entity_id", alert->action_entity_id, sizeof(alert->action_entity_id));
        ui_runtime_copy_text_field(item, "action_label", alert->action_label, sizeof(alert->action_label));
        ui_runtime_copy_json_string(item, "camera_entity_id", alert->camera_entity_id, sizeof(alert->camera_entity_id));
        cJSON *sound = cJSON_GetObjectItemCaseSensitive(item, "sound");
        if (cJSON_IsString(sound)) (void)chime_kind_parse(sound->valuestring, &alert->sound);
        cJSON *timeout = cJSON_GetObjectItemCaseSensitive(item, "timeout_sec");
        alert->timeout_sec = cJSON_IsNumber(timeout) && timeout->valuedouble > 0.0 ? (uint32_t)timeout->valuedouble : 30U;
        if (alert->message[0] == '\0') {
            strlcpy(alert->message, alert->title[0] != '\0' ? alert->title : "Alert", sizeof(alert->message));
        }
        s_alert_count++;
    }
}

/* Called with the display lock held for every changed entity (and once to prime after a layout load). */
static void ui_runtime_check_alerts(const char *entity_id)
{
    for (size_t i = 0; i < s_alert_count; i++) {
        ui_alert_t *alert = &s_alerts[i];
        if (entity_id != NULL && strncmp(alert->entity_id, entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            continue;
        }
        ha_state_t state = {0};
        if (!ha_model_get_state(alert->entity_id, &state)) {
            continue;
        }
        const bool usable = strcmp(state.state, "unavailable") != 0 && strcmp(state.state, "unknown") != 0;
        bool fire = false;
        if (alert->primed && usable) {
            if (strncmp(alert->entity_id, "event.", 6) == 0) {
                fire = strcmp(state.state, alert->last_state) != 0;
            } else {
                fire = strcmp(state.state, "on") == 0 && strcmp(alert->last_state, "on") != 0;
            }
        }
        strlcpy(alert->last_state, state.state, sizeof(alert->last_state));
        alert->primed = true;
        if (!fire) {
            continue;
        }
        app_notification_t *notification = calloc(1, sizeof(*notification));
        if (notification == NULL) {
            continue;
        }
        strlcpy(notification->title, alert->title, sizeof(notification->title));
        strlcpy(notification->message, alert->message, sizeof(notification->message));
        snprintf(notification->message_id, sizeof(notification->message_id), "alert_%u", (unsigned)i);
        notification->timeout_sec = alert->timeout_sec;
        notification->sound = alert->sound;
        strlcpy(notification->action_entity_id, alert->action_entity_id, sizeof(notification->action_entity_id));
        strlcpy(notification->action_label, alert->action_label, sizeof(notification->action_label));
        strlcpy(notification->camera_entity_id, alert->camera_entity_id, sizeof(notification->camera_entity_id));
        notification_sanitize_entities(notification);
        ESP_LOGI(TAG_UI, "alert fired: %s", alert->entity_id);
        ui_notification_popup_show(notification); /* takes ownership */
    }
}

/* Device tests without touch (POST /api/ui/tap): only tiles whose tap opens a read-only popup answer, so a test can never
 * switch a light or unlock a door. The popup a tap opened is remembered so it can be dismissed the same way. */
static const char *const k_test_tap_types[] = {"weather_3day", "energy_timeline", "room_tile", "sensor", "binary_tile",
                                               "contact_tile", "solar_forecast", "car_widget", "graph"};
static lv_obj_t *s_test_popup = NULL;

static bool layer_top_has_child(const lv_obj_t *obj)
{
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (lv_obj_get_child(top, (int32_t)i) == obj) {
            return true;
        }
    }
    return false;
}

static void ui_runtime_test_tap(const app_event_tap_t *tap)
{
    if (tap->close) {
        if (s_test_popup != NULL && layer_top_has_child(s_test_popup)) {
            lv_obj_send_event(s_test_popup, LV_EVENT_CLICKED, NULL);
        }
        s_test_popup = NULL;
        return;
    }
    if (strcmp(tap->widget_id, "topbar_stock") == 0 || strcmp(tap->widget_id, "topbar_stock_12m") == 0) {
        /* the stock chip's tap: a read-only history popup (the second id also selects its 12-month view) */
        lv_obj_t *top = lv_layer_top();
        const uint32_t before = lv_obj_get_child_count(top);
        ui_runtime_stock_chip_tap();
        if (strcmp(tap->widget_id, "topbar_stock_12m") == 0) ui_history_popup_range_select(true);
        const uint32_t after = lv_obj_get_child_count(top);
        if (after > before) s_test_popup = lv_obj_get_child(top, (int32_t)after - 1);
        return;
    }
    for (size_t i = 0; i < s_widget_count; i++) {
        if (strcmp(s_widgets[i].id, tap->widget_id) != 0) {
            continue;
        }
        bool allowed = false;
        for (size_t t = 0; t < sizeof(k_test_tap_types) / sizeof(k_test_tap_types[0]); t++) {
            allowed = allowed || strcmp(s_widgets[i].type, k_test_tap_types[t]) == 0;
        }
        if (!allowed || s_widgets[i].obj == NULL) {
            ESP_LOGW(TAG_UI, "test tap refused for %s (%s)", s_widgets[i].id, s_widgets[i].type);
            return;
        }
        lv_obj_t *top = lv_layer_top();
        const uint32_t before = lv_obj_get_child_count(top);
        lv_obj_send_event(s_widgets[i].obj, LV_EVENT_CLICKED, NULL);
        const uint32_t after = lv_obj_get_child_count(top);
        if (after > before) {
            s_test_popup = lv_obj_get_child(top, (int32_t)after - 1);
        }
        return;
    }
    ESP_LOGW(TAG_UI, "test tap: no widget %s", tap->widget_id);
}

static void ui_runtime_rotate(int degrees)
{
    if (!display_orientation_supported() || degrees == display_geometry_rotation()) return;
    if (!display_lock(20)) return; /* sensor retries until applied */
    if (lv_obj_get_child_count(lv_layer_top()) || ui_display_protection_is_active()) {
        display_unlock();
        return; /* Keep open controls and saver timers alive until dismissed. */
    }
    const int previous = display_geometry_rotation();
    if (display_orientation_apply(degrees)) {
        esp_err_t err = ui_runtime_reload_layout();
        if (err != ESP_OK) {
            display_orientation_apply(previous);
            (void)ui_runtime_reload_layout();
            ESP_LOGW(TAG_UI, "Rotation deferred: %s", esp_err_to_name(err));
        }
    }
    display_unlock();
}

static void ui_runtime_handle_event(const app_event_t *event)
{
    if (event == NULL) {
        return;
    }

    bool needs_lock = event->type != EV_LAYOUT_UPDATED && event->type != EV_DISPLAY_ROTATION;
    if (needs_lock && !display_lock(UI_RUNTIME_LOCK_MS)) {
        if (event->type == EV_HA_STATE_CHANGED || event->type == EV_HA_CONNECTED ||
            event->type == EV_HA_ENERGY_CHANGED) {
            s_pending_state_reconcile = true;
            s_pending_topbar_refresh = true;
        } else if (event->type == EV_HA_DISCONNECTED) {
            s_pending_topbar_refresh = true;
        } else if (event->type == EV_DISPLAY_PROTECTION_UPDATED) {
            s_pending_protection_reload = true;
        } else if (event->type == EV_NOTIFICATION_SHOW) {
            /* Keep ownership: stash for retry instead of leaking the payload.
             * A newer notification replaces an older deferred one. */
            free(s_pending_notification);
            s_pending_notification = event->data.notification.notification;
        }

        s_deferred_event_count++;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - s_deferred_event_log_ms) >= 5000) {
            ESP_LOGW(TAG_UI, "Deferred UI event processing due to display lock contention (deferred=%u)",
                (unsigned)s_deferred_event_count);
            s_deferred_event_log_ms = now_ms;
            s_deferred_event_count = 0;
        }
        return;
    }

    switch (event->type) {
    case EV_HA_STATE_CHANGED:
        ui_runtime_apply_entity_state(event->data.ha_state_changed.entity_id);
        ui_runtime_check_alerts(event->data.ha_state_changed.entity_id);
#if APP_HA_ROUTE_TRACE_LOG
        ESP_LOGI(TAG_UI, "route panel->ui entity=%s", event->data.ha_state_changed.entity_id);
#endif
        break;
    case EV_HA_CONNECTED:
        ui_runtime_check_alerts(NULL); /* prime what is already there */
        ui_runtime_refresh_topbar();
        /* During initial/partial HA sync we may temporarily miss some entities.
         * Preserve currently rendered widgets instead of forcing unavailable. */
        ui_runtime_apply_all_states_preserve_missing();
        break;
    case EV_HA_ENERGY_CHANGED:
        for (size_t i = 0; i < s_energy_page_count; i++) {
            ui_energy_page_apply_all_states(&s_energy_pages[i]);
        }
        break;
    case EV_HA_DISCONNECTED:
        ui_runtime_refresh_topbar();
        break;
    case EV_DISPLAY_ROTATION:
        ui_runtime_rotate(event->data.rotation_degrees);
        break;
    case EV_LAYOUT_UPDATED:
        /* The revision poll retries until the saved configuration is applied. */
        break;
    case EV_DISPLAY_PROTECTION_UPDATED:
        ui_display_protection_reload();
        break;
    case EV_UI_NAVIGATE:
        ui_pages_show(event->data.navigate.page_id);
        break;
    case EV_NOTIFICATION_SHOW:
        ui_notification_popup_show(event->data.notification.notification);
        break;
    case EV_UI_TAP:
        ui_runtime_test_tap(&event->data.tap);
        break;
    case EV_NONE:
    default:
        break;
    }

    if (needs_lock) {
        display_unlock();
    }
}

static void ui_runtime_task(void *arg)
{
    (void)arg;
    int64_t last_reload_ms = -1000;
    while (true) {
        ui_watchdog_beat_runtime();
        app_event_t event = {0};
        while (app_events_receive(&event, 0)) {
            ui_runtime_handle_event(&event);
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (app_events_layout_revision() != s_applied_layout_revision && now_ms - last_reload_ms >= 1000) {
            last_reload_ms = now_ms;
            esp_err_t reload_err = ui_runtime_reload_layout();
            if (reload_err != ESP_OK) ESP_LOGW(TAG_UI, "Saved configuration reload deferred: %s", esp_err_to_name(reload_err));
        }
        uint32_t model_revision = ha_model_state_revision();
        if (model_revision != s_last_model_revision) {
            s_last_model_revision = model_revision;
            s_model_reconcile_pending = true;
        }

        if ((s_pending_state_reconcile || s_pending_topbar_refresh) && display_lock(20)) {
            if (s_pending_topbar_refresh) {
                ui_runtime_refresh_topbar();
                s_pending_topbar_refresh = false;
            }
            if (s_pending_state_reconcile) {
                /* Reconcile all states after lock contention so UI never stays stale. */
                ui_runtime_apply_all_states_preserve_missing();
                s_pending_state_reconcile = false;
            }
            display_unlock();
        }

        if (s_pending_protection_reload && display_lock(20)) {
            ui_display_protection_reload();
            display_unlock();
            s_pending_protection_reload = false;
        }

        if (s_pending_notification != NULL && display_lock(20)) {
            app_notification_t *pending = s_pending_notification;
            s_pending_notification = NULL;
            ui_notification_popup_show(pending);
            display_unlock();
        }

        if (s_model_reconcile_pending && (now_ms - s_last_model_reconcile_ms) >= UI_MODEL_RECONCILE_INTERVAL_MS &&
            display_lock(20)) {
            /* Protect against missed per-entity events under burst: periodically reconcile from model snapshot. */
            ui_runtime_apply_all_states_preserve_missing();
            display_unlock();
            s_model_reconcile_pending = false;
            s_last_model_reconcile_ms = now_ms;
        }

        if ((now_ms - s_last_topbar_refresh_ms) >= 1000) {
            if (display_lock(20)) {
                ui_runtime_refresh_topbar();
                ui_display_protection_poll();
                display_unlock();
                s_last_topbar_refresh_ms = now_ms;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t ui_runtime_init(void)
{
    esp_err_t err = ui_runtime_alloc_buffers();
    if (err != ESP_OK) {
        return err;
    }

    if (!display_lock(UI_RUNTIME_LOCK_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    s_topbar_cache.valid = false;
    theme_default_init();
    ui_pages_init();
    ui_pages_set_stock_chip_handler(ui_runtime_stock_chip_tap);
    ui_watchdog_attach_lvgl();
    ui_display_protection_init();
    ui_pages_set_show_callback(ui_runtime_on_page_shown);
    ui_runtime_show_weather_icon_overlay();
    ui_runtime_refresh_topbar();
    display_unlock();
    s_last_topbar_refresh_ms = esp_timer_get_time() / 1000;
    s_last_model_reconcile_ms = s_last_topbar_refresh_ms;
    s_last_model_revision = ha_model_state_revision();
    s_model_reconcile_pending = false;
    s_pending_state_reconcile = false;
    s_pending_topbar_refresh = false;
    s_deferred_event_count = 0;
    s_deferred_event_log_ms = 0;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ui_runtime_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ui_task != NULL) {
        return ESP_OK;
    }

    BaseType_t created =
        xTaskCreate(ui_runtime_task, "ui_runtime", APP_UI_TASK_STACK, NULL, APP_UI_TASK_PRIO, &s_ui_task);
    return (created == pdPASS) ? ESP_OK : ESP_FAIL;
}
