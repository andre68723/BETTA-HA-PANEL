/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "drivers/display_port.h"
#include "drivers/display_rotation.h"

#include <stdlib.h>
#include "app_config.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "util/log_tags.h"

/* One display per panel. The adapter owns its LVGL buffers and flush handshake;
 * this context owns only the rotation scratch buffer and optional accelerator.
 * LVGL waits for the preceding color-transfer completion before calling the
 * next bitmap callback, so scratch remains owned until DMA2D has consumed it. */
typedef struct {
    uint16_t width, height;
    bool swap_bytes;
    uint8_t *scratch;
    size_t capacity;
#if CONFIG_APP_DISPLAY_PPA
    ppa_client_handle_t ppa;
    SemaphoreHandle_t done;
    bool ppa_failed;
#endif
} display_port_context_t;

#if CONFIG_APP_DISPLAY_PPA
/* Existing crash evidence hook, also retained for the legacy BSP dependency. */
void lvgl_port_ppa_timeout_hook(uint32_t width, uint32_t height, uint32_t x,
                              uint32_t y, uint32_t rotation, uintptr_t input,
                              uintptr_t output, uint32_t capacity);

static bool IRAM_ATTR rotation_done(ppa_client_handle_t client,
                                   ppa_event_data_t *event, void *user_ctx)
{
    (void)client;
    (void)event;
    display_port_context_t *ctx = user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(ctx->done, &woken);
    return woken == pdTRUE;
}

