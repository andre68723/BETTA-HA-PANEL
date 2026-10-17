/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/api_routes.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "app_config.h"
#include "bsp/display.h"
#include "drivers/ambient_camera.h"
#include "drivers/display_orientation.h"
#include "drivers/chime.h"
#include "drivers/audio_out.h"
#include "drivers/display_init.h"
#include "ha/ha_auth_state.h"
#include "ha/ha_client.h"
#include "net/mqtt_mgr.h"
#include "net/wifi_mgr.h"
#include "settings/i18n_store.h"
#include "app_events.h"
#include "net/time_sync.h"
#include "net/update_check.h"
#include "net/auto_update_task.h"
#include "settings/admin_auth.h"
#include "settings/device_identity.h"
#include "settings/night_mode.h"
#include "settings/runtime_settings.h"
#include "settings/settings_restart.h"
#include "settings/settings_validate.h"
#include "util/entity_id.h"
#include "settings/brightness_model.h"
#include "ui/text_scale.h"
#include "ui/theme/ui_style_registry.h"
#include "util/reboot_log_esp.h"

static esp_timer_handle_t s_restart_timer = NULL;

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", (message != NULL) ? message : "Invalid request");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

static bool has_ws_scheme(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return true;
    }
    return strncmp(url, "ws://", 5) == 0 || strncmp(url, "wss://", 6) == 0;
}

static bool normalize_country_code(char *country_code, size_t country_code_len)
{
    if (country_code == NULL || country_code_len < APP_WIFI_COUNTRY_CODE_MAX_LEN) {
        return false;
    }
    if (country_code[0] == '\0') {
        strlcpy(country_code, APP_WIFI_COUNTRY_CODE, country_code_len);
        return true;
    }
    if (strlen(country_code) != 2) {
        return false;
    }
    if (!isalpha((unsigned char)country_code[0]) || !isalpha((unsigned char)country_code[1])) {
        return false;
    }

    country_code[0] = (char)toupper((unsigned char)country_code[0]);
    country_code[1] = (char)toupper((unsigned char)country_code[1]);
    country_code[2] = '\0';
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool normalize_bssid(char *bssid, size_t bssid_len)
{
    if (bssid == NULL || bssid_len < APP_WIFI_BSSID_MAX_LEN) {
        return false;
    }
    if (bssid[0] == '\0') {
        return true;
    }
    if (strlen(bssid) != 17U) {
        return false;
    }

    char sep = bssid[2];
    if (sep != ':' && sep != '-') {
        return false;
    }

    for (size_t i = 0; i < 6; i++) {
        size_t idx = i * 3;
        int hi = hex_nibble(bssid[idx]);
        int lo = hex_nibble(bssid[idx + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        bssid[idx] = (char)toupper((unsigned char)bssid[idx]);
        bssid[idx + 1] = (char)toupper((unsigned char)bssid[idx + 1]);
        if (i < 5) {
            if (bssid[idx + 2] != sep) {
                return false;
            }
            bssid[idx + 2] = ':';
        }
    }
    bssid[17] = '\0';
    return true;
}

static bool normalize_ui_language(char *language, size_t language_len)
{
    if (language == NULL || language_len == 0) {
        return false;
    }
    if (language[0] == '\0') {
        strlcpy(language, APP_UI_DEFAULT_LANGUAGE, language_len);
        return true;
    }

    char normalized[APP_UI_LANGUAGE_MAX_LEN] = {0};
    if (!i18n_store_normalize_language_code(language, normalized, sizeof(normalized))) {
        return false;
    }
    strlcpy(language, normalized, language_len);
    return true;
}

/* Brightness, night mode and the camera features take effect without a reboot
 * (PUT with "reboot": false, used by the editor's live sliders). */
/* Signature of everything that needs the UI to be rebuilt when it changes. */
static void ui_settings_signature(const runtime_settings_t *s, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s|%s|%s|%s|%s|%d|%s|%s|%d", s->ui_language, s->ui_style, s->topbar_left_slots[0],
        s->topbar_left_slots[1], s->topbar_left_slots[2], s->topbar_weather_enabled ? 1 : 0,
        s->topbar_weather_entity_id, s->topbar_stock_entity_id, s->ui_text_step);
}

static void apply_hardware_settings_live(const runtime_settings_t *settings)
{
    display_orientation_configure(settings->hardware_auto_rotation);
    chime_configure(settings->sound_enabled, settings->sound_volume, settings->sound_night_mute);
    /* The time zone is a single call. */
    (void)time_sync_set_timezone(settings->time_tz);
    display_configure_brightness(settings->hardware_active_brightness_percent,
        settings->hardware_dim_brightness_percent, settings->hardware_dim_timeout_seconds);
    night_mode_configure(settings->hardware_night_enabled, settings->hardware_night_start_min,
        settings->hardware_night_end_min, settings->hardware_night_brightness_percent);
    ambient_camera_configure(settings->hardware_auto_brightness, settings->hardware_presence_wake,
        settings->hardware_presence_sensitivity, settings->hardware_night_presence_wake);
}

static void restart_timer_cb(void *arg)
{
    (void)arg;
    /* Avoid random panel colors during software reset. */
    (void)bsp_display_backlight_off();
    esp_restart();
}

static void schedule_restart(void)
{
    reboot_log_mark(REBOOT_CAUSE_SETTINGS);
    if (s_restart_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &restart_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "settings_restart",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&timer_args, &s_restart_timer) != ESP_OK) {
            esp_restart();
            return;
        }
    }

    if (esp_timer_is_active(s_restart_timer)) {
        (void)esp_timer_stop(s_restart_timer);
    }
    if (esp_timer_start_once(s_restart_timer, 1500ULL * 1000ULL) != ESP_OK) {
        esp_restart();
    }
}

