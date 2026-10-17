/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *low;
    lv_obj_t *precip;
} weather_now_view_t;

typedef struct {
    bool has_temp, has_low, night, hide_name;
    float temp, low, precip;
    int humidity, precip_prob;
    const char *unit, *condition, *condition_key, *precip_unit;
} weather_now_data_t;

void weather_now_hide(weather_now_view_t *view);
/* False leaves the caller's font/Lottie fallback in charge. Children belong to the card. */
bool weather_now_render(lv_obj_t *card, lv_obj_t *condition, lv_obj_t *temp, lv_obj_t *meta,
                        weather_now_view_t *view, const weather_now_data_t *data);
