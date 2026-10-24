/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_easy_stock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "ha/ha_auth_state.h"
#include "ha/ha_client.h"
#include "util/transfer_deadline.h"

#define EASY_TASK_STACK 8192
#define EASY_DISPATCH_LOCK_MS 2000
#define EASY_SYMBOL_MAX 24
#define EASY_GATE_WAIT_MS 20000
#define EASY_TRANSFER_MS 30000

static const char *TAG = "easy_stock";

typedef struct {
    char symbol[EASY_SYMBOL_MAX];
    uint32_t generation;
    ha_easy_stock_cb_t callback;
} easy_request_t;

typedef struct {
    easy_request_t request;
    ha_easy_stock_history_t *history;
} easy_dispatch_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

void ha_easy_stock_free(ha_easy_stock_history_t *history)
{
    if (history != NULL) heap_caps_free(history);
}

static void dispatch_on_lvgl(void *user)
{
    easy_dispatch_t *dispatch = (easy_dispatch_t *)user;
    dispatch->request.callback(dispatch->request.generation, dispatch->history);
    free(dispatch);
}

/* Percent-encodes everything but unreserved characters ("^GDAXI", "EURUSD=X"). */
static bool encode_symbol(const char *symbol, char *out, size_t cap)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)symbol; *p != '\0'; p++) {
        const bool plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                           *p == '.' || *p == '-' || *p == '_' || *p == '~';
        const int len = plain ? snprintf(out + n, cap - n, "%c", *p) : snprintf(out + n, cap - n, "%%%02X", *p);
        if (len < 0 || (size_t)len >= cap - n) return false;
        n += (size_t)len;
    }
    return n > 0;
}

static esp_err_t fetch(const easy_request_t *request, ha_easy_stock_history_t *history)
{
    if (ha_auth_state_blocked()) return ESP_ERR_INVALID_STATE;
    const int64_t gate_started = esp_timer_get_time() / 1000;
    while (ha_client_heavy_gate_is_busy()) {
        if (ha_auth_state_blocked()) return ESP_ERR_INVALID_STATE;
        if (esp_timer_get_time() / 1000 - gate_started >= EASY_GATE_WAIT_MS) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    ha_client_http_ctx_t ctx;
    if (!ha_client_get_http_context(&ctx)) return ESP_ERR_INVALID_STATE;
    char symbol[EASY_SYMBOL_MAX * 3], url[512];
    if (!encode_symbol(request->symbol, symbol, sizeof(symbol))) return ESP_ERR_INVALID_ARG;
    const int url_len = snprintf(url, sizeof(url), "%s/api/easy_stock/history?symbol=%s", ctx.base_url, symbol);
    if (url_len < 0 || (size_t)url_len >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    /* the whole reply in PSRAM: it is parsed as one document, and refused above the bound */
    char *body = heap_caps_malloc(HA_QUOTE_HISTORY_MAX_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) return ESP_ERR_NO_MEM;
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 15000,
        .buffer_size = 1024, .buffer_size_tx = 1024, .keep_alive_enable = false,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    if (ctx.cert_common_name[0]) cfg.common_name = ctx.cert_common_name;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        heap_caps_free(body);
        return ESP_ERR_NO_MEM;
    }
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
    transfer_deadline_init(&deadline, started_us / 1000, 15000, EASY_TRANSFER_MS);
    ha_client_set_aux_http_pressure(true, 2000);
    if (esp_http_client_open(cli, 0) != ESP_OK) goto done;
    const int64_t content_len = esp_http_client_fetch_headers(cli);
    status = esp_http_client_get_status_code(cli);
    if (ha_auth_state_report_http(status, false, true))
        ESP_LOGE(TAG, "HA rejected the history request (HTTP %d); token requests stop until restart", status);
    if (status == 404) { result = ESP_ERR_NOT_FOUND; goto close; }
    if (status != 200) { result = ESP_ERR_HTTP_BASE + status; goto close; }
    if (content_len > (int64_t)HA_QUOTE_HISTORY_MAX_BYTES) { result = ESP_ERR_INVALID_SIZE; goto close; }
    for (;;) {
        if (transfer_deadline_expired(&deadline, esp_timer_get_time() / 1000)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        /* one byte more than the bound may arrive, so an oversized reply without Content-Length is noticed */
        const int n = esp_http_client_read(cli, body + received, (int)(HA_QUOTE_HISTORY_MAX_BYTES + 1 - received));
        if (n < 0) { result = ESP_FAIL; break; }
        if (n == 0) {
            result = esp_http_client_is_complete_data_received(cli) ? ESP_OK : ESP_ERR_TIMEOUT;
            break;
        }
        received += (size_t)n;
        transfer_deadline_progress(&deadline, esp_timer_get_time() / 1000);
        if (received > HA_QUOTE_HISTORY_MAX_BYTES || ha_auth_state_blocked()) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (result == ESP_OK) history->outcome = ha_quote_easy_history_parse(body, received, request->symbol, &history->daily);
close:
    esp_http_client_close(cli);
done:
    esp_http_client_cleanup(cli);
    heap_caps_free(body);
    ESP_LOGI(TAG, "history %s: %s, HTTP %d, %u bytes, %u closes, %lld ms, internal heap %u free", request->symbol,
             esp_err_to_name(result), status, (unsigned)received, (unsigned)history->daily.count,
             (long long)((esp_timer_get_time() - started_us) / 1000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ha_client_set_aux_http_pressure(false, 2000);
    return result;
}

static void easy_task(void *arg)
{
    (void)arg;
    easy_request_t request;
    for (;;) {
        if (xQueueReceive(s_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        ha_easy_stock_history_t *history = heap_caps_calloc(1, sizeof(*history), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (history == NULL) continue; /* the feed asks again after its timeout */
        history->status = fetch(&request, history);
        easy_dispatch_t *dispatch = malloc(sizeof(*dispatch));
        if (dispatch == NULL) {
            ha_easy_stock_free(history);
            continue;
        }
        *dispatch = (easy_dispatch_t){.request = request, .history = history};
        /* lv_async_call() edits LVGL's timer list: take the display lock (see ha_history.c) */
        bool queued = false;
        if (display_lock(EASY_DISPATCH_LOCK_MS)) {
            queued = lv_async_call(dispatch_on_lvgl, dispatch) == LV_RESULT_OK;
            display_unlock();
        }
        if (!queued) {
            ha_easy_stock_free(history);
            free(dispatch);
        }
    }
}

esp_err_t ha_easy_stock_request(const char *symbol, uint32_t generation, ha_easy_stock_cb_t callback)
{
    if (symbol == NULL || symbol[0] == '\0' || callback == NULL || strlen(symbol) >= EASY_SYMBOL_MAX) return ESP_ERR_INVALID_ARG;
    if (s_queue == NULL) {
        s_queue = xQueueCreate(1, sizeof(easy_request_t));
        if (s_queue == NULL) return ESP_ERR_NO_MEM;
        BaseType_t created;
/* the stack lives in PSRAM: internal RAM is the scarce resource and the task never touches flash */
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
        created = xTaskCreatePinnedToCoreWithCaps(easy_task, "easy_stock", EASY_TASK_STACK, NULL, 3, &s_task, 1,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        created = xTaskCreatePinnedToCore(easy_task, "easy_stock", EASY_TASK_STACK, NULL, 3, &s_task, 1);
#endif
        if (created != pdPASS) {
            vQueueDelete(s_queue);
            s_queue = NULL;
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    easy_request_t request = {.generation = generation, .callback = callback};
    strlcpy(request.symbol, symbol, sizeof(request.symbol));
    return xQueueOverwrite(s_queue, &request) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}
