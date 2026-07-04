/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "drivers/display_init.h"

#include <stdbool.h>

#include "bsp/display.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/log_tags.h"

#define DISPLAY_FULL_BUFFER_PIXELS ((APP_SCREEN_WIDTH * APP_SCREEN_HEIGHT))

static bool s_display_ready = false;
static lv_display_t *s_lv_display = NULL;
static esp_timer_handle_t s_dim_timer = NULL;
static int s_display_brightness = -1;
static int s_active_brightness_percent = APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT;
static int s_dim_brightness_percent = APP_DISPLAY_DIM_BRIGHTNESS_PERCENT;
static uint32_t s_dim_timeout_ms = APP_DISPLAY_DIM_TIMEOUT_MS;

static void display_restart_dim_timer(void);

static lvgl_port_cfg_t display_port_cfg(void)
{
    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_priority = 20;
    cfg.task_stack = APP_LVGL_TASK_STACK;
    cfg.task_affinity = 1;
    cfg.task_max_sleep_ms = 100;
    return cfg;
}

static int display_clamp_brightness(int percent)
{
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

static int display_clamp_config_brightness(int percent)
{
    if (percent < 10) {
        return 10;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

esp_err_t display_set_brightness_percent(int percent)
{
    const int next = display_clamp_brightness(percent);
    if (s_display_brightness == next) {
        return ESP_OK;
    }

    esp_err_t err = bsp_display_brightness_set(next);
    if (err == ESP_OK) {
        s_display_brightness = next;
    } else {
        ESP_LOGW(TAG_DISPLAY, "Could not set backlight to %d%%: %s", next, esp_err_to_name(err));
    }
    return err;
}

void display_configure_brightness(int active_brightness_percent, int dim_brightness_percent, int dim_timeout_seconds)
{
    s_active_brightness_percent = display_clamp_config_brightness(active_brightness_percent);
    s_dim_brightness_percent = display_clamp_config_brightness(dim_brightness_percent);
    if (dim_timeout_seconds < 5) {
        dim_timeout_seconds = 5;
    }
    if (dim_timeout_seconds > 3600) {
        dim_timeout_seconds = 3600;
    }
    s_dim_timeout_ms = (uint32_t)dim_timeout_seconds * 1000U;
    if (s_display_ready) {
        (void)display_set_brightness_percent(s_active_brightness_percent);
        display_restart_dim_timer();
    }
}

static void display_dim_timer_cb(void *arg)
{
    (void)arg;
    if (!s_display_ready) {
        return;
    }
    (void)display_set_brightness_percent(s_dim_brightness_percent);
}

static esp_err_t display_dim_timer_init(void)
{
    if (s_dim_timer != NULL) {
        return ESP_OK;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = display_dim_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_dim",
        .skip_unhandled_events = true,
    };
    return esp_timer_create(&timer_args, &s_dim_timer);
}

static void display_restart_dim_timer(void)
{
    if (s_dim_timer == NULL) {
        return;
    }
    if (esp_timer_is_active(s_dim_timer)) {
        (void)esp_timer_stop(s_dim_timer);
    }
    const uint64_t timeout_us = (uint64_t)s_dim_timeout_ms * 1000ULL;
    esp_err_t err = esp_timer_start_once(s_dim_timer, timeout_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not start display dim timer: %s", esp_err_to_name(err));
    }
}

void display_note_activity(void)
{
    if (!s_display_ready) {
        return;
    }
    (void)display_set_brightness_percent(s_active_brightness_percent);
    display_restart_dim_timer();
}

esp_err_t display_init(void)
{
    if (s_display_ready) {
        return ESP_OK;
    }

    lvgl_port_cfg_t lvgl_cfg = display_port_cfg();
    esp_err_t err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_DISPLAY, "lvgl_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    bsp_lcd_handles_t lcd = {0};
    err = bsp_display_new_with_handles(NULL, &lcd);
    if (err != ESP_OK || lcd.panel == NULL) {
        ESP_LOGE(TAG_DISPLAY, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    err = esp_lcd_panel_disp_on_off(lcd.panel, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not enable LCD panel output: %s", esp_err_to_name(err));
    }

    err = display_set_brightness_percent(APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not enable backlight: %s", esp_err_to_name(err));
    }

    err = display_dim_timer_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not create display dim timer: %s", esp_err_to_name(err));
    }

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd.io,
        .panel_handle = lcd.panel,
        .control_handle = lcd.control,
        .buffer_size = DISPLAY_FULL_BUFFER_PIXELS / 5U,
        .double_buffer = true,
        .hres = APP_SCREEN_WIDTH,
        .vres = APP_SCREEN_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
#if LV_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
#if LV_VERSION_MAJOR >= 9
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
            .full_refresh = false,
            .direct_mode = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,
        },
    };

    static const uint8_t draw_buf_divisors[] = {5U, 8U, 10U, 12U};
    uint8_t used_divisor = 0U;
    uint32_t used_buffer_pixels = 0U;
    for (size_t i = 0; i < (sizeof(draw_buf_divisors) / sizeof(draw_buf_divisors[0])); i++) {
        uint8_t divisor = draw_buf_divisors[i];
        if (divisor == 0U) {
            continue;
        }
        disp_cfg.buffer_size = DISPLAY_FULL_BUFFER_PIXELS / divisor;
        s_lv_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
        if (s_lv_display != NULL) {
            used_divisor = divisor;
            used_buffer_pixels = disp_cfg.buffer_size;
            break;
        }
        ESP_LOGW(TAG_DISPLAY, "lvgl_port_add_disp_dsi failed with draw_buf=1/%u (%u px), trying smaller buffer",
            (unsigned)divisor, (unsigned)disp_cfg.buffer_size);
    }

    if (s_lv_display == NULL) {
        ESP_LOGE(TAG_DISPLAY, "lvgl_port_add_disp_dsi failed");
        return ESP_FAIL;
    }

    lv_display_set_antialiasing(s_lv_display, APP_LVGL_ANTIALIASING != 0);
    ESP_LOGI(TAG_DISPLAY, "LVGL antialiasing: %s", (APP_LVGL_ANTIALIASING != 0) ? "on" : "off");

    s_display_ready = true;
    ESP_LOGI(TAG_DISPLAY,
        "Display initialized (esp_lvgl_port + DSI, avoid_tearing=0, direct_mode=0, double_buffer=1, draw_buf=1/%u, %u px)",
        (unsigned)used_divisor, (unsigned)used_buffer_pixels);
    display_note_activity();
    return ESP_OK;
}

bool display_is_ready(void)
{
    return s_display_ready;
}

bool display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void)
{
    lvgl_port_unlock();
}
