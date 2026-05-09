/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/ui_widget_factory.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ui/fonts/app_text_fonts.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/theme/theme_default.h"

#define GRAPH_POINTS_MIN 16
#define GRAPH_POINTS_MAX 64
#define GRAPH_DEFAULT_POINT_COUNT 32

/* Upper bound for the internally computed slot count used by smooth modes.
   Higher values produce smoother curves at the cost of a bit more LVGL work. */
#define GRAPH_SMOOTH_POINTS_MAX 160
#define GRAPH_SMOOTH_POINTS_MIN 32

#define GRAPH_BAR_BUCKETS_MAX 120
#define GRAPH_BAR_BUCKET_MIN_DEFAULT 15

#define GRAPH_DISPLAY_MODE_LINE         "line"
#define GRAPH_DISPLAY_MODE_LINE_SMOOTH  "line_smooth"
#define GRAPH_DISPLAY_MODE_LINE_SMOOTH_POINTS "line_smooth_points"
#define GRAPH_DISPLAY_MODE_BARS         "bars"

#define GRAPH_TIME_WINDOW_MIN_MIN 1
#define GRAPH_TIME_WINDOW_MIN_MAX 1440
#define GRAPH_DEFAULT_TIME_WINDOW_MIN 120

#define GRAPH_HISTORY_MIN_INTERVAL_SEC 2U
#define GRAPH_HISTORY_RETENTION_MIN 1440U
#define GRAPH_HISTORY_RETENTION_SEC (GRAPH_HISTORY_RETENTION_MIN * 60U)
/* Cap samples by count as well as by age. Each sample is 8 bytes, so the
   default cap uses ~32 KB RAM per widget. When the buffer fills before the
   24h retention kicks in, graph_history_decimate_oldest merges the two
   oldest samples instead of dropping them outright, so the graph keeps
   covering the full 24h window at progressively coarser resolution at
   the old end while the newest data stays at full event resolution. */
#define GRAPH_HISTORY_MAX_SAMPLES 4096
#define GRAPH_HISTORY_SAVE_INTERVAL_SEC 120U
#define GRAPH_HISTORY_PERSIST_QUEUE_LEN 4U
#define GRAPH_HISTORY_PERSIST_TASK_STACK 4096
#define GRAPH_HISTORY_PERSIST_TASK_PRIO 2

#define GRAPH_HISTORY_FILE_MAGIC 0x47525048U
#define GRAPH_HISTORY_FILE_VERSION 1U
#define GRAPH_HISTORY_DIR "/littlefs/graphs"
#define GRAPH_HISTORY_PATH_MAX 128

#define GRAPH_VALUE_SCALE 10
#define GRAPH_VALID_EPOCH_MIN 1609459200U

typedef struct {
    uint32_t bucket_ts;
    float value;
} graph_sample_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t count;
} graph_history_file_header_t;

typedef struct {
    char history_path[GRAPH_HISTORY_PATH_MAX];
    int history_count;
    uint32_t bucket_ts;
    graph_sample_t history[GRAPH_HISTORY_MAX_SAMPLES];
} graph_history_persist_job_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *meta_label;
    lv_obj_t *chart;
    lv_chart_series_t *series;

    char unit[16];
    char history_path[GRAPH_HISTORY_PATH_MAX];
    graph_sample_t history[GRAPH_HISTORY_MAX_SAMPLES];
    int history_count;
    bool history_dirty;
    uint32_t last_persist_bucket_ts;

    int configured_point_count;
    int point_count;
    int time_window_min;
    int history_offset_min;

    /* Display mode state: one of GRAPH_DISPLAY_MODE_*. */
    char display_mode[24];
    int bar_bucket_min;

    /* Real-sample pixel coordinates cached during rebuild so the draw
       callback can render dots at those positions for line_smooth_points. */
    struct {
        int16_t x_id;   /* chart value index (0..point_count-1) */
        float value;
    } real_points[GRAPH_HISTORY_MAX_SAMPLES];
    int real_points_count;
    int32_t range_min_scaled;
    int32_t range_max_scaled;

    lv_color_t line_color;
    bool unavailable;
} w_graph_ctx_t;

static const char *TAG = "w_graph";
static bool s_graph_history_dir_ready = false;
static QueueHandle_t s_graph_persist_queue = NULL;
static TaskHandle_t s_graph_persist_task = NULL;

static bool graph_state_is_unavailable(const char *state_text)
{
    if (state_text == NULL || state_text[0] == '\0') {
        return true;
    }
    return strcmp(state_text, "unavailable") == 0 || strcmp(state_text, "unknown") == 0;
}

static bool graph_parse_float_relaxed(const char *text, float *out_value)
{
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return false;
    }

    char buf[40] = {0};
    size_t n = strnlen(text, sizeof(buf) - 1U);
    for (size_t i = 0; i < n; i++) {
        buf[i] = (text[i] == ',') ? '.' : text[i];
    }
    buf[n] = '\0';

    char *end = NULL;
    float parsed = strtof(buf, &end);
    if (end == buf) {
        return false;
    }
    *out_value = parsed;
    return true;
}

static int32_t graph_scaled_value(float value)
{
    float scaled = value * (float)GRAPH_VALUE_SCALE;
    return (int32_t)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void graph_format_value(char *dst, size_t dst_size, float value, const char *unit)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    int whole = (int)value;
    float frac = value - (float)whole;
    if (frac < 0.0f) {
        frac = -frac;
    }
    bool show_decimal = frac >= 0.05f;

    if (unit != NULL && unit[0] != '\0') {
        snprintf(dst, dst_size, show_decimal ? "%.1f %s" : "%.0f %s", (double)value, unit);
    } else {
        snprintf(dst, dst_size, show_decimal ? "%.1f" : "%.0f", (double)value);
    }
}

static void graph_format_duration(char *dst, size_t dst_size, int minutes)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (minutes < 60) {
        snprintf(dst, dst_size, "%dm", minutes);
        return;
    }

    int hours = minutes / 60;
    int mins = minutes % 60;
    if (mins == 0) {
        snprintf(dst, dst_size, "%dh", hours);
    } else {
        snprintf(dst, dst_size, "%dh%02dm", hours, mins);
    }
}

