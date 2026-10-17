/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "app_config.h"

typedef struct {
    char wifi_ssid[APP_WIFI_SSID_MAX_LEN];
    char wifi_password[APP_WIFI_PASSWORD_MAX_LEN];
    char wifi_country_code[APP_WIFI_COUNTRY_CODE_MAX_LEN];
    char wifi_bssid[APP_WIFI_BSSID_MAX_LEN];
    char ha_ws_url[APP_HA_WS_URL_MAX_LEN];
    char ha_access_token[APP_HA_ACCESS_TOKEN_MAX_LEN];
    bool ha_rest_enabled;
    bool mqtt_enabled;
    char mqtt_broker_host[APP_MQTT_BROKER_HOST_MAX_LEN];
    int mqtt_broker_port;
    bool mqtt_tls_enabled;
    char mqtt_username[APP_MQTT_USERNAME_MAX_LEN];
    char mqtt_password[APP_MQTT_PASSWORD_MAX_LEN];
    char mqtt_topic_prefix[APP_MQTT_TOPIC_PREFIX_MAX_LEN];
    /* Home Assistant-visible device name for MQTT discovery (e.g. "Kitchen
     * Panel"). Empty means "use the default" (see mqtt_mgr.c) -- lets
     * multiple panels on the same broker/topic_prefix show up as distinct
     * HA devices instead of all being named identically. */
    char mqtt_device_name[APP_MQTT_DEVICE_NAME_MAX_LEN];
    char device_room[32]; /* room the panel stands in; gives the device id */
    char ntp_server[APP_NTP_SERVER_MAX_LEN];
    char time_tz[APP_TIME_TZ_MAX_LEN];
    char ui_language[APP_UI_LANGUAGE_MAX_LEN];
    char ui_style[12]; /* a supported ID, see ui/theme/ui_style_registry.h */
    int ui_text_step;  /* text size: 0 Standard, 1 Larger, 2 Largest (ui/text_scale.h) */
    char topbar_left_slots[APP_TOPBAR_LEFT_SLOTS][APP_MAX_UI_OPTION_LEN];
    bool topbar_weather_enabled;
    char topbar_weather_entity_id[APP_MAX_ENTITY_ID_LEN];
    char topbar_stock_entity_id[APP_MAX_ENTITY_ID_LEN];
    int hardware_active_brightness_percent;
    int hardware_dim_brightness_percent;
    int hardware_dim_timeout_seconds;
    /* Inactivity protection: disabled, off, weather or page cycling followed by off. */
    char hardware_saver_mode[12];
    int hardware_saver_timeout_minutes;
    int hardware_saver_weather_minutes;
    int hardware_saver_pages_minutes;
    int hardware_saver_page_interval_minutes;
    bool hardware_saver_page_wake_home;
    int hardware_saver_weather_brightness_percent;
    bool hardware_saver_pixel_refresh;
    char hardware_saver_weather_entity_id[APP_MAX_ENTITY_ID_LEN];
    bool sound_enabled;
    int sound_volume;
    bool sound_night_mute;
    bool hardware_night_enabled;   /* backlight off between start and end */
    bool hardware_auto_brightness;   /* ambient light (camera) limits the backlight */
    bool hardware_auto_rotation;     /* Tab5 BMI270; other panels keep fixed orientation */
    int hardware_presence_sensitivity; /* 1 (low) .. 10 (high) */
    bool hardware_night_presence_wake; /* movement may also wake the display at night */
    bool hardware_presence_wake;     /* movement in front of the panel wakes a dimmed display */
    int hardware_night_brightness_percent; /* 0..100, 0 = backlight off */
    int hardware_night_start_min;  /* minutes since midnight */
    int hardware_night_end_min;
    bool ota_update_check;          /* daily check for a newer release on GitHub */
    bool ota_include_prereleases;   /* the check also offers pre-releases (release candidates) */
    bool ota_auto_install;          /* install a newer release by itself at night (net/auto_update_task.c); off by default */
} runtime_settings_t;

void runtime_settings_set_defaults(runtime_settings_t *out);
/* "HH:MM" <-> minutes since midnight. */
bool runtime_settings_parse_hhmm(const char *text, int *out_minutes);
void runtime_settings_format_hhmm(int minutes, char *out, size_t out_size);
esp_err_t runtime_settings_init(void);
esp_err_t runtime_settings_load(runtime_settings_t *out);
esp_err_t runtime_settings_save(const runtime_settings_t *settings);
bool runtime_settings_has_wifi(const runtime_settings_t *settings);
bool runtime_settings_has_ha(const runtime_settings_t *settings);
bool runtime_settings_has_mqtt(const runtime_settings_t *settings);
