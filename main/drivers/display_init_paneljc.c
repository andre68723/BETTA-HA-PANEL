/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Third-party material in this file (details at each definition):
 *   - Pin map and V1 DSI/DPI timing follow the Guition reference Arduino
 *     example, sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6 (no license
 *     declared by that repository).
 *   - The V1 JD9365 init table matches espressif/esp_lcd_jd9365's default
 *     init sequence (Copyright 2024 Espressif Systems (Shanghai) CO LTD,
 *     SPDX-License-Identifier: Apache-2.0).
 *   - The V2 JD9365 init table and DSI/DPI timing come from
 *     jtenniswood/espcontrol (PolyForm Noncommercial License 1.0.0;
 *     Required Notice: Copyright (c) 2026 jtenniswood).
 *   - Cross-check only, nothing copied from it: ESPHome's mipi_dsi models
 *     for "JC8012P4A1" and "JC8012P4A1-V2" (esphome/esphome,
 *     esphome/components/mipi_dsi/models/guition.py, MIT for the Python
 *     part). Its V1 table agrees with ours in 193 of 195 entries and it lists
 *     V1 as 60 MHz pclk / 1 Gbps, V2 as 70 MHz / 1.5 Gbps with vsync back
 *     porch 10 / front porch 20. Its V2 table equals ours (the two extra
 *     entries here are the plain sleep-out and display-on commands); the V2 DSI
 *     timing below follows ESPHome now (espcontrol used 840 Mbps and front
 *     porch 30 before).
 *   - "V3" is not a third panel: it is a JC8012P4A1 with ESP32-P4 v3.x
 *     production silicon (espcontrol docs/screens/jc8012p4a1-v3.md, ESPHome
 *     issue 19237). Same JD9365 init as V2; the MIPI DSI PHY clock source must
 *     be left at 0 so ESP-IDF picks XTAL on v3 silicon. This build targets
 *     pre-v3 silicon (CONFIG_ESP32P4_SELECTS_REV_LESS_V3) and does not boot
 *     on v3.x chips.
 *
 * Display init for Guition JC8012P4A1 (ESP32-P4 + JD9365 MIPI-DSI 800×1280).
 *
 * Hardware:
 *   Panel:     JD9365, 800×1280 native portrait, 2-lane MIPI-DSI
 *   Backlight: LEDC GPIO23 (active-high, 10-bit PWM @ 1 kHz)
 *   LCD Reset: GPIO27
 *   MIPI LDO:  channel 3 → VDD_MIPI_DPHY @ 2500 mV
 *
 * Native orientation is portrait (800×1280).  LVGL rotation 90° gives
 * landscape 1280×800, matching the panel10 content-box dimensions.
 *
 * Touch (GSL3680, I2C SDA=7 SCL=8 RST=22 INT=21): see touch_init_paneljc.c
 *
 * Pin map from Guition reference Arduino example (pins_config.h):
 *   https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6
 *
 * Panel revisions: the JC8012P4A1(C) ships with physically different panel
 * hardware under the same SKU, identified by a rear-case date code. This
 * file's default init table/timing target the original ("V1") panel; set
 * CONFIG_APP_PANELJC_PANEL_V2 for the newer ("V2") panel. See docs/choose-display.md
 * for how to tell which one you have.
 */
#include "drivers/display_backlight.h"
#include "drivers/display_init.h"

#include <stdbool.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "drivers/display_port.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/log_tags.h"

/* ── Pin / hardware constants ────────────────────────────────────────────── */
#define PANELJC_BL_GPIO              GPIO_NUM_23
#define PANELJC_LCD_RST_GPIO         GPIO_NUM_27
#define PANELJC_MIPI_LDO_CHAN        3
#define PANELJC_MIPI_LDO_MV          2500

/* Native panel resolution (portrait). LVGL rotates to landscape 1280×800. */
#define PANELJC_PANEL_H_RES          800
#define PANELJC_PANEL_V_RES          1280

#if CONFIG_APP_PANELJC_PANEL_V2
/* "V2" panel revision (rear case date code ~2628+). Timing sourced from
 * jtenniswood/espcontrol (github.com/jtenniswood/espcontrol), device profile
 * guition-esp32-p4-jc8012p4a1-v2/device/device.yaml. hsync timing is
 * unchanged from V1; only pclk and vsync porches differ.
 *
 * That project is licensed under the PolyForm Noncommercial License 1.0.0
 * (https://polyformproject.org/licenses/noncommercial/1.0.0), which requires
 * propagating its Required Notice line verbatim to anyone who gets this code
 * from us:
 * Required Notice: Copyright (c) 2026 jtenniswood */
