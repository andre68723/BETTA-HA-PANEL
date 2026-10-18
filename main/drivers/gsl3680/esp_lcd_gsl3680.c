/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork: Espressif's ESP Panel
 * configuration and logging dependencies removed, initialisation errors propagated and the
 * firmware checked after loading, touch coordinates reported in this project's native
 * portrait frame (x: short axis, y: long axis), one finger followed by nearest-point tracking that
 * holds implausible jumps. Replaces the earlier driver that depended on the GPL Silead
 * point-tracking algorithm.
 * Source: https://github.com/esp-arduino-libs/ESP32_Display_Panel (commit 92b790e,
 * src/drivers/touch/port/esp_lcd_touch_gsl3680.{c,h,_fw.h}), Apache-2.0, license text in
 * LICENSES/Apache-2.0.txt.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "drivers/touch_track.h"
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_gsl3680.h"
#include "esp_lcd_gsl3680_fw.h"

static const char *TAG = "GSL3680";

/* GSL3680 registers */
#define ESP_LCD_TOUCH_GSL3680_READ_XY_REG     (0x80)

//#define ESP_LCD_TOUCH_GSL3680_READ_KEY_REG    (0x8093)
#define ESP_LCD_TOUCH_GSL3680_CONFIG_REG      (0xF0)
//#define ESP_LCD_TOUCH_GSL3680_PRODUCT_ID_REG  (0x8140)
#define ESP_LCD_TOUCH_GSL3680_ENTER_SLEEP    (0x05)

/* GSL3680 support key num */
#define ESP_GSL3680_TOUCH_MAX_BUTTONS         (9)
//#define ESP_GSL3680_TOUCH_MAX_BUTTONS    CONFIG_ESP_LCD_TOUCH_MAX_POINTS


/*******************************************************************************
* Function definitions
*******************************************************************************/
static esp_err_t esp_lcd_touch_gsl3680_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_gsl3680_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gsl3680_get_button_state(esp_lcd_touch_handle_t tp, uint8_t n, uint8_t *state);
#endif
static esp_err_t esp_lcd_touch_gsl3680_del(esp_lcd_touch_handle_t tp);

/* I2C read/write */
static esp_err_t touch_gsl3680_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t touch_gsl3680_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);

/* GSL3680 reset */
static esp_err_t touch_gsl3680_reset(esp_lcd_touch_handle_t tp);
/* Read status and config register */
static esp_err_t touch_gsl3680_read_cfg(esp_lcd_touch_handle_t tp);

