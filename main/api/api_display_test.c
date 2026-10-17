/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 * Temporary fixed backlight preview; settings and the night schedule are untouched.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "api/api_routes.h"
#include "drivers/display_init.h"
#include "ui/ui_display_protection.h"

static esp_err_t send_status(httpd_req_t *req)
{
    /* The caller holds the display lock while reading the UI-owned test state. */
    const int remaining = ui_display_brightness_test_remaining_seconds();
    char reply[144];
    snprintf(reply, sizeof(reply), "{\"active\":%s,\"remaining_s\":%d,\"kind\":\"%s\",\"brightness_percent\":%d}",
        remaining > 0 ? "true" : "false", remaining, ui_display_brightness_test_kind(),
        display_get_brightness_percent());
    display_unlock();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, reply);
}

static esp_err_t send_busy(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "Display busy");
}

esp_err_t api_display_test_get_handler(httpd_req_t *req)
{
    if (!display_lock(100)) return send_busy(req);
    return send_status(req);
}

esp_err_t api_display_test_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    if (req->content_len <= 0 || req->content_len >= (int)sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
    for (int got = 0; got < req->content_len;) {
        const int count = httpd_req_recv(req, body + got, req->content_len - got);
        if (count <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        got += count;
    }
    cJSON *root = cJSON_Parse(body);
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *percent = cJSON_GetObjectItemCaseSensitive(root, "percent");
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    const bool start = cJSON_IsString(action) && strcmp(action->valuestring, "start") == 0;
    const bool update = cJSON_IsString(action) && strcmp(action->valuestring, "update") == 0;
    const bool stop = cJSON_IsString(action) && strcmp(action->valuestring, "stop") == 0;
    const int kind_id = cJSON_IsString(kind) ? (strcmp(kind->valuestring, "active") == 0 ? 0 :
        (strcmp(kind->valuestring, "dim") == 0 ? 1 : (strcmp(kind->valuestring, "night") == 0 ? 2 : -1))) : -1;
    const bool valid_percent = cJSON_IsNumber(percent) && isfinite(percent->valuedouble) &&
        percent->valuedouble >= 0 && percent->valuedouble <= 100 && floor(percent->valuedouble) == percent->valuedouble;
    if (!cJSON_IsObject(root) || (!start && !update && !stop) ||
        ((start || update) && !valid_percent) || (start && kind_id < 0)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected start/update with percent 0..100, or stop");
    }
    const int value = valid_percent ? percent->valueint : 0;
    cJSON_Delete(root);
    if (!display_lock(100)) return send_busy(req);
    if (start && !ui_display_brightness_test_start(value, kind_id)) {
        display_unlock();
        return send_busy(req);
    }
    if (update) (void)ui_display_brightness_test_update(value);
    if (stop) ui_display_brightness_test_stop();
    return send_status(req);
}
