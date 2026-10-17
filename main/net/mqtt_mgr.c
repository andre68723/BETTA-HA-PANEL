/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/mqtt_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "app_config.h"
#include "app_events.h"
#include "net/wifi_mgr.h"
#include "settings/device_identity.h"
#include "util/log_tags.h"
#include "util/notification_parse.h"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static volatile bool s_connected = false;
static char s_status_topic[96] = {0};
static char s_device_id[32] = {0};
static char s_notify_config_topic[96] = {0};
static char s_online_config_topic[96] = {0};
static char s_command_topic[96] = {0};
static char s_ip_config_topic[96] = {0};
static char s_ip_state_topic[96] = {0};
/* Station MAC as "aa:bb:cc:dd:ee:ff" for the HA device "connections" list;
 * empty if the MAC could not be read. Filled by mqtt_mgr_get_device_id(). */
static char s_mac_str[18] = {0};
static char s_device_name[APP_MQTT_DEVICE_NAME_MAX_LEN] = {0};

/* Accumulation state for an in-progress MQTT_EVENT_DATA message on the
 * command topic. esp-mqtt can split one publish into multiple
 * MQTT_EVENT_DATA callbacks when it is longer than the client's internal
 * buffer (see mqtt_client.h's esp_mqtt_event_t doc comment); only the
 * first chunk carries a non-zero topic_len, so `s_cmd_buf != NULL` (or
 * `s_cmd_drop`) tracks "we're mid-message" across chunks that arrive with
 * topic_len == 0.
 *
 * A single static accumulation buffer is safe here because this device
 * subscribes to exactly one topic (s_command_topic) on this client: the
 * MQTT connection is one TCP stream serviced by esp-mqtt's single
 * internal task, so publishes for a given client are received and
 * dispatched strictly in order -- there is no possibility of two
 * messages interleaving their MQTT_EVENT_DATA chunks. If a second
 * subscription is ever added to this client, this assumption must be
 * revisited (e.g. keyed accumulation per topic). */
static char *s_cmd_buf = NULL;
static int s_cmd_total_len = 0; /* total_data_len of the in-flight message; 0 == none in flight */
static bool s_cmd_drop = false; /* true if the in-flight message is being ignored (bad topic or too large) */