esp_err_t api_settings_get_handler(httpd_req_t *req)
{
    runtime_settings_t *settings = calloc(1, sizeof(runtime_settings_t));
    if (settings == NULL) {
        return httpd_resp_send_500(req);
    }

    /* Not the defaults: an editor that saves what it was shown would overwrite the real settings. The editor
     * retries a failed read (settings.js, loadSettingsForBootstrap). */
    esp_err_t settings_err = runtime_settings_load(settings);
    if (settings_err != ESP_OK) {
        free(settings);
        return send_json_error(req, "500 Internal Server Error", "Stored settings could not be read");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *ha = cJSON_CreateObject();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON *time_cfg = cJSON_CreateObject();
    cJSON *ui = cJSON_CreateObject();
    cJSON *topbar = cJSON_CreateObject();
    cJSON *topbar_left_slots = cJSON_CreateArray();
    cJSON *hardware = cJSON_CreateObject();
    if (root == NULL || wifi == NULL || ha == NULL || mqtt == NULL || time_cfg == NULL || ui == NULL ||
        topbar == NULL || topbar_left_slots == NULL || hardware == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(wifi);
        cJSON_Delete(ha);
        cJSON_Delete(mqtt);
        cJSON_Delete(time_cfg);
        cJSON_Delete(ui);
        cJSON_Delete(topbar);
        cJSON_Delete(topbar_left_slots);
        cJSON_Delete(hardware);
        free(settings);
        return httpd_resp_send_500(req);
    }

    cJSON_AddStringToObject(wifi, "ssid", settings->wifi_ssid);
    cJSON_AddStringToObject(wifi, "country_code", settings->wifi_country_code);
    cJSON_AddStringToObject(wifi, "bssid", settings->wifi_bssid);
    cJSON_AddBoolToObject(wifi, "password_set", settings->wifi_password[0] != '\0');
    cJSON_AddBoolToObject(wifi, "configured", runtime_settings_has_wifi(settings));
    cJSON_AddBoolToObject(wifi, "connected", wifi_mgr_is_connected());
    cJSON_AddBoolToObject(wifi, "setup_ap_active", wifi_mgr_is_setup_ap_active());
    cJSON_AddStringToObject(wifi, "setup_ap_ssid", wifi_mgr_get_setup_ap_ssid());
    wifi_mgr_sta_ap_info_t sta_ap = {0};
    if (wifi_mgr_get_sta_ap_info(&sta_ap) == ESP_OK) {
        char connected_bssid[APP_WIFI_BSSID_MAX_LEN] = {0};
        snprintf(
            connected_bssid,
            sizeof(connected_bssid),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            sta_ap.bssid[0],
            sta_ap.bssid[1],
            sta_ap.bssid[2],
            sta_ap.bssid[3],
            sta_ap.bssid[4],
            sta_ap.bssid[5]);
        cJSON_AddNumberToObject(wifi, "rssi_dbm", (double)sta_ap.rssi);
        cJSON_AddStringToObject(wifi, "connected_bssid", connected_bssid);
        cJSON_AddNumberToObject(wifi, "connected_channel", (double)sta_ap.channel);
    } else {
        cJSON_AddNullToObject(wifi, "rssi_dbm");
        cJSON_AddNullToObject(wifi, "connected_bssid");
        cJSON_AddNullToObject(wifi, "connected_channel");
    }
    cJSON_AddBoolToObject(wifi, "scan_supported", true);
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON_AddStringToObject(ha, "ws_url", settings->ha_ws_url);
    cJSON_AddBoolToObject(ha, "access_token_set", settings->ha_access_token[0] != '\0');
    cJSON_AddBoolToObject(ha, "rest_enabled", settings->ha_rest_enabled);
    cJSON_AddBoolToObject(ha, "configured", runtime_settings_has_ha(settings));
    cJSON_AddBoolToObject(ha, "connected", ha_client_is_connected());
    /* "ok", or why the panel stopped talking to Home Assistant (ha_auth_state.h). */
    cJSON_AddStringToObject(ha, "auth", ha_auth_state_name());
    cJSON_AddItemToObject(root, "ha", ha);

    cJSON_AddBoolToObject(mqtt, "enabled", settings->mqtt_enabled);
    cJSON_AddStringToObject(mqtt, "broker_host", settings->mqtt_broker_host);
    cJSON_AddNumberToObject(mqtt, "broker_port", settings->mqtt_broker_port);
    cJSON_AddBoolToObject(mqtt, "tls_enabled", settings->mqtt_tls_enabled);
    cJSON_AddStringToObject(mqtt, "username", settings->mqtt_username);
    cJSON_AddStringToObject(mqtt, "topic_prefix", settings->mqtt_topic_prefix);
    cJSON_AddStringToObject(mqtt, "device_name", settings->mqtt_device_name);
    cJSON_AddBoolToObject(mqtt, "password_set", settings->mqtt_password[0] != '\0');
    cJSON_AddBoolToObject(mqtt, "configured", runtime_settings_has_mqtt(settings));
    cJSON_AddBoolToObject(mqtt, "connected", mqtt_mgr_is_connected());
    const char *command_topic = mqtt_mgr_get_command_topic();
    if (command_topic != NULL && command_topic[0] != '\0') {
        cJSON_AddStringToObject(mqtt, "command_topic", command_topic);
    }
    cJSON_AddItemToObject(root, "mqtt", mqtt);
    {
        /* Device identity: the room the panel stands in and the id derived from it. */
        cJSON *device = cJSON_CreateObject();
        char id[40] = {0};
        char name[64] = {0};
        device_identity_set_room(settings->device_room);
        device_identity_effective_id(id, sizeof(id));
        device_identity_display_name(name, sizeof(name));
        cJSON_AddStringToObject(device, "room", settings->device_room);
        cJSON_AddStringToObject(device, "id", id);
        cJSON_AddStringToObject(device, "name", name);
        cJSON_AddItemToObject(root, "device", device);
    }
    {
        cJSON *security = cJSON_CreateObject();
        cJSON_AddBoolToObject(security, "password_set", admin_auth_enabled());
        cJSON_AddStringToObject(security, "user", ADMIN_AUTH_USER);
        cJSON_AddItemToObject(root, "security", security);
    }

    cJSON_AddStringToObject(time_cfg, "ntp_server", settings->ntp_server);
    cJSON_AddStringToObject(time_cfg, "timezone", settings->time_tz);
    cJSON_AddItemToObject(root, "time", time_cfg);

    cJSON_AddStringToObject(ui, "language", settings->ui_language);
    cJSON_AddStringToObject(ui, "style", settings->ui_style);
    cJSON_AddNumberToObject(ui, "text_step", settings->ui_text_step);
    cJSON_AddItemToObject(root, "ui", ui);

    for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
        cJSON *item = cJSON_CreateString(settings->topbar_left_slots[i]);
        if (item == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(topbar);
            cJSON_Delete(topbar_left_slots);
            cJSON_Delete(hardware);
            free(settings);
            return httpd_resp_send_500(req);
        }
        cJSON_AddItemToArray(topbar_left_slots, item);
    }
    cJSON_AddItemToObject(topbar, "left_slots", topbar_left_slots);
    cJSON_AddStringToObject(topbar, "weather_entity_id", settings->topbar_weather_entity_id);
    cJSON_AddStringToObject(topbar, "stock_entity_id", settings->topbar_stock_entity_id);
    cJSON_AddItemToObject(root, "topbar", topbar);

    cJSON_AddNumberToObject(hardware, "active_brightness", settings->hardware_active_brightness_percent);
    cJSON_AddNumberToObject(hardware, "dim_brightness", settings->hardware_dim_brightness_percent);
    cJSON_AddNumberToObject(hardware, "dim_timeout_seconds", settings->hardware_dim_timeout_seconds);
    cJSON_AddStringToObject(hardware, "saver_mode", settings->hardware_saver_mode);
    cJSON_AddNumberToObject(hardware, "saver_timeout_minutes", settings->hardware_saver_timeout_minutes);
    cJSON_AddNumberToObject(hardware, "saver_weather_minutes", settings->hardware_saver_weather_minutes);
    cJSON_AddNumberToObject(hardware, "saver_pages_minutes", settings->hardware_saver_pages_minutes);
    cJSON_AddNumberToObject(hardware, "saver_page_interval_minutes", settings->hardware_saver_page_interval_minutes);
    cJSON_AddBoolToObject(hardware, "saver_page_wake_home", settings->hardware_saver_page_wake_home);
    cJSON_AddNumberToObject(hardware, "saver_weather_brightness_percent", settings->hardware_saver_weather_brightness_percent);
    cJSON_AddBoolToObject(hardware, "saver_pixel_refresh", settings->hardware_saver_pixel_refresh);
    cJSON_AddStringToObject(hardware, "saver_weather_entity_id", settings->hardware_saver_weather_entity_id);
    cJSON_AddBoolToObject(hardware, "sound_supported", audio_out_available());
    cJSON_AddBoolToObject(hardware, "sound_enabled", settings->sound_enabled);
    cJSON_AddNumberToObject(hardware, "sound_volume", settings->sound_volume);
    cJSON_AddBoolToObject(hardware, "sound_night_mute", settings->sound_night_mute);
    cJSON_AddBoolToObject(hardware, "night_enabled", settings->hardware_night_enabled);
    cJSON_AddNumberToObject(hardware, "night_brightness_percent", settings->hardware_night_brightness_percent);
    cJSON_AddBoolToObject(hardware, "auto_brightness", settings->hardware_auto_brightness);
    cJSON_AddBoolToObject(hardware, "rotation_supported", display_orientation_supported());
    cJSON_AddBoolToObject(hardware, "auto_rotation", settings->hardware_auto_rotation);
    cJSON_AddBoolToObject(hardware, "presence_wake", settings->hardware_presence_wake);
    cJSON_AddNumberToObject(hardware, "presence_sensitivity", settings->hardware_presence_sensitivity);
    cJSON_AddBoolToObject(hardware, "night_presence_wake", settings->hardware_night_presence_wake);
    {
        char hhmm[8];
        runtime_settings_format_hhmm(settings->hardware_night_start_min, hhmm, sizeof(hhmm));
        cJSON_AddStringToObject(hardware, "night_start", hhmm);
        runtime_settings_format_hhmm(settings->hardware_night_end_min, hhmm, sizeof(hhmm));
        cJSON_AddStringToObject(hardware, "night_end", hhmm);
    }
    cJSON_AddItemToObject(root, "hardware", hardware);
    {
        cJSON *ota = cJSON_AddObjectToObject(root, "ota");
        cJSON_AddBoolToObject(ota, "update_check", settings->ota_update_check);
        cJSON_AddBoolToObject(ota, "include_prereleases", settings->ota_include_prereleases);
        cJSON_AddBoolToObject(ota, "auto_install", settings->ota_auto_install);
    }

    cJSON_AddBoolToObject(root, "ok", true);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(settings);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

static bool update_string_setting(
    cJSON *obj,
    const char *key,
    char *dst,
    size_t dst_len,
    bool *out_invalid_type,
    bool *out_too_long)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return false;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        if (strlen(item->valuestring) >= dst_len) {
            if (out_too_long != NULL) {
                *out_too_long = true;
            }
            return false;
        }
        strlcpy(dst, item->valuestring, dst_len);
        return true;
    }
    if (cJSON_IsNull(item)) {
        dst[0] = '\0';
        return true;
    }

    if (out_invalid_type != NULL) {
        *out_invalid_type = true;
    }
    return false;
}