static bool graph_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int graph_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool graph_parse_hex_color(const char *text, lv_color_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }

    const char *p = text;
    if (p[0] == '#') {
        p++;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    if (strlen(p) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; i++) {
        if (!graph_is_hex_digit(p[i])) {
            return false;
        }
    }

    int r_hi = graph_hex_nibble(p[0]);
    int r_lo = graph_hex_nibble(p[1]);
    int g_hi = graph_hex_nibble(p[2]);
    int g_lo = graph_hex_nibble(p[3]);
    int b_hi = graph_hex_nibble(p[4]);
    int b_lo = graph_hex_nibble(p[5]);
    if (r_hi < 0 || r_lo < 0 || g_hi < 0 || g_lo < 0 || b_hi < 0 || b_lo < 0) {
        return false;
    }

    uint32_t rgb = (uint32_t)(((r_hi << 4) | r_lo) << 16) | (uint32_t)(((g_hi << 4) | g_lo) << 8) |
                   (uint32_t)((b_hi << 4) | b_lo);
    *out = lv_color_hex(rgb);
    return true;
}

static int graph_clamp_point_count(int configured)
{
    if (configured <= 0) {
        return 0;
    }
    if (configured < GRAPH_POINTS_MIN) {
        return GRAPH_POINTS_MIN;
    }
    if (configured > GRAPH_POINTS_MAX) {
        return GRAPH_POINTS_MAX;
    }
    return configured;
}

static int graph_clamp_time_window_min(int configured)
{
    if (configured <= 0) {
        return GRAPH_DEFAULT_TIME_WINDOW_MIN;
    }
    if (configured < GRAPH_TIME_WINDOW_MIN_MIN) {
        return GRAPH_TIME_WINDOW_MIN_MIN;
    }
    if (configured > GRAPH_TIME_WINDOW_MIN_MAX) {
        return GRAPH_TIME_WINDOW_MIN_MAX;
    }
    return configured;
}

static const char *graph_normalize_display_mode(const char *raw)
{
    if (raw == NULL || raw[0] == '\0') {
        return GRAPH_DISPLAY_MODE_LINE;
    }
    if (strcmp(raw, GRAPH_DISPLAY_MODE_LINE) == 0) return GRAPH_DISPLAY_MODE_LINE;
    if (strcmp(raw, GRAPH_DISPLAY_MODE_LINE_SMOOTH) == 0) return GRAPH_DISPLAY_MODE_LINE_SMOOTH;
    if (strcmp(raw, GRAPH_DISPLAY_MODE_LINE_SMOOTH_POINTS) == 0) return GRAPH_DISPLAY_MODE_LINE_SMOOTH_POINTS;
    if (strcmp(raw, GRAPH_DISPLAY_MODE_BARS) == 0) return GRAPH_DISPLAY_MODE_BARS;
    return GRAPH_DISPLAY_MODE_LINE;
}

static int graph_clamp_bar_bucket_min(int configured)
{
    switch (configured) {
    case 5:
    case 10:
    case 15:
    case 30:
        return configured;
    default:
        return GRAPH_BAR_BUCKET_MIN_DEFAULT;
    }
}

static bool graph_mode_is_smooth(const char *mode)
{
    return (strcmp(mode, GRAPH_DISPLAY_MODE_LINE_SMOOTH) == 0) ||
           (strcmp(mode, GRAPH_DISPLAY_MODE_LINE_SMOOTH_POINTS) == 0);
}

static bool graph_is_epoch_valid(time_t epoch)
{
    if (epoch < 0) {
        return false;
    }
    return (uint32_t)epoch >= GRAPH_VALID_EPOCH_MIN;
}

static uint32_t graph_current_bucket_ts(void)
{
    time_t now = time(NULL);
    if (!graph_is_epoch_valid(now)) {
        return 0U;
    }
    /* Event-rate sampling: use the exact wall-clock second. Burst coalescing
       happens in graph_history_append_or_update so we never rely on a fixed
       minute alignment. */
    return (uint32_t)now;
}

static uint32_t graph_display_now_bucket_ts(const w_graph_ctx_t *ctx)
{
    uint32_t now_bucket = graph_current_bucket_ts();
    if (now_bucket != 0U) {
        return now_bucket;
    }
    if (ctx != NULL && ctx->history_count > 0) {
        return ctx->history[ctx->history_count - 1].bucket_ts;
    }
    return 0U;
}

static void graph_sanitize_widget_id(const char *widget_id, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    size_t out = 0;
    if (widget_id != NULL) {
        for (size_t i = 0; widget_id[i] != '\0' && out < (dst_size - 1U); i++) {
            unsigned char c = (unsigned char)widget_id[i];
            if (isalnum(c) || c == '_' || c == '-') {
                dst[out++] = (char)c;
            } else {
                dst[out++] = '_';
            }
        }
    }

    if (out == 0U) {
        snprintf(dst, dst_size, "graph");
    } else {
        dst[out] = '\0';
    }
}

