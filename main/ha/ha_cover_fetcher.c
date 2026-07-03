/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ha/ha_cover_fetcher.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "soc/soc_caps.h"

#include "app_config.h"
#include "miniz.h"

/* JPEG backend selection ---------------------------------------------------
 * - ESP32-P4 has a hardware JPEG codec. We use the IDF `driver/jpeg_decode`
 *   API for fast zero-copy RGB565 output.
 * - All other targets (e.g. ESP32-S3 / panels3) fall back to the ROM-resident
 *   TJpgDec software decoder. It is single-threaded, runs at low priority on
 *   core 1, and outputs RGB888 which we convert to RGB565 in the output
 *   callback. Sufficient for ~1 cover/sec at typical HA sizes. */
#if SOC_JPEG_DECODE_SUPPORTED
#include "driver/jpeg_decode.h"
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STBIDEF static
#define STBI_MALLOC(sz) heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(p, sz) heap_caps_realloc((p), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(p) heap_caps_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "../../managed_components/lvgl__lvgl/src/libs/gltf/stb_image/stb_image.h"
#else
#include "rom/tjpgd.h"
#endif

#include "ha/ha_client.h"

#define TAG "ha_cover"

#define COVER_Q_DEPTH 4
#define COVER_MAX_DOWNLOAD 1024 * 1024  /* 1 MiB upper bound per cover image */
#define COVER_GATE_POLL_MS 120
#define COVER_MIN_W 16
#define COVER_MIN_H 16
#define COVER_HTTP_WAIT_LOG_MS 5000
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
#define COVER_HTTP_MIN_INTERNAL_FREE_BYTES (12U * 1024U)
#define COVER_HTTP_MIN_INTERNAL_LARGEST_BYTES (8U * 1024U)
#define COVER_HTTP_PRESSURE_HOLD_MS 2000
#define COVER_HTTP_MAX_ATTEMPTS 6
#define COVER_HTTP_RETRY_DELAY_MS 2500
#define COVER_HTTP_BUFFER_SIZE 512
#define COVER_HTTP_TX_BUFFER_SIZE 768
#define COVER_HTTP_READ_CHUNK 512
#define COVER_SINK_INITIAL_CAP (8U * 1024U)
#else
#define COVER_HTTP_MIN_INTERNAL_FREE_BYTES (14U * 1024U)
#define COVER_HTTP_MIN_INTERNAL_LARGEST_BYTES (8U * 1024U)
#define COVER_HTTP_PRESSURE_HOLD_MS 1000
#define COVER_HTTP_MAX_ATTEMPTS 3
#define COVER_HTTP_RETRY_DELAY_MS 1000
#define COVER_HTTP_BUFFER_SIZE 1024
#define COVER_HTTP_TX_BUFFER_SIZE 1024
#define COVER_HTTP_READ_CHUNK 1024
#define COVER_SINK_INITIAL_CAP (32U * 1024U)
#endif

typedef struct {
    char url[512];         /* proxy path or absolute URL */
    int target_w;
    int target_h;
    ha_cover_cb_t cb;
    void *user;
} cover_req_t;

typedef struct {
    ha_cover_result_t result;
    ha_cover_cb_t cb;
    void *user;
} cover_dispatch_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_lock = NULL;
#if SOC_JPEG_DECODE_SUPPORTED
static jpeg_decoder_handle_t s_decoder = NULL;
#endif
static void *s_cancelled_user = NULL; /* serialised via s_lock with the worker */
static cover_req_t s_inflight = {0};  /* guarded by s_lock                    */
static bool s_inflight_valid = false;

static void cover_dispatch_on_lvgl(void *data);

/* ---- HTTP download into a heap buffer ---- */

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    bool truncated;
} http_sink_t;

typedef struct {
    ha_client_http_ctx_t ctx;
    char full_url[640];
    char auth[600];
} cover_http_work_t;

static void *cover_heap_calloc(size_t count, size_t size)
{
#if defined(CONFIG_SPIRAM)
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
}

static bool cover_request_cancelled_locked(void *user)
{
    return (s_cancelled_user != NULL && s_cancelled_user == user);
}

static bool cover_wait_until_http_ready(void *user)
{
    int64_t last_log_ms = 0;
    for (;;) {
        ha_client_set_aux_http_pressure(true, COVER_HTTP_PRESSURE_HOLD_MS);
        bool connected = ha_client_is_connected();
        bool heavy_busy = ha_client_heavy_gate_is_busy();
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        bool heap_ok = (free_internal >= COVER_HTTP_MIN_INTERNAL_FREE_BYTES) &&
                       (largest_internal >= COVER_HTTP_MIN_INTERNAL_LARGEST_BYTES);

        if (connected && !heavy_busy && heap_ok) {
            return true;
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_log_ms) >= COVER_HTTP_WAIT_LOG_MS) {
            last_log_ms = now_ms;
            ESP_LOGI(TAG,
                "Cover HTTP waiting: connected=%d heavy=%d free_internal=%u largest=%u",
                connected ? 1 : 0,
                heavy_busy ? 1 : 0,
                (unsigned)free_internal,
                (unsigned)largest_internal);
        }

        vTaskDelay(pdMS_TO_TICKS(COVER_GATE_POLL_MS));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool cancel_now = cover_request_cancelled_locked(user);
        xSemaphoreGive(s_lock);
        if (cancel_now) {
            ha_client_set_aux_http_pressure(false, 0);
            return false;
        }
    }
}

static bool cover_delay_or_cancel(void *user, uint32_t delay_ms)
{
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < delay_ms) {
        uint32_t step_ms = delay_ms - elapsed_ms;
        if (step_ms > COVER_GATE_POLL_MS) {
            step_ms = COVER_GATE_POLL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool cancel_now = cover_request_cancelled_locked(user);
        xSemaphoreGive(s_lock);
        if (cancel_now) {
            return false;
        }
    }
    return true;
}

static bool cover_is_png(const uint8_t *buf, size_t len)
{
    static const uint8_t magic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return buf != NULL && len >= sizeof(magic) && memcmp(buf, magic, sizeof(magic)) == 0;
}

static const char *cover_guess_format(const uint8_t *buf, size_t len)
{
    if (cover_is_png(buf, len)) return "png";
    if (buf != NULL && len >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) return "jpeg";
    if (buf != NULL && len >= 12 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WEBP", 4) == 0) return "webp";
    if (buf != NULL && len >= 6 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0)) return "gif";
    return "unknown";
}

static uint32_t cover_png_read_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static bool cover_png_get_header(const uint8_t *buf, size_t len,
                                 uint32_t *out_w, uint32_t *out_h,
                                 uint8_t *out_bit_depth, uint8_t *out_color_type,
                                 uint8_t *out_interlace)
{
    if (!cover_is_png(buf, len) || out_w == NULL || out_h == NULL || len < 29) {
        return false;
    }

    if (memcmp(buf + 12, "IHDR", 4) != 0) {
        return false;
    }

    *out_w = cover_png_read_be32(buf + 16);
    *out_h = cover_png_read_be32(buf + 20);
    if (out_bit_depth != NULL) {
        *out_bit_depth = buf[24];
    }
    if (out_color_type != NULL) {
        *out_color_type = buf[25];
    }
    if (out_interlace != NULL) {
        *out_interlace = buf[28];
    }
    return *out_w > 0 && *out_h > 0;
}

static void cover_png_free_decoded(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
#if defined(CONFIG_SPIRAM)
    heap_caps_free(ptr);
#else
    lv_free(ptr);
#endif
}

static bool cover_png_collect_idat(const uint8_t *png, size_t len, uint8_t **out_idat, size_t *out_len)
{
    if (png == NULL || out_idat == NULL || out_len == NULL || !cover_is_png(png, len)) {
        return false;
    }

    uint8_t *idat = NULL;
    size_t idat_len = 0;
    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t chunk_len = cover_png_read_be32(png + pos);
        size_t chunk_total = (size_t)chunk_len + 12U;
        if (pos + chunk_total > len || pos + chunk_total < pos) {
            cover_png_free_decoded(idat);
            return false;
        }

        const uint8_t *chunk_type = png + pos + 4;
        const uint8_t *chunk_data = png + pos + 8;
        if (memcmp(chunk_type, "IDAT", 4) == 0) {
            size_t new_len = idat_len + chunk_len;
            uint8_t *grown = heap_caps_realloc(idat, new_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (grown == NULL) {
                cover_png_free_decoded(idat);
                return false;
            }
            idat = grown;
            memcpy(idat + idat_len, chunk_data, chunk_len);
            idat_len = new_len;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }

        pos += chunk_total;
    }

    if (idat == NULL || idat_len == 0) {
        cover_png_free_decoded(idat);
        return false;
    }

    *out_idat = idat;
    *out_len = idat_len;
    return true;
}

static bool cover_png_read_palette(const uint8_t *png, size_t len,
                                   uint8_t *palette_rgb, uint8_t *palette_alpha,
                                   uint32_t *out_entries)
{
    if (png == NULL || palette_rgb == NULL || palette_alpha == NULL ||
        out_entries == NULL || !cover_is_png(png, len)) {
        return false;
    }

    memset(palette_rgb, 0, 256U * 3U);
    memset(palette_alpha, 0xFF, 256U);
    *out_entries = 0;

    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t chunk_len = cover_png_read_be32(png + pos);
        size_t chunk_total = (size_t)chunk_len + 12U;
        if (pos + chunk_total > len || pos + chunk_total < pos) {
            return false;
        }

        const uint8_t *chunk_type = png + pos + 4;
        const uint8_t *chunk_data = png + pos + 8;
        if (memcmp(chunk_type, "PLTE", 4) == 0) {
            if (chunk_len == 0 || (chunk_len % 3U) != 0) {
                return false;
            }
            uint32_t entries = chunk_len / 3U;
            if (entries > 256U) {
                entries = 256U;
            }
            memcpy(palette_rgb, chunk_data, (size_t)entries * 3U);
            *out_entries = entries;
        } else if (memcmp(chunk_type, "tRNS", 4) == 0) {
            uint32_t entries = chunk_len;
            if (entries > 256U) {
                entries = 256U;
            }
            memcpy(palette_alpha, chunk_data, entries);
        } else if (memcmp(chunk_type, "IDAT", 4) == 0 || memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }

        pos += chunk_total;
    }

    return *out_entries > 0;
}

static void cover_pick_png_output_size(uint32_t src_w, uint32_t src_h,
                                       int target_w, int target_h,
                                       uint32_t *out_w, uint32_t *out_h)
{
    if (out_w == NULL || out_h == NULL || src_w == 0 || src_h == 0) {
        return;
    }

    if (target_w <= 0 || target_h <= 0) {
        *out_w = src_w;
        *out_h = src_h;
        return;
    }

    uint32_t dst_w = (uint32_t)target_w;
    uint32_t dst_h = (uint32_t)(((uint64_t)src_h * dst_w) / src_w);
    if (dst_h == 0) {
        dst_h = 1;
    }

    if (dst_h > (uint32_t)target_h) {
        dst_h = (uint32_t)target_h;
        dst_w = (uint32_t)(((uint64_t)src_w * dst_h) / src_h);
        if (dst_w == 0) {
            dst_w = 1;
        }
    }

    if (dst_w > src_w) {
        dst_w = src_w;
    }
    if (dst_h > src_h) {
        dst_h = src_h;
    }

    *out_w = dst_w > 0 ? dst_w : 1;
    *out_h = dst_h > 0 ? dst_h : 1;
}

static uint32_t cover_png_source_bpp_for_type(uint8_t color_type)
{
    if (color_type == 3U) return 1U; /* indexed color, 8-bit palette index */
    if (color_type == 2U) return 3U; /* RGB8 */
    if (color_type == 6U) return 4U; /* RGBA8 */
    return 0;
}

static bool cover_png_is_transparent_bg(uint8_t red, uint8_t green, uint8_t blue)
{
    return red <= 2U && green <= 2U && blue <= 2U;
}

static uint8_t cover_png_paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

static bool cover_unfilter_png_row(uint8_t *dst, const uint8_t *src, const uint8_t *prev,
                                   size_t rowbytes, uint8_t filter, size_t bytewidth)
{
    if (dst == NULL || src == NULL || rowbytes == 0) {
        return false;
    }
    if (bytewidth == 0) {
        return false;
    }

    switch (filter) {
        case 0:
            memcpy(dst, src, rowbytes);
            return true;
        case 1:
            for (size_t i = 0; i < rowbytes; i++) {
                uint8_t left = i >= bytewidth ? dst[i - bytewidth] : 0U;
                dst[i] = (uint8_t)(src[i] + left);
            }
            return true;
        case 2:
            for (size_t i = 0; i < rowbytes; i++) {
                uint8_t up = prev != NULL ? prev[i] : 0U;
                dst[i] = (uint8_t)(src[i] + up);
            }
            return true;
        case 3:
            for (size_t i = 0; i < rowbytes; i++) {
                uint8_t left = i >= bytewidth ? dst[i - bytewidth] : 0U;
                uint8_t up = prev != NULL ? prev[i] : 0U;
                dst[i] = (uint8_t)(src[i] + (uint8_t)(((uint16_t)left + (uint16_t)up) / 2U));
            }
            return true;
        case 4:
            for (size_t i = 0; i < rowbytes; i++) {
                uint8_t left = i >= bytewidth ? dst[i - bytewidth] : 0U;
                uint8_t up = prev != NULL ? prev[i] : 0U;
                uint8_t up_left = (prev != NULL && i >= bytewidth) ? prev[i - bytewidth] : 0U;
                dst[i] = (uint8_t)(src[i] + cover_png_paeth(left, up, up_left));
            }
            return true;
        default:
            return false;
    }
}

static void cover_scale_png_row_to_rgb565a8(const uint8_t *src_row, uint32_t src_w, uint32_t source_bpp,
                                            const uint8_t *palette_rgb, const uint8_t *palette_alpha,
                                            uint32_t palette_entries,
                                            uint8_t *dst_color_row, uint8_t *dst_alpha_row,
                                            uint32_t dst_w)
{
    if (src_row == NULL || dst_color_row == NULL || dst_alpha_row == NULL ||
        src_w == 0 || dst_w == 0 ||
        (source_bpp != 1U && source_bpp != 3U && source_bpp != 4U)) {
        return;
    }
    if (source_bpp == 1U && (palette_rgb == NULL || palette_alpha == NULL || palette_entries == 0)) {
        return;
    }

    for (uint32_t x = 0; x < dst_w; x++) {
        uint32_t src_x = (uint32_t)(((uint64_t)x * src_w) / dst_w);
        if (src_x >= src_w) {
            src_x = src_w - 1U;
        }
        const uint8_t *px = src_row + ((size_t)src_x * source_bpp);
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        uint8_t alpha = 0;
        if (source_bpp == 1U) {
            uint8_t index = px[0];
            if (index < palette_entries) {
                const uint8_t *pal = palette_rgb + ((size_t)index * 3U);
                red = pal[0];
                green = pal[1];
                blue = pal[2];
                alpha = palette_alpha[index];
            }
        } else {
            red = px[0];
            green = px[1];
            blue = px[2];
            alpha = source_bpp == 4U
                        ? px[3]
                        : (cover_png_is_transparent_bg(red, green, blue) ? 0U : 0xFFU);
        }
        uint16_t rgb565 = (uint16_t)(((uint16_t)(red & 0xF8U) << 8) |
                                     ((uint16_t)(green & 0xFCU) << 3) |
                                     ((uint16_t)blue >> 3));
        dst_color_row[x * 2U] = (uint8_t)(rgb565 & 0xFFU);
        dst_color_row[x * 2U + 1U] = (uint8_t)(rgb565 >> 8);
        dst_alpha_row[x] = alpha;
    }
}

typedef struct {
    uint32_t src_w;
    uint32_t src_h;
    uint32_t source_bpp;
    const uint8_t *palette_rgb;
    const uint8_t *palette_alpha;
    uint32_t palette_entries;
    uint32_t out_w;
    uint32_t out_h;
    uint32_t color_stride;
    uint32_t alpha_stride;
    uint32_t next_dst_y;
    uint32_t src_y;
    uint32_t step_x;
    uint32_t step_y;
    uint32_t samples;
    uint64_t r_acc;
    uint64_t g_acc;
    uint64_t b_acc;
    uint8_t *line;
    size_t line_size;
    size_t line_len;
    uint8_t *prev_row;
    uint8_t *curr_row;
    uint8_t *rgb565a8;
    uint8_t *alpha_plane;
    size_t rowbytes;
    size_t expected_size;
    size_t emitted;
    bool failed;
} cover_png_stream_ctx_t;

static void cover_accumulate_dominant_png_row(const uint8_t *row, uint32_t src_w, uint32_t src_y,
                                              uint32_t source_bpp,
                                              const uint8_t *palette_rgb, const uint8_t *palette_alpha,
                                              uint32_t palette_entries,
                                              uint32_t step_x, uint32_t step_y,
                                              uint64_t *r_acc, uint64_t *g_acc, uint64_t *b_acc,
                                              uint32_t *samples)
{
    if (row == NULL || src_w == 0 || (source_bpp != 1U && source_bpp != 3U && source_bpp != 4U) ||
        r_acc == NULL || g_acc == NULL || b_acc == NULL || samples == NULL) {
        return;
    }
    if (source_bpp == 1U && (palette_rgb == NULL || palette_alpha == NULL || palette_entries == 0)) {
        return;
    }
    if (step_y == 0 || (src_y % step_y) != 0) {
        return;
    }

    for (uint32_t x = 0; x < src_w; x += step_x) {
        const uint8_t *px = row + ((size_t)x * source_bpp);
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        uint8_t alpha = 0;
        if (source_bpp == 1U) {
            uint8_t index = px[0];
            if (index >= palette_entries) {
                continue;
            }
            const uint8_t *pal = palette_rgb + ((size_t)index * 3U);
            red = pal[0];
            green = pal[1];
            blue = pal[2];
            alpha = palette_alpha[index];
        } else {
            red = px[0];
            green = px[1];
            blue = px[2];
            alpha = source_bpp == 4U
                        ? px[3]
                        : (cover_png_is_transparent_bg(red, green, blue) ? 0U : 0xFFU);
        }
        if (alpha < 24U) {
            continue;
        }

        uint16_t lum = (uint16_t)red + (uint16_t)green + (uint16_t)blue;
        if (lum < 18U || lum > 720U) {
            continue;
        }

        *r_acc += red;
        *g_acc += green;
        *b_acc += blue;
        (*samples)++;
    }
}

static bool cover_png_process_streamed_row(cover_png_stream_ctx_t *ctx)
{
    if (ctx == NULL || ctx->line == NULL || ctx->curr_row == NULL ||
        ctx->src_y >= ctx->src_h ||
        (ctx->source_bpp != 1U && ctx->source_bpp != 3U && ctx->source_bpp != 4U)) {
        return false;
    }

    if (!cover_unfilter_png_row(ctx->curr_row, ctx->line + 1U,
                                ctx->src_y > 0 ? ctx->prev_row : NULL,
                                ctx->rowbytes, ctx->line[0], ctx->source_bpp)) {
        ESP_LOGW(TAG, "png unsupported filter %u", ctx->line[0]);
        return false;
    }

    cover_accumulate_dominant_png_row(ctx->curr_row, ctx->src_w, ctx->src_y,
                                      ctx->source_bpp, ctx->palette_rgb, ctx->palette_alpha,
                                      ctx->palette_entries, ctx->step_x, ctx->step_y,
                                      &ctx->r_acc, &ctx->g_acc, &ctx->b_acc,
                                      &ctx->samples);

    while (ctx->next_dst_y < ctx->out_h &&
           (uint32_t)(((uint64_t)ctx->next_dst_y * ctx->src_h) / ctx->out_h) == ctx->src_y) {
        uint8_t *dst_color_row = ctx->rgb565a8 + ((size_t)ctx->next_dst_y * ctx->color_stride);
        uint8_t *dst_alpha_row = ctx->alpha_plane + ((size_t)ctx->next_dst_y * ctx->alpha_stride);
        cover_scale_png_row_to_rgb565a8(ctx->curr_row, ctx->src_w, ctx->source_bpp,
                                        ctx->palette_rgb, ctx->palette_alpha,
                                        ctx->palette_entries,
                                        dst_color_row, dst_alpha_row, ctx->out_w);
        ctx->next_dst_y++;
    }

    uint8_t *swap = ctx->prev_row;
    ctx->prev_row = ctx->curr_row;
    ctx->curr_row = swap;
    ctx->src_y++;
    ctx->line_len = 0;
    return true;
}

static int cover_png_inflate_cb(const void *buf, int len, void *user)
{
    cover_png_stream_ctx_t *ctx = (cover_png_stream_ctx_t *)user;
    if (ctx == NULL || buf == NULL || len < 0 || ctx->line == NULL) {
        return 0;
    }

    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = (size_t)len;
    while (remaining > 0) {
        if (ctx->emitted >= ctx->expected_size) {
            ctx->failed = true;
            return 0;
        }

        size_t line_space = ctx->line_size - ctx->line_len;
        size_t expected_left = ctx->expected_size - ctx->emitted;
        size_t take = remaining;
        if (take > line_space) {
            take = line_space;
        }
        if (take > expected_left) {
            take = expected_left;
        }
        if (take == 0) {
            ctx->failed = true;
            return 0;
        }

        memcpy(ctx->line + ctx->line_len, src, take);
        ctx->line_len += take;
        ctx->emitted += take;
        src += take;
        remaining -= take;

        if (ctx->line_len == ctx->line_size) {
            if (!cover_png_process_streamed_row(ctx)) {
                ctx->failed = true;
                return 0;
            }
        }
    }

    return 1;
}

static bool cover_png_inflate_stream_lowlevel(const uint8_t *idat, size_t idat_len,
                                              cover_png_stream_ctx_t *stream,
                                              size_t *consumed_out,
                                              tinfl_status *status_out)
{
    if (idat == NULL || idat_len == 0 || stream == NULL) {
        if (consumed_out != NULL) *consumed_out = 0;
        if (status_out != NULL) *status_out = TINFL_STATUS_BAD_PARAM;
        return false;
    }

    tinfl_decompressor *decomp = (tinfl_decompressor *)heap_caps_calloc(
        1, sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *dict = (uint8_t *)heap_caps_malloc(
        TINFL_LZ_DICT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (decomp == NULL) {
        decomp = (tinfl_decompressor *)heap_caps_calloc(
            1, sizeof(tinfl_decompressor), MALLOC_CAP_8BIT);
    }
    if (dict == NULL) {
        dict = (uint8_t *)heap_caps_malloc(TINFL_LZ_DICT_SIZE, MALLOC_CAP_8BIT);
    }
    if (decomp == NULL || dict == NULL) {
        ESP_LOGW(TAG, "png inflate alloc failed: decomp=%p dict=%p psram_free=%u psram_largest=%u",
                 decomp, dict,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        heap_caps_free(decomp);
        heap_caps_free(dict);
        if (consumed_out != NULL) *consumed_out = 0;
        if (status_out != NULL) *status_out = TINFL_STATUS_BAD_PARAM;
        return false;
    }

    tinfl_init(decomp);
    size_t in_ofs = 0;
    size_t dict_ofs = 0;
    tinfl_status status = TINFL_STATUS_FAILED;
    bool ok = false;

    for (;;) {
        size_t in_size = idat_len - in_ofs;
        size_t out_size = (size_t)TINFL_LZ_DICT_SIZE - dict_ofs;
        status = tinfl_decompress(decomp,
                                  idat + in_ofs,
                                  &in_size,
                                  dict,
                                  dict + dict_ofs,
                                  &out_size,
                                  TINFL_FLAG_PARSE_ZLIB_HEADER);
        in_ofs += in_size;

        if (out_size > 0) {
            if (!cover_png_inflate_cb(dict + dict_ofs, (int)out_size, stream)) {
                status = TINFL_STATUS_FAILED;
                break;
            }
            dict_ofs = (dict_ofs + out_size) & ((size_t)TINFL_LZ_DICT_SIZE - 1U);
        }

        if (status == TINFL_STATUS_DONE) {
            ok = true;
            break;
        }
        if (status < TINFL_STATUS_DONE) {
            break;
        }
        if (status == TINFL_STATUS_NEEDS_MORE_INPUT && in_ofs >= idat_len) {
            status = TINFL_STATUS_FAILED;
            break;
        }
        if (in_size == 0 && out_size == 0) {
            status = TINFL_STATUS_FAILED;
            break;
        }
    }

    if (consumed_out != NULL) *consumed_out = in_ofs;
    if (status_out != NULL) *status_out = status;
    heap_caps_free(decomp);
    heap_caps_free(dict);
    return ok;
}

static bool cover_decode_png_streamed(const uint8_t *png, size_t len,
                                      uint32_t src_w, uint32_t src_h,
                                      uint32_t source_bpp,
                                      const uint8_t *palette_rgb, const uint8_t *palette_alpha,
                                      uint32_t palette_entries,
                                      int target_w, int target_h,
                                      ha_cover_result_t *out)
{
    if (png == NULL || len == 0 || src_w == 0 || src_h == 0 ||
        (source_bpp != 1U && source_bpp != 3U && source_bpp != 4U) || out == NULL) {
        return false;
    }
    if (source_bpp == 1U && (palette_rgb == NULL || palette_alpha == NULL || palette_entries == 0)) {
        return false;
    }

    bool ok = false;
    uint8_t *idat = NULL;
    uint8_t *prev_row = NULL;
    uint8_t *curr_row = NULL;
    uint8_t *line = NULL;
    uint8_t *rgb565a8 = NULL;
    size_t idat_len = 0;
    if ((size_t)src_w > (SIZE_MAX - 1U) / source_bpp ||
        ((size_t)src_w * source_bpp + 1U) > SIZE_MAX / (size_t)src_h) {
        ESP_LOGW(TAG, "png stream size overflow");
        goto cleanup;
    }

    size_t rowbytes = (size_t)src_w * source_bpp;
    size_t expected_size = (rowbytes + 1U) * src_h;
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    uint32_t color_stride = 0;
    uint32_t alpha_stride = 0;
    uint64_t r_acc = 0;
    uint64_t g_acc = 0;
    uint64_t b_acc = 0;
    uint32_t samples = 0;
    uint32_t step_x = src_w > 96 ? src_w / 48U : 2U;
    uint32_t step_y = src_h > 96 ? src_h / 48U : 2U;
    if (step_x == 0) step_x = 1;
    if (step_y == 0) step_y = 1;

    if (!cover_png_collect_idat(png, len, &idat, &idat_len)) {
        ESP_LOGW(TAG, "png IDAT collect failed");
        goto cleanup;
    }

    line = heap_caps_malloc(rowbytes + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    prev_row = heap_caps_malloc(rowbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    curr_row = heap_caps_malloc(rowbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (line == NULL || prev_row == NULL || curr_row == NULL) {
        ESP_LOGW(TAG, "png row alloc failed: row=%u line=%u psram_free=%u psram_largest=%u",
                 (unsigned)rowbytes,
                 (unsigned)(rowbytes + 1U),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        goto cleanup;
    }
    memset(prev_row, 0, rowbytes);
    memset(curr_row, 0, rowbytes);
    memset(line, 0, rowbytes + 1U);

    cover_pick_png_output_size(src_w, src_h, target_w, target_h, &out_w, &out_h);
    color_stride = lv_draw_buf_width_to_stride(out_w, LV_COLOR_FORMAT_RGB565A8);
    alpha_stride = color_stride / 2U;
    if (out_w == 0 || out_h == 0 || color_stride == 0 || alpha_stride == 0 ||
        (size_t)color_stride > SIZE_MAX / (size_t)out_h ||
        (size_t)alpha_stride > SIZE_MAX / (size_t)out_h ||
        ((size_t)color_stride * (size_t)out_h) > SIZE_MAX - ((size_t)alpha_stride * (size_t)out_h)) {
        ESP_LOGW(TAG, "png output size overflow");
        goto cleanup;
    }
    size_t out_size = (size_t)color_stride * (size_t)out_h + (size_t)alpha_stride * (size_t)out_h;
    rgb565a8 = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb565a8 == NULL) {
        ESP_LOGW(TAG, "png alloc %u bytes failed", (unsigned)out_size);
        goto cleanup;
    }
    memset(rgb565a8, 0, out_size);

    uint8_t *alpha_plane = rgb565a8 + ((size_t)color_stride * out_h);
    cover_png_stream_ctx_t stream = {
        .src_w = src_w,
        .src_h = src_h,
        .source_bpp = source_bpp,
        .palette_rgb = palette_rgb,
        .palette_alpha = palette_alpha,
        .palette_entries = palette_entries,
        .out_w = out_w,
        .out_h = out_h,
        .color_stride = color_stride,
        .alpha_stride = alpha_stride,
        .step_x = step_x,
        .step_y = step_y,
        .line = line,
        .line_size = rowbytes + 1U,
        .prev_row = prev_row,
        .curr_row = curr_row,
        .rgb565a8 = rgb565a8,
        .alpha_plane = alpha_plane,
        .rowbytes = rowbytes,
        .expected_size = expected_size,
    };

    ESP_LOGI(TAG, "png stream: src=%ux%u bpp=%u palette=%u out=%ux%u idat=%u raw=%u",
             (unsigned)src_w, (unsigned)src_h,
             (unsigned)source_bpp, (unsigned)palette_entries,
             (unsigned)out_w, (unsigned)out_h,
             (unsigned)idat_len, (unsigned)expected_size);

    size_t consumed = 0;
    tinfl_status inflate_status = TINFL_STATUS_FAILED;
    bool inflate_ok = cover_png_inflate_stream_lowlevel(idat, idat_len, &stream,
                                                        &consumed, &inflate_status);
    if (!inflate_ok || stream.failed || stream.emitted != expected_size ||
        stream.src_y != src_h || stream.line_len != 0) {
        ESP_LOGW(TAG,
                 "png zlib stream failed: ok=%d status=%d failed=%d consumed=%u/%u emitted=%u/%u row=%u/%u line=%u psram_free=%u psram_largest=%u",
                 inflate_ok ? 1 : 0, (int)inflate_status, (int)stream.failed,
                 (unsigned)consumed, (unsigned)idat_len,
                 (unsigned)stream.emitted, (unsigned)expected_size,
                 (unsigned)stream.src_y, (unsigned)src_h,
                 (unsigned)stream.line_len,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        goto cleanup;
    }

    while (stream.next_dst_y < out_h) {
        uint8_t *dst_color_row = rgb565a8 + ((size_t)stream.next_dst_y * color_stride);
        uint8_t *dst_alpha_row = alpha_plane + ((size_t)stream.next_dst_y * alpha_stride);
        cover_scale_png_row_to_rgb565a8(stream.prev_row, src_w, source_bpp,
                                        palette_rgb, palette_alpha, palette_entries,
                                        dst_color_row, dst_alpha_row, out_w);
        stream.next_dst_y++;
    }

    out->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    out->image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    out->image.header.flags = 0;
    out->image.header.w = out_w;
    out->image.header.h = out_h;
    out->image.header.stride = color_stride;
    out->image.data_size = (size_t)color_stride * out_h + (size_t)alpha_stride * out_h;
    out->image.data = rgb565a8;
    out->source_w = src_w;
    out->source_h = src_h;
    samples = stream.samples;
    r_acc = stream.r_acc;
    g_acc = stream.g_acc;
    b_acc = stream.b_acc;
    out->dominant_rgb = samples > 0
                            ? ((((uint32_t)(r_acc / samples)) << 16) |
                               (((uint32_t)(g_acc / samples)) << 8) |
                               ((uint32_t)(b_acc / samples)))
                            : 0;
    out->valid = true;
    rgb565a8 = NULL;
    ok = true;

cleanup:
    cover_png_free_decoded(idat);
    cover_png_free_decoded(line);
    cover_png_free_decoded(prev_row);
    cover_png_free_decoded(curr_row);
    cover_png_free_decoded(rgb565a8);
    return ok;
}

static bool cover_decode_png(const uint8_t *png, size_t len, int target_w, int target_h,
                             ha_cover_result_t *out)
{
    if (png == NULL || len == 0 || out == NULL) {
        return false;
    }

    uint32_t src_w = 0;
    uint32_t src_h = 0;
    uint8_t bit_depth = 0;
    uint8_t color_type = 0;
    uint8_t interlace = 0;
    if (!cover_png_get_header(png, len, &src_w, &src_h, &bit_depth, &color_type, &interlace)) {
        ESP_LOGW(TAG, "png header parse failed");
        return false;
    }

    uint32_t source_bpp = cover_png_source_bpp_for_type(color_type);
    if (bit_depth == 8U && interlace == 0U && source_bpp > 0) {
        uint8_t palette_rgb[256U * 3U];
        uint8_t palette_alpha[256U];
        uint32_t palette_entries = 0;
        const uint8_t *palette_rgb_ptr = NULL;
        const uint8_t *palette_alpha_ptr = NULL;
        if (color_type == 3U) {
            if (!cover_png_read_palette(png, len, palette_rgb, palette_alpha, &palette_entries)) {
                ESP_LOGW(TAG, "png palette parse failed");
                return false;
            }
            palette_rgb_ptr = palette_rgb;
            palette_alpha_ptr = palette_alpha;
        }
        return cover_decode_png_streamed(png, len, src_w, src_h, source_bpp,
                                         palette_rgb_ptr, palette_alpha_ptr, palette_entries,
                                         target_w, target_h, out);
    }

    ESP_LOGW(TAG, "unsupported png cover format: %ux%u depth=%u type=%u interlace=%u",
             (unsigned)src_w, (unsigned)src_h,
             (unsigned)bit_depth, (unsigned)color_type, (unsigned)interlace);
    return false;
}

static esp_err_t cover_sink_append(http_sink_t *sink, const uint8_t *data, size_t len)
{
    if (sink == NULL || data == NULL || len == 0 || sink->truncated) {
        return ESP_OK;
    }

    size_t needed = sink->len + len;
    if (needed > COVER_MAX_DOWNLOAD) {
        sink->truncated = true;
        return ESP_ERR_INVALID_SIZE;
    }
    if (needed > sink->cap) {
        size_t new_cap = sink->cap ? sink->cap * 2 : COVER_SINK_INITIAL_CAP;
        while (new_cap < needed) new_cap *= 2;
        if (new_cap > COVER_MAX_DOWNLOAD) new_cap = COVER_MAX_DOWNLOAD;
        uint8_t *resized = heap_caps_realloc(sink->buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (resized == NULL) {
            sink->truncated = true;
            return ESP_ERR_NO_MEM;
        }
        sink->buf = resized;
        sink->cap = new_cap;
    }
    memcpy(sink->buf + sink->len, data, len);
    sink->len += len;
    return ESP_OK;
}

static const char *cover_url_kind(const char *url)
{
    if (url == NULL) {
        return "unknown";
    }
    if (strstr(url, "media_player_proxy") != NULL) {
        return "media";
    }
    if (strstr(url, "image_proxy") != NULL) {
        return "image";
    }
    if (strstr(url, "camera_proxy") != NULL) {
        return "camera";
    }
    return "cover";
}

static const char *cover_header_or_dash(esp_http_client_handle_t cli, const char *key)
{
    char *value = NULL;
    if (esp_http_client_get_header(cli, key, &value) == ESP_OK && value != NULL && value[0] != '\0') {
        return value;
    }
    return "-";
}

static esp_err_t cover_download(const char *url, http_sink_t *sink)
{
    cover_http_work_t *work = (cover_http_work_t *)cover_heap_calloc(1, sizeof(*work));
    if (work == NULL) {
        ESP_LOGW(TAG, "cover http work alloc failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_FAIL;
    uint8_t *read_buf = NULL;
    if (!ha_client_get_http_context(&work->ctx)) {
        ESP_LOGW(TAG, "no HA http context yet");
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        snprintf(work->full_url, sizeof(work->full_url), "%s", url);
    } else if (url[0] == '/') {
        snprintf(work->full_url, sizeof(work->full_url), "%s%s", work->ctx.base_url, url);
    } else {
        snprintf(work->full_url, sizeof(work->full_url), "%s/%s", work->ctx.base_url, url);
    }
    const char *url_kind = cover_url_kind(work->full_url);

    esp_http_client_config_t cfg = {
        .url = work->full_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = COVER_HTTP_BUFFER_SIZE,
        .buffer_size_tx = COVER_HTTP_TX_BUFFER_SIZE,
        .keep_alive_enable = false,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    if (work->ctx.cert_common_name[0] != '\0') cfg.common_name = work->ctx.cert_common_name;

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    if (work->ctx.host_header[0] != '\0') {
        esp_http_client_set_header(cli, "Host", work->ctx.host_header);
    }
    esp_http_client_set_header(cli, "Connection", "close");
    esp_http_client_set_header(cli, "Accept", "image/*");
    /* The HA media_player_proxy URLs already carry a ?token=... query; a Bearer
     * header is harmless but unnecessary and would bloat the request.  Only
     * attach it for URLs that don't include a token. */
    if (strstr(work->full_url, "token=") == NULL && work->ctx.bearer_token[0] != '\0') {
        snprintf(work->auth, sizeof(work->auth), "Bearer %s", work->ctx.bearer_token);
        esp_http_client_set_header(cli, "Authorization", work->auth);
    }

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http open failed: %s errno=%d free_internal=%u largest=%u",
            esp_err_to_name(err),
            esp_http_client_get_errno(cli),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        ret = err;
        goto client_cleanup;
    }

    int64_t content_length = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (content_length < 0) {
        ESP_LOGW(TAG, "http fetch headers failed: %" PRId64, content_length);
        ret = ESP_ERR_HTTP_FETCH_HEADER;
        goto client_close;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "http status=%d for cover fetch", status);
        ret = ESP_ERR_HTTP_BASE + status;
        goto client_close;
    }
    if (content_length > COVER_MAX_DOWNLOAD) {
        ESP_LOGW(TAG, "cover content-length too large: %" PRId64, content_length);
        ret = ESP_ERR_INVALID_SIZE;
        goto client_close;
    }

    ESP_LOGI(TAG,
        "Cover HTTP headers kind=%s status=%d len=%" PRId64 " type=%s transfer=%s encoding=%s conn=%s",
        url_kind,
        status,
        content_length,
        cover_header_or_dash(cli, "Content-Type"),
        cover_header_or_dash(cli, "Transfer-Encoding"),
        cover_header_or_dash(cli, "Content-Encoding"),
        cover_header_or_dash(cli, "Connection"));

    read_buf = (uint8_t *)heap_caps_malloc(COVER_HTTP_READ_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (read_buf == NULL) {
        read_buf = (uint8_t *)heap_caps_malloc(COVER_HTTP_READ_CHUNK, MALLOC_CAP_8BIT);
    }
    if (read_buf == NULL) {
        ESP_LOGW(TAG, "cover read buffer alloc failed (%u bytes)", (unsigned)COVER_HTTP_READ_CHUNK);
        ret = ESP_ERR_NO_MEM;
        goto client_close;
    }

    uint8_t zero_read_count = 0;
    const bool known_length = (content_length > 0);
    while (true) {
        int read_len = esp_http_client_read(cli, (char *)read_buf, COVER_HTTP_READ_CHUNK);
        if (read_len < 0) {
            int tls_code = 0;
            int tls_flags = 0;
            esp_err_t tls_err = esp_http_client_get_and_clear_last_tls_error(cli, &tls_code, &tls_flags);
            ESP_LOGW(TAG,
                "http read failed: %d kind=%s status=%d got=%u/%" PRId64
                " errno=%d tls_err=%s tls_code=0x%x tls_flags=0x%x complete=%d free_internal=%u largest=%u",
                read_len,
                url_kind,
                status,
                (unsigned)sink->len,
                content_length,
                esp_http_client_get_errno(cli),
                esp_err_to_name(tls_err),
                (unsigned)tls_code,
                (unsigned)tls_flags,
                esp_http_client_is_complete_data_received(cli) ? 1 : 0,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            ret = (read_len == -ESP_ERR_HTTP_EAGAIN) ? ESP_ERR_HTTP_EAGAIN : ESP_FAIL;
            goto client_close;
        }
        if (read_len == 0) {
            if (esp_http_client_is_complete_data_received(cli) ||
                (known_length && sink->len >= (size_t)content_length)) {
                break;
            }
            zero_read_count++;
            if (zero_read_count < 3) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            ESP_LOGW(TAG, "http incomplete data received, %u/%" PRId64 " bytes",
                (unsigned)sink->len, content_length);
            ret = ESP_ERR_HTTP_INCOMPLETE_DATA;
            goto client_close;
        }
        zero_read_count = 0;
        ret = cover_sink_append(sink, read_buf, (size_t)read_len);
        if (ret != ESP_OK) {
            goto client_close;
        }
    }

    if (sink->truncated) {
        ESP_LOGW(TAG, "cover exceeds %d bytes, truncated", COVER_MAX_DOWNLOAD);
        ret = ESP_ERR_INVALID_SIZE;
        goto client_close;
    }
    ret = (sink->len >= 64) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;

client_close:
    if (read_buf != NULL) {
        heap_caps_free(read_buf);
    }
    esp_http_client_close(cli);
client_cleanup:
    esp_http_client_cleanup(cli);
cleanup:
    heap_caps_free(work);
    return ret;
}

static bool cover_download_error_is_retryable(esp_err_t err)
{
    return err == ESP_ERR_HTTP_CONNECT ||
           err == ESP_ERR_HTTP_FETCH_HEADER ||
           err == ESP_ERR_HTTP_EAGAIN ||
           err == ESP_ERR_HTTP_CONNECTION_CLOSED ||
           err == ESP_ERR_HTTP_INCOMPLETE_DATA ||
           err == ESP_ERR_TIMEOUT ||
           err == ESP_ERR_NO_MEM ||
           err == ESP_FAIL;
}

/* ---- Decode + dominant color ---- */

static bool cover_jpeg_find_sof(const uint8_t *buf, size_t len,
                                uint8_t *marker_out, uint16_t *width_out,
                                uint16_t *height_out, uint8_t *components_out)
{
    if (buf == NULL || len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        return false;
    }

    size_t pos = 2;
    while (pos + 1 < len) {
        while (pos < len && buf[pos] != 0xFF) {
            pos++;
        }
        while (pos < len && buf[pos] == 0xFF) {
            pos++;
        }
        if (pos >= len) {
            break;
        }

        uint8_t marker = buf[pos++];
        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;
        }
        if (pos + 2 > len) {
            break;
        }

        uint16_t seg_len = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
        if (seg_len < 2 || pos + seg_len > len) {
            break;
        }

        bool is_sof = (marker >= 0xC0 && marker <= 0xCF &&
                       marker != 0xC4 && marker != 0xC8 && marker != 0xCC);
        if (is_sof) {
            if (seg_len < 8) {
                return false;
            }
            if (marker_out != NULL) *marker_out = marker;
            if (height_out != NULL) *height_out = ((uint16_t)buf[pos + 3] << 8) | buf[pos + 4];
            if (width_out != NULL) *width_out = ((uint16_t)buf[pos + 5] << 8) | buf[pos + 6];
            if (components_out != NULL) *components_out = buf[pos + 7];
            return true;
        }

        pos += seg_len;
    }

    return false;
}

static const char *cover_jpeg_sof_name(uint8_t marker)
{
    switch (marker) {
        case 0xC0: return "SOF0 baseline";
        case 0xC1: return "SOF1 extended sequential";
        case 0xC2: return "SOF2 progressive";
        case 0xC3: return "SOF3 lossless";
        case 0xC5: return "SOF5 differential sequential";
        case 0xC6: return "SOF6 differential progressive";
        case 0xC7: return "SOF7 differential lossless";
        case 0xC9: return "SOF9 arithmetic sequential";
        case 0xCA: return "SOF10 arithmetic progressive";
        case 0xCB: return "SOF11 arithmetic lossless";
        case 0xCD: return "SOF13 arithmetic differential sequential";
        case 0xCE: return "SOF14 arithmetic differential progressive";
        case 0xCF: return "SOF15 arithmetic differential lossless";
        default: return "SOF unknown";
    }
}

static void cover_log_jpeg_sof(const uint8_t *buf, size_t len)
{
    uint8_t marker = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t components = 0;
    if (cover_jpeg_find_sof(buf, len, &marker, &width, &height, &components)) {
        ESP_LOGI(TAG, "jpeg SOF marker: 0x%02x (%s) %ux%u components=%u",
                 (unsigned)marker, cover_jpeg_sof_name(marker),
                 (unsigned)width, (unsigned)height, (unsigned)components);
    } else {
        ESP_LOGW(TAG, "jpeg SOF marker not found");
    }
}

/* Pick sample_method that yields an output no larger than max(target_w,target_h)*1.5
 * while still keeping enough pixels for color analysis. Hardware supports 1/1, 1/2,
 * 1/4 and 1/8 scales through jpeg_down_sampling_type_t; we rely on it implicitly by
 * letting the decoder choose a sample method consistent with the picture info. */

/* Sanitize a JPEG bitstream in-place by removing unreliable optional segments
 * (COM=0xFFFE and APPn with n>=2) before the SOF marker. Some HA media sources
 * (Spotify proxy) emit COM segments with a declared length that exceeds the
 * remaining bytes, which the ESP32-P4 HW decoder rejects with "COM marker
 * data underflow". Stripping them keeps all image data intact.
 *
 * Returns the new length. Operates only between SOI and the first SOF. */
static size_t cover_jpeg_sanitize(uint8_t *buf, size_t len)
{
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return len;
    size_t rp = 2; /* read pointer, after SOI */
    size_t wp = 2; /* write pointer */
    while (rp + 3 < len) {
        if (buf[rp] != 0xFF) {
            /* Not a marker boundary — bail out, copy remainder verbatim. */
            break;
        }
        uint8_t m = buf[rp + 1];
        /* Fill bytes: 0xFF 0xFF ... */
        if (m == 0xFF) { buf[wp++] = 0xFF; rp++; continue; }
        /* Markers without payload: SOI/EOI/RSTn/TEM */
        if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
            if (wp != rp) { buf[wp] = 0xFF; buf[wp + 1] = m; }
            wp += 2; rp += 2;
            continue;
        }
        /* SOF / SOS / DQT / DHT / DRI / JFIF(APP0) / Exif(APP1): keep.
         * COM (0xFE) and APPn with n>=2: drop. */
        if (rp + 4 > len) break;
        uint16_t seg_len = ((uint16_t)buf[rp + 2] << 8) | buf[rp + 3];
        if (seg_len < 2 || rp + 2 + seg_len > len) {
            /* Malformed length — keep from here onwards verbatim so decoder
             * can still reach SOS if possible. */
            break;
        }
        bool drop = (m == 0xFE) || (m >= 0xE2 && m <= 0xEF);
        if (m == 0xDA /* SOS */) {
            /* After SOS comes compressed entropy data; stop filtering. */
            size_t remain = len - rp;
            if (wp != rp) memmove(buf + wp, buf + rp, remain);
            return wp + remain;
        }
        if (drop) {
            rp += 2 + seg_len;
        } else {
            size_t seg_total = 2 + seg_len;
            if (wp != rp) memmove(buf + wp, buf + rp, seg_total);
            wp += seg_total;
            rp += seg_total;
        }
    }
    /* Copy any remaining bytes verbatim. */
    size_t remain = len - rp;
    if (remain > 0) {
        if (wp != rp) memmove(buf + wp, buf + rp, remain);
        wp += remain;
    }
    return wp;
}

#if SOC_JPEG_DECODE_SUPPORTED
static bool cover_decode(const uint8_t *jpg, size_t len, int target_w, int target_h,
                          ha_cover_result_t *out)
{
    jpeg_decode_picture_info_t pic = {0};
    if (jpeg_decoder_get_info(jpg, len, &pic) != ESP_OK) {
        ESP_LOGW(TAG, "jpeg header parse failed");
        return false;
    }
    ESP_LOGD(TAG, "jpeg info: src=%ux%u sample=%u",
             (unsigned)pic.width, (unsigned)pic.height,
             (unsigned)pic.sample_method);
    if (pic.width == 0 || pic.height == 0) {
        ESP_LOGW(TAG, "jpeg header has zero dimensions");
        cover_log_jpeg_sof(jpg, len);
        return false;
    }

    /* Decoder output dimensions must be a multiple of 16. The HW decoder does
     * not downscale: the output always has the source resolution (rounded up
     * to 16). We therefore allocate based on the actual picture size and only
     * reject images that would exceed a sane PSRAM budget. */
    uint32_t out_w = (pic.width + 15U) & ~15U;
    uint32_t out_h = (pic.height + 15U) & ~15U;
    (void)target_w;
    (void)target_h;

    /* Hard cap ~4 MB RGB565 (≈1440x1440). HA album art is typically ≤ 640²
     * which is ~0.8 MB. */
    const uint32_t MAX_PIXELS = 1440U * 1440U;
    if ((uint32_t)out_w * out_h > MAX_PIXELS) {
        ESP_LOGW(TAG, "jpeg %ux%u exceeds decode cap, skipping",
                 (unsigned)pic.width, (unsigned)pic.height);
        return false;
    }

    size_t rgb_size = (size_t)out_w * out_h * 2; /* RGB565 */
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t allocated = 0;
    uint8_t *rgb = jpeg_alloc_decoder_mem(rgb_size, &mem_cfg, &allocated);
    if (rgb == NULL) {
        ESP_LOGW(TAG, "jpeg alloc output %u failed", (unsigned)rgb_size);
        return false;
    }

    /* LVGL 9 with LV_COLOR_FORMAT_RGB565 reads native little-endian 16-bit
     * words with R in the high 5 bits. The HW JPEG decoder writes the RGB
     * components MSB first when ELEMENT_ORDER_BGR is selected, which matches
     * LVGL's memory layout. Using RGB order here produces swapped colors
     * that look grainy/miscolored. */
    jpeg_decode_cfg_t dcfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t produced = 0;
    esp_err_t err = jpeg_decoder_process(s_decoder, &dcfg, jpg, len, rgb, allocated, &produced);
    if (err != ESP_OK || produced == 0) {
        ESP_LOGW(TAG, "jpeg decode failed: %s", esp_err_to_name(err));
        heap_caps_free(rgb);
        return false;
    }

    out->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    out->image.header.cf = LV_COLOR_FORMAT_RGB565;
    out->image.header.flags = 0;
    out->image.header.w = (uint32_t)out_w;
    out->image.header.h = (uint32_t)out_h;
    out->image.header.stride = (uint32_t)(out_w * 2);
    out->image.data_size = produced;
    out->image.data = rgb;
    out->source_w = pic.width;
    out->source_h = pic.height;

    /* Dominant color: coarse bucket histogram over a downsampled grid. */
    const int STEP = 8;
    uint32_t r_acc = 0, g_acc = 0, b_acc = 0, n = 0;
    const uint16_t *px = (const uint16_t *)rgb;
    for (uint32_t y = 0; y < out_h; y += STEP) {
        for (uint32_t x = 0; x < out_w; x += STEP) {
            uint16_t v = px[y * out_w + x];
            uint8_t r = (v >> 11) & 0x1F;
            uint8_t g = (v >> 5) & 0x3F;
            uint8_t b = v & 0x1F;
            /* Skip near-black and near-white pixels that would bias the mean
             * towards grey when the cover has a bold border. */
            int lum = r + g / 2 + b; /* rough luminance in 5-bit scale */
            if (lum < 4 || lum > 50) continue;
            r_acc += r;
            g_acc += g;
            b_acc += b;
            n++;
        }
    }
    uint32_t r8, g8, b8;
    if (n > 0) {
        r8 = (r_acc / n) * 255U / 31U;
        g8 = (g_acc / n) * 255U / 63U;
        b8 = (b_acc / n) * 255U / 31U;
    } else {
        r8 = 0x4D; g8 = 0xA3; b8 = 0xFF;
    }
    out->dominant_rgb = (r8 << 16) | (g8 << 8) | b8;
    out->valid = true;
    return true;
}

static bool cover_decode_stb_jpeg(const uint8_t *jpg, size_t len, int target_w, int target_h,
                                  ha_cover_result_t *out)
{
    if (jpg == NULL || out == NULL || len == 0 || len > INT32_MAX) {
        return false;
    }

    int info_w = 0;
    int info_h = 0;
    int info_comp = 0;
    if (!stbi_info_from_memory(jpg, (int)len, &info_w, &info_h, &info_comp)) {
        ESP_LOGW(TAG, "stb jpeg info failed: %s", stbi_failure_reason());
        return false;
    }
    if (info_w <= 0 || info_h <= 0) {
        ESP_LOGW(TAG, "stb jpeg invalid dimensions: %dx%d", info_w, info_h);
        return false;
    }

    const uint32_t MAX_PIXELS = 1440U * 1440U;
    if ((uint32_t)info_w * (uint32_t)info_h > MAX_PIXELS) {
        ESP_LOGW(TAG, "stb jpeg %dx%d exceeds decode cap, skipping", info_w, info_h);
        return false;
    }

    int src_w = 0;
    int src_h = 0;
    int src_comp = 0;
    int64_t t0 = esp_timer_get_time();
    uint8_t *rgb24 = stbi_load_from_memory(jpg, (int)len, &src_w, &src_h, &src_comp, 3);
    if (rgb24 == NULL) {
        ESP_LOGW(TAG, "stb jpeg decode failed: %s", stbi_failure_reason());
        return false;
    }
    if (src_w <= 0 || src_h <= 0) {
        ESP_LOGW(TAG, "stb jpeg decoded invalid dimensions: %dx%d", src_w, src_h);
        stbi_image_free(rgb24);
        return false;
    }

    uint32_t out_w = 0;
    uint32_t out_h = 0;
    cover_pick_png_output_size((uint32_t)src_w, (uint32_t)src_h, target_w, target_h, &out_w, &out_h);
    if (out_w == 0 || out_h == 0) {
        stbi_image_free(rgb24);
        return false;
    }

    size_t rgb_size = (size_t)out_w * out_h * 2U;
    uint8_t *rgb565 = (uint8_t *)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb565 == NULL) {
        rgb565 = (uint8_t *)heap_caps_malloc(rgb_size, MALLOC_CAP_8BIT);
    }
    if (rgb565 == NULL) {
        ESP_LOGW(TAG, "stb jpeg alloc output %u failed", (unsigned)rgb_size);
        stbi_image_free(rgb24);
        return false;
    }

    uint32_t r_acc = 0;
    uint32_t g_acc = 0;
    uint32_t b_acc = 0;
    uint32_t n_acc = 0;
    uint16_t *dst = (uint16_t *)rgb565;
    for (uint32_t y = 0; y < out_h; y++) {
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint32_t)src_h) / out_h);
        const uint8_t *src_row = rgb24 + (size_t)sy * (uint32_t)src_w * 3U;
        for (uint32_t x = 0; x < out_w; x++) {
            uint32_t sx = (uint32_t)(((uint64_t)x * (uint32_t)src_w) / out_w);
            const uint8_t *px = src_row + (size_t)sx * 3U;
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            dst[(size_t)y * out_w + x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            if (((x & 7U) == 0U) && ((y & 7U) == 0U)) {
                int lum = (r >> 3) + ((g >> 2) >> 1) + (b >> 3);
                if (lum >= 4 && lum <= 50) {
                    r_acc += r;
                    g_acc += g;
                    b_acc += b;
                    n_acc++;
                }
            }
        }
    }
    stbi_image_free(rgb24);

    ESP_LOGI(TAG, "stb jpeg decoded %dx%d comp=%d -> %ux%u in %lld ms",
             src_w, src_h, src_comp, (unsigned)out_w, (unsigned)out_h,
             (long long)((esp_timer_get_time() - t0) / 1000));

    out->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    out->image.header.cf = LV_COLOR_FORMAT_RGB565;
    out->image.header.flags = 0;
    out->image.header.w = out_w;
    out->image.header.h = out_h;
    out->image.header.stride = out_w * 2U;
    out->image.data_size = (uint32_t)rgb_size;
    out->image.data = rgb565;
    out->source_w = (uint32_t)src_w;
    out->source_h = (uint32_t)src_h;
    out->dominant_rgb = n_acc > 0
        ? (((r_acc / n_acc) << 16) | ((g_acc / n_acc) << 8) | (b_acc / n_acc))
        : ((0x4DU << 16) | (0xA3U << 8) | 0xFFU);
    out->valid = true;
    return true;
}
#else /* !SOC_JPEG_DECODE_SUPPORTED — software path via ROM TJpgDec */

/* TJpgDec session state. The decoder is created on demand for each cover
 * because TJpgDec is not designed for reuse across images. */
typedef struct {
    /* Input */
    const uint8_t *src;
    size_t src_len;
    size_t src_pos;
    /* Output */
    uint8_t *rgb565;       /* destination RGB565 buffer (LVGL native) */
    uint32_t out_w;        /* output buffer width  in pixels */
    uint32_t out_h;        /* output buffer height in pixels */
    /* Stats for dominant-color computation */
    uint32_t r_acc, g_acc, b_acc, n_acc;
} cover_sw_ctx_t;

static UINT cover_sw_in(JDEC *jd, BYTE *buf, UINT nb)
{
    cover_sw_ctx_t *ctx = (cover_sw_ctx_t *)jd->device;
    size_t remain = ctx->src_len - ctx->src_pos;
    if ((size_t)nb > remain) {
        nb = (UINT)remain;
    }
    if (buf != NULL && nb > 0) {
        memcpy(buf, ctx->src + ctx->src_pos, nb);
    }
    ctx->src_pos += nb;
    return nb;
}

/* TJpgDec ROM build outputs RGB888 (3 bytes/pixel). We convert each row to
 * RGB565 (LVGL native, little-endian) directly into the destination buffer. */
static UINT cover_sw_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    cover_sw_ctx_t *ctx = (cover_sw_ctx_t *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    const uint32_t rect_w = (uint32_t)(rect->right - rect->left + 1);
    const uint32_t rect_h = (uint32_t)(rect->bottom - rect->top + 1);

    for (uint32_t row = 0; row < rect_h; ++row) {
        const uint32_t y = (uint32_t)rect->top + row;
        if (y >= ctx->out_h) {
            break;
        }
        uint16_t *dst_row = (uint16_t *)(ctx->rgb565 + (size_t)y * ctx->out_w * 2);
        const uint8_t *src_row = src + (size_t)row * rect_w * 3;
        for (uint32_t col = 0; col < rect_w; ++col) {
            const uint32_t x = (uint32_t)rect->left + col;
            if (x >= ctx->out_w) {
                break;
            }
            uint8_t r = src_row[col * 3 + 0];
            uint8_t g = src_row[col * 3 + 1];
            uint8_t b = src_row[col * 3 + 2];
            dst_row[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            /* Sample every 8th pixel for the dominant-color histogram, with
             * the same near-black/near-white skip the HW path uses. */
            if (((x & 7U) == 0U) && ((y & 7U) == 0U)) {
                int lum = (r >> 3) + ((g >> 2) >> 1) + (b >> 3);
                if (lum >= 4 && lum <= 50) {
                    ctx->r_acc += r;
                    ctx->g_acc += g;
                    ctx->b_acc += b;
                    ctx->n_acc += 1;
                }
            }
        }
    }
    return 1; /* continue */
}

static uint8_t cover_sw_pick_scale(uint32_t src_w, uint32_t src_h,
                                    int target_w, int target_h)
{
    if (target_w <= 0 || target_h <= 0) return 0;
    /* TJpgDec supports 0=1/1, 1=1/2, 2=1/4, 3=1/8 */
    for (uint8_t s = 3; s > 0; --s) {
        uint32_t sw = src_w >> s;
        uint32_t sh = src_h >> s;
        if (sw >= (uint32_t)target_w && sh >= (uint32_t)target_h) {
            return s;
        }
    }
    return 0;
}

static bool cover_decode(const uint8_t *jpg, size_t len, int target_w, int target_h,
                          ha_cover_result_t *out)
{
    /* TJpgDec needs a scratch "pool" of ~3.5 KiB for its work tables. */
    enum { TJPGD_POOL_SZ = 3800 };
    void *pool = NULL;
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480) && defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    pool = heap_caps_malloc(TJPGD_POOL_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (pool == NULL) {
        pool = heap_caps_malloc(TJPGD_POOL_SZ, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    if (pool == NULL) {
        ESP_LOGW(TAG, "tjpgd pool alloc failed");
        return false;
    }

    cover_sw_ctx_t ctx = {0};
    ctx.src = jpg;
    ctx.src_len = len;

    JDEC jd;
    JRESULT jr = jd_prepare(&jd, cover_sw_in, pool, TJPGD_POOL_SZ, &ctx);
    if (jr != JDR_OK) {
        ESP_LOGW(TAG, "tjpgd jd_prepare failed: %d", (int)jr);
        heap_caps_free(pool);
        return false;
    }
    if (jd.width == 0 || jd.height == 0) {
        heap_caps_free(pool);
        return false;
    }

    uint8_t scale = cover_sw_pick_scale(jd.width, jd.height, target_w, target_h);
    uint32_t out_w = (uint32_t)jd.width >> scale;
    uint32_t out_h = (uint32_t)jd.height >> scale;
    if (out_w == 0 || out_h == 0) {
        heap_caps_free(pool);
        return false;
    }

    /* Hard cap ~4 MB RGB565 (≈1440x1440). */
    const uint32_t MAX_PIXELS = 1440U * 1440U;
    if ((uint32_t)out_w * out_h > MAX_PIXELS) {
        ESP_LOGW(TAG, "jpeg %ux%u (scale 1/%u) exceeds decode cap, skipping",
                 (unsigned)jd.width, (unsigned)jd.height, 1U << scale);
        heap_caps_free(pool);
        return false;
    }

    size_t rgb_size = (size_t)out_w * out_h * 2;
    uint8_t *rgb = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb == NULL) {
        rgb = heap_caps_malloc(rgb_size, MALLOC_CAP_8BIT);
    }
    if (rgb == NULL) {
        ESP_LOGW(TAG, "tjpgd alloc output %u failed", (unsigned)rgb_size);
        heap_caps_free(pool);
        return false;
    }
    ctx.rgb565 = rgb;
    ctx.out_w = out_w;
    ctx.out_h = out_h;

    int64_t t0 = esp_timer_get_time();
    jr = jd_decomp(&jd, cover_sw_out, scale);
    heap_caps_free(pool);
    if (jr != JDR_OK) {
        ESP_LOGW(TAG, "tjpgd decode failed: %d", (int)jr);
        heap_caps_free(rgb);
        return false;
    }
    ESP_LOGD(TAG, "sw jpeg decoded %ux%u (scale 1/%u) in %lld ms",
             (unsigned)out_w, (unsigned)out_h, 1U << scale,
             (long long)((esp_timer_get_time() - t0) / 1000));

    out->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    out->image.header.cf = LV_COLOR_FORMAT_RGB565;
    out->image.header.flags = 0;
    out->image.header.w = out_w;
    out->image.header.h = out_h;
    out->image.header.stride = out_w * 2;
    out->image.data_size = (uint32_t)rgb_size;
    out->image.data = rgb;
    out->source_w = jd.width;
    out->source_h = jd.height;

    if (ctx.n_acc > 0) {
        out->dominant_rgb = ((ctx.r_acc / ctx.n_acc) << 16) |
                            ((ctx.g_acc / ctx.n_acc) << 8) |
                            (ctx.b_acc / ctx.n_acc);
    } else {
        out->dominant_rgb = (0x4DU << 16) | (0xA3U << 8) | 0xFFU;
    }
    out->valid = true;
    return true;
}
#endif /* SOC_JPEG_DECODE_SUPPORTED */

/* ---- Worker task ---- */

static void cover_task(void *arg)
{
    (void)arg;
    for (;;) {
        cover_req_t req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        /* Mark as in-flight so cancel() can invalidate us. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_inflight = req;
        s_inflight_valid = true;
        void *cancelled = s_cancelled_user;
        xSemaphoreGive(s_lock);

        if (cancelled != NULL && cancelled == req.user) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_inflight_valid = false;
            s_cancelled_user = NULL;
            xSemaphoreGive(s_lock);
            continue;
        }

        /* 1) Wait for WS liveness, the heavy WS gate and a small internal
         *    heap/socket reserve. Cover fetches are nice-to-have UI work. */
        if (!cover_wait_until_http_ready(req.user)) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (cover_request_cancelled_locked(req.user)) {
                s_cancelled_user = NULL;
            }
            xSemaphoreGive(s_lock);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_inflight_valid = false;
            xSemaphoreGive(s_lock);
            continue;
        }

        /* 2) Download with a few delayed retries for transient socket/TLS
         *    pressure. This avoids permanently losing the cover on one low
         *    internal-heap moment. */
        http_sink_t sink = {0};
        esp_err_t download_err = ESP_FAIL;
        bool ok = false;
        bool cancelled_download = false;
        for (uint8_t attempt = 0; attempt < COVER_HTTP_MAX_ATTEMPTS; attempt++) {
            if (!cover_wait_until_http_ready(req.user)) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (cover_request_cancelled_locked(req.user)) {
                    s_cancelled_user = NULL;
                }
                xSemaphoreGive(s_lock);
                cancelled_download = true;
                break;
            }

            download_err = cover_download(req.url, &sink);
            ha_client_set_aux_http_pressure(false, 0);
            if (download_err == ESP_OK) {
                ok = true;
                break;
            }

            if (sink.buf != NULL) {
                heap_caps_free(sink.buf);
                sink = (http_sink_t){0};
            }
            if (!cover_download_error_is_retryable(download_err) ||
                attempt + 1 >= COVER_HTTP_MAX_ATTEMPTS) {
                break;
            }

            uint32_t retry_ms = COVER_HTTP_RETRY_DELAY_MS * (uint32_t)(attempt + 1U);
            ESP_LOGI(TAG,
                "Cover HTTP retry %u/%u in %u ms after %s",
                (unsigned)(attempt + 2U),
                (unsigned)COVER_HTTP_MAX_ATTEMPTS,
                (unsigned)retry_ms,
                esp_err_to_name(download_err));
            if (!cover_delay_or_cancel(req.user, retry_ms)) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (cover_request_cancelled_locked(req.user)) {
                    s_cancelled_user = NULL;
                }
                xSemaphoreGive(s_lock);
                cancelled_download = true;
                break;
            }
        }
        ha_client_set_aux_http_pressure(false, 0);
        if (cancelled_download) {
            if (sink.buf != NULL) {
                heap_caps_free(sink.buf);
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_inflight_valid = false;
            xSemaphoreGive(s_lock);
            continue;
        }

        /* 3) Decode + color */
        ha_cover_result_t result = {0};
        if (ok) {
            const char *format = cover_guess_format(sink.buf, sink.len);
            ESP_LOGD(TAG, "cover decode start: format=%s len=%u magic=%02x %02x %02x %02x",
                     format, (unsigned)sink.len,
                     sink.len > 0 ? sink.buf[0] : 0,
                     sink.len > 1 ? sink.buf[1] : 0,
                     sink.len > 2 ? sink.buf[2] : 0,
                     sink.len > 3 ? sink.buf[3] : 0);
            if (cover_is_png(sink.buf, sink.len)) {
                ok = cover_decode_png(sink.buf, sink.len, req.target_w, req.target_h, &result);
            } else if (sink.len >= 2 && sink.buf[0] == 0xFF && sink.buf[1] == 0xD8) {
                ok = cover_decode(sink.buf, sink.len, req.target_w, req.target_h, &result);
                if (!ok) {
                    size_t original_len = sink.len;
                    size_t sanitized_len = cover_jpeg_sanitize(sink.buf, original_len);
                    if (sanitized_len != original_len) {
                        ESP_LOGI(TAG, "retrying sanitized jpeg: len=%u -> %u",
                                 (unsigned)original_len, (unsigned)sanitized_len);
                        ok = cover_decode(sink.buf, sanitized_len, req.target_w, req.target_h, &result);
                        sink.len = sanitized_len;
                    }
                }
#if SOC_JPEG_DECODE_SUPPORTED
                if (!ok) {
                    ESP_LOGI(TAG, "retrying jpeg with software decoder");
                    ok = cover_decode_stb_jpeg(sink.buf, sink.len, req.target_w, req.target_h, &result);
                }
#endif
            } else {
                ESP_LOGW(TAG, "unsupported cover image format: %s len=%u", format, (unsigned)sink.len);
                ok = false;
            }
            if (ok && result.valid && result.image.data != NULL) {
                ESP_LOGD(TAG, "cover decode ok: cf=%u out=%ux%u stride=%u size=%u src=%ux%u dominant=%06x",
                         (unsigned)result.image.header.cf,
                         (unsigned)result.image.header.w,
                         (unsigned)result.image.header.h,
                         (unsigned)result.image.header.stride,
                         (unsigned)result.image.data_size,
                         (unsigned)result.source_w,
                         (unsigned)result.source_h,
                         (unsigned)(result.dominant_rgb & 0xFFFFFFU));
            } else {
                ESP_LOGW(TAG, "cover decode failed: format=%s len=%u", format, (unsigned)sink.len);
            }
        }
        if (sink.buf) heap_caps_free(sink.buf);

        /* 4) Re-check cancellation, then dispatch on LVGL task. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool cancelled_final = (s_cancelled_user != NULL && s_cancelled_user == req.user);
        if (cancelled_final) s_cancelled_user = NULL;
        s_inflight_valid = false;
        xSemaphoreGive(s_lock);

        if (cancelled_final) {
            if (result.valid && result.image.data) heap_caps_free((void *)result.image.data);
            continue;
        }

        if (req.cb != NULL) {
            cover_dispatch_t *disp = (cover_dispatch_t *)cover_heap_calloc(1, sizeof(cover_dispatch_t));
            if (disp == NULL) {
                if (result.valid && result.image.data) heap_caps_free((void *)result.image.data);
                continue;
            }
            disp->result = result;
            disp->cb = req.cb;
            disp->user = req.user;
            if (lv_async_call(cover_dispatch_on_lvgl, disp) != LV_RESULT_OK) {
                if (result.valid && result.image.data) heap_caps_free((void *)result.image.data);
                heap_caps_free(disp);
            }
        } else if (result.valid && result.image.data) {
            heap_caps_free((void *)result.image.data);
        }
    }
}

static void cover_dispatch_on_lvgl(void *data)
{
    cover_dispatch_t *disp = (cover_dispatch_t *)data;
    if (disp == NULL) return;
    /* Final cancel check on the LVGL side – the consumer may have been
     * destroyed between enqueue and dispatch. */
    bool cancelled = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_cancelled_user != NULL && s_cancelled_user == disp->user) {
        s_cancelled_user = NULL;
        cancelled = true;
    }
    xSemaphoreGive(s_lock);

    if (!cancelled) {
        disp->cb(disp->user, &disp->result);
    } else if (disp->result.valid && disp->result.image.data != NULL) {
        heap_caps_free((void *)disp->result.image.data);
    }
    /* If the callback did not keep the buffer, it must have called
     * ha_cover_result_release() which frees it. We cannot free here without
     * double-free risk; the contract is: callback owns the buffer. */
    heap_caps_free(disp);
}

/* ---- Public API ---- */

esp_err_t ha_cover_fetcher_init(void)
{
    if (s_task != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(COVER_Q_DEPTH, sizeof(cover_req_t));
    if (s_lock == NULL || s_queue == NULL) return ESP_ERR_NO_MEM;

#if SOC_JPEG_DECODE_SUPPORTED
    jpeg_decode_engine_cfg_t eng = {
        .intr_priority = 0,
        .timeout_ms = 2000,
    };
    if (jpeg_new_decoder_engine(&eng, &s_decoder) != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_new_decoder_engine failed");
        return ESP_FAIL;
    }
#endif

    /* Low priority (below HA WS RX task). Run on core 1 to keep core 0
     * for the HA WS / LVGL path. */
    BaseType_t ok;
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480) && CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    ok = xTaskCreatePinnedToCoreWithCaps(
        cover_task, "ha_cover", APP_HA_COVER_TASK_STACK, NULL, APP_HA_COVER_TASK_PRIO, &s_task, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    ok = xTaskCreatePinnedToCore(
        cover_task, "ha_cover", APP_HA_COVER_TASK_STACK, NULL, APP_HA_COVER_TASK_PRIO, &s_task, 1);
#endif
    if (ok != pdPASS) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t ha_cover_fetcher_request(const char *entity_picture, int target_w, int target_h,
                                    ha_cover_cb_t cb, void *user)
{
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (entity_picture == NULL || entity_picture[0] == '\0' || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (target_w < COVER_MIN_W) target_w = COVER_MIN_W;
    if (target_h < COVER_MIN_H) target_h = COVER_MIN_H;

    cover_req_t req = {0};
    snprintf(req.url, sizeof(req.url), "%s", entity_picture);
    req.target_w = target_w;
    req.target_h = target_h;
    req.cb = cb;
    req.user = user;

    /* Clear any stale cancel token for this consumer. After widget destroy the
     * heap may recycle the same ctx pointer for a freshly created widget; we
     * must not let the old cancel mark suppress legitimate new requests. */
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_cancelled_user == user) s_cancelled_user = NULL;
        xSemaphoreGive(s_lock);
    }

    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        /* Drop-oldest policy: the newest entity_picture supersedes any stale
         * queued request for the same consumer anyway. */
        cover_req_t drop;
        (void)xQueueReceive(s_queue, &drop, 0);
        if (xQueueSend(s_queue, &req, 0) != pdTRUE) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ha_cover_fetcher_cancel(void *user)
{
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cancelled_user = user;
    xSemaphoreGive(s_lock);
}

void ha_cover_result_release(ha_cover_result_t *result)
{
    if (result == NULL || !result->valid) return;
    if (result->image.data) {
        heap_caps_free((void *)result->image.data);
        result->image.data = NULL;
    }
    result->valid = false;
}