/* 70 MHz pixel clock and 1.5 Gbit/s per lane as in ESPHome's "JC8012P4A1-V2" model
 * (esphome/esphome, mipi_dsi/models/guition.py) and Guition's own ESP-IDF 6.0.2 "New Panel"
 * demo; vsync porches 10/30 as in that Guition demo (JD9365_800_1280_PANEL_60HZ_DPI_CONFIG) and
 * in espcontrol (ESPHome uses a front porch of 20). Not verified here: no V2 hardware. If a V2
 * panel shows noise, try Guition's older 1000 Mbit/s (its ESP-IDF 5.5.4 demo). */
#define PANELJC_DSI_LANE_BITRATE_MBPS 1500
#define PANELJC_DPI_CLK_MHZ          70
#define PANELJC_VSYNC_BACK_PORCH     10
#define PANELJC_VSYNC_FRONT_PORCH    30
#else
#define PANELJC_DSI_LANE_BITRATE_MBPS 1000
#define PANELJC_DPI_CLK_MHZ          60
#define PANELJC_VSYNC_BACK_PORCH     8
#define PANELJC_VSYNC_FRONT_PORCH    20
#endif

/* ── LEDC backlight ──────────────────────────────────────────────────────── */
#define BL_LEDC_TIMER                LEDC_TIMER_0
#define BL_LEDC_MODE                 LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL              LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES             LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ              1000U

/* ── State ───────────────────────────────────────────────────────────────── */
static bool                    s_display_ready = false;
static lv_display_t           *s_lv_display    = NULL;
static esp_lcd_panel_handle_t  s_panel         = NULL;
static esp_lcd_panel_io_handle_t s_dbi_io      = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* ── Backlight LEDC ──────────────────────────────────────────────────────── */
static esp_err_t backlight_ledc_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG_DISPLAY, "LEDC timer config failed");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = PANELJC_BL_GPIO,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch_cfg);
}

esp_err_t display_backlight_write(int percent)
{
    const uint32_t duty = (uint32_t)(percent * ((1u << 10) - 1)) / 100u;
    esp_err_t err = ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    if (err == ESP_OK) err = ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_DISPLAY, "backlight set %d%% (LEDC duty %"PRIu32"/1023 on GPIO%d)",
            percent, duty, (int)PANELJC_BL_GPIO);
    } else {
        ESP_LOGW(TAG_DISPLAY, "backlight set %d%% failed: %s", percent, esp_err_to_name(err));
    }
    return err;
}

/* Ambient light (camera): the configured level is scaled with the room light factor (10..100 %),
 * but never below 5 % (or the level itself when that is lower). */
/* ── Dim timer ───────────────────────────────────────────────────────────── */
/* ── JD9365 MIPI-DSI panel ───────────────────────────────────────────────── */
static esp_err_t mipi_dsi_phy_power_on(void)
{
    /* VDD_MIPI_DPHY must be at 2.5 V.  The ESP32-P4 internal LDO channel 3
     * is wired to VDD_MIPI_DPHY on the Guition JC8012P4A1 board. */
    esp_ldo_channel_handle_t ldo = NULL;
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = PANELJC_MIPI_LDO_CHAN,
        .voltage_mv = PANELJC_MIPI_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo),
        TAG_DISPLAY, "MIPI DSI PHY LDO acquire failed");
    ESP_LOGI(TAG_DISPLAY, "MIPI DSI PHY powered (LDO ch%d, %dmV)",
        PANELJC_MIPI_LDO_CHAN, PANELJC_MIPI_LDO_MV);
    return ESP_OK;
}

#if CONFIG_APP_PANELJC_PANEL_V2
/* "V2" panel revision (rear case date code ~2628+); init table sourced from
 * jtenniswood/espcontrol (github.com/jtenniswood/espcontrol), device profile
 * guition-esp32-p4-jc8012p4a1-v2/device/device.yaml.
 *
 * That project is licensed under the PolyForm Noncommercial License 1.0.0
 * (https://polyformproject.org/licenses/noncommercial/1.0.0), which requires
 * propagating its Required Notice line verbatim to anyone who gets this code
 * from us:
 * Required Notice: Copyright (c) 2026 jtenniswood */
