/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "sdkconfig.h"

/* ---- Panel-variant-dependent geometry & identity ----------------------
 * Selected via CONFIG_APP_PANEL_VARIANT_* (see main/Kconfig.projbuild).
 * Defaults fall back to the 4" 720x720 baseline so an older sdkconfig
 * (pre-variant) still builds the smart86 target unchanged. */
#if defined(CONFIG_APP_PANEL_VARIANT_10INCH_1280)
#  define APP_NAME               "betta-ha-panel-10.1"
#  define APP_BOARD_DESCRIPTION  "Waveshare ESP32-P4 Module Nano + 10.1\" DSI panel"
#  define APP_SCREEN_WIDTH       1280
#  define APP_SCREEN_HEIGHT      800
#  define APP_CONTENT_BOX_WIDTH  1280
#  define APP_CONTENT_BOX_HEIGHT 680
#elif defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
/* M5Stack Tab5: 5" 1280x720 landscape (build-checked, not yet run on hardware). */
#  define APP_NAME               "betta-ha-panel-tab5"
#  define APP_BOARD_DESCRIPTION  "M5Stack Tab5"
#  define APP_SCREEN_WIDTH       1280
#  define APP_SCREEN_HEIGHT      720
#  define APP_CONTENT_BOX_WIDTH  1280
#  define APP_CONTENT_BOX_HEIGHT 600
#elif defined(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
/* Guition JC8012P4A1: native portrait 800×1280, LVGL rotation 90° → landscape */
#  define APP_NAME               "betta-ha-panel-jc"
#  if CONFIG_APP_PANELJC_PANEL_V2
#    define APP_BOARD_DESCRIPTION  "Guition JC8012P4A1 (\"V2\" panel, rear case date code ~2628+)"
#  else
#    define APP_BOARD_DESCRIPTION  "Guition JC8012P4A1 (\"V1\"/original panel, rear case date code up to ~2627)"
#  endif
#  define APP_SCREEN_WIDTH       1280
#  define APP_SCREEN_HEIGHT      800
#  define APP_CONTENT_BOX_WIDTH  1280
#  define APP_CONTENT_BOX_HEIGHT 680
#else /* CONFIG_APP_PANEL_VARIANT_4INCH_720 (default) */
#  define APP_NAME               "betta-ha-panel"
#  define APP_BOARD_DESCRIPTION  "Waveshare ESP32-P4-WIFI6-Touch-LCD-4B"
#  define APP_SCREEN_WIDTH       720
#  define APP_SCREEN_HEIGHT      720
#  define APP_CONTENT_BOX_WIDTH  720
#  define APP_CONTENT_BOX_HEIGHT 600
#endif
#define APP_CONTENT_BOX_X 0
#define APP_CONTENT_BOX_Y 60
#define APP_NAV_BUTTON_COUNT 5
#define APP_LVGL_ANTIALIASING 0
#define APP_UI_TEST_WEATHER_ICON_OVERLAY 0

#define APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT 100
#define APP_DISPLAY_DIM_BRIGHTNESS_PERCENT 50
#define APP_DISPLAY_DIM_TIMEOUT_MS (5 * 60 * 1000)
#define APP_DISPLAY_NIGHT_BRIGHTNESS_PERCENT 1

#define APP_EVENT_QUEUE_LENGTH 96
#define APP_EVENT_QUEUE_WAIT_MS 50

#define APP_LAYOUT_PATH "/littlefs/layout.json"
/* Largest stored layout (the buffer and the parsed tree live in PSRAM). 32 KB holds the 224-widget total with real titles
 * (about 140 bytes per widget). Build time is no longer the limit: 224 widgets take 3.6 s on the paneljc. */
#define APP_LAYOUT_MAX_JSON_LEN 32768
#define APP_LAYOUT_MAX_ERRORS 16

#define APP_SETTINGS_PATH "/littlefs/settings.json"
#define APP_SETTINGS_MAX_JSON_LEN 4096