/* GSL3680 enter/exit sleep mode */
static esp_err_t esp_lcd_touch_gsl3680_enter_sleep(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gsl3680_exit_sleep(esp_lcd_touch_handle_t tp);

static esp_err_t esp_lcd_touch_gsl3680_startup_chip(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gsl3680_read_ram_fw(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gsl3680_load_fw(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gsl3680_clear_reg(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gsl3680_init(esp_lcd_touch_handle_t tp);
static void gsl3680_track_reset(void);

/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t esp_lcd_touch_new_i2c_gsl3680(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    ESP_LOGI(TAG, "GSL3680 driver version: %d.%d.%d", ESP_LCD_TOUCH_GSL3680_VER_MAJOR, ESP_LCD_TOUCH_GSL3680_VER_MINOR,
             ESP_LCD_TOUCH_GSL3680_VER_PATCH);
    esp_err_t ret = ESP_OK;

    assert(io != NULL);
    assert(config != NULL);
    assert(out_touch != NULL);
    gsl3680_track_reset();

    /* Prepare main structure */
    // Use `calloc` instead of `heap_caps_calloc` for MicroPython compatibility
    esp_lcd_touch_handle_t esp_lcd_touch_gsl3680 = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_GOTO_ON_FALSE(esp_lcd_touch_gsl3680, ESP_ERR_NO_MEM, err, TAG, "no mem for GSL3680 controller");

    /* Communication interface */
    esp_lcd_touch_gsl3680->io = io;

    /* Only supported callbacks are set */
    esp_lcd_touch_gsl3680->read_data = esp_lcd_touch_gsl3680_read_data;
    esp_lcd_touch_gsl3680->get_xy = esp_lcd_touch_gsl3680_get_xy;
#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    esp_lcd_touch_gsl3680->get_button_state = esp_lcd_touch_gsl3680_get_button_state;
#endif
    esp_lcd_touch_gsl3680->del = esp_lcd_touch_gsl3680_del;
    esp_lcd_touch_gsl3680->enter_sleep = esp_lcd_touch_gsl3680_enter_sleep;
    esp_lcd_touch_gsl3680->exit_sleep = esp_lcd_touch_gsl3680_exit_sleep;

    /* Mutex */
    esp_lcd_touch_gsl3680->data.lock.owner = portMUX_FREE_VAL;

    /* Save config */
    memcpy(&esp_lcd_touch_gsl3680->config, config, sizeof(esp_lcd_touch_config_t));

    /* Prepare pin for touch controller reset */
    if (esp_lcd_touch_gsl3680->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(esp_lcd_touch_gsl3680->config.rst_gpio_num)
        };
        ret = gpio_config(&rst_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
    }

    if (esp_lcd_touch_gsl3680->config.rst_gpio_num != GPIO_NUM_NC && esp_lcd_touch_gsl3680->config.int_gpio_num != GPIO_NUM_NC) {
        /* Prepare pin for touch controller int */
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
            .pull_down_en = 0,
            .pull_up_en = 1,
            .pin_bit_mask = BIT64(esp_lcd_touch_gsl3680->config.int_gpio_num),
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

        ret = gpio_set_level(esp_lcd_touch_gsl3680->config.rst_gpio_num, esp_lcd_touch_gsl3680->config.levels.reset);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO set level error!");
        ret = gpio_set_level(esp_lcd_touch_gsl3680->config.int_gpio_num, 0);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Select I2C addr, set output high or low */
        uint32_t gpio_level = 0;

        ret = gpio_set_level(esp_lcd_touch_gsl3680->config.int_gpio_num, gpio_level);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(1));

        ret = gpio_set_level(esp_lcd_touch_gsl3680->config.rst_gpio_num, !esp_lcd_touch_gsl3680->config.levels.reset);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));

        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        ESP_LOGW(TAG, "Unable to initialize the I2C address");
        /* Reset controller */
        ret = touch_gsl3680_reset(esp_lcd_touch_gsl3680);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GSL3680 reset failed");
    }

    /* Read status and config info */
    ESP_LOGI(TAG, "init GSL3680");
    ret = touch_gsl3680_read_cfg(esp_lcd_touch_gsl3680);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GSL3680 config read failed");
    ret = esp_lcd_touch_gsl3680_init(esp_lcd_touch_gsl3680);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GSL3680 init sequence failed");
    ret = esp_lcd_touch_gsl3680_read_ram_fw(esp_lcd_touch_gsl3680);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "GSL3680 firmware verification failed");

    /* Prepare pin for touch interrupt */
    if (esp_lcd_touch_gsl3680->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (esp_lcd_touch_gsl3680->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(esp_lcd_touch_gsl3680->config.int_gpio_num)
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

        /* Register interrupt callback */
        if (esp_lcd_touch_gsl3680->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_gsl3680, esp_lcd_touch_gsl3680->config.interrupt_callback);
        }
    }

    *out_touch = esp_lcd_touch_gsl3680;
    return ESP_OK;

err:
    ESP_LOGE(TAG, "Error (0x%x)! Touch controller GSL3680 initialization failed!", ret);
    if (esp_lcd_touch_gsl3680) {
        esp_lcd_touch_gsl3680_del(esp_lcd_touch_gsl3680);
    }

    return ret;
}

static esp_err_t esp_lcd_touch_gsl3680_enter_sleep(esp_lcd_touch_handle_t tp)
{
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, 0), TAG, "Enter Sleep failed!");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_gsl3680_exit_sleep(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, 1), TAG, "High GPIO config failed!");
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

