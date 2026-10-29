/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Production scheduler with more than 24 registrations, visible-page selection and late replies across growth/deletion. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../main/ui/quote_history_feed.c"

struct lv_timer_t { int unused; };
static unsigned allocations, timers, sends, replies;
static int fail_after = -1;
static uint32_t generation;
static ha_history_cb_t response;

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps;
    if (fail_after == 0) { fail_after = -1; return NULL; }
    if (fail_after > 0) fail_after--;
    void *p = calloc(n, size); if (p) allocations++; return p;
}
void heap_caps_free(void *p) { free(p); }
void host_log(const char *level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
int64_t esp_timer_get_time(void) { return 1000000; }
size_t host_strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t k = n < size ? n : size - 1; memcpy(dst, src, k); dst[k] = 0; }
    return n;
}
bool lv_obj_is_visible(const lv_obj_t *obj) { return obj->visible; }
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user) {
    (void)cb; (void)period; (void)user; timers++; return calloc(1, sizeof(lv_timer_t));
}
void lv_timer_delete(lv_timer_t *t) { timers--; free(t); }
esp_err_t ha_history_request_today(const char *id, uint32_t gen, ha_history_cb_t cb) {
    const bool numeric = true;
    (void)id; assert(numeric); sends++; generation = gen; response = cb; return ESP_OK;
}
esp_err_t ha_history_request_week(const char *id, const char *unit, bool cls, const char *device,
                                uint32_t gen, ha_history_week_cb_t cb) {
    (void)id; (void)unit; (void)cls; (void)device; (void)gen; (void)cb; return ESP_FAIL;
}
esp_err_t ha_history_request_range(const char *id, ha_range_kind_t kind, uint32_t gen, ha_history_range_cb_t cb) {
    (void)id; (void)kind; (void)gen; (void)cb; return ESP_FAIL;
}
esp_err_t ha_easy_stock_request(const char *symbol, uint32_t gen, ha_easy_stock_cb_t cb) {
    (void)symbol; (void)gen; (void)cb; return ESP_FAIL;
}
void ha_history_snapshot_free(ha_history_snapshot_t *s) { free(s); }
void ha_history_week_free(ha_history_week_t *s) { free(s); }
void ha_history_range_free(ha_history_range_t *s) { free(s); }
void ha_easy_stock_free(ha_easy_stock_history_t *s) { free(s); }
static void received(void *user, quote_feed_status_t status, const ha_quote_series_t *series) {
    (void)user; (void)series; assert(status == QUOTE_FEED_FAILED); replies++;
}
int main(void) {
    void *handles[80]; lv_obj_t objects[80] = {0};
    for (int i = 0; i < 40; i++) {
        if (i == 24) {
            fail_after = 0;
            assert(quote_history_feed_register(&objects[i], false, received, NULL) == NULL);
            assert(s_capacity == 24); /* a failed growth keeps every previous slot */
        }
        handles[i] = quote_history_feed_register(&objects[i], false, received, NULL); assert(handles[i]);
        char id[40]; snprintf(id, sizeof(id), "sensor.quote_%d", i);
        quote_history_feed_set(handles[i], id, HA_QUOTE_PROVIDER_EASY_STOCK, "TEST", HA_QUOTE_PERIOD_1D, 100);
    }
    objects[39].visible = true; feed_tick(NULL);
    assert(sends == 1 && s_state.inflight == 39);
    /* Grow the arrays with a request outstanding: its handle and generation must remain valid. */
    for (int i = 40; i < 80; i++) {
        handles[i] = quote_history_feed_register(&objects[i], i == 79, received, NULL); assert(handles[i]);
    }
    response(generation, NULL, ESP_FAIL);
    assert(replies == 1 && s_state.inflight == -1);
    assert(strcmp(s_entries[39].entity_id, "sensor.quote_39") == 0);
    objects[79].visible = true;
    quote_history_feed_set(handles[79], "sensor.popup", HA_QUOTE_PROVIDER_EASY_STOCK, "TEST", HA_QUOTE_PERIOD_1D, 100);
    feed_tick(NULL); assert(sends == 2 && s_state.inflight == 79);
    for (int i = 0; i < 80; i++) quote_history_feed_unregister(handles[i]);
    assert(s_capacity == 0 && s_slots == NULL && s_entries == NULL && timers == 0);
    response(generation, NULL, ESP_FAIL); /* a reply after the final tile was deleted */
    assert(replies == 1);
    handles[0] = quote_history_feed_register(&objects[0], true, received, NULL); assert(handles[0]);
    quote_history_feed_unregister(handles[0]); assert(s_capacity == 0 && timers == 0);
    assert(allocations >= 8);
    puts("quote feed: growth, allocation failure, 80 registrations, visible pages, priority popup and late replies passed");
    return 0;
}
