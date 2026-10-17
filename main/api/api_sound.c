/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/api_routes.h"
#include "drivers/chime.h"
#include "drivers/audio_out.h"
#include "cJSON.h"
#include <math.h>

esp_err_t api_sound_test_post_handler(httpd_req_t *req)
{
    if (!audio_out_available()) {
        httpd_resp_set_status(req, "501 Not Implemented");
        return httpd_resp_sendstr(req, "Audio is not supported on this board");
    }
    if (req->content_len < 1 || req->content_len > 64) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Expected volume 0..100");
    }
    char body[65] = {0};
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return httpd_resp_send_500(req);
        received += count;
    }
    cJSON *root = cJSON_Parse(body);
    cJSON *volume = cJSON_GetObjectItemCaseSensitive(root, "volume");
    bool valid = cJSON_IsNumber(volume) && volume->valuedouble >= 0 && volume->valuedouble <= 100 &&
                 floor(volume->valuedouble) == volume->valuedouble;
    int value = valid ? volume->valueint : 0;
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Expected volume 0..100");
    }
    if (!chime_request(CHIME_DOORBELL, true, value)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Could not schedule chime");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"queued\":true}");
}