static const jd9365_lcd_init_cmd_t paneljc_jd9365_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x44}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x38}, 1, 0},
    {0x0C, (uint8_t[]){0x74}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xAF}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xAF}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x09}, 1, 0},
    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x01}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0x7F}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0xA0}, 1, 0},
    {0x42, (uint8_t[]){0x81}, 1, 0},
    {0x43, (uint8_t[]){0x1E}, 1, 0},
    {0x44, (uint8_t[]){0x0D}, 1, 0},
    {0x45, (uint8_t[]){0x28}, 1, 0},
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x69}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x2A}, 1, 0},
    {0x5B, (uint8_t[]){0x17}, 1, 0},
    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x6B}, 1, 0},
    {0x5F, (uint8_t[]){0x5C}, 1, 0},
    {0x60, (uint8_t[]){0x50}, 1, 0},
    {0x61, (uint8_t[]){0x4C}, 1, 0},
    {0x62, (uint8_t[]){0x3E}, 1, 0},
    {0x63, (uint8_t[]){0x41}, 1, 0},
    {0x64, (uint8_t[]){0x2B}, 1, 0},
    {0x65, (uint8_t[]){0x43}, 1, 0},
    {0x66, (uint8_t[]){0x42}, 1, 0},
    {0x67, (uint8_t[]){0x43}, 1, 0},
    {0x68, (uint8_t[]){0x62}, 1, 0},
    {0x69, (uint8_t[]){0x52}, 1, 0},
    {0x6A, (uint8_t[]){0x5A}, 1, 0},
    {0x6B, (uint8_t[]){0x4C}, 1, 0},
    {0x6C, (uint8_t[]){0x48}, 1, 0},
    {0x6D, (uint8_t[]){0x3A}, 1, 0},
    {0x6E, (uint8_t[]){0x28}, 1, 0},
    {0x6F, (uint8_t[]){0x10}, 1, 0},
    {0x70, (uint8_t[]){0x7F}, 1, 0},
    {0x71, (uint8_t[]){0x6B}, 1, 0},
    {0x72, (uint8_t[]){0x5C}, 1, 0},
    {0x73, (uint8_t[]){0x50}, 1, 0},
    {0x74, (uint8_t[]){0x4C}, 1, 0},
    {0x75, (uint8_t[]){0x3E}, 1, 0},
    {0x76, (uint8_t[]){0x41}, 1, 0},
    {0x77, (uint8_t[]){0x2B}, 1, 0},
    {0x78, (uint8_t[]){0x43}, 1, 0},
    {0x79, (uint8_t[]){0x42}, 1, 0},
    {0x7A, (uint8_t[]){0x43}, 1, 0},
    {0x7B, (uint8_t[]){0x62}, 1, 0},
    {0x7C, (uint8_t[]){0x52}, 1, 0},
    {0x7D, (uint8_t[]){0x5A}, 1, 0},
    {0x7E, (uint8_t[]){0x4C}, 1, 0},
    {0x7F, (uint8_t[]){0x48}, 1, 0},
    {0x80, (uint8_t[]){0x3A}, 1, 0},
    {0x81, (uint8_t[]){0x28}, 1, 0},
    {0x82, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x42}, 1, 0},
    {0x01, (uint8_t[]){0x42}, 1, 0},
    {0x02, (uint8_t[]){0x40}, 1, 0},
    {0x03, (uint8_t[]){0x40}, 1, 0},
    {0x04, (uint8_t[]){0x5E}, 1, 0},
    {0x05, (uint8_t[]){0x5E}, 1, 0},
    {0x06, (uint8_t[]){0x5F}, 1, 0},
    {0x07, (uint8_t[]){0x5F}, 1, 0},
    {0x08, (uint8_t[]){0x5F}, 1, 0},
    {0x09, (uint8_t[]){0x57}, 1, 0},
    {0x0A, (uint8_t[]){0x57}, 1, 0},
    {0x0B, (uint8_t[]){0x77}, 1, 0},
    {0x0C, (uint8_t[]){0x77}, 1, 0},
    {0x0D, (uint8_t[]){0x47}, 1, 0},
    {0x0E, (uint8_t[]){0x47}, 1, 0},
    {0x0F, (uint8_t[]){0x45}, 1, 0},
    {0x10, (uint8_t[]){0x45}, 1, 0},
    {0x11, (uint8_t[]){0x4B}, 1, 0},
    {0x12, (uint8_t[]){0x4B}, 1, 0},
    {0x13, (uint8_t[]){0x49}, 1, 0},
    {0x14, (uint8_t[]){0x49}, 1, 0},
    {0x15, (uint8_t[]){0x5F}, 1, 0},
    {0x16, (uint8_t[]){0x41}, 1, 0},
    {0x17, (uint8_t[]){0x41}, 1, 0},
    {0x18, (uint8_t[]){0x40}, 1, 0},
    {0x19, (uint8_t[]){0x40}, 1, 0},
    {0x1A, (uint8_t[]){0x5E}, 1, 0},
    {0x1B, (uint8_t[]){0x5E}, 1, 0},
    {0x1C, (uint8_t[]){0x5F}, 1, 0},
    {0x1D, (uint8_t[]){0x5F}, 1, 0},
    {0x1E, (uint8_t[]){0x5F}, 1, 0},
    {0x1F, (uint8_t[]){0x57}, 1, 0},
    {0x20, (uint8_t[]){0x57}, 1, 0},
    {0x21, (uint8_t[]){0x77}, 1, 0},
    {0x22, (uint8_t[]){0x77}, 1, 0},
    {0x23, (uint8_t[]){0x46}, 1, 0},
    {0x24, (uint8_t[]){0x46}, 1, 0},
    {0x25, (uint8_t[]){0x44}, 1, 0},
    {0x26, (uint8_t[]){0x44}, 1, 0},
    {0x27, (uint8_t[]){0x4A}, 1, 0},
    {0x28, (uint8_t[]){0x4A}, 1, 0},
    {0x29, (uint8_t[]){0x48}, 1, 0},
    {0x2A, (uint8_t[]){0x48}, 1, 0},
    {0x2B, (uint8_t[]){0x5F}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x2D, (uint8_t[]){0x01}, 1, 0},
    {0x2E, (uint8_t[]){0x00}, 1, 0},
    {0x2F, (uint8_t[]){0x00}, 1, 0},
    {0x30, (uint8_t[]){0x1F}, 1, 0},
    {0x31, (uint8_t[]){0x1F}, 1, 0},
    {0x32, (uint8_t[]){0x1E}, 1, 0},
    {0x33, (uint8_t[]){0x1E}, 1, 0},
    {0x34, (uint8_t[]){0x1F}, 1, 0},
    {0x35, (uint8_t[]){0x17}, 1, 0},
    {0x36, (uint8_t[]){0x17}, 1, 0},
    {0x37, (uint8_t[]){0x37}, 1, 0},
    {0x38, (uint8_t[]){0x37}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x08}, 1, 0},
    {0x3B, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x0A}, 1, 0},
    {0x3D, (uint8_t[]){0x04}, 1, 0},
    {0x3E, (uint8_t[]){0x04}, 1, 0},
    {0x3F, (uint8_t[]){0x06}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},
    {0x42, (uint8_t[]){0x02}, 1, 0},
    {0x43, (uint8_t[]){0x02}, 1, 0},
    {0x44, (uint8_t[]){0x00}, 1, 0},
    {0x45, (uint8_t[]){0x00}, 1, 0},
    {0x46, (uint8_t[]){0x1F}, 1, 0},
    {0x47, (uint8_t[]){0x1F}, 1, 0},
    {0x48, (uint8_t[]){0x1E}, 1, 0},
    {0x49, (uint8_t[]){0x1E}, 1, 0},
    {0x4A, (uint8_t[]){0x1F}, 1, 0},
    {0x4B, (uint8_t[]){0x17}, 1, 0},
    {0x4C, (uint8_t[]){0x17}, 1, 0},
    {0x4D, (uint8_t[]){0x37}, 1, 0},
    {0x4E, (uint8_t[]){0x37}, 1, 0},
    {0x4F, (uint8_t[]){0x09}, 1, 0},
    {0x50, (uint8_t[]){0x09}, 1, 0},
    {0x51, (uint8_t[]){0x0B}, 1, 0},
    {0x52, (uint8_t[]){0x0B}, 1, 0},
    {0x53, (uint8_t[]){0x05}, 1, 0},
    {0x54, (uint8_t[]){0x05}, 1, 0},
    {0x55, (uint8_t[]){0x07}, 1, 0},
    {0x56, (uint8_t[]){0x07}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},
    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x5B, (uint8_t[]){0x30}, 1, 0},
    {0x5C, (uint8_t[]){0x00}, 1, 0},
    {0x5D, (uint8_t[]){0x34}, 1, 0},
    {0x5E, (uint8_t[]){0x05}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x63, (uint8_t[]){0x00}, 1, 0},
    {0x64, (uint8_t[]){0x6A}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x07}, 1, 0},
    {0x69, (uint8_t[]){0x08}, 1, 0},
    {0x6A, (uint8_t[]){0x6A}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},
    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x00}, 1, 0},
    {0x6E, (uint8_t[]){0x00}, 1, 0},
    {0x6F, (uint8_t[]){0x88}, 1, 0},
    {0x75, (uint8_t[]){0xFF}, 1, 0},
    {0x77, (uint8_t[]){0xDD}, 1, 0},
    {0x78, (uint8_t[]){0x2C}, 1, 0},
    {0x79, (uint8_t[]){0x15}, 1, 0},
    {0x7A, (uint8_t[]){0x17}, 1, 0},
    {0x7D, (uint8_t[]){0x14}, 1, 0},
    {0x7E, (uint8_t[]){0x82}, 1, 0},
    {0xE0, (uint8_t[]){0x04}, 1, 0},
    {0x00, (uint8_t[]){0x0E}, 1, 0},
    {0x02, (uint8_t[]){0xB3}, 1, 0},
    {0x09, (uint8_t[]){0x60}, 1, 0},
    {0x0E, (uint8_t[]){0x48}, 1, 0},
    {0x37, (uint8_t[]){0x58}, 1, 0},
    {0x2B, (uint8_t[]){0x0F}, 1, 0},
    {0xE0, (uint8_t[]){0x05}, 1, 0},
    {0x15, (uint8_t[]){0x1D}, 1, 0},
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE6, (uint8_t[]){0x02}, 1, 0},
    {0xE7, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};
