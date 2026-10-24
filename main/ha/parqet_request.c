/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/parqet_request.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "ha/ha_client.h"

#define TAG "parqet_request"
#define PQ_QUEUE 8
#define PQ_CACHE 4
#define PQ_CACHE_US (5LL * 60LL * 1000000LL)
#define PQ_REPLY_TIMEOUT_MS 20000
#define PQ_RETRY_MS 2000
#define PQ_RETRIES 10 /* about 20 s of waiting for the heavy-request gate */
#define PQ_DISPATCH_LOCK_MS 2000

typedef struct {
    char entry_id[APP_MAX_ENTITY_ID_LEN];
    char portfolio_id[APP_MAX_ENTITY_ID_LEN];
    char interval[8];
    void *user;
    parqet_perf_cb_t cb;
} pq_wait_t;

typedef struct {
    bool used;
    char portfolio_id[APP_MAX_ENTITY_ID_LEN];
    char interval[8];
    int64_t at_us;
    parqet_perf_t perf;
} pq_cache_t;

typedef struct {
    uint32_t generation;
    esp_err_t status;
    parqet_perf_t perf;
} pq_done_t;

static pq_wait_t s_wait[PQ_QUEUE];
static size_t s_wait_count;
static pq_wait_t s_current;
static bool s_inflight;
static uint32_t s_generation = 0x70000000U; /* separate from the statistics worker's response tokens */
static int s_retries;
static lv_timer_t *s_timer; /* retry for the gate, or the reply timeout */
static pq_cache_t s_cache[PQ_CACHE];

static void pump(void);

static pq_cache_t *cache_find(const char *portfolio_id, const char *interval)
{
    const int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < PQ_CACHE; i++) {
        pq_cache_t *c = &s_cache[i];
        if (c->used && now - c->at_us < PQ_CACHE_US && strcmp(c->portfolio_id, portfolio_id) == 0 && strcmp(c->interval, interval) == 0) return c;
    }
    return NULL;
}

static void cache_store(const char *portfolio_id, const char *interval, const parqet_perf_t *perf)
{
    size_t slot = 0;
    int64_t oldest = INT64_MAX;
    for (size_t i = 0; i < PQ_CACHE; i++) {
        pq_cache_t *c = &s_cache[i];
        if (c->used && strcmp(c->portfolio_id, portfolio_id) == 0 && strcmp(c->interval, interval) == 0) { slot = i; break; }
        if (!c->used) { slot = i; oldest = INT64_MIN; continue; }
        if (c->at_us < oldest) { oldest = c->at_us; slot = i; }
    }
    pq_cache_t *c = &s_cache[slot];
    c->used = true;
    strlcpy(c->portfolio_id, portfolio_id, sizeof(c->portfolio_id));
    strlcpy(c->interval, interval, sizeof(c->interval));
    c->perf = *perf;
    c->at_us = esp_timer_get_time();
}

static void stop_timer(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
}

/* Gives the result to everyone who waits for the same portfolio and interval: the current request and the queue. */
static void deliver(const char *portfolio_id, const char *interval, esp_err_t status, const parqet_perf_t *perf)
{
    pq_wait_t hit[PQ_QUEUE + 1];
    size_t n = 0;
    if (s_current.cb != NULL && strcmp(s_current.portfolio_id, portfolio_id) == 0 && strcmp(s_current.interval, interval) == 0) hit[n++] = s_current;
    for (size_t i = 0; i < s_wait_count;) {
        if (strcmp(s_wait[i].portfolio_id, portfolio_id) == 0 && strcmp(s_wait[i].interval, interval) == 0) {
            hit[n++] = s_wait[i];
            s_wait[i] = s_wait[--s_wait_count];
        } else {
            i++;
        }
    }
    memset(&s_current, 0, sizeof(s_current));
    for (size_t i = 0; i < n; i++) {
        if (hit[i].cb != NULL) hit[i].cb(hit[i].user, status, perf);
    }
}

static void fail_current(esp_err_t status)
{
    stop_timer();
    s_inflight = false;
    ha_client_cancel_pending_responses_for_user((void *)(uintptr_t)s_generation);
    s_generation++; /* a late reply is dropped */
    char portfolio[APP_MAX_ENTITY_ID_LEN], interval[8];
    strlcpy(portfolio, s_current.portfolio_id, sizeof(portfolio));
    strlcpy(interval, s_current.interval, sizeof(interval));
    ESP_LOGW(TAG, "%s %s: %s", portfolio, interval, esp_err_to_name(status));
    deliver(portfolio, interval, status, NULL);
    pump();
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_timer = NULL; /* a repeat-count-1 timer deletes itself after this call */
    if (s_inflight) fail_current(ESP_ERR_TIMEOUT); /* no reply in time */
}

static void dispatch_on_lvgl(void *arg)
{
    pq_done_t *done = (pq_done_t *)arg;
    if (done->generation == s_generation && s_inflight) {
        stop_timer();
        s_inflight = false;
        char portfolio[APP_MAX_ENTITY_ID_LEN], interval[8];
        strlcpy(portfolio, s_current.portfolio_id, sizeof(portfolio));
        strlcpy(interval, s_current.interval, sizeof(interval));
        strlcpy(done->perf.interval, interval, sizeof(done->perf.interval));
        if (done->status == ESP_OK) cache_store(portfolio, interval, &done->perf);
        deliver(portfolio, interval, done->status, done->status == ESP_OK ? &done->perf : NULL);
        pump();
    }
    free(done);
}

