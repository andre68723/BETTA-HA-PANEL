/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/text_scale.h"
#include "ui/theme/ui_style_registry.h"
#include "settings/runtime_settings.h"
#include "sdkconfig.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings/runtime_secrets.h"
#include "util/flash_gate.h"

#include "settings/i18n_store.h"
#include "util/atomic_file.h"
#include "util/flash_trace.h"
#include "util/log_tags.h"
#include "settings/brightness_model.h"

#define SETTINGS_LOCK_WAIT_MS 2000U
static _Atomic(SemaphoreHandle_t) s_settings_lock;

static esp_err_t settings_lock(bool writing)
{
    if (s_settings_lock == NULL) {
        static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t fresh = xSemaphoreCreateMutex();
        if (fresh == NULL) return ESP_ERR_NO_MEM;
        taskENTER_CRITICAL(&guard);
        if (s_settings_lock == NULL) {
            s_settings_lock = fresh;
            fresh = NULL;
        }
        taskEXIT_CRITICAL(&guard);
        if (fresh != NULL) vSemaphoreDelete(fresh);
    }
    /* A reader may run under the display lock: it must never wait for flash. */
    return xSemaphoreTake(s_settings_lock, writing ? pdMS_TO_TICKS(SETTINGS_LOCK_WAIT_MS) : 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t settings_generation(cJSON *root, uint32_t *generation)
{
    *generation = 0U;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "secret_generation");
    if (item == NULL) return ESP_OK; /* upstream v0.8.2 */
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= 1.0 && item->valuedouble <= UINT32_MAX) ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) return ESP_ERR_INVALID_RESPONSE;
    *generation = (uint32_t)item->valuedouble;
    return ESP_OK;
}

static bool is_placeholder(const char *text)
{
    return text == NULL || text[0] == '\0' || strstr(text, "YOUR_") != NULL;
}

static void normalize_ui_language(char *language, size_t language_len)
{
    if (language == NULL || language_len == 0) {
        return;
    }
    if (language[0] == '\0') {
        strlcpy(language, APP_UI_DEFAULT_LANGUAGE, language_len);
        return;
    }

    char normalized[APP_UI_LANGUAGE_MAX_LEN] = {0};
    if (!i18n_store_normalize_language_code(language, normalized, sizeof(normalized))) {
        strlcpy(language, APP_UI_DEFAULT_LANGUAGE, language_len);
        return;
    }
    strlcpy(language, normalized, language_len);
}

static void json_copy_string(cJSON *obj, const char *key, char *dst, size_t dst_len)
{
    if (obj == NULL || key == NULL || dst == NULL || dst_len == 0) {
        return;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strlcpy(dst, item->valuestring, dst_len);
    }
}

static void json_copy_bool(cJSON *obj, const char *key, bool *dst)
{
    if (obj == NULL || key == NULL || dst == NULL) {
        return;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) {
        *dst = cJSON_IsTrue(item);
    }
}

static void json_copy_int(cJSON *obj, const char *key, int *dst)
{
    if (obj == NULL || key == NULL || dst == NULL) {
        return;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        *dst = item->valueint;
    }
}


static int clamp_dim_timeout_seconds(int seconds)
{
    if (seconds < 5) {
        return 5;
    }
    if (seconds > 3600) {
        return 3600;
    }
    return seconds;
}

static bool topbar_slot_is_valid(const char *slot)
{
    return slot != NULL &&
           (strcmp(slot, "") == 0 ||
            strcmp(slot, "none") == 0 ||
            strcmp(slot, "date") == 0 ||
            strcmp(slot, "weather") == 0 ||
            strcmp(slot, "stock") == 0);
}

static void topbar_normalize_slot(char *slot, size_t slot_len)
{
    if (slot == NULL || slot_len == 0) {
        return;
    }
    if (!topbar_slot_is_valid(slot)) {
        slot[0] = '\0';
    }
}