static void mqtt_mgr_get_device_id(char *out, size_t out_len)
{
    /* On ESP32-P4 + esp_hosted, WiFi runs entirely on the remote C6
     * coprocessor: this chip's local eFuse MAC table has no WiFi STA/SoftAP
     * entry at all, so esp_read_mac(..., ESP_MAC_WIFI_*) fails here with
     * "mac type is incorrect (not found)" regardless of which WiFi mac_type
     * is requested. esp_wifi_get_mac() goes through esp_wifi_remote's RPC
     * to the coprocessor's real radio MAC instead, and works identically on
     * variants with native WiFi. mqtt_mgr_start() always runs after Wi-Fi
     * has already connected (see app_main.c ordering), so the STA MAC is
     * guaranteed available here; esp_read_mac() is kept as a fallback only
     * for robustness against a future call-order change. */
    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) {
        err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    if (err == ESP_OK) {
        snprintf(out, out_len, "panel-%02X%02X", mac[4], mac[5]);
        snprintf(s_mac_str, sizeof(s_mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "panel-0000");
        s_mac_str[0] = '\0';
    }
}

/* Current station IPv4 address as text, or false if Wi-Fi has no address
 * (should not happen while MQTT is connected, but the getter can fail). */
static bool mqtt_mgr_get_sta_ip(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    return wifi_mgr_get_sta_ip(out, out_len) == ESP_OK && out[0] != '\0';
}

/* Builds the shared "device" block used by both HA discovery configs, so
 * both entities (notify + the online binary_sensor) get grouped under a
 * single device in Home Assistant's UI. Caller owns the returned object
 * (attach it to a parent with cJSON_AddItemToObject(), or cJSON_Delete()
 * it directly if unused). Returns NULL on allocation failure -- callers
 * must check before dereferencing. */
static cJSON *build_device_block(void)
{
    cJSON *device = cJSON_CreateObject();
    if (device == NULL) {
        return NULL;
    }

    cJSON *identifiers = cJSON_CreateArray();
    if (identifiers == NULL) {
        cJSON_Delete(device);
        return NULL;
    }
    cJSON_AddItemToArray(identifiers, cJSON_CreateString(s_device_id));
    cJSON_AddItemToObject(device, "identifiers", identifiers);

    cJSON_AddStringToObject(device, "name", s_device_name[0] != '\0' ? s_device_name : "BETTA HA Panel");
    cJSON_AddStringToObject(device, "manufacturer", "Cpt_Kirk");
    /* APP_BOARD_DESCRIPTION may itself contain literal double-quote
     * characters (see the paneljc "V1"/"V2" variant strings in
     * app_config.h) -- cJSON_AddStringToObject()/cJSON_Print() escape
     * those automatically when serializing, so this is safe even for
     * those variants. Never hand-build this JSON via snprintf. */
    cJSON_AddStringToObject(device, "model", APP_BOARD_DESCRIPTION);

    const esp_app_desc_t *desc = esp_app_get_description();
    const char *sw_version = (desc != NULL && desc->version[0] != '\0') ? desc->version : "unknown";
    cJSON_AddStringToObject(device, "sw_version", sw_version);

    /* "connections" lets HA show the MAC on the device page and match the
     * device against other integrations that key on it. */
    if (s_mac_str[0] != '\0') {
        cJSON *connections = cJSON_CreateArray();
        cJSON *conn = cJSON_CreateArray();
        if (connections != NULL && conn != NULL) {
            cJSON_AddItemToArray(conn, cJSON_CreateString("mac"));
            cJSON_AddItemToArray(conn, cJSON_CreateString(s_mac_str));
            cJSON_AddItemToArray(connections, conn);
            cJSON_AddItemToObject(device, "connections", connections);
        } else {
            cJSON_Delete(connections);
            cJSON_Delete(conn);
        }
    }

    /* "configuration_url" makes HA render a "Visit device" link on the
     * device page, pointing at the panel's web editor. Discovery configs
     * are republished on every MQTT connect, so a DHCP address change is
     * picked up on the next reconnect. */
    char ip[32];
    if (mqtt_mgr_get_sta_ip(ip, sizeof(ip))) {
        char url[48];
        snprintf(url, sizeof(url), "http://%s/", ip);
        cJSON_AddStringToObject(device, "configuration_url", url);
    }

    return device;
}

/* Publishes a retained, QoS-1 HA discovery config to `topic`. Frees `root`
 * regardless of outcome. Logs and returns on any failure (OOM building the
 * JSON string, or the underlying publish call failing) -- discovery is
 * best-effort and must never crash or block the rest of connect handling. */
static void publish_discovery_config(const char *topic, cJSON *root)
{
    if (root == NULL) {
        ESP_LOGW(TAG_MQTT, "Discovery payload build failed (OOM) for topic %s", topic);
        return;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        ESP_LOGW(TAG_MQTT, "Discovery payload serialize failed (OOM) for topic %s", topic);
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, true);
    if (msg_id < 0) {
        ESP_LOGW(TAG_MQTT, "Failed to publish discovery config to %s", topic);
    }
    cJSON_free(payload);
}

/* Publishes both HA discovery configs (notify + connectivity binary_sensor).
 * Called once per MQTT_EVENT_CONNECTED, after the retained "online"
 * availability publish. */
static void publish_ha_discovery(void)
{
    if (s_mqtt_client == NULL) {
        return;
    }

    /* notify.mqtt discovery config. command_template is itself a JSON
     * document (a Jinja2 template HA renders into the actual command
     * payload); it is placed into the discovery JSON as a plain string
     * value via cJSON_AddStringToObject(), which escapes the embedded
     * quotes/braces correctly when the outer object is serialized -- do
     * not pre-escape it. */
    cJSON *notify_root = cJSON_CreateObject();
    if (notify_root != NULL) {
        cJSON_AddStringToObject(notify_root, "name", "Notify");
        char unique_id[48];
        snprintf(unique_id, sizeof(unique_id), "%s-notify", s_device_id);
        cJSON_AddStringToObject(notify_root, "unique_id", unique_id);
        cJSON_AddStringToObject(notify_root, "command_topic", s_command_topic);
        /* HA's MQTT notify platform (homeassistant/components/mqtt/notify.py)
         * renders command_template with ONLY the message text, bound to the
         * Jinja variable "value" (MqttCommandTemplate always uses that name,
         * not "message") -- title is accepted by notify.send_message but is
         * never forwarded into the template at all, and there is no "data"
         * variable either. So title/message_id/timeout_sec cannot reach the
         * device through this notify entity's command_topic; only the plain
         * message text can. Automations that need those fields must publish
         * directly to the command topic with mqtt.publish instead (see the
         * web UI's MQTT settings card for that example). */
        cJSON_AddStringToObject(notify_root, "command_template", "{\"message\": \"{{ value }}\"}");
        cJSON_AddStringToObject(notify_root, "availability_topic", s_status_topic);
        cJSON_AddStringToObject(notify_root, "payload_available", "online");
        cJSON_AddStringToObject(notify_root, "payload_not_available", "offline");
        cJSON *device = build_device_block();
        if (device != NULL) {
            cJSON_AddItemToObject(notify_root, "device", device);
        }
    }
    publish_discovery_config(s_notify_config_topic, notify_root);

    /* binary_sensor "connectivity" discovery config, reusing the same
     * availability topic/payloads as its state topic (this device only
     * publishes "online"/"offline" to s_status_topic). */
    cJSON *online_root = cJSON_CreateObject();
    if (online_root != NULL) {
        cJSON_AddStringToObject(online_root, "name", "Online");
        char unique_id[48];
        snprintf(unique_id, sizeof(unique_id), "%s-online", s_device_id);
        cJSON_AddStringToObject(online_root, "unique_id", unique_id);
        cJSON_AddStringToObject(online_root, "device_class", "connectivity");
        cJSON_AddStringToObject(online_root, "state_topic", s_status_topic);
        cJSON_AddStringToObject(online_root, "payload_on", "online");
        cJSON_AddStringToObject(online_root, "payload_off", "offline");
        cJSON_AddStringToObject(online_root, "availability_topic", s_status_topic);
        cJSON_AddStringToObject(online_root, "payload_available", "online");
        cJSON_AddStringToObject(online_root, "payload_not_available", "offline");
        cJSON *device = build_device_block();
        if (device != NULL) {
            cJSON_AddItemToObject(online_root, "device", device);
        }
    }
    publish_discovery_config(s_online_config_topic, online_root);

    /* Diagnostic sensor carrying the panel's IP address; its state is
     * published retained to s_ip_state_topic by publish_ip_state(). */
    cJSON *ip_root = cJSON_CreateObject();
    if (ip_root != NULL) {
        cJSON_AddStringToObject(ip_root, "name", "IP address");
        char unique_id[48];
        snprintf(unique_id, sizeof(unique_id), "%s-ip", s_device_id);
        cJSON_AddStringToObject(ip_root, "unique_id", unique_id);
        cJSON_AddStringToObject(ip_root, "state_topic", s_ip_state_topic);
        cJSON_AddStringToObject(ip_root, "icon", "mdi:ip-network");
        cJSON_AddStringToObject(ip_root, "entity_category", "diagnostic");
        cJSON_AddStringToObject(ip_root, "availability_topic", s_status_topic);
        cJSON_AddStringToObject(ip_root, "payload_available", "online");
        cJSON_AddStringToObject(ip_root, "payload_not_available", "offline");
        cJSON *device = build_device_block();
        if (device != NULL) {
            cJSON_AddItemToObject(ip_root, "device", device);
        }
    }
    publish_discovery_config(s_ip_config_topic, ip_root);
}

/* Publishes the current station IP (retained, QoS 1) to the IP sensor's
 * state topic. Called on every MQTT connect, which also covers reconnects
 * after a network change. */
static void publish_ip_state(void)
{
    char ip[32];
    if (s_mqtt_client == NULL || !mqtt_mgr_get_sta_ip(ip, sizeof(ip))) {
        return;
    }
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_ip_state_topic, ip, 0, 1, true);
    if (msg_id < 0) {
        ESP_LOGW(TAG_MQTT, "Failed to publish IP address to %s", s_ip_state_topic);
    } else {
        ESP_LOGI(TAG_MQTT, "Published IP address %s", ip);
    }
}

