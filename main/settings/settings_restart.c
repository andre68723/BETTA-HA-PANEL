/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/settings_restart.h"

#include <string.h>

#define SAME_TEXT(field) (strcmp(before->field, after->field) == 0)
#define SAME(field) (before->field == after->field)

bool settings_need_restart(const runtime_settings_t *before, const runtime_settings_t *after)
{
    if (before == NULL || after == NULL) {
        return true;
    }
    const bool wifi = SAME_TEXT(wifi_ssid) && SAME_TEXT(wifi_password) && SAME_TEXT(wifi_country_code) &&
                      SAME_TEXT(wifi_bssid);
    const bool ha = SAME_TEXT(ha_ws_url) && SAME_TEXT(ha_access_token) && SAME(ha_rest_enabled);
    const bool mqtt = SAME(mqtt_enabled) && SAME_TEXT(mqtt_broker_host) && SAME(mqtt_broker_port) &&
                      SAME(mqtt_tls_enabled) && SAME_TEXT(mqtt_username) && SAME_TEXT(mqtt_password) &&
                      SAME_TEXT(mqtt_topic_prefix) && SAME_TEXT(mqtt_device_name);
    /* The room gives the device id, which MQTT discovery announces at start. */
    const bool identity = SAME_TEXT(device_room) && SAME_TEXT(ntp_server);
    return !(wifi && ha && mqtt && identity);
}