#else
/* "V1" / original panel revision (rear case date code up to ~2627).
 * Sourced from the Guition reference Arduino project (see file header) and
 * independently cross-validated against another third-party open-source
 * project targeting this exact model. This is the default, unconditional
 * path used by all builds that do not enable CONFIG_APP_PANELJC_PANEL_V2.
 *
 * This register sequence matches espressif/esp_lcd_jd9365's own built-in
 * vendor_specific_init_default[] (esp_lcd_jd9365.c, Copyright 2024
 * Espressif Systems (Shanghai) CO LTD, SPDX-License-Identifier: Apache-2.0)
 * -- noted here per Apache-2.0 SS4's attribution-notice requirement, since
 * this project already depends on that component (see
 * main/idf_component.paneljc.yml) but reproduces its default table
 * directly rather than calling it. */
static const jd9365_lcd_init_cmd_t paneljc_jd9365_init_cmds[] = {
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x39}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x41}, 1, 0},
    {0x0C, (uint8_t[]){0x74}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xD7}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xD7}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},
    {0x24, (uint8_t[]){0xFE}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},
    {0x37, (uint8_t[]){0x69}, 1, 0},
    {0x38, (uint8_t[]){0x05}, 1, 0},
    {0x39, (uint8_t[]){0x06}, 1, 0},
    {0x3A, (uint8_t[]){0x08}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0xFF}, 1, 0},
    {0x40, (uint8_t[]){0x06}, 1, 0},
    {0x41, (uint8_t[]){0xA0}, 1, 0},
    {0x43, (uint8_t[]){0x14}, 1, 0},
    {0x44, (uint8_t[]){0x0B}, 1, 0},
    {0x45, (uint8_t[]){0x30}, 1, 0},
    {0x4B, (uint8_t[]){0x04}, 1, 0},
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x89}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x28}, 1, 0},
    {0x5B, (uint8_t[]){0x15}, 1, 0},
    {0x5D, (uint8_t[]){0x50}, 1, 0},
    {0x5E, (uint8_t[]){0x37}, 1, 0},
    {0x5F, (uint8_t[]){0x29}, 1, 0},
    {0x60, (uint8_t[]){0x1E}, 1, 0},
    {0x61, (uint8_t[]){0x1D}, 1, 0},
    {0x62, (uint8_t[]){0x12}, 1, 0},
    {0x63, (uint8_t[]){0x1A}, 1, 0},
    {0x64, (uint8_t[]){0x08}, 1, 0},
    {0x65, (uint8_t[]){0x25}, 1, 0},
    {0x66, (uint8_t[]){0x26}, 1, 0},
    {0x67, (uint8_t[]){0x28}, 1, 0},
    {0x68, (uint8_t[]){0x49}, 1, 0},
    {0x69, (uint8_t[]){0x3A}, 1, 0},
    {0x6A, (uint8_t[]){0x43}, 1, 0},
    {0x6B, (uint8_t[]){0x3A}, 1, 0},
    {0x6C, (uint8_t[]){0x3B}, 1, 0},
    {0x6D, (uint8_t[]){0x32}, 1, 0},
    {0x6E, (uint8_t[]){0x1F}, 1, 0},
    {0x6F, (uint8_t[]){0x0E}, 1, 0},
    {0x70, (uint8_t[]){0x50}, 1, 0},
    {0x71, (uint8_t[]){0x37}, 1, 0},
    {0x72, (uint8_t[]){0x29}, 1, 0},
    {0x73, (uint8_t[]){0x1E}, 1, 0},
    {0x74, (uint8_t[]){0x1D}, 1, 0},
    {0x75, (uint8_t[]){0x12}, 1, 0},
    {0x76, (uint8_t[]){0x1A}, 1, 0},
    {0x77, (uint8_t[]){0x08}, 1, 0},
    {0x78, (uint8_t[]){0x25}, 1, 0},
    {0x79, (uint8_t[]){0x26}, 1, 0},
    {0x7A, (uint8_t[]){0x28}, 1, 0},
    {0x7B, (uint8_t[]){0x49}, 1, 0},
    {0x7C, (uint8_t[]){0x3A}, 1, 0},
    {0x7D, (uint8_t[]){0x43}, 1, 0},
    {0x7E, (uint8_t[]){0x3A}, 1, 0},
    {0x7F, (uint8_t[]){0x3B}, 1, 0},
    {0x80, (uint8_t[]){0x32}, 1, 0},
    {0x81, (uint8_t[]){0x1F}, 1, 0},
    {0x82, (uint8_t[]){0x0E}, 1, 0},
    {0xE0, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x1F}, 1, 0},
    {0x01, (uint8_t[]){0x1F}, 1, 0},
    {0x02, (uint8_t[]){0x52}, 1, 0},
    {0x03, (uint8_t[]){0x51}, 1, 0},
    {0x04, (uint8_t[]){0x50}, 1, 0},
    {0x05, (uint8_t[]){0x4B}, 1, 0},
    {0x06, (uint8_t[]){0x4A}, 1, 0},
    {0x07, (uint8_t[]){0x49}, 1, 0},
    {0x08, (uint8_t[]){0x48}, 1, 0},
    {0x09, (uint8_t[]){0x47}, 1, 0},
    {0x0A, (uint8_t[]){0x46}, 1, 0},
    {0x0B, (uint8_t[]){0x45}, 1, 0},
    {0x0C, (uint8_t[]){0x44}, 1, 0},
    {0x0D, (uint8_t[]){0x40}, 1, 0},
    {0x0E, (uint8_t[]){0x41}, 1, 0},
    {0x0F, (uint8_t[]){0x1F}, 1, 0},
    {0x10, (uint8_t[]){0x1F}, 1, 0},
    {0x11, (uint8_t[]){0x1F}, 1, 0},
    {0x12, (uint8_t[]){0x1F}, 1, 0},
    {0x13, (uint8_t[]){0x1F}, 1, 0},
    {0x14, (uint8_t[]){0x1F}, 1, 0},
    {0x15, (uint8_t[]){0x1F}, 1, 0},
    {0x16, (uint8_t[]){0x1F}, 1, 0},
    {0x17, (uint8_t[]){0x1F}, 1, 0},
    {0x18, (uint8_t[]){0x52}, 1, 0},
    {0x19, (uint8_t[]){0x51}, 1, 0},
    {0x1A, (uint8_t[]){0x50}, 1, 0},
    {0x1B, (uint8_t[]){0x4B}, 1, 0},
    {0x1C, (uint8_t[]){0x4A}, 1, 0},
    {0x1D, (uint8_t[]){0x49}, 1, 0},
    {0x1E, (uint8_t[]){0x48}, 1, 0},
    {0x1F, (uint8_t[]){0x47}, 1, 0},
    {0x20, (uint8_t[]){0x46}, 1, 0},
    {0x21, (uint8_t[]){0x45}, 1, 0},
    {0x22, (uint8_t[]){0x44}, 1, 0},
    {0x23, (uint8_t[]){0x40}, 1, 0},
    {0x24, (uint8_t[]){0x41}, 1, 0},
    {0x25, (uint8_t[]){0x1F}, 1, 0},
    {0x26, (uint8_t[]){0x1F}, 1, 0},
    {0x27, (uint8_t[]){0x1F}, 1, 0},
    {0x28, (uint8_t[]){0x1F}, 1, 0},
    {0x29, (uint8_t[]){0x1F}, 1, 0},
    {0x2A, (uint8_t[]){0x1F}, 1, 0},
    {0x2B, (uint8_t[]){0x1F}, 1, 0},
    {0x2C, (uint8_t[]){0x1F}, 1, 0},
    {0x2D, (uint8_t[]){0x1F}, 1, 0},
    {0x2E, (uint8_t[]){0x52}, 1, 0},
    {0x2F, (uint8_t[]){0x40}, 1, 0},
    {0x30, (uint8_t[]){0x41}, 1, 0},
    {0x31, (uint8_t[]){0x48}, 1, 0},
    {0x32, (uint8_t[]){0x49}, 1, 0},
    {0x33, (uint8_t[]){0x4A}, 1, 0},
    {0x34, (uint8_t[]){0x4B}, 1, 0},
    {0x35, (uint8_t[]){0x44}, 1, 0},
    {0x36, (uint8_t[]){0x45}, 1, 0},
    {0x37, (uint8_t[]){0x46}, 1, 0},
    {0x38, (uint8_t[]){0x47}, 1, 0},
    {0x39, (uint8_t[]){0x51}, 1, 0},
    {0x3A, (uint8_t[]){0x50}, 1, 0},
    {0x3B, (uint8_t[]){0x1F}, 1, 0},
    {0x3C, (uint8_t[]){0x1F}, 1, 0},
    {0x3D, (uint8_t[]){0x1F}, 1, 0},
    {0x3E, (uint8_t[]){0x1F}, 1, 0},
    {0x3F, (uint8_t[]){0x1F}, 1, 0},
    {0x40, (uint8_t[]){0x1F}, 1, 0},
    {0x41, (uint8_t[]){0x1F}, 1, 0},
    {0x42, (uint8_t[]){0x1F}, 1, 0},
    {0x43, (uint8_t[]){0x1F}, 1, 0},
    {0x44, (uint8_t[]){0x52}, 1, 0},
    {0x45, (uint8_t[]){0x40}, 1, 0},
    {0x46, (uint8_t[]){0x41}, 1, 0},
    {0x47, (uint8_t[]){0x48}, 1, 0},
    {0x48, (uint8_t[]){0x49}, 1, 0},
    {0x49, (uint8_t[]){0x4A}, 1, 0},
    {0x4A, (uint8_t[]){0x4B}, 1, 0},
    {0x4B, (uint8_t[]){0x44}, 1, 0},
    {0x4C, (uint8_t[]){0x45}, 1, 0},
    {0x4D, (uint8_t[]){0x46}, 1, 0},
    {0x4E, (uint8_t[]){0x47}, 1, 0},
    {0x4F, (uint8_t[]){0x51}, 1, 0},
    {0x50, (uint8_t[]){0x50}, 1, 0},
    {0x51, (uint8_t[]){0x1F}, 1, 0},
    {0x52, (uint8_t[]){0x1F}, 1, 0},
    {0x53, (uint8_t[]){0x1F}, 1, 0},
    {0x54, (uint8_t[]){0x1F}, 1, 0},
    {0x55, (uint8_t[]){0x1F}, 1, 0},
    {0x56, (uint8_t[]){0x1F}, 1, 0},
    {0x57, (uint8_t[]){0x1F}, 1, 0},
    {0x58, (uint8_t[]){0x40}, 1, 0},
    {0x59, (uint8_t[]){0x00}, 1, 0},
    {0x5A, (uint8_t[]){0x00}, 1, 0},
    {0x5B, (uint8_t[]){0x10}, 1, 0},
    {0x5C, (uint8_t[]){0x05}, 1, 0},
    {0x5D, (uint8_t[]){0x50}, 1, 0},
    {0x5E, (uint8_t[]){0x01}, 1, 0},
    {0x5F, (uint8_t[]){0x02}, 1, 0},
    {0x60, (uint8_t[]){0x50}, 1, 0},
    {0x61, (uint8_t[]){0x06}, 1, 0},
    {0x62, (uint8_t[]){0x04}, 1, 0},
    {0x63, (uint8_t[]){0x03}, 1, 0},
    {0x64, (uint8_t[]){0x64}, 1, 0},
    {0x65, (uint8_t[]){0x65}, 1, 0},
    {0x66, (uint8_t[]){0x0B}, 1, 0},
    {0x67, (uint8_t[]){0x73}, 1, 0},
    {0x68, (uint8_t[]){0x07}, 1, 0},
    {0x69, (uint8_t[]){0x06}, 1, 0},
    {0x6A, (uint8_t[]){0x64}, 1, 0},
    {0x6B, (uint8_t[]){0x08}, 1, 0},
    {0x6C, (uint8_t[]){0x00}, 1, 0},
    {0x6D, (uint8_t[]){0x32}, 1, 0},
    {0x6E, (uint8_t[]){0x08}, 1, 0},
    {0xE0, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x6B}, 1, 0},
    {0x35, (uint8_t[]){0x08}, 1, 0},
    {0x37, (uint8_t[]){0x00}, 1, 0},
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 120},
};
#endif /* CONFIG_APP_PANELJC_PANEL_V2 */