/* Frees/resets the command-topic accumulation state so the next message
 * starts clean. Safe to call whether or not a message is in flight. */
static void reset_cmd_accum(void)
{
    free(s_cmd_buf);
    s_cmd_buf = NULL;
    s_cmd_total_len = 0;
    s_cmd_drop = false;
}

/* Handles one MQTT_EVENT_DATA callback for the (single) command topic
 * subscription, accumulating chunked payloads and dispatching a complete
 * notification once fully received. See the s_cmd_buf/s_cmd_drop comment
 * above for the single-buffer-is-safe reasoning. */
static void handle_command_data(esp_mqtt_event_handle_t event)
{
    if (event == NULL) {
        return;
    }

    if (event->topic_len > 0) {
        /* First chunk of a new message: any previous in-flight message
         * that never reached completion (shouldn't normally happen) is
         * abandoned so we don't corrupt/mix buffers. */
        reset_cmd_accum();

        bool topic_matches = (size_t)event->topic_len == strlen(s_command_topic) &&
                              strncmp(event->topic, s_command_topic, (size_t)event->topic_len) == 0;
        if (!topic_matches) {
            ESP_LOGW(TAG_MQTT, "Ignoring MQTT_EVENT_DATA on unexpected topic (len=%d)", event->topic_len);
            s_cmd_drop = true;
            s_cmd_total_len = event->total_data_len > 0 ? event->total_data_len : 1;
            /* Fall through to the drop/accounting logic below so a
             * multi-chunk unexpected-topic message is still consumed
             * (not re-matched) until its final chunk. */
        } else if (event->total_data_len > APP_NOTIFICATION_MAX_JSON_LEN) {
            ESP_LOGW(TAG_MQTT, "Dropping oversized MQTT command payload (%d > %d bytes)",
                event->total_data_len, APP_NOTIFICATION_MAX_JSON_LEN);
            s_cmd_drop = true;
            s_cmd_total_len = event->total_data_len;
        } else {
            s_cmd_buf = calloc(1, (size_t)APP_NOTIFICATION_MAX_JSON_LEN + 1);
            if (s_cmd_buf == NULL) {
                ESP_LOGE(TAG_MQTT, "OOM allocating MQTT command accumulation buffer");
                s_cmd_drop = true;
                s_cmd_total_len = event->total_data_len;
            } else {
                s_cmd_total_len = event->total_data_len;
            }
        }
    }

    bool have_state = s_cmd_buf != NULL || s_cmd_drop;
    if (!have_state) {
        /* No message in flight (e.g. a stray continuation chunk with no
         * preceding first-chunk event) -- nothing to do. Note this is
         * deliberately independent of s_cmd_total_len, which may
         * legitimately be 0 for a just-started, zero-length payload. */
        return;
    }

    if (!s_cmd_drop && s_cmd_buf != NULL) {
        long offset = event->current_data_offset;
        long end = offset + (long)event->data_len;
        if (offset < 0 || event->data_len < 0 || end > APP_NOTIFICATION_MAX_JSON_LEN) {
            ESP_LOGW(TAG_MQTT, "MQTT command chunk out of bounds (offset=%ld len=%d), dropping message",
                offset, event->data_len);
            s_cmd_drop = true;
        } else if (event->data_len > 0) {
            memcpy(s_cmd_buf + offset, event->data, (size_t)event->data_len);
        }
    }

    bool is_final_chunk = (event->current_data_offset + event->data_len) >= s_cmd_total_len;
    if (!is_final_chunk) {
        return;
    }

    if (!s_cmd_drop && s_cmd_buf != NULL) {
        size_t total_len = (size_t)s_cmd_total_len;
        if (total_len > APP_NOTIFICATION_MAX_JSON_LEN) {
            total_len = APP_NOTIFICATION_MAX_JSON_LEN;
        }
        s_cmd_buf[total_len] = '\0';

        app_notification_t *notification = NULL;
        const char *err_msg = NULL;
        bool parsed = notification_parse_json(s_cmd_buf, total_len, &notification, &err_msg);
        if (parsed) {
            ESP_LOGI(TAG_MQTT, "MQTT notification command accepted: id=%s timeout=%us",
                notification->message_id[0] != '\0' ? notification->message_id : "-",
                (unsigned)notification->timeout_sec);
            app_event_t app_event = {
                .type = EV_NOTIFICATION_SHOW,
                .data.notification.notification = notification,
            };
            if (!app_events_publish(&app_event, pdMS_TO_TICKS(100))) {
                /* Ownership only transfers to the queue on a successful
                 * publish; free it ourselves here, mirroring
                 * api_notifications_post_handler()'s failure path. */
                free(notification);
                ESP_LOGW(TAG_MQTT, "Failed to publish EV_NOTIFICATION_SHOW (queue full)");
            }
        } else {
            ESP_LOGW(TAG_MQTT, "Failed to parse MQTT notification command: %s",
                err_msg != NULL ? err_msg : "internal error");
        }
    }

    reset_cmd_accum();
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG_MQTT, "Connected");
        if (s_status_topic[0] != '\0' && s_mqtt_client != NULL) {
            int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_status_topic, "online", 0, 1, true);
            if (msg_id < 0) {
                ESP_LOGW(TAG_MQTT, "Failed to publish online availability");
            }
        }
        publish_ha_discovery();
        publish_ip_state();
        if (s_command_topic[0] != '\0' && s_mqtt_client != NULL) {
            int sub_id = esp_mqtt_client_subscribe(s_mqtt_client, s_command_topic, 1);
            if (sub_id < 0) {
                ESP_LOGW(TAG_MQTT, "Failed to subscribe to command topic %s", s_command_topic);
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG_MQTT, "Disconnected");
        /* Discard any partially-received command payload -- a reconnect
         * means a fresh subscribe and no continuity guarantee for
         * in-flight message chunks. */
        reset_cmd_accum();
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_MQTT, "MQTT error (type=%d)", event != NULL ? event->error_handle->error_type : -1);
        break;
    case MQTT_EVENT_DATA:
        handle_command_data(event);
        break;
    default:
        break;
    }
}

