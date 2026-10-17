/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/camera/test: force presence sampling for a short while even with the display
 * active, so Settings > Display > Camera can show a live "presence detected" status while the
 * sensitivity is being tried out (ambient_camera.c normally only samples once the display is
 * already dimmed, to wake it). No body. A no-op (still returns ok) on variants without the
 * built-in camera; GET /api/version's "camera" object is absent there instead.
 */
#include "esp_http_server.h"

#include "drivers/ambient_camera.h"

#define API_CAMERA_TEST_DURATION_MS 30000

esp_err_t api_camera_test_post_handler(httpd_req_t *req)
{
    ambient_camera_start_test(API_CAMERA_TEST_DURATION_MS);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