static bool topbar_has_slot(const runtime_settings_t *settings, const char *slot)
{
    if (settings == NULL || slot == NULL) {
        return false;
    }
    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        if (strcmp(settings->topbar_left_slots[i], slot) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t load_file_text(const char *path, size_t max_len, char **out_text)
{
    if (path == NULL || out_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_text = NULL;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0 || (size_t)size > max_len) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);

    char *buf = calloc((size_t)size + 1U, sizeof(char));
    if (buf == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buf, 1U, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }

    *out_text = buf;
    return ESP_OK;
}

static esp_err_t serialize_public_settings(const runtime_settings_t *settings, uint32_t generation, char **out)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *ha = cJSON_AddObjectToObject(root, "ha");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *time_cfg = cJSON_AddObjectToObject(root, "time");
    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON *topbar = cJSON_AddObjectToObject(root, "topbar");
    cJSON *topbar_left_slots = topbar != NULL ? cJSON_AddArrayToObject(topbar, "left_slots") : NULL;
    cJSON *hardware = cJSON_AddObjectToObject(root, "hardware");
    cJSON *ota = cJSON_AddObjectToObject(root, "ota");
    if (wifi == NULL || ha == NULL || mqtt == NULL || device == NULL || time_cfg == NULL || ui == NULL ||
        topbar == NULL || topbar_left_slots == NULL || hardware == NULL || ota == NULL) goto oom;
    if (cJSON_AddNumberToObject(root, "version", 1) == NULL) goto oom;
    if (cJSON_AddNumberToObject(root, "secret_generation", generation) == NULL) goto oom;

    if (cJSON_AddStringToObject(wifi, "ssid", settings->wifi_ssid) == NULL) goto oom;
    if (cJSON_AddStringToObject(wifi, "country_code", settings->wifi_country_code) == NULL) goto oom;
    if (cJSON_AddStringToObject(wifi, "bssid", settings->wifi_bssid) == NULL) goto oom;

    if (cJSON_AddStringToObject(ha, "ws_url", settings->ha_ws_url) == NULL) goto oom;
    if (cJSON_AddBoolToObject(ha, "rest_enabled", settings->ha_rest_enabled) == NULL) goto oom;

    if (cJSON_AddBoolToObject(mqtt, "enabled", settings->mqtt_enabled) == NULL) goto oom;
    if (cJSON_AddStringToObject(mqtt, "broker_host", settings->mqtt_broker_host) == NULL) goto oom;
    if (cJSON_AddNumberToObject(mqtt, "broker_port", settings->mqtt_broker_port) == NULL) goto oom;
    if (cJSON_AddBoolToObject(mqtt, "tls_enabled", settings->mqtt_tls_enabled) == NULL) goto oom;
    if (cJSON_AddStringToObject(mqtt, "username", settings->mqtt_username) == NULL) goto oom;
    if (cJSON_AddStringToObject(mqtt, "topic_prefix", settings->mqtt_topic_prefix) == NULL) goto oom;
    if (cJSON_AddStringToObject(mqtt, "device_name", settings->mqtt_device_name) == NULL) goto oom;
    {
        if (cJSON_AddStringToObject(device, "room", settings->device_room) == NULL) goto oom;
    }

    if (cJSON_AddStringToObject(time_cfg, "ntp_server", settings->ntp_server) == NULL) goto oom;
    if (cJSON_AddStringToObject(time_cfg, "timezone", settings->time_tz) == NULL) goto oom;

    if (cJSON_AddStringToObject(ui, "language", settings->ui_language) == NULL) goto oom;
    if (cJSON_AddStringToObject(ui, "style", settings->ui_style) == NULL) goto oom;
    if (cJSON_AddNumberToObject(ui, "text_step", settings->ui_text_step) == NULL) goto oom;

    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        cJSON *item = cJSON_CreateString(settings->topbar_left_slots[i]);
        if (item == NULL) goto oom;
        if (!cJSON_AddItemToArray(topbar_left_slots, item)) {
            cJSON_Delete(item);
            goto oom;
        }
    }
    if (cJSON_AddStringToObject(topbar, "weather_entity_id", settings->topbar_weather_entity_id) == NULL) goto oom;
    if (cJSON_AddStringToObject(topbar, "stock_entity_id", settings->topbar_stock_entity_id) == NULL) goto oom;

    if (cJSON_AddNumberToObject(hardware, "active_brightness", settings->hardware_active_brightness_percent) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "dim_brightness", settings->hardware_dim_brightness_percent) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "dim_timeout_seconds", settings->hardware_dim_timeout_seconds) == NULL) goto oom;
    if (cJSON_AddStringToObject(hardware, "saver_mode", settings->hardware_saver_mode) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "saver_timeout_minutes", settings->hardware_saver_timeout_minutes) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "saver_weather_minutes", settings->hardware_saver_weather_minutes) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "saver_pages_minutes", settings->hardware_saver_pages_minutes) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "saver_page_interval_minutes", settings->hardware_saver_page_interval_minutes) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "saver_page_wake_home", settings->hardware_saver_page_wake_home) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "saver_weather_brightness_percent", settings->hardware_saver_weather_brightness_percent) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "saver_pixel_refresh", settings->hardware_saver_pixel_refresh) == NULL) goto oom;
    if (cJSON_AddStringToObject(hardware, "saver_weather_entity_id", settings->hardware_saver_weather_entity_id) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "sound_enabled", settings->sound_enabled) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "sound_volume", settings->sound_volume) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "sound_night_mute", settings->sound_night_mute) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "night_enabled", settings->hardware_night_enabled) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "night_brightness_percent", settings->hardware_night_brightness_percent) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "auto_brightness", settings->hardware_auto_brightness) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "auto_rotation", settings->hardware_auto_rotation) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "presence_wake", settings->hardware_presence_wake) == NULL) goto oom;
    if (cJSON_AddNumberToObject(hardware, "presence_sensitivity", settings->hardware_presence_sensitivity) == NULL) goto oom;
    if (cJSON_AddBoolToObject(hardware, "night_presence_wake", settings->hardware_night_presence_wake) == NULL) goto oom;
    {
        char hhmm[8];
        runtime_settings_format_hhmm(settings->hardware_night_start_min, hhmm, sizeof(hhmm));
        if (cJSON_AddStringToObject(hardware, "night_start", hhmm) == NULL) goto oom;
        runtime_settings_format_hhmm(settings->hardware_night_end_min, hhmm, sizeof(hhmm));
        if (cJSON_AddStringToObject(hardware, "night_end", hhmm) == NULL) goto oom;
    }
    {
        if (cJSON_AddBoolToObject(ota, "update_check", settings->ota_update_check) == NULL) goto oom;
        if (cJSON_AddBoolToObject(ota, "include_prereleases", settings->ota_include_prereleases) == NULL) goto oom;
        if (cJSON_AddBoolToObject(ota, "auto_install", settings->ota_auto_install) == NULL) goto oom;
    }


    *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out != NULL ? ESP_OK : ESP_ERR_NO_MEM;
