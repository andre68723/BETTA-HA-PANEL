/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Low-priority, bounded Recorder fetcher. HTTP is streamed through a pure reducer so a chatty
 * entity cannot consume the WebSocket receive buffer or a full JSON reply in RAM.
 */
#include "ha/ha_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "drivers/display_init.h"

#include "app_config.h"
#include "ha/ha_auth_state.h"
#include "ha/ha_client.h"
#include "util/transfer_deadline.h"

#define HISTORY_MAX_RESPONSE (512U * 1024U)
#define HISTORY_CACHE_US (60LL * 1000000LL)
#define HISTORY_TASK_STACK 10240
#define HISTORY_DISPATCH_LOCK_MS 2000
#define HISTORY_QUEUE_LEN 4

static const char *TAG = "ha_history";

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    bool numeric;
    bool today;
    uint32_t generation;
    ha_history_cb_t callback;
} history_request_t;

typedef struct {
    history_request_t request;
    ha_history_snapshot_t *snapshot;
    esp_err_t status;
} history_dispatch_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static ha_history_snapshot_t *s_cache;
static char s_cache_entity[APP_MAX_ENTITY_ID_LEN];
static bool s_cache_numeric;
static int64_t s_cache_at_us;

void ha_history_snapshot_free(ha_history_snapshot_t *snapshot)
{
    if (snapshot != NULL) heap_caps_free(snapshot);
}