/* Raw controller range per axis. The controller's X axis runs along the panel's long side. */
#define RAW_MAX_X 1650
#define RAW_MAX_Y 890
/* Bytes 4..23 of the report hold up to five 4-byte points. */
#define GSL3680_REPORT_POINTS 5

/* The controller sends stray points (a ghost at the edge in the first frame of a contact, reordered points of a second
 * finger, a bad frame at lift-off). touch_track.c follows one finger through them, see there. */
#define GSL3680_LOG_GAP_US 1000000

static touch_track_t s_track;
static int64_t s_last_log_us;

static void gsl3680_track_reset(void)
{
    touch_track_reset(&s_track);
    s_last_log_us = 0;
}

static esp_err_t esp_lcd_touch_gsl3680_read_data(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    uint8_t touch_data[24] = {0};

    esp_err_t err = touch_gsl3680_i2c_read(tp, ESP_LCD_TOUCH_GSL3680_READ_XY_REG, touch_data, sizeof(touch_data));
    touch_track_note_read(esp_timer_get_time(), err != ESP_OK, err == ESP_OK && (touch_data[0] & 0x0F) == 0);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    uint8_t touch_cnt = touch_data[0] & 0x0F;
    if (touch_cnt == 0) {
        touch_track_reset(&s_track); /* finger lifted: the next contact starts fresh */
        return ESP_OK;
    }
    if (touch_cnt > GSL3680_REPORT_POINTS || touch_cnt > CONFIG_ESP_LCD_TOUCH_MAX_POINTS) {
        touch_cnt = (GSL3680_REPORT_POINTS < CONFIG_ESP_LCD_TOUCH_MAX_POINTS) ? GSL3680_REPORT_POINTS : CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
    }

    const int64_t now_us = esp_timer_get_time();
    uint16_t px[GSL3680_REPORT_POINTS];
    uint16_t py[GSL3680_REPORT_POINTS];
    int valid = 0;
    unsigned flagged = 0;
    unsigned invalid = 0;
    for (int i = 0; i < touch_cnt; i++) {
        size_t offset = 4 + (i * 4);

        uint16_t raw_x = ((uint16_t)(touch_data[offset + 3] & 0x0F) << 8) | touch_data[offset + 2];
        uint16_t raw_y = ((uint16_t)(touch_data[offset + 1]) << 8) | touch_data[offset + 0];
        /* The controller sets 0x4000 in the Y word when the signal is weak, typically in the first frames of a contact
         * and near the edges. The coordinate in the low bits is still the touch point, so it is kept. */
        if ((raw_y & TOUCH_TRACK_Y_FLAG) != 0) {
            flagged++;
        }
        raw_y &= TOUCH_TRACK_Y_MASK;
        if (!touch_track_raw_valid(raw_x, raw_y, RAW_MAX_X, RAW_MAX_Y)) {
            invalid++;
            if (now_us - s_last_log_us >= GSL3680_LOG_GAP_US) {
                s_last_log_us = now_us;
                ESP_LOGW(TAG, "touch point out of range ignored: raw (%u,%u) of %u point(s)", raw_x, raw_y, (unsigned)touch_cnt);
            }
            continue;
        }

        /* Native portrait frame: x along the short side (config.x_max), y along the long side
         * (config.y_max). Espressif's reference maps raw X to y_max and raw Y to x_max; the two
         * outputs are swapped here so the caller needs no swap_xy. */
        uint32_t x = ((uint32_t)raw_y * tp->config.x_max) / RAW_MAX_Y;
        uint32_t y = ((uint32_t)raw_x * tp->config.y_max) / RAW_MAX_X;
        if (x >= tp->config.x_max) {
            x = tp->config.x_max - 1;
        }
        if (y >= tp->config.y_max) {
            y = tp->config.y_max - 1;
        }
        px[valid] = (uint16_t)x;
        py[valid] = (uint16_t)y;
        valid++;
    }

    touch_track_note_frame(flagged, invalid);
    const bool was_tracking = s_track.tracking;
    uint16_t out_x = 0;
    uint16_t out_y = 0;
    const touch_track_result_t result = touch_track_update(&s_track, px, py, valid, now_us, &out_x, &out_y);
    if (result == TOUCH_TRACK_NONE) {
        return ESP_OK;
    }
    if (result == TOUCH_TRACK_HELD && now_us - s_last_log_us >= GSL3680_LOG_GAP_US) {
        s_last_log_us = now_us;
        ESP_LOGW(TAG, "touch jump held at (%u,%u): %d usable of %u point(s), first (%d,%d)%s", out_x, out_y, valid,
            (unsigned)touch_cnt, valid > 0 ? px[0] : -1, valid > 0 ? py[0] : -1, was_tracking ? "" : " (new contact)");
    }

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 1; /* LVGL's pointer follows one finger */
    tp->data.coords[0].x = out_x;
    tp->data.coords[0].y = out_y;
    tp->data.coords[0].strength = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool esp_lcd_touch_gsl3680_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    /* Count of points */
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    /* Invalidate */
    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gsl3680_get_button_state(esp_lcd_touch_handle_t tp, uint8_t n, uint8_t *state)
{
    esp_err_t err = ESP_OK;
    assert(tp != NULL);
    assert(state != NULL);

    *state = 0;

    portENTER_CRITICAL(&tp->data.lock);

    if (n > tp->data.buttons) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        *state = tp->data.button[n].status;
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return err;
}
#endif

static esp_err_t esp_lcd_touch_gsl3680_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    /* Reset GPIO pin settings */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    /* Reset GPIO pin settings */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);

    return ESP_OK;
}

