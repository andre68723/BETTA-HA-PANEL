/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include "ha/ha_cover_fetcher.h"

typedef struct ha_camera_stream ha_camera_stream_t;
typedef enum {
    HA_CAMERA_CONNECTING, HA_CAMERA_LIVE, HA_CAMERA_WAITING,
    HA_CAMERA_UNSUPPORTED, HA_CAMERA_FAILED
} ha_camera_stream_status_t;

/* HA-origin MJPEG only, at most two streams. No arbitrary URL or printer credentials.
 * Handles own one worker reference and one consumer reference. No worker touches
 * LVGL or stores consumer pointers. Stop is non-blocking, including during HTTP.
 * All three functions may be called with the display lock held. */
ha_camera_stream_t *ha_camera_stream_start(const char *entity_id, int width, int height);
bool ha_camera_stream_take(ha_camera_stream_t *stream, ha_cover_result_t *out,
                           ha_camera_stream_status_t *status);
void ha_camera_stream_stop(ha_camera_stream_t *stream);