static esp_err_t rotate_ppa(display_port_context_t *ctx, const void *pixels,
                            display_rect_t logical, display_rect_t physical,
                            unsigned rotation)
{
    if (ctx->ppa_failed) return ESP_ERR_INVALID_STATE;
    const uint32_t width = logical.x2 - logical.x1;
    const uint32_t height = logical.y2 - logical.y1;
    ppa_srm_oper_config_t operation = {
        .in = {
            .buffer = pixels, .pic_w = width, .pic_h = height,
            .block_w = width, .block_h = height,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = ctx->scratch, .buffer_size = ctx->capacity,
            .pic_w = physical.x2 - physical.x1,
            .pic_h = physical.y2 - physical.y1,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = (ppa_srm_rotation_angle_t)rotation,
        .scale_x = 1, .scale_y = 1,
        .byte_swap = ctx->swap_bytes,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
        .user_data = ctx,
    };
    (void)xSemaphoreTake(ctx->done, 0);
    esp_err_t err = ppa_do_scale_rotate_mirror(ctx->ppa, &operation);
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(ctx->done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        /* Never submit or reuse uncertain output after a timed-out transaction,
         * even when the watchdog's repeated-restart guard declines to abort. */
        ctx->ppa_failed = true;
        lvgl_port_ppa_timeout_hook(width, height, logical.x1, logical.y1,
                                  rotation * 90U, (uintptr_t)pixels,
                                  (uintptr_t)ctx->scratch, ctx->capacity);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
#endif

static esp_err_t draw_bitmap(lv_display_t *disp, esp_lcd_panel_handle_t panel,
                             int x1, int y1, int x2, int y2,
                             const void *pixels, void *user_ctx)
{
    display_port_context_t *ctx = user_ctx;
    const display_rect_t logical = {x1, y1, x2, y2};
    display_rect_t physical;
    const unsigned rotation = lv_display_get_rotation(disp);
    if (!display_rotation_rect(logical, ctx->width, ctx->height, rotation, &physical)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int width = x2 - x1, height = y2 - y1;
    const size_t count = (size_t)width * height;
    uint8_t *output = (uint8_t *)pixels;
    if (rotation) {
        if (!ctx->scratch || count > ctx->capacity / sizeof(uint16_t)) return ESP_ERR_INVALID_SIZE;
#if CONFIG_APP_DISPLAY_PPA
        esp_err_t err = rotate_ppa(ctx, pixels, logical, physical, rotation);
        if (err != ESP_OK) return err;
#else
        /* The adapter callback receives tightly packed RGB565 after its stride
         * compaction. Use packed output too, including narrow/odd dirty areas. */
        lv_draw_sw_rotate(pixels, ctx->scratch, width, height, width * 2,
                          (physical.x2 - physical.x1) * 2,
                          (lv_display_rotation_t)rotation, LV_COLOR_FORMAT_RGB565);
        if (ctx->swap_bytes) lv_draw_sw_rgb565_swap(ctx->scratch, count);
#endif
        output = ctx->scratch;
    } else if (ctx->swap_bytes) {
        lv_draw_sw_rgb565_swap(output, count);
    }
    return esp_lcd_panel_draw_bitmap(panel, physical.x1, physical.y1,
                                     physical.x2, physical.y2, output);
}

static void context_destroy(display_port_context_t *ctx)
{
    if (!ctx) return;
#if CONFIG_APP_DISPLAY_PPA
    if (ctx->ppa) ppa_unregister_client(ctx->ppa);
    if (ctx->done) vSemaphoreDelete(ctx->done);
#endif
    heap_caps_free(ctx->scratch);
    free(ctx);
}

static display_port_context_t *context_create(uint16_t width, uint16_t height,
                                               size_t pixels, bool rotate, bool swap)
{
    display_port_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->width = width;
    ctx->height = height;
    ctx->swap_bytes = swap;
    if (!rotate) return ctx;
    const size_t align = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
    ctx->capacity = (pixels * sizeof(uint16_t) + align - 1) & ~(align - 1);
    ctx->scratch = heap_caps_aligned_alloc(align, ctx->capacity,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!ctx->scratch) goto fail;
#if CONFIG_APP_DISPLAY_PPA
    ctx->done = xSemaphoreCreateBinary();
    if (!ctx->done) goto fail;
    const ppa_client_config_t config = {.oper_type = PPA_OPERATION_SRM};
    if (ppa_register_client(&config, &ctx->ppa) != ESP_OK) goto fail;
    const ppa_event_callbacks_t callbacks = {.on_trans_done = rotation_done};
    if (ppa_client_register_event_callbacks(ctx->ppa, &callbacks) != ESP_OK) goto fail;
#endif
    return ctx;
fail:
    context_destroy(ctx);
    return NULL;
}

esp_err_t display_port_init(void)
{
    esp_lv_adapter_config_t config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    config.task_priority = 20;
    config.task_stack_size = APP_LVGL_TASK_STACK;
    config.task_core_id = 1;
    config.tick_period_ms = 5;
    config.task_max_delay_ms = 100;
    /* The LVGL task can access flash; its stack stays in internal RAM. LVGL
     * allocations and draw/rotation buffers retain the PSRAM-first policy. */
    config.stack_in_psram = false;
    return esp_lv_adapter_init(&config);
}

esp_err_t display_port_start(void)
{
    return esp_lv_adapter_start();
}

bool display_port_lock(uint32_t timeout_ms)
{
    /* Preserve the public display_lock(0) contract: wait indefinitely. */
    return esp_lv_adapter_lock(timeout_ms ? (int32_t)timeout_ms : -1) == ESP_OK;
}

void display_port_unlock(void)
{
    esp_lv_adapter_unlock();
}

lv_display_t *display_port_add_dsi(esp_lcd_panel_handle_t panel,
                                  esp_lcd_panel_io_handle_t io,
                                  uint16_t width, uint16_t height,
                                  bool rotate, bool swap_bytes)
{
    static const uint8_t divisors[] = {5, 8, 10, 12};
    for (size_t i = 0; i < sizeof(divisors); ++i) {
        const uint16_t rows = height / divisors[i];
        const size_t pixels = (size_t)width * rows;
        display_port_context_t *ctx = context_create(width, height, pixels, rotate, swap_bytes);
        if (!ctx) continue;
        esp_lv_adapter_display_config_t config = {
            .panel = panel, .panel_io = io,
            .profile = {
                .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
                .rotation = ESP_LV_ADAPTER_ROTATE_0,
                .hor_res = width, .ver_res = height, .buffer_height = rows,
                .use_psram = true, .enable_ppa_accel = false,
                .require_double_buffer = true,
            },
            .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
            .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
        };
        lv_display_t *disp = esp_lv_adapter_register_display(&config);
        if (disp) {
            const esp_lv_adapter_draw_bitmap_callbacks_t callbacks = {.custom_draw_bitmap = draw_bitmap};
            if (esp_lv_adapter_set_draw_bitmap_callbacks(disp, &callbacks, ctx) == ESP_OK) {
                ESP_LOGI(TAG_DISPLAY, "esp_lvgl_adapter DSI: partial RGB565, PSRAM double buffers, %zu pixels, rotation=%s",
                         pixels, rotate ?
#if CONFIG_APP_DISPLAY_PPA
                         "bounded PPA"
#else
                         "CPU"
#endif
                         : "none");
                return disp;
            }
            esp_lv_adapter_unregister_display(disp);
        }
        context_destroy(ctx);
        ESP_LOGW(TAG_DISPLAY, "Adapter registration failed at draw buffer 1/%u", divisors[i]);
    }
    return NULL;
}