static bool graph_ensure_history_dir(void)
{
    if (s_graph_history_dir_ready) {
        return true;
    }

    struct stat st = {0};
    if (stat(GRAPH_HISTORY_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        s_graph_history_dir_ready = true;
        return true;
    }

    (void)mkdir(GRAPH_HISTORY_DIR, 0775);
    if (stat(GRAPH_HISTORY_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        s_graph_history_dir_ready = true;
        return true;
    }

    return false;
}

static void graph_build_history_path(const char *widget_id, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';

    if (!graph_ensure_history_dir()) {
        return;
    }

    char safe_id[APP_MAX_WIDGET_ID_LEN] = {0};
    graph_sanitize_widget_id(widget_id, safe_id, sizeof(safe_id));
    snprintf(dst, dst_size, "%s/%s.grph", GRAPH_HISTORY_DIR, safe_id);
}

static void graph_history_drop_oldest(w_graph_ctx_t *ctx, int drop_count)
{
    if (ctx == NULL || drop_count <= 0 || ctx->history_count <= 0) {
        return;
    }
    if (drop_count >= ctx->history_count) {
        ctx->history_count = 0;
        return;
    }

    memmove(ctx->history,
        ctx->history + drop_count,
        (size_t)(ctx->history_count - drop_count) * sizeof(ctx->history[0]));
    ctx->history_count -= drop_count;
}

/* Merge the two oldest samples into one (average value, later timestamp) to
   free a slot without losing the 24h coverage. Used when the ring buffer
   fills before the retention window kicks in, so the old end of the graph
   gets progressively downsampled instead of truncated. */
static void graph_history_decimate_oldest(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->history_count < 2) {
        graph_history_drop_oldest(ctx, 1);
        return;
    }
    graph_sample_t *a = &ctx->history[0];
    graph_sample_t *b = &ctx->history[1];
    b->value = (a->value + b->value) * 0.5f;
    /* Keep b->bucket_ts (the newer of the two) so ordering stays strict. */
    graph_history_drop_oldest(ctx, 1);
}

static void graph_history_trim_retention(w_graph_ctx_t *ctx, uint32_t newest_bucket_ts)
{
    if (ctx == NULL || ctx->history_count <= 0 || newest_bucket_ts == 0U) {
        return;
    }
    if (newest_bucket_ts <= GRAPH_HISTORY_RETENTION_SEC) {
        return;
    }

    uint32_t keep_after = newest_bucket_ts - GRAPH_HISTORY_RETENTION_SEC;
    int drop = 0;
    while (drop < ctx->history_count && ctx->history[drop].bucket_ts < keep_after) {
        drop++;
    }
    graph_history_drop_oldest(ctx, drop);
}

static esp_err_t graph_history_load(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->history_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(ctx->history_path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    graph_history_file_header_t header = {0};
    size_t got = fread(&header, 1U, sizeof(header), f);
    if (got != sizeof(header) || header.magic != GRAPH_HISTORY_FILE_MAGIC ||
        header.version != GRAPH_HISTORY_FILE_VERSION ||
        header.count > (uint32_t)GRAPH_HISTORY_MAX_SAMPLES) {
        fclose(f);
        return ESP_FAIL;
    }

    size_t count = (size_t)header.count;
    if (count > 0U) {
        got = fread(ctx->history, sizeof(ctx->history[0]), count, f);
        if (got != count) {
            fclose(f);
            ctx->history_count = 0;
            return ESP_FAIL;
        }
    }
    fclose(f);

    ctx->history_count = (int)count;
    for (int i = 1; i < ctx->history_count; i++) {
        if (ctx->history[i].bucket_ts <= ctx->history[i - 1].bucket_ts) {
            ctx->history_count = 0;
            return ESP_FAIL;
        }
    }
    if (ctx->history_count > 0) {
        graph_history_trim_retention(ctx, ctx->history[ctx->history_count - 1].bucket_ts);
    }
    return ESP_OK;
}

static esp_err_t graph_history_save_buffer(const char *history_path, const graph_sample_t *history, int history_count)
{
    if (history_path == NULL || history_path[0] == '\0' || history_count < 0 || history_count > GRAPH_HISTORY_MAX_SAMPLES) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(history_path, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }

    graph_history_file_header_t header = {
        .magic = GRAPH_HISTORY_FILE_MAGIC,
        .version = GRAPH_HISTORY_FILE_VERSION,
        .reserved = 0U,
        .count = (uint32_t)history_count,
    };

    size_t written = fwrite(&header, 1U, sizeof(header), f);
    if (written != sizeof(header)) {
        fclose(f);
        return ESP_FAIL;
    }

    if (history_count > 0) {
        written = fwrite(history, sizeof(graph_sample_t), (size_t)history_count, f);
        if (written != (size_t)history_count) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    return ESP_OK;
}

static void graph_persist_task(void *arg)
{
    (void)arg;
    while (true) {
        graph_history_persist_job_t *job = NULL;
        if (xQueueReceive(s_graph_persist_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job == NULL) {
            continue;
        }

        esp_err_t err = graph_history_save_buffer(job->history_path, job->history, job->history_count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "async history save failed (%s, count=%d): %s",
                job->history_path, job->history_count, esp_err_to_name(err));
        }
        free(job);
    }
}

static void graph_persist_start_once(void)
{
    if (s_graph_persist_queue != NULL && s_graph_persist_task != NULL) {
        return;
    }

    if (s_graph_persist_queue == NULL) {
        s_graph_persist_queue = xQueueCreate(GRAPH_HISTORY_PERSIST_QUEUE_LEN, sizeof(graph_history_persist_job_t *));
        if (s_graph_persist_queue == NULL) {
            ESP_LOGW(TAG, "failed to create graph persist queue");
            return;
        }
    }

    if (s_graph_persist_task == NULL) {
        BaseType_t created = xTaskCreate(graph_persist_task, "graph_persist", GRAPH_HISTORY_PERSIST_TASK_STACK, NULL,
            GRAPH_HISTORY_PERSIST_TASK_PRIO, &s_graph_persist_task);
        if (created != pdPASS) {
            ESP_LOGW(TAG, "failed to start graph persist task");
            vQueueDelete(s_graph_persist_queue);
            s_graph_persist_queue = NULL;
            return;
        }
    }
}

static bool graph_history_enqueue_persist(const w_graph_ctx_t *ctx, uint32_t bucket_ts)
{
    if (ctx == NULL || ctx->history_path[0] == '\0') {
        return false;
    }

    graph_persist_start_once();
    if (s_graph_persist_queue == NULL) {
        return false;
    }

    graph_history_persist_job_t *job =
        heap_caps_malloc(sizeof(graph_history_persist_job_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (job == NULL) {
        job = malloc(sizeof(graph_history_persist_job_t));
    }
    if (job == NULL) {
        ESP_LOGW(TAG, "failed to allocate graph persist job");
        return false;
    }

    memset(job, 0, sizeof(*job));
    snprintf(job->history_path, sizeof(job->history_path), "%s", ctx->history_path);
    job->bucket_ts = bucket_ts;
    job->history_count = ctx->history_count;
    if (job->history_count > GRAPH_HISTORY_MAX_SAMPLES) {
        job->history_count = GRAPH_HISTORY_MAX_SAMPLES;
    }
    if (job->history_count > 0) {
        memcpy(job->history, ctx->history, (size_t)job->history_count * sizeof(job->history[0]));
    }

    if (xQueueSend(s_graph_persist_queue, &job, 0) != pdTRUE) {
        graph_history_persist_job_t *dropped = NULL;
        if (xQueueReceive(s_graph_persist_queue, &dropped, 0) == pdTRUE && dropped != NULL) {
            free(dropped);
        }
        if (xQueueSend(s_graph_persist_queue, &job, 0) != pdTRUE) {
            ESP_LOGW(TAG, "graph persist queue full, dropping snapshot");
            free(job);
            return false;
        }
    }

    return true;
}

static void graph_history_try_persist(w_graph_ctx_t *ctx, uint32_t bucket_ts, bool force)
{
    if (ctx == NULL || ctx->history_path[0] == '\0' || !ctx->history_dirty) {
        return;
    }

    if (!force) {
        if (GRAPH_HISTORY_SAVE_INTERVAL_SEC == 0U || bucket_ts == 0U) {
            return;
        }

        if (ctx->last_persist_bucket_ts != 0U) {
            uint32_t elapsed = (bucket_ts >= ctx->last_persist_bucket_ts) ? (bucket_ts - ctx->last_persist_bucket_ts) : 0U;
            if (elapsed < GRAPH_HISTORY_SAVE_INTERVAL_SEC) {
                return;
            }
        }
    }

    if (graph_history_enqueue_persist(ctx, bucket_ts)) {
        ctx->history_dirty = false;
        if (bucket_ts != 0U) {
            ctx->last_persist_bucket_ts = bucket_ts;
        } else if (ctx->history_count > 0) {
            ctx->last_persist_bucket_ts = ctx->history[ctx->history_count - 1].bucket_ts;
        }
    }
}

static bool graph_history_append_or_update(w_graph_ctx_t *ctx, uint32_t bucket_ts, float value, bool *out_appended)
{
    if (out_appended != NULL) {
        *out_appended = false;
    }
    if (ctx == NULL || bucket_ts == 0U) {
        return false;
    }

    if (ctx->history_count > 0) {
        graph_sample_t *last = &ctx->history[ctx->history_count - 1];
        if (bucket_ts < last->bucket_ts) {
            return false;
        }
        /* Coalesce pushes that arrive within the min interval: keep the newest
           timestamp + value, overwriting the previous sample. Prevents chatty
           sensors (e.g. power meters reporting every 1-2s) from blowing the
           fixed-size ring buffer. */
        uint32_t delta_t = bucket_ts - last->bucket_ts;
        if (delta_t < GRAPH_HISTORY_MIN_INTERVAL_SEC) {
            float delta_v = last->value - value;
            if (delta_v < 0.0f) delta_v = -delta_v;
            bool value_changed = delta_v >= 0.0001f;
            bool ts_changed = last->bucket_ts != bucket_ts;
            if (!value_changed && !ts_changed) {
                return false;
            }
            last->bucket_ts = bucket_ts;
            last->value = value;
            return true;
        }
    }

    if (ctx->history_count >= GRAPH_HISTORY_MAX_SAMPLES) {
        graph_history_decimate_oldest(ctx);
    }
    ctx->history[ctx->history_count].bucket_ts = bucket_ts;
    ctx->history[ctx->history_count].value = value;
    ctx->history_count++;
    graph_history_trim_retention(ctx, bucket_ts);
    if (out_appended != NULL) {
        *out_appended = true;
    }
    return true;
}

static int graph_desired_point_count(const w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return GRAPH_DEFAULT_POINT_COUNT;
    }

    lv_coord_t content_w = lv_obj_get_width(ctx->card) - lv_obj_get_style_pad_left(ctx->card, LV_PART_MAIN) -
                           lv_obj_get_style_pad_right(ctx->card, LV_PART_MAIN);
    int desired = (int)(content_w / 12);
    if (desired < GRAPH_POINTS_MIN) {
        desired = GRAPH_POINTS_MIN;
    }
    if (desired > GRAPH_POINTS_MAX) {
        desired = GRAPH_POINTS_MAX;
    }
    return desired;
}

/* For smooth modes we oversample the curve so LVGL's piecewise-linear
   rendering visually approximates a spline. */
static int graph_desired_smooth_point_count(const w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return GRAPH_SMOOTH_POINTS_MIN;
    }
    lv_coord_t content_w = lv_obj_get_width(ctx->card) - lv_obj_get_style_pad_left(ctx->card, LV_PART_MAIN) -
                           lv_obj_get_style_pad_right(ctx->card, LV_PART_MAIN);
    int desired = (int)(content_w / 4);
    if (desired < GRAPH_SMOOTH_POINTS_MIN) desired = GRAPH_SMOOTH_POINTS_MIN;
    if (desired > GRAPH_SMOOTH_POINTS_MAX) desired = GRAPH_SMOOTH_POINTS_MAX;
    return desired;
}

static int graph_desired_bar_bucket_count(const w_graph_ctx_t *ctx)
{
    if (ctx == NULL) return 1;
    int bucket_min = graph_clamp_bar_bucket_min(ctx->bar_bucket_min);
    int buckets = (ctx->time_window_min + bucket_min - 1) / bucket_min;
    if (buckets < 1) buckets = 1;
    if (buckets > GRAPH_BAR_BUCKETS_MAX) buckets = GRAPH_BAR_BUCKETS_MAX;
    return buckets;
}

/* Catmull-Rom interpolation between p1 and p2 using p0 and p3 as neighbours. */
static float graph_catmull_rom(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/* Symmetric moving average around history index k, clamped to [lo, hi]. */
static float graph_smoothed_value(const w_graph_ctx_t *ctx, int lo, int hi, int k, int half_window)
{
    int a = k - half_window;
    int b = k + half_window;
    if (a < lo) a = lo;
    if (b > hi) b = hi;
    double sum = 0.0;
    int n = 0;
    for (int i = a; i <= b; ++i) {
        sum += ctx->history[i].value;
        n++;
    }
    if (n <= 0) {
        return ctx->history[k].value;
    }
    return (float)(sum / (double)n);
}

static int graph_max_history_offset_min(const w_graph_ctx_t *ctx, uint32_t now_bucket_ts)
{
    if (ctx == NULL || ctx->history_count <= 0 || now_bucket_ts == 0U) {
        return 0;
    }

    uint32_t oldest = ctx->history[0].bucket_ts;
    if (now_bucket_ts <= oldest) {
        return 0;
    }

    uint32_t delta = now_bucket_ts - oldest;
    int max_offset = (int)(delta / 60U);
    if (max_offset > (int)GRAPH_HISTORY_RETENTION_MIN) {
        max_offset = (int)GRAPH_HISTORY_RETENTION_MIN;
    }
    return max_offset;
}

static int graph_pan_step_min(const w_graph_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 1;
    }

    int step = ctx->time_window_min / 8;
    if (step < 1) {
        step = 1;
    }
    if (step > 60) {
        step = 60;
    }
    return step;
}

/* Sync chart type + point count + indicator style to the current display mode.
   Must be called before populating series values. */
static void graph_sync_chart_to_mode(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->chart == NULL) {
        return;
    }
    const char *mode = graph_normalize_display_mode(ctx->display_mode);

    if (strcmp(mode, GRAPH_DISPLAY_MODE_BARS) == 0) {
        lv_chart_set_type(ctx->chart, LV_CHART_TYPE_BAR);
        int buckets = graph_desired_bar_bucket_count(ctx);
        ctx->point_count = buckets;
        lv_chart_set_point_count(ctx->chart, (uint32_t)buckets);
        /* Bars: no indicator dots. */
        lv_obj_set_style_size(ctx->chart, 0, 0, LV_PART_INDICATOR);
    } else {
        lv_chart_set_type(ctx->chart, LV_CHART_TYPE_LINE);
        int slots;
        if (graph_mode_is_smooth(mode)) {
            slots = graph_desired_smooth_point_count(ctx);
            /* Dots at real samples are drawn via custom post-draw callback for
               line_smooth_points; suppress LVGL's per-slot indicators to avoid
               drawing them on every oversampled slot. */
            lv_obj_set_style_size(ctx->chart, 0, 0, LV_PART_INDICATOR);
        } else {
            /* Plain line mode: user-configured or auto point count with dots. */
            slots = (ctx->configured_point_count > 0)
                ? ctx->configured_point_count
                : graph_desired_point_count(ctx);
            lv_obj_set_style_size(ctx->chart, 5, 5, LV_PART_INDICATOR);
        }
        ctx->point_count = slots;
        lv_chart_set_point_count(ctx->chart, (uint32_t)slots);
    }
}

/* Draw dots for real samples on top of the smooth line (line_smooth_points). */
static void graph_draw_dots_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_POST_END) {
        return;
    }
    w_graph_ctx_t *ctx = (w_graph_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL || ctx->chart == NULL) {
        return;
    }
    if (strcmp(graph_normalize_display_mode(ctx->display_mode), GRAPH_DISPLAY_MODE_LINE_SMOOTH_POINTS) != 0) {
        return;
    }
    if (ctx->real_points_count <= 0 || ctx->point_count <= 1) {
        return;
    }

    lv_obj_t *chart = ctx->chart;
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);
    lv_coord_t pad_left = lv_obj_get_style_pad_left(chart, LV_PART_MAIN);
    lv_coord_t pad_right = lv_obj_get_style_pad_right(chart, LV_PART_MAIN);
    lv_coord_t pad_top = lv_obj_get_style_pad_top(chart, LV_PART_MAIN);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(chart, LV_PART_MAIN);

    lv_coord_t x0 = coords.x1 + pad_left;
    lv_coord_t x1 = coords.x2 - pad_right;
    lv_coord_t y0 = coords.y1 + pad_top;
    lv_coord_t y1 = coords.y2 - pad_bottom;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    int32_t plot_w = (int32_t)(x1 - x0);
    int32_t plot_h = (int32_t)(y1 - y0);

    int32_t range_span = ctx->range_max_scaled - ctx->range_min_scaled;
    if (range_span <= 0) {
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(e);
    if (layer == NULL) {
        return;
    }

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = ctx->line_color;
    dot_dsc.bg_opa = LV_OPA_COVER;
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;

    const int slots = ctx->point_count;
    for (int i = 0; i < ctx->real_points_count; ++i) {
        int slot = ctx->real_points[i].x_id;
        if (slot < 0 || slot >= slots) continue;

        /* X position: LVGL spreads points so slot 0 at x0, slot (slots-1) at x1. */
        int32_t px = (slots > 1)
            ? (x0 + (int32_t)(((int64_t)slot * (int64_t)plot_w) / (int64_t)(slots - 1)))
            : (x0 + plot_w / 2);

        int32_t scaled = graph_scaled_value(ctx->real_points[i].value);
        int32_t y_span = scaled - ctx->range_min_scaled;
        /* Invert Y: higher value -> smaller Y. */
        int32_t py = y1 - (int32_t)(((int64_t)y_span * (int64_t)plot_h) / (int64_t)range_span);

        lv_area_t dot_area = {
            .x1 = (lv_coord_t)(px - 2),
            .y1 = (lv_coord_t)(py - 2),
            .x2 = (lv_coord_t)(px + 2),
            .y2 = (lv_coord_t)(py + 2),
        };
        lv_draw_rect(layer, &dot_dsc, &dot_area);
    }
}

