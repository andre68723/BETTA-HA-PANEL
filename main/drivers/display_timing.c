/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 * Original-JC instrumentation. Does not change lock deadlines or rendering.
 */
#include "drivers/display_timing.h"
#include "drivers/display_port.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static DRAM_ATTR portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR display_timing_state_t s_state;
static const char *const names[] = {"lock_wait", "lock_hold", "refresh", "rotation",
                                    "flush_callback", "transfer_submit", "transfer_complete"};

bool display_timing_lock(uint32_t timeout_ms, uintptr_t caller)
{
    int64_t before = esp_timer_get_time();
    bool acquired = display_port_lock(timeout_ms);
    int64_t now = esp_timer_get_time();
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_lock);
    display_timing_record(&s_state, DISPLAY_TIME_LOCK_WAIT, now, now - before, !acquired, caller);
    if (acquired) {
        if (s_state.depth && s_state.owner_task == (uintptr_t)task) {
            ++s_state.depth;
        } else {
            s_state.owner_task = (uintptr_t)task;
            s_state.owner_caller = caller;
            s_state.hold_start_us = now;
            s_state.depth = 1;
            const char *name = pcTaskGetName(task);
            size_t i = 0;
            for (; i + 1 < sizeof(s_state.owner_name) && name[i]; ++i) s_state.owner_name[i] = name[i];
            s_state.owner_name[i] = 0;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return acquired;
}

void display_timing_unlock(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (s_state.depth && s_state.owner_task == (uintptr_t)xTaskGetCurrentTaskHandle()) {
        if (s_state.depth == 1) {
            display_timing_record(&s_state, DISPLAY_TIME_LOCK_HOLD, now,
                                 now - s_state.hold_start_us, false, s_state.owner_caller);
            s_state.owner_task = s_state.owner_caller = 0;
            s_state.owner_name[0] = 0;
        }
        --s_state.depth;
    }
    portEXIT_CRITICAL(&s_lock);
    display_port_unlock();
}

static void refresh_event(lv_event_t *event)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (lv_event_get_code(event) == LV_EVENT_REFR_START) {
        s_state.refresh_start_us = now;
        s_state.refreshing = true;
    } else if (s_state.refreshing) {
        display_timing_record(&s_state, DISPLAY_TIME_REFRESH, now, now - s_state.refresh_start_us, false, 0);
        s_state.refreshing = false;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void summary_timer(void *arg)
{
    (void)arg;
    display_timing_summary();
}

void display_timing_attach(lv_display_t *display)
{
    lv_display_add_event_cb(display, refresh_event, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(display, refresh_event, LV_EVENT_REFR_READY, NULL);
    static esp_timer_handle_t timer;
    if (!timer) {
        const esp_timer_create_args_t config = {.callback = summary_timer, .name = "display_timing"};
        esp_err_t err = esp_timer_create(&config, &timer);
        if (err == ESP_OK) err = esp_timer_start_periodic(timer, 60000000);
        if (err != ESP_OK) ESP_LOGW("display_timing", "summary timer unavailable: %s", esp_err_to_name(err));
    }
}

void display_timing_measure(display_time_kind_t kind, int64_t start_us, bool error)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    display_timing_record(&s_state, kind, now, now - start_us, error, s_state.owner_caller);
    portEXIT_CRITICAL(&s_lock);
}

void display_timing_transfer_begin(void)
{
    portENTER_CRITICAL(&s_lock);
    s_state.transfer_start_us = esp_timer_get_time();
    s_state.transfer_pending = true;
    portEXIT_CRITICAL(&s_lock);
}

void display_timing_transfer_failed(void)
{
    portENTER_CRITICAL(&s_lock);
    s_state.transfer_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

void IRAM_ATTR display_timing_transfer_done_isr(void)
{
    portENTER_CRITICAL_ISR(&s_lock);
    if (s_state.transfer_pending) {
        int64_t now = esp_timer_get_time();
        display_timing_record(&s_state, DISPLAY_TIME_TRANSFER, now, now - s_state.transfer_start_us, false, 0);
        s_state.transfer_pending = false;
    }
    portEXIT_CRITICAL_ISR(&s_lock);
}

static void add_pc(cJSON *object, const char *key, uintptr_t pc)
{
    char text[20];
    snprintf(text, sizeof(text), "0x%08" PRIxPTR, pc);
    cJSON_AddStringToObject(object, key, text);
}

void display_timing_json(cJSON *root)
{
    cJSON *json = cJSON_AddObjectToObject(root, "display_timing");
    if (!json) return;
    display_timing_state_t *copy = heap_caps_malloc(sizeof(*copy), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cJSON_AddBoolToObject(json, "available", copy != NULL);
    if (!copy) return;
    portENTER_CRITICAL(&s_lock);
    *copy = s_state;
    portEXIT_CRITICAL(&s_lock);
    int64_t now = esp_timer_get_time();
    cJSON_AddNumberToObject(json, "slow_threshold_ms", DISPLAY_TIMING_SLOW_US / 1000);
    cJSON_AddStringToObject(json, "application_owner", copy->depth ? copy->owner_name : "untracked");
    add_pc(json, "owner_caller", copy->owner_caller);
    cJSON_AddNumberToObject(json, "owner_depth", copy->depth);
    cJSON_AddNumberToObject(json, "owner_hold_ms", copy->depth ? (double)(now - copy->hold_start_us) / 1000 : 0);
    cJSON_AddBoolToObject(json, "refreshing", copy->refreshing);
    cJSON_AddNumberToObject(json, "refresh_age_ms", copy->refreshing ? (double)(now - copy->refresh_start_us) / 1000 : 0);
    for (int i = 0; i < DISPLAY_TIME_COUNT; ++i) {
        const display_time_metric_t *m = &copy->metric[i];
        cJSON *o = cJSON_AddObjectToObject(json, names[i]);
        if (!o) continue;
        cJSON_AddNumberToObject(o, "count", (double)m->count);
        cJSON_AddNumberToObject(o, "total_ms", (double)m->total_us / 1000);
        cJSON_AddNumberToObject(o, "max_ms", (double)m->maximum_us / 1000);
        cJSON_AddNumberToObject(o, "slow", (double)m->slow);
        cJSON_AddNumberToObject(o, "errors", (double)m->errors);
        add_pc(o, "max_caller", m->maximum_caller);
    }
    cJSON_AddNumberToObject(json, "events_total", (double)copy->events);
    cJSON *events = cJSON_AddArrayToObject(json, "recent_events");
    uint64_t first = copy->events > DISPLAY_TIMING_EVENTS ? copy->events - DISPLAY_TIMING_EVENTS : 0;
    for (uint64_t i = first; events && i < copy->events; ++i) {
        display_time_event_t *e = &copy->event[i % DISPLAY_TIMING_EVENTS];
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "kind", names[e->kind]);
        cJSON_AddNumberToObject(o, "uptime_ms", (double)e->at_us / 1000);
        cJSON_AddNumberToObject(o, "duration_ms", (double)e->duration_us / 1000);
        cJSON_AddBoolToObject(o, "error", e->error);
        cJSON_AddBoolToObject(o, "owner_known", e->owner_known);
        cJSON_AddStringToObject(o, "application_owner", e->owner_known ? e->owner_name : "untracked");
        cJSON_AddBoolToObject(o, "refreshing", e->refreshing);
        add_pc(o, "caller", e->caller);
        add_pc(o, "owner_caller", e->owner_caller);
        cJSON_AddItemToArray(events, o);
    }
    heap_caps_free(copy);
}

void display_timing_summary(void)
{
    display_time_metric_t metrics[DISPLAY_TIME_COUNT];
    static uint64_t previous_errors, previous_slow, previous_all_errors;
    portENTER_CRITICAL(&s_lock);
    memcpy(metrics, s_state.metric, sizeof(metrics));
    portEXIT_CRITICAL(&s_lock);
    uint64_t slow = 0, all_errors = 0;
    for (int i = 0; i < DISPLAY_TIME_COUNT; ++i) {
        slow += metrics[i].slow;
        all_errors += metrics[i].errors;
    }
    uint64_t errors = metrics[DISPLAY_TIME_LOCK_WAIT].errors;
    if (all_errors != previous_all_errors || slow != previous_slow) {
        ESP_LOGW("display_timing", "minute: lock timeouts=%" PRIu64 " errors=%" PRIu64 " slow=%" PRIu64
                 "; boot maxima ms: hold=%" PRIu64 " refresh=%" PRIu64 " rotate=%" PRIu64
                 " transfer=%" PRIu64 " hold_pc=0x%08" PRIxPTR,
                 errors - previous_errors, all_errors - previous_all_errors, slow - previous_slow,
                 metrics[DISPLAY_TIME_LOCK_HOLD].maximum_us / 1000,
                 metrics[DISPLAY_TIME_REFRESH].maximum_us / 1000,
                 metrics[DISPLAY_TIME_ROTATION].maximum_us / 1000,
                 metrics[DISPLAY_TIME_TRANSFER].maximum_us / 1000,
                 metrics[DISPLAY_TIME_LOCK_HOLD].maximum_caller);
    }
    previous_errors = errors;
    previous_all_errors = all_errors;
    previous_slow = slow;
}