static esp_err_t panel_create(void)
{
    /* 1. DSI bus: JC8012P4A1 ESPHome profile uses 2 lanes at 1 Gbps/lane. */
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    bus_cfg.lane_bit_rate_mbps = PANELJC_DSI_LANE_BITRATE_MBPS;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus),
        TAG_DISPLAY, "DSI bus init failed");

    /* 2. DBI command IO (sends init register writes to the JD9365) */
    esp_lcd_dbi_io_config_t dbi_cfg = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &s_dbi_io),
        TAG_DISPLAY, "DBI panel IO init failed");

    /* 3. JD9365 DPI panel: JC8012P4A1-specific timing and init sequence. */
    const esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANELJC_DPI_CLK_MHZ,
        .virtual_channel = 0,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
#else
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
#endif
        .num_fbs = 1,
        .video_timing = {
            .h_size = PANELJC_PANEL_H_RES,
            .v_size = PANELJC_PANEL_V_RES,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = PANELJC_VSYNC_BACK_PORCH,
            .vsync_pulse_width = 4,
            .vsync_front_porch = PANELJC_VSYNC_FRONT_PORCH,
        },
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        .flags.use_dma2d = true,
#endif
    };

    const jd9365_vendor_config_t vendor_cfg = {
        .init_cmds = paneljc_jd9365_init_cmds,
        .init_cmds_size = sizeof(paneljc_jd9365_init_cmds) / sizeof(paneljc_jd9365_init_cmds[0]),
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = 2,
        },
    };
    const esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .reset_gpio_num = PANELJC_LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = (void *)&vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(s_dbi_io, &panel_dev_cfg, &s_panel),
        TAG_DISPLAY, "JD9365 panel create failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG_DISPLAY, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG_DISPLAY, "panel init failed");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(s_panel), TAG_DISPLAY, "DMA2D enable failed");
