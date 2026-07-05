/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "settings/runtime_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

#include "settings/i18n_store.h"
#include "util/log_tags.h"

#define SETTINGS_NVS_NAMESPACE "runtime_sec"
#define SETTINGS_NVS_KEY_WIFI_PASSWORD "wifi_pwd"
#define SETTINGS_NVS_KEY_HA_ACCESS_TOKEN "ha_token"

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

static int clamp_config_brightness(int percent)
{
    if (percent < 10) {
        return 10;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
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
    for (size_t i = 0; i < 2U; i++) {
        if (strcmp(settings->topbar_left_slots[i], slot) == 0) {
            return true;
        }
    }
    return false;
}

static void topbar_migrate_legacy_slots(runtime_settings_t *settings)
{
    if (settings == NULL || settings->topbar_left_slots[1][0] != '\0') {
        return;
    }
    if (settings->topbar_left_slots[0][0] == '\0') {
        strlcpy(settings->topbar_left_slots[0], "date", sizeof(settings->topbar_left_slots[0]));
    } else if (strcmp(settings->topbar_left_slots[0], "date") != 0) {
        return;
    }
    if (settings->topbar_weather_enabled && settings->topbar_weather_entity_id[0] != '\0') {
        strlcpy(settings->topbar_left_slots[1], "weather", sizeof(settings->topbar_left_slots[1]));
    } else if (settings->topbar_stock_entity_ids[0][0] != '\0') {
        strlcpy(settings->topbar_left_slots[1], "stock", sizeof(settings->topbar_left_slots[1]));
    }
}

static esp_err_t load_file_text(const char *path, size_t max_len, char **out_text)
{
    if (path == NULL || out_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_text = NULL;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
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

static esp_err_t write_public_settings_file(const runtime_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *ha = cJSON_CreateObject();
    cJSON *time_cfg = cJSON_CreateObject();
    cJSON *ui = cJSON_CreateObject();
    cJSON *topbar = cJSON_CreateObject();
    cJSON *topbar_left_slots = cJSON_CreateArray();
    cJSON *weather = cJSON_CreateObject();
    cJSON *stocks = cJSON_CreateObject();
    cJSON *stock_entity_ids = cJSON_CreateArray();
    cJSON *hardware = cJSON_CreateObject();
    if (root == NULL || wifi == NULL || ha == NULL || time_cfg == NULL || ui == NULL ||
        topbar == NULL || topbar_left_slots == NULL || weather == NULL || stocks == NULL ||
        stock_entity_ids == NULL || hardware == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(wifi);
        cJSON_Delete(ha);
        cJSON_Delete(time_cfg);
        cJSON_Delete(ui);
        cJSON_Delete(topbar);
        cJSON_Delete(topbar_left_slots);
        cJSON_Delete(weather);
        cJSON_Delete(stocks);
        cJSON_Delete(stock_entity_ids);
        cJSON_Delete(hardware);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "version", 1);

    cJSON_AddStringToObject(wifi, "ssid", settings->wifi_ssid);
    cJSON_AddStringToObject(wifi, "country_code", settings->wifi_country_code);
    cJSON_AddStringToObject(wifi, "bssid", settings->wifi_bssid);
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON_AddStringToObject(ha, "ws_url", settings->ha_ws_url);
    cJSON_AddBoolToObject(ha, "rest_enabled", settings->ha_rest_enabled);
    cJSON_AddItemToObject(root, "ha", ha);

    cJSON_AddStringToObject(time_cfg, "ntp_server", settings->ntp_server);
    cJSON_AddStringToObject(time_cfg, "timezone", settings->time_tz);
    cJSON_AddItemToObject(root, "time", time_cfg);

    cJSON_AddStringToObject(ui, "language", settings->ui_language);
    cJSON_AddItemToObject(root, "ui", ui);

    for (size_t i = 0; i < 2U; i++) {
        cJSON *item = cJSON_CreateString(settings->topbar_left_slots[i]);
        if (item == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(topbar);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToArray(topbar_left_slots, item);
    }
    cJSON_AddItemToObject(topbar, "left_slots", topbar_left_slots);
    cJSON_AddStringToObject(topbar, "weather_entity_id", settings->topbar_weather_entity_id);
    cJSON_AddStringToObject(topbar, "stock_entity_id", settings->topbar_stock_entity_ids[0]);
    cJSON_AddItemToObject(root, "topbar", topbar);

    cJSON_AddBoolToObject(weather, "topbar_enabled", settings->topbar_weather_enabled);
    cJSON_AddStringToObject(weather, "entity_id", settings->topbar_weather_entity_id);
    cJSON_AddItemToObject(root, "weather", weather);

    for (size_t i = 0; i < 3U; i++) {
        cJSON *item = cJSON_CreateString(settings->topbar_stock_entity_ids[i]);
        if (item == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(stocks);
            cJSON_Delete(stock_entity_ids);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddItemToArray(stock_entity_ids, item);
    }
    cJSON_AddItemToObject(stocks, "entity_ids", stock_entity_ids);
    cJSON_AddItemToObject(root, "stocks", stocks);

    cJSON_AddNumberToObject(hardware, "active_brightness", settings->hardware_active_brightness_percent);
    cJSON_AddNumberToObject(hardware, "dim_brightness", settings->hardware_dim_brightness_percent);
    cJSON_AddNumberToObject(hardware, "dim_timeout_seconds", settings->hardware_dim_timeout_seconds);
    cJSON_AddItemToObject(root, "hardware", hardware);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(APP_SETTINGS_PATH, "wb");
    if (f == NULL) {
        cJSON_free(payload);
        return ESP_FAIL;
    }

    size_t len = strlen(payload);
    size_t written = fwrite(payload, 1U, len, f);
    fclose(f);
    cJSON_free(payload);

    if (written != len) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t parse_settings_json(
    const char *json,
    runtime_settings_t *out,
    bool *out_legacy_wifi_password,
    bool *out_legacy_ha_access_token)
{
    if (json == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (out_legacy_wifi_password != NULL) {
        *out_legacy_wifi_password = false;
    }
    if (out_legacy_ha_access_token != NULL) {
        *out_legacy_ha_access_token = false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    cJSON *ha = cJSON_GetObjectItemCaseSensitive(root, "ha");
    cJSON *time_cfg = cJSON_GetObjectItemCaseSensitive(root, "time");
    cJSON *ui = cJSON_GetObjectItemCaseSensitive(root, "ui");
    cJSON *topbar = cJSON_GetObjectItemCaseSensitive(root, "topbar");
    cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    cJSON *stocks = cJSON_GetObjectItemCaseSensitive(root, "stocks");
    cJSON *hardware = cJSON_GetObjectItemCaseSensitive(root, "hardware");

    if (cJSON_IsObject(wifi)) {
        json_copy_string(wifi, "ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
        json_copy_string(wifi, "country_code", out->wifi_country_code, sizeof(out->wifi_country_code));
        json_copy_string(wifi, "bssid", out->wifi_bssid, sizeof(out->wifi_bssid));
        cJSON *pwd = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (pwd != NULL) {
            if (out_legacy_wifi_password != NULL) {
                *out_legacy_wifi_password = true;
            }
            if (cJSON_IsString(pwd) && pwd->valuestring != NULL) {
                strlcpy(out->wifi_password, pwd->valuestring, sizeof(out->wifi_password));
            }
        }
    } else {
        json_copy_string(root, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
        json_copy_string(root, "wifi_country_code", out->wifi_country_code, sizeof(out->wifi_country_code));
        json_copy_string(root, "wifi_bssid", out->wifi_bssid, sizeof(out->wifi_bssid));
        cJSON *pwd = cJSON_GetObjectItemCaseSensitive(root, "wifi_password");
        if (pwd != NULL) {
            if (out_legacy_wifi_password != NULL) {
                *out_legacy_wifi_password = true;
            }
            if (cJSON_IsString(pwd) && pwd->valuestring != NULL) {
                strlcpy(out->wifi_password, pwd->valuestring, sizeof(out->wifi_password));
            }
        }
    }

    if (cJSON_IsObject(ha)) {
        json_copy_string(ha, "ws_url", out->ha_ws_url, sizeof(out->ha_ws_url));
        cJSON *rest_enabled = cJSON_GetObjectItemCaseSensitive(ha, "rest_enabled");
        if (cJSON_IsBool(rest_enabled)) {
            out->ha_rest_enabled = cJSON_IsTrue(rest_enabled);
        }
        cJSON *token = cJSON_GetObjectItemCaseSensitive(ha, "access_token");
        if (token != NULL) {
            if (out_legacy_ha_access_token != NULL) {
                *out_legacy_ha_access_token = true;
            }
            if (cJSON_IsString(token) && token->valuestring != NULL) {
                strlcpy(out->ha_access_token, token->valuestring, sizeof(out->ha_access_token));
            }
        }
    } else {
        json_copy_string(root, "ha_ws_url", out->ha_ws_url, sizeof(out->ha_ws_url));
        cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "ha_access_token");
        if (token != NULL) {
            if (out_legacy_ha_access_token != NULL) {
                *out_legacy_ha_access_token = true;
            }
            if (cJSON_IsString(token) && token->valuestring != NULL) {
                strlcpy(out->ha_access_token, token->valuestring, sizeof(out->ha_access_token));
            }
        }
    }
    cJSON *rest_enabled = cJSON_GetObjectItemCaseSensitive(root, "ha_rest_enabled");
    if (cJSON_IsBool(rest_enabled)) {
        out->ha_rest_enabled = cJSON_IsTrue(rest_enabled);
    }

    if (cJSON_IsObject(time_cfg)) {
        json_copy_string(time_cfg, "ntp_server", out->ntp_server, sizeof(out->ntp_server));
        json_copy_string(time_cfg, "timezone", out->time_tz, sizeof(out->time_tz));
    } else {
        json_copy_string(root, "ntp_server", out->ntp_server, sizeof(out->ntp_server));
        json_copy_string(root, "time_tz", out->time_tz, sizeof(out->time_tz));
    }

    if (cJSON_IsObject(ui)) {
        json_copy_string(ui, "language", out->ui_language, sizeof(out->ui_language));
    } else {
        json_copy_string(root, "language", out->ui_language, sizeof(out->ui_language));
    }

    if (cJSON_IsObject(weather)) {
        json_copy_bool(weather, "topbar_enabled", &out->topbar_weather_enabled);
        json_copy_string(weather, "entity_id", out->topbar_weather_entity_id, sizeof(out->topbar_weather_entity_id));
    } else {
        json_copy_bool(root, "topbar_weather_enabled", &out->topbar_weather_enabled);
        json_copy_string(root, "topbar_weather_entity_id", out->topbar_weather_entity_id, sizeof(out->topbar_weather_entity_id));
    }

    if (cJSON_IsObject(stocks)) {
        cJSON *entity_ids = cJSON_GetObjectItemCaseSensitive(stocks, "entity_ids");
        if (cJSON_IsArray(entity_ids)) {
            for (size_t i = 0; i < 3U; i++) {
                cJSON *item = cJSON_GetArrayItem(entity_ids, (int)i);
                if (cJSON_IsString(item) && item->valuestring != NULL) {
                    strlcpy(out->topbar_stock_entity_ids[i], item->valuestring, sizeof(out->topbar_stock_entity_ids[i]));
                }
            }
        }
    } else {
        json_copy_string(root, "topbar_stock_entity_id_1", out->topbar_stock_entity_ids[0], sizeof(out->topbar_stock_entity_ids[0]));
        json_copy_string(root, "topbar_stock_entity_id_2", out->topbar_stock_entity_ids[1], sizeof(out->topbar_stock_entity_ids[1]));
        json_copy_string(root, "topbar_stock_entity_id_3", out->topbar_stock_entity_ids[2], sizeof(out->topbar_stock_entity_ids[2]));
    }

    if (cJSON_IsObject(topbar)) {
        cJSON *left_slots = cJSON_GetObjectItemCaseSensitive(topbar, "left_slots");
        if (cJSON_IsArray(left_slots)) {
            for (size_t i = 0; i < 2U; i++) {
                cJSON *item = cJSON_GetArrayItem(left_slots, (int)i);
                if (cJSON_IsString(item) && item->valuestring != NULL) {
                    strlcpy(out->topbar_left_slots[i], item->valuestring, sizeof(out->topbar_left_slots[i]));
                } else if (cJSON_IsNull(item)) {
                    out->topbar_left_slots[i][0] = '\0';
                }
            }
        }
        json_copy_string(topbar, "weather_entity_id", out->topbar_weather_entity_id, sizeof(out->topbar_weather_entity_id));
        json_copy_string(topbar, "stock_entity_id", out->topbar_stock_entity_ids[0], sizeof(out->topbar_stock_entity_ids[0]));
        out->topbar_stock_entity_ids[1][0] = '\0';
        out->topbar_stock_entity_ids[2][0] = '\0';
    }
    for (size_t i = 0; i < 2U; i++) {
        topbar_normalize_slot(out->topbar_left_slots[i], sizeof(out->topbar_left_slots[i]));
    }
    topbar_migrate_legacy_slots(out);
    out->topbar_weather_enabled = topbar_has_slot(out, "weather");

    if (cJSON_IsObject(hardware)) {
        json_copy_int(hardware, "active_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(hardware, "dim_brightness", &out->hardware_dim_brightness_percent);
        json_copy_int(hardware, "dim_timeout_seconds", &out->hardware_dim_timeout_seconds);
        json_copy_int(hardware, "day_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(hardware, "night_brightness", &out->hardware_dim_brightness_percent);
    } else {
        json_copy_int(root, "hardware_active_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(root, "hardware_dim_brightness", &out->hardware_dim_brightness_percent);
        json_copy_int(root, "hardware_dim_timeout_seconds", &out->hardware_dim_timeout_seconds);
        json_copy_int(root, "hardware_day_brightness", &out->hardware_active_brightness_percent);
        json_copy_int(root, "hardware_night_brightness", &out->hardware_dim_brightness_percent);
    }
    out->hardware_active_brightness_percent = clamp_config_brightness(out->hardware_active_brightness_percent);
    out->hardware_dim_brightness_percent = clamp_config_brightness(out->hardware_dim_brightness_percent);
    out->hardware_dim_timeout_seconds = clamp_dim_timeout_seconds(out->hardware_dim_timeout_seconds);

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t nvs_load_secret(const char *key, char *out, size_t out_len, bool *out_found)
{
    if (key == NULL || out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (out_found != NULL) {
        *out_found = false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t required = 0;
    err = nvs_get_str(handle, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    if (required == 0) {
        if (out_found != NULL) {
            *out_found = true;
        }
        nvs_close(handle);
        return ESP_OK;
    }
    if (required > out_len) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_get_str(handle, key, out, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    if (out_found != NULL) {
        *out_found = true;
    }
    return ESP_OK;
}

static esp_err_t nvs_set_or_erase_secret(nvs_handle_t handle, const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (value[0] == '\0') {
        esp_err_t err = nvs_erase_key(handle, key);
        if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        }
        return err;
    }

    return nvs_set_str(handle, key, value);
}

static esp_err_t nvs_save_secrets(const runtime_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_or_erase_secret(handle, SETTINGS_NVS_KEY_WIFI_PASSWORD, settings->wifi_password);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_set_or_erase_secret(handle, SETTINGS_NVS_KEY_HA_ACCESS_TOKEN, settings->ha_access_token);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void runtime_settings_merge_secrets_from_nvs(runtime_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }

    char wifi_password[APP_WIFI_PASSWORD_MAX_LEN] = {0};
    bool has_wifi_password = false;
    esp_err_t wifi_err =
        nvs_load_secret(SETTINGS_NVS_KEY_WIFI_PASSWORD, wifi_password, sizeof(wifi_password), &has_wifi_password);
    if (wifi_err == ESP_OK && has_wifi_password) {
        strlcpy(settings->wifi_password, wifi_password, sizeof(settings->wifi_password));
    }

    char ha_access_token[APP_HA_ACCESS_TOKEN_MAX_LEN] = {0};
    bool has_ha_access_token = false;
    esp_err_t token_err = nvs_load_secret(
        SETTINGS_NVS_KEY_HA_ACCESS_TOKEN, ha_access_token, sizeof(ha_access_token), &has_ha_access_token);
    if (token_err == ESP_OK && has_ha_access_token) {
        strlcpy(settings->ha_access_token, ha_access_token, sizeof(settings->ha_access_token));
    }
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
    strlcpy(out->ntp_server, APP_NTP_SERVER, sizeof(out->ntp_server));
    strlcpy(out->time_tz, APP_TIME_TZ, sizeof(out->time_tz));
    strlcpy(out->ui_language, APP_UI_DEFAULT_LANGUAGE, sizeof(out->ui_language));
    strlcpy(out->topbar_left_slots[0], "date", sizeof(out->topbar_left_slots[0]));
    out->topbar_left_slots[1][0] = '\0';
    out->topbar_weather_enabled = false;
    out->topbar_weather_entity_id[0] = '\0';
    out->hardware_active_brightness_percent = APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT;
    out->hardware_dim_brightness_percent = APP_DISPLAY_DIM_BRIGHTNESS_PERCENT;
    out->hardware_dim_timeout_seconds = APP_DISPLAY_DIM_TIMEOUT_MS / 1000;
}

esp_err_t runtime_settings_load(runtime_settings_t *out)
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

    bool legacy_wifi_password = false;
    bool legacy_ha_access_token = false;
    esp_err_t parse_err = parse_settings_json(json, out, &legacy_wifi_password, &legacy_ha_access_token);
    free(json);
    if (parse_err != ESP_OK) {
        return parse_err;
    }
    if (out->wifi_country_code[0] == '\0') {
        strlcpy(out->wifi_country_code, APP_WIFI_COUNTRY_CODE, sizeof(out->wifi_country_code));
    }
    normalize_ui_language(out->ui_language, sizeof(out->ui_language));

    char nvs_wifi_password[APP_WIFI_PASSWORD_MAX_LEN] = {0};
    bool has_nvs_wifi_password = false;
    esp_err_t nvs_err =
        nvs_load_secret(SETTINGS_NVS_KEY_WIFI_PASSWORD, nvs_wifi_password, sizeof(nvs_wifi_password), &has_nvs_wifi_password);
    if (nvs_err != ESP_OK) {
        return nvs_err;
    }
    if (has_nvs_wifi_password) {
        strlcpy(out->wifi_password, nvs_wifi_password, sizeof(out->wifi_password));
    }

    char nvs_ha_access_token[APP_HA_ACCESS_TOKEN_MAX_LEN] = {0};
    bool has_nvs_ha_access_token = false;
    nvs_err = nvs_load_secret(
        SETTINGS_NVS_KEY_HA_ACCESS_TOKEN, nvs_ha_access_token, sizeof(nvs_ha_access_token), &has_nvs_ha_access_token);
    if (nvs_err != ESP_OK) {
        return nvs_err;
    }
    if (has_nvs_ha_access_token) {
        strlcpy(out->ha_access_token, nvs_ha_access_token, sizeof(out->ha_access_token));
    }

    bool migrate_to_nvs =
        (legacy_wifi_password && !has_nvs_wifi_password && out->wifi_password[0] != '\0') ||
        (legacy_ha_access_token && !has_nvs_ha_access_token && out->ha_access_token[0] != '\0');

    if (migrate_to_nvs) {
        nvs_err = nvs_save_secrets(out);
        if (nvs_err != ESP_OK) {
            return nvs_err;
        }
    }

    if (legacy_wifi_password || legacy_ha_access_token) {
        esp_err_t scrub_err = write_public_settings_file(out);
        if (scrub_err != ESP_OK) {
            ESP_LOGW(TAG_APP, "Failed to scrub legacy secrets from LittleFS settings: %s", esp_err_to_name(scrub_err));
        }
    }

    return ESP_OK;
}

esp_err_t runtime_settings_save(const runtime_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t file_err = write_public_settings_file(settings);
    if (file_err != ESP_OK) {
        return file_err;
    }

    esp_err_t nvs_err = nvs_save_secrets(settings);
    if (nvs_err != ESP_OK) {
        return nvs_err;
    }

    ESP_LOGI(TAG_APP, "Saved runtime settings");
    return ESP_OK;
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

    ESP_LOGW(TAG_APP, "Settings missing/invalid (%s), writing defaults", esp_err_to_name(err));
    runtime_settings_set_defaults(settings);
    runtime_settings_merge_secrets_from_nvs(settings);
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
