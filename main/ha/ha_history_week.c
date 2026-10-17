/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_history_week.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "drivers/display_init.h"

#include "app_config.h"
#include "ha/ha_client.h"

#define WEEK_TASK_STACK 6144
/* The waits are compile-time overridable so the host test can run the real worker with short ones. */
#ifndef WEEK_REPLY_TIMEOUT_MS
#define WEEK_REPLY_TIMEOUT_MS 20000
#endif
#ifndef WEEK_GATE_POLL_MS
#define WEEK_GATE_POLL_MS 250
#endif
#ifndef WEEK_CHECK_GATE_WAIT_MS
#define WEEK_CHECK_GATE_WAIT_MS 10000
#endif
#ifndef WEEK_REQUEST_GATE_WAIT_MS
#define WEEK_REQUEST_GATE_WAIT_MS 45000
#endif
#define WEEK_DISPATCH_LOCK_MS 2000
#define WEEK_CHECK_CACHE_US (10LL * 60LL * 1000000LL)
#define WEEK_CHECK_CACHE_SLOTS 8

static const char *TAG = "ha_week";

/* One work item of the worker, and the single WebSocket operations it is made of. */
typedef enum { WORK_CHECK, WORK_WEEK, WORK_DAYS, WORK_RANGE } work_kind_t;
typedef enum { OP_METADATA, OP_MEAN, OP_CHANGE, OP_PERIOD } op_kind_t;

typedef struct {
    work_kind_t kind;
    char ids[HA_DAYS_MAX_SOURCES][APP_MAX_ENTITY_ID_LEN];
    uint8_t id_count;
    char display_unit[HA_STATS_UNIT_MAX];
    ha_stats_state_class_t state_class;
    bool convert;
    char convert_class[HA_STATS_CLASS_MAX];
    uint32_t generation;
    ha_history_check_cb_t check_cb;
    ha_history_week_cb_t week_cb;
    ha_history_days_cb_t days_cb;
    ha_range_kind_t range_kind;
    ha_history_range_cb_t range_cb;
} week_request_t;

typedef struct {
    week_request_t request;
    ha_history_check_t check;
    ha_history_week_t *week;
    ha_history_days_t *days;
    ha_history_range_t *range;
} week_dispatch_t;

typedef struct {
    bool used;
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char display_unit[HA_STATS_UNIT_MAX];
    ha_stats_state_class_t state_class;
    ha_stats_eligibility_t result;
    int64_t at_us;
} check_cache_t;

typedef struct {
    op_kind_t kind;
    const char *ids[HA_DAYS_MAX_SOURCES];
    uint8_t id_count;
    const ha_stats_window_t *window;
    ha_stats_series_t *series;   /* parse target of OP_MEAN / OP_CHANGE */
    ha_range_series_t *range;    /* parse target of OP_PERIOD */
    ha_range_kind_t range_kind;
    const char *period;          /* OP_PERIOD: "day" or "week" (every other statistics operation asks for hours) */
    const char *units_class;     /* ask HA to convert: units {units_class: units_target} */
    const char *units_target;
    int gate_wait_ms;
} op_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_reply;
static SemaphoreHandle_t s_lock;   /* callback body versus the worker abandoning an operation */
static check_cache_t s_cache[WEEK_CHECK_CACHE_SLOTS];

/* The operation in flight. Only the worker starts one; the WS callback fills it under s_lock. */
static uint32_t s_op;
static op_kind_t s_op_kind;
static char s_op_ids[HA_DAYS_MAX_SOURCES][APP_MAX_ENTITY_ID_LEN];
static uint8_t s_op_id_count;
static ha_stats_meta_t s_op_metas[HA_DAYS_MAX_SOURCES];
static const ha_stats_window_t *s_op_window;
static ha_stats_series_t *s_op_series;
static ha_range_series_t *s_op_range;
static ha_range_kind_t s_op_range_kind;
static ha_stats_outcome_t s_op_outcome;
static bool s_op_ok;