#endif
    /* Mirror both axes — matches Guition reference example behaviour */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true),
        TAG_DISPLAY, "panel mirror failed");

    ESP_LOGI(TAG_DISPLAY, "JD9365 panel ready (%dx%d, pclk=%dMHz, DSI=%dMbps/lane)",
        PANELJC_PANEL_H_RES, PANELJC_PANEL_V_RES,
        PANELJC_DPI_CLK_MHZ, PANELJC_DSI_LANE_BITRATE_MBPS);
    return ESP_OK;
}

/* ── LVGL display ────────────────────────────────────────────────────────── */
static esp_err_t lvgl_display_add(void)
{
    s_lv_display = display_port_add_dsi(s_panel, s_dbi_io,
                                        PANELJC_PANEL_H_RES, PANELJC_PANEL_V_RES,
                                        true, false);
    if (!s_lv_display) return ESP_FAIL;

    /* Rotate to landscape with the USB connector / board orientation at the top. */
    if (display_port_lock(2000)) {
        lv_display_set_antialiasing(s_lv_display, APP_LVGL_ANTIALIASING != 0);
        lv_display_set_rotation(s_lv_display, LV_DISPLAY_ROTATION_90);
        display_port_unlock();
    } else {
        ESP_LOGE(TAG_DISPLAY, "LVGL lock timeout during rotation setup");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */
esp_err_t display_init(void)
{
    if (s_display_ready) return ESP_OK;

    /* 1. LEDC backlight (stays off until boot splash is ready) */
    ESP_RETURN_ON_ERROR(backlight_ledc_init(), TAG_DISPLAY, "backlight LEDC init failed");

    /* 2. MIPI DSI PHY LDO */
    ESP_RETURN_ON_ERROR(mipi_dsi_phy_power_on(), TAG_DISPLAY, "MIPI PHY power-on failed");

    /* 3. LVGL port task */
    ESP_RETURN_ON_ERROR(display_port_init(), TAG_DISPLAY, "display_port_init failed");

    /* 4. JD9365 panel init */
    ESP_RETURN_ON_ERROR(panel_create(), TAG_DISPLAY, "panel_create failed");

    /* 5. Register with LVGL */
    ESP_RETURN_ON_ERROR(lvgl_display_add(), TAG_DISPLAY, "lvgl_display_add failed");

    /* 6. Dim timer (non-fatal) */
    (void)display_brightness_start();

    ESP_RETURN_ON_ERROR(display_port_start(), TAG_DISPLAY, "Adapter task start failed");
    s_display_ready = true;
    ESP_LOGI(TAG_DISPLAY,
        "Display init OK — JD9365 MIPI-DSI %dx%d portrait → LVGL 1280×800 landscape (rot=90), "
        "LEDC backlight GPIO%d",
        PANELJC_PANEL_H_RES, PANELJC_PANEL_V_RES, (int)PANELJC_BL_GPIO);

    display_note_activity();
    return ESP_OK;
}

bool display_is_ready(void)            { return s_display_ready; }
bool display_lock(uint32_t timeout_ms) { return display_port_lock(timeout_ms); }
void display_unlock(void)              { display_port_unlock(); }

/* ESP-BSP API shim: api_settings.c / api_ota.c call this before destructive actions */
esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG_DISPLAY, "bsp_display_backlight_off");
    return display_set_brightness_percent(0);
}
