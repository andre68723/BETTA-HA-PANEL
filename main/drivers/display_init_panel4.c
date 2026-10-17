/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/display_backlight.h"
#include "drivers/display_init.h"

#include <stdbool.h>

#include "bsp/display.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "drivers/display_port.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/log_tags.h"

static bool s_display_ready = false;
static lv_display_t *s_lv_display = NULL;


esp_err_t display_backlight_write(int percent)
{
    const esp_err_t err = bsp_display_brightness_set(percent);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not set backlight to %d%%: %s", percent, esp_err_to_name(err));
    }
    return err;
}

esp_err_t display_init(void)
{
    if (s_display_ready) {
        return ESP_OK;
    }

    esp_err_t err = display_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_DISPLAY, "display_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    bsp_lcd_handles_t lcd = {0};
    err = bsp_display_new_with_handles(NULL, &lcd);
    if (err != ESP_OK || lcd.panel == NULL) {
        ESP_LOGE(TAG_DISPLAY, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    /* The ST7703 constructor already installs IDF 6's DMA2D copy hook.
     * Installing it twice returns ESP_ERR_INVALID_STATE and aborts startup. */

    err = esp_lcd_panel_disp_on_off(lcd.panel, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not enable LCD panel output: %s", esp_err_to_name(err));
    }

    err = display_set_brightness_percent(APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not enable backlight: %s", esp_err_to_name(err));
    }

    err = display_brightness_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not create display dim timer: %s", esp_err_to_name(err));
    }

    s_lv_display = display_port_add_dsi(lcd.panel, lcd.io, APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT,
                                        false, BSP_LCD_BIGENDIAN);

    if (s_lv_display == NULL) {
        ESP_LOGE(TAG_DISPLAY, "display_port_add_dsi failed");
        return ESP_FAIL;
    }

    lv_display_set_antialiasing(s_lv_display, APP_LVGL_ANTIALIASING != 0);
    ESP_LOGI(TAG_DISPLAY, "LVGL antialiasing: %s", (APP_LVGL_ANTIALIASING != 0) ? "on" : "off");

    ESP_RETURN_ON_ERROR(display_port_start(), TAG_DISPLAY, "Adapter task start failed");
    s_display_ready = true;
    ESP_LOGI(TAG_DISPLAY, "Display initialized (shared esp_lvgl_adapter + DSI)");
    display_note_activity();
    return ESP_OK;
}

bool display_is_ready(void)
{
    return s_display_ready;
}

bool display_lock(uint32_t timeout_ms)
{
    return display_port_lock(timeout_ms);
}

void display_unlock(void)
{
    display_port_unlock();
}

