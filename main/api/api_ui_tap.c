/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/ui/tap {"widget": "<widget id>"} taps a tile the way a finger would, for device tests without touch
 * (the tile's popup opens; "topbar_stock" is the stock chip, a tap there, "topbar_stock_12m" also selects the year); {"close": true} dismisses the popup that tap opened. Only tiles whose tap opens a
 * read-only popup answer (the UI task checks the type), so this cannot switch a light or unlock a door. Like every
 * write endpoint it needs the admin login when one is set.
 */
#include "api/api_routes.h"

#include <string.h>

#include "app_events.h"
#include "cJSON.h"

esp_err_t api_ui_tap_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    if (req->content_len <= 0 || req->content_len >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
    }
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        }
        got += r;
    }
    cJSON *root = cJSON_Parse(body);
    cJSON *widget = cJSON_GetObjectItemCaseSensitive(root, "widget");
    app_event_t event = {.type = EV_UI_TAP};
    event.data.tap.close = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "close"));
    if (!event.data.tap.close) {
        if (!cJSON_IsString(widget) || widget->valuestring == NULL || widget->valuestring[0] == '\0') {
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "widget or close is required");
        }
        strlcpy(event.data.tap.widget_id, widget->valuestring, sizeof(event.data.tap.widget_id));
    }
    cJSON_Delete(root);
    if (!app_events_publish(&event, pdMS_TO_TICKS(50))) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Event queue busy");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
