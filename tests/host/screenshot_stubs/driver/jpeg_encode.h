/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_screenshot.c.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct jpeg_encoder_t *jpeg_encoder_handle_t;
typedef struct { int intr_priority; int timeout_ms; } jpeg_encode_engine_cfg_t;
typedef enum { JPEG_ENC_ALLOC_INPUT_BUFFER = 0, JPEG_ENC_ALLOC_OUTPUT_BUFFER = 1 } jpeg_enc_buffer_alloc_direction_t;
typedef struct { jpeg_enc_buffer_alloc_direction_t buffer_direction; } jpeg_encode_memory_alloc_cfg_t;
typedef enum { JPEG_ENCODE_IN_FORMAT_RGB888 = 1 } jpeg_enc_input_format_t;
typedef enum { JPEG_DOWN_SAMPLING_YUV444 = 1 } jpeg_down_sampling_type_t;
typedef struct {
    uint32_t height, width;
    jpeg_enc_input_format_t src_type;
    jpeg_down_sampling_type_t sub_sample;
    uint32_t image_quality;
    bool pixel_reverse;
} jpeg_encode_cfg_t;
esp_err_t jpeg_new_encoder_engine(const jpeg_encode_engine_cfg_t *cfg, jpeg_encoder_handle_t *ret);
esp_err_t jpeg_encoder_process(jpeg_encoder_handle_t engine, const jpeg_encode_cfg_t *cfg, const uint8_t *in, uint32_t in_size,
                               uint8_t *out, uint32_t out_size, uint32_t *written);
esp_err_t jpeg_del_encoder_engine(jpeg_encoder_handle_t engine);
void *jpeg_alloc_encoder_mem(size_t size, const jpeg_encode_memory_alloc_cfg_t *cfg, size_t *allocated);
