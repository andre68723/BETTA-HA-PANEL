/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The real worker of the 7-day history (main/ha/ha_history_week.c) on the host: pthread-backed FreeRTOS primitives
 * (tests/host/week_stubs), a counting heap and a scriptable Home Assistant WebSocket. It covers what no device test can
 * provoke on demand: a busy heavy-request gate with retries, a reply that never comes, a reply that comes too late,
 * a failing or partial source, a different unit, queue overflow, and that a thousand requests leave no allocation behind. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "ha/ha_client.h"
#include "ha/ha_history_week.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static int g_checks, g_failed;
time_t history_test_time(time_t *out)
{
    const time_t now = 1792058400;
    if (out != NULL) *out = now;
    return now;
}
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---------- FreeRTOS on pthreads ---------- */

typedef struct { pthread_mutex_t m; pthread_cond_t c; unsigned count; unsigned max; } sem_t_;
typedef struct { pthread_mutex_t m; pthread_cond_t c; unsigned char *buf; unsigned len, size, head, count; } queue_t_;

static void abs_deadline(struct timespec *ts, TickType_t ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    sem_t_ *s = calloc(1, sizeof(*s));
    pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL);
    s->count = 1; s->max = 1;
    return s;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    sem_t_ *s = calloc(1, sizeof(*s));
    pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL);
    s->count = 0; s->max = 1;
    return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks)
{
    sem_t_ *s = h;
    struct timespec ts;
    if (ticks != portMAX_DELAY) abs_deadline(&ts, ticks);
    pthread_mutex_lock(&s->m);
    while (s->count == 0) {
        if (ticks == portMAX_DELAY) pthread_cond_wait(&s->c, &s->m);
        else if (pthread_cond_timedwait(&s->c, &s->m, &ts) != 0 && s->count == 0) { pthread_mutex_unlock(&s->m); return pdFALSE; }
    }
    s->count--;
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
    sem_t_ *s = h;
    pthread_mutex_lock(&s->m);
    if (s->count < s->max) s->count++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    queue_t_ *q = calloc(1, sizeof(*q));
    pthread_mutex_init(&q->m, NULL); pthread_cond_init(&q->c, NULL);
    q->buf = calloc(length, item_size); q->len = length; q->size = item_size;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t ticks)
{
    (void)ticks; /* the worker only ever sends with a zero wait */
    queue_t_ *q = h;
    pthread_mutex_lock(&q->m);
    if (q->count == q->len) { pthread_mutex_unlock(&q->m); return pdFALSE; }
    memcpy(q->buf + ((q->head + q->count) % q->len) * q->size, item, q->size);
    q->count++;
    pthread_cond_signal(&q->c);
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t ticks)
{
    (void)ticks; /* portMAX_DELAY */
    queue_t_ *q = h;
    pthread_mutex_lock(&q->m);
    while (q->count == 0) pthread_cond_wait(&q->c, &q->m);
    memcpy(item, q->buf + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->len;
    q->count--;
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { usleep((useconds_t)ticks * 1000u); }

typedef struct { void (*fn)(void *); void *arg; } thread_arg_t;
static void *thread_main(void *p)
{
    thread_arg_t a = *(thread_arg_t *)p;
    free(p);
    a.fn(a.arg);
    return NULL;
}
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int priority,
                                   TaskHandle_t *handle, int core)
{
    (void)name; (void)stack; (void)priority; (void)handle; (void)core;
    thread_arg_t *a = malloc(sizeof(*a));
    a->fn = fn; a->arg = arg;
    pthread_t t;
    if (pthread_create(&t, NULL, thread_main, a) != 0) return pdFALSE;
    pthread_detach(t);
    return pdPASS;
}

/* ---------- counting heap, log, strlcpy ---------- */

static pthread_mutex_t g_heap_m = PTHREAD_MUTEX_INITIALIZER;
static size_t g_live_bytes, g_live_blocks, g_peak_bytes;
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    size_t total = n * size;
    size_t *p = calloc(1, total + sizeof(size_t) * 2);
    if (p == NULL) return NULL;
    p[0] = total;
    pthread_mutex_lock(&g_heap_m);
    g_live_bytes += total; g_live_blocks++;
    if (g_live_bytes > g_peak_bytes) g_peak_bytes = g_live_bytes;
    pthread_mutex_unlock(&g_heap_m);
    return p + 2;
}
void heap_caps_free(void *ptr)
{
    if (ptr == NULL) return;
    size_t *p = (size_t *)ptr - 2;
    pthread_mutex_lock(&g_heap_m);
    g_live_bytes -= p[0]; g_live_blocks--;
    pthread_mutex_unlock(&g_heap_m);
    free(p);
}
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 300000; }
size_t host_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size > 0) { size_t c = n >= size ? size - 1 : n; memcpy(dst, src, c); dst[c] = '\0'; }
    return n;
}
void host_log(const char *level, const char *tag, const char *fmt, ...)
{
    if (getenv("HOST_LOG") == NULL) return;
    va_list ap;
    va_start(ap, fmt);
    printf("%s %s: ", level, tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    default: return "ESP_ERR_OTHER";
    }
}

