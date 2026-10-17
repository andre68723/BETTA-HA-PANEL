/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/ota_validate.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi_mgr.h"
#include "util/ota_health.h"

#define OTA_VALIDATE_POLL_MS 5000
#define OTA_VALIDATE_TASK_STACK 4096

static const char *TAG = "ota_validate";

const char *ota_validate_image_state(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return "undefined";
    }
    switch (state) {
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

/* A request to the panel's own HTTP server: it must answer, not just have been started. */
static bool http_server_answers(void)
{
    char url[48];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/api/version", APP_HTTP_PORT);
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 3000};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return false;
    }
    const bool ok = esp_http_client_perform(client) == ESP_OK && esp_http_client_get_status_code(client) == 200;
    esp_http_client_cleanup(client);
    return ok;
}

bool ota_validate_bootloader_info(uint32_t *version, char *built, size_t built_len)
{
    esp_bootloader_desc_t desc;
    if (esp_ota_get_bootloader_description(NULL, &desc) != ESP_OK) {
        return false;
    }
    *version = desc.version;
    if (built != NULL && built_len > 0) {
        snprintf(built, built_len, "%.*s", (int)sizeof(desc.date_time), desc.date_time);
    }
    return true;
}

static void ota_validate_task(void *arg)
{
    (void)arg;
    while (strcmp(ota_validate_image_state(), "valid") != 0 && strcmp(ota_validate_image_state(), "undefined") != 0) {
        vTaskDelay(pdMS_TO_TICKS(OTA_VALIDATE_POLL_MS));
        const ota_health_t health = {
            .uptime_s = esp_timer_get_time() / 1000000,
            .boot_completed = true,
            .http_answers = esp_timer_get_time() / 1000000 >= OTA_HEALTH_MIN_UPTIME_S && http_server_answers(),
            .network_ok = wifi_mgr_is_connected() || wifi_mgr_is_setup_ap_active(),
        };
        if (!ota_health_ok(&health)) {
            continue;
        }
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "image marked valid after %lld s: %s", (long long)health.uptime_s, esp_err_to_name(err));
        wifi_mgr_run_deferred_c6_update();
        break;
    }
    vTaskDelete(NULL);
}

void ota_validate_start(void)
{
    const char *state = ota_validate_image_state();
    if (strcmp(state, "pending_verify") != 0) {
        ESP_LOGI(TAG, "running image state: %s, nothing to verify", state);
        return;
    }
    ESP_LOGW(TAG, "new image, pending verify: it counts as healthy after %d s with the HTTP server answering and the network up",
             OTA_HEALTH_MIN_UPTIME_S);
    xTaskCreate(ota_validate_task, "ota_validate", OTA_VALIDATE_TASK_STACK, NULL, 1, NULL);
}
