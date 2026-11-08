/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/api_routes.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "zlib.h"
#include "soc/soc_caps.h"
#if SOC_JPEG_ENCODE_SUPPORTED
#include "driver/jpeg_encode.h"
#endif

#include "drivers/display_init.h"
#include "lvgl.h"
#include "util/log_tags.h"

#define SCREENSHOT_LOCK_TIMEOUT_MS 1500U
#define SCREENSHOT_JPEG_QUALITY 90U
#define SCREENSHOT_JPEG_TIMEOUT_MS 4000
/* Upper bound of the compressed image; a larger result fails the request. */
#define SCREENSHOT_JPEG_MAX_BYTES (1024U * 1024U)
#define SCREENSHOT_PNG_MAX_BYTES (1024U * 1024U)
#define SCREENSHOT_PNG_LEVEL 1

static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_text_error(httpd_req_t *req, const char *status, const char *message)
{
    set_common_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, message);
}

#if LV_USE_SNAPSHOT
/* Validate both captures completely, then blend the straight-alpha ARGB8888
 * top layer (bytes B, G, R, A) onto the RGB888 page snapshot. Nothing is
 * modified unless every format, dimension, stride and buffer size fits. */
static bool blend_top_layer(lv_draw_buf_t *page, const lv_draw_buf_t *top)
{
    if (page == NULL || top == NULL || page->data == NULL || top->data == NULL) {
        return false;
    }
    const uint32_t w = page->header.w;
    const uint32_t h = page->header.h;
    if (w == 0U || h == 0U || top->header.w != w || top->header.h != h ||
        page->header.cf != LV_COLOR_FORMAT_RGB888 || top->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        return false;
    }
    if (page->header.stride < (uint64_t)w * 3U || top->header.stride < (uint64_t)w * 4U ||
        page->data_size < (uint64_t)page->header.stride * h || top->data_size < (uint64_t)top->header.stride * h) {
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *dst = (uint8_t *)page->data + (size_t)y * page->header.stride;
        const uint8_t *src = (const uint8_t *)top->data + (size_t)y * top->header.stride;
        for (uint32_t x = 0; x < w; x++, dst += 3, src += 4) {
            const uint32_t a = src[3];
            if (a == 0U) {
                continue;
            }
            if (a == 255U) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            } else {
                dst[0] = (uint8_t)((src[0] * a + dst[0] * (255U - a)) / 255U);
                dst[1] = (uint8_t)((src[1] * a + dst[1] * (255U - a)) / 255U);
                dst[2] = (uint8_t)((src[2] * a + dst[2] * (255U - a)) / 255U);
            }
        }
    }
    return true;
}
#endif

#if LV_USE_SNAPSHOT && SOC_JPEG_ENCODE_SUPPORTED
/* Compress the RGB888 snapshot with the P4's hardware JPEG engine (4:4:4 keeps coloured text sharp). The engine exists only for
 * the duration of the request. Returns an aligned PSRAM buffer the caller frees with heap_caps_free(), or NULL on any failure. */
static uint8_t *encode_jpeg(const lv_draw_buf_t *page, uint32_t *out_len)
{
    const uint32_t w = page->header.w;
    const uint32_t h = page->header.h;
    if (page->header.stride != w * 3U || page->data_size < (uint64_t)w * h * 3U) {
        return NULL; /* the engine reads a packed w*h image */
    }
    jpeg_encoder_handle_t engine = NULL;
    const jpeg_encode_engine_cfg_t engine_cfg = {.intr_priority = 0, .timeout_ms = SCREENSHOT_JPEG_TIMEOUT_MS};
    if (jpeg_new_encoder_engine(&engine_cfg, &engine) != ESP_OK) {
        return NULL;
    }
    const jpeg_encode_memory_alloc_cfg_t mem_cfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    uint8_t *out = jpeg_alloc_encoder_mem(SCREENSHOT_JPEG_MAX_BYTES, &mem_cfg, &out_cap);
    uint32_t size = 0;
    if (out != NULL) {
        const jpeg_encode_cfg_t cfg = {
            .height = h,
            .width = w,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV444,
            .image_quality = SCREENSHOT_JPEG_QUALITY,
            .pixel_reverse = false,
        };
        if (jpeg_encoder_process(engine, &cfg, page->data, (uint32_t)((uint64_t)w * h * 3U), out, (uint32_t)out_cap, &size) != ESP_OK ||
            size < 4U || size > out_cap) {
            heap_caps_free(out);
            out = NULL;
        }
    }
    jpeg_del_encoder_engine(engine);
    *out_len = size;
    return out;
}
#endif

#if LV_USE_SNAPSHOT
static void *png_zalloc(void *opaque, unsigned items, unsigned size)
{
    (void)opaque;
    return heap_caps_malloc((size_t)items * size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void png_zfree(void *opaque, void *ptr)
{
    (void)opaque;
    heap_caps_free(ptr);
}

static void png_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

/* Lossless PNG of the RGB888 snapshot: filter 0 and zlib at a fast level (UI screens are flat areas and compress well).
 * Returns a PSRAM buffer the caller frees with heap_caps_free(), or NULL on any failure or when it exceeds the cap. */
static uint8_t *encode_png(const lv_draw_buf_t *page, uint32_t *out_len)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    enum { HEAD = 8 + 25 + 8, TAIL = 4 + 12 };
    const uint32_t w = page->header.w;
    const uint32_t h = page->header.h;
    uint8_t *out = heap_caps_malloc(SCREENSHOT_PNG_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *row = heap_caps_malloc(1U + (size_t)w * 3U, MALLOC_CAP_8BIT);
    if (out == NULL || row == NULL) {
        heap_caps_free(out);
        heap_caps_free(row);
        return NULL;
    }
    z_stream z;
    memset(&z, 0, sizeof(z));
    z.zalloc = png_zalloc;
    z.zfree = png_zfree;
    if (deflateInit2(&z, SCREENSHOT_PNG_LEVEL, Z_DEFLATED, 12, 5, Z_DEFAULT_STRATEGY) != Z_OK) {
        heap_caps_free(out);
        heap_caps_free(row);
        return NULL;
    }
    z.next_out = out + HEAD;
    z.avail_out = SCREENSHOT_PNG_MAX_BYTES - HEAD - TAIL;
    /* LVGL's RGB888 is stored as B, G, R; PNG wants R, G, B. Each row is swapped into a small buffer behind its filter byte. */
    bool ok = true;
    for (uint32_t y = 0; y < h && ok; y++) {
        const uint8_t *src = (const uint8_t *)page->data + (size_t)y * page->header.stride;
        row[0] = 0;
        for (uint32_t x = 0; x < w; x++) {
            row[1 + x * 3U] = src[x * 3U + 2U];
            row[2 + x * 3U] = src[x * 3U + 1U];
            row[3 + x * 3U] = src[x * 3U];
        }
        z.next_in = row;
        z.avail_in = 1U + w * 3U;
        ok = deflate(&z, Z_NO_FLUSH) == Z_OK && z.avail_in == 0;
    }
    heap_caps_free(row);
    ok = ok && deflate(&z, Z_FINISH) == Z_STREAM_END;
    const uint32_t idat_len = (uint32_t)z.total_out;
    deflateEnd(&z);
    if (!ok) {
        heap_caps_free(out);
        return NULL;
    }
    memcpy(out, signature, 8);
    png_put_u32(out + 8, 13);
    memcpy(out + 12, "IHDR", 4);
    png_put_u32(out + 16, w);
    png_put_u32(out + 20, h);
    out[24] = 8; /* bit depth */
    out[25] = 2; /* RGB */
    out[26] = out[27] = out[28] = 0;
    png_put_u32(out + 29, (uint32_t)crc32(0, out + 12, 17));
    png_put_u32(out + 33, idat_len);
    memcpy(out + 37, "IDAT", 4);
    png_put_u32(out + HEAD + idat_len, (uint32_t)crc32(0, out + 37, idat_len + 4U));
    uint8_t *end = out + HEAD + idat_len + 4;
    png_put_u32(end, 0);
    memcpy(end + 4, "IEND", 4);
    png_put_u32(end + 8, (uint32_t)crc32(0, end + 4, 4));
    *out_len = HEAD + idat_len + TAIL;
    return out;
}
#endif

static esp_err_t screenshot_handle(httpd_req_t *req, bool png)
{
#if !LV_USE_SNAPSHOT
    return send_text_error(req, "501 Not Implemented", "LVGL snapshot support is disabled");
#else
    lv_draw_buf_t *snapshot = NULL;
    lv_draw_buf_t *top = NULL;
    bool has_overlay = false;
    const int64_t t0 = esp_timer_get_time();
    if (!display_lock(SCREENSHOT_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG_HTTP, "Screenshot request failed: could not lock display");
        return send_text_error(req, "503 Service Unavailable", "Display is busy");
    }

    snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (snapshot != NULL) {
        /* Popups, notifications and the update overlay live on the top layer, which a snapshot of the active screen does
         * not include. Without any child there is nothing to add, so the 4 MB ARGB render is skipped. */
        has_overlay = lv_obj_get_child_count(lv_layer_top()) > 0U;
        if (has_overlay) {
            top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
        }
    }
    display_unlock();
    const int64_t t_snap = esp_timer_get_time();

    /* A missing or inconsistent overlay fails the request rather than returning a page that silently lacks a popup. */
    bool overlay_ok = true;
    if (snapshot != NULL && has_overlay) {
        overlay_ok = blend_top_layer(snapshot, top);
    }
    if (top != NULL) {
        lv_draw_buf_destroy(top);
    }
    const int64_t t_blend = esp_timer_get_time();

    if (snapshot == NULL) {
        ESP_LOGW(TAG_HTTP, "Screenshot request failed: lv_snapshot_take returned NULL");
        return send_text_error(req, "500 Internal Server Error", "Failed to capture screenshot");
    }
    if (!overlay_ok) {
        ESP_LOGW(TAG_HTTP, "Screenshot request failed: top layer capture missing or inconsistent");
        lv_draw_buf_destroy(snapshot);
        return send_text_error(req, "500 Internal Server Error", "Failed to capture overlay");
    }

    if (snapshot->header.w == 0 || snapshot->header.h == 0 || snapshot->data == NULL ||
        snapshot->header.stride < snapshot->header.w * 3U) {
        lv_draw_buf_destroy(snapshot);
        return send_text_error(req, "500 Internal Server Error", "Invalid screenshot buffer");
    }

    uint32_t len = 0;
    uint8_t *image = NULL;
    if (png) {
        image = encode_png(snapshot, &len);
    }
#if SOC_JPEG_ENCODE_SUPPORTED
    else {
        image = encode_jpeg(snapshot, &len);
    }
#endif
    const int64_t t_enc = esp_timer_get_time();
    lv_draw_buf_destroy(snapshot);
    if (image == NULL) {
        ESP_LOGW(TAG_HTTP, "Screenshot %s encoding failed", png ? "PNG" : "JPEG");
        return send_text_error(req, "500 Internal Server Error", "Failed to encode screenshot");
    }
    set_common_headers(req);
    httpd_resp_set_type(req, png ? "image/png" : "image/jpeg");
    const esp_err_t err = httpd_resp_send(req, (const char *)image, (ssize_t)len);
    heap_caps_free(image);
    ESP_LOGI(TAG_HTTP, "Screenshot %s %u B: snapshot %d ms, overlay %d ms, encode %d ms, send %d ms", png ? "PNG" : "JPEG", (unsigned)len,
             (int)((t_snap - t0) / 1000), (int)((t_blend - t_snap) / 1000), (int)((t_enc - t_blend) / 1000),
             (int)((esp_timer_get_time() - t_enc) / 1000));
    return err;
#endif
}

esp_err_t api_screenshot_jpg_get_handler(httpd_req_t *req)
{
    return screenshot_handle(req, false);
}

esp_err_t api_screenshot_png_get_handler(httpd_req_t *req)
{
    return screenshot_handle(req, true);
}
