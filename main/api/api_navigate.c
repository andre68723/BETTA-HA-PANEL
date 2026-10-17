/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/navigate {"page": "<page id>"}: switch the panel to a page (for automations and for
 * measuring page switches without touching the screen).
 */
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include "api/api_routes.h"
#include "app_events.h"

esp_err_t api_navigate_post_handler(httpd_req_t *req)
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
    cJSON *page = cJSON_GetObjectItemCaseSensitive(root, "page");
    if (!cJSON_IsString(page) || page->valuestring == NULL || page->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "page is required");
    }
    app_event_t event = {.type = EV_UI_NAVIGATE};
    strlcpy(event.data.navigate.page_id, page->valuestring, sizeof(event.data.navigate.page_id));
    cJSON_Delete(root);
    if (!app_events_publish(&event, pdMS_TO_TICKS(50))) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Event queue busy");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
