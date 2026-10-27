/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ha/ha_camera_stream.h"
#include "ha/ha_client.h"
#include "ha/ha_auth_state.h"
#include "ha/mjpeg_parser.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>

struct ha_camera_stream {
    SemaphoreHandle_t lock;
    _Atomic unsigned references;
    _Atomic bool cancelled;
    int width, height;
    _Atomic ha_camera_stream_status_t status;
    ha_cover_result_t latest;
    int64_t last_decode_ms;
    char path[144];
    char content_type[160];
    ha_client_http_ctx_t http;
    char url[640], auth[600];
    mjpeg_parser_t parser;
    uint8_t read_buffer[2048];
};

static portMUX_TYPE s_count_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_workers;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool cancelled(ha_camera_stream_t *s)
{
    return atomic_load(&s->cancelled);
}

static void status_set(ha_camera_stream_t *s, ha_camera_stream_status_t status)
{
    atomic_store(&s->status, status);
}

static void release(ha_camera_stream_t *s)
{
    bool last = atomic_fetch_sub(&s->references, 1) == 1;
    if (last) {
        ha_cover_result_release(&s->latest);
        vSemaphoreDelete(s->lock);
        memset(&s->http, 0, sizeof(s->http));
        memset(s->auth, 0, sizeof(s->auth));
        heap_caps_free(s);
    }
}

static bool ready(void)
{
    return ha_client_is_connected() && !ha_client_heavy_gate_is_busy() &&
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 20U * 1024U &&
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 12U * 1024U;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    ha_camera_stream_t *s = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Content-Type")) {
        snprintf(s->content_type, sizeof(s->content_type), "%s", event->header_value);
    }
    return ESP_OK;
}

static bool frame_ready(void *user, const uint8_t *jpeg, size_t length)
{
    ha_camera_stream_t *s = user;
    if (cancelled(s)) return false;
    /* Keep just one decoded frame, never queue LVGL callbacks or accumulate
     * frames behind a slow display. Cap decoding at five frames per second. */
    xSemaphoreTake(s->lock, portMAX_DELAY);
    bool full = s->latest.valid;
    xSemaphoreGive(s->lock);
    if (full || now_ms() - s->last_decode_ms < 200) return true;
    s->last_decode_ms = now_ms();
    ha_cover_result_t frame = {0};
    if (!ha_cover_fetcher_decode_jpeg(jpeg, length, s->width, s->height, &frame)) return false;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (!s->cancelled) {
        s->latest = frame;
        s->status = HA_CAMERA_LIVE;
        memset(&frame, 0, sizeof(frame));
    }
    xSemaphoreGive(s->lock);
    ha_cover_result_release(&frame);
    return !cancelled(s);
}

