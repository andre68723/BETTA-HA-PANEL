/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/notifications — accept a title/message payload and hand it to
 * the UI task as an EV_NOTIFICATION_SHOW event. The payload is heap
 * allocated; ownership transfers to the UI task once the event is queued.
 */
#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "api/api_routes.h"
#include "app_config.h"
#include "app_events.h"
#include "util/log_tags.h"
#include "util/notification_parse.h"

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

    app_notification_t *notification = NULL;
    const char *err_msg = NULL;
    bool parsed = notification_parse_json(buf, (size_t)received, &notification, &err_msg);
    free(buf);
    if (!parsed) {
        if (err_msg != NULL) {
            return send_json_error(req, "400 Bad Request", err_msg);
        }
        return httpd_resp_send_500(req);
    }

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