/* ---------- a scriptable Home Assistant WebSocket ---------- */

typedef enum { MODE_OK, MODE_BUSY, MODE_FAIL_REPLY, MODE_NO_REPLY, MODE_LATE, MODE_SEND_ERROR, MODE_BAD_SHAPE } reply_mode_t;

typedef struct {
    const char *id;
    bool has_mean, has_sum;
    int mean_type;
    const char *unit, *unit_class;
    float per_hour;       /* value of every hourly row */
    reply_mode_t stats_mode;   /* what the statistics call of this ID does */
} source_t;

#define MAX_SCRIPT 16
static struct {
    source_t sources[8];
    int source_count;
    reply_mode_t meta_mode;
    int meta_busy;                 /* the first N metadata sends are refused by the gate */
    int stats_busy;                /* likewise for a statistics send */
    int late_ms;
    int commands;                  /* sends that reached the transport (refused ones too) */
    int refused;
    int cancels;
    char last_stats_cmd[1024];
    char last_meta_cmd[512];
    char all_units[8][64];
    int unit_count;
} g_ws;

static pthread_mutex_t g_ws_m = PTHREAD_MUTEX_INITIALIZER;

bool ha_client_is_connected(void) { return true; }
void ha_client_cancel_pending_responses_for_user(void *user) { (void)user; pthread_mutex_lock(&g_ws_m); g_ws.cancels++; pthread_mutex_unlock(&g_ws_m); }

typedef struct { ha_client_response_cb_t cb; void *user; cJSON *reply; bool success; int delay_ms; } reply_job_t;
static void *reply_thread(void *p)
{
    reply_job_t *j = p;
    usleep((useconds_t)j->delay_ms * 1000u);
    j->cb(j->success, j->reply, j->user);   /* on the "WS RX task": the tree is borrowed for the call only */
    cJSON_Delete(j->reply);
    free(j);
    return NULL;
}
static void start_reply(ha_client_response_cb_t cb, void *user, cJSON *reply, bool success, int delay_ms)
{
    reply_job_t *j = calloc(1, sizeof(*j));
    j->cb = cb; j->user = user; j->reply = reply; j->success = success; j->delay_ms = delay_ms;
    pthread_t t;
    pthread_create(&t, NULL, reply_thread, j);
    pthread_detach(t);
}

static const source_t *find_source(const char *id)
{
    for (int i = 0; i < g_ws.source_count; i++) if (strcmp(g_ws.sources[i].id, id) == 0) return &g_ws.sources[i];
    return NULL;
}

