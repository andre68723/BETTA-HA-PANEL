/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#include "net/c6_memory.h"

#include <string.h>
#include "net/wifi_diag_store.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static c6_memory_state_t s_state;
static c6_memory_incident_t s_incident;
static uint32_t s_boot_id, s_incident_revision, s_saved_revision;
static bool s_initialized, s_pending, s_urgent;
static TaskHandle_t s_worker;
static c6_memory_configure_fn s_configure;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void worker(void *arg)
{
    (void)arg;
    c6_memory_incident_t retained;
    if (wifi_diag_store_load_c6(&retained)) {
        portENTER_CRITICAL(&s_lock);
        if (!s_pending) s_incident = retained;
        portEXIT_CRITICAL(&s_lock);
    }
    int64_t last_save = -5000;
    for (;;) {
        const int64_t now = now_ms();
        portENTER_CRITICAL(&s_lock);
        const bool due = c6_memory_state_due(&s_state, now);
        const uint32_t generation = s_state.generation;
        const bool save = s_pending && (s_urgent || now - last_save >= 5000);
        retained = s_incident;
        const uint32_t revision = s_incident_revision;
        portEXIT_CRITICAL(&s_lock);
        if (save) {
            const bool ok = wifi_diag_store_save_c6(&retained);
            if (!ok) ESP_LOGW("c6_memory", "Could not retain C6 incident snapshot");
            last_save = now_ms();
            portENTER_CRITICAL(&s_lock);
            if (revision == s_incident_revision) {
                if (ok) s_pending = false;
                s_urgent = false; /* a failed write retries at the regular, bounded interval */
            }
            if (ok) s_saved_revision = revision;
            portEXIT_CRITICAL(&s_lock);
        }
        if (due) {
            c6_memory_values_t values = {0};
            bool busy = false;
            esp_err_t err = s_configure(&values, &busy);
            if (err == ESP_OK && !c6_memory_values_valid(&values)) err = ESP_ERR_INVALID_RESPONSE;
            portENTER_CRITICAL(&s_lock);
            c6_memory_state_result(&s_state, generation, now_ms(), err, busy,
                err == ESP_ERR_NOT_SUPPORTED, &values);
            portEXIT_CRITICAL(&s_lock);
            if (err != ESP_OK && !busy) ESP_LOGW("c6_memory", "Memory monitor: %s", esp_err_to_name(err));
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)); /* a restart wakes the worker early */
    }
}

void c6_memory_init(c6_memory_configure_fn configure)
{
    if (s_initialized) return;
    s_initialized = true;
    s_configure = configure;
    s_boot_id = esp_random();
    c6_memory_state_init(&s_state, configure != NULL);
    /* This task writes incident records to flash: keep its modest stack internal. */
    if (xTaskCreate(worker, "c6_memory", 6144, NULL, 2, &s_worker) != pdPASS) {
        s_state.last_error = ESP_ERR_NO_MEM;
        ESP_LOGW("c6_memory", "Could not start memory diagnostics");
    }
}

void c6_memory_allow(bool allow)
{
    portENTER_CRITICAL(&s_lock);
    s_state.allowed = allow;
    portEXIT_CRITICAL(&s_lock);
}

bool c6_memory_can_configure(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool ready = s_state.allowed && s_state.link_up;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

void c6_memory_note_incident(void)
{
    portENTER_CRITICAL(&s_lock);
    if (c6_memory_incident_capture(&s_state, s_boot_id, now_ms(), &s_incident)) {
        s_incident_revision++;
        s_pending = true;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool c6_memory_retain_for_restart(uint8_t cause, uint32_t timeout_ms)
{
    portENTER_CRITICAL(&s_lock);
    const bool captured = c6_memory_incident_capture(&s_state, s_boot_id, now_ms(), &s_incident);
    if (captured) {
        s_incident.restart_cause = cause;
        s_incident_revision++;
        s_pending = true;
    }
    /* Without a current sample an unsaved earlier incident (a disconnect) is still worth keeping. */
    s_urgent = s_pending;
    const uint32_t revision = s_incident_revision;
    const bool wanted = s_pending;
    portEXIT_CRITICAL(&s_lock);
    if (!wanted || s_worker == NULL) return false;
    xTaskNotifyGive(s_worker);
    for (uint32_t waited = 0; waited < timeout_ms; waited += 20) {
        portENTER_CRITICAL(&s_lock);
        const bool saved = s_saved_revision == revision;
        portEXIT_CRITICAL(&s_lock);
        if (saved) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

void c6_memory_transport(bool up)
{
    if (!up) c6_memory_note_incident();
    portENTER_CRITICAL(&s_lock);
    if (!up) c6_memory_state_reset(&s_state, now_ms());
    s_state.link_up = up;
    portEXIT_CRITICAL(&s_lock);
}

void c6_memory_cp_init(void)
{
    c6_memory_note_incident();
    portENTER_CRITICAL(&s_lock);
    s_state.unsupported = false;
    c6_memory_state_reset(&s_state, now_ms());
    portEXIT_CRITICAL(&s_lock);
}

void c6_memory_report(const c6_memory_values_t *values)
{
    portENTER_CRITICAL(&s_lock);
    c6_memory_state_report(&s_state, now_ms(), values);
    portEXIT_CRITICAL(&s_lock);
}

void c6_memory_add_json(cJSON *root, cJSON *wifi_diag)
{
    c6_memory_state_t state;
    c6_memory_incident_t incident;
    portENTER_CRITICAL(&s_lock);
    state = s_state;
    incident = s_incident;
    const uint32_t boot = s_boot_id;
    portEXIT_CRITICAL(&s_lock);
    const int64_t now = now_ms();
    c6_memory_state_json(root, &state, now, boot);
    /* A co-processor image without the memory monitor never reports: an incident retained from an earlier image is not shown beside it. */
    if (strcmp(c6_memory_state_name(&state, now), "unsupported") != 0) c6_memory_incident_json(wifi_diag, &incident, boot);
}
