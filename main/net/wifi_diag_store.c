/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/wifi_diag_store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "util/flash_trace.h"
#include "util/flash_gate.h"

static const char *TAG = "wifi_diag_store";

#define WIFI_DIAG_NVS_NAMESPACE "wifi_diag"
#define WIFI_DIAG_NVS_KEY "last"
#define WIFI_DIAG_MIN_SAVE_INTERVAL_MS 5000

static int64_t s_last_save_ms = 0;

void wifi_diag_store_save(const wifi_mgr_diag_t *diag, bool critical)
{
    if (diag == NULL) {
        return;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (!critical && s_last_save_ms != 0 && (now_ms - s_last_save_ms) < WIFI_DIAG_MIN_SAVE_INTERVAL_MS) {
        return;
    }

    if (!flash_gate_enter(2000)) return;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_DIAG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        flash_gate_leave();
        return;
    }

    err = nvs_set_blob(handle, WIFI_DIAG_NVS_KEY, diag, sizeof(*diag));
    if (err == ESP_OK) {
        FLASH_TRACE_BEGIN(trace_start);
        err = nvs_commit(handle);
        FLASH_TRACE_END(trace_start, "wifi diagnostics (nvs)", sizeof(*diag));
    }
    nvs_close(handle);
    flash_gate_leave();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist Wi-Fi diagnostics: %s", esp_err_to_name(err));
        return;
    }
    s_last_save_ms = now_ms;
}

bool wifi_diag_store_load(wifi_mgr_diag_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_DIAG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, WIFI_DIAG_NVS_KEY, out, &len);
    nvs_close(handle);

    if (err != ESP_OK || len != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

bool wifi_diag_store_save_c6(const c6_memory_incident_t *record)
{
    if (!c6_memory_incident_valid(record, sizeof(*record)) || !flash_gate_enter(2000)) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_DIAG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "c6_last", record, sizeof(*record));
        if (err == ESP_OK) {
            FLASH_TRACE_BEGIN(trace_start);
            err = nvs_commit(handle);
            FLASH_TRACE_END(trace_start, "C6 memory incident (nvs)", sizeof(*record));
        }
        nvs_close(handle);
    }
    flash_gate_leave();
    return err == ESP_OK;
}

bool wifi_diag_store_load_c6(c6_memory_incident_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    nvs_handle_t handle;
    if (nvs_open(WIFI_DIAG_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof(*out);
    esp_err_t err = nvs_get_blob(handle, "c6_last", out, &size);
    nvs_close(handle);
    if (err == ESP_OK && c6_memory_incident_valid(out, size)) return true;
    memset(out, 0, sizeof(*out));
    return false;
}
