/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* MQTT broker connection config, sourced from runtime_settings_t's mqtt_*
 * fields. All pointer fields must remain valid for the duration of the
 * mqtt_mgr_start() call (they are copied/duplicated internally as needed;
 * see mqtt_mgr.c). */
typedef struct {
    const char *broker_host;
    int broker_port;
    bool tls_enabled;
    const char *username; /* may be NULL/empty for anonymous auth */
    const char *password; /* may be NULL/empty for anonymous auth */
    const char *topic_prefix;
    const char *device_name; /* HA-visible device name; NULL/empty falls back to "BETTA HA Panel" */
} mqtt_mgr_config_t;

/* Starts the MQTT client and connects (asynchronously) to the configured
 * broker. Sets up a retained "offline" last-will on
 * "<topic_prefix>/<device_id>/status" (qos=1, retain=true) and, once
 * MQTT_EVENT_CONNECTED fires: publishes a retained "online" availability
 * message, publishes retained HA discovery configs for a `notify` entity
 * and an "Online" connectivity `binary_sensor` under
 * "homeassistant/{notify,binary_sensor}/<device_id>/config" plus an
 * "IP address" diagnostic `sensor` under
 * "homeassistant/sensor/<device_id>/ip/config" (state retained on
 * "<topic_prefix>/<device_id>/ip"; the device block carries the MAC and a
 * configuration_url so HA shows a "Visit device" link), and
 * subscribes (qos=1) to "<topic_prefix>/<device_id>/notify/set" -- JSON
 * payloads on that topic (same shape as POST /api/notifications) are
 * parsed via notification_parse_json() and surfaced as EV_NOTIFICATION_SHOW.
 * Safe to call at most once without an intervening mqtt_mgr_stop();
 * returns ESP_OK if already started. */
esp_err_t mqtt_mgr_start(const mqtt_mgr_config_t *cfg);

/* Stops and destroys the MQTT client, if running. Safe to call even if
 * mqtt_mgr_start() was never called or already failed. */
void mqtt_mgr_stop(void);

/* True once MQTT_EVENT_CONNECTED has fired and no MQTT_EVENT_DISCONNECTED
 * has fired since. */
bool mqtt_mgr_is_connected(void);

/* The command topic ("<topic_prefix>/<device_id>/notify/set") this device
 * subscribes to for notifications, for display in the web UI so users can
 * target it directly with mqtt.publish (HA's MQTT notify platform only
 * forwards the plain message text through notify.send_message -- title,
 * message_id and timeout_sec need a raw JSON payload published straight to
 * this topic instead). Returns an empty string if mqtt_mgr_start() has not
 * been called yet (e.g. MQTT disabled). The returned pointer is valid for
 * the process lifetime. */
const char *mqtt_mgr_get_command_topic(void);

/* True once mqtt_mgr_start() has created a client (MQTT enabled with a
 * broker host); false when MQTT is disabled or the client was stopped. */
bool mqtt_mgr_is_started(void);

/* The "panel-XXYY" device id used for topics and HA discovery; empty until
 * mqtt_mgr_start() has run. Valid for the process lifetime. */
const char *mqtt_mgr_device_id(void);