/* Populate the chart series for the current display mode. Returns true if
   at least one slot has a value. Also records min/max. */
static bool graph_populate_series(
    w_graph_ctx_t *ctx,
    uint32_t start_ts,
    uint32_t window_sec,
    float *out_min,
    float *out_max)
{
    if (ctx == NULL || ctx->chart == NULL || ctx->series == NULL) {
        return false;
    }

    /* Clear slots first. */
    for (int i = 0; i < ctx->point_count; i++) {
        lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, LV_CHART_POINT_NONE);
    }
    ctx->real_points_count = 0;

    const char *mode = graph_normalize_display_mode(ctx->display_mode);
    bool has_values = false;
    float min_v = 0.0f;
    float max_v = 0.0f;

    /* Locate first history sample inside the window (inclusive of start_ts). */
    int first_in_window = 0;
    while (first_in_window < ctx->history_count &&
           ctx->history[first_in_window].bucket_ts < start_ts) {
        first_in_window++;
    }
    uint32_t end_ts = start_ts + window_sec;

    if (strcmp(mode, GRAPH_DISPLAY_MODE_BARS) == 0) {
        /* Bucket averages. */
        uint32_t bucket_sec = (uint32_t)graph_clamp_bar_bucket_min(ctx->bar_bucket_min) * 60U;
        if (bucket_sec == 0U) bucket_sec = 60U;

        for (int i = 0; i < ctx->point_count; i++) {
            uint32_t slot_start = start_ts + (uint32_t)i * bucket_sec;
            uint32_t slot_end = slot_start + bucket_sec;
            if (slot_end > end_ts) slot_end = end_ts;
            if (slot_start >= end_ts) break;

            double sum = 0.0;
            int count = 0;
            for (int k = first_in_window; k < ctx->history_count; ++k) {
                uint32_t ts = ctx->history[k].bucket_ts;
                if (ts >= slot_end) break;
                if (ts >= slot_start) {
                    sum += ctx->history[k].value;
                    count++;
                }
            }
            if (count > 0) {
                float avg = (float)(sum / (double)count);
                lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(avg));
                if (!has_values) {
                    min_v = avg; max_v = avg; has_values = true;
                } else {
                    if (avg < min_v) min_v = avg;
                    if (avg > max_v) max_v = avg;
                }
            }
        }
    } else if (graph_mode_is_smooth(mode)) {
        /* Collect real samples inside the window. */
        int real_start = first_in_window;
        int real_end = first_in_window;
        while (real_end < ctx->history_count && ctx->history[real_end].bucket_ts < end_ts) {
            real_end++;
        }
        int real_count = real_end - real_start;

        if (real_count <= 0) {
            if (out_min) *out_min = 0.0f;
            if (out_max) *out_max = 0.0f;
            return false;
        }

        /* Moving-average pre-smoothing. Scale with sample density but stay
           aggressive enough to dissolve short plateaus that would otherwise
           produce visible stairsteps after Catmull-Rom. */
        int half_window = real_count / 10;
        if (half_window < 3) half_window = 3;
        if (half_window > 24) half_window = 24;

        /* Cache slot-position + smoothed value per real sample; also track min/max
           from smoothed values so range padding matches the rendered line. */
        has_values = true;
        bool first = true;
        for (int k = real_start; k < real_end; ++k) {
            float vs = graph_smoothed_value(ctx, real_start, real_end - 1, k, half_window);
            if (first) { min_v = vs; max_v = vs; first = false; }
            else { if (vs < min_v) min_v = vs; if (vs > max_v) max_v = vs; }

            uint32_t ts = ctx->history[k].bucket_ts;
            if (ts < start_ts) ts = start_ts;
            if (ts > end_ts) ts = end_ts;
            int slot = (ctx->point_count > 1)
                ? (int)(((uint64_t)(ts - start_ts) * (uint64_t)(ctx->point_count - 1)) / (uint64_t)window_sec)
                : 0;
            if (slot < 0) slot = 0;
            if (slot >= ctx->point_count) slot = ctx->point_count - 1;
            if (ctx->real_points_count < (int)(sizeof(ctx->real_points) / sizeof(ctx->real_points[0]))) {
                ctx->real_points[ctx->real_points_count].x_id = (int16_t)slot;
                ctx->real_points[ctx->real_points_count].value = vs;
                ctx->real_points_count++;
            }
        }

        if (real_count == 1) {
            float vs = graph_smoothed_value(ctx, real_start, real_end - 1, real_start, half_window);
            for (int i = 0; i < ctx->point_count; i++) {
                lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(vs));
            }
        } else {
            /* Fill every slot via Catmull-Rom over the smoothed sample values. */
            int j = real_start;
            for (int i = 0; i < ctx->point_count; i++) {
                uint32_t slot_ts = (ctx->point_count > 1)
                    ? (start_ts + (uint32_t)(((uint64_t)window_sec * (uint64_t)i) / (uint64_t)(ctx->point_count - 1)))
                    : start_ts;

                while (j + 1 < real_end && ctx->history[j + 1].bucket_ts <= slot_ts) {
                    j++;
                }

                if (slot_ts <= ctx->history[real_start].bucket_ts) {
                    float vs = graph_smoothed_value(ctx, real_start, real_end - 1, real_start, half_window);
                    lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(vs));
                    continue;
                }
                if (j >= real_end - 1) {
                    float vs = graph_smoothed_value(ctx, real_start, real_end - 1, real_end - 1, half_window);
                    lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(vs));
                    continue;
                }

                uint32_t ts1 = ctx->history[j].bucket_ts;
                uint32_t ts2 = ctx->history[j + 1].bucket_ts;
                float p0 = (j > real_start)
                    ? graph_smoothed_value(ctx, real_start, real_end - 1, j - 1, half_window)
                    : graph_smoothed_value(ctx, real_start, real_end - 1, j, half_window);
                float p1 = graph_smoothed_value(ctx, real_start, real_end - 1, j, half_window);
                float p2 = graph_smoothed_value(ctx, real_start, real_end - 1, j + 1, half_window);
                float p3 = (j + 2 < real_end)
                    ? graph_smoothed_value(ctx, real_start, real_end - 1, j + 2, half_window)
                    : graph_smoothed_value(ctx, real_start, real_end - 1, j + 1, half_window);

                float t = (ts2 > ts1) ? ((float)(slot_ts - ts1) / (float)(ts2 - ts1)) : 0.0f;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float v = graph_catmull_rom(p0, p1, p2, p3, t);

                /* Soft local clamp: allow the spline its natural curvature
                   around plateaus but stop runaway ringing. */
                float seg_min = p1 < p2 ? p1 : p2;
                float seg_max = p1 > p2 ? p1 : p2;
                float seg_span = seg_max - seg_min;
                float margin = (seg_span > 0.0f) ? (seg_span * 0.75f) : (fabsf(p2 - p1) * 0.25f);
                /* Also factor in the neighbourhood so completely flat segments
                   can still curve gently toward the next non-equal sample. */
                float nbr_span = fmaxf(fabsf(p3 - p2), fabsf(p1 - p0));
                if (margin < nbr_span * 0.25f) margin = nbr_span * 0.25f;
                float lo = seg_min - margin;
                float hi = seg_max + margin;
                if (v < lo) v = lo;
                if (v > hi) v = hi;

                lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(v));
            }
        }
    } else {
        /* Plain line mode (original behaviour): one real value per slot, gaps stay NONE. */
        int history_idx = first_in_window;
        for (int i = 0; i < ctx->point_count; i++) {
            uint32_t slot_start = start_ts + (uint32_t)(((uint64_t)window_sec * (uint64_t)i) / (uint64_t)ctx->point_count);
            uint32_t slot_end = start_ts + (uint32_t)(((uint64_t)window_sec * (uint64_t)(i + 1)) / (uint64_t)ctx->point_count);
            if (slot_end <= slot_start) {
                slot_end = slot_start + 1U;
            }
            bool is_last_slot = (i == (ctx->point_count - 1));

            bool slot_has_value = false;
            float slot_value = 0.0f;
            while (history_idx < ctx->history_count) {
                uint32_t ts = ctx->history[history_idx].bucket_ts;
                if (is_last_slot ? (ts > slot_end) : (ts >= slot_end)) {
                    break;
                }
                if (ts >= slot_start) {
                    slot_has_value = true;
                    slot_value = ctx->history[history_idx].value;
                }
                history_idx++;
            }

            if (slot_has_value) {
                lv_chart_set_value_by_id(ctx->chart, ctx->series, (uint32_t)i, graph_scaled_value(slot_value));
                if (!has_values) {
                    min_v = slot_value; max_v = slot_value; has_values = true;
                } else {
                    if (slot_value < min_v) min_v = slot_value;
                    if (slot_value > max_v) max_v = slot_value;
                }
            }
        }
    }

    if (out_min) *out_min = min_v;
    if (out_max) *out_max = max_v;
    return has_values;
}