static ha_history_snapshot_t *snapshot_new(void)
{
    return (ha_history_snapshot_t *)heap_caps_calloc(1, sizeof(ha_history_snapshot_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void dispatch_on_lvgl(void *user)
{
    history_dispatch_t *dispatch = (history_dispatch_t *)user;
    dispatch->request.callback(dispatch->request.generation, dispatch->snapshot, dispatch->status);
    free(dispatch);
}

static time_t history_window_start(time_t end, int hours, bool today)
{
    time_t start = end - (time_t)hours * 3600;
    if (today) {
        struct tm local;
        if (localtime_r(&end, &local) == NULL) return (time_t)-1;
        local.tm_hour = local.tm_min = local.tm_sec = 0;
        local.tm_isdst = -1;
        const time_t midnight = mktime(&local);
        if (midnight == (time_t)-1) return midnight;
        if (hours == 24 || start < midnight) start = midnight;
    }
    return start;
}

static esp_err_t fetch_once(const history_request_t *request, int hours, ha_history_snapshot_t *snapshot)
{
    if (ha_auth_state_blocked()) return ESP_ERR_INVALID_STATE;
    const int64_t gate_started = esp_timer_get_time() / 1000;
    while (ha_client_heavy_gate_is_busy()) {
        if (ha_auth_state_blocked()) return ESP_ERR_INVALID_STATE;
        if (esp_timer_get_time() / 1000 - gate_started >= 20000) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    ha_client_http_ctx_t ctx;
    if (!ha_client_get_http_context(&ctx)) return ESP_ERR_INVALID_STATE;
    time_t end = time(NULL);
    if (end < 1700000000) return ESP_ERR_INVALID_STATE;
    const time_t start = history_window_start(end, hours, request->today);
    if (start == (time_t)-1) return ESP_FAIL;
    struct tm start_tm, end_tm;
    if (gmtime_r(&start, &start_tm) == NULL || gmtime_r(&end, &end_tm) == NULL) return ESP_FAIL;
    char start_iso[32], end_iso[32], url[640];
    strftime(start_iso, sizeof(start_iso), "%Y-%m-%dT%H:%M:%SZ", &start_tm);
    strftime(end_iso, sizeof(end_iso), "%Y-%m-%dT%H:%M:%SZ", &end_tm);
    int len = snprintf(url, sizeof(url), "%s/api/history/period/%s?filter_entity_id=%s&end_time=%s"
                       "&minimal_response&no_attributes&significant_changes_only=0",
                       ctx.base_url, start_iso, request->entity_id, end_iso);
    if (len < 0 || (size_t)len >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 15000,
        .buffer_size = 1024, .buffer_size_tx = 1024, .keep_alive_enable = false,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    if (ctx.cert_common_name[0]) cfg.common_name = ctx.cert_common_name;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) return ESP_ERR_NO_MEM;
    if (ctx.host_header[0]) esp_http_client_set_header(cli, "Host", ctx.host_header);
    char auth[sizeof(ctx.bearer_token) + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", ctx.bearer_token);
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");

    esp_err_t result = ESP_FAIL;
    int status = 0;
    size_t received = 0;
    const int64_t started_us = esp_timer_get_time();
    transfer_deadline_t deadline;
    transfer_deadline_init(&deadline, started_us / 1000, 15000, 30000);
    ha_client_set_aux_http_pressure(true, 2000);
    if (esp_http_client_open(cli, 0) != ESP_OK) goto done;
    int64_t content_len = esp_http_client_fetch_headers(cli);
    status = esp_http_client_get_status_code(cli);
    if (ha_auth_state_report_http(status, false, true))
        ESP_LOGE(TAG, "HA rejected history request (HTTP %d); token requests stop until restart", status);
    if (status != 200) { result = ESP_ERR_HTTP_BASE + status; goto close; }
    if (content_len > HISTORY_MAX_RESPONSE) { result = ESP_ERR_INVALID_SIZE; goto close; }

    ha_history_reducer_t reducer;
    ha_history_reduce_init(&reducer, snapshot, request->numeric, (int64_t)start, (int64_t)end);
    char chunk[1024];
    for (;;) {
        if (transfer_deadline_expired(&deadline, esp_timer_get_time() / 1000)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        int n = esp_http_client_read(cli, chunk, sizeof(chunk));
        if (n < 0) { result = ESP_FAIL; break; }
        if (n == 0) {
            result = esp_http_client_is_complete_data_received(cli) ? ESP_OK : ESP_ERR_TIMEOUT;
            break;
        }
        received += (size_t)n;
        transfer_deadline_progress(&deadline, esp_timer_get_time() / 1000);
        if (received > HISTORY_MAX_RESPONSE || ha_auth_state_blocked()) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        ha_history_reduce_feed(&reducer, chunk, (size_t)n);
    }
    if (result == ESP_OK) ha_history_reduce_finish(snapshot);
close:
    esp_http_client_close(cli);
done:
    esp_http_client_cleanup(cli);
    ESP_LOGI(TAG, "history %s %dh: %s, HTTP %d, %u bytes, %u values, %lld ms, internal heap %u free / %u min",
             request->entity_id, hours, esp_err_to_name(result), status, (unsigned)received,
             (unsigned)snapshot->valid_count, (long long)((esp_timer_get_time() - started_us) / 1000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ha_client_set_aux_http_pressure(false, 2000);
    return result;
}

static esp_err_t fetch_history(const history_request_t *req, ha_history_snapshot_t *snapshot)
{
    for (int hours = 24; hours >= 6; hours /= 2) {
        esp_err_t status = fetch_once(req, hours, snapshot);
        if (status != ESP_ERR_INVALID_SIZE) {
            if (status == ESP_OK && hours < 24) snapshot->shortened = true;
            return status;
        }
    }
    return ESP_ERR_INVALID_SIZE;
}

static void history_task(void *arg)
{
    (void)arg;
    history_request_t req;
    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        ha_history_snapshot_t *snapshot = snapshot_new();
        esp_err_t status = snapshot == NULL ? ESP_ERR_NO_MEM : ESP_OK;
        if (snapshot != NULL) {
            if (s_cache != NULL && strcmp(req.entity_id, s_cache_entity) == 0 &&
                req.numeric == s_cache_numeric && !req.today &&
                esp_timer_get_time() - s_cache_at_us < HISTORY_CACHE_US) {
                memcpy(snapshot, s_cache, sizeof(*snapshot));
            } else {
                status = fetch_history(&req, snapshot);
                if (status == ESP_OK && !req.today) {
                    if (s_cache == NULL) s_cache = snapshot_new();
                    if (s_cache != NULL) {
                        memcpy(s_cache, snapshot, sizeof(*snapshot));
                        strlcpy(s_cache_entity, req.entity_id, sizeof(s_cache_entity));
                        s_cache_numeric = req.numeric;
                        s_cache_at_us = esp_timer_get_time();
                    }
                }
            }
        }
        history_dispatch_t *dispatch = malloc(sizeof(*dispatch));
        if (dispatch == NULL) { ha_history_snapshot_free(snapshot); continue; }
        *dispatch = (history_dispatch_t){.request = req, .snapshot = snapshot, .status = status};
        /* lv_async_call() inserts into LVGL's timer list, which the LVGL task walks under the display lock
         * (LV_USE_OS is off, LVGL has no lock of its own): take the lock from this task. */
        bool queued = false;
        if (display_lock(HISTORY_DISPATCH_LOCK_MS)) {
            queued = lv_async_call(dispatch_on_lvgl, dispatch) == LV_RESULT_OK;
            display_unlock();
        }
        if (!queued) {
            ha_history_snapshot_free(snapshot);
            free(dispatch);
        }
    }
}

static esp_err_t request_history(const char *entity_id, bool numeric, bool today, uint32_t generation, ha_history_cb_t callback)
{
    if (entity_id == NULL || callback == NULL || strlen(entity_id) >= APP_MAX_ENTITY_ID_LEN) return ESP_ERR_INVALID_ARG;
    const char *dot = strchr(entity_id, '.');
    if (dot == NULL || dot == entity_id || dot[1] == '\0') return ESP_ERR_INVALID_ARG;
    for (const char *p = entity_id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || p == dot))
            return ESP_ERR_INVALID_ARG;
    }
    if (s_queue == NULL) {
        /* One place per caller with a request outstanding (graph feed, quote feed, popup) plus a spare: a single
         * overwritten place let a graph's request replace the waiting quote request, which then never got an answer. */
        s_queue = xQueueCreate(HISTORY_QUEUE_LEN, sizeof(history_request_t));
        if (s_queue == NULL) return ESP_ERR_NO_MEM;
        BaseType_t created;
/* the stack lives in PSRAM (2 KB of 8 KB were left with a 5,000-value day); the task never touches flash */
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
        created = xTaskCreatePinnedToCoreWithCaps(history_task, "ha_history", HISTORY_TASK_STACK,
                    NULL, 4, &s_task, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        created = xTaskCreatePinnedToCore(history_task, "ha_history", HISTORY_TASK_STACK, NULL, 4, &s_task, 1);
#endif
        if (created != pdPASS) {
            vQueueDelete(s_queue);
            s_queue = NULL;
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    history_request_t request = {.numeric = numeric, .today = today, .generation = generation, .callback = callback};
    strlcpy(request.entity_id, entity_id, sizeof(request.entity_id));
    return xQueueSend(s_queue, &request, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ha_history_request(const char *entity_id, bool numeric, uint32_t generation, ha_history_cb_t callback)
{
    return request_history(entity_id, numeric, false, generation, callback);
}

esp_err_t ha_history_request_today(const char *entity_id, uint32_t generation, ha_history_cb_t callback)
{
    return request_history(entity_id, true, true, generation, callback);
}
