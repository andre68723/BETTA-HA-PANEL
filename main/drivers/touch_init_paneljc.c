/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Touch init for Guition JC8012P4A1 (GSL3680 / Silead, I2C).
 *
 * Hardware:
 *   Touch IC: GSL3680 (Silead), I2C address 0x40
 *   SDA: GPIO7   SCL: GPIO8   RST: GPIO22   INT: GPIO21
 *
 * The GSL3680 driver is vendored in main/drivers/gsl3680/ (Espressif's Apache-2.0
 * esp_lcd_touch_gsl3680 from esp-arduino-libs/ESP32_Display_Panel) because no
 * published ESP-IDF component exists for this Silead controller.
 *
 * Pins and bus settings follow the Guition reference Arduino example
 * (pins_config.h):
 *   https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6
 *
 * Coordinate orientation: x_max=800, y_max=1280 (native portrait panel).
 * LVGL rotation 90° (set in display_init_paneljc.c) transforms touch
 * coordinates to landscape 1280×800 automatically via LVGL.
 */
/* Cross-references (nothing copied): ESPHome's gsl3670 touchscreen component,
 * model GUITION-JC8012P4A1 (esphome/esphome, esphome/components/gsl3670, C++
 * part GPLv3): I2C SDA 7 / SCL 8 at 400 kHz without pull-ups, reset GPIO22,
 * interrupt GPIO21, swap_xy + mirror_x, raw range 20..880 x 20..1648, up to
 * 3 touches read from register 0x80, reset -> load firmware -> startup -> reset
 * -> startup sequence with 1 ms / 5 ms delays. The GSL3680 takes the same
 * firmware as the Seeed reTerminal D1001 (esphome/esphome PR 17843); ESPHome
 * downloads it from esphome-libs/gsl3670-firmware (Apache-2.0, provenance not
 * stated there); the firmware used here is Espressif's own copy. */
#include "drivers/touch_init.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "drivers/display_port.h"
#include "drivers/touch_latch.h"
#include "drivers/touch_track.h"
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
#define PANELJC_TOUCH_SCAN_MS    5     /* controller scan period, independent of LVGL's read timer */
#define PANELJC_TOUCH_SCAN_STACK 4096
#define PANELJC_TOUCH_SCAN_PRIO  10
#define PANELJC_TOUCH_INIT_RETRIES 8
#define PANELJC_TOUCH_RETRY_MS   250

/* ── State ───────────────────────────────────────────────────────────────── */
static bool                    s_touch_ready   = false;
static lv_indev_t             *s_touch_indev   = NULL;
static esp_lcd_touch_handle_t  s_touch_handle  = NULL;
static esp_lcd_panel_io_handle_t s_touch_io    = NULL;
static i2c_master_bus_handle_t   s_i2c_bus     = NULL;
static TaskHandle_t              s_scan_task    = NULL;
static touch_latch_t             s_latch;
static portMUX_TYPE              s_latch_lock   = portMUX_INITIALIZER_UNLOCKED;

/* ── Scan task and LVGL read ─────────────────────────────────────────────── */
/* LVGL reads its pointer only when its own timer runs (tens of milliseconds apart, more while a page is built), and a
 * quick tap that starts and ends between two reads was never seen: measured on the 10-inch (2026-10-18, /api/version
 * "touch"), 6 of 21 taps on the bottom bar produced no controller read at all. This task scans the controller every few
 * milliseconds and the latch (touch_latch.c) hands each press to LVGL at least once. */
