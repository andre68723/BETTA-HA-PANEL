/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Global, single-worker cover-art fetcher.
 *
 * Consumers submit a target size + HA `entity_picture` proxy path and receive
 * an LVGL-ready image buffer (RGB565 for JPEG, RGB565A8 for PNG) plus a
 * derived dominant color via a callback on the LVGL thread. HTTP/TLS traffic
 * always yields to the HA WS client and the heavy-TLS gate
 * (see ha_client_heavy_gate_is_busy()).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_image_dsc_t image;     /* LVGL-ready pixel buffer owned by the callback */
    uint32_t dominant_rgb;    /* 0xRRGGBB of the most prominent cover color (0 if unavailable) */
    uint32_t source_w;        /* Original source image width before any downscale */
    uint32_t source_h;        /* Original source image height before any downscale */
    bool valid;               /* true if fetch+decode/prepare succeeded      */
} ha_cover_result_t;

/* Callback is invoked on the LVGL task via lv_async_call.  Ownership of
 * `result->image.data` is transferred to the callback: the callback MUST
 * either keep it (and later free via ha_cover_result_release) or free it
 * immediately via ha_cover_result_release. `result` itself is stack-owned
 * by the dispatcher and only valid during the callback. */
typedef void (*ha_cover_cb_t)(void *user, const ha_cover_result_t *result);

/* Public API */
esp_err_t ha_cover_fetcher_init(void);

/* Queue a cover-art request.  `entity_picture` is the raw proxy URL as emitted
 * by HA (e.g. "/api/media_player_proxy/media_player.kuche?token=..."); it may
 * also be an absolute URL.  `target_w`/`target_h` are widget pixel hints used
 * for scale selection.  `user` is passed through to the callback verbatim; it
 * MUST outlive the request or be cancelled via
 * ha_cover_fetcher_cancel(user). Returns ESP_OK when enqueued. */
esp_err_t ha_cover_fetcher_request(const char *entity_picture,
                                    int target_w,
                                    int target_h,
                                    ha_cover_cb_t cb,
                                    void *user);

/* Drop any queued or in-progress requests that match the given user pointer.
 * The callback for those requests will not be invoked.  Safe to call from any
 * task (including the LVGL task during widget destruction). */
void ha_cover_fetcher_cancel(void *user);
/* On HA disconnect, discard camera URLs queued before the next state refresh. */
void ha_cover_fetcher_cancel_cameras(void);

/* Free an image buffer obtained through a ha_cover_cb_t callback. */
void ha_cover_result_release(ha_cover_result_t *result);

/* Worker-only baseline JPEG decode, serialized with the still-image worker.
 * Input is borrowed for this call. On success the caller owns out->image.data.
 * Never call while holding the display lock. */
bool ha_cover_fetcher_decode_jpeg(const uint8_t *jpeg, size_t length, int width, int height,
                                 ha_cover_result_t *out);

#ifdef __cplusplus
}
#endif
