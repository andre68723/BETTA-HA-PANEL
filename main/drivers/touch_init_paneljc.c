/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Touch init for Guition JC8012P4A1 (GSL3680 / Silead, I2C).
 *
 * Hardware:
 *   Touch IC: GSL3680 (Silead), I2C address 0x40
 *   SDA: GPIO7   SCL: GPIO8   RST: GPIO22   INT: GPIO21
 *
 * The GSL3680 driver is vendored in main/drivers/gsl3680/ because no
 * published ESP-IDF component exists for this Silead controller.
 *
 * Touch config mirrors the Guition reference Arduino example
 * (pins_config.h + gsl3680_touch.cpp):
 *   https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6
 *
 * Coordinate orientation: x_max=800, y_max=1280 (native portrait panel).
 * LVGL rotation 90° (set in display_init_paneljc.c) transforms touch
 * coordinates to landscape 1280×800 automatically via lvgl_port.
 */
#include "drivers/touch_init.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "esp_lcd_gsl3680.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "util/log_tags.h"

/* ── Pin / bus constants ─────────────────────────────────────────────────── */
#define PANELJC_TOUCH_I2C_NUM    I2C_NUM_0
#define PANELJC_TOUCH_SDA_GPIO   GPIO_NUM_7
#define PANELJC_TOUCH_SCL_GPIO   GPIO_NUM_8
#define PANELJC_TOUCH_RST_GPIO   GPIO_NUM_22
#define PANELJC_TOUCH_INT_GPIO   GPIO_NUM_21
#define PANELJC_TOUCH_I2C_HZ     400000U

/* Native panel portrait dimensions (coordinates before LVGL rotation) */
#define PANELJC_TOUCH_X_MAX      800
#define PANELJC_TOUCH_Y_MAX      1280

#define PANELJC_TOUCH_POLL_MS    10
#define PANELJC_TOUCH_INIT_RETRIES 8
#define PANELJC_TOUCH_RETRY_MS   250

/* ── State ───────────────────────────────────────────────────────────────── */
static bool                    s_touch_ready   = false;
static lv_indev_t             *s_touch_indev   = NULL;
static esp_lcd_touch_handle_t  s_touch_handle  = NULL;
static esp_lcd_panel_io_handle_t s_touch_io    = NULL;
static i2c_master_bus_handle_t   s_i2c_bus     = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void touch_activity_event_cb(lv_event_t *event)
{
    (void)event;
    display_note_activity();
}

static lv_display_t *touch_get_display(void)
{
#if LV_VERSION_MAJOR >= 9
    return lv_display_get_default();
#else
    return lv_disp_get_default();
#endif
}

static void touch_release_handles(void)
{
    if (s_touch_handle != NULL) {
        esp_lcd_touch_del(s_touch_handle);
        s_touch_handle = NULL;
    }
    if (s_touch_io != NULL) {
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
    }
}

static esp_err_t touch_i2c_bus_init(void)
{
    if (s_i2c_bus != NULL) return ESP_OK;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port       = PANELJC_TOUCH_I2C_NUM,
        .sda_io_num     = PANELJC_TOUCH_SDA_GPIO,
        .scl_io_num     = PANELJC_TOUCH_SCL_GPIO,
        .clk_source     = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(PANELJC_TOUCH_I2C_NUM, &s_i2c_bus);
    }
    return err;
}

static esp_err_t touch_create_gsl3680(esp_lcd_touch_handle_t *out_touch)
{
    if (out_touch == NULL) return ESP_ERR_INVALID_ARG;
    *out_touch = NULL;

    ESP_RETURN_ON_ERROR(touch_i2c_bus_init(), TAG_TOUCH, "I2C bus init failed");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    io_cfg.scl_speed_hz = PANELJC_TOUCH_I2C_HZ;

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_touch_io),
        TAG_TOUCH, "GSL3680 I2C IO init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max         = PANELJC_TOUCH_X_MAX,
        .y_max         = PANELJC_TOUCH_Y_MAX,
        .rst_gpio_num  = PANELJC_TOUCH_RST_GPIO,
        .int_gpio_num  = PANELJC_TOUCH_INT_GPIO,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        /* The GSL3680 algorithm reports native portrait coordinates. Y must be
         * mirrored before LVGL maps the point into the logical 1280x800
         * landscape canvas. */
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };

    esp_err_t err = esp_lcd_touch_new_i2c_gsl3680(s_touch_io, &tp_cfg, out_touch);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
    }
    return err;
}

static void touch_apply_polling_tune(lv_indev_t *indev)
{
    if (indev == NULL) return;
    lv_indev_set_mode(indev, LV_INDEV_MODE_TIMER);
    lv_timer_t *t = lv_indev_get_read_timer(indev);
    if (t != NULL) {
        lv_timer_set_period(t, PANELJC_TOUCH_POLL_MS);
        lv_timer_ready(t);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
esp_err_t touch_init(void)
{
    if (s_touch_ready) return ESP_OK;
    if (!display_is_ready()) return ESP_ERR_INVALID_STATE;

    lv_display_t *disp = touch_get_display();
    if (disp == NULL) {
        ESP_LOGE(TAG_TOUCH, "No active LVGL display for touch binding");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= PANELJC_TOUCH_INIT_RETRIES; ++attempt) {
        err = touch_create_gsl3680(&s_touch_handle);
        if (err == ESP_OK && s_touch_handle != NULL) break;

        ESP_LOGW(TAG_TOUCH, "GSL3680 init attempt %d/%d failed: %s",
            attempt, PANELJC_TOUCH_INIT_RETRIES, esp_err_to_name(err));
        touch_release_handles();
        if (attempt < PANELJC_TOUCH_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(PANELJC_TOUCH_RETRY_MS));
        }
    }

    if (err != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG_TOUCH, "GSL3680 init failed after %d attempts: %s",
            PANELJC_TOUCH_INIT_RETRIES, esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = disp,
        .handle = s_touch_handle,
        .scale  = { .x = 1.0f, .y = 1.0f },
    };

    if (!display_lock(1000)) {
        touch_release_handles();
        return ESP_ERR_TIMEOUT;
    }
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (s_touch_indev != NULL) {
        lv_indev_add_event_cb(s_touch_indev, touch_activity_event_cb,
            LV_EVENT_PRESSED, NULL);
    }
    display_unlock();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG_TOUCH, "lvgl_port_add_touch failed");
        touch_release_handles();
        return ESP_FAIL;
    }

    touch_apply_polling_tune(s_touch_indev);

    s_touch_ready = true;
    ESP_LOGI(TAG_TOUCH,
        "Touch initialized (GSL3680 0x%02x, SDA=%d, SCL=%d, RST=%d, INT=%d, mirror_y=1)",
        ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS,
        PANELJC_TOUCH_SDA_GPIO, PANELJC_TOUCH_SCL_GPIO,
        PANELJC_TOUCH_RST_GPIO, PANELJC_TOUCH_INT_GPIO);
    return ESP_OK;
}

bool touch_is_ready(void) { return s_touch_ready; }
