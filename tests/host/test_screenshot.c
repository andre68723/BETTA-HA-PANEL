/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The production screenshot handler (main/api/api_screenshot.c) against stubbed LVGL snapshots, display lock and HTTP
 * responses: overlay blending, failed or inconsistent captures, bounded-lock failure, send failures, buffer cleanup,
 * and BMP row order and padding. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "api/api_screenshot.c" /* NOLINT: the production file, with the stubs of tests/host/screenshot_stubs */

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static struct lv_obj { int x; } screen_obj, top_obj;
static lv_draw_buf_t *page_buf, *top_buf;
static bool lock_ok, locked;
static int live_bufs, lock_calls, unlock_calls, take_calls;
static int fail_chunk_at; /* 1-based chunk index that fails, 0 = never */
static int chunks;
static char status[64], type[32];
static uint8_t body[1 << 18];
static size_t body_len;
static int sendstr_calls;

lv_obj_t *lv_screen_active(void) { return &screen_obj; }
lv_obj_t *lv_layer_top(void) { return &top_obj; }
static uint32_t top_children = 1;
uint32_t lv_obj_get_child_count(const lv_obj_t *obj) { (void)obj; return top_children; }
int64_t esp_timer_get_time(void) { static int64_t t; return t += 1000; }
bool display_lock(uint32_t timeout_ms) { (void)timeout_ms; lock_calls++; locked = lock_ok; return lock_ok; }
void display_unlock(void) { unlock_calls++; locked = false; }
lv_draw_buf_t *lv_snapshot_take(lv_obj_t *obj, lv_color_format_t cf)
{
    (void)cf;
    take_calls++;
    CHECK(locked);
    lv_draw_buf_t *src = obj == &screen_obj ? page_buf : top_buf;
    if (src == NULL) return NULL;
    live_bufs++;
    return src;
}
void lv_draw_buf_destroy(lv_draw_buf_t *buf)
{
    live_bufs--;
    if (buf->data != NULL) free(buf->data);
    free(buf);
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *f, const char *v) { (void)r; (void)f; (void)v; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) { (void)r; snprintf(status, sizeof(status), "%s", s); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) { (void)r; snprintf(type, sizeof(type), "%s", t); return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s) { (void)r; (void)s; sendstr_calls++; return ESP_OK; }
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len)
{
    (void)r;
    chunks++;
    if (fail_chunk_at != 0 && chunks == fail_chunk_at) return ESP_FAIL;
    if (buf != NULL && body_len + (size_t)len <= sizeof(body)) { memcpy(body + body_len, buf, (size_t)len); body_len += (size_t)len; }
    return ESP_OK;
}

/* Fake hardware JPEG engine: engines and output buffers are counted so leaks show; the "image" is a recognisable byte pattern. */
static int fail_heap_malloc, engines, jpeg_mem, fail_engine_new, fail_jpeg_process, jpeg_calls;
static uint32_t jpeg_size_to_write = 5000;
static uint8_t jpeg_in_first;
esp_err_t jpeg_new_encoder_engine(const jpeg_encode_engine_cfg_t *cfg, jpeg_encoder_handle_t *ret)
{
    (void)cfg;
    if (fail_engine_new) return ESP_FAIL;
    engines++;
    *ret = (jpeg_encoder_handle_t)&engines;
    return ESP_OK;
}
esp_err_t jpeg_del_encoder_engine(jpeg_encoder_handle_t e) { (void)e; engines--; return ESP_OK; }
void *jpeg_alloc_encoder_mem(size_t size, const jpeg_encode_memory_alloc_cfg_t *cfg, size_t *allocated)
{
    CHECK(cfg->buffer_direction == JPEG_ENC_ALLOC_OUTPUT_BUFFER);
    jpeg_mem++;
    *allocated = size;
    return calloc(1, size);
}
void heap_caps_free(void *p) { if (p != NULL) jpeg_mem--; free(p); }
void *heap_caps_malloc(size_t size, unsigned caps) { (void)caps; if (fail_heap_malloc) return NULL; jpeg_mem++; return malloc(size); }
esp_err_t jpeg_encoder_process(jpeg_encoder_handle_t e, const jpeg_encode_cfg_t *cfg, const uint8_t *in, uint32_t in_size,
                               uint8_t *out, uint32_t out_size, uint32_t *written)
{
    (void)e;
    jpeg_calls++;
    CHECK(cfg->src_type == JPEG_ENCODE_IN_FORMAT_RGB888 && in_size == cfg->width * cfg->height * 3U);
    jpeg_in_first = in[0];
    if (fail_jpeg_process) return ESP_FAIL;
    CHECK(jpeg_size_to_write <= out_size);
    out[0] = 0xFF; out[1] = 0xD8;
    for (uint32_t i = 2; i < jpeg_size_to_write; i++) out[i] = (uint8_t)i;
    *written = jpeg_size_to_write;
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len)
{
    (void)r;
    sendstr_calls += 100;
    if (fail_chunk_at == -1) return ESP_FAIL;
    if ((size_t)len <= sizeof(body)) { memcpy(body, buf, (size_t)len); body_len = (size_t)len; }
    return ESP_OK;
}