/* Runs on the WebSocket RX task: the reply is only valid during the call, so the figures are taken out here. */
static void reply_cb(bool success, cJSON *result, void *user)
{
    pq_done_t *done = calloc(1, sizeof(*done));
    if (done == NULL) return; /* the reply timeout ends the request */
    done->generation = (uint32_t)(uintptr_t)user;
    done->status = !success ? ESP_FAIL : (parqet_perf_parse(result, "", &done->perf) ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
    bool queued = false;
    if (display_lock(PQ_DISPATCH_LOCK_MS)) {
        queued = lv_async_call(dispatch_on_lvgl, done) == LV_RESULT_OK;
        display_unlock();
    }
    if (!queued) free(done); /* the reply timeout ends the request */
}

static void send_current(void);

static void retry_cb(lv_timer_t *timer)
{
    (void)timer;
    s_timer = NULL; /* a repeat-count-1 timer deletes itself after this call */
    if (s_inflight) send_current();
}

static void send_current(void)
{
    cJSON *cmd = cJSON_CreateObject();
    if (cmd == NULL) {
        fail_current(ESP_ERR_NO_MEM);
        return;
    }
    cJSON_AddStringToObject(cmd, "type", "parqet/get_performance");
    cJSON_AddStringToObject(cmd, "entry_id", s_current.entry_id);
    cJSON_AddStringToObject(cmd, "portfolio_id", s_current.portfolio_id);
    cJSON_AddStringToObject(cmd, "interval", s_current.interval);
    const esp_err_t err = ha_client_send_command_with_response(cmd, reply_cb, (void *)(uintptr_t)s_generation);
    stop_timer();
    if (err == ESP_ERR_INVALID_STATE && s_retries < PQ_RETRIES) { /* another heavy request runs: try again soon */
        s_retries++;
        s_timer = lv_timer_create(retry_cb, PQ_RETRY_MS, NULL);
        if (s_timer == NULL) { fail_current(ESP_ERR_NO_MEM); return; }
        lv_timer_set_repeat_count(s_timer, 1);
        return;
    }
    if (err != ESP_OK) {
        fail_current(err == ESP_ERR_INVALID_STATE ? ESP_ERR_TIMEOUT : ESP_FAIL);
        return;
    }
    s_timer = lv_timer_create(timer_cb, PQ_REPLY_TIMEOUT_MS, NULL);
    if (s_timer == NULL) { fail_current(ESP_ERR_NO_MEM); return; }
    lv_timer_set_repeat_count(s_timer, 1);
}

static void pump(void)
{
    while (!s_inflight && s_wait_count > 0) {
        pq_wait_t next = s_wait[0];
        memmove(&s_wait[0], &s_wait[1], (s_wait_count - 1) * sizeof(s_wait[0]));
        s_wait_count--;
        pq_cache_t *c = cache_find(next.portfolio_id, next.interval);
        if (c != NULL) {
            if (next.cb != NULL) next.cb(next.user, ESP_OK, &c->perf);
            continue;
        }
        s_current = next;
        s_inflight = true;
        s_retries = 0;
        s_generation++;
        send_current();
    }
}

esp_err_t parqet_request_perf(const char *entry_id, const char *portfolio_id, const char *interval, void *user, parqet_perf_cb_t cb)
{
    if (entry_id == NULL || portfolio_id == NULL || interval == NULL || cb == NULL || entry_id[0] == '\0' || portfolio_id[0] == '\0' ||
        interval[0] == '\0' || strlen(interval) >= sizeof(((pq_wait_t *)0)->interval)) {
        return ESP_ERR_INVALID_ARG;
    }
    pq_cache_t *c = cache_find(portfolio_id, interval);
    if (c != NULL) {
        cb(user, ESP_OK, &c->perf);
        return ESP_OK;
    }
    /* one waiting entry per user and interval */
    for (size_t i = 0; i < s_wait_count; i++) {
        if (s_wait[i].user == user && strcmp(s_wait[i].interval, interval) == 0 && strcmp(s_wait[i].portfolio_id, portfolio_id) == 0) return ESP_OK;
    }
    if (s_inflight && s_current.user == user && strcmp(s_current.interval, interval) == 0 && strcmp(s_current.portfolio_id, portfolio_id) == 0) return ESP_OK;
    if (s_wait_count >= PQ_QUEUE) return ESP_ERR_NO_MEM;
    pq_wait_t *w = &s_wait[s_wait_count++];
    memset(w, 0, sizeof(*w));
    strlcpy(w->entry_id, entry_id, sizeof(w->entry_id));
    strlcpy(w->portfolio_id, portfolio_id, sizeof(w->portfolio_id));
    strlcpy(w->interval, interval, sizeof(w->interval));
    w->user = user;
    w->cb = cb;
    pump();
    return ESP_OK;
}

void parqet_request_cancel(void *user)
{
    for (size_t i = 0; i < s_wait_count;) {
        if (s_wait[i].user == user) s_wait[i] = s_wait[--s_wait_count];
        else i++;
    }
    if (s_inflight && s_current.user == user) {
        s_current.cb = NULL; /* the reply still fills the cache */
        s_current.user = NULL;
    }
}