static void graph_rebuild_chart(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->chart == NULL || ctx->series == NULL || ctx->meta_label == NULL) {
        return;
    }

    /* Sync LVGL chart config (type, point count, indicator size) to the mode. */
    graph_sync_chart_to_mode(ctx);

    if (ctx->point_count <= 0) {
        lv_obj_add_flag(ctx->chart, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint32_t now_bucket = graph_display_now_bucket_ts(ctx);
    int max_offset_min = graph_max_history_offset_min(ctx, now_bucket);
    if (ctx->history_offset_min > max_offset_min) {
        ctx->history_offset_min = max_offset_min;
    }
    if (ctx->history_offset_min < 0) {
        ctx->history_offset_min = 0;
    }

    char window_text[16] = {0};
    char offset_text[16] = {0};
    graph_format_duration(window_text, sizeof(window_text), ctx->time_window_min);
    graph_format_duration(offset_text, sizeof(offset_text), ctx->history_offset_min);

    if (now_bucket == 0U || ctx->history_count <= 0) {
        lv_obj_add_flag(ctx->chart, LV_OBJ_FLAG_HIDDEN);
        char meta_text[64] = {0};
        snprintf(meta_text, sizeof(meta_text), "%s | %s", ui_i18n_get("graph.no_history", "no history"), window_text);
        lv_label_set_text(ctx->meta_label, meta_text);
        return;
    }

    uint32_t window_sec = (uint32_t)ctx->time_window_min * 60U;
    uint32_t offset_sec = (uint32_t)ctx->history_offset_min * 60U;
    uint32_t end_ts = (now_bucket > offset_sec) ? (now_bucket - offset_sec) : 0U;
    uint32_t start_ts = (end_ts > window_sec) ? (end_ts - window_sec) : 0U;

    float min_v = 0.0f;
    float max_v = 0.0f;
    bool has_values = graph_populate_series(ctx, start_ts, window_sec, &min_v, &max_v);

    if (!has_values) {
        lv_obj_add_flag(ctx->chart, LV_OBJ_FLAG_HIDDEN);
        char meta_text[64] = {0};
        if (ctx->history_offset_min > 0) {
            snprintf(
                meta_text,
                sizeof(meta_text),
                "%s | %s @-%s",
                ui_i18n_get("graph.no_data", "no data"),
                window_text,
                offset_text);
        } else {
            snprintf(meta_text, sizeof(meta_text), "%s | %s", ui_i18n_get("graph.no_data", "no data"), window_text);
        }
        lv_label_set_text(ctx->meta_label, meta_text);
        return;
    }

    lv_obj_clear_flag(ctx->chart, LV_OBJ_FLAG_HIDDEN);

    float span = max_v - min_v;
    float pad = (span < 0.5f) ? 0.5f : (span * 0.12f);
    int32_t min_i = graph_scaled_value(min_v - pad);
    int32_t max_i = graph_scaled_value(max_v + pad);
    if (min_i >= max_i) {
        max_i = min_i + 1;
    }
    ctx->range_min_scaled = min_i;
    ctx->range_max_scaled = max_i;
    lv_chart_set_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, min_i, max_i);
    lv_chart_refresh(ctx->chart);

    char min_text[24] = {0};
    char max_text[24] = {0};
    char meta_text[96] = {0};
    graph_format_value(min_text, sizeof(min_text), min_v, ctx->unit);
    graph_format_value(max_text, sizeof(max_text), max_v, ctx->unit);
    const char *min_label = ui_i18n_get("graph.min", "min");
    const char *max_label = ui_i18n_get("graph.max", "max");
    if (ctx->history_offset_min > 0) {
        snprintf(
            meta_text,
            sizeof(meta_text),
            "%s %s   %s %s | %s @-%s",
            min_label,
            min_text,
            max_label,
            max_text,
            window_text,
            offset_text);
    } else {
        snprintf(meta_text, sizeof(meta_text), "%s %s   %s %s | %s", min_label, min_text, max_label, max_text, window_text);
    }
    lv_label_set_text(ctx->meta_label, meta_text);
}