static void *psram_calloc(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void ha_history_week_free(ha_history_week_t *week)
{
    if (week != NULL) heap_caps_free(week);
}

void ha_history_days_free(ha_history_days_t *days)
{
    if (days != NULL) heap_caps_free(days);
}

void ha_history_range_free(ha_history_range_t *range)
{
    if (range != NULL) heap_caps_free(range);
}

static void reply_cb(bool success, cJSON *result, void *user)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if ((uint32_t)(uintptr_t)user != s_op) {
        xSemaphoreGive(s_lock);
        return; /* an operation the worker already gave up on */
    }
    s_op_ok = false;
    if (success && result != NULL) {
        if (s_op_kind == OP_METADATA) {
            s_op_ok = true;
            for (uint8_t i = 0; i < s_op_id_count; i++) {
                if (!ha_stats_parse_metadata(result, s_op_ids[i], &s_op_metas[i])) s_op_ok = false;
            }
        } else if (s_op_kind == OP_PERIOD) {
            if (s_op_range != NULL && s_op_window != NULL) {
                s_op_outcome = ha_stats_parse_range(result, s_op_ids[0], s_op_window, s_op_range_kind, s_op_range);
                s_op_ok = s_op_outcome != HA_STATS_OUTCOME_INVALID;
            }
        } else if (s_op_series != NULL && s_op_window != NULL) {
            const bool mean = s_op_kind == OP_MEAN;
            s_op_outcome = ha_stats_parse_hourly(result, s_op_ids[0], s_op_window,
                                                 mean ? HA_STATS_VALUE_MEAN : HA_STATS_VALUE_CHANGE, mean, s_op_series);
            s_op_ok = s_op_outcome != HA_STATS_OUTCOME_INVALID;
        }
    }
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_reply);
}

static bool iso_time(int64_t epoch, char *out, size_t out_len)
{
    time_t t = (time_t)epoch;
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) return false;
    return strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &tm) > 0;
}

/* The client takes the command whether or not it is sent. */
static esp_err_t send_op(const op_t *op, uint32_t op_id)
{
    cJSON *cmd = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    if (cmd == NULL || ids == NULL) {
        cJSON_Delete(cmd);
        cJSON_Delete(ids);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < op->id_count; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(op->ids[i]));
    if (op->kind == OP_METADATA) {
        cJSON_AddStringToObject(cmd, "type", "recorder/get_statistics_metadata");
        cJSON_AddItemToObject(cmd, "statistic_ids", ids);
    } else {
        char start_iso[32], end_iso[32];
        if (!iso_time(op->window->start_epoch, start_iso, sizeof(start_iso)) ||
            !iso_time(op->window->end_epoch, end_iso, sizeof(end_iso))) {
            cJSON_Delete(cmd);
            cJSON_Delete(ids);
            return ESP_FAIL;
        }
        cJSON_AddStringToObject(cmd, "type", "recorder/statistics_during_period");
        cJSON_AddStringToObject(cmd, "start_time", start_iso);
        cJSON_AddStringToObject(cmd, "end_time", end_iso);
        cJSON_AddStringToObject(cmd, "period", op->kind == OP_PERIOD && op->period != NULL ? op->period : "hour");
        cJSON_AddItemToObject(cmd, "statistic_ids", ids);
        cJSON *types = cJSON_AddArrayToObject(cmd, "types");
        if (types == NULL) {
            cJSON_Delete(cmd);
            return ESP_ERR_NO_MEM;
        }
        if (op->kind == OP_MEAN || op->kind == OP_PERIOD) {
            cJSON_AddItemToArray(types, cJSON_CreateString("mean"));
            cJSON_AddItemToArray(types, cJSON_CreateString("min"));
            cJSON_AddItemToArray(types, cJSON_CreateString("max"));
        } else {
            cJSON_AddItemToArray(types, cJSON_CreateString("change"));
        }
        if (op->units_class != NULL && op->units_class[0] != '\0') {
            cJSON *units = cJSON_AddObjectToObject(cmd, "units");
            if (units == NULL) {
                cJSON_Delete(cmd);
                return ESP_ERR_NO_MEM;
            }
            cJSON_AddStringToObject(units, op->units_class, op->units_target);
        }
    }
    return ha_client_send_command_with_response(cmd, reply_cb, (void *)(uintptr_t)op_id);
}