static time_t parse_iso(const char *iso)
{
    struct tm tm = {0};
    strptime(iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return timegm(&tm);
}

esp_err_t ha_client_send_command_with_response(cJSON *cmd, ha_client_response_cb_t cb, void *user)
{
    const char *type = cJSON_GetObjectItemCaseSensitive(cmd, "type")->valuestring;
    const bool meta = strcmp(type, "recorder/get_statistics_metadata") == 0;
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(cmd, "statistic_ids");
    char *printed = cJSON_PrintUnformatted(cmd);
    pthread_mutex_lock(&g_ws_m);
    g_ws.commands++;
    if (meta) snprintf(g_ws.last_meta_cmd, sizeof(g_ws.last_meta_cmd), "%s", printed);
    else snprintf(g_ws.last_stats_cmd, sizeof(g_ws.last_stats_cmd), "%s", printed);
    const cJSON *units = cJSON_GetObjectItemCaseSensitive(cmd, "units");
    if (units != NULL && g_ws.unit_count < 8) { char *u = cJSON_PrintUnformatted(units); snprintf(g_ws.all_units[g_ws.unit_count++], 64, "%s", u); free(u); }
    int *busy = meta ? &g_ws.meta_busy : &g_ws.stats_busy;
    const bool refuse = *busy > 0;
    if (refuse) { (*busy)--; g_ws.refused++; }
    const reply_mode_t meta_mode = g_ws.meta_mode;
    const int late_ms = g_ws.late_ms;
    pthread_mutex_unlock(&g_ws_m);
    free(printed);
    if (refuse) { cJSON_Delete(cmd); return ESP_ERR_INVALID_STATE; } /* the heavy gate: the caller retries */

    cJSON *reply = NULL;
    reply_mode_t mode = MODE_OK;
    if (meta) {
        mode = meta_mode;
        reply = cJSON_CreateArray();
        const cJSON *id;
        cJSON_ArrayForEach(id, ids) {
            const source_t *s = find_source(id->valuestring);
            if (s == NULL) continue;                    /* no statistics: the ID is simply missing */
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "statistic_id", s->id);
            cJSON_AddBoolToObject(o, "has_mean", s->has_mean);
            cJSON_AddNumberToObject(o, "mean_type", s->mean_type);
            cJSON_AddBoolToObject(o, "has_sum", s->has_sum);
            cJSON_AddStringToObject(o, "statistics_unit_of_measurement", s->unit);
            cJSON_AddStringToObject(o, "unit_class", s->unit_class);
            cJSON_AddItemToArray(reply, o);
        }
        if (mode == MODE_BAD_SHAPE) { cJSON_Delete(reply); reply = cJSON_CreateObject(); }
    } else {
        const source_t *s = find_source(ids->child->valuestring);
        mode = s != NULL ? s->stats_mode : MODE_OK;
        reply = cJSON_CreateObject();
        time_t start = parse_iso(cJSON_GetObjectItemCaseSensitive(cmd, "start_time")->valuestring);
        time_t end = parse_iso(cJSON_GetObjectItemCaseSensitive(cmd, "end_time")->valuestring);
        const bool mean = strcmp(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cmd, "types"), 0)->valuestring, "mean") == 0;
        cJSON *rows = cJSON_AddArrayToObject(reply, ids->child->valuestring);
        const char *period = cJSON_GetObjectItemCaseSensitive(cmd, "period")->valuestring;
        const int step_days = strcmp(period, "day") == 0 ? 1 : strcmp(period, "week") == 0 ? 7 : 0;
        if (s != NULL && mode != MODE_BAD_SHAPE && step_days > 0) {
            /* a long period: one row per day or week from the window's start (a local midnight) */
            if (step_days == 7) {
                struct tm local;
                localtime_r(&start, &local);
                start = (time_t)ha_stats_local_day_start(start, -((local.tm_wday + 6) % 7));
            }
            for (time_t t = start; t < end; t = (time_t)ha_stats_local_day_start(t, step_days)) {
                cJSON *r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "start", (double)t * 1000.0);
                cJSON_AddNumberToObject(r, "end", (double)ha_stats_local_day_start(t, step_days) * 1000.0);
                cJSON_AddNumberToObject(r, "mean", s->per_hour);
                cJSON_AddNumberToObject(r, "min", s->per_hour - 1);
                cJSON_AddNumberToObject(r, "max", s->per_hour + 1);
                cJSON_AddItemToArray(rows, r);
            }
        } else if (s != NULL && mode != MODE_BAD_SHAPE) {
            for (time_t t = (start / 3600) * 3600; t + 3600 <= end - 3600 + 3600 && t < (end / 3600) * 3600; t += 3600) {
                if (t < start - 3599) continue;
                cJSON *r = cJSON_CreateObject();
                cJSON_AddNumberToObject(r, "start", (double)t * 1000.0);
                cJSON_AddNumberToObject(r, "end", ((double)t + 3600.0) * 1000.0);
                cJSON_AddNumberToObject(r, mean ? "mean" : "change", s->per_hour);
                if (mean) { cJSON_AddNumberToObject(r, "min", s->per_hour - 1); cJSON_AddNumberToObject(r, "max", s->per_hour + 1); }
                cJSON_AddItemToArray(rows, r);
            }
        }
        if (mode == MODE_BAD_SHAPE) { cJSON_Delete(reply); reply = cJSON_CreateObject(); cJSON_AddStringToObject(reply, ids->child->valuestring, "not rows"); }
    }
    cJSON_Delete(cmd);
    switch (mode) {
    case MODE_NO_REPLY: cJSON_Delete(reply); return ESP_OK;                     /* the answer never comes */
    case MODE_SEND_ERROR: cJSON_Delete(reply); return ESP_FAIL;
    case MODE_FAIL_REPLY: cJSON_Delete(reply); start_reply(cb, user, NULL, false, 5); return ESP_OK;
    case MODE_LATE: start_reply(cb, user, reply, true, late_ms); return ESP_OK;
    default: start_reply(cb, user, reply, true, 5); return ESP_OK;
    }
}

