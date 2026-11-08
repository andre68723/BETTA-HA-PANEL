/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_screenshot.c: just enough of ESP-IDF, HTTP server and LVGL for main/api/api_screenshot.c.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LV_USE_SNAPSHOT 1
typedef enum { LV_COLOR_FORMAT_RGB888 = 1, LV_COLOR_FORMAT_ARGB8888 = 2 } lv_color_format_t;
typedef struct { uint32_t w, h, stride; lv_color_format_t cf; } lv_image_header_t;
typedef struct { lv_image_header_t header; uint32_t data_size; uint8_t *data; } lv_draw_buf_t;
typedef struct lv_obj lv_obj_t;
lv_obj_t *lv_screen_active(void);
lv_obj_t *lv_layer_top(void);
lv_draw_buf_t *lv_snapshot_take(lv_obj_t *obj, lv_color_format_t cf);
void lv_draw_buf_destroy(lv_draw_buf_t *buf);
uint32_t lv_obj_get_child_count(const lv_obj_t *obj);
