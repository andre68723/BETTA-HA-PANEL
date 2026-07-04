/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * POST /api/notifications — accept a title/message payload and hand it to
 * the UI task as an EV_NOTIFICATION_SHOW event. The payload is heap
 * allocated; ownership transfers to the UI task once the event is queued.
 */
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "api/api_routes.h"
#include "app_config.h"
#include "app_events.h"
#include "util/log_tags.h"

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddStringToObject(root, "error", message);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return httpd_resp_send_500(req);
    }
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static void copy_json_string(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

esp_err_t api_notifications_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > APP_NOTIFICATION_MAX_JSON_LEN) {
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

    const cJSON *message_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsString(message_item) || message_item->valuestring == NULL ||
        message_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "Missing message");
    }

    app_notification_t *notification = calloc(1, sizeof(app_notification_t));
    if (notification == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    snprintf(notification->message, sizeof(notification->message), "%s", message_item->valuestring);
    copy_json_string(root, "title", notification->title, sizeof(notification->title));
    copy_json_string(root, "message_id", notification->message_id, sizeof(notification->message_id));

    const cJSON *timeout_item = cJSON_GetObjectItemCaseSensitive(root, "timeout_sec");
    if (cJSON_IsNumber(timeout_item)) {
        double timeout = timeout_item->valuedouble;
        if (timeout < 0.0) {
            timeout = 0.0;
        }
        if (timeout > (double)APP_NOTIFICATION_MAX_TIMEOUT_SEC) {
            timeout = (double)APP_NOTIFICATION_MAX_TIMEOUT_SEC;
        }
        notification->timeout_sec = (uint32_t)timeout;
    }
    /* "priority" is accepted but intentionally ignored in the first version. */
    cJSON_Delete(root);

    ESP_LOGI(TAG_HTTP, "notification accepted: id=%s timeout=%us",
             notification->message_id[0] != '\0' ? notification->message_id : "-",
             (unsigned)notification->timeout_sec);

    /* Ownership of `notification` transfers to the UI task on success; do not
     * touch it after a successful publish. */
    app_event_t event = {
        .type = EV_NOTIFICATION_SHOW,
        .data.notification.notification = notification,
    };
    if (!app_events_publish(&event, pdMS_TO_TICKS(100))) {
        free(notification);
        return send_json_error(req, "503 Service Unavailable", "Event queue full");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
