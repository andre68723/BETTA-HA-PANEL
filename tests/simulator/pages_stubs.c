/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * What the real top bar and navigation (main/ui/ui_pages.c) ask of the network and settings layers, answered
 * with a connected, healthy panel so the simulator's frame is the firmware's own.
 */
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "net/mqtt_mgr.h"
#include "net/update_check.h"
#include "net/wifi_mgr.h"
#include "settings/runtime_settings.h"

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t desc = {.version = "v0.9.0", .project_name = "betta-widget-sim",
                                        .date = "", .time = ""};
    return &desc;
}

/* The fixture frame's "mqtt": true shows the MQTT chip and fills its popup (simulator_frame.c). */
bool g_sim_mqtt = false;
bool mqtt_mgr_is_started(void) { return g_sim_mqtt; }
bool mqtt_mgr_is_connected(void) { return g_sim_mqtt; }
const char *mqtt_mgr_device_id(void) { return g_sim_mqtt ? "betta-panel-3f2a" : ""; }
bool update_check_available_version(char *out, size_t out_len) { if (out_len > 0) out[0] = '\0'; return false; }

bool wifi_mgr_is_connected(void) { return true; }
bool wifi_mgr_is_setup_ap_active(void) { return false; }
const char *wifi_mgr_get_setup_ap_ssid(void) { return ""; }
const char *wifi_mgr_get_setup_ap_password(void) { return ""; }
esp_err_t wifi_mgr_get_sta_ip(char *out, size_t out_len) { snprintf(out, out_len, "192.0.2.10"); return ESP_OK; }
esp_err_t wifi_mgr_get_ap_ip(char *out, size_t out_len) { snprintf(out, out_len, "192.168.4.1"); return ESP_OK; }
esp_err_t wifi_mgr_get_sta_ap_info(wifi_mgr_sta_ap_info_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->ssid, sizeof(out->ssid), "Demo home");
    out->rssi = -68;
    out->channel = 6;
    return ESP_OK;
}
esp_err_t wifi_mgr_try_get_sta_rssi(int8_t *out) { *out = -55; return ESP_OK; }
void wifi_mgr_get_diag(wifi_mgr_diag_t *out) { memset(out, 0, sizeof(*out)); }
const char *wifi_mgr_reason_to_str(uint8_t reason) { (void)reason; return ""; }

esp_err_t runtime_settings_load(runtime_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    /* Public demo hosts for the connectivity popups. */
    snprintf(out->ha_ws_url, sizeof(out->ha_ws_url), "ws://homeassistant.local:8123/api/websocket");
    out->mqtt_enabled = g_sim_mqtt;
    if (g_sim_mqtt) {
        snprintf(out->mqtt_broker_host, sizeof(out->mqtt_broker_host), "mqtt.local");
        out->mqtt_broker_port = 1883;
    }
    return ESP_OK;
}
