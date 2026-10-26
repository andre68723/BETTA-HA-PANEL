/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Run the real HTTP workers with a virtual clock, scripted HTTP and failed task creation.
 * No radio, token or wall-clock waits. Compile once per worker to keep their static state isolated. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_EASY_STOCK
#include "../../main/ha/ha_easy_stock.c"
#else
#include "../../main/ha/ha_history.c"
#endif

static int64_t clock_us;
static bool gate_busy, auth_blocked, fail_task;
static unsigned task_attempts, queues, opens, closes, cleanups, reads;
static int http_status = 200;
static int64_t body_size;
static bool slow;

const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(TickType_t ticks) { clock_us += (int64_t)ticks * 1000; }
void host_log(const char *level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }
bool ha_auth_state_blocked(void) { return auth_blocked; }
bool ha_auth_state_report_http(int status, bool discovery, bool token) {
    (void)discovery; (void)token;
    if (status == 401 || status == 403) auth_blocked = true;
    return auth_blocked;
}
bool ha_client_heavy_gate_is_busy(void) { return gate_busy; }
bool ha_client_get_http_context(ha_client_http_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    strcpy(ctx->base_url, "https://ha.example.invalid");
    return true;
}
void ha_client_set_aux_http_pressure(bool active, int64_t hold_ms) { (void)active; (void)hold_ms; }
void *heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void heap_caps_free(void *p) { free(p); }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 50000; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 40000; }
size_t host_strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t k = n < size ? n : size - 1; memcpy(dst, src, k); dst[k] = 0; }
    return n;
}
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size) {
    (void)length; (void)item_size; queues++; return malloc(1);
}
void vQueueDelete(QueueHandle_t q) { queues--; free(q); }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks) {
    (void)q; (void)item; (void)ticks; return pdPASS;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item) { return xQueueSend(q, item, 0); }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks) {
    (void)q; (void)item; (void)ticks; return pdFALSE;
}
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                  int priority, TaskHandle_t *handle, int core) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    task_attempts++; return fail_task ? pdFALSE : pdPASS;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) { (void)cfg; return (void *)1; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) {
    (void)c; (void)k; (void)v; return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int size) { (void)c; (void)size; opens++; return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return body_size; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return http_status; }
int esp_http_client_read(esp_http_client_handle_t c, char *data, int size) {
    (void)c; assert(size > 0); reads++;
    if (!slow) return 0;
    clock_us += 10000000; data[0] = ' '; return 1;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return true; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; closes++; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; cleanups++; return ESP_OK; }

#ifdef TEST_EASY_STOCK
ha_quote_history_outcome_t ha_quote_easy_history_parse(const char *s, size_t n, const char *symbol, ha_quote_daily_t *out) {
    (void)s; (void)n; (void)symbol; (void)out; return HA_QUOTE_HISTORY_EMPTY;
}
static esp_err_t fetch_test(void) {
    easy_request_t req = {.symbol = "TEST"};
    static ha_easy_stock_history_t history;
    return fetch(&req, &history);
}
static void callback(uint32_t generation, ha_easy_stock_history_t *history) { (void)generation; (void)history; }
static esp_err_t request_test(void) { return ha_easy_stock_request("TEST", 1, callback); }
#else
static esp_err_t fetch_test(void) {
    history_request_t req = {.entity_id = "sensor.test", .numeric = true};
    static ha_history_snapshot_t snapshot;
    return fetch_once(&req, 24, &snapshot);
}
static void callback(uint32_t generation, ha_history_snapshot_t *snapshot, esp_err_t status) {
    (void)generation; (void)snapshot; (void)status;
}
static esp_err_t request_test(void) { return ha_history_request("sensor.test", true, 1, callback); }
#endif

int main(void) {
#ifndef TEST_EASY_STOCK
    setenv("TZ", "Europe/Berlin", 1); tzset();
    struct tm local = {.tm_year = 126, .tm_mon = 9, .tm_mday = 25, .tm_hour = 23, .tm_min = 30, .tm_isdst = -1};
    const time_t autumn = mktime(&local);
    assert(autumn - history_window_start(autumn, 24, true) == 24 * 3600 + 1800);
    assert(autumn - history_window_start(autumn, 24, false) == 24 * 3600);
    assert(autumn - history_window_start(autumn, 6, true) == 6 * 3600);
    local = (struct tm){.tm_year = 126, .tm_mon = 2, .tm_mday = 29, .tm_hour = 23, .tm_min = 30, .tm_isdst = -1};
    const time_t spring = mktime(&local);
    assert(spring - history_window_start(spring, 24, true) == 22 * 3600 + 1800);
    local = (struct tm){.tm_year = 126, .tm_mon = 9, .tm_mday = 26, .tm_hour = 1, .tm_isdst = -1};
    const time_t early = mktime(&local);
    assert(early - history_window_start(early, 6, true) == 3600);
#endif
    gate_busy = true;
    assert(fetch_test() == ESP_ERR_TIMEOUT && clock_us >= 20000000 && clock_us < 20200000 && opens == 0);
    gate_busy = false; clock_us = 0; slow = true;
    assert(fetch_test() == ESP_ERR_TIMEOUT && reads == 3 && clock_us == 30000000);
    assert(closes == 1 && cleanups == 1);
    slow = false; body_size = 1024 * 1024;
    assert(fetch_test() == ESP_ERR_INVALID_SIZE && reads == 3);
    assert(closes == 2 && cleanups == 2);
    body_size = 0; http_status = 401;
    assert(fetch_test() != ESP_OK && auth_blocked);
    unsigned before = opens;
    assert(fetch_test() == ESP_ERR_INVALID_STATE && opens == before);
    fail_task = true;
    assert(request_test() == ESP_ERR_NO_MEM && queues == 0);
    assert(request_test() == ESP_ERR_NO_MEM && queues == 0 && task_attempts == 2);
    fail_task = false;
    assert(request_test() == ESP_OK && queues == 1 && task_attempts == 3);
    vQueueDelete(s_queue); s_queue = NULL;
    puts("auxiliary history: 12 fault-path scenarios passed");
    return 0;
}