static void graph_apply_layout(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL || ctx->title_label == NULL || ctx->value_label == NULL || ctx->meta_label == NULL ||
        ctx->chart == NULL) {
        return;
    }

    lv_obj_t *card = ctx->card;
    lv_obj_update_layout(card);

    lv_coord_t content_w =
        lv_obj_get_width(card) - lv_obj_get_style_pad_left(card, LV_PART_MAIN) - lv_obj_get_style_pad_right(card, LV_PART_MAIN);
    lv_coord_t content_h =
        lv_obj_get_height(card) - lv_obj_get_style_pad_top(card, LV_PART_MAIN) - lv_obj_get_style_pad_bottom(card, LV_PART_MAIN);
    if (content_w < 40) {
        content_w = 40;
    }
    if (content_h < 40) {
        content_h = 40;
    }

    lv_coord_t title_w = (lv_coord_t)((content_w * 58) / 100);
    if (title_w < 80) {
        title_w = 80;
    }
    if (title_w > content_w - 30) {
        title_w = content_w - 30;
    }
    lv_coord_t value_w = content_w - title_w;
    if (value_w < 30) {
        value_w = 30;
        title_w = content_w - value_w;
    }

    lv_obj_set_width(ctx->title_label, title_w);
    lv_obj_set_width(ctx->value_label, value_w);
    lv_obj_set_width(ctx->meta_label, content_w);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->meta_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(ctx->value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_align_to(ctx->meta_label, ctx->title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_coord_t chart_y = lv_obj_get_y(ctx->meta_label) + lv_obj_get_height(ctx->meta_label) + 4;
    if (chart_y < 20) {
        chart_y = 20;
    }
    if (chart_y > content_h - 24) {
        chart_y = content_h - 24;
    }

    lv_obj_set_pos(ctx->chart, 0, chart_y);
    lv_obj_set_size(ctx->chart, content_w, content_h - chart_y);

    int desired_points = (ctx->configured_point_count > 0) ? ctx->configured_point_count : graph_desired_point_count(ctx);
    if (desired_points != ctx->point_count) {
        ctx->point_count = desired_points;
        lv_chart_set_point_count(ctx->chart, (uint32_t)ctx->point_count);
    }

    graph_rebuild_chart(ctx);
}

static void graph_apply_unavailable(w_graph_ctx_t *ctx)
{
    if (ctx == NULL || ctx->value_label == NULL) {
        return;
    }

    ctx->unavailable = true;
    lv_label_set_text(ctx->value_label, ui_i18n_get("common.unavailable", "unavailable"));
    graph_rebuild_chart(ctx);
}

static void graph_pan_history(w_graph_ctx_t *ctx, bool older)
{
    if (ctx == NULL) {
        return;
    }

    uint32_t now_bucket = graph_display_now_bucket_ts(ctx);
    int max_offset = graph_max_history_offset_min(ctx, now_bucket);
    int step = graph_pan_step_min(ctx);

    if (older) {
        int next = ctx->history_offset_min + step;
        if (next > max_offset) {
            next = max_offset;
        }
        if (next != ctx->history_offset_min) {
            ctx->history_offset_min = next;
            graph_rebuild_chart(ctx);
        }
    } else {
        int next = ctx->history_offset_min - step;
        if (next < 0) {
            next = 0;
        }
        if (next != ctx->history_offset_min) {
            ctx->history_offset_min = next;
            graph_rebuild_chart(ctx);
        }
    }
}

static void w_graph_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }

    w_graph_ctx_t *ctx = (w_graph_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        graph_history_try_persist(ctx, graph_display_now_bucket_ts(ctx), true);
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        graph_apply_layout(ctx);
    } else if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_active();
        if (indev == NULL) {
            return;
        }
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT) {
            graph_pan_history(ctx, true);
        } else if (dir == LV_DIR_RIGHT) {
            graph_pan_history(ctx, false);
        }
    }
}