oom:
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
}

/* Reads the settings file in the format written by upstream v0.8.2 (nested wifi/ha/time/ui objects,
 * secrets only in NVS) and by this fork (plus mqtt, device, topbar, hardware). Older upstream formats
 * (flat root keys, secrets in the file) and the fork's own unreleased intermediate formats are not read:
 * the supported upgrade path is from v0.8.2 only. */
static esp_err_t parse_settings_json(const char *json, runtime_settings_t *out, uint32_t *generation)
{
    if (json == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (settings_generation(root, generation) != ESP_OK) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    cJSON *ha = cJSON_GetObjectItemCaseSensitive(root, "ha");
    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    cJSON *time_cfg = cJSON_GetObjectItemCaseSensitive(root, "time");
    cJSON *ui = cJSON_GetObjectItemCaseSensitive(root, "ui");
    cJSON *topbar = cJSON_GetObjectItemCaseSensitive(root, "topbar");
    cJSON *hardware = cJSON_GetObjectItemCaseSensitive(root, "hardware");
    cJSON *ota = cJSON_GetObjectItemCaseSensitive(root, "ota");

    if (cJSON_IsObject(ota)) {
        json_copy_bool(ota, "update_check", &out->ota_update_check);
        json_copy_bool(ota, "include_prereleases", &out->ota_include_prereleases);
        json_copy_bool(ota, "auto_install", &out->ota_auto_install);
    }

    if (cJSON_IsObject(wifi)) {
        json_copy_string(wifi, "ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
        json_copy_string(wifi, "country_code", out->wifi_country_code, sizeof(out->wifi_country_code));
        json_copy_string(wifi, "bssid", out->wifi_bssid, sizeof(out->wifi_bssid));
    }

    if (cJSON_IsObject(ha)) {
        json_copy_string(ha, "ws_url", out->ha_ws_url, sizeof(out->ha_ws_url));
        json_copy_bool(ha, "rest_enabled", &out->ha_rest_enabled);
    }

    cJSON *device_obj = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (cJSON_IsObject(device_obj)) {
        json_copy_string(device_obj, "room", out->device_room, sizeof(out->device_room));
    }

    if (cJSON_IsObject(mqtt)) {
        json_copy_bool(mqtt, "enabled", &out->mqtt_enabled);
        json_copy_string(mqtt, "broker_host", out->mqtt_broker_host, sizeof(out->mqtt_broker_host));
        json_copy_int(mqtt, "broker_port", &out->mqtt_broker_port);
        json_copy_bool(mqtt, "tls_enabled", &out->mqtt_tls_enabled);
        json_copy_string(mqtt, "username", out->mqtt_username, sizeof(out->mqtt_username));
        json_copy_string(mqtt, "topic_prefix", out->mqtt_topic_prefix, sizeof(out->mqtt_topic_prefix));
        json_copy_string(mqtt, "device_name", out->mqtt_device_name, sizeof(out->mqtt_device_name));
    }

    if (cJSON_IsObject(time_cfg)) {
        json_copy_string(time_cfg, "ntp_server", out->ntp_server, sizeof(out->ntp_server));
        json_copy_string(time_cfg, "timezone", out->time_tz, sizeof(out->time_tz));
    }

    if (cJSON_IsObject(ui)) {
        json_copy_string(ui, "language", out->ui_language, sizeof(out->ui_language));
        cJSON *style = cJSON_GetObjectItemCaseSensitive(ui, "style");
        if (cJSON_IsString(style) && style->valuestring != NULL) {
            strlcpy(out->ui_style, ui_style_name(ui_style_from_name(style->valuestring)), sizeof(out->ui_style));
        }
        json_copy_int(ui, "text_step", &out->ui_text_step);
        out->ui_text_step = text_scale_clamp_user_step(out->ui_text_step);
    }

    if (cJSON_IsObject(topbar)) {
        cJSON *left_slots = cJSON_GetObjectItemCaseSensitive(topbar, "left_slots");
        if (cJSON_IsArray(left_slots)) {
            for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
                cJSON *item = cJSON_GetArrayItem(left_slots, (int)i);
                if (cJSON_IsString(item) && item->valuestring != NULL) {
                    strlcpy(out->topbar_left_slots[i], item->valuestring, sizeof(out->topbar_left_slots[i]));
                } else if (cJSON_IsNull(item)) {
                    out->topbar_left_slots[i][0] = '\0';
                }
            }
        }
        json_copy_string(topbar, "weather_entity_id", out->topbar_weather_entity_id, sizeof(out->topbar_weather_entity_id));
        json_copy_string(topbar, "stock_entity_id", out->topbar_stock_entity_id, sizeof(out->topbar_stock_entity_id));
    }
    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        topbar_normalize_slot(out->topbar_left_slots[i], sizeof(out->topbar_left_slots[i]));
    }
    out->topbar_weather_enabled = topbar_has_slot(out, "weather");

    if (cJSON_IsObject(hardware)) {
        /* Keys of older formats first, so the current keys below win when a file has both. */
        json_copy_int(hardware, "day_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(hardware, "night_brightness", &out->hardware_dim_brightness_percent);
        json_copy_int(hardware, "active_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(hardware, "dim_brightness", &out->hardware_dim_brightness_percent);
        json_copy_int(hardware, "dim_timeout_seconds", &out->hardware_dim_timeout_seconds);
        json_copy_string(hardware, "saver_mode", out->hardware_saver_mode, sizeof(out->hardware_saver_mode));
        json_copy_int(hardware, "saver_timeout_minutes", &out->hardware_saver_timeout_minutes);
        json_copy_int(hardware, "saver_weather_minutes", &out->hardware_saver_weather_minutes);
        json_copy_int(hardware, "saver_pages_minutes", &out->hardware_saver_pages_minutes);
        json_copy_int(hardware, "saver_page_interval_minutes", &out->hardware_saver_page_interval_minutes);
        cJSON *page_wake_home = cJSON_GetObjectItemCaseSensitive(hardware, "saver_page_wake_home");
        if (cJSON_IsBool(page_wake_home)) out->hardware_saver_page_wake_home = cJSON_IsTrue(page_wake_home);
        json_copy_int(hardware, "saver_weather_brightness_percent", &out->hardware_saver_weather_brightness_percent);
        cJSON *pixel_refresh = cJSON_GetObjectItemCaseSensitive(hardware, "saver_pixel_refresh");
        if (cJSON_IsBool(pixel_refresh)) out->hardware_saver_pixel_refresh = cJSON_IsTrue(pixel_refresh);
        json_copy_string(hardware, "saver_weather_entity_id", out->hardware_saver_weather_entity_id,
            sizeof(out->hardware_saver_weather_entity_id));
        cJSON *sound_enabled = cJSON_GetObjectItemCaseSensitive(hardware, "sound_enabled");
        if (cJSON_IsBool(sound_enabled)) out->sound_enabled = cJSON_IsTrue(sound_enabled);
        cJSON *sound_mute = cJSON_GetObjectItemCaseSensitive(hardware, "sound_night_mute");
        if (cJSON_IsBool(sound_mute)) out->sound_night_mute = cJSON_IsTrue(sound_mute);
        json_copy_int(hardware, "sound_volume", &out->sound_volume);
        if (out->sound_volume < 0) out->sound_volume = 0;
        if (out->sound_volume > 100) out->sound_volume = 100;
        json_copy_int(hardware, "night_brightness_percent", &out->hardware_night_brightness_percent);
        cJSON *auto_rotation = cJSON_GetObjectItemCaseSensitive(hardware, "auto_rotation");
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
        if (cJSON_IsBool(auto_rotation)) out->hardware_auto_rotation = cJSON_IsTrue(auto_rotation);
#else
        (void)auto_rotation;
#endif
        cJSON *auto_brightness = cJSON_GetObjectItemCaseSensitive(hardware, "auto_brightness");
        if (cJSON_IsBool(auto_brightness)) {
            out->hardware_auto_brightness = cJSON_IsTrue(auto_brightness);
        }
        json_copy_int(hardware, "presence_sensitivity", &out->hardware_presence_sensitivity);
        cJSON *night_presence_wake = cJSON_GetObjectItemCaseSensitive(hardware, "night_presence_wake");
        if (cJSON_IsBool(night_presence_wake)) {
            out->hardware_night_presence_wake = cJSON_IsTrue(night_presence_wake);
        }
        cJSON *presence_wake = cJSON_GetObjectItemCaseSensitive(hardware, "presence_wake");
        if (cJSON_IsBool(presence_wake)) {
            out->hardware_presence_wake = cJSON_IsTrue(presence_wake);
        }
        cJSON *night_enabled = cJSON_GetObjectItemCaseSensitive(hardware, "night_enabled");
        if (cJSON_IsBool(night_enabled)) {
            out->hardware_night_enabled = cJSON_IsTrue(night_enabled);
        }
        cJSON *night_start = cJSON_GetObjectItemCaseSensitive(hardware, "night_start");
        cJSON *night_end = cJSON_GetObjectItemCaseSensitive(hardware, "night_end");
        int minutes = 0;
        if (cJSON_IsString(night_start) && runtime_settings_parse_hhmm(night_start->valuestring, &minutes)) {
            out->hardware_night_start_min = minutes;
        }
        if (cJSON_IsString(night_end) && runtime_settings_parse_hhmm(night_end->valuestring, &minutes)) {
            out->hardware_night_end_min = minutes;
        }
    } else {
        json_copy_int(root, "hardware_active_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(root, "hardware_dim_brightness", &out->hardware_dim_brightness_percent);
        json_copy_int(root, "hardware_dim_timeout_seconds", &out->hardware_dim_timeout_seconds);
        json_copy_int(root, "hardware_day_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(root, "hardware_night_brightness", &out->hardware_dim_brightness_percent);
    }
    brightness_levels_t levels = {out->hardware_active_brightness_percent, out->hardware_dim_brightness_percent,
        out->hardware_night_brightness_percent};
    brightness_levels_normalize(&levels);
    out->hardware_active_brightness_percent = levels.active;
    out->hardware_dim_brightness_percent = levels.dimmed;
    out->hardware_night_brightness_percent = levels.night;
    out->hardware_dim_timeout_seconds = clamp_dim_timeout_seconds(out->hardware_dim_timeout_seconds);
    if (out->mqtt_broker_port < 1 || out->mqtt_broker_port > 65535) out->mqtt_broker_port = 1883;
    if (strcmp(out->hardware_saver_mode, "off") != 0 && strcmp(out->hardware_saver_mode, "weather") != 0 &&
        strcmp(out->hardware_saver_mode, "pages") != 0)
        strlcpy(out->hardware_saver_mode, "disabled", sizeof(out->hardware_saver_mode));
    if (out->hardware_saver_timeout_minutes < 1 || out->hardware_saver_timeout_minutes > 120)
        out->hardware_saver_timeout_minutes = 10;
    if (out->hardware_saver_weather_minutes < 1 || out->hardware_saver_weather_minutes > 120)
        out->hardware_saver_weather_minutes = 30;
    if (out->hardware_saver_pages_minutes < 1 || out->hardware_saver_pages_minutes > 120)
        out->hardware_saver_pages_minutes = 30;
    if (out->hardware_saver_page_interval_minutes < 1 || out->hardware_saver_page_interval_minutes > 30)
        out->hardware_saver_page_interval_minutes = 5;
    if (out->hardware_saver_weather_brightness_percent < 1 || out->hardware_saver_weather_brightness_percent > 30)
        out->hardware_saver_weather_brightness_percent = 5;
    if (out->hardware_presence_sensitivity < 1 || out->hardware_presence_sensitivity > 10) out->hardware_presence_sensitivity = 5;

    cJSON_Delete(root);
    return ESP_OK;
}

void runtime_settings_set_defaults(runtime_settings_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    if (!is_placeholder(APP_WIFI_SSID)) {
        strlcpy(out->wifi_ssid, APP_WIFI_SSID, sizeof(out->wifi_ssid));
    }
    if (!is_placeholder(APP_WIFI_PASSWORD)) {
        strlcpy(out->wifi_password, APP_WIFI_PASSWORD, sizeof(out->wifi_password));
    }
    strlcpy(out->wifi_country_code, APP_WIFI_COUNTRY_CODE, sizeof(out->wifi_country_code));
    if (!is_placeholder(APP_HA_WS_URL)) {
        strlcpy(out->ha_ws_url, APP_HA_WS_URL, sizeof(out->ha_ws_url));
    }
    if (!is_placeholder(APP_HA_ACCESS_TOKEN)) {
        strlcpy(out->ha_access_token, APP_HA_ACCESS_TOKEN, sizeof(out->ha_access_token));
    }
    out->ha_rest_enabled = false;
    out->mqtt_enabled = false;
    out->mqtt_broker_port = 1883;
    out->mqtt_tls_enabled = false;
    strlcpy(out->mqtt_topic_prefix, "betta_ha_panel", sizeof(out->mqtt_topic_prefix));
    strlcpy(out->ntp_server, APP_NTP_SERVER, sizeof(out->ntp_server));
    strlcpy(out->time_tz, APP_TIME_TZ, sizeof(out->time_tz));
    strlcpy(out->ui_language, APP_UI_DEFAULT_LANGUAGE, sizeof(out->ui_language));
    strlcpy(out->ui_style, ui_style_name(UI_STYLE_DEFAULT), sizeof(out->ui_style));
    out->ui_text_step = 0;
    strlcpy(out->topbar_left_slots[0], "date", sizeof(out->topbar_left_slots[0]));
    for (size_t i = 1; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        out->topbar_left_slots[i][0] = '\0';
    }
    out->topbar_weather_enabled = false;
    out->topbar_weather_entity_id[0] = '\0';
    out->hardware_active_brightness_percent = APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT;
    out->hardware_dim_brightness_percent = APP_DISPLAY_DIM_BRIGHTNESS_PERCENT;
    out->hardware_dim_timeout_seconds = APP_DISPLAY_DIM_TIMEOUT_MS / 1000;
    strlcpy(out->hardware_saver_mode, "disabled", sizeof(out->hardware_saver_mode));
    out->hardware_saver_timeout_minutes = 10;
    out->hardware_saver_weather_minutes = 30;
    out->hardware_saver_pages_minutes = 30;
    out->hardware_saver_page_interval_minutes = 5;
    out->hardware_saver_page_wake_home = true;
    out->hardware_saver_weather_brightness_percent = 5;
    out->hardware_saver_pixel_refresh = true;
    out->sound_enabled = false;
    out->sound_volume = 60; /* the compact speakers are quiet */
    out->sound_night_mute = true;
    out->hardware_night_enabled = true;
#if defined(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
    /* Keep the JC defaults; Tab5 camera sensing starts off until device calibration. */
    out->hardware_auto_brightness = true;
    out->hardware_presence_wake = true;
#else
    out->hardware_auto_brightness = false;
    out->hardware_presence_wake = false;
#endif
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    out->hardware_auto_rotation = true;
#else
    out->hardware_auto_rotation = false;
#endif
    out->hardware_presence_sensitivity = 5;
    out->hardware_night_presence_wake = false;
    out->hardware_night_brightness_percent = APP_DISPLAY_NIGHT_BRIGHTNESS_PERCENT;
    out->hardware_night_start_min = 22 * 60;
    out->hardware_night_end_min = 6 * 60;
    out->ota_update_check = true;
    out->ota_include_prereleases = false;
    out->ota_auto_install = false;
}

bool runtime_settings_parse_hhmm(const char *text, int *out_minutes)
{
    int h = 0;
    int m = 0;
    if (text == NULL || out_minutes == NULL || sscanf(text, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *out_minutes = h * 60 + m;
    return true;
}

void runtime_settings_format_hhmm(int minutes, char *out, size_t out_size)
{
    if (minutes < 0 || minutes >= 1440) {
        minutes = 0;
    }
    snprintf(out, out_size, "%02d:%02d", minutes / 60, minutes % 60);
}

static esp_err_t runtime_settings_load_unlocked(runtime_settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime_settings_set_defaults(out);

    char *json = NULL;
    esp_err_t file_err = load_file_text(APP_SETTINGS_PATH, APP_SETTINGS_MAX_JSON_LEN, &json);
    if (file_err != ESP_OK) {
        return file_err;
    }

    uint32_t generation = 0;
    esp_err_t parse_err = parse_settings_json(json, out, &generation);
    free(json);
    if (parse_err != ESP_OK) {
        return parse_err;
    }
    if (out->wifi_country_code[0] == '\0') {
        strlcpy(out->wifi_country_code, APP_WIFI_COUNTRY_CODE, sizeof(out->wifi_country_code));
    }
    normalize_ui_language(out->ui_language, sizeof(out->ui_language));

    return runtime_secrets_load(generation, out);
}

esp_err_t runtime_settings_load(runtime_settings_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = settings_lock(false);
    if (err != ESP_OK) {
        runtime_settings_set_defaults(out);
        out->wifi_password[0] = out->ha_access_token[0] = out->mqtt_password[0] = '\0';
        return err;
    }
    err = runtime_settings_load_unlocked(out);
    if (err != ESP_OK) {
        /* Callers must not observe partially read credentials after a storage error. */
        runtime_settings_set_defaults(out);
        out->wifi_password[0] = out->ha_access_token[0] = out->mqtt_password[0] = '\0';
    }
    xSemaphoreGive(s_settings_lock);
    return err;
}

static esp_err_t runtime_settings_save_unlocked(const runtime_settings_t *settings)
{
    char *previous = NULL;
    uint32_t generation = 0U;
    esp_err_t err = load_file_text(APP_SETTINGS_PATH, APP_SETTINGS_MAX_JSON_LEN, &previous);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return err;
    runtime_settings_t *legacy = NULL;
    if (previous != NULL) {
        legacy = calloc(1, sizeof(*legacy));
        if (legacy == NULL) { free(previous); return ESP_ERR_NO_MEM; }
        runtime_settings_set_defaults(legacy);
        err = parse_settings_json(previous, legacy, &generation);
        free(previous);
        if (err == ESP_OK) err = runtime_secrets_load(generation, legacy);
        if (err != ESP_OK) { free(legacy); return err; }
    }
    /* Wrap to an even generation, never overwrite the currently referenced slot. */
    uint32_t next = generation == UINT32_MAX ? 2U : generation + 1U;
    char *payload = NULL;
    err = serialize_public_settings(settings, next, &payload);
    if (err == ESP_OK && strlen(payload) > APP_SETTINGS_MAX_JSON_LEN) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK && !flash_gate_enter(SETTINGS_LOCK_WAIT_MS)) err = ESP_ERR_TIMEOUT;
    if (err == ESP_OK) {
        /* Preserve a legacy file's complete old bundle before retiring its keys. */
        if (generation == 0U && legacy != NULL) err = runtime_secrets_stage(0U, legacy);
        if (err == ESP_OK) err = runtime_secrets_stage(next, settings);
        if (err == ESP_OK) err = runtime_secrets_retire_legacy(legacy, settings);
        if (err == ESP_OK) err = atomic_file_write_locked(APP_SETTINGS_PATH, payload, strlen(payload));
        flash_gate_leave();
    }
    free(legacy);
    cJSON_free(payload);
    return err;
}

esp_err_t runtime_settings_save(const runtime_settings_t *settings)
{
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = settings_lock(true);
    if (err != ESP_OK) return err;
    err = runtime_settings_save_unlocked(settings);
    xSemaphoreGive(s_settings_lock);
    return err;
}

esp_err_t runtime_settings_init(void)
{
    runtime_settings_t *settings = calloc(1, sizeof(runtime_settings_t));
    if (settings == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = runtime_settings_load(settings);
    if (err == ESP_OK) {
        free(settings);
        return ESP_OK;
    }

    if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG_APP, "Settings load failed (%s), preserving stored configuration", esp_err_to_name(err));
        free(settings);
        return err;
    }
    ESP_LOGW(TAG_APP, "Settings file absent, writing defaults");
    runtime_settings_set_defaults(settings);
    esp_err_t save_err = runtime_settings_save(settings);
    free(settings);
    return save_err;
}

bool runtime_settings_has_wifi(const runtime_settings_t *settings)
{
    return settings != NULL && settings->wifi_ssid[0] != '\0';
}

bool runtime_settings_has_ha(const runtime_settings_t *settings)
{
    return settings != NULL && settings->ha_ws_url[0] != '\0' && settings->ha_access_token[0] != '\0';
}

bool runtime_settings_has_mqtt(const runtime_settings_t *settings)
{
    return settings != NULL && settings->mqtt_enabled && settings->mqtt_broker_host[0] != '\0';
}
