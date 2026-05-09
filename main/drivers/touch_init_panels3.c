/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Touch init for the Guition CYD ESP32-S3 4848S040.
 *
 * Manufacturer Arduino examples:
 *   GT911 SDA=GPIO19, SCL=GPIO45, INT=-1, RST=-1
 *   TOUCH_MAP_X1=480, TOUCH_MAP_X2=0, TOUCH_MAP_Y1=480, TOUCH_MAP_Y2=0
 */
#include "drivers/touch_init.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "util/log_tags.h"

#define PANELS3_TOUCH_I2C_NUM       I2C_NUM_0
#define PANELS3_TOUCH_SDA_GPIO      GPIO_NUM_19
#define PANELS3_TOUCH_SCL_GPIO      GPIO_NUM_45
#define PANELS3_TOUCH_INT_GPIO      GPIO_NUM_NC
#define PANELS3_TOUCH_RST_GPIO      GPIO_NUM_NC
#define PANELS3_TOUCH_I2C_HZ        100000U
#define PANELS3_TOUCH_PROBE_MS      50
#define PANELS3_TOUCH_POLL_MS       10
#define PANELS3_TOUCH_INIT_RETRIES  8
#define PANELS3_TOUCH_RETRY_MS      250

static bool s_touch_ready = false;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_lcd_panel_io_handle_t s_touch_io_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static uint8_t s_gt911_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;

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

static esp_err_t touch_i2c_bus_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = PANELS3_TOUCH_I2C_NUM,
        .sda_io_num = PANELS3_TOUCH_SDA_GPIO,
        .scl_io_num = PANELS3_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(PANELS3_TOUCH_I2C_NUM, &s_i2c_bus);
    }
    return err;
}

static esp_err_t touch_detect_gt911_address(uint8_t *out_addr)
{
    if (out_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t candidates[] = {
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
    };

    for (size_t i = 0; i < sizeof(candidates); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, candidates[i], PANELS3_TOUCH_PROBE_MS);
        if (err == ESP_OK) {
            *out_addr = candidates[i];
            return ESP_OK;
        }
        ESP_LOGD(TAG_TOUCH, "GT911 probe 0x%02x failed: %s", candidates[i], esp_err_to_name(err));
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t touch_create_gt911(esp_lcd_touch_handle_t *out_touch)
{
    if (out_touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_touch = NULL;

    ESP_RETURN_ON_ERROR(touch_i2c_bus_init(), TAG_TOUCH, "I2C bus init failed");

    uint8_t addr = 0;
    ESP_RETURN_ON_ERROR(touch_detect_gt911_address(&addr), TAG_TOUCH, "GT911 not found on I2C");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = addr;
    io_cfg.scl_speed_hz = PANELS3_TOUCH_I2C_HZ;

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io_handle),
        TAG_TOUCH, "GT911 I2C IO init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = APP_SCREEN_WIDTH,
        .y_max = APP_SCREEN_HEIGHT,
        .rst_gpio_num = PANELS3_TOUCH_RST_GPIO,
        .int_gpio_num = PANELS3_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_err_t err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, out_touch);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(io_handle);
        return err;
    }

    s_touch_io_handle = io_handle;
    s_gt911_addr = addr;
    return ESP_OK;
}

static void touch_apply_polling_tune(lv_indev_t *indev)
{
    if (indev == NULL) {
        return;
    }

    lv_indev_set_mode(indev, LV_INDEV_MODE_TIMER);
    lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
    if (read_timer != NULL) {
        lv_timer_set_period(read_timer, PANELS3_TOUCH_POLL_MS);
        lv_timer_ready(read_timer);
    }
}

esp_err_t touch_init(void)
{
    if (s_touch_ready) {
        return ESP_OK;
    }
    if (!display_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_display_t *disp = touch_get_display();
    if (disp == NULL) {
        ESP_LOGE(TAG_TOUCH, "No active LVGL display for touch binding");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= PANELS3_TOUCH_INIT_RETRIES; ++attempt) {
        err = touch_create_gt911(&s_touch_handle);
        if (err == ESP_OK && s_touch_handle != NULL) {
            break;
        }

        ESP_LOGW(TAG_TOUCH, "GT911 init attempt %d/%d failed: %s",
            attempt, PANELS3_TOUCH_INIT_RETRIES, esp_err_to_name(err));
        touch_release_handle();
        if (attempt < PANELS3_TOUCH_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(PANELS3_TOUCH_RETRY_MS));
        }
    }

    if (err != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG_TOUCH, "GT911 init failed after %d attempts: %s",
            PANELS3_TOUCH_INIT_RETRIES, esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
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
    s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (s_touch_indev != NULL) {
        lv_indev_add_event_cb(s_touch_indev, touch_activity_event_cb, LV_EVENT_PRESSED, NULL);
    }
    display_unlock();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG_TOUCH, "lvgl_port_add_touch failed");
        touch_release_handle();
        return ESP_FAIL;
    }

    touch_apply_polling_tune(s_touch_indev);

    s_touch_ready = true;
    ESP_LOGI(TAG_TOUCH,
        "Touch initialized (GT911 addr=0x%02x, SDA=%d, SCL=%d, mirror_x=0, mirror_y=0)",
        s_gt911_addr, PANELS3_TOUCH_SDA_GPIO, PANELS3_TOUCH_SCL_GPIO);
    return ESP_OK;
}

bool touch_is_ready(void)
{
    return s_touch_ready;
}
