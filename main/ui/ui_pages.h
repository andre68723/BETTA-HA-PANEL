/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "lvgl.h"

typedef void (*ui_pages_show_cb_t)(const char *page_id, uint16_t index);

typedef struct {
    char symbol[12];
    char value[24];
    bool value_is_euro;
} ui_pages_stock_item_t;

void ui_pages_init(void);
void ui_pages_reset(void);
lv_obj_t *ui_pages_add(const char *page_id, const char *title);
bool ui_pages_show(const char *page_id);
bool ui_pages_show_index(uint16_t index);
bool ui_pages_next(void);
const char *ui_pages_current_id(void);
/* Register a single callback that is invoked whenever the active page
 * changes (after the new page has been made visible).  Passing NULL
 * clears the callback.  The callback runs on the LVGL UI task. */
void ui_pages_set_show_callback(ui_pages_show_cb_t cb);
uint16_t ui_pages_count(void);
void ui_pages_set_topbar_status(
    bool wifi_connected, bool wifi_setup_ap_active, bool api_connected, bool api_initial_sync_done);
void ui_pages_set_topbar_datetime(const struct tm *timeinfo);
void ui_pages_set_topbar_weather(bool visible, float temperature, const char *unit, uint32_t icon_codepoint);
void ui_pages_set_topbar_stocks(const ui_pages_stock_item_t *items, size_t count);