/* Runs one operation: waits for the heavy gate for a bounded time, then for the reply. */
static esp_err_t run_op(const op_t *op)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_op_kind = op->kind;
    s_op_id_count = op->id_count;
    for (uint8_t i = 0; i < op->id_count; i++) strlcpy(s_op_ids[i], op->ids[i], sizeof(s_op_ids[i]));
    memset(s_op_metas, 0, sizeof(s_op_metas));
    s_op_window = op->window;
    s_op_series = op->series;
    s_op_range = op->range;
    s_op_range_kind = op->range_kind;
    s_op_outcome = HA_STATS_OUTCOME_INVALID;
    s_op_ok = false;
    uint32_t op_id = ++s_op;
    xSemaphoreGive(s_lock);
    xSemaphoreTake(s_reply, 0);

    int waited_ms = 0;
    esp_err_t err;
    for (;;) {
        err = send_op(op, op_id);
        if (err != ESP_ERR_INVALID_STATE || waited_ms >= op->gate_wait_ms || !ha_client_is_connected()) break;
        vTaskDelay(pdMS_TO_TICKS(WEEK_GATE_POLL_MS));
        waited_ms += WEEK_GATE_POLL_MS;
    }
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(s_reply, pdMS_TO_TICKS(WEEK_REPLY_TIMEOUT_MS)) != pdTRUE) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_op++; /* a late reply is ignored; waiting for s_lock lets a running callback finish first */
        s_op_series = NULL;
        s_op_range = NULL;
        s_op_window = NULL;
        xSemaphoreGive(s_lock);
        ha_client_cancel_pending_responses_for_user((void *)(uintptr_t)op_id);
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_op_ok;
    s_op_series = NULL;
    s_op_range = NULL;
    s_op_window = NULL;
    xSemaphoreGive(s_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static void dispatch_on_lvgl(void *user)
{
    week_dispatch_t *dispatch = (week_dispatch_t *)user;
    switch (dispatch->request.kind) {
    case WORK_CHECK:
        dispatch->request.check_cb(dispatch->request.generation, &dispatch->check);
        break;
    case WORK_WEEK:
        dispatch->request.week_cb(dispatch->request.generation, dispatch->week); /* the callback owns the result */
        break;
    case WORK_DAYS:
        dispatch->request.days_cb(dispatch->request.generation, dispatch->days);
        break;
    case WORK_RANGE:
        dispatch->request.range_cb(dispatch->request.generation, dispatch->range);
        break;
    }
    free(dispatch);
}

static void dispatch(const week_request_t *req, const ha_history_check_t *check, ha_history_week_t *week,
                     ha_history_days_t *days, ha_history_range_t *range)
{
    week_dispatch_t *d = calloc(1, sizeof(*d));
    if (d == NULL) {
        ha_history_week_free(week);
        ha_history_days_free(days);
        ha_history_range_free(range);
        return;
    }
    d->request = *req;
    if (check != NULL) d->check = *check;
    d->week = week;
    d->days = days;
    d->range = range;
    /* lv_async_call() inserts into LVGL's timer list: take the display lock from this task (see ha_history.c). */
    bool queued = false;
    if (display_lock(WEEK_DISPATCH_LOCK_MS)) {
        queued = lv_async_call(dispatch_on_lvgl, d) == LV_RESULT_OK;
        display_unlock();
    }
    if (!queued) {
        ha_history_week_free(week);
        ha_history_days_free(days);
        ha_history_range_free(range);
        free(d);
    }
}

static check_cache_t *cache_find(const week_request_t *req)
{
    const int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < WEEK_CHECK_CACHE_SLOTS; i++) {
        check_cache_t *c = &s_cache[i];
        if (c->used && now - c->at_us < WEEK_CHECK_CACHE_US && c->state_class == req->state_class &&
            strcmp(c->entity_id, req->ids[0]) == 0 && strcmp(c->display_unit, req->display_unit) == 0) {
            return c;
        }
    }
    return NULL;
}

static void cache_store(const week_request_t *req, const ha_stats_eligibility_t *result)
{
    size_t slot = 0;
    int64_t oldest = INT64_MAX;
    for (size_t i = 0; i < WEEK_CHECK_CACHE_SLOTS; i++) {
        if (!s_cache[i].used) { slot = i; break; }
        if (s_cache[i].at_us < oldest) { oldest = s_cache[i].at_us; slot = i; }
    }
    check_cache_t *c = &s_cache[slot];
    c->used = true;
    strlcpy(c->entity_id, req->ids[0], sizeof(c->entity_id));
    strlcpy(c->display_unit, req->display_unit, sizeof(c->display_unit));
    c->state_class = req->state_class;
    c->result = *result;
    c->at_us = esp_timer_get_time();
}

