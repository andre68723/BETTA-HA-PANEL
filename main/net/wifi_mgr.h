/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "app_config.h"

typedef struct {
    const char *ssid;
    const char *password;
    const char *country_code;
    uint8_t channel;
    uint8_t max_connection;
} wifi_mgr_ap_config_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX_LEN];
    int8_t rssi;
    uint8_t authmode;
    uint8_t channel;
    uint8_t bssid[6];
    bool connected;
} wifi_mgr_scan_result_t;

typedef struct {
    const char *ssid;
    const char *password;
    const char *country_code;
    const char *bssid;
    bool wait_for_ip;
    int connect_timeout_ms;
    int max_retries;
} wifi_mgr_config_t;

typedef struct {
    char ssid[APP_WIFI_SSID_MAX_LEN];
    int8_t rssi;
    uint8_t authmode;
    uint8_t channel;
    uint8_t bssid[6];
} wifi_mgr_sta_ap_info_t;

/* Connectivity diagnostics: not needed for normal operation, only to answer "what has the
 * automatic recovery in wifi_mgr.c actually tried" when the panel is unreachable and the only way
 * left to look is the screen itself (the Wi-Fi/HA popup) or, after a forced reset, the persisted
 * copy wifi_diag_store.c keeps (GET /api/version). All *_uptime_ms fields are esp_timer uptime
 * (ms since boot), 0 meaning "never happened this boot"; last_hard_recover_err is an esp_err_t. */
typedef struct {
    uint8_t last_disconnect_reason;
    int64_t last_disconnect_uptime_ms;
    uint32_t disconnect_count;
    uint32_t reconnect_attempts;
    uint32_t hard_recover_count;
    int32_t last_hard_recover_err;
    int64_t last_hard_recover_uptime_ms;
} wifi_mgr_diag_t;

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *cfg);
bool wifi_mgr_is_connected(void);
esp_err_t wifi_mgr_force_reconnect(void);
esp_err_t wifi_mgr_force_transport_recover(void);
esp_err_t wifi_mgr_start_setup_ap(const wifi_mgr_ap_config_t *cfg);
esp_err_t wifi_mgr_stop_setup_ap(void);
bool wifi_mgr_is_setup_ap_active(void);
const char *wifi_mgr_get_setup_ap_ssid(void);
/* The setup AP's WPA2 password (random per start unless the config set one), "" when not active. */
const char *wifi_mgr_get_setup_ap_password(void);
esp_err_t wifi_mgr_get_sta_ip(char *out, size_t out_len);
/* The station's default gateway as an lwIP IPv4 address (network byte order); false without a lease. */
bool wifi_mgr_get_sta_gateway(uint32_t *out_addr);
/* After a firmware update whose esp_hosted version needs a different C6 image, the C6 update waits until the new image is marked valid (it ends with
 * a restart, which would otherwise roll the new image back). util/ota_validate.c calls this right after marking it valid. */
void wifi_mgr_run_deferred_c6_update(void);
/* Version the C6 reported over ESP-Hosted in this boot ("3.0.10"); false until the query succeeded or without a co-processor.
 * The image embedded in the build is APP_HOSTED_C6_EXPECTED_VERSION, which says nothing about what the chip runs. */
bool wifi_mgr_get_c6_version(char *out, size_t out_len);
esp_err_t wifi_mgr_get_ap_ip(char *out, size_t out_len);
esp_err_t wifi_mgr_get_sta_ap_info(wifi_mgr_sta_ap_info_t *out_info);
/* The connected AP's RSSI without waiting for the Wi-Fi API: ESP_ERR_TIMEOUT when it is busy, so a
 * UI task can poll it and simply try again later. */
esp_err_t wifi_mgr_try_get_sta_rssi(int8_t *out_rssi_dbm);
esp_err_t wifi_mgr_get_sta_rssi(int8_t *out_rssi_dbm);
esp_err_t wifi_mgr_scan(wifi_mgr_scan_result_t *results, size_t max_results, size_t *out_count);
void wifi_mgr_get_diag(wifi_mgr_diag_t *out);
/* Short technical code for a Wi-Fi disconnect reason (e.g. "AUTH_EXPIRE"), the same text logged by
 * wifi_mgr.c itself -- not translated, meant for a diagnostics display, not the general UI. */
const char *wifi_mgr_reason_to_str(uint8_t reason);