static lv_draw_buf_t *make_buf(uint32_t w, uint32_t h, lv_color_format_t cf, uint32_t bpp, uint32_t stride)
{
    lv_draw_buf_t *b = calloc(1, sizeof(*b));
    b->header.w = w; b->header.h = h; b->header.cf = cf; b->header.stride = stride;
    b->data_size = stride * h;
    b->data = calloc(1, b->data_size);
    (void)bpp;
    return b;
}

static void reset(void)
{
    page_buf = top_buf = NULL; top_children = 1;
    lock_ok = true; locked = false;
    live_bufs = lock_calls = unlock_calls = take_calls = 0;
    fail_chunk_at = chunks = 0;
    body_len = 0; status[0] = type[0] = '\0'; sendstr_calls = 0;
    fail_heap_malloc = 0;
    fail_engine_new = fail_jpeg_process = jpeg_calls = 0; jpeg_size_to_write = 5000;
}

/* 3x2 page, stride 12 (row padding in the source), top layer 4 bytes per pixel. */
static void fill_page(void)
{
    page_buf = make_buf(3, 2, LV_COLOR_FORMAT_RGB888, 3, 12);
    top_buf = make_buf(3, 2, LV_COLOR_FORMAT_ARGB8888, 4, 12);
    for (uint32_t i = 0; i < 6; i++) { page_buf->data[(i / 3) * 12 + (i % 3) * 3] = (uint8_t)(10 + i); }
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* Validates the PNG in body (signature, IHDR, IDAT and IEND with their CRCs) and returns the unfiltered RGB rows (w*3 each). */
static bool decode_png(uint32_t w, uint32_t h, uint8_t *rgb)
{
    bool ok = body_len > 57 && memcmp(body, "\x89PNG\r\n\x1a\n", 8) == 0 && be32(body + 8) == 13 && memcmp(body + 12, "IHDR", 4) == 0 &&
              be32(body + 16) == w && be32(body + 20) == h && body[24] == 8 && body[25] == 2 &&
              crc32(0, body + 12, 17) == be32(body + 29);
    if (!ok) return false;
    const uint32_t idat = be32(body + 33);
    ok = memcmp(body + 37, "IDAT", 4) == 0 && crc32(0, body + 37, idat + 4) == be32(body + 41 + idat) &&
         body_len == 41 + idat + 4 + 12 && memcmp(body + 45 + idat + 4, "IEND", 4) == 0;
    if (!ok) return false;
    const size_t row = 1U + (size_t)w * 3U;
    uint8_t *raw = malloc(row * h);
    uLongf raw_len = (uLongf)(row * h);
    ok = uncompress(raw, &raw_len, body + 41, idat) == Z_OK && raw_len == row * h;
    for (uint32_t y = 0; ok && y < h; y++) {
        ok = raw[y * row] == 0;
        memcpy(rgb + (size_t)y * w * 3U, raw + y * row + 1U, (size_t)w * 3U);
    }
    free(raw);
    return ok;
}

int main(void)
{
    httpd_req_t req = {0};

    /* Lock failure: 503, no capture, nothing allocated. */
    reset(); lock_ok = false;
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(strcmp(status, "503 Service Unavailable") == 0 && take_calls == 0 && live_bufs == 0 && unlock_calls == 0);

    /* No popups: the top layer is not rendered at all and the page is sent as captured. */
    reset(); fill_page(); top_children = 0;
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(take_calls == 1 && live_bufs == 0 && strcmp(type, "image/png") == 0 && jpeg_mem == 0);
    free(top_buf->data); free(top_buf); top_buf = NULL;

    /* Page capture fails. */
    reset();
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(strcmp(status, "500 Internal Server Error") == 0 && unlock_calls == 1 && live_bufs == 0 && body_len == 0);

    /* Top layer capture fails: no false success, page buffer freed. */
    reset(); page_buf = make_buf(3, 2, LV_COLOR_FORMAT_RGB888, 3, 12);
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(strcmp(status, "500 Internal Server Error") == 0 && body_len == 0 && live_bufs == 0 && unlock_calls == 1);

    /* Size mismatch, wrong format, short stride, short buffer: all 500 and freed. */
    for (int variant = 0; variant < 4; variant++) {
        reset(); fill_page();
        if (variant == 0) top_buf->header.w = 2;
        if (variant == 1) top_buf->header.cf = LV_COLOR_FORMAT_RGB888;
        if (variant == 2) top_buf->header.stride = 8;
        if (variant == 3) top_buf->data_size = 3;
        CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
        CHECK(strcmp(status, "500 Internal Server Error") == 0 && body_len == 0 && live_bufs == 0 && jpeg_mem == 0);
    }

    /* PNG with an overlay: opaque, half-transparent and transparent pixels; RGB order (the snapshot is stored B, G, R),
     * padded source stride, valid chunks and CRCs, nothing leaked. */
    reset(); fill_page();
    for (uint32_t i = 0; i < 6; i++) { page_buf->data[(i / 3) * 12 + (i % 3) * 3 + 1] = (uint8_t)(20 + i); page_buf->data[(i / 3) * 12 + (i % 3) * 3 + 2] = (uint8_t)(30 + i); }
    top_buf->data[0 * 12 + 0 * 4 + 0] = 200; top_buf->data[0 * 12 + 0 * 4 + 3] = 255;       /* (0,0) opaque blue channel */
    top_buf->data[0 * 12 + 1 * 4 + 0] = 100; top_buf->data[0 * 12 + 1 * 4 + 3] = 128;       /* (1,0) half over 11 */
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(strcmp(type, "image/png") == 0 && live_bufs == 0 && unlock_calls == 1 && status[0] == '\0' && jpeg_mem == 0 && chunks == 0);
    {
        uint8_t rgb[2 * 3 * 3];
        CHECK(decode_png(3, 2, rgb));
        CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 200);                                       /* (0,0): opaque overlay replaces the page */
        CHECK(rgb[3 + 2] == (uint8_t)((100 * 128 + 11 * 127) / 255) && rgb[3] == (uint8_t)(31 * 127 / 255) && rgb[4] == (uint8_t)(21 * 127 / 255));
        CHECK(rgb[6] == 32 && rgb[7] == 22 && rgb[8] == 12);                                      /* (2,0) transparent overlay */
        CHECK(rgb[9] == 33 && rgb[10] == 23 && rgb[11] == 13);                                    /* row 1 */
    }

    /* PNG failures: allocation, then every send failure; buffers released, no BMP fallback any more. */
    reset(); fill_page(); top_children = 0; fail_heap_malloc = 1;
    CHECK(api_screenshot_png_get_handler(&req) == ESP_OK);
    CHECK(strcmp(status, "500 Internal Server Error") == 0 && body_len == 0 && jpeg_mem == 0 && live_bufs == 0);
    free(top_buf->data); free(top_buf); top_buf = NULL;
    reset(); fill_page(); top_children = 0; fail_chunk_at = -1;
    CHECK(api_screenshot_png_get_handler(&req) == ESP_FAIL && live_bufs == 0 && jpeg_mem == 0);
    free(top_buf->data); free(top_buf); top_buf = NULL;

    /* JPEG route: packed 1024x40 snapshot goes through the engine; one httpd_resp_send, nothing leaked. */
    reset();
    page_buf = make_buf(1024, 40, LV_COLOR_FORMAT_RGB888, 3, 3072);
    top_buf = make_buf(1024, 40, LV_COLOR_FORMAT_ARGB8888, 4, 4096);
    page_buf->data[0] = 77;
    CHECK(api_screenshot_jpg_get_handler(&req) == ESP_OK);
    CHECK(strcmp(type, "image/jpeg") == 0 && body_len == 5000 && body[0] == 0xFF && body[1] == 0xD8 && jpeg_in_first == 77);
    CHECK(sendstr_calls == 100 && chunks == 0 && live_bufs == 0 && engines == 0 && jpeg_mem == 0);

    /* JPEG failures (engine creation, encoding): 500, engine and buffers released. */
    for (int mode = 0; mode < 2; mode++) {
        reset();
        page_buf = make_buf(1024, 40, LV_COLOR_FORMAT_RGB888, 3, 3072);
        top_buf = make_buf(1024, 40, LV_COLOR_FORMAT_ARGB8888, 4, 4096);
        if (mode == 0) fail_engine_new = 1;
        if (mode == 1) fail_jpeg_process = 1;
        CHECK(api_screenshot_jpg_get_handler(&req) == ESP_OK);
        CHECK(strcmp(status, "500 Internal Server Error") == 0 && body_len == 0 && live_bufs == 0 && engines == 0 && jpeg_mem == 0);
    }

    /* Padded source stride cannot be encoded as packed: 500 without touching the engine. */
    reset();
    page_buf = make_buf(3, 2, LV_COLOR_FORMAT_RGB888, 3, 16);
    top_buf = make_buf(3, 2, LV_COLOR_FORMAT_ARGB8888, 4, 16);
    CHECK(api_screenshot_jpg_get_handler(&req) == ESP_OK);
    CHECK(strcmp(status, "500 Internal Server Error") == 0 && jpeg_calls == 0 && engines == 0 && live_bufs == 0);

    /* JPEG send failure releases everything. */
    reset(); fail_chunk_at = -1;
    page_buf = make_buf(1024, 40, LV_COLOR_FORMAT_RGB888, 3, 3072);
    top_buf = make_buf(1024, 40, LV_COLOR_FORMAT_ARGB8888, 4, 4096);
    CHECK(api_screenshot_jpg_get_handler(&req) == ESP_FAIL && live_bufs == 0 && engines == 0 && jpeg_mem == 0);

    if (failures == 0) printf("screenshot handler tests passed\n");
    return failures == 0 ? 0 : 1;
}
