/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MJPEG_HEADER_MAX 2048
#define MJPEG_BOUNDARY_MAX 72
#define MJPEG_FRAME_MAX (1024U * 1024U)

/* Bounded multipart reader. The caller owns the frame buffer and invokes this
 * only on its worker. Requires JPEG parts with Content-Length, as HA emits.
 * A completed frame is borrowed by the callback until it returns. */
typedef bool (*mjpeg_frame_cb_t)(void *user, const uint8_t *jpeg, size_t length);
typedef struct {
    char boundary[MJPEG_BOUNDARY_MAX + 3];
    char line[MJPEG_HEADER_MAX + 1];
    size_t line_length, header_bytes, length, received;
    uint8_t *frame;
    size_t capacity;
    unsigned state;
    bool jpeg, have_length, failed, ended;
} mjpeg_parser_t;

bool mjpeg_parser_init(mjpeg_parser_t *parser, const char *content_type, uint8_t *frame, size_t capacity);
bool mjpeg_parser_feed(mjpeg_parser_t *parser, const uint8_t *data, size_t length,
                       mjpeg_frame_cb_t completed, void *user);
bool mjpeg_camera_path(const char *entity_id, char *out, size_t capacity);
