/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Display init for Guition JC8012P4A1 (ESP32-P4 + JD9365 MIPI-DSI 800×1280).
 *
 * Hardware:
 *   Panel:     JD9365, 800×1280 native portrait, 2-lane MIPI-DSI
 *   Backlight: LEDC GPIO23 (active-high, 10-bit PWM @ 1 kHz)
 *   LCD Reset: GPIO27
 *   MIPI LDO:  channel 3 → VDD_MIPI_DPHY @ 2500 mV
 *
 * Native orientation is portrait (800×1280).  LVGL rotation 270° gives
 * landscape 1280×800, matching the panel10 content-box dimensions.
 *
 * Touch (GSL3680, I2C SDA=7 SCL=8 RST=22 INT=21): see touch_init_paneljc.c
 *
 * Pin map from Guition reference Arduino example (pins_config.h):
 *   https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6
 */
#include "drivers/display_init.h"

#include <stdbool.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
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
#define PANELJC_DSI_LANE_BITRATE_MBPS 1000
#define PANELJC_DPI_CLK_MHZ          60

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
static esp_timer_handle_t      s_dim_timer     = NULL;
static int                     s_display_brightness = -1;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static lvgl_port_cfg_t display_port_cfg(void)
{
    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_priority     = 20;
    cfg.task_stack        = APP_LVGL_TASK_STACK;
    cfg.task_affinity     = 1;
    cfg.task_max_sleep_ms = 100;
    return cfg;
}

static int display_clamp_brightness(int percent)
{
    if (percent < 0)   return 0;
    if (percent > 100) return 100;
    return percent;
}

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

esp_err_t display_set_brightness_percent(int percent)
{
    const int next = display_clamp_brightness(percent);
    if (s_display_brightness == next) return ESP_OK;

    const uint32_t duty = (uint32_t)(next * ((1u << 10) - 1)) / 100u;
    esp_err_t err = ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    if (err == ESP_OK) err = ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
    if (err == ESP_OK) {
        s_display_brightness = next;
        ESP_LOGI(TAG_DISPLAY, "backlight set %d%% (LEDC duty %"PRIu32"/1023 on GPIO%d)",
            next, duty, (int)PANELJC_BL_GPIO);
    } else {
        ESP_LOGW(TAG_DISPLAY, "backlight set %d%% failed: %s", next, esp_err_to_name(err));
    }
    return err;
}

/* ── Dim timer ───────────────────────────────────────────────────────────── */
static void display_dim_timer_cb(void *arg)
{
    (void)arg;
    if (!s_display_ready) return;
    (void)display_set_brightness_percent(APP_DISPLAY_DIM_BRIGHTNESS_PERCENT);
}

static esp_err_t display_dim_timer_init(void)
{
    if (s_dim_timer != NULL) return ESP_OK;
    const esp_timer_create_args_t args = {
        .callback              = display_dim_timer_cb,
        .arg                   = NULL,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "display_dim",
        .skip_unhandled_events = true,
    };
    return esp_timer_create(&args, &s_dim_timer);
}

static void display_restart_dim_timer(void)
{
    if (s_dim_timer == NULL) return;
    if (esp_timer_is_active(s_dim_timer)) (void)esp_timer_stop(s_dim_timer);
    const uint64_t us = (uint64_t)APP_DISPLAY_DIM_TIMEOUT_MS * 1000ULL;
    (void)esp_timer_start_once(s_dim_timer, us);
}

void display_note_activity(void)
{
    if (!s_display_ready) return;
    (void)display_set_brightness_percent(APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT);
    display_restart_dim_timer();
}

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
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = PANELJC_PANEL_H_RES,
            .v_size = PANELJC_PANEL_V_RES,
            .hsync_back_porch = 20,
            .hsync_pulse_width = 20,
            .hsync_front_porch = 40,
            .vsync_back_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_front_porch = 20,
        },
        .flags.use_dma2d = true,
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
    /* hres/vres are the native panel dimensions (portrait 800×1280).
     * lv_display_set_rotation(ROTATION_270) below rotates LVGL's logical
     * canvas to 1280×800 landscape, matching APP_SCREEN_WIDTH/HEIGHT. */
    const uint32_t buf_pixels = PANELJC_PANEL_H_RES * PANELJC_PANEL_V_RES;
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle      = s_dbi_io,
        .panel_handle   = s_panel,
        .control_handle = NULL,
        .buffer_size    = buf_pixels / 5U,
        .double_buffer  = true,
        .hres           = PANELJC_PANEL_H_RES,
        .vres           = PANELJC_PANEL_V_RES,
        .monochrome     = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
#if LV_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_dma    = true,
            .buff_spiram = true,
            .sw_rotate   = true,
#if LV_VERSION_MAJOR >= 9
            .swap_bytes  = false,
#endif
            .full_refresh = false,
            .direct_mode  = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,
        },
    };

    /* Try progressively smaller draw buffers if PSRAM is tight */
    static const uint8_t divisors[] = {5U, 8U, 10U, 12U};
    for (size_t i = 0; i < sizeof(divisors); ++i) {
        disp_cfg.buffer_size = buf_pixels / divisors[i];
        s_lv_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
        if (s_lv_display != NULL) {
            ESP_LOGI(TAG_DISPLAY,
                "LVGL DSI display added (buf=1/%u, %"PRIu32" px)",
                (unsigned)divisors[i], disp_cfg.buffer_size);
            break;
        }
        ESP_LOGW(TAG_DISPLAY,
            "lvgl_port_add_disp_dsi failed at buf=1/%u, retrying smaller",
            (unsigned)divisors[i]);
    }
    if (s_lv_display == NULL) {
        ESP_LOGE(TAG_DISPLAY, "lvgl_port_add_disp_dsi failed");
        return ESP_FAIL;
    }

    /* Rotate to landscape with the USB connector / board orientation at the bottom. */
    if (lvgl_port_lock(2000)) {
        lv_display_set_antialiasing(s_lv_display, APP_LVGL_ANTIALIASING != 0);
        lv_display_set_rotation(s_lv_display, LV_DISPLAY_ROTATION_270);
        lvgl_port_unlock();
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
    lvgl_port_cfg_t lvgl_cfg = display_port_cfg();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG_DISPLAY, "lvgl_port_init failed");

    /* 4. JD9365 panel init */
    ESP_RETURN_ON_ERROR(panel_create(), TAG_DISPLAY, "panel_create failed");

    /* 5. Register with LVGL */
    ESP_RETURN_ON_ERROR(lvgl_display_add(), TAG_DISPLAY, "lvgl_display_add failed");

    /* 6. Dim timer (non-fatal) */
    (void)display_dim_timer_init();

    s_display_ready = true;
    ESP_LOGI(TAG_DISPLAY,
        "Display init OK — JD9365 MIPI-DSI %dx%d portrait → LVGL 1280×800 landscape (rot=270), "
        "LEDC backlight GPIO%d",
        PANELJC_PANEL_H_RES, PANELJC_PANEL_V_RES, (int)PANELJC_BL_GPIO);

    display_note_activity();
    return ESP_OK;
}

bool display_is_ready(void)            { return s_display_ready; }
bool display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void display_unlock(void)              { lvgl_port_unlock(); }

/* ESP-BSP API shim: api_settings.c / api_ota.c call this before destructive actions */
esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG_DISPLAY, "bsp_display_backlight_off");
    return display_set_brightness_percent(0);
}