esp_err_t mqtt_mgr_start(const mqtt_mgr_config_t *cfg)
{
    if (cfg == NULL || cfg->broker_host == NULL || cfg->broker_host[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mqtt_client != NULL) {
        return ESP_OK;
    }

    mqtt_mgr_get_device_id(s_device_id, sizeof(s_device_id));
    char room_id[sizeof(s_device_id)];
    if (device_identity_room_id(room_id, sizeof(room_id))) {
        strlcpy(s_device_id, room_id, sizeof(s_device_id)); /* the room names the device */
    }
    if (cfg->device_name != NULL && cfg->device_name[0] != '\0') {
        strlcpy(s_device_name, cfg->device_name, sizeof(s_device_name));
    } else {
        device_identity_display_name(s_device_name, sizeof(s_device_name));
    }

    const char *prefix = (cfg->topic_prefix != NULL && cfg->topic_prefix[0] != '\0')
                              ? cfg->topic_prefix
                              : "betta_ha_panel";
    snprintf(s_status_topic, sizeof(s_status_topic), "%s/%s/status", prefix, s_device_id);
    snprintf(s_command_topic, sizeof(s_command_topic), "%s/%s/notify/set", prefix, s_device_id);
    snprintf(s_ip_state_topic, sizeof(s_ip_state_topic), "%s/%s/ip", prefix, s_device_id);
    /* HA discovery always uses the fixed "homeassistant" root topic
     * (Home Assistant's default discovery_prefix), independent of the
     * user-configurable MQTT topic_prefix used for status/command topics
     * above. */
    snprintf(s_notify_config_topic, sizeof(s_notify_config_topic), "homeassistant/notify/%s/config", s_device_id);
    snprintf(s_online_config_topic, sizeof(s_online_config_topic), "homeassistant/binary_sensor/%s/config",
        s_device_id);
    /* component/node_id/object_id/config form, so further sensors can sit
     * next to this one under the same node_id. */
    snprintf(s_ip_config_topic, sizeof(s_ip_config_topic), "homeassistant/sensor/%s/ip/config", s_device_id);
    ESP_LOGI(TAG_MQTT, "Device id=%s, command topic=%s", s_device_id, s_command_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = cfg->broker_host,
        .broker.address.port = cfg->broker_port > 0 ? cfg->broker_port : 1883,
        .broker.address.transport =
            cfg->tls_enabled ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = (cfg->username != NULL && cfg->username[0] != '\0') ? cfg->username : NULL,
        .credentials.authentication.password =
            (cfg->password != NULL && cfg->password[0] != '\0') ? cfg->password : NULL,
        .credentials.client_id = s_device_id,
        .session.last_will.topic = s_status_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    if (cfg->tls_enabled) {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#else
        ESP_LOGW(TAG_MQTT,
            "MQTT TLS requested but CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is disabled; server verification may fail");
#endif
    }

    ESP_LOGI(TAG_MQTT, "Starting MQTT client (host=%s port=%d tls=%d)", cfg->broker_host,
        mqtt_cfg.broker.address.port, (int)cfg->tls_enabled);

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }

    err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        return err;
    }

    return ESP_OK;
}

void mqtt_mgr_stop(void)
{
    s_connected = false;
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
    reset_cmd_accum();
}

bool mqtt_mgr_is_connected(void)
{
    return s_connected;
}

const char *mqtt_mgr_get_command_topic(void)
{
    return s_command_topic;
}

bool mqtt_mgr_is_started(void)
{
    return s_mqtt_client != NULL;
}

const char *mqtt_mgr_device_id(void)
{
    return s_device_id;
}
