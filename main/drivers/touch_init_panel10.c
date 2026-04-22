/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "drivers/touch_init.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp32_p4_nano.h"
#include "bsp/touch.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "drivers/display_init.h"
#include "util/log_tags.h"

static bool s_touch_ready = false;
static lv_indev_t *s_touch_indev = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static const int TOUCH_INIT_RETRIES = 8;
static const int TOUCH_INIT_RETRY_DELAY_MS = 250;
static const uint32_t TOUCH_POLL_PERIOD_MS = 10;

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
}

static void touch_apply_display_rotation_alignment(esp_lcd_touch_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    const bool swap_xy = false;
    const bool mirror_x = false;
    const bool mirror_y = false;

    esp_err_t err = esp_lcd_touch_set_swap_xy(handle, swap_xy);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "esp_lcd_touch_set_swap_xy failed: %s", esp_err_to_name(err));
    }
    err = esp_lcd_touch_set_mirror_x(handle, mirror_x);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "esp_lcd_touch_set_mirror_x failed: %s", esp_err_to_name(err));
    }
    err = esp_lcd_touch_set_mirror_y(handle, mirror_y);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "esp_lcd_touch_set_mirror_y failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG_TOUCH, "Touch rotation aligned (swap=%d, mirror_x=%d, mirror_y=%d)",
        swap_xy ? 1 : 0, mirror_x ? 1 : 0, mirror_y ? 1 : 0);
}

static void touch_apply_polling_tune(lv_indev_t *indev)
{
    if (indev == NULL) {
        return;
    }

    lv_indev_set_mode(indev, LV_INDEV_MODE_TIMER);
    lv_timer_t *read_timer = lv_indev_get_read_timer(indev);
    if (read_timer != NULL) {
        lv_timer_set_period(read_timer, TOUCH_POLL_PERIOD_MS);
        lv_timer_ready(read_timer);
    }

    ESP_LOGI(TAG_TOUCH, "Touch polling tuned: mode=timer, period=%lu ms",
        (unsigned long)TOUCH_POLL_PERIOD_MS);
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
        err = bsp_touch_new(NULL, &s_touch_handle);
        if (err == ESP_OK && s_touch_handle != NULL) {
            break;
        }
        ESP_LOGW(TAG_TOUCH, "bsp_touch_new attempt %d/%d failed: %s",
            attempt, TOUCH_INIT_RETRIES, esp_err_to_name(err));
        if (attempt < TOUCH_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(TOUCH_INIT_RETRY_DELAY_MS));
        }
    }
    if (err != ESP_OK || s_touch_handle == NULL) {
        ESP_LOGE(TAG_TOUCH, "bsp_touch_new failed after %d attempts: %s",
            TOUCH_INIT_RETRIES, esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    touch_apply_display_rotation_alignment(s_touch_handle);

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
    ESP_LOGI(TAG_TOUCH, "Touch initialized (esp_lvgl_port + GT911)");
    return ESP_OK;
}

bool touch_is_ready(void)
{
    return s_touch_ready;
}