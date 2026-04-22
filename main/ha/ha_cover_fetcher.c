/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ha/ha_cover_fetcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/jpeg_decode.h"

#include "ha/ha_client.h"

#define TAG "ha_cover"

#define COVER_Q_DEPTH 4
#define COVER_MAX_DOWNLOAD 1024 * 1024  /* 1 MiB upper bound per cover JPG */
#define COVER_GATE_POLL_MS 120
#define COVER_MIN_W 16
#define COVER_MIN_H 16

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
static jpeg_decoder_handle_t s_decoder = NULL;
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

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) return ESP_OK;
    http_sink_t *sink = (http_sink_t *)evt->user_data;
    if (sink == NULL || sink->truncated) return ESP_OK;

    size_t needed = sink->len + evt->data_len;
    if (needed > COVER_MAX_DOWNLOAD) {
        sink->truncated = true;
        return ESP_OK;
    }
    if (needed > sink->cap) {
        size_t new_cap = sink->cap ? sink->cap * 2 : 32 * 1024;
        while (new_cap < needed) new_cap *= 2;
        if (new_cap > COVER_MAX_DOWNLOAD) new_cap = COVER_MAX_DOWNLOAD;
        uint8_t *resized = heap_caps_realloc(sink->buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (resized == NULL) {
            sink->truncated = true;
            return ESP_OK;
        }
        sink->buf = resized;
        sink->cap = new_cap;
    }
    memcpy(sink->buf + sink->len, evt->data, evt->data_len);
    sink->len += evt->data_len;
    return ESP_OK;
}

static bool cover_download(const char *url, http_sink_t *sink)
{
    ha_client_http_ctx_t ctx = {0};
    if (!ha_client_get_http_context(&ctx)) {
        ESP_LOGW(TAG, "no HA http context yet");
        return false;
    }

    char full_url[640];
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        snprintf(full_url, sizeof(full_url), "%s", url);
    } else if (url[0] == '/') {
        snprintf(full_url, sizeof(full_url), "%s%s", ctx.base_url, url);
    } else {
        snprintf(full_url, sizeof(full_url), "%s/%s", ctx.base_url, url);
    }

    esp_http_client_config_t cfg = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .event_handler = http_event_cb,
        .user_data = sink,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    if (ctx.cert_common_name[0] != '\0') cfg.common_name = ctx.cert_common_name;

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) return false;

    if (ctx.host_header[0] != '\0') {
        esp_http_client_set_header(cli, "Host", ctx.host_header);
    }
    /* The HA media_player_proxy URLs already carry a ?token=... query; a Bearer
     * header is harmless but unnecessary and would bloat the request.  Only
     * attach it for URLs that don't include a token. */
    if (strstr(full_url, "token=") == NULL && ctx.bearer_token[0] != '\0') {
        char auth[600];
        snprintf(auth, sizeof(auth), "Bearer %s", ctx.bearer_token);
        esp_http_client_set_header(cli, "Authorization", auth);
    }

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http perform failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "http status=%d for cover fetch", status);
        return false;
    }
    if (sink->truncated) {
        ESP_LOGW(TAG, "cover exceeds %d bytes, truncated", COVER_MAX_DOWNLOAD);
        return false;
    }
    if (sink->len < 64) return false;
    return true;
}

/* ---- Decode + dominant color ---- */

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

static bool cover_decode(const uint8_t *jpg, size_t len, int target_w, int target_h,
                          ha_cover_result_t *out)
{
    jpeg_decode_picture_info_t pic = {0};
    if (jpeg_decoder_get_info(jpg, len, &pic) != ESP_OK) {
        ESP_LOGW(TAG, "jpeg header parse failed");
        return false;
    }
    if (pic.width == 0 || pic.height == 0) return false;

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

    out->image.header.cf = LV_COLOR_FORMAT_RGB565;
    out->image.header.w = (uint32_t)out_w;
    out->image.header.h = (uint32_t)out_h;
    out->image.header.stride = (uint32_t)(out_w * 2);
    out->image.data_size = produced;
    out->image.data = rgb;

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

        /* 1) Wait for the heavy WS gate and WS liveness. Cover fetch is always
         *    subordinate to the HA WS traffic. */
        while (!ha_client_is_connected() || ha_client_heavy_gate_is_busy()) {
            vTaskDelay(pdMS_TO_TICKS(COVER_GATE_POLL_MS));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool cancel_now = (s_cancelled_user != NULL && s_cancelled_user == req.user);
            xSemaphoreGive(s_lock);
            if (cancel_now) break;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool cancelled_now = (s_cancelled_user != NULL && s_cancelled_user == req.user);
        if (cancelled_now) s_cancelled_user = NULL;
        xSemaphoreGive(s_lock);
        if (cancelled_now) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_inflight_valid = false;
            xSemaphoreGive(s_lock);
            continue;
        }

        /* 2) Download */
        http_sink_t sink = {0};
        bool ok = cover_download(req.url, &sink);

        /* 3) Decode + color */
        ha_cover_result_t result = {0};
        if (ok) {
            sink.len = cover_jpeg_sanitize(sink.buf, sink.len);
            ok = cover_decode(sink.buf, sink.len, req.target_w, req.target_h, &result);
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
            cover_dispatch_t *disp = calloc(1, sizeof(cover_dispatch_t));
            if (disp == NULL) {
                if (result.valid && result.image.data) heap_caps_free((void *)result.image.data);
                continue;
            }
            disp->result = result;
            disp->cb = req.cb;
            disp->user = req.user;
            if (lv_async_call(cover_dispatch_on_lvgl, disp) != LV_RESULT_OK) {
                if (result.valid && result.image.data) heap_caps_free((void *)result.image.data);
                free(disp);
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
    }
    /* If the callback did not keep the buffer, it must have called
     * ha_cover_result_release() which frees it. We cannot free here without
     * double-free risk; the contract is: callback owns the buffer. */
    free(disp);
}

/* ---- Public API ---- */

esp_err_t ha_cover_fetcher_init(void)
{
    if (s_task != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(COVER_Q_DEPTH, sizeof(cover_req_t));
    if (s_lock == NULL || s_queue == NULL) return ESP_ERR_NO_MEM;

    jpeg_decode_engine_cfg_t eng = {
        .intr_priority = 0,
        .timeout_ms = 2000,
    };
    if (jpeg_new_decoder_engine(&eng, &s_decoder) != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_new_decoder_engine failed");
        return ESP_FAIL;
    }

    /* Low priority (below HA WS RX task). Run on core 1 to keep core 0
     * for the HA WS / LVGL path. */
    BaseType_t ok = xTaskCreatePinnedToCore(cover_task, "ha_cover", 6 * 1024, NULL, 3, &s_task, 1);
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