static void stream_task(void *user)
{
    ha_camera_stream_t *s = user;
    uint8_t *jpeg = heap_caps_malloc(MJPEG_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned failures = 0;
    if (!jpeg) status_set(s, HA_CAMERA_FAILED);
    while (jpeg && !cancelled(s) && failures < 3) {
        if (ha_auth_state_blocked()) break;
        if (!ready()) {
            status_set(s, HA_CAMERA_WAITING);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        status_set(s, HA_CAMERA_CONNECTING);
        if (!ha_client_get_http_context(&s->http)) { failures++; continue; }
        int n = snprintf(s->url, sizeof(s->url), "%s%s", s->http.base_url, s->path);
        if (n < 0 || (size_t)n >= sizeof(s->url) || !s->http.bearer_token[0]) break;
        snprintf(s->auth, sizeof(s->auth), "Bearer %s", s->http.bearer_token);
        s->content_type[0] = '\0';
        esp_http_client_config_t cfg = {
            .url = s->url, .timeout_ms = 1000, .buffer_size = 2048, .buffer_size_tx = 1024,
            .disable_auto_redirect = true, .event_handler = http_event, .user_data = s,
            .common_name = s->http.cert_common_name[0] ? s->http.cert_common_name : NULL,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        };
        esp_http_client_handle_t http = esp_http_client_init(&cfg);
        bool unsupported = false, yielding = false;
        if (http) {
            esp_http_client_set_header(http, "Authorization", s->auth);
            esp_http_client_set_header(http, "Accept", "multipart/x-mixed-replace");
            if (s->http.host_header[0]) esp_http_client_set_header(http, "Host", s->http.host_header);
            if (esp_http_client_open(http, 0) == ESP_OK) {
                (void)esp_http_client_fetch_headers(http);
                int code = esp_http_client_get_status_code(http);
                (void)ha_auth_state_report_image_fetch(code, true);
                if (code == 200) {
                    unsupported = !mjpeg_parser_init(&s->parser, s->content_type, jpeg, MJPEG_FRAME_MAX);
                    int64_t progress = now_ms(), last_frame = progress;
                    while (!unsupported && !cancelled(s)) {
                        if (!ready()) { yielding = true; break; }
                        int count = esp_http_client_read(http, (char *)s->read_buffer, sizeof(s->read_buffer));
                        if (count > 0) {
                            progress = now_ms();
                            int64_t decoded_before = s->last_decode_ms;
                            if (!mjpeg_parser_feed(&s->parser, s->read_buffer, (size_t)count, frame_ready, s) ||
                                s->parser.ended) break;
                            if (s->last_decode_ms != decoded_before) { last_frame = now_ms(); failures = 0; }
                        } else if (count == 0 || count != -ESP_ERR_HTTP_EAGAIN) {
                            break;
                        }
                        /* Both silence and endless partial frames are bounded. */
                        if (now_ms() - progress > 15000 || now_ms() - last_frame > 15000) break;
                        if (count < 0) vTaskDelay(pdMS_TO_TICKS(20));
                    }
                } else if (code == 401 || code == 403 || (code >= 300 && code < 400)) {
                    failures = 3; /* never forward credentials or repeat rejected authentication */
                }
            }
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
        }
        memset(s->auth, 0, sizeof(s->auth));
        memset(&s->http, 0, sizeof(s->http));
        if (unsupported) { status_set(s, HA_CAMERA_UNSUPPORTED); break; }
        if (!yielding) failures++;
        status_set(s, yielding ? HA_CAMERA_WAITING : HA_CAMERA_FAILED);
        if (!cancelled(s)) vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!cancelled(s) && s->status != HA_CAMERA_UNSUPPORTED) status_set(s, HA_CAMERA_FAILED);
    heap_caps_free(jpeg);
    taskENTER_CRITICAL(&s_count_lock);
    s_workers--;
    taskEXIT_CRITICAL(&s_count_lock);
    release(s);
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    vTaskDeleteWithCaps(NULL);
#else
    vTaskDelete(NULL);
#endif
}

ha_camera_stream_t *ha_camera_stream_start(const char *entity_id, int width, int height)
{
    char path[144];
    if (!mjpeg_camera_path(entity_id, path, sizeof(path)) || width < 16 || height < 16) return NULL;
    taskENTER_CRITICAL(&s_count_lock);
    bool available = s_workers < 2;
    if (available) s_workers++;
    taskEXIT_CRITICAL(&s_count_lock);
    if (!available) return NULL;
    ha_camera_stream_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s) s->lock = xSemaphoreCreateMutex();
    if (s && s->lock) {
        s->references = 2;
        s->width = width > 960 ? 960 : width;
        s->height = height > 640 ? 640 : height;
        s->status = HA_CAMERA_CONNECTING;
        snprintf(s->path, sizeof(s->path), "%s", path);
        BaseType_t result;
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
        result = xTaskCreatePinnedToCoreWithCaps(stream_task, "ha_camera", 8192, s, 2, NULL, 1,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        result = xTaskCreatePinnedToCore(stream_task, "ha_camera", 8192, s, 2, NULL, 1);
#endif
        if (result == pdPASS) return s;
    }
    if (s && s->lock) vSemaphoreDelete(s->lock);
    heap_caps_free(s);
    taskENTER_CRITICAL(&s_count_lock);
    s_workers--;
    taskEXIT_CRITICAL(&s_count_lock);
    return NULL;
}

bool ha_camera_stream_take(ha_camera_stream_t *s, ha_cover_result_t *out, ha_camera_stream_status_t *status)
{
    if (!s || !out || !status) return false;
    *status = s->status;
    if (xSemaphoreTake(s->lock, 0) != pdTRUE) return false;
    bool valid = s->latest.valid;
    if (valid) { *out = s->latest; memset(&s->latest, 0, sizeof(s->latest)); }
    xSemaphoreGive(s->lock);
    return valid;
}

void ha_camera_stream_stop(ha_camera_stream_t *s)
{
    if (!s) return;
    atomic_store(&s->cancelled, true);
    release(s);
}