static void ws_reset(void)
{
    pthread_mutex_lock(&g_ws_m);
    memset(&g_ws, 0, sizeof(g_ws));
    pthread_mutex_unlock(&g_ws_m);
}
static void ws_add(const source_t *s) { g_ws.sources[g_ws.source_count++] = *s; }

/* ---------- the callbacks (the LVGL side) ---------- */

static pthread_mutex_t g_cb_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cb_c = PTHREAD_COND_INITIALIZER;
static int g_cb_count;
static ha_history_check_t g_check;
static ha_history_week_t *g_week;
static ha_history_days_t *g_days;
static ha_history_range_t *g_range;
static uint32_t g_gen;

static void on_check(uint32_t gen, const ha_history_check_t *c)
{
    pthread_mutex_lock(&g_cb_m);
    g_check = *c; g_gen = gen; g_cb_count++;
    pthread_cond_broadcast(&g_cb_c);
    pthread_mutex_unlock(&g_cb_m);
}
static void on_week(uint32_t gen, ha_history_week_t *w)
{
    pthread_mutex_lock(&g_cb_m);
    ha_history_week_free(g_week);
    g_week = w; g_gen = gen; g_cb_count++;
    pthread_cond_broadcast(&g_cb_c);
    pthread_mutex_unlock(&g_cb_m);
}
static void on_days(uint32_t gen, ha_history_days_t *d)
{
    pthread_mutex_lock(&g_cb_m);
    ha_history_days_free(g_days);
    g_days = d; g_gen = gen; g_cb_count++;
    pthread_cond_broadcast(&g_cb_c);
    pthread_mutex_unlock(&g_cb_m);
}
static void on_range(uint32_t gen, ha_history_range_t *r)
{
    pthread_mutex_lock(&g_cb_m);
    ha_history_range_free(g_range);
    g_range = r; g_gen = gen; g_cb_count++;
    pthread_cond_broadcast(&g_cb_c);
    pthread_mutex_unlock(&g_cb_m);
}
static bool wait_callbacks(int want, int timeout_ms)
{
    struct timespec ts;
    abs_deadline(&ts, (TickType_t)timeout_ms);
    pthread_mutex_lock(&g_cb_m);
    bool ok = true;
    while (g_cb_count < want) {
        if (pthread_cond_timedwait(&g_cb_c, &g_cb_m, &ts) != 0 && g_cb_count < want) { ok = false; break; }
    }
    pthread_mutex_unlock(&g_cb_m);
    return ok;
}
static void cb_reset(void)
{
    pthread_mutex_lock(&g_cb_m);
    g_cb_count = 0;
    ha_history_week_free(g_week); g_week = NULL;
    ha_history_days_free(g_days); g_days = NULL;
    ha_history_range_free(g_range); g_range = NULL;
    memset(&g_check, 0, sizeof(g_check));
    pthread_mutex_unlock(&g_cb_m);
}

static const source_t S_TEMP = {"sensor.temp", true, false, 1, "\xC2\xB0" "C", "temperature", 21.5f, MODE_OK};
static const source_t S_KWH = {"sensor.kwh", false, true, 0, "kWh", "energy", 0.5f, MODE_OK};
static const source_t S_WATER = {"sensor.water", false, true, 0, "L", "volume", 10.0f, MODE_OK};
static const source_t S_WATER2 = {"sensor.water_b", false, true, 0, "L", "volume", 5.0f, MODE_OK};
static const source_t S_GAS = {"sensor.gas", false, true, 0, "m\xC2\xB3", "volume", 0.1f, MODE_OK};
static const source_t S_POWER = {"sensor.power", true, false, 1, "W", "power", 100.0f, MODE_OK};

static esp_err_t check(const char *id, const char *unit, ha_stats_state_class_t sc)
{
    return ha_history_check_week(id, unit, sc, 7, on_check);
}