static bool update_bool_setting(cJSON *obj, const char *key, bool *dst, bool *out_invalid_type)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return false;
    }

    if (cJSON_IsBool(item)) {
        *dst = cJSON_IsTrue(item);
        return true;
    }

    if (out_invalid_type != NULL) {
        *out_invalid_type = true;
    }
    return false;
}

static bool update_int_setting(
    cJSON *obj,
    const char *key,
    int *dst,
    int min_value,
    int max_value,
    bool *out_invalid_type,
    bool *out_out_of_range)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return false;
    }

    if (cJSON_IsNumber(item)) {
        if (item->valuedouble != floor(item->valuedouble)) {
            if (out_invalid_type != NULL) {
                *out_invalid_type = true; /* 50.7 is not a whole number: reject instead of truncating */
            }
            return false;
        }
        int value = item->valueint;
        if (value < min_value || value > max_value) {
            if (out_out_of_range != NULL) {
                *out_out_of_range = true;
            }
            return false;
        }
        *dst = value;
        return true;
    }

    if (out_invalid_type != NULL) {
        *out_invalid_type = true;
    }
    return false;
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

esp_err_t api_settings_put_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > APP_SETTINGS_MAX_JSON_LEN) {
        return send_json_error(req, "400 Bad Request", "Invalid payload size");
    }

    char *buf = calloc((size_t)req->content_len + 1U, sizeof(char));
    if (buf == NULL) {
        return httpd_resp_send_500(req);
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return send_json_error(req, "400 Bad Request", "Failed to read request body");
        }
        received += r;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "Invalid JSON");
    }

    /* settings[1] keeps the stored values for "reboot": "auto"; one allocation, so every
     * free(settings) below releases both. */
    runtime_settings_t *settings = calloc(2, sizeof(runtime_settings_t));
    if (settings == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    /* A failed read must not turn into a save of defaults plus this request: that would overwrite the stored
     * Wi-Fi, Home Assistant and MQTT settings and erase their secrets. Boot already rewrites a missing or
     * corrupt file (runtime_settings_init), so only a transient error (storage, memory) reaches this point. */
    esp_err_t load_err = runtime_settings_load(settings);
    if (load_err != ESP_OK) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "500 Internal Server Error", "Stored settings could not be read; nothing was changed");
    }
    settings[1] = settings[0];
    char ui_sig_before[512];
    ui_settings_signature(settings, ui_sig_before, sizeof(ui_sig_before));
    char old_saver_mode[sizeof(settings->hardware_saver_mode)];
    char old_saver_weather_entity_id[sizeof(settings->hardware_saver_weather_entity_id)];
    strlcpy(old_saver_mode, settings->hardware_saver_mode, sizeof(old_saver_mode));
    strlcpy(old_saver_weather_entity_id, settings->hardware_saver_weather_entity_id,
        sizeof(old_saver_weather_entity_id));
    const int old_saver_timeout = settings->hardware_saver_timeout_minutes;
    const int old_saver_weather_minutes = settings->hardware_saver_weather_minutes;
    const int old_saver_pages_minutes = settings->hardware_saver_pages_minutes;
    const int old_saver_page_interval = settings->hardware_saver_page_interval_minutes;
    const int old_saver_weather_brightness = settings->hardware_saver_weather_brightness_percent;
    const int old_dim_brightness = settings->hardware_dim_brightness_percent;
    const bool old_saver_page_wake_home = settings->hardware_saver_page_wake_home;
    const bool old_saver_pixel_refresh = settings->hardware_saver_pixel_refresh;
    /* What is stored now: a changed address must not receive the stored secret (see the check below). */
    char old_ws_url[sizeof(settings->ha_ws_url)];
    char old_mqtt_host[sizeof(settings->mqtt_broker_host)];
    strlcpy(old_ws_url, settings->ha_ws_url, sizeof(old_ws_url));
    strlcpy(old_mqtt_host, settings->mqtt_broker_host, sizeof(old_mqtt_host));
    const bool had_ha_token = settings->ha_access_token[0] != '\0';
    const bool had_mqtt_password = settings->mqtt_password[0] != '\0';

    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    cJSON *ha = cJSON_GetObjectItemCaseSensitive(root, "ha");
    cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    cJSON *time_cfg = cJSON_GetObjectItemCaseSensitive(root, "time");
    cJSON *ui = cJSON_GetObjectItemCaseSensitive(root, "ui");
    cJSON *topbar = cJSON_GetObjectItemCaseSensitive(root, "topbar");
    cJSON *hardware = cJSON_GetObjectItemCaseSensitive(root, "hardware");
    cJSON *security_cfg = cJSON_GetObjectItemCaseSensitive(root, "security");
    cJSON *ota = cJSON_GetObjectItemCaseSensitive(root, "ota");
    if (ota != NULL && !cJSON_IsObject(ota)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "ota must be an object");
    }
    if (wifi != NULL && !cJSON_IsObject(wifi)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "wifi must be an object");
    }
    if (ha != NULL && !cJSON_IsObject(ha)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "ha must be an object");
    }
    if (mqtt != NULL && !cJSON_IsObject(mqtt)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "mqtt must be an object");
    }
    if (time_cfg != NULL && !cJSON_IsObject(time_cfg)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "time must be an object");
    }
    if (ui != NULL && !cJSON_IsObject(ui)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "ui must be an object");
    }
    if (topbar != NULL && !cJSON_IsObject(topbar)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "topbar must be an object");
    }
    if (hardware != NULL && !cJSON_IsObject(hardware)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "hardware must be an object");
    }
    if (security_cfg != NULL && !cJSON_IsObject(security_cfg)) {
        cJSON_Delete(root);
        free(settings);
        return send_json_error(req, "400 Bad Request", "security must be an object");
    }

    bool invalid_type = false;
    bool too_long = false;
    bool out_of_range = false;
    bool change_admin_password = false;
    char admin_password[ADMIN_AUTH_MAX_LEN + 1] = {0};
    if (cJSON_IsObject(wifi)) {
        (void)update_string_setting(
            wifi, "ssid", settings->wifi_ssid, sizeof(settings->wifi_ssid), &invalid_type, &too_long);
        (void)update_string_setting(
            wifi, "password", settings->wifi_password, sizeof(settings->wifi_password), &invalid_type, &too_long);
        (void)update_string_setting(
            wifi, "country_code", settings->wifi_country_code, sizeof(settings->wifi_country_code), &invalid_type, &too_long);
        (void)update_string_setting(
            wifi, "bssid", settings->wifi_bssid, sizeof(settings->wifi_bssid), &invalid_type, &too_long);
    }
    if (cJSON_IsObject(ha)) {
        (void)update_string_setting(
            ha, "ws_url", settings->ha_ws_url, sizeof(settings->ha_ws_url), &invalid_type, &too_long);
        (void)update_string_setting(
            ha, "access_token", settings->ha_access_token, sizeof(settings->ha_access_token), &invalid_type, &too_long);
        (void)update_bool_setting(ha, "rest_enabled", &settings->ha_rest_enabled, &invalid_type);
    }
    if (cJSON_IsObject(mqtt)) {
        (void)update_bool_setting(mqtt, "enabled", &settings->mqtt_enabled, &invalid_type);
        (void)update_string_setting(
            mqtt, "broker_host", settings->mqtt_broker_host, sizeof(settings->mqtt_broker_host), &invalid_type, &too_long);
        (void)update_int_setting(
            mqtt, "broker_port", &settings->mqtt_broker_port, 1, 65535, &invalid_type, &out_of_range);
        (void)update_bool_setting(mqtt, "tls_enabled", &settings->mqtt_tls_enabled, &invalid_type);
        (void)update_string_setting(
            mqtt, "username", settings->mqtt_username, sizeof(settings->mqtt_username), &invalid_type, &too_long);
        (void)update_string_setting(
            mqtt, "password", settings->mqtt_password, sizeof(settings->mqtt_password), &invalid_type, &too_long);
        (void)update_string_setting(
            mqtt, "topic_prefix", settings->mqtt_topic_prefix, sizeof(settings->mqtt_topic_prefix), &invalid_type,
            &too_long);
        (void)update_string_setting(
            mqtt, "device_name", settings->mqtt_device_name, sizeof(settings->mqtt_device_name), &invalid_type,
            &too_long);
    }
    cJSON *device_cfg = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (cJSON_IsObject(device_cfg)) {
        (void)update_string_setting(
            device_cfg, "room", settings->device_room, sizeof(settings->device_room), &invalid_type, &too_long);
    }
    if (cJSON_IsObject(security_cfg)) {
        cJSON *pw = cJSON_GetObjectItemCaseSensitive(security_cfg, "admin_password");
        if (cJSON_IsString(pw) && pw->valuestring != NULL) {
            const size_t n = strlen(pw->valuestring);
            if (n != 0 && (n < ADMIN_AUTH_MIN_LEN || n > ADMIN_AUTH_MAX_LEN)) {
                too_long = true;
            } else {
                strlcpy(admin_password, pw->valuestring, sizeof(admin_password));
                change_admin_password = true;
            }
        } else if (pw != NULL) {
            invalid_type = true;
        }
    }
    if (cJSON_IsObject(time_cfg)) {
        (void)update_string_setting(
            time_cfg, "ntp_server", settings->ntp_server, sizeof(settings->ntp_server), &invalid_type, &too_long);
        (void)update_string_setting(
            time_cfg, "timezone", settings->time_tz, sizeof(settings->time_tz), &invalid_type, &too_long);
    }
    if (cJSON_IsObject(ui)) {
        (void)update_string_setting(
            ui, "language", settings->ui_language, sizeof(settings->ui_language), &invalid_type, &too_long);
        (void)update_int_setting(ui, "text_step", &settings->ui_text_step, 0, TEXT_USER_STEP_MAX, &invalid_type, &out_of_range);
        cJSON *style_item = cJSON_GetObjectItemCaseSensitive(ui, "style");
        if (style_item != NULL) {
            ui_style_kind_t style_kind;
            if (cJSON_IsString(style_item) && ui_style_lookup(style_item->valuestring, &style_kind)) {
                strlcpy(settings->ui_style, ui_style_name(style_kind), sizeof(settings->ui_style));
            } else {
                invalid_type = true;
            }
        }
    }
    if (cJSON_IsObject(topbar)) {
        cJSON *left_slots = cJSON_GetObjectItemCaseSensitive(topbar, "left_slots");
        if (left_slots != NULL && !cJSON_IsArray(left_slots)) {
            invalid_type = true;
        } else if (cJSON_IsArray(left_slots)) {
            for (size_t i = 0; i < APP_TOPBAR_LEFT_SLOTS; i++) {
                cJSON *item = cJSON_GetArrayItem(left_slots, (int)i);
                if (item == NULL || cJSON_IsNull(item)) {
                    settings->topbar_left_slots[i][0] = '\0';
                    continue;
                }
                if (!cJSON_IsString(item) || item->valuestring == NULL) {
                    invalid_type = true;
                    continue;
                }
                if (strlen(item->valuestring) >= sizeof(settings->topbar_left_slots[i])) {
                    too_long = true;
                    continue;
                }
                if (!topbar_slot_is_valid(item->valuestring)) {
                    invalid_type = true;
                    continue;
                }
                strlcpy(settings->topbar_left_slots[i], item->valuestring, sizeof(settings->topbar_left_slots[i]));
            }
        }
        (void)update_string_setting(
            topbar, "weather_entity_id", settings->topbar_weather_entity_id, sizeof(settings->topbar_weather_entity_id),
            &invalid_type, &too_long);
        (void)update_string_setting(
            topbar, "stock_entity_id", settings->topbar_stock_entity_id, sizeof(settings->topbar_stock_entity_id),
            &invalid_type, &too_long);
    }
    if (cJSON_IsObject(topbar)) {
        settings->topbar_weather_enabled = topbar_has_slot(settings, "weather");
    }
    if (cJSON_IsObject(hardware)) {
        (void)update_int_setting(
            hardware, "active_brightness", &settings->hardware_active_brightness_percent, 1, 100, &invalid_type,
            &out_of_range);
        (void)update_int_setting(
            hardware, "dim_brightness", &settings->hardware_dim_brightness_percent, 1, 100, &invalid_type,
            &out_of_range);
        (void)update_int_setting(
            hardware, "dim_timeout_seconds", &settings->hardware_dim_timeout_seconds, 5, 3600, &invalid_type,
            &out_of_range);
        (void)update_string_setting(hardware, "saver_mode", settings->hardware_saver_mode,
            sizeof(settings->hardware_saver_mode), &invalid_type, &too_long);
        (void)update_int_setting(hardware, "saver_timeout_minutes", &settings->hardware_saver_timeout_minutes,
            1, 120, &invalid_type, &out_of_range);
        (void)update_int_setting(hardware, "saver_weather_minutes", &settings->hardware_saver_weather_minutes,
            1, 120, &invalid_type, &out_of_range);
        (void)update_int_setting(hardware, "saver_pages_minutes", &settings->hardware_saver_pages_minutes,
            1, 120, &invalid_type, &out_of_range);
        (void)update_int_setting(hardware, "saver_page_interval_minutes", &settings->hardware_saver_page_interval_minutes,
            1, 30, &invalid_type, &out_of_range);
        (void)update_bool_setting(hardware, "saver_page_wake_home", &settings->hardware_saver_page_wake_home, &invalid_type);
        (void)update_int_setting(hardware, "saver_weather_brightness_percent",
            &settings->hardware_saver_weather_brightness_percent, 1, 30, &invalid_type, &out_of_range);
        (void)update_bool_setting(hardware, "saver_pixel_refresh", &settings->hardware_saver_pixel_refresh, &invalid_type);
        (void)update_string_setting(hardware, "saver_weather_entity_id", settings->hardware_saver_weather_entity_id,
            sizeof(settings->hardware_saver_weather_entity_id), &invalid_type, &too_long);
        (void)update_int_setting(
            hardware, "night_brightness_percent", &settings->hardware_night_brightness_percent, 0, 100, &invalid_type,
            &out_of_range);
        (void)update_bool_setting(hardware, "sound_enabled", &settings->sound_enabled, &invalid_type);
        (void)update_bool_setting(hardware, "sound_night_mute", &settings->sound_night_mute, &invalid_type);
        (void)update_int_setting(hardware, "sound_volume", &settings->sound_volume, 0, 100, &invalid_type, &out_of_range);
        (void)update_bool_setting(hardware, "night_enabled", &settings->hardware_night_enabled, &invalid_type);
        (void)update_bool_setting(hardware, "auto_brightness", &settings->hardware_auto_brightness, &invalid_type);
        if (display_orientation_supported())
            (void)update_bool_setting(hardware, "auto_rotation", &settings->hardware_auto_rotation, &invalid_type);
        (void)update_bool_setting(hardware, "presence_wake", &settings->hardware_presence_wake, &invalid_type);
        (void)update_bool_setting(
            hardware, "night_presence_wake", &settings->hardware_night_presence_wake, &invalid_type);
        (void)update_int_setting(
            hardware, "presence_sensitivity", &settings->hardware_presence_sensitivity, 1, 10, &invalid_type,
            &out_of_range);
        static const char *const night_keys[2] = {"night_start", "night_end"};
        int *const night_dst[2] = {&settings->hardware_night_start_min, &settings->hardware_night_end_min};
        for (int i = 0; i < 2; i++) {
            cJSON *item = cJSON_GetObjectItemCaseSensitive(hardware, night_keys[i]);
            if (item == NULL) {
                continue;
            }
            int minutes = 0;
            if (cJSON_IsString(item) && runtime_settings_parse_hhmm(item->valuestring, &minutes)) {
                *night_dst[i] = minutes;
            } else {
                invalid_type = true;
            }
        }
    }

    if (ota != NULL) {
        (void)update_bool_setting(ota, "update_check", &settings->ota_update_check, &invalid_type);
        (void)update_bool_setting(ota, "include_prereleases", &settings->ota_include_prereleases, &invalid_type);
        (void)update_bool_setting(ota, "auto_install", &settings->ota_auto_install, &invalid_type);
    }

    /* true, false, or "auto": restart only when a setting changed that takes effect at boot. */
    bool reboot = true;
    bool reboot_auto = false;
    cJSON *reboot_item = cJSON_GetObjectItemCaseSensitive(root, "reboot");
    if (reboot_item != NULL) {
        if (cJSON_IsBool(reboot_item)) {
            reboot = cJSON_IsTrue(reboot_item);
        } else if (cJSON_IsString(reboot_item) && strcmp(reboot_item->valuestring, "auto") == 0) {
            reboot_auto = true;
        } else {
            cJSON_Delete(root);
            free(settings);
            return send_json_error(req, "400 Bad Request", "reboot must be true, false or \"auto\"");
        }
    }

    /* Did this request carry a new secret? (read before the JSON tree is freed) */
    bool token_supplied = false;
    bool password_supplied = false;
    {
        const cJSON *tok = cJSON_IsObject(ha) ? cJSON_GetObjectItemCaseSensitive(ha, "access_token") : NULL;
        token_supplied = cJSON_IsString(tok) && tok->valuestring != NULL && tok->valuestring[0] != '\0';
        const cJSON *pw = cJSON_IsObject(mqtt) ? cJSON_GetObjectItemCaseSensitive(mqtt, "password") : NULL;
        password_supplied = cJSON_IsString(pw) && pw->valuestring != NULL && pw->valuestring[0] != '\0';
    }

    cJSON_Delete(root);

    if (invalid_type) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "One or more settings fields have invalid type");
    }
    if (too_long) {
        free(settings);
        return send_json_error(
            req,
            "400 Bad Request",
            "One or more settings values are too long (ssid<=32, wifi_password<=64, country_code<=2, bssid<=17, ws_url<=255, token<=511, ntp<=127, timezone<=127, language<=15, entity_id<=95, mqtt_broker_host<=127, mqtt_username<=63, mqtt_password<=127, mqtt_topic_prefix<=31, mqtt_device_name<=47)");
    }
    if (out_of_range) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "numbers out of range: brightness 1-100 (night 0-100), dim timeout 5-3600 s, saver minutes 1-120, page interval 1-30, saver brightness 1-30, sensitivity 1-10, volume 0-100, mqtt broker_port 1-65535");
    }
    if (strcmp(settings->hardware_saver_mode, "disabled") != 0 &&
        strcmp(settings->hardware_saver_mode, "off") != 0 &&
        strcmp(settings->hardware_saver_mode, "weather") != 0 &&
        strcmp(settings->hardware_saver_mode, "pages") != 0) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "hardware.saver_mode must be disabled, off, weather or pages");
    }
    if (settings->hardware_saver_weather_entity_id[0] != '\0' &&
        strncmp(settings->hardware_saver_weather_entity_id, "weather.", 8) != 0) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "hardware.saver_weather_entity_id must be a weather entity");
    }
    if (!settings_host_valid(settings->ntp_server) || !settings_host_valid(settings->mqtt_broker_host)) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "time.ntp_server and mqtt.broker_host may only contain letters, digits, '.', '-', '_' and ':'");
    }
    if (!settings_timezone_valid(settings->time_tz)) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "time.timezone must be printable ASCII (POSIX TZ string)");
    }
    if (!settings_topic_prefix_valid(settings->mqtt_topic_prefix)) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "mqtt.topic_prefix must not contain spaces, '+', '#', or a leading, trailing or doubled '/'");
    }
    if (!settings_text_valid(settings->device_room) || !settings_text_valid(settings->mqtt_device_name) ||
        !settings_text_valid(settings->mqtt_username)) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "device.room, mqtt.device_name and mqtt.username must not contain control characters");
    }
    if ((settings->topbar_weather_entity_id[0] != '\0' && !entity_id_valid(settings->topbar_weather_entity_id)) ||
        (settings->topbar_stock_entity_id[0] != '\0' && !entity_id_valid(settings->topbar_stock_entity_id)) ||
        (settings->hardware_saver_weather_entity_id[0] != '\0' && !entity_id_valid(settings->hardware_saver_weather_entity_id))) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "entity ids must look like domain.object_id (lowercase letters, digits, underscore)");
    }
    if (!has_ws_scheme(settings->ha_ws_url)) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "ha.ws_url must start with ws:// or wss://");
    }
    /* Anyone who can change the address could point the panel at a host of their own and receive the stored
     * token (or MQTT password) on the next connect. A changed address therefore needs the secret in the same
     * request, which only someone who knows it can send. */
    if (had_ha_token && !token_supplied && strcmp(old_ws_url, settings->ha_ws_url) != 0) {
        free(settings);
        return send_json_error(req, "400 Bad Request",
            "ha.access_token is required in the same request when ha.ws_url changes (the stored token is not sent to a new address)");
    }
    if (had_mqtt_password && !password_supplied && strcmp(old_mqtt_host, settings->mqtt_broker_host) != 0) {
        free(settings);
        return send_json_error(req, "400 Bad Request",
            "mqtt.password is required in the same request when mqtt.broker_host changes (the stored password is not sent to a new broker)");
    }
    if (!normalize_country_code(settings->wifi_country_code, sizeof(settings->wifi_country_code))) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "wifi.country_code must be a 2-letter ISO code (e.g. US, DE)");
    }
    if (!normalize_bssid(settings->wifi_bssid, sizeof(settings->wifi_bssid))) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "wifi.bssid must be empty or MAC format AA:BB:CC:DD:EE:FF");
    }
    if (settings->wifi_ssid[0] == '\0') {
        settings->wifi_password[0] = '\0';
        settings->wifi_bssid[0] = '\0';
    }
    if (settings->ntp_server[0] == '\0') {
        strlcpy(settings->ntp_server, APP_NTP_SERVER, sizeof(settings->ntp_server));
    }
    if (settings->time_tz[0] == '\0') {
        strlcpy(settings->time_tz, APP_TIME_TZ, sizeof(settings->time_tz));
    }
    if (settings->mqtt_topic_prefix[0] == '\0') {
        strlcpy(settings->mqtt_topic_prefix, "betta_ha_panel", sizeof(settings->mqtt_topic_prefix));
    }
    if (settings->mqtt_broker_port <= 0) {
        settings->mqtt_broker_port = 1883;
    }
    if (!normalize_ui_language(settings->ui_language, sizeof(settings->ui_language))) {
        free(settings);
        return send_json_error(req, "400 Bad Request", "ui.language must use [a-z0-9_-] and be 2-15 chars");
    }

    brightness_levels_t levels = {settings->hardware_active_brightness_percent, settings->hardware_dim_brightness_percent,
        settings->hardware_night_brightness_percent};
    brightness_levels_normalize(&levels); /* one scale: night <= dimmed <= active */
    settings->hardware_active_brightness_percent = levels.active;
    settings->hardware_dim_brightness_percent = levels.dimmed;
    settings->hardware_night_brightness_percent = levels.night;

    if (reboot_auto) {
        reboot = settings_need_restart(&settings[1], settings);
        /* After Home Assistant rejected the panel (ha_auth_state.h) it makes no further request until a restart; a token typed in
         * again, even an identical one, is the signal to try once more. */
        if (!reboot && token_supplied && ha_auth_state_blocked()) {
            reboot = true;
        }
    }
    esp_err_t save_err = runtime_settings_save(settings);
    /* Commit the password only after every field has passed validation and the
     * settings save succeeded. A rejected PUT must never change authentication. */
    if (save_err == ESP_OK && change_admin_password) {
        save_err = admin_auth_set_password(admin_password);
    }
    if (save_err == ESP_OK && !reboot) {
        apply_hardware_settings_live(settings);
        update_check_configure(settings->ota_update_check, settings->ota_include_prereleases);
        auto_update_task_configure(settings->ota_auto_install);
        bool saver_source_changed = strcmp(old_saver_mode, settings->hardware_saver_mode) != 0 ||
            strcmp(old_saver_weather_entity_id, settings->hardware_saver_weather_entity_id) != 0;
        bool saver_changed = saver_source_changed ||
            old_saver_timeout != settings->hardware_saver_timeout_minutes ||
            old_saver_weather_minutes != settings->hardware_saver_weather_minutes ||
            old_saver_pages_minutes != settings->hardware_saver_pages_minutes ||
            old_saver_page_interval != settings->hardware_saver_page_interval_minutes ||
            old_saver_weather_brightness != settings->hardware_saver_weather_brightness_percent ||
            (old_dim_brightness != settings->hardware_dim_brightness_percent &&
                (strcmp(old_saver_mode, "pages") == 0 || strcmp(settings->hardware_saver_mode, "pages") == 0)) ||
            old_saver_page_wake_home != settings->hardware_saver_page_wake_home ||
            old_saver_pixel_refresh != settings->hardware_saver_pixel_refresh;
        if (saver_changed) {
            app_event_t protection_event = {.type = EV_DISPLAY_PROTECTION_UPDATED};
            (void)app_events_publish(&protection_event, pdMS_TO_TICKS(20));
        }
        if (saver_source_changed)
            (void)ha_client_notify_layout_updated();
        char ui_sig_after[512];
        ui_settings_signature(settings, ui_sig_after, sizeof(ui_sig_after));
        if (strcmp(ui_sig_before, ui_sig_after) != 0) {
            /* Language, appearance or top bar changed: the UI task re-reads them and rebuilds the pages. */
            app_event_t event = {.type = EV_LAYOUT_UPDATED};
            (void)app_events_publish(&event, pdMS_TO_TICKS(20));
        }
    }
    free(settings);
    if (save_err != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "rebooting", reboot);
    char *payload = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    esp_err_t send_err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);

    if (send_err == ESP_OK && reboot) {
        schedule_restart();
    }
    return send_err;
}
