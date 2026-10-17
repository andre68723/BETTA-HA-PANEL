/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/restart: restart the panel without changing anything ("Restart panel" in Settings > Device). Needed after Home Assistant
 * lifted an IP ban or accepted a token again: the panel does not retry by itself once Home Assistant rejected it, and saving the settings
 * unchanged does not restart it. The restart is logged as a manual one in the restart history.
 */
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api/api_routes.h"
#include "util/reboot_log_esp.h"

static const char *TAG_RESTART = "restart";

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800)); /* let the HTTP response leave first */
    reboot_log_mark(REBOOT_CAUSE_MANUAL);
    esp_restart();
}

esp_err_t api_restart_post_handler(httpd_req_t *req)
{
    if (xTaskCreate(restart_task, "manual_restart", 3072, NULL, 5, NULL) != pdPASS) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start the restart");
    }
    ESP_LOGW(TAG_RESTART, "restart requested from the editor");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
}