esp_err_t w_graph_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    theme_default_style_card(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    lv_obj_set_style_text_color(title, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_20, LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, theme_default_color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_text_font(value, APP_FONT_TEXT_20, LV_PART_MAIN);

    lv_obj_t *meta = lv_label_create(card);
    lv_label_set_text(meta, "");
    lv_obj_set_style_text_color(meta, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_font(meta, APP_FONT_TEXT_20, LV_PART_MAIN);

    lv_obj_t *chart = lv_chart_create(card);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_size(chart, 5, 5, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    w_graph_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_graph_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    ctx->card = card;
    ctx->title_label = title;
    ctx->value_label = value;
    ctx->meta_label = meta;
    ctx->chart = chart;
    ctx->history_count = 0;
    ctx->history_dirty = false;
    ctx->last_persist_bucket_ts = 0U;
    ctx->configured_point_count = graph_clamp_point_count(def->graph_point_count);
    ctx->point_count = (ctx->configured_point_count > 0) ? ctx->configured_point_count : GRAPH_DEFAULT_POINT_COUNT;
    ctx->time_window_min = graph_clamp_time_window_min(def->graph_time_window_min);
    ctx->history_offset_min = 0;
    snprintf(ctx->display_mode, sizeof(ctx->display_mode), "%s", graph_normalize_display_mode(def->graph_display_mode));
    ctx->bar_bucket_min = graph_clamp_bar_bucket_min(def->graph_bar_bucket_min);
    ctx->real_points_count = 0;
    ctx->range_min_scaled = 0;
    ctx->range_max_scaled = 0;
    ctx->line_color = lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE);
    lv_color_t parsed_color = lv_color_hex(0);
    if (graph_parse_hex_color(def->graph_line_color, &parsed_color)) {
        ctx->line_color = parsed_color;
    }
    ctx->unavailable = true;
    ctx->unit[0] = '\0';
    graph_build_history_path(def->id, ctx->history_path, sizeof(ctx->history_path));
    if (ctx->history_path[0] != '\0') {
        (void)graph_history_load(ctx);
        if (ctx->history_count > 0) {
            ctx->last_persist_bucket_ts = ctx->history[ctx->history_count - 1].bucket_ts;
        }
    }

    lv_chart_set_point_count(chart, (uint32_t)ctx->point_count);
    ctx->series = lv_chart_add_series(chart, ctx->line_color, LV_CHART_AXIS_PRIMARY_Y);
    if (ctx->series == NULL) {
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_event_cb(card, w_graph_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, w_graph_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, w_graph_event_cb, LV_EVENT_GESTURE, ctx);
    lv_obj_add_event_cb(chart, graph_draw_dots_cb, LV_EVENT_DRAW_POST_END, ctx);

    graph_apply_unavailable(ctx);
    graph_apply_layout(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_graph_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }

    w_graph_ctx_t *ctx = (w_graph_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    bool was_unavailable = ctx->unavailable;

    if (graph_state_is_unavailable(state->state)) {
        graph_apply_unavailable(ctx);
        return;
    }

    const char *unit = NULL;
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
        if (cJSON_IsString(unit_item) && unit_item->valuestring != NULL) {
            unit = unit_item->valuestring;
        }
    }
    if (unit != NULL && unit[0] != '\0') {
        snprintf(ctx->unit, sizeof(ctx->unit), "%s", unit);
    }
    if (attrs != NULL) {
        cJSON_Delete(attrs);
    }

    float numeric = 0.0f;
    bool parsed = graph_parse_float_relaxed(state->state, &numeric);
    ctx->unavailable = false;

    if (!parsed) {
        lv_label_set_text(ctx->value_label, state->state);
        if (was_unavailable) {
            graph_rebuild_chart(ctx);
        }
        return;
    }

    char value_text[48] = {0};
    graph_format_value(value_text, sizeof(value_text), numeric, ctx->unit);
    lv_label_set_text(ctx->value_label, value_text);

    uint32_t bucket_ts = graph_current_bucket_ts();
    bool history_changed = graph_history_append_or_update(ctx, bucket_ts, numeric, NULL);
    if (history_changed) {
        ctx->history_dirty = true;
        graph_history_try_persist(ctx, bucket_ts, false);
    }

    if (was_unavailable || history_changed) {
        graph_rebuild_chart(ctx);
    }
}

void w_graph_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }

    w_graph_ctx_t *ctx = (w_graph_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    graph_apply_unavailable(ctx);
}
