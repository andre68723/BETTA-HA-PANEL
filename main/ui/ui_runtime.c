/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
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
#include "layout/layout_store.h"
#include "net/wifi_mgr.h"
#include "settings/runtime_settings.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/ui_energy_page.h"
#include "ui/ui_memory.h"
#include "ui/ui_notification_popup.h"
#include "ui/ui_pages.h"
#include "ui/ui_widget_factory.h"
#include "ui/theme/theme_default.h"
#include "util/log_tags.h"

#define UI_MODEL_RECONCILE_INTERVAL_MS 1000

static ui_widget_instance_t *s_widgets = NULL;
static size_t s_widget_count = 0;
static ui_energy_page_instance_t *s_energy_pages = NULL;
static size_t s_energy_page_count = 0;

static void ui_runtime_update_widget_visibility(const char *page_id, bool refresh_visible_widgets);

/* Invoked by ui_pages whenever the active page changes.
 * If the newly shown page is an energy dashboard, ask the HA client to
 * refresh the statistics immediately so the user sees fresh kWh values
 * instead of waiting for the next HA_ENERGY_SYNC_INTERVAL_MS tick. */
static void ui_runtime_on_page_shown(const char *page_id, uint16_t index)
{
    (void)index;
    if (page_id == NULL || page_id[0] == '\0') {
        return;
    }
    for (size_t i = 0; i < s_energy_page_count; i++) {
        if (strncmp(s_energy_pages[i].page_id, page_id, APP_MAX_PAGE_ID_LEN) == 0) {
            (void)ha_client_request_energy_refresh();
            (void)ui_energy_page_apply_all_states(&s_energy_pages[i]);
            break;
        }
    }
    ui_runtime_update_widget_visibility(page_id, true);
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
static char s_topbar_left_slots[2][APP_MAX_UI_OPTION_LEN] = { "date", "" };
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

static uint32_t ui_runtime_weather_icon_codepoint_for_key(const char *key)
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
    for (size_t i = 0; i < 2U; i++) {
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
    for (size_t i = 0; i < 3U; i++) {
        snprintf(
            s_topbar_stock_entity_ids[i],
            sizeof(s_topbar_stock_entity_ids[i]),
            "%s",
            i == 0 ? settings.topbar_stock_entity_ids[i] : "");
    }
    ui_pages_set_topbar_left_slots(s_topbar_left_slots[0], s_topbar_left_slots[1]);
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
    snprintf(out, out_len, "%s", tmp[0] != '\0' ? tmp : "STK");
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

typedef struct {
    int min_w;
    int min_h;
    int max_w;
    int max_h;
} ui_widget_size_limits_t;

static ui_widget_size_limits_t ui_runtime_widget_size_limits(const char *type)
{
    ui_widget_size_limits_t limits = {
        .min_w = 60,
        .min_h = 60,
        .max_w = APP_CONTENT_BOX_WIDTH,
        .max_h = APP_CONTENT_BOX_HEIGHT,
    };

    if (type == NULL) {
        return limits;
    }

    if (strcmp(type, "sensor") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 90;
        limits.min_h = 60;
#else
        limits.min_w = 120;
        limits.min_h = 80;
#endif
    } else if (strcmp(type, "button") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 82;
        limits.min_h = 82;
        limits.max_w = 320;
        limits.max_h = 260;
#else
        limits.min_w = 100;
        limits.min_h = 100;
        limits.max_w = 480;
        limits.max_h = 320;
#endif
    } else if (strcmp(type, "slider") == 0) {
        limits.min_w = 100;
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_h = 80;
#else
        limits.min_h = 100;
#endif
    } else if (strcmp(type, "graph") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 150;
        limits.min_h = 100;
#else
        limits.min_w = 220;
        limits.min_h = 140;
#endif
    } else if (strcmp(type, "empty_tile") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 100;
        limits.min_h = 70;
#else
        limits.min_w = 120;
        limits.min_h = 80;
#endif
    } else if (strcmp(type, "light_tile") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 140;
        limits.min_h = 140;
#else
        limits.min_w = 180;
        limits.min_h = 180;
#endif
        limits.max_w = 480;
        limits.max_h = 480;
    } else if (strcmp(type, "heating_tile") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 150;
        limits.min_h = 150;
#else
        limits.min_w = 220;
        limits.min_h = 200;
#endif
        limits.max_w = 480;
        limits.max_h = 480;
    } else if (strcmp(type, "weather_tile") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 160;
        limits.min_h = 150;
#else
        limits.min_w = 220;
        limits.min_h = 200;
#endif
        limits.max_w = 480;
        limits.max_h = 480;
    } else if (strcmp(type, "weather_3day") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 300;
        limits.min_h = 190;
#else
        limits.min_w = 260;
        limits.min_h = 220;
#endif
        limits.max_w = 640;
        limits.max_h = 480;
    } else if (strcmp(type, "solar_forecast") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 280;
        limits.min_h = 180;
#else
        limits.min_w = 260;
        limits.min_h = 220;
#endif
        limits.max_w = 640;
        limits.max_h = 480;
    } else if (strcmp(type, "todo_list") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 180;
        limits.min_h = 160;
#else
        limits.min_w = 220;
        limits.min_h = 200;
#endif
        limits.max_w = 640;
        limits.max_h = 640;
    } else if (strcmp(type, "media_player") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 200;
        limits.min_h = 170;
#else
        limits.min_w = 260;
        limits.min_h = 220;
#endif
        limits.max_w = APP_CONTENT_BOX_WIDTH;
        limits.max_h = APP_CONTENT_BOX_HEIGHT;
    } else if (strcmp(type, "roborock_tile") == 0) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        limits.min_w = 220;
        limits.min_h = 190;
#else
        limits.min_w = 240;
        limits.min_h = 220;
#endif
        limits.max_w = APP_CONTENT_BOX_WIDTH;
        limits.max_h = APP_CONTENT_BOX_HEIGHT;
    }

    if (limits.max_w > APP_CONTENT_BOX_WIDTH) {
        limits.max_w = APP_CONTENT_BOX_WIDTH;
    }
    if (limits.max_h > APP_CONTENT_BOX_HEIGHT) {
        limits.max_h = APP_CONTENT_BOX_HEIGHT;
    }
    return limits;
}

static void ui_runtime_clamp_widget_rect(ui_widget_def_t *def)
{
    if (def == NULL) {
        return;
    }

    ui_widget_size_limits_t limits = ui_runtime_widget_size_limits(def->type);

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

    if (def->x + def->w > APP_CONTENT_BOX_WIDTH) {
        def->x = APP_CONTENT_BOX_WIDTH - def->w;
    }
    if (def->y + def->h > APP_CONTENT_BOX_HEIGHT) {
        def->y = APP_CONTENT_BOX_HEIGHT - def->h;
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
    lv_obj_add_flag(s_weather_icon_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
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

static void ui_runtime_apply_entity_state_ex(const char *entity_id, bool mark_unavailable_if_missing)
{
    if (entity_id == NULL || entity_id[0] == '\0') {
        return;
    }

    memset(&s_state_scratch, 0, sizeof(s_state_scratch));
    bool found = ha_model_get_state(entity_id, &s_state_scratch);
    for (size_t i = 0; i < s_widget_count; i++) {
        bool is_primary = (strncmp(entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_secondary = (s_widgets[i].secondary_entity_id[0] != '\0') &&
                            (strncmp(entity_id, s_widgets[i].secondary_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_tertiary = (s_widgets[i].tertiary_entity_id[0] != '\0') &&
                           (strncmp(entity_id, s_widgets[i].tertiary_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_forecast_today = (s_widgets[i].forecast_today_entity_id[0] != '\0') &&
                                 (strncmp(entity_id, s_widgets[i].forecast_today_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_forecast_tomorrow = (s_widgets[i].forecast_tomorrow_entity_id[0] != '\0') &&
                                    (strncmp(entity_id, s_widgets[i].forecast_tomorrow_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_forecast_day_3 = (s_widgets[i].forecast_day_3_entity_id[0] != '\0') &&
                                 (strncmp(entity_id, s_widgets[i].forecast_day_3_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_forecast_day_4 = (s_widgets[i].forecast_day_4_entity_id[0] != '\0') &&
                                 (strncmp(entity_id, s_widgets[i].forecast_day_4_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        bool is_forecast_day_5 = (s_widgets[i].forecast_day_5_entity_id[0] != '\0') &&
                                 (strncmp(entity_id, s_widgets[i].forecast_day_5_entity_id, APP_MAX_ENTITY_ID_LEN) == 0);
        if (!is_primary && !is_secondary && !is_tertiary && !is_forecast_today && !is_forecast_tomorrow &&
            !is_forecast_day_3 && !is_forecast_day_4 && !is_forecast_day_5) {
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

    if (widget->secondary_entity_id[0] != '\0' &&
        strncmp(widget->secondary_entity_id, widget->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        memset(&s_state_scratch, 0, sizeof(s_state_scratch));
        if (ha_model_get_state(widget->secondary_entity_id, &s_state_scratch)) {
            ui_widget_factory_apply_state(widget, &s_state_scratch);
        }
    }
    if (widget->tertiary_entity_id[0] != '\0' &&
        strncmp(widget->tertiary_entity_id, widget->entity_id, APP_MAX_ENTITY_ID_LEN) != 0 &&
        strncmp(widget->tertiary_entity_id, widget->secondary_entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        memset(&s_state_scratch, 0, sizeof(s_state_scratch));
        if (ha_model_get_state(widget->tertiary_entity_id, &s_state_scratch)) {
            ui_widget_factory_apply_state(widget, &s_state_scratch);
        }
    }

    const char *forecast_entity_ids[] = {
        widget->forecast_today_entity_id,
        widget->forecast_tomorrow_entity_id,
        widget->forecast_day_3_entity_id,
        widget->forecast_day_4_entity_id,
        widget->forecast_day_5_entity_id,
    };
    for (size_t i = 0; i < sizeof(forecast_entity_ids) / sizeof(forecast_entity_ids[0]); i++) {
        const char *forecast_entity_id = forecast_entity_ids[i];
        if (forecast_entity_id[0] == '\0' ||
            strncmp(forecast_entity_id, widget->entity_id, APP_MAX_ENTITY_ID_LEN) == 0 ||
            strncmp(forecast_entity_id, widget->secondary_entity_id, APP_MAX_ENTITY_ID_LEN) == 0 ||
            strncmp(forecast_entity_id, widget->tertiary_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            continue;
        }
        memset(&s_state_scratch, 0, sizeof(s_state_scratch));
        if (ha_model_get_state(forecast_entity_id, &s_state_scratch)) {
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
        if (visible && refresh_visible_widgets) {
            ui_runtime_apply_widget_current_state(&s_widgets[i], false);
        }
    }
}

static void ui_runtime_apply_all_states(void)
{
    for (size_t i = 0; i < s_widget_count; i++) {
        ui_runtime_apply_entity_state(s_widgets[i].entity_id);
        if (s_widgets[i].secondary_entity_id[0] != '\0' &&
            strncmp(s_widgets[i].secondary_entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            ui_runtime_apply_entity_state(s_widgets[i].secondary_entity_id);
        }
        if (s_widgets[i].tertiary_entity_id[0] != '\0' &&
            strncmp(s_widgets[i].tertiary_entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) != 0 &&
            strncmp(s_widgets[i].tertiary_entity_id, s_widgets[i].secondary_entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            ui_runtime_apply_entity_state(s_widgets[i].tertiary_entity_id);
        }
        if (s_widgets[i].forecast_today_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state(s_widgets[i].forecast_today_entity_id);
        }
        if (s_widgets[i].forecast_tomorrow_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state(s_widgets[i].forecast_tomorrow_entity_id);
        }
        if (s_widgets[i].forecast_day_3_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state(s_widgets[i].forecast_day_3_entity_id);
        }
        if (s_widgets[i].forecast_day_4_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state(s_widgets[i].forecast_day_4_entity_id);
        }
        if (s_widgets[i].forecast_day_5_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state(s_widgets[i].forecast_day_5_entity_id);
        }
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
    for (size_t i = 0; i < s_widget_count; i++) {
        ui_runtime_apply_entity_state_ex(s_widgets[i].entity_id, false);
        if (s_widgets[i].secondary_entity_id[0] != '\0' &&
            strncmp(s_widgets[i].secondary_entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            ui_runtime_apply_entity_state_ex(s_widgets[i].secondary_entity_id, false);
        }
        if (s_widgets[i].tertiary_entity_id[0] != '\0' &&
            strncmp(s_widgets[i].tertiary_entity_id, s_widgets[i].entity_id, APP_MAX_ENTITY_ID_LEN) != 0 &&
            strncmp(s_widgets[i].tertiary_entity_id, s_widgets[i].secondary_entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
            ui_runtime_apply_entity_state_ex(s_widgets[i].tertiary_entity_id, false);
        }
        if (s_widgets[i].forecast_today_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state_ex(s_widgets[i].forecast_today_entity_id, false);
        }
        if (s_widgets[i].forecast_tomorrow_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state_ex(s_widgets[i].forecast_tomorrow_entity_id, false);
        }
        if (s_widgets[i].forecast_day_3_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state_ex(s_widgets[i].forecast_day_3_entity_id, false);
        }
        if (s_widgets[i].forecast_day_4_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state_ex(s_widgets[i].forecast_day_4_entity_id, false);
        }
        if (s_widgets[i].forecast_day_5_entity_id[0] != '\0') {
            ui_runtime_apply_entity_state_ex(s_widgets[i].forecast_day_5_entity_id, false);
        }
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
    cJSON *solar_forecast_bar_max_kwh = cJSON_GetObjectItemCaseSensitive(widget_json, "solar_forecast_bar_max_kwh");
    cJSON *solar_forecast_bar_orientation = cJSON_GetObjectItemCaseSensitive(widget_json, "solar_forecast_bar_orientation");
    cJSON *slider_direction = cJSON_GetObjectItemCaseSensitive(widget_json, "slider_direction");
    cJSON *slider_accent_color = cJSON_GetObjectItemCaseSensitive(widget_json, "slider_accent_color");
    cJSON *button_accent_color = cJSON_GetObjectItemCaseSensitive(widget_json, "button_accent_color");
    cJSON *button_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "button_mode");
    cJSON *graph_line_color = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_line_color");
    cJSON *graph_point_count = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_point_count");
    cJSON *graph_time_window_min = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_time_window_min");
    cJSON *graph_display_mode = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_display_mode");
    cJSON *graph_bar_bucket_min = cJSON_GetObjectItemCaseSensitive(widget_json, "graph_bar_bucket_min");
    cJSON *style_variant = cJSON_GetObjectItemCaseSensitive(widget_json, "style_variant");
    cJSON *arc_opening = cJSON_GetObjectItemCaseSensitive(widget_json, "arc_opening");
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
    snprintf(out->id, sizeof(out->id), "%s", id->valuestring);
    snprintf(out->type, sizeof(out->type), "%s", type->valuestring);
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
    if (cJSON_IsNumber(solar_forecast_bar_max_kwh)) {
        out->solar_forecast_bar_max_kwh = (float)solar_forecast_bar_max_kwh->valuedouble;
    }
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
    if (cJSON_IsNumber(graph_time_window_min)) {
        out->graph_time_window_min = graph_time_window_min->valueint;
    }
    if (cJSON_IsString(graph_display_mode) && graph_display_mode->valuestring != NULL) {
        snprintf(out->graph_display_mode, sizeof(out->graph_display_mode), "%s", graph_display_mode->valuestring);
    }
    if (cJSON_IsNumber(graph_bar_bucket_min)) {
        out->graph_bar_bucket_min = graph_bar_bucket_min->valueint;
    }
    if (cJSON_IsString(style_variant) && style_variant->valuestring != NULL) {
        snprintf(out->style_variant, sizeof(out->style_variant), "%s", style_variant->valuestring);
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

static void ui_runtime_energy_config_from_json(cJSON *page_json, const char *page_id, const char *page_title,
    ui_energy_page_config_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->page_id, sizeof(out->page_id), "%s", page_id != NULL ? page_id : "");
    snprintf(out->title, sizeof(out->title), "%s", (page_title != NULL && page_title[0] != '\0') ? page_title : "Energy");
    snprintf(out->source, sizeof(out->source), "%s", "ha_energy");

    cJSON *energy = (page_json != NULL) ? cJSON_GetObjectItemCaseSensitive(page_json, "energy") : NULL;
    if (!cJSON_IsObject(energy)) {
        return;
    }

    cJSON *source_item = cJSON_GetObjectItemCaseSensitive(energy, "source");
    bool source_was_configured =
        cJSON_IsString(source_item) && source_item->valuestring != NULL && source_item->valuestring[0] != '\0';
    ui_runtime_copy_json_string(energy, "source", out->source, sizeof(out->source));
    if (strcmp(out->source, "manual_live") != 0 && strcmp(out->source, "ha_energy") != 0) {
        snprintf(out->source, sizeof(out->source), "%s", "ha_energy");
    }

    ui_runtime_copy_json_string(
        energy, "home_power_entity_id", out->home_power_entity_id, sizeof(out->home_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "solar_power_entity_id", out->solar_power_entity_id, sizeof(out->solar_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "grid_power_entity_id", out->grid_power_entity_id, sizeof(out->grid_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "grid_import_power_entity_id", out->grid_import_power_entity_id, sizeof(out->grid_import_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "grid_export_power_entity_id", out->grid_export_power_entity_id, sizeof(out->grid_export_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "battery_power_entity_id", out->battery_power_entity_id, sizeof(out->battery_power_entity_id));
    ui_runtime_copy_json_string(energy,
        "battery_charge_power_entity_id",
        out->battery_charge_power_entity_id,
        sizeof(out->battery_charge_power_entity_id));
    ui_runtime_copy_json_string(energy,
        "battery_discharge_power_entity_id",
        out->battery_discharge_power_entity_id,
        sizeof(out->battery_discharge_power_entity_id));
    ui_runtime_copy_json_string(
        energy, "battery_soc_entity_id", out->battery_soc_entity_id, sizeof(out->battery_soc_entity_id));

    if (!source_was_configured &&
        (out->home_power_entity_id[0] != '\0' || out->solar_power_entity_id[0] != '\0' ||
            out->grid_power_entity_id[0] != '\0' || out->grid_import_power_entity_id[0] != '\0' ||
            out->grid_export_power_entity_id[0] != '\0' || out->battery_power_entity_id[0] != '\0' ||
            out->battery_charge_power_entity_id[0] != '\0' || out->battery_discharge_power_entity_id[0] != '\0' ||
            out->battery_soc_entity_id[0] != '\0')) {
        snprintf(out->source, sizeof(out->source), "%s", "manual_live");
    }
}

static bool ui_runtime_is_background_widget_type(const char *type)
{
    return type != NULL && strcmp(type, "empty_tile") == 0;
}

esp_err_t ui_runtime_load_layout(const char *layout_json)
{
    if (!s_initialized || layout_json == NULL || s_widgets == NULL || s_energy_pages == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_Parse(layout_json);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsArray(pages)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!display_lock(0)) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }

    s_topbar_cache.valid = false;
    ui_pages_reset();
    ui_runtime_load_topbar_settings();
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
            continue;
        }

        bool is_energy_page = cJSON_IsString(page_type) && page_type->valuestring != NULL &&
                              strcmp(page_type->valuestring, "energy_dashboard") == 0;
        if (is_energy_page) {
            if (s_energy_page_count >= APP_MAX_PAGES) {
                continue;
            }
            ui_energy_page_config_t energy_config = {0};
            ui_runtime_energy_config_from_json(page,
                page_id->valuestring,
                cJSON_IsString(page_title) ? page_title->valuestring : page_id->valuestring,
                &energy_config);
            esp_err_t err =
                ui_energy_page_create(&energy_config, page_container, &s_energy_pages[s_energy_page_count]);
            if (err == ESP_OK) {
                s_energy_page_count++;
            }
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
                ui_widget_def_t def = {0};
                if (!ui_runtime_widget_from_json(cJSON_GetArrayItem(widgets, w), &def)) {
                    continue;
                }
                bool is_background = ui_runtime_is_background_widget_type(def.type);
                if (is_background != background_pass) {
                    continue;
                }
                esp_err_t err = ui_widget_factory_create(&def, page_container, &s_widgets[s_widget_count]);
                if (err == ESP_OK) {
                    snprintf(s_widgets[s_widget_count].page_id, sizeof(s_widgets[s_widget_count].page_id),
                             "%s", page_id->valuestring);
                    s_widget_count++;
                }
            }
        }
    }

    cJSON_Delete(root);

    if (ui_pages_count() > 0) {
        ui_pages_show_index(0);
    }
    ui_runtime_apply_all_states();
    ui_runtime_refresh_topbar();
    display_unlock();
    ESP_LOGI(TAG_UI, "Layout loaded: %u widgets", (unsigned)s_widget_count);
    return ESP_OK;
}

esp_err_t ui_runtime_reload_layout(void)
{
    /* Rebuild theme styles so that a live theme change is picked up by the
     * newly created widgets. Safe: must be called from the UI task under the
     * display lock, which is the case when triggered via EV_LAYOUT_UPDATED. */
    theme_default_rebuild_styles();

    char *json = NULL;
    esp_err_t err = layout_store_load(&json);
    if (err != ESP_OK || json == NULL) {
        json = strdup(layout_store_default_json());
        if (json == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    err = ui_runtime_load_layout(json);
    free(json);
    return err;
}

static void ui_runtime_handle_event(const app_event_t *event)
{
    if (event == NULL) {
        return;
    }

    bool needs_lock = (event->type != EV_LAYOUT_UPDATED);
    if (needs_lock && !display_lock(0)) {
        if (event->type == EV_HA_STATE_CHANGED || event->type == EV_HA_CONNECTED ||
            event->type == EV_HA_ENERGY_CHANGED) {
            s_pending_state_reconcile = true;
            s_pending_topbar_refresh = true;
        } else if (event->type == EV_HA_DISCONNECTED) {
            s_pending_topbar_refresh = true;
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
#if APP_HA_ROUTE_TRACE_LOG
        ESP_LOGI(TAG_UI, "route panel->ui entity=%s", event->data.ha_state_changed.entity_id);
#endif
        break;
    case EV_HA_CONNECTED:
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
    case EV_LAYOUT_UPDATED:
        ui_runtime_reload_layout();
        break;
    case EV_UI_NAVIGATE:
        ui_pages_show(event->data.navigate.page_id);
        break;
    case EV_NOTIFICATION_SHOW:
        ui_notification_popup_show(event->data.notification.notification);
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
    while (true) {
        app_event_t event = {0};
        while (app_events_receive(&event, 0)) {
            ui_runtime_handle_event(&event);
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
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

    if (!display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    s_topbar_cache.valid = false;
    theme_default_init();
    ui_pages_init();
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