static void test_check(void)
{
    ws_reset(); cb_reset();
    ws_add(&S_TEMP); ws_add(&S_KWH);
    CHECK(check("sensor.temp", "\xC2\xB0" "C", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.status == ESP_OK && g_check.result.kind == HA_STATS_KIND_MEASUREMENT && !g_check.result.convert);
    CHECK(g_gen == 7);
    CHECK(g_ws.commands == 1);

    /* the answer is cached: the same question sends nothing */
    cb_reset();
    CHECK(check("sensor.temp", "\xC2\xB0" "C", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.result.kind == HA_STATS_KIND_MEASUREMENT && g_ws.commands == 1);

    /* another unit shown: asks Home Assistant to convert */
    cb_reset();
    CHECK(check("sensor.temp", "\xC2\xB0" "F", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.result.kind == HA_STATS_KIND_MEASUREMENT && g_check.result.convert && strcmp(g_check.result.convert_class, "temperature") == 0);

    /* a counter and an ID without statistics get no selector */
    cb_reset();
    CHECK(check("sensor.kwh", "kWh", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.status == ESP_OK && g_check.result.kind == HA_STATS_KIND_NONE && g_check.result.why == HA_STATS_WHY_NO_MEAN);
    cb_reset();
    CHECK(check("sensor.nothing", "x", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.status == ESP_OK && g_check.result.kind == HA_STATS_KIND_NONE && g_check.result.why == HA_STATS_WHY_NOT_FOUND);
}

static void test_check_failures(void)
{
    /* the gate refuses three times, then lets the check through (poll 10 ms, wait 100 ms) */
    ws_reset(); cb_reset();
    source_t fresh = S_TEMP;
    fresh.id = "sensor.temp_gate";    /* an ID of its own: sensor.temp is already in the eligibility cache */
    ws_add(&fresh);
    g_ws.meta_busy = 3;
    CHECK(check("sensor.temp_gate", "\xC2\xB0" "C", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.status == ESP_OK && g_check.result.kind == HA_STATS_KIND_MEASUREMENT);
    CHECK(g_ws.refused == 3 && g_ws.commands == 4);

    /* the gate stays shut for longer than the wait: no answer, no selector, and the retries are bounded */
    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    g_ws.meta_busy = 1000;
    CHECK(check("sensor.other", "\xC2\xB0" "C", HA_STATS_SC_MEASUREMENT) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_check.status == ESP_ERR_INVALID_STATE);
    CHECK(g_ws.refused >= 5 && g_ws.refused <= 20);   /* about wait / poll attempts: bounded, not endless */

    /* a failed reply, a reply of the wrong shape, a send error and no reply at all */
    const reply_mode_t modes[] = {MODE_FAIL_REPLY, MODE_BAD_SHAPE, MODE_SEND_ERROR, MODE_NO_REPLY};
    const esp_err_t want[] = {ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE, ESP_FAIL, ESP_ERR_TIMEOUT};
    for (int i = 0; i < 4; i++) {
        ws_reset(); cb_reset();
        ws_add(&S_TEMP);
        g_ws.meta_mode = modes[i];
        char id[32];
        snprintf(id, sizeof(id), "sensor.fail%d", i); /* a new ID: not served from the cache */
        CHECK(check(id, "x", HA_STATS_SC_MEASUREMENT) == ESP_OK);
        CHECK(wait_callbacks(1, 3000));
        CHECK(g_check.status == want[i]);
        CHECK(g_check.result.kind == HA_STATS_KIND_NONE);
        if (modes[i] == MODE_NO_REPLY) CHECK(g_ws.cancels == 1);  /* the pending response is dropped */
    }
}

static void test_range(void)
{
    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    CHECK(ha_history_request_range("sensor.temp", HA_RANGE_30_DAYS, 4, on_range) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_range != NULL && g_range->status == ESP_OK && g_range->outcome == HA_STATS_OUTCOME_OK && g_gen == 4);
    CHECK(g_range->series.count >= 29 && g_range->series.count <= 31 && g_range->series.rejected == 0);
    CHECK(strstr(g_ws.last_stats_cmd, "\"period\":\"day\"") != NULL);
    CHECK(strstr(g_ws.last_stats_cmd, "\"types\":[\"mean\",\"min\",\"max\"]") != NULL);

    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    CHECK(ha_history_request_range("sensor.temp", HA_RANGE_12_MONTHS, 5, on_range) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_range->status == ESP_OK && g_range->series.count >= 52 && g_range->series.count <= 53);
    CHECK(g_range->series.rejected == 0);
    CHECK(strstr(g_ws.last_stats_cmd, "\"period\":\"week\"") != NULL);

    /* an entity without statistics, a wrong shape, silence, and arguments */
    ws_reset(); cb_reset();
    CHECK(ha_history_request_range("sensor.unknown", HA_RANGE_30_DAYS, 6, on_range) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_range->status == ESP_OK && g_range->outcome == HA_STATS_OUTCOME_EMPTY && g_range->series.count == 0);
    ws_reset(); cb_reset();
    source_t bad = S_TEMP;
    bad.stats_mode = MODE_BAD_SHAPE;
    ws_add(&bad);
    CHECK(ha_history_request_range("sensor.temp", HA_RANGE_30_DAYS, 7, on_range) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_range->status == ESP_ERR_INVALID_RESPONSE);
    ws_reset(); cb_reset();
    source_t silent = S_TEMP;
    silent.stats_mode = MODE_NO_REPLY;
    ws_add(&silent);
    CHECK(ha_history_request_range("sensor.temp", HA_RANGE_30_DAYS, 8, on_range) == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_range->status == ESP_ERR_TIMEOUT && g_ws.cancels == 1);
    CHECK(ha_history_request_range("Sensor.Bad", HA_RANGE_30_DAYS, 1, on_range) == ESP_ERR_INVALID_ARG);
    CHECK(ha_history_request_range("sensor.temp", (ha_range_kind_t)9, 1, on_range) == ESP_ERR_INVALID_ARG);
    CHECK(ha_history_request_range("sensor.temp", HA_RANGE_30_DAYS, 1, NULL) == ESP_ERR_INVALID_ARG);
}

static esp_err_t week(const char *id, const char *unit, bool convert, const char *klass)
{
    return ha_history_request_week(id, unit, convert, klass, 9, on_week);
}

static void test_week(void)
{
    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week != NULL && g_week->status == ESP_OK && g_week->outcome == HA_STATS_OUTCOME_OK);
    CHECK(g_week->series.valid >= 166 && g_week->series.valid <= 168 && g_week->series.rejected == 0);
    CHECK(g_week->series.flags[g_week->series.slots - 1] == 0);   /* the current hour has no row */
    CHECK(strstr(g_ws.last_stats_cmd, "\"period\":\"hour\"") != NULL);
    CHECK(strstr(g_ws.last_stats_cmd, "\"types\":[\"mean\",\"min\",\"max\"]") != NULL);
    CHECK(strstr(g_ws.last_stats_cmd, "units") == NULL);           /* same unit: no conversion asked */
    CHECK(g_gen == 9);

    /* a different unit: Home Assistant converts into the shown one */
    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    CHECK(week("sensor.temp", "\xC2\xB0" "F", true, "temperature") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week->status == ESP_OK);
    CHECK(strstr(g_ws.last_stats_cmd, "\"units\":{\"temperature\":\"\xC2\xB0" "F\"}") != NULL);

    /* a valid reply without rows is a data state, not an error */
    ws_reset(); cb_reset();
    CHECK(week("sensor.unknown", "x", false, "") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week->status == ESP_OK && g_week->outcome == HA_STATS_OUTCOME_EMPTY && g_week->series.valid == 0);

    /* wrong shape, failed reply, send error, silence */
    const reply_mode_t modes[] = {MODE_BAD_SHAPE, MODE_FAIL_REPLY, MODE_SEND_ERROR, MODE_NO_REPLY};
    const esp_err_t want[] = {ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE, ESP_FAIL, ESP_ERR_TIMEOUT};
    for (int i = 0; i < 4; i++) {
        ws_reset(); cb_reset();
        source_t s = S_TEMP;
        s.stats_mode = modes[i];
        ws_add(&s);
        CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);
        CHECK(wait_callbacks(1, 3000));
        CHECK(g_week != NULL && g_week->status == want[i]);
    }

    /* the gate refuses the statistics call a few times, then it goes through */
    ws_reset(); cb_reset();
    ws_add(&S_TEMP);
    g_ws.stats_busy = 4;
    CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week->status == ESP_OK && g_ws.refused == 4);
}

static void test_late_reply(void)
{
    /* the reply arrives after the worker gave up (timeout 300 ms): it must neither crash nor corrupt the next request */
    ws_reset(); cb_reset();
    source_t slow = S_TEMP;
    slow.stats_mode = MODE_LATE;
    ws_add(&slow);
    g_ws.late_ms = 600;
    CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week->status == ESP_ERR_TIMEOUT && g_ws.cancels == 1);
    /* the next request starts while the late reply is still on its way */
    ws_reset();
    ws_add(&S_TEMP);
    cb_reset();
    CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);
    CHECK(wait_callbacks(1, 3000));
    CHECK(g_week->status == ESP_OK && g_week->outcome == HA_STATS_OUTCOME_OK);
    usleep(700 * 1000);   /* the late reply of the first request has arrived by now and was ignored */
    CHECK(g_cb_count == 1);
}

static esp_err_t days(const char *const *ids, size_t n) { return ha_history_request_days(ids, n, 11, on_days); }

static void test_days(void)
{
    /* two sources of the same unit are summed */
    ws_reset(); cb_reset();
    ws_add(&S_WATER); ws_add(&S_WATER2);
    const char *two[] = {"sensor.water", "sensor.water_b"};
    CHECK(days(two, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days != NULL && g_days->status == ESP_OK && g_days->outcome == HA_STATS_OUTCOME_OK);
    CHECK(g_days->sources_total == 2 && g_days->sources_eligible == 2 && g_days->sources_ok == 2 && strcmp(g_days->unit, "L") == 0);
    CHECK(g_days->days.count == HA_DAYS_COUNT);
    CHECK(g_days->series.sources == 2);
    {   /* the first complete day: 24 hours of 10 + 5 L */
        const ha_stats_day_t *d = &g_days->days.day[1];
        CHECK(d->hours == 24 || d->hours == 23 || d->hours == 25);
        CHECK(d->valid == d->hours && d->complete == d->hours && d->total > 14.9f * (float)d->hours && d->total < 15.1f * (float)d->hours);
    }
    CHECK(g_days->days.day[HA_DAYS_COUNT - 1].today);
    CHECK(g_gen == 11);
    CHECK(g_ws.commands == 3);   /* one metadata call for both, then one statistics call per source */
    CHECK(strstr(g_ws.last_meta_cmd, "\"sensor.water\"") != NULL && strstr(g_ws.last_meta_cmd, "\"sensor.water_b\"") != NULL);
    CHECK(g_ws.unit_count == 0);

    /* the second source fails: the first still draws, the days are partial and the answer says so */
    ws_reset(); cb_reset();
    source_t bad = S_WATER2;
    bad.stats_mode = MODE_FAIL_REPLY;
    ws_add(&S_WATER); ws_add(&bad);
    CHECK(days(two, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days->status == ESP_OK && g_days->outcome == HA_STATS_OUTCOME_OK);
    CHECK(g_days->sources_eligible == 2 && g_days->sources_ok == 1);
    CHECK(g_days->days.day[1].valid == g_days->days.day[1].hours && g_days->days.day[1].complete == 0);   /* a source is missing */

    /* every source fails: no sum, an error status */
    ws_reset(); cb_reset();
    source_t b1 = S_WATER, b2 = S_WATER2;
    b1.stats_mode = MODE_NO_REPLY; b2.stats_mode = MODE_NO_REPLY;
    ws_add(&b1); ws_add(&b2);
    CHECK(days(two, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 8000));
    CHECK(g_days->sources_ok == 0 && g_days->outcome == HA_STATS_OUTCOME_EMPTY && g_days->status == ESP_ERR_TIMEOUT);

    /* a source in another unit of the same class is converted by Home Assistant into the first one's unit */
    ws_reset(); cb_reset();
    source_t cubic = S_GAS;
    cubic.unit_class = "volume";
    ws_add(&S_WATER); ws_add(&cubic);
    const char *mixed[] = {"sensor.water", "sensor.gas"};
    CHECK(days(mixed, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days->sources_ok == 2 && strcmp(g_days->unit, "L") == 0);
    CHECK(g_ws.unit_count == 1 && strstr(g_ws.all_units[0], "\"volume\":\"L\"") != NULL);

    /* another unit class (energy next to volume) is skipped, not summed */
    ws_reset(); cb_reset();
    ws_add(&S_WATER); ws_add(&S_KWH);
    const char *classes[] = {"sensor.water", "sensor.kwh"};
    CHECK(days(classes, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days->sources_total == 2 && g_days->sources_eligible == 1 && g_days->sources_ok == 1 && strcmp(g_days->unit, "L") == 0);

    /* nothing usable: a power sensor is no counter, an unknown ID has no statistics */
    ws_reset(); cb_reset();
    ws_add(&S_POWER);
    const char *none[] = {"sensor.power", "sensor.nothing"};
    CHECK(days(none, 2) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days->status == ESP_OK && g_days->sources_eligible == 0 && g_days->outcome == HA_STATS_OUTCOME_EMPTY);
    CHECK(g_ws.commands == 1);   /* no statistics call at all */

    /* the metadata call itself fails */
    ws_reset(); cb_reset();
    ws_add(&S_WATER);
    g_ws.meta_mode = MODE_FAIL_REPLY;
    const char *one[] = {"sensor.water"};
    CHECK(days(one, 1) == ESP_OK);
    CHECK(wait_callbacks(1, 5000));
    CHECK(g_days->status == ESP_ERR_INVALID_RESPONSE && g_days->sources_eligible == 0);

    /* argument checks */
    CHECK(ha_history_request_days(NULL, 1, 1, on_days) == ESP_ERR_INVALID_ARG);
    CHECK(ha_history_request_days(one, 0, 1, on_days) == ESP_ERR_INVALID_ARG);
    CHECK(ha_history_request_days(one, HA_DAYS_MAX_SOURCES + 1, 1, on_days) == ESP_ERR_INVALID_ARG);
    const char *bad_id[] = {"Not An Entity"};
    CHECK(ha_history_request_days(bad_id, 1, 1, on_days) == ESP_ERR_INVALID_ARG);
    CHECK(ha_history_request_week("sensor.x", "u", true, "", 1, on_week) == ESP_ERR_INVALID_ARG);   /* convert without a class */
}

static void test_queue_overflow(void)
{
    /* the worker is busy with a silent request (300 ms): two more can wait, the next is refused, none leaks */
    ws_reset(); cb_reset();
    source_t silent = S_TEMP;
    silent.stats_mode = MODE_NO_REPLY;
    ws_add(&silent);
    CHECK(week("sensor.temp", "\xC2\xB0" "C", false, "") == ESP_OK);   /* taken by the worker */
    usleep(80 * 1000);
    int accepted = 0, refused = 0;
    for (int i = 0; i < 5; i++) {
        esp_err_t err = week("sensor.temp", "\xC2\xB0" "C", false, "");
        if (err == ESP_OK) accepted++; else if (err == ESP_ERR_TIMEOUT) refused++;
    }
    CHECK(accepted == 2 && refused == 3);
    CHECK(wait_callbacks(3, 6000));   /* all three accepted requests end (as timeouts) */
}

static void test_no_allocation_left_behind(void)
{
    /* a thousand requests of every kind and outcome: after the callbacks free their results nothing stays allocated, and
     * the live bytes at 100 and at 1000 requests are the same: a plateau, not growth */
    ws_reset(); cb_reset();
    ws_add(&S_TEMP); ws_add(&S_WATER); ws_add(&S_WATER2);
    source_t failing = S_GAS;
    failing.stats_mode = MODE_FAIL_REPLY;
    ws_add(&failing);
    const char *three[] = {"sensor.water", "sensor.water_b", "sensor.gas"};
    size_t live_100 = 0, peak_before = 0;
    int done = 0;
    for (int i = 1; i <= 1000; i++) {
        const int kind = i % 4;
        if (kind == 0) check("sensor.temp", "\xC2\xB0" "C", HA_STATS_SC_MEASUREMENT);
        else if (kind == 1) week("sensor.temp", "\xC2\xB0" "C", false, "");
        else if (kind == 2) days(three, 3);
        else week("sensor.unknown", "x", false, "");
        done++;
        if (!wait_callbacks(done, 5000)) { CHECK(0); break; }
        if (i == 100) { cb_reset(); done = 0; pthread_mutex_lock(&g_heap_m); live_100 = g_live_bytes; peak_before = g_peak_bytes; pthread_mutex_unlock(&g_heap_m); }
    }
    cb_reset();
    usleep(50 * 1000);
    pthread_mutex_lock(&g_heap_m);
    size_t live_end = g_live_bytes, blocks_end = g_live_blocks, peak_end = g_peak_bytes;
    pthread_mutex_unlock(&g_heap_m);
    CHECK(blocks_end == 0);               /* every result was freed */
    CHECK(live_end == 0);
    CHECK(live_100 <= 2 * sizeof(ha_history_days_t));   /* at most the result kept by the callback at that moment */
    CHECK(peak_end <= peak_before + sizeof(ha_history_days_t)); /* the peak did not grow with the number of requests */
}

int main(void)
{
    setenv("TZ", "Europe/Berlin", 1);
    tzset();
    test_check();
    test_check_failures();
    test_week();
    test_late_reply();
    test_days();
    test_range();
    test_queue_overflow();
    test_no_allocation_left_behind();
    printf("%s: %d checks, %d failed\n", g_failed == 0 ? "PASS history worker" : "FAIL history worker", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
