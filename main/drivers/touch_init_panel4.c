/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "drivers/touch_init.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "drivers/display_init.h"
#include "util/log_tags.h"

static bool s_touch_ready = false;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static esp_lcd_panel_io_handle_t s_touch_io_handle = NULL;
static const int TOUCH_INIT_RETRIES = 8;
static const int TOUCH_INIT_RETRY_DELAY_MS = 250;
static const int TOUCH_GT911_RESET_ASSERT_MS = 20;
static const int TOUCH_GT911_POST_RESET_MS = 140;

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

static esp_err_t touch_gt911_prepare_reset(void)
{
    if (BSP_LCD_TOUCH_RST == GPIO_NUM_NC) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_GT911_POST_RESET_MS));
        return ESP_OK;
    }

    const gpio_config_t rst_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(BSP_LCD_TOUCH_RST),
    };
    esp_err_t err = gpio_config(&rst_gpio_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(BSP_LCD_TOUCH_RST, 0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_GT911_RESET_ASSERT_MS));

    err = gpio_set_level(BSP_LCD_TOUCH_RST, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_GT911_POST_RESET_MS));
    return ESP_OK;
}

static esp_err_t touch_create_gt911(esp_lcd_touch_handle_t *out_touch)
{
    if (out_touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_touch = NULL;

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    err = touch_gt911_prepare_reset();
    if (err != ESP_OK) {
        return err;
    }

    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (i2c_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;

    esp_lcd_panel_io_handle_t io_handle = NULL;
    err = esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &io_handle);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
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

    err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, out_touch);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(io_handle);
        return err;
    }

    s_touch_io_handle = io_handle;
    return ESP_OK;
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
    for (int attempt = 1; attempt <= TOUCH_INIT_RETRIES; attempt++) {
        err = touch_create_gt911(&s_touch_handle);
        if (err == ESP_OK && s_touch_handle != NULL) {
            break;
        }
        ESP_LOGW(TAG_TOUCH, "GT911 init attempt %d/%d failed: %s",
            attempt, TOUCH_INIT_RETRIES, esp_err_to_name(err));
        if (attempt < TOUCH_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(TOUCH_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG_TOUCH, "GT911 init failed after %d attempts: %s",
            TOUCH_INIT_RETRIES, esp_err_to_name(err));
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

    s_touch_ready = true;
    ESP_LOGI(TAG_TOUCH, "Touch initialized (esp_lvgl_port + GT911)");
    return ESP_OK;
}

bool touch_is_ready(void)
{
    return s_touch_ready;
}