#define APP_WIFI_SSID_MAX_LEN 33
#define APP_WIFI_PASSWORD_MAX_LEN 65
#define APP_WIFI_COUNTRY_CODE_MAX_LEN 3
#define APP_WIFI_BSSID_MAX_LEN 18
#define APP_HA_WS_URL_MAX_LEN 256
#define APP_HA_ACCESS_TOKEN_MAX_LEN 512
#define APP_MQTT_BROKER_HOST_MAX_LEN 128
#define APP_MQTT_USERNAME_MAX_LEN 64
#define APP_MQTT_PASSWORD_MAX_LEN 128
#define APP_MQTT_TOPIC_PREFIX_MAX_LEN 32
#define APP_MQTT_DEVICE_NAME_MAX_LEN 48
#define APP_NTP_SERVER_MAX_LEN 128
#define APP_TIME_TZ_MAX_LEN 128
#define APP_UI_LANGUAGE_MAX_LEN 16

#define APP_UI_DEFAULT_LANGUAGE "en"

#define APP_I18N_DIR "/littlefs/i18n"
#define APP_I18N_MAX_JSON_LEN 32768

#define APP_THEME_DIR "/littlefs/themes"
#define APP_THEME_ACTIVE_PATH "/littlefs/themes/active.id"

/* The 10-inch paneljc has an ES8311 chime path, but no speaker was found on the owner's unit (2026-10-04, silent at every
 * volume) and the rear box is closed. While 0, audio_out_available() is false there and the editor hides the Sound
 * settings. Set to 1 once a speaker is confirmed. */
#define APP_PANELJC_AUDIO 0

/* Items the top bar can show left of the clock (date, weather, stock quote). A narrow screen shows fewer (ui_pages.c). */
#define APP_TOPBAR_LEFT_SLOTS 3

#define APP_SETUP_AP_SSID_PREFIX "BETTA-Setup"
/* The setup AP is WPA2 with a random passphrase per start ("maple-river-42", net/setup_passphrase.c),
 * shown on the display (wifi_mgr.c). */
#define APP_SETUP_AP_PASSWORD_LEN 19
#define APP_SETUP_AP_CHANNEL 1
#define APP_SETUP_AP_MAX_CONNECTIONS 4

/* Page limit: a tab for every page must stay hit-able and readable. Up to a few pages the tabs show icon and title; beyond that
 * the bar turns compact (ui/nav_layout.c): the shown page keeps its title, the others show an icon or a number. 8 tabs fit the
 * 720 px bar at 64 px each, 12 the 1280 px bar comfortably; the limit is nav_layout_max_pages() (host-tested), not memory. */
#if APP_SCREEN_WIDTH >= 1000
#  define APP_MAX_PAGES 12
#else
#  define APP_MAX_PAGES 8
#endif
#define APP_MAX_WIDGETS_PER_PAGE 32
/* The total stays at 224 widgets (7 pages of 32): this was set when 224 widgets needed a minute to build; they take 3.6 s now, so the total is a policy cap, not a measured limit. */
#define APP_MAX_WIDGETS_TOTAL ((APP_MAX_PAGES * APP_MAX_WIDGETS_PER_PAGE) > 224 ? 224 : (APP_MAX_PAGES * APP_MAX_WIDGETS_PER_PAGE))

#define APP_MAX_ENTITY_ID_LEN 96
#define APP_MAX_WIDGET_ID_LEN 32
#define APP_MAX_PAGE_ID_LEN 32
#define APP_MAX_STATE_LEN 64
#define APP_MAX_NAME_LEN 64
#define APP_MAX_UNIT_LEN 24
#define APP_MAX_ICON_LEN 64
#define APP_MAX_UI_OPTION_LEN 24
#define APP_MAX_COLOR_STR_LEN 16

/* Notification overlay (POST /api/notifications) */
#define APP_NOTIFICATION_MAX_JSON_LEN 2048
#define APP_NOTIFICATION_MAX_TITLE_LEN 64
#define APP_NOTIFICATION_MAX_MESSAGE_LEN 768
#define APP_NOTIFICATION_MAX_ID_LEN 64
#define APP_NOTIFICATION_MAX_ACTION_LABEL_LEN 24
#define APP_NOTIFICATION_MAX_TIMEOUT_SEC 3600

/* All ESP32-P4 variants (10" and 4") keep the cache in PSRAM (about 8 MB for
 * 4096 entities and states, of 24+ MB free); raise the ceiling for large HA installs. */
