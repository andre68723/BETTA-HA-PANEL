/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Real request scheduler: timer exhaustion, gate/reply timeout, late replies, coalescing and cancellation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "../../main/ha/parqet_request.c"

struct lv_timer_t { lv_timer_cb_t cb; };
static bool fail_timer;
static esp_err_t send_status;
static unsigned sends, cancelled, delivered, timers;
static void *pending_user;
static ha_client_response_cb_t pending_cb;
static esp_err_t delivered_status;

void host_log(const char *level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
int64_t esp_timer_get_time(void) { return 1000000; }
size_t host_strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t k = n < size ? n : size - 1; memcpy(dst, src, k); dst[k] = 0; }
    return n;
}
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user) {
    (void)period; (void)user;
    if (fail_timer) return NULL;
    lv_timer_t *t = malloc(sizeof(*t)); assert(t); t->cb = cb; timers++; return t;
}
void lv_timer_delete(lv_timer_t *t) { assert(t); timers--; free(t); }
void lv_timer_set_repeat_count(lv_timer_t *t, int count) { assert(t && count == 1); }
esp_err_t ha_client_send_command_with_response(cJSON *cmd, ha_client_response_cb_t cb, void *user) {
    cJSON_Delete(cmd); sends++;
    if (send_status == ESP_OK) { pending_cb = cb; pending_user = user; }
    return send_status;
}
void ha_client_cancel_pending_responses_for_user(void *user) {
    if (pending_user == user) { pending_cb = NULL; pending_user = NULL; cancelled++; }
}
static void received(void *user, esp_err_t status, const parqet_perf_t *perf) {
    (void)user; delivered++; delivered_status = status;
    assert(status != ESP_OK || (perf && perf->value_end == 105));
}
static void request(void *user, const char *portfolio) {
    assert(parqet_request_perf("entry", portfolio, "1m", user, received) == ESP_OK);
}
static void fire_timer(void) {
    lv_timer_t *t = s_timer; assert(t); t->cb(t); lv_timer_delete(t);
}
static void reply(void) {
    cJSON *result = cJSON_Parse("{\"performance\":{\"valuation\":{\"atIntervalStart\":100,\"atIntervalEnd\":105}}}");
    ha_client_response_cb_t cb = pending_cb; void *user = pending_user;
    pending_cb = NULL; pending_user = NULL; assert(cb); cb(true, result, user); cJSON_Delete(result);
}
int main(void) {
    int first, second;
    fail_timer = true;
    request(&first, "no-timer");
    assert(delivered == 1 && delivered_status == ESP_ERR_NO_MEM && !s_inflight && cancelled == 1);
    send_status = ESP_ERR_INVALID_STATE;
    request(&first, "no-retry-timer");
    assert(delivered == 2 && delivered_status == ESP_ERR_NO_MEM && !s_inflight);
    fail_timer = false;
    request(&first, "busy");
    while (s_timer) fire_timer();
    assert(delivered == 3 && delivered_status == ESP_ERR_TIMEOUT && !s_inflight);
    send_status = ESP_OK;
    request(&first, "silence");
    ha_client_response_cb_t late = pending_cb; void *late_user = pending_user;
    fire_timer();
    assert(delivered == 4 && delivered_status == ESP_ERR_TIMEOUT && cancelled == 2);
    late(false, NULL, late_user); /* must neither deliver again nor affect the next request */
    assert(delivered == 4 && !s_inflight);
    request(&first, "shared"); request(&second, "shared");
    unsigned before = sends;
    reply();
    assert(delivered == 6 && sends == before && delivered_status == ESP_OK);
    request(&first, "shared"); /* cached, no send */
    assert(delivered == 7 && sends == before);
    request(&first, "cancel"); request(&second, "cancel");
    parqet_request_cancel(&first); reply();
    assert(delivered == 8 && !s_inflight && timers == 0 && s_wait_count == 0);
    puts("Parqet requests: 8 fault-path scenarios passed");
    return 0;
}