static void touch_scan_task(void *arg)
{
    esp_lcd_touch_handle_t handle = (esp_lcd_touch_handle_t)arg;
    for (;;) {
        esp_lcd_touch_point_data_t point = {0};
        uint8_t count = 0;
        bool ok = esp_lcd_touch_read_data(handle) == ESP_OK &&
                  esp_lcd_touch_get_data(handle, &point, &count, 1) == ESP_OK;
        if (ok) {
            portENTER_CRITICAL(&s_latch_lock);
            touch_latch_scan(&s_latch, esp_timer_get_time(), count > 0, point.x, point.y);
            portEXIT_CRITICAL(&s_latch_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(PANELJC_TOUCH_SCAN_MS));
    }
}

/* Replaces the adapter's own controller read: LVGL takes what the scan task latched. */
static esp_err_t touch_latched_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points, uint8_t *count,
                                    uint8_t max_count, void *user_ctx)
{
    (void)tp;
    (void)user_ctx;
    uint16_t x = 0;
    uint16_t y = 0;
    portENTER_CRITICAL(&s_latch_lock);
    const bool pressed = touch_latch_read(&s_latch, esp_timer_get_time(), &x, &y);
    portEXIT_CRITICAL(&s_latch_lock);
    *count = (pressed && max_count > 0) ? 1 : 0;
    if (*count > 0) {
        points[0].x = x;
        points[0].y = y;
        points[0].strength = 0;
        points[0].track_id = 0;
    }
    return ESP_OK;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void touch_activity_event_cb(lv_event_t *event)
{
    (void)event;
    display_note_activity();
}

/* What LVGL made of the contacts: compare with the controller counters in /api/version "touch". */
static void touch_ui_event_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED: touch_track_note_ui(TOUCH_UI_PRESSED); break;
    case LV_EVENT_RELEASED: touch_track_note_ui(TOUCH_UI_RELEASED); break;
    case LV_EVENT_CLICKED: touch_track_note_ui(TOUCH_UI_CLICKED); break;
    case LV_EVENT_SCROLL_BEGIN: touch_track_note_ui(TOUCH_UI_SCROLLED); break;
    case LV_EVENT_LONG_PRESSED: touch_track_note_ui(TOUCH_UI_LONG); break;
    case LV_EVENT_PRESS_LOST: touch_track_note_ui(TOUCH_UI_LOST); break;
    default: break;
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

/* The camera (SCCB) shares this I2C bus; see ambient_camera.c. */
i2c_master_bus_handle_t touch_paneljc_i2c_bus(void)
{
    return s_i2c_bus;
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
        /* The driver reports native portrait coordinates (x short axis, y long axis). Y must be
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

    /* The scan task reads the controller; the adapter must not wait for an interrupt before it asks for data. The INT
     * pin stays configured for the reset sequence above; only the adapter's view of it changes. */
    s_touch_handle->config.int_gpio_num = GPIO_NUM_NC;
    touch_latch_reset(&s_latch);
    const esp_lv_adapter_touch_config_t touch_cfg = {
        .disp   = disp,
        .handle = s_touch_handle,
        .scale  = { .x = 1.0f, .y = 1.0f },
        .callbacks = { .custom_touch_read = touch_latched_read },
    };

    if (!display_lock(1000)) {
        touch_release_handles();
        return ESP_ERR_TIMEOUT;
    }
    s_touch_indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (s_touch_indev != NULL) {
        lv_indev_add_event_cb(s_touch_indev, touch_activity_event_cb,
            LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(s_touch_indev, touch_ui_event_cb, LV_EVENT_ALL, NULL);
        touch_apply_polling_tune(s_touch_indev);
        /* Start the sole controller reader only after all fallible binding steps. Otherwise a failed
         * registration would free the controller while the scan task still reads it. */
        if (xTaskCreatePinnedToCore(touch_scan_task, "touch_scan", PANELJC_TOUCH_SCAN_STACK, s_touch_handle,
                                   PANELJC_TOUCH_SCAN_PRIO, &s_scan_task, 0) != pdPASS) {
            esp_lv_adapter_unregister_touch(s_touch_indev);
            s_touch_indev = NULL;
            err = ESP_ERR_NO_MEM;
        }
    }
    display_unlock();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG_TOUCH, "esp_lv_adapter_register_touch failed");
        touch_release_handles();
        return err == ESP_ERR_NO_MEM ? err : ESP_FAIL;
    }

    s_touch_ready = true;
    ESP_LOGI(TAG_TOUCH,
        "Touch initialized (GSL3680 0x%02x, SDA=%d, SCL=%d, RST=%d, INT=%d, mirror_y=1)",
        ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS,
        PANELJC_TOUCH_SDA_GPIO, PANELJC_TOUCH_SCL_GPIO,
        PANELJC_TOUCH_RST_GPIO, PANELJC_TOUCH_INT_GPIO);
    return ESP_OK;
}

bool touch_is_ready(void) { return s_touch_ready; }