#define APP_HA_MAX_ENTITIES 4096
#define APP_HA_MAX_STATES 4096
/* Must fit weather entity attributes incl. the compact forecast array plus temperature/humidity/units.
 * The P4 boards have plenty of PSRAM: leave room for the weather detail attributes (pressure, wind,
 * visibility) next to the forecast. */
#define APP_HA_ATTRS_MAX_LEN 1280
#define APP_HA_WEATHER_DETAILS (APP_HA_ATTRS_MAX_LEN >= 1280)
#define APP_HA_LIGHT_DISCOVERY_MAX_ITEMS 640 /* room cards list every entity of every area */
#define APP_HA_LIGHT_DISCOVERY_MAX_AREAS 96
#define APP_HA_LIGHT_DISCOVERY_MAX_DEVICES 256
#define APP_HA_DISCOVERY_ID_MAX_LEN 48
#define APP_HA_DISCOVERY_DOMAIN_MAX_LEN 24 /* "alarm_control_panel" is 19 characters */
#define APP_HA_DISCOVERY_SEARCH_MAX_LEN 64
/* Disable only the raw HA registry WS discovery by default; the light picker stays enabled via template pages. */
#define APP_HA_LIGHT_DISCOVERY_REGISTRY_ENABLED 0
/* Default discovery path: HA renders compact light pages server-side; the panel fetches them one by one. */
#define APP_HA_LIGHT_DISCOVERY_TEMPLATE_ENABLED 1
#define APP_HA_LIGHT_DISCOVERY_PAGE_SIZE 40 /* about 6 KB of JSON, inside the 8 KB fallback payload cap of ha_client.c */
/* Internal heap an entity-discovery template page waits for before it is sent at once (the
 * values are in ha_client.c). Below it the request still runs after an 8 s wait with 64 KB free and
 * a 24 KB block. The JC panel used to idle at ~100 KB free with a largest internal block of exactly 32 KB,
 * so it needed lower values (88 KB / 28 KB); with LVGL in PSRAM (2026-10-01) it idles at ~320 KB / ~160 KB and
 * the generic values are reached at once again. A page's TLS and receive buffers are in PSRAM and take at most
 * ~30 KB internal (measured). */
#define APP_HA_DISCOVERY_TEMPLATE_MIN_INTERNAL_FREE_BYTES (120U * 1024U)
#define APP_HA_DISCOVERY_TEMPLATE_MIN_INTERNAL_LARGEST_BYTES (48U * 1024U)

#define APP_HA_QUEUE_LENGTH 96
#define APP_HA_TASK_STACK 12288
#define APP_HA_TASK_PRIO 8
#define APP_HA_COVER_TASK_STACK 8192
#define APP_HA_COVER_TASK_PRIO 3

/* Keep pre-language behavior for light commands (explicit no-fade). */
#define APP_HA_LIGHT_USE_TRANSITION_ZERO 1

/* Optional A/B: use light.toggle for panel power actions (with desired-state guard). */
#define APP_HA_LIGHT_POWER_USE_TOGGLE 0

/* Keep pre-language WS subscription path enabled. */
#define APP_HA_USE_WS_ENTITIES_SUBSCRIPTION 1

/* Route tracing for state flow: WS -> panel model -> UI (verbose, keep off by default). */
#define APP_HA_ROUTE_TRACE_LOG 0

#define APP_UI_TASK_STACK 16384 /* used 8.6 KB at most (2026-09-30, paneljc) */
#define APP_UI_TASK_PRIO 4

#define APP_LVGL_TASK_STACK 20480 /* used 10.3 KB at most; popups and glass drawing leave room */

#define APP_HTTP_PORT 80
#define APP_HTTP_TASK_STACK 14336 /* one unidentified editor request once left only 1.4 KB of 12 KB (paneljc, 2026-09-30) */

#define APP_OTA_URL_MAX_LEN 512
#define APP_OTA_JSON_MAX_LEN 768
#define APP_OTA_CHUNK_SIZE 4096
#define APP_OTA_URL_TOTAL_TIMEOUT_MS (15 * 60 * 1000) /* below the automatic-update observer's 20 minutes */
#define APP_OTA_TASK_STACK 8192
#define APP_OTA_TASK_PRIO 5
/* GitHub repository whose releases the daily update check reads (net/update_check.c). */
#define APP_OTA_RELEASE_REPO "andre68723/BETTA-HA-PANEL"

#ifndef APP_HAVE_HOSTED_C6_FW_IMAGE
#define APP_HAVE_HOSTED_C6_FW_IMAGE 0
#endif

#ifdef CONFIG_APP_WIFI_SSID
#define APP_WIFI_SSID CONFIG_APP_WIFI_SSID
#else
#define APP_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifdef CONFIG_APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD CONFIG_APP_WIFI_PASSWORD
#else
#define APP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifdef CONFIG_APP_WIFI_COUNTRY_CODE
#define APP_WIFI_COUNTRY_CODE CONFIG_APP_WIFI_COUNTRY_CODE
#else
#define APP_WIFI_COUNTRY_CODE "DE"
#endif

#ifdef CONFIG_APP_WIFI_WAIT_FOR_IP
#define APP_WIFI_WAIT_FOR_IP CONFIG_APP_WIFI_WAIT_FOR_IP
#else
#define APP_WIFI_WAIT_FOR_IP 1
#endif

#ifdef CONFIG_APP_WIFI_CONNECT_TIMEOUT_MS
#define APP_WIFI_CONNECT_TIMEOUT_MS CONFIG_APP_WIFI_CONNECT_TIMEOUT_MS
#else
#define APP_WIFI_CONNECT_TIMEOUT_MS 15000
#endif

#ifdef CONFIG_APP_WIFI_MAX_RETRIES
#define APP_WIFI_MAX_RETRIES CONFIG_APP_WIFI_MAX_RETRIES
#else
#define APP_WIFI_MAX_RETRIES 8
#endif

#ifdef CONFIG_APP_WIFI_DISABLE_POWER_SAVE
#define APP_WIFI_DISABLE_POWER_SAVE CONFIG_APP_WIFI_DISABLE_POWER_SAVE
#else
#define APP_WIFI_DISABLE_POWER_SAVE 1
#endif

#ifdef CONFIG_APP_HOSTED_AUTO_UPDATE_C6_FW
#define APP_HOSTED_AUTO_UPDATE_C6_FW CONFIG_APP_HOSTED_AUTO_UPDATE_C6_FW
#else
#define APP_HOSTED_AUTO_UPDATE_C6_FW 1
#endif

/* Expert override: allow bundled C6 FW version different from host ESP-Hosted stack version. */
#define APP_HOSTED_ALLOW_BUNDLED_C6_VERSION_MISMATCH 0

#ifdef CONFIG_APP_HA_WS_URL
#define APP_HA_WS_URL CONFIG_APP_HA_WS_URL
#else
#define APP_HA_WS_URL "YOUR_HA_WS_URL"
#endif

#ifdef CONFIG_APP_HA_ACCESS_TOKEN
#define APP_HA_ACCESS_TOKEN CONFIG_APP_HA_ACCESS_TOKEN
#else
#define APP_HA_ACCESS_TOKEN "YOUR_HA_ACCESS_TOKEN"
#endif

#ifdef CONFIG_APP_HA_FETCH_INITIAL_STATES
#define APP_HA_FETCH_INITIAL_STATES CONFIG_APP_HA_FETCH_INITIAL_STATES
#else
#define APP_HA_FETCH_INITIAL_STATES 1
#endif

#ifdef CONFIG_APP_HA_SUBSCRIBE_STATE_CHANGED
#define APP_HA_SUBSCRIBE_STATE_CHANGED CONFIG_APP_HA_SUBSCRIBE_STATE_CHANGED
#else
#define APP_HA_SUBSCRIBE_STATE_CHANGED 1
#endif

#ifdef CONFIG_APP_HA_PING_INTERVAL_MS
#define APP_HA_PING_INTERVAL_MS CONFIG_APP_HA_PING_INTERVAL_MS
#else
#define APP_HA_PING_INTERVAL_MS 8000
#endif

#ifdef CONFIG_APP_NTP_SERVER
#define APP_NTP_SERVER CONFIG_APP_NTP_SERVER
#else
#define APP_NTP_SERVER "pool.ntp.org"
#endif

#ifdef CONFIG_APP_TIME_TZ
#define APP_TIME_TZ CONFIG_APP_TIME_TZ
#else
#define APP_TIME_TZ "CET-1CEST,M3.5.0/2,M10.5.0/3"
#endif
