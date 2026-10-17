/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * M5Stack Tab5 (run on one unit with an ST7121 panel, ESP32-P4 v1.3, 2026-10-05: display and landscape rotation confirmed): the display comes up through Espressif's
 * m5stack_tab5 board support package, which picks the ILI9881C or the ST7123/ST7121 panel of the unit and
 * drives backlight PWM on GPIO22 and reset lines over the on-board I/O expanders. Native orientation is portrait
 * (720 x 1280); LVGL applies the detected or remembered orientation before the first boot screen.
 * The touch driver needs no matching change: LVGL
 * rotates the native-portrait touch points with the display rotation (lv_indev.c), so touch_init_tab5.c sets
 * no swap or mirror flags.
 */
#include "drivers/display_backlight.h"
#include "drivers/display_init.h"
#include "drivers/display_orientation.h"
#include "ui/display_geometry.h"

#include <stdbool.h>
#include <inttypes.h>

#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "hal/ledc_ll.h"
#include "soc/gpio_struct.h"
#include "soc/ledc_struct.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "drivers/display_port.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/log_tags.h"

#define DISPLAY_FULL_BUFFER_PIXELS ((APP_SCREEN_WIDTH * APP_SCREEN_HEIGHT))
/* BSP 1.3.2 owns the original 5 kHz, 10-bit PWM configuration and linear percent conversion. */
#define TAB5_BACKLIGHT_TIMER LEDC_TIMER_0

static bool s_display_ready = false;
static lv_display_t *s_lv_display = NULL;

static uint32_t backlight_max_duty(void)
{
    uint32_t maximum;
    ledc_ll_get_max_duty(&LEDC, LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_TIMER, &maximum);
    return maximum - 1U;
}


esp_err_t display_backlight_write(int percent)
{
    const esp_err_t err = bsp_display_brightness_set(percent);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not set backlight to %d%%: %s", percent, esp_err_to_name(err));
    } else {
        /* LEDC latches duty at the next PWM period; reading immediately can report the previous level. */
        esp_rom_delay_us(250); /* One period of the BSP's 5 kHz PWM, with a small margin. */
        ESP_LOGI(TAG_DISPLAY, "backlight set %d%% (LEDC duty %"PRIu32"/%"PRIu32" at %"PRIu32" Hz on GPIO%d)",
            percent, ledc_get_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH),
            backlight_max_duty(),
            ledc_get_freq(LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_TIMER), (int)BSP_LCD_BACKLIGHT);
    }
    return err;
}

bool display_backlight_diagnostics(display_backlight_diagnostics_t *out)
{
    if (!out || !s_display_ready) return false;
    gpio_io_config_t pad = {0};
    if (gpio_get_io_config(BSP_LCD_BACKLIGHT, &pad) != ESP_OK) return false;
    *out = (display_backlight_diagnostics_t){
        .gpio_pin = BSP_LCD_BACKLIGHT,
        .requested_percent = display_get_brightness_percent(),
        .frequency_hz = ledc_get_freq(LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_TIMER),
        .duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH),
        .max_duty = backlight_max_duty(),
        .gpio_function = pad.fun_sel, .gpio_signal = pad.sig_out,
        .gpio_input = pad.ie, .gpio_output = pad.oe,
        .gpio_peripheral_output = pad.oe_ctrl_by_periph, .gpio_open_drain = pad.od,
    };
    /* Target four PWM periods, bounded to 8–50 ms; the original BSP profile uses 8 ms. */
    uint32_t window_us = out->frequency_hz ? 4000000U / out->frequency_hz : 8000U;
    if (window_us < 8000U) window_us = 8000U;
    if (window_us > 50000U) window_us = 50000U;
    /* Input sensing does not alter the PWM output path. Restore its prior state after sampling. */
    if (!pad.ie && gpio_input_enable(BSP_LCD_BACKLIGHT) != ESP_OK) return false;
    const int64_t start_us = esp_timer_get_time();
    do {
        out->high_samples += gpio_get_level(BSP_LCD_BACKLIGHT) != 0;
        out->samples++;
        esp_rom_delay_us(7);
    } while (esp_timer_get_time() - start_us < window_us);
    out->elapsed_us = (uint32_t)(esp_timer_get_time() - start_us);
    /* No public input-disable API; restore only IE, preserving peripheral OE and matrix routing. */
    if (!pad.ie) gpio_ll_input_disable(&GPIO, BSP_LCD_BACKLIGHT);
    out->stable = out->duty == ledc_get_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH) &&
        out->max_duty == backlight_max_duty() &&
        out->requested_percent == display_get_brightness_percent() &&
        out->frequency_hz == ledc_get_freq(LEDC_LOW_SPEED_MODE, TAB5_BACKLIGHT_TIMER);
    return true;
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

    /* The C6 Wi-Fi module sits behind an expander pin (BSP_WIFI_EN); the display comes up first in app_main, so
     * switch it on here, before ESP-Hosted starts. */
    err = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not enable the Wi-Fi module: %s", esp_err_to_name(err));
    }

    bsp_lcd_handles_t lcd = {0};
    err = bsp_display_new_with_handles(NULL, &lcd);
    if (err != ESP_OK || lcd.panel == NULL) {
        ESP_LOGE(TAG_DISPLAY, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    const int boot_rotation = display_orientation_prepare_boot();

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

    s_lv_display = display_port_add_dsi(lcd.panel, lcd.io, BSP_LCD_H_RES, BSP_LCD_V_RES,
                                        true, BSP_LCD_BIGENDIAN);

    if (s_lv_display == NULL) {
        ESP_LOGE(TAG_DISPLAY, "display_port_add_dsi failed");
        return ESP_FAIL;
    }

    if (display_lock(1000)) {
        lv_display_set_rotation(s_lv_display, (lv_display_rotation_t)(boot_rotation / 90));
        display_unlock();
    } else {
        ESP_LOGE(TAG_DISPLAY, "LVGL lock timeout during rotation setup");
        return ESP_ERR_TIMEOUT;
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

