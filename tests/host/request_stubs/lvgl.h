/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "../week_stubs/lvgl.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct lv_timer_t lv_timer_t;
typedef struct { bool visible; } lv_obj_t;
bool lv_obj_is_visible(const lv_obj_t *obj);
typedef void (*lv_timer_cb_t)(lv_timer_t *timer);
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user);
void lv_timer_delete(lv_timer_t *timer);
void lv_timer_set_repeat_count(lv_timer_t *timer, int count);
