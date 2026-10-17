/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * POST /api/factory-reset {"confirm": "RESET"}: erase the settings (Wi-Fi, Home Assistant token, MQTT, admin password)
 * and the stored layout, themes and history, then restart. The panel comes back like a freshly flashed one and opens
 * the setup access point. The app itself is not touched.
 */
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api/api_routes.h"
#include "util/reboot_log_esp.h"

static const char *TAG_RESET = "factory_reset";

static void erase_partition(esp_partition_type_t type, esp_partition_subtype_t subtype)
{
    const esp_partition_t *part = esp_partition_find_first(type, subtype, NULL);
    if (part == NULL) {
        return;
    }
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    ESP_LOGW(TAG_RESET, "erased %s (%u KB): %s", part->label, (unsigned)(part->size / 1024), esp_err_to_name(err));
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800)); /* let the HTTP response leave first */
    erase_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS);
    erase_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS);
    ESP_LOGW(TAG_RESET, "restarting");
    reboot_log_mark(REBOOT_CAUSE_FACTORY_RESET);
    esp_restart();
}

esp_err_t api_factory_reset_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
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
    cJSON *confirm = cJSON_GetObjectItemCaseSensitive(root, "confirm");
    const bool ok = cJSON_IsString(confirm) && confirm->valuestring != NULL && strcmp(confirm->valuestring, "RESET") == 0;
    cJSON_Delete(root);
    if (!ok) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "confirm must be \"RESET\"");
    }
    if (xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 5, NULL) != pdPASS) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start the reset");
    }
    ESP_LOGW(TAG_RESET, "factory reset requested");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
}
