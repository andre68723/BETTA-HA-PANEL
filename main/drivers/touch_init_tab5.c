/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * M5Stack Tab5 (run on one unit with an ST7121 panel, 2026-10-05; touch itself not yet confirmed by hand): the touch controller (GT911 on units with the
 * ILI9881C display, the integrated ST7123/ST7121 on newer units) is picked and wired by Espressif's
 * m5stack_tab5 BSP; it reports native portrait coordinates (720 x 1280) and LVGL maps them through the display rotation
 * set in display_init_tab5.c.
 */
#include "drivers/touch_init.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_tab5.h"
#include "bsp/touch.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "drivers/display_port.h"
#include "lvgl.h"

#include "drivers/display_init.h"
#include "util/log_tags.h"

static bool s_touch_ready = false;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_lcd_panel_io_handle_t s_touch_io_handle = NULL;
static const int TOUCH_INIT_RETRIES = 8;
static const int TOUCH_INIT_RETRY_DELAY_MS = 250;

static void touch_activity_event_cb(lv_event_t *event)
{
    (void)event;
    display_note_activity();
}

/* Diagnostic for a top bar that is hard to hit on the Tab5 (294 ppi): where a touch in the bar starts and ends and
 * whether LVGL took it for a scroll, which would cancel the click. The bar's chips log when the tap arrives. */
static void touch_top_bar_diag_cb(lv_event_t *event)
{
    if (s_touch_indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(s_touch_indev, &point);
    ESP_LOGI(TAG_TOUCH, "touch %s x=%d y=%d scroll=%d",
        lv_event_get_code(event) == LV_EVENT_PRESSED ? "down" : "up", (int)point.x, (int)point.y,
        lv_indev_get_scroll_obj(s_touch_indev) != NULL ? 1 : 0);
}

/* Measured on a Tab5 (2026-10-07): a finger aimed at the middle of a target is reported about 25 px below it (mean +27 px over
 * 15 taps spread over the whole screen, none in x). At 294 ppi that is 2.2 mm and put every tap on the 40 px high status chips of
 * the top bar below the bar. The reading is moved up by this much after esp_lvgl_adapter has mapped it to the screen, so it holds in
 * both orientations. */
#define TAB5_TOUCH_Y_BIAS_PX 24

static lv_indev_read_cb_t s_port_read_cb;

static void touch_read_with_bias(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_port_read_cb(indev, data);
    if (data->state == LV_INDEV_STATE_PRESSED) {
        data->point.y = data->point.y > TAB5_TOUCH_Y_BIAS_PX ? (lv_coord_t)(data->point.y - TAB5_TOUCH_Y_BIAS_PX) : 0;
    }
}

static lv_display_t *touch_get_display(void)
{
#if LV_VERSION_MAJOR >= 9
    return lv_display_get_default();
#else
    return lv_disp_get_default();
#endif
}

static void touch_release_handle(void)
{
    if (s_touch_handle != NULL) {
        esp_lcd_touch_del(s_touch_handle);
        s_touch_handle = NULL;
    }
    if (s_touch_io_handle != NULL) {
        esp_lcd_panel_io_del(s_touch_io_handle);
        s_touch_io_handle = NULL;
    }
}

esp_err_t touch_init(void)
{
    if (s_touch_ready) {
        return ESP_OK;
    }

    lv_display_t *disp = touch_get_display();
    if (disp == NULL) {
        ESP_LOGE(TAG_TOUCH, "Display must be initialized before touch");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= TOUCH_INIT_RETRIES; attempt++) {
        err = bsp_touch_new(NULL, &s_touch_handle);
        if (err == ESP_OK && s_touch_handle != NULL) {
            break;
        }
        ESP_LOGW(TAG_TOUCH, "Touch init attempt %d/%d failed: %s", attempt, TOUCH_INIT_RETRIES, esp_err_to_name(err));
        if (attempt < TOUCH_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(TOUCH_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG_TOUCH, "Touch init failed after %d attempts: %s", TOUCH_INIT_RETRIES, esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    const esp_lv_adapter_touch_config_t touch_cfg = {
        .disp = disp,
        .handle = s_touch_handle,
        .scale = {
            .x = 1.0f,
            .y = 1.0f,
        },
    };

    if (!display_lock(1000)) {
        touch_release_handle();
        return ESP_ERR_TIMEOUT;
    }
    s_touch_indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (s_touch_indev != NULL) {
        s_port_read_cb = lv_indev_get_read_cb(s_touch_indev);
        if (s_port_read_cb != NULL) {
            lv_indev_set_read_cb(s_touch_indev, touch_read_with_bias);
        }
        lv_indev_add_event_cb(s_touch_indev, touch_activity_event_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(s_touch_indev, touch_top_bar_diag_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(s_touch_indev, touch_top_bar_diag_cb, LV_EVENT_RELEASED, NULL);
    }
    display_unlock();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG_TOUCH, "esp_lv_adapter_register_touch failed");
        touch_release_handle();
        return ESP_FAIL;
    }

    s_touch_ready = true;
    ESP_LOGI(TAG_TOUCH, "Touch initialized (esp_lvgl_adapter + the Tab5 BSP touch controller)");
    return ESP_OK;
}

bool touch_is_ready(void)
{
    return s_touch_ready;
}
