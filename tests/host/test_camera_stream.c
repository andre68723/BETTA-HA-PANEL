/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 * Real worker with deterministic HTTP/task interleavings; no sockets or HA.
 */
#include "ha/ha_camera_stream.h"
#include "ha/ha_client.h"
#include "camera_test_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, allocations, semaphores, opens, decodes, reads;
static int code = 401;
static bool blocked, busy, fail_task, fail_heap, silence, consume;
static int64_t clock_ms = 1000;
static const char *content_type = "multipart/x-mixed-replace;boundary=--x";
static const char body[] = "--x\r\nContent-Type: image/jpeg\r\nContent-Length: 8\r\n\r\n"
    "\xff\xd8" "ABCD" "\xff\xd9" "\r\n";
static size_t position;
static ha_camera_stream_t *owner;
static esp_http_client_config_t configuration;
static struct { void (*run)(void *); void *arg; } tasks[8];
static unsigned task_count;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Camera worker %d: %s\n", __LINE__, #c); exit(1); } } while (0)

void *heap_caps_malloc(size_t n, unsigned caps)
{
    if (fail_heap) { fail_heap = false; return NULL; }
    void *p = malloc(n); if (p) allocations++; return p;
}
void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{
    void *p = heap_caps_malloc(n * size, caps); if (p) memset(p, 0, n * size); return p;
}
void heap_caps_free(void *p) { if (p) { CHECK(allocations > 0); allocations--; free(p); } }
size_t heap_caps_get_free_size(unsigned caps) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { return 50000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { semaphores++; return malloc(1); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) { return !(blocked && !wait); }
void xSemaphoreGive(SemaphoreHandle_t s) {}
void vSemaphoreDelete(SemaphoreHandle_t s) { CHECK(semaphores > 0); semaphores--; free(s); }
BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*run)(void *), const char *name, unsigned stack, void *arg,
                                         unsigned priority, void *handle, int core, unsigned caps)
{
    if (fail_task) { fail_task = false; return 0; }
    CHECK(task_count < 8 && stack >= 6144);
    tasks[task_count].run = run; tasks[task_count++].arg = arg; return pdPASS;
}
void vTaskDeleteWithCaps(void *task) {}
void vTaskDelay(TickType_t ms) { clock_ms += ms; busy = false; }
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
bool ha_client_is_connected(void) { return true; }
bool ha_client_heavy_gate_is_busy(void) { return busy; }
bool ha_client_get_http_context(ha_client_http_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->base_url, sizeof(ctx->base_url), "http://ha.example:8123");
    snprintf(ctx->bearer_token, sizeof(ctx->bearer_token), "synthetic-test-credential");
    return true;
}
bool ha_cover_fetcher_decode_jpeg(const uint8_t *jpeg, size_t length, int w, int h, ha_cover_result_t *out)
{
    CHECK(length == 8 && w <= 960 && h <= 640);
    decodes++;
    memset(out, 0, sizeof(*out));
    out->image.data = heap_caps_malloc(64, MALLOC_CAP_SPIRAM);
    out->valid = out->image.data != NULL;
    return out->valid;
}
void ha_cover_result_release(ha_cover_result_t *out)
{
    if (out->valid) heap_caps_free((void *)out->image.data);
    memset(out, 0, sizeof(*out));
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    CHECK(cfg->disable_auto_redirect && cfg->timeout_ms <= 1000);
    CHECK(!strcmp(cfg->url, "http://ha.example:8123/api/camera_proxy_stream/camera.synthetic"));
    configuration = *cfg; position = 0; return &configuration;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *name, const char *value) { return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int length) { opens++; return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t h)
{
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER, .user_data = configuration.user_data,
                                    .header_key = "Content-Type", .header_value = content_type};
    configuration.event_handler(&event); return 0;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) { return code; }
int esp_http_client_read(esp_http_client_handle_t h, char *out, int length)
{
    reads++; clock_ms += 5;
    if (silence) { clock_ms += 1000; return -ESP_ERR_HTTP_EAGAIN; }
    if (position < sizeof(body) - 1) { *out = body[position++]; return 1; }
    if (owner) {
        if (consume) {
            ha_cover_result_t frame = {0}; ha_camera_stream_status_t status;
            blocked = true;
            CHECK(!ha_camera_stream_take(owner, &frame, &status)); /* UI never waits on decoder/mailbox */
            blocked = false;
            CHECK(ha_camera_stream_take(owner, &frame, &status) && frame.valid && status == HA_CAMERA_LIVE);
            ha_cover_result_release(&frame);
        }
        ha_camera_stream_stop(owner); owner = NULL; /* close while the worker is in read() */
    }
    return 0;
}
void esp_http_client_close(esp_http_client_handle_t h) {}
void esp_http_client_cleanup(esp_http_client_handle_t h) {}

static void run_tasks(void)
{
    for (unsigned i = 0; i < task_count; i++) tasks[i].run(tasks[i].arg);
    task_count = 0;
}
static ha_camera_stream_t *start(void) { return ha_camera_stream_start("camera.synthetic", 1280, 800); }
static void clean(void) { CHECK(allocations == 0 && semaphores == 0 && task_count == 0); }

int main(void)
{
    CHECK(!ha_camera_stream_start("camera.bad?token=x", 320, 180)); clean();
    fail_heap = true; CHECK(!start()); clean();
    fail_task = true; CHECK(!start()); clean();
    owner = start(); CHECK(owner != NULL); ha_camera_stream_stop(owner); owner = NULL;
    run_tasks(); CHECK(opens == 0); clean();
    ha_camera_stream_t *a = start(), *b = start(); CHECK(a && b && !start());
    ha_camera_stream_stop(a); ha_camera_stream_stop(b); run_tasks(); clean();
    for (int status_code = 301; status_code <= 403; status_code += status_code == 301 ? 100 : 2) {
        unsigned before = opens; code = status_code;
        a = start(); run_tasks();
        ha_cover_result_t frame = {0}; ha_camera_stream_status_t status;
        CHECK(!ha_camera_stream_take(a, &frame, &status) && status == HA_CAMERA_FAILED);
        CHECK(opens == before + 1); ha_camera_stream_stop(a); clean();
    }
    code = 200; content_type = "image/jpeg";
    a = start(); run_tasks();
    ha_cover_result_t frame = {0}; ha_camera_stream_status_t status;
    CHECK(!ha_camera_stream_take(a, &frame, &status) && status == HA_CAMERA_UNSUPPORTED);
    ha_camera_stream_stop(a); clean();
    content_type = "multipart/x-mixed-replace;boundary=--x";
    owner = start(); consume = true; busy = true; run_tasks();
    CHECK(owner == NULL && decodes == 1); clean();
    /* A queued frame is released even when the consumer never takes it. */
    owner = start(); consume = false; run_tasks(); CHECK(owner == NULL && decodes == 2); clean();
    silence = true; unsigned before = reads;
    a = start(); run_tasks();
    CHECK(reads - before <= 60);
    CHECK(!ha_camera_stream_take(a, &frame, &status) && status == HA_CAMERA_FAILED);
    ha_camera_stream_stop(a); clean();
    printf("Camera worker: %u checks passed (HTTP failures, framing, silence, cancellation, bounds, ownership)\n", checks);
}