/*******************************************************************************
* Private API function
*******************************************************************************/

/* Init controller */
static esp_err_t esp_lcd_touch_gsl3680_init(esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "start init");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_gsl3680_clear_reg(tp), TAG, "clear reg failed");
    ESP_RETURN_ON_ERROR(touch_gsl3680_reset(tp), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_gsl3680_load_fw(tp), TAG, "firmware load failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_gsl3680_startup_chip(tp), TAG, "startup failed");
    ESP_RETURN_ON_ERROR(touch_gsl3680_reset(tp), TAG, "second reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_gsl3680_startup_chip(tp), TAG, "second startup failed");

    return ESP_OK;
}

/* Reset controller */
static esp_err_t touch_gsl3680_reset(esp_lcd_touch_handle_t tp)
{
    esp_err_t err;
    unsigned char write_buf[4];
    uint8_t addr;
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    addr = 0xe0;
    write_buf[0] = 0x88;
    err = touch_gsl3680_i2c_write(tp, addr, write_buf, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C write error!");
    vTaskDelay(pdMS_TO_TICKS(10));

    addr = 0xe4;
    write_buf[0] = 0x04;
    err = touch_gsl3680_i2c_write(tp, addr, write_buf, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C write error!");
    vTaskDelay(pdMS_TO_TICKS(10));

    write_buf[0] = 0x00;
    write_buf[1] = 0x00;
    write_buf[2] = 0x00;
    write_buf[3] = 0x00;
    err = touch_gsl3680_i2c_write(tp, 0xbc, write_buf, 4);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C write error!");

    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

static esp_err_t touch_gsl3680_read_cfg(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[4];
    uint8_t write[4];
    // uint8_t i2c_buffer_read = 0;
    // uint8_t i2c_buffer_write = 0x12;

    write[0] = 0x12;
    write[1] = 0x34;
    write[2] = 0x56;
    assert(tp != NULL);

    ESP_LOGI(TAG, "GSL3680 connect");

    ESP_RETURN_ON_ERROR(touch_gsl3680_i2c_read(tp, 0xf0, (uint8_t *)&buf, 4), TAG, "I2C read error!");
    ESP_LOGI(TAG, "GSL3680 read reg 0xf0 before is %x %x %x %x", buf[0], buf[1], buf[2], buf[3]);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "GSL3680 writing 0xf0 0x12");
    ESP_RETURN_ON_ERROR(touch_gsl3680_i2c_write(tp, 0xf0, write, 4), TAG, "I2C write error");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(touch_gsl3680_i2c_read(tp, 0xf0, (uint8_t *)&buf, 4), TAG, "I2C read error!");
    ESP_LOGI(TAG, "GSL3680 read reg 0xf0 after is %x %x %x %x", buf[0], buf[1], buf[2], buf[3]);

    return ESP_OK;

    //TODO: Check this fix
    /*
    if(i2c_buffer_read == i2c_buffer_write)
    {
        ret = ESP_OK;
        ESP_LOGI(TAG, "Read config success");
    }
    else
        ret = ESP_FAIL;*/

    // return ret;
}

static esp_err_t esp_lcd_touch_gsl3680_startup_chip(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret = ESP_OK;
    uint8_t write_buf[4];
    uint8_t addr = 0xe0;
    write_buf[0] = 0x00;
    ESP_LOGI(TAG, "enter");
    ESP_RETURN_ON_ERROR(touch_gsl3680_i2c_write(tp, addr, write_buf, 1), TAG, "I2C write error");
    vTaskDelay(pdMS_TO_TICKS(10));

    return ret;
}

static esp_err_t esp_lcd_touch_gsl3680_read_ram_fw(esp_lcd_touch_handle_t tp)
{
    uint8_t read_buf[4];
    uint8_t addr = 0xb0;
    ESP_LOGI(TAG, "enter read_ram_fw");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(touch_gsl3680_i2c_read(tp, addr, (uint8_t *)&read_buf, 4), TAG, "I2C read error!");
    ESP_LOGI(TAG, "GSL3680 firmware check: read 0xb0 = %x,%x,%x,%x ", read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
    if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t touch_gsl3680_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t touch_gsl3680_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);

    // *INDENT-OFF*
    /* Write data */
    return esp_lcd_panel_io_tx_param(tp->io, reg, data, len);
    // *INDENT-ON*
}

static esp_err_t esp_lcd_touch_gsl3680_load_fw(esp_lcd_touch_handle_t tp)
{
    ESP_LOGI(TAG, "start load fw");
    uint16_t addr;
    unsigned char wrbuf[4];
    size_t source_len = sizeof(GSLX680_FW) / sizeof(struct fw_data);

    for (size_t source_line = 0; source_line < source_len; source_line++) {
        addr = (uint8_t)GSLX680_FW[source_line].offset;
        wrbuf[0] = (uint8_t)(GSLX680_FW[source_line].val & 0x000000ff);
        wrbuf[1] = (uint8_t)((GSLX680_FW[source_line].val & 0x0000ff00) >> 8);
        wrbuf[2] = (uint8_t)((GSLX680_FW[source_line].val & 0x00ff0000) >> 16);
        wrbuf[3] = (uint8_t)((GSLX680_FW[source_line].val & 0xff000000) >> 24);
        esp_err_t err = (addr == 0xf0)
            ? touch_gsl3680_i2c_write(tp, addr, wrbuf, 1)
            : touch_gsl3680_i2c_write(tp, addr, wrbuf, 4);
        ESP_RETURN_ON_ERROR(err, TAG, "GSL3680 firmware write failed");
        if ((source_line % 256U) == 0U && esp_task_wdt_status(NULL) == ESP_OK) {
            (void)esp_task_wdt_reset();
        }
    }
    ESP_LOGI(TAG, "load fw success");
    return ESP_OK;
}

static esp_err_t esp_lcd_touch_gsl3680_clear_reg(esp_lcd_touch_handle_t tp)
{
    uint8_t addr;
    uint8_t wrbuf[4];

    ESP_LOGI(TAG, "clear reg");
    addr = 0xe0;
    wrbuf[0] = 0x88;
    touch_gsl3680_i2c_write(tp, addr, wrbuf, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    addr = 0x88;
    wrbuf[0] = 0x01;
    touch_gsl3680_i2c_write(tp, addr, wrbuf, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    addr = 0xe4;
    wrbuf[0] = 0x04;
    touch_gsl3680_i2c_write(tp, addr, wrbuf, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    addr = 0xe0;
    wrbuf[0] = 0x00;
    touch_gsl3680_i2c_write(tp, addr, wrbuf, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