static void handle_check(const week_request_t *req)
{
    ha_history_check_t check = {0};
    const int64_t started_us = esp_timer_get_time();
    check_cache_t *cached = cache_find(req);
    bool from_cache = cached != NULL;
    if (from_cache) {
        check.status = ESP_OK;
        check.result = cached->result;
    } else {
        op_t op = {.kind = OP_METADATA, .ids = {req->ids[0]}, .id_count = 1, .gate_wait_ms = WEEK_CHECK_GATE_WAIT_MS};
        check.status = run_op(&op);
        if (check.status == ESP_OK) {
            check.result = ha_stats_classify(&s_op_metas[0], req->state_class, req->display_unit);
            cache_store(req, &check.result);
        }
    }
    ESP_LOGI(TAG, "check %s: %s, kind %d, why %d%s, %d ms", req->ids[0], esp_err_to_name(check.status),
             (int)check.result.kind, (int)check.result.why, from_cache ? " (cached)" : "",
             (int)((esp_timer_get_time() - started_us) / 1000));
    dispatch(req, &check, NULL, NULL, NULL);
}

static void handle_week(const week_request_t *req)
{
    ha_history_week_t *week = psram_calloc(sizeof(*week));
    if (week == NULL) return;
    const int64_t started_us = esp_timer_get_time();
    const time_t now = time(NULL);
    if (now < 1700000000 || !ha_stats_week_window((int64_t)now, &week->window)) {
        week->status = ESP_ERR_INVALID_STATE; /* the clock is not set: hours cannot be placed */
    } else {
        op_t op = {.kind = OP_MEAN, .ids = {req->ids[0]}, .id_count = 1, .window = &week->window,
                   .series = &week->series, .gate_wait_ms = WEEK_REQUEST_GATE_WAIT_MS};
        if (req->convert) {
            op.units_class = req->convert_class;
            op.units_target = req->display_unit;
        }
        week->status = run_op(&op);
        week->outcome = s_op_outcome;
    }
    ESP_LOGI(TAG, "week %s: %s, outcome %d, %u of %u hours, %u rejected, %d ms, internal heap %u free",
             req->ids[0], esp_err_to_name(week->status), (int)week->outcome, (unsigned)week->series.valid,
             (unsigned)week->series.slots, (unsigned)week->series.rejected,
             (int)((esp_timer_get_time() - started_us) / 1000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    dispatch(req, NULL, week, NULL, NULL);
}

/* Daily consumption: metadata of all sources in one call, then the hourly change of each eligible source. */
static void handle_days(const week_request_t *req)
{
    ha_history_days_t *days = psram_calloc(sizeof(*days));
    ha_stats_series_t *one = psram_calloc(sizeof(*one));
    if (days == NULL || one == NULL) {
        ha_history_days_free(days);
        heap_caps_free(one);
        return;
    }
    const int64_t started_us = esp_timer_get_time();
    days->sources_total = req->id_count;
    const time_t now = time(NULL);
    if (now < 1700000000 || !ha_stats_days_window((int64_t)now, HA_DAYS_COUNT, &days->window)) {
        days->status = ESP_ERR_INVALID_STATE;
        goto done;
    }
    {
        op_t meta_op = {.kind = OP_METADATA, .id_count = req->id_count, .gate_wait_ms = WEEK_CHECK_GATE_WAIT_MS};
        for (uint8_t i = 0; i < req->id_count; i++) meta_op.ids[i] = req->ids[i];
        days->status = run_op(&meta_op);
    }
    if (days->status != ESP_OK) goto done;

    ha_stats_meta_t metas[HA_DAYS_MAX_SOURCES];
    memcpy(metas, s_op_metas, sizeof(metas));
    const char *first_class = NULL;
    esp_err_t last_error = ESP_OK;
    for (uint8_t i = 0; i < req->id_count; i++) {
        if (ha_stats_classify_counter(&metas[i]).kind != HA_STATS_KIND_CONSUMPTION) {
            ESP_LOGW(TAG, "days %s: not a usable counter (found %d, sum %d, class %s)", req->ids[i], (int)metas[i].found,
                     (int)metas[i].has_sum, metas[i].unit_class);
            continue;
        }
        if (first_class == NULL) {
            first_class = metas[i].unit_class;
            strlcpy(days->unit, metas[i].unit, sizeof(days->unit));
        } else if (strcmp(first_class, metas[i].unit_class) != 0) {
            ESP_LOGW(TAG, "days %s: unit class %s differs from %s, source skipped", req->ids[i], metas[i].unit_class, first_class);
            continue;
        }
        days->sources_eligible++;
        op_t op = {.kind = OP_CHANGE, .ids = {req->ids[i]}, .id_count = 1, .window = &days->window, .series = one,
                   .gate_wait_ms = WEEK_REQUEST_GATE_WAIT_MS};
        if (strcmp(metas[i].unit, days->unit) != 0) {
            op.units_class = metas[i].unit_class; /* HA converts this source into the unit of the sums */
            op.units_target = days->unit;
        }
        esp_err_t err = run_op(&op);
        if (err != ESP_OK || s_op_outcome != HA_STATS_OUTCOME_OK) {
            if (err != ESP_OK) last_error = err;
            ESP_LOGW(TAG, "days %s: %s, outcome %d", req->ids[i], esp_err_to_name(err), (int)s_op_outcome);
            continue;
        }
        if (days->sources_ok == 0) days->series = *one;
        else if (!ha_stats_series_add(&days->series, one)) continue;
        days->sources_ok++;
    }
    /* a day is complete only when every eligible source had a value: a source that failed makes all days partial */
    if (days->sources_ok > 0) days->series.sources = days->sources_eligible;
    if (days->sources_eligible == 0) {
        days->status = ESP_OK;
        days->outcome = HA_STATS_OUTCOME_EMPTY;
    } else if (days->sources_ok == 0) {
        days->status = last_error != ESP_OK ? last_error : ESP_OK;
        days->outcome = HA_STATS_OUTCOME_EMPTY;
    } else if (ha_stats_sum_days(&days->series, (int64_t)now, HA_DAYS_COUNT, &days->days)) {
        days->outcome = HA_STATS_OUTCOME_OK;
    } else {
        days->status = ESP_ERR_INVALID_STATE;
    }
done:
    ESP_LOGI(TAG, "days: %s, outcome %d, %u of %u sources (%u eligible), unit %s, %d ms, internal heap %u free",
             esp_err_to_name(days->status), (int)days->outcome, (unsigned)days->sources_ok, (unsigned)days->sources_total,
             (unsigned)days->sources_eligible, days->unit, (int)((esp_timer_get_time() - started_us) / 1000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    heap_caps_free(one);
    dispatch(req, NULL, NULL, days, NULL);
}

static void handle_range(const week_request_t *req)
{
    ha_history_range_t *range = psram_calloc(sizeof(*range));
    if (range == NULL) return;
    const int64_t started_us = esp_timer_get_time();
    const time_t now = time(NULL);
    const char *period = "day";
    if (now < 1700000000 || !ha_stats_range_window((int64_t)now, req->range_kind, &range->window, &period)) {
        range->status = ESP_ERR_INVALID_STATE;
    } else {
        op_t op = {.kind = OP_PERIOD, .ids = {req->ids[0]}, .id_count = 1, .window = &range->window, .range = &range->series,
                   .range_kind = req->range_kind, .period = period, .gate_wait_ms = WEEK_REQUEST_GATE_WAIT_MS};
        range->status = run_op(&op);
        range->outcome = s_op_outcome;
    }
    ESP_LOGI(TAG, "range %s (%s): %s, outcome %d, %u points, %u rejected, %d ms", req->ids[0], period,
             esp_err_to_name(range->status), (int)range->outcome, (unsigned)range->series.count,
             (unsigned)range->series.rejected, (int)((esp_timer_get_time() - started_us) / 1000));
    dispatch(req, NULL, NULL, NULL, range);
}

static void week_task(void *arg)
{
    (void)arg;
    week_request_t req;
    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        switch (req.kind) {
        case WORK_CHECK: handle_check(&req); break;
        case WORK_WEEK: handle_week(&req); break;
        case WORK_DAYS: handle_days(&req); break;
        case WORK_RANGE: handle_range(&req); break;
        }
    }
}

static esp_err_t ensure_worker(void)
{
    if (s_queue != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_reply = xSemaphoreCreateBinary();
    s_queue = xQueueCreate(2, sizeof(week_request_t)); /* two items can wait, no more */
    if (s_lock == NULL || s_reply == NULL || s_queue == NULL) {
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    BaseType_t created;
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    created = xTaskCreatePinnedToCoreWithCaps(week_task, "ha_week", WEEK_TASK_STACK, NULL, 4, NULL, 1,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    created = xTaskCreatePinnedToCore(week_task, "ha_week", WEEK_TASK_STACK, NULL, 4, NULL, 1);
#endif
    if (created != pdPASS) {
        ESP_LOGW(TAG, "worker not created");
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool entity_id_ok(const char *id)
{
    if (id == NULL || strlen(id) >= APP_MAX_ENTITY_ID_LEN) return false;
    const char *dot = strchr(id, '.');
    if (dot == NULL || dot == id || dot[1] == '\0') return false;
    for (const char *p = id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || p == dot)) return false;
    }
    return true;
}

static esp_err_t enqueue(week_request_t *req)
{
    esp_err_t err = ensure_worker();
    if (err != ESP_OK) return err;
    return xQueueSend(s_queue, req, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ha_history_check_week(const char *entity_id, const char *display_unit, ha_stats_state_class_t state_class,
                                uint32_t generation, ha_history_check_cb_t callback)
{
    if (!entity_id_ok(entity_id) || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (display_unit != NULL && strlen(display_unit) >= HA_STATS_UNIT_MAX) return ESP_ERR_INVALID_ARG;
    week_request_t req = {.kind = WORK_CHECK, .id_count = 1, .state_class = state_class, .generation = generation,
                          .check_cb = callback};
    strlcpy(req.ids[0], entity_id, sizeof(req.ids[0]));
    strlcpy(req.display_unit, display_unit != NULL ? display_unit : "", sizeof(req.display_unit));
    return enqueue(&req);
}

esp_err_t ha_history_request_week(const char *entity_id, const char *display_unit, bool convert,
                                  const char *convert_class, uint32_t generation, ha_history_week_cb_t callback)
{
    if (!entity_id_ok(entity_id) || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (display_unit == NULL || strlen(display_unit) >= HA_STATS_UNIT_MAX) return ESP_ERR_INVALID_ARG;
    if (convert && (convert_class == NULL || convert_class[0] == '\0' || strlen(convert_class) >= HA_STATS_CLASS_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }
    week_request_t req = {.kind = WORK_WEEK, .id_count = 1, .convert = convert, .generation = generation, .week_cb = callback};
    strlcpy(req.ids[0], entity_id, sizeof(req.ids[0]));
    strlcpy(req.display_unit, display_unit, sizeof(req.display_unit));
    if (convert) strlcpy(req.convert_class, convert_class, sizeof(req.convert_class));
    return enqueue(&req);
}

esp_err_t ha_history_request_days(const char *const *ids, size_t count, uint32_t generation, ha_history_days_cb_t callback)
{
    if (ids == NULL || callback == NULL || count == 0 || count > HA_DAYS_MAX_SOURCES) return ESP_ERR_INVALID_ARG;
    week_request_t req = {.kind = WORK_DAYS, .id_count = (uint8_t)count, .generation = generation, .days_cb = callback};
    for (size_t i = 0; i < count; i++) {
        if (!entity_id_ok(ids[i])) return ESP_ERR_INVALID_ARG;
        strlcpy(req.ids[i], ids[i], sizeof(req.ids[i]));
    }
    return enqueue(&req);
}

esp_err_t ha_history_request_range(const char *entity_id, ha_range_kind_t kind, uint32_t generation, ha_history_range_cb_t callback)
{
    if (!entity_id_ok(entity_id) || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (kind != HA_RANGE_30_DAYS && kind != HA_RANGE_12_MONTHS) return ESP_ERR_INVALID_ARG;
    week_request_t req = {.kind = WORK_RANGE, .id_count = 1, .generation = generation, .range_kind = kind, .range_cb = callback};
    strlcpy(req.ids[0], entity_id, sizeof(req.ids[0]));
    return enqueue(&req);
}
