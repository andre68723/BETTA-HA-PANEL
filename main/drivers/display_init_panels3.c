/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Display init â€” Guition CYD ESP32-S3 4848S040 (RGB ST7701S).
 *
 * Uses espressif/esp_lcd_st7701 (v2.0.2) for the full vendor register
 * init sequence and esp_lcd_panel_io_additions for 3-wire SPI GPIO.
 *
 * Hardware:
 *   Panel:     ST7701S, 480Ã—480, 3-wire SPI config + 16-bit parallel RGB
 *   Backlight: LEDC GPIO38, 150 Hz, 10-bit PWM
 *
 * 3-wire SPI (GPIO bit-bang, no hardware SPI):
 *   CLK=48, MOSI=47, CS=39 â€” no overlap with RGB data pins
 *   enable_io_multiplex=1 â†’ SPI pins released after init sequence * SPI mode: MODE3 (CPOL=1, CPHA=1) — verified via ESPHome Guition 4848S040 config
 * PCLK polarity: active HIGH (pclk_active_neg=0) — ESPHome: pclk_inverted: False *
 * RGB pin map (data_gpio_nums D0â€¦D15 per esp_lcd_rgb_panel_config_t):
 *   D[ 0.. 4] = B[1..5] = { 4,  5,  6,  7, 15}
 *   D[ 5..10] = G[0..5] = { 8, 20,  3, 46,  9, 10}
 *   D[11..15] = R[1..5] = {11, 12, 13, 14,  0}
 *
 * Sync timing (PCLK=16 MHz, manufacturer Guition demo confirmed-working):
 *   HSYNC: pulse=8  front=10  back=50
 *   VSYNC: pulse=8  front=10  back=20
 *   Source: tools/4.0inch_ESP32-4848S040/.../1_2_4.0_LvglWidgets.ino
 *           and 1_1_86switch_onoff.ino (Arduino_GFX ST7701_RGBPanel ctor)
 *
 * Touch (GT911, I2C SDA=19 SCL=45): see touch_init_panels3.c
 */
#include "drivers/display_init.h"

#include <stdbool.h>
#include <inttypes.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"    /* esp_lcd_panel_io_additions */
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"               /* espressif/esp_lcd_st7701   */
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "util/log_tags.h"

/* â”€â”€ Pin definitions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
#define PANELS3_BL_GPIO      38

/* 3-wire SPI for ST7701S register init */
#define PANELS3_SPI_CLK      48
#define PANELS3_SPI_MOSI     47
#define PANELS3_SPI_CS       39

/* RGB parallel interface */
#define PANELS3_PCLK_GPIO    21
#define PANELS3_DE_GPIO      18
#define PANELS3_HSYNC_GPIO   16
#define PANELS3_VSYNC_GPIO   17

/* D0â€¦D15: [B1,B2,B3,B4,B5, G0,G1,G2,G3,G4,G5, R1,R2,R3,R4,R5] */
#define PANELS3_DATA_GPIOS   { 4, 5, 6, 7, 15,  8, 20, 3, 46, 9, 10,  11, 12, 13, 14, 0 }

/* â”€â”€ LEDC backlight â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
#define BL_LEDC_TIMER        LEDC_TIMER_0
#define BL_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL      LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES     LEDC_TIMER_10_BIT
#define BL_LEDC_FREQUENCY_HZ 150U

/* â”€â”€ RGB panel timing (Guition manufacturer demo confirmed) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
/* PCLK lowered to 12 MHz: the manufacturer demo uses 16 MHz but runs
 * Arduino_GFX without a bounce buffer (single-FB, partial updates).
 * Our setup has the FB in PSRAM with a 10-line bounce buffer + 64-B
 * GDMA bursts (PSRAM HW limit), so 16 MHz starves the buffer and shows
 * as stripe artifacts. 10 MHz did not improve the panel's bright-to-dark
 * bleed and reduces refresh rate too much, so 12 MHz is the safer compromise. */
#define PANELS3_PCLK_HZ      (12 * 1000 * 1000)
#define PANELS3_H_PULSE      8
#define PANELS3_H_FRONT      10
#define PANELS3_H_BACK       50                    /* manufacturer: hsync_back_porch=50 */
#define PANELS3_V_PULSE      8
#define PANELS3_V_FRONT      10
#define PANELS3_V_BACK       20                    /* manufacturer: vsync_back_porch=20 */

/* ST7701S analog/line-drive tune.
 *
 * Arduino_GFX type1 renders correct colors but shows faint bright-to-dark
 * vertical bleed on this panel. Global INVON (0x21) removes the bleed, but
 * logically inverts all colors. The HSD vendor init for this glass specifies
 * a 2-dot inversion drive tune via PAGE0 C2 plus PAGE1 B1/B2; try those
 * non-logical drive values while keeping COLMOD/MADCTL on the working path. */
#define PANELS3_USE_HSD_DRIVE_TUNE 0
#if PANELS3_USE_HSD_DRIVE_TUNE
#define PANELS3_ST7701_C2_0 0x21
#define PANELS3_ST7701_C2_1 0x08
#define PANELS3_ST7701_B1   0x30
#define PANELS3_ST7701_B2   0x87
#else
#define PANELS3_ST7701_C2_0 0x31
#define PANELS3_ST7701_C2_1 0x05
#define PANELS3_ST7701_B1   0x32
#define PANELS3_ST7701_B2   0x07
#endif

/* Bounce buffer lines (internal SRAM, managed by RGB driver).
 * 10 lines × 480 px × 2 B = 9.6 KiB. Larger values (20) starve the
 * remaining internal SRAM and made FreeRTOS task creation fail later
 * (ui_runtime task could not allocate its stack). */
#define PANELS3_BOUNCE_LINES   10
/* LVGL draw buffer height in lines (2Ã— PSRAM double-buffer) */
#define PANELS3_LVGL_BUF_LINES 20

/* â”€â”€ State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static bool                   s_display_ready = false;
static lv_display_t          *s_lv_display    = NULL;
static esp_lcd_panel_handle_t s_panel         = NULL;
static esp_timer_handle_t     s_dim_timer      = NULL;
static int                    s_display_brightness = -1;
static int                    s_active_brightness_percent = APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT;
static int                    s_dim_brightness_percent = APP_DISPLAY_DIM_BRIGHTNESS_PERCENT;
static uint32_t               s_dim_timeout_ms = APP_DISPLAY_DIM_TIMEOUT_MS;

static void display_restart_dim_timer(void);

/* â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static lvgl_port_cfg_t display_port_cfg(void)
{
    lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_priority    = 20;
    cfg.task_stack       = APP_LVGL_TASK_STACK;
    cfg.task_affinity    = 1;
    cfg.task_max_sleep_ms = 100;
    cfg.task_stack_caps  = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    return cfg;
}

static int display_clamp_brightness(int percent)
{
    if (percent < 0)   return 0;
    if (percent > 100) return 100;
    return percent;
}

static int display_clamp_config_brightness(int percent)
{
    if (percent < 10) return 10;
    if (percent > 100) return 100;
    return percent;
}

/* â”€â”€ Backlight LEDC â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static esp_err_t backlight_ledc_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQUENCY_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG_DISPLAY, "LEDC timer config failed");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = PANELS3_BL_GPIO,
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
        ESP_LOGD(TAG_DISPLAY, "backlight %d%% (duty=%" PRIu32 ")", next, duty);
    } else {
        ESP_LOGW(TAG_DISPLAY, "backlight set failed: %s", esp_err_to_name(err));
    }
    return err;
}

void display_configure_brightness(int active_brightness_percent, int dim_brightness_percent, int dim_timeout_seconds)
{
    s_active_brightness_percent = display_clamp_config_brightness(active_brightness_percent);
    s_dim_brightness_percent = display_clamp_config_brightness(dim_brightness_percent);
    if (dim_timeout_seconds < 5) dim_timeout_seconds = 5;
    if (dim_timeout_seconds > 3600) dim_timeout_seconds = 3600;
    s_dim_timeout_ms = (uint32_t)dim_timeout_seconds * 1000U;
    if (s_display_ready) {
        (void)display_set_brightness_percent(s_active_brightness_percent);
        display_restart_dim_timer();
    }
}

/* â”€â”€ Dim timer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static void display_dim_timer_cb(void *arg)
{
    (void)arg;
    if (!s_display_ready) return;
    (void)display_set_brightness_percent(s_dim_brightness_percent);
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
    const uint64_t us = (uint64_t)s_dim_timeout_ms * 1000ULL;
    (void)esp_timer_start_once(s_dim_timer, us);
}

void display_note_activity(void)
{
    if (!s_display_ready) return;
    (void)display_set_brightness_percent(s_active_brightness_percent);
    display_restart_dim_timer();
}

/* â”€â”€ ST7701S panel init (3-wire SPI + RGB) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ *
 * Uses espressif/esp_lcd_st7701 component:
 *   - esp_lcd_new_panel_io_3wire_spi()  â†’  bit-bang SPI panel IO
 *   - esp_lcd_new_panel_st7701()        â†’  full vendor init + RGB panel
 *
 * enable_io_multiplex=1: SPI GPIO pins are freed after the init sequence
 * completes; the RGB interface takes over all data GPIOs.
 * The rgb_config is embedded in vendor_config so the component creates
 * the RGB panel internally â€” s_panel IS the RGB panel handle on return.
 */
/*
 * Board-specific ST7701S init sequence for Guition CYD ESP32-S3 4848S040.
 *
 * Mostly follows `st7701_type1_init_operations[]` from the manufacturer's
 * Arduino_GFX library (the lib their working demo uses):
 *   tools/4.0inch_ESP32-4848S040/.../Libraries/Arduino_GFX-master/src/
 *     display/Arduino_ST7701_RGBPanel.h  (lines 14..114)
 *
 * NOTE: The HSD '验证OK' txt file in the same vendor pkg has different
 * C2/B1/B2 values for the same 480x480 HSD glass. We keep the working
 * Guition/Arduino color path, but can borrow those three analog drive values
 * via PANELS3_USE_HSD_DRIVE_TUNE when the glass shows bright-to-dark bleed.
 *
 * COLMOD: We force COLMOD=0x60 (RGB666) at the end like Arduino_GFX does,
 * even though our peripheral feeds 16 bit/pixel — the C2 LINE_SET in this
 * sequence is tuned for the panel's RGB666 internal pixel mode. The ESP
 * peripheral keeps streaming 16 bits per pclk; the panel samples the upper
 * 6 bits of each color component into its 6+6+6 latch. The component pre-
 * sends 0x3A=0x50, but our 0x3A=0x60 below overrides it (last write wins;
 * the component logs a harmless warning).
 *
 * MADCTL: rgb_ele_order=RGB → component sends 0x36=0x00 → BGR bit clear,
 * matches Arduino_GFX's `_bgr=true` path which sends MADCTL=0x00 (it
 * compensates the BGR-wired glass by clearing the MADCTL.BGR bit — yes,
 * counter-intuitive naming).
 */
static const st7701_lcd_init_cmd_t s_guition_4848_init[] = {
    /* PAGE0 — display timing, interface, gamma */
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t []){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t []){PANELS3_ST7701_C2_0, PANELS3_ST7701_C2_1}, 2, 0},   /* line/inversion drive tune */
    {0xCD, (uint8_t []){0x00}, 1, 0},
    /* Positive Voltage Gamma Control */
    {0xB0, (uint8_t []){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
                         0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    /* Negative Voltage Gamma Control */
    {0xB1, (uint8_t []){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
                         0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},

    /* PAGE1 — power / charge-pump (Arduino_GFX type1 values) */
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x60}, 1, 0},   /* Vop = 4.7375 V */
    {0xB1, (uint8_t []){PANELS3_ST7701_B1}, 1, 0},   /* VCOM/drive tune */
    {0xB2, (uint8_t []){PANELS3_ST7701_B2}, 1, 0},   /* VGH/drive tune */
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x49}, 1, 0},   /* VGL = -10.17 V */
    {0xB7, (uint8_t []){0x85}, 1, 0},
    {0xB8, (uint8_t []){0x21}, 1, 0},   /* AVDD = 6.6, AVCL = -4.6 */
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},

    /* EQ / GIP timing */
    {0xE0, (uint8_t []){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00,
                         0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
                         0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
                         0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
                         0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t []){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t []){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
                         0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},

    /* PAGE3 — VAP & VAN (Arduino_GFX includes this; HSD txt did not) */
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE5, (uint8_t []){0xE4}, 1, 0},

    /* Back to user command page */
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

    /* Override COLMOD: the component already sent 0x50 (RGB565); switch to
     * 0x60 (RGB666 internal pixel mode) — required by the C2 LINE_SET above. */
    {0x3A, (uint8_t []){0x60}, 1, 0},

    /* Sleep Out + Display On (per Arduino_GFX timing) */
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

static esp_err_t panel_create(void)
{
    /* 3-wire SPI IO (GPIO bit-bang, no hardware SPI peripheral) */
    const spi_line_config_t line_config = {
        .cs_io_type  = IO_TYPE_GPIO,
        .cs_gpio_num = PANELS3_SPI_CS,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = PANELS3_SPI_CLK,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = PANELS3_SPI_MOSI,
        .io_expander = NULL,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_cfg =
        ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    io_cfg.spi_mode = 3;  /* ST7701S uses MODE3 (CPOL=1,CPHA=1) — ESPHome: spi_mode: MODE3 */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io_handle),
        TAG_DISPLAY, "3-wire SPI IO init failed");

    /* RGB parallel panel config (inside vendor_config) */
    const esp_lcd_rgb_panel_config_t rgb_cfg = {
        .clk_src             = LCD_CLK_SRC_DEFAULT,
        /* PSRAM access via GDMA is hardware-limited to ≤64 byte bursts on
         * ESP32-S3 (gdma_config_transfer rejects >64 for external memory). */
        .dma_burst_size      = 64,
        .data_width          = 16,
        .bits_per_pixel      = 16,
        .de_gpio_num         = PANELS3_DE_GPIO,
        .pclk_gpio_num       = PANELS3_PCLK_GPIO,
        .vsync_gpio_num      = PANELS3_VSYNC_GPIO,
        .hsync_gpio_num      = PANELS3_HSYNC_GPIO,
        .disp_gpio_num       = GPIO_NUM_NC,
        .data_gpio_nums      = PANELS3_DATA_GPIOS,
        .timings = {
            .pclk_hz           = PANELS3_PCLK_HZ,
            .h_res             = APP_SCREEN_WIDTH,
            .v_res             = APP_SCREEN_HEIGHT,
            .hsync_pulse_width = PANELS3_H_PULSE,
            .hsync_front_porch = PANELS3_H_FRONT,
            .hsync_back_porch  = PANELS3_H_BACK,
            .vsync_pulse_width = PANELS3_V_PULSE,
            .vsync_front_porch = PANELS3_V_FRONT,
            .vsync_back_porch  = PANELS3_V_BACK,
            .flags.pclk_active_neg = 0,  /* pclk_inverted: False per ESPHome Guition 4848S040 config */
        },
        /* Two framebuffers in PSRAM for tear-free direct rendering:
         * lvgl_port retrieves them via esp_lcd_rgb_panel_get_frame_buffer()
         * and uses them as LVGL draw buffers.
         *
         * Bounce buffer (internal SRAM) is REQUIRED when FB is in PSRAM:
         * the RGB DMA bursts pixels from PSRAM into the bounce buffer, then
         * streams from there to the panel — eliminates PSRAM-bandwidth
         * starvation that otherwise shows as a violet/green tinted overlay
         * with subtle horizontal stripes and a small vertical offset on
         * the first lines of each frame. 10 lines × 480 px × 2 B = 9.6 KiB. */
        .num_fbs               = 2,
        .bounce_buffer_size_px = APP_SCREEN_WIDTH * PANELS3_BOUNCE_LINES,
        .flags = {
            .fb_in_psram         = 1,
            .bb_invalidate_cache = 1,  /* invalidate PSRAM cache before DMA reads */
        },
    };

    /* ST7701S vendor config: board-specific init sequence for Guition 4848S040 */
    const st7701_vendor_config_t vendor_cfg = {
        .rgb_config      = &rgb_cfg,
        .init_cmds       = s_guition_4848_init,
        .init_cmds_size  = sizeof(s_guition_4848_init) / sizeof(st7701_lcd_init_cmd_t),
        /* enable_io_multiplex=1: release SPI GPIO after init, RGB takes over */
        .flags.enable_io_multiplex = 1,
    };

    const esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .reset_gpio_num  = GPIO_NUM_NC,          /* no dedicated reset pin */
        /* Pin wiring is B@LSB(D0..4), G(D5..10), R@MSB(D11..15) → bus is RGB565 byte
         * order. So MADCTL.BGR must be 0 (RGB). Setting BGR here would make the
         * panel swap R/B internally → cyan would render as yellow. */
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = 16,
        .vendor_config   = (void *)&vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7701(io_handle, &panel_dev_cfg, &s_panel),
        TAG_DISPLAY, "esp_lcd_new_panel_st7701 failed");

    /* reset: sends ST7701S init sequence via SPI (and releases SPI IO) */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG_DISPLAY, "panel reset failed");
    /* init: starts the RGB panel DMA engine */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG_DISPLAY, "panel init failed");
    /* NOTE: disp_on_off is NOT called here — with enable_io_multiplex=1 the SPI IO
     * is already deleted at this point and the call would fail with an error.
     * The ST7701S init sequence has already enabled the display output. */

    ESP_LOGI(TAG_DISPLAY, "ST7701S panel ready (%dx%d, PCLK=%dHz, 2x FB in PSRAM, avoid_tearing)",
        APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT, PANELS3_PCLK_HZ);
    return ESP_OK;
}

/* â”€â”€ LVGL display â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
static esp_err_t lvgl_display_add(void)
{
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle  = s_panel,
        /* avoid_tearing overrides buffer_size to hres*vres internally;
         * set it to the full frame so direct mode can use the RGB framebuffers. */
        .buffer_size   = (uint32_t)(APP_SCREEN_WIDTH * APP_SCREEN_HEIGHT),
        .double_buffer = false,
        .hres          = APP_SCREEN_WIDTH,
        .vres          = APP_SCREEN_HEIGHT,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
#if LV_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_spiram  = true,
            .sw_rotate    = false,
            /* Full-refresh mode keeps both RGB framebuffers visually coherent.
             * Direct mode is lighter, but with many small dashboard updates it
             * can leave faint stale bright columns on this ST7701S/S3 setup. */
            .direct_mode  = false,
            .full_refresh = true,
        },
    };
    /* The RGB driver uses a bounce buffer, so let esp_lvgl_port wait for
     * frame-buffer completion instead of a raw VSYNC tick. */
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,
            .avoid_tearing = true,
        },
    };

    s_lv_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (s_lv_display == NULL) {
        ESP_LOGE(TAG_DISPLAY, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }
    lv_display_set_default(s_lv_display);
    lv_display_set_antialiasing(s_lv_display, APP_LVGL_ANTIALIASING != 0);

    ESP_LOGI(TAG_DISPLAY, "LVGL display added (%dx%d, RGB full-refresh mode, avoid_tearing=1, bb_mode=1)",
        APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT);
    return ESP_OK;
}

/* â”€â”€ Public API â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
esp_err_t display_init(void)
{
    if (s_display_ready) return ESP_OK;

    /* 1. Backlight LEDC (stays at 0% until LVGL is ready) */
    ESP_RETURN_ON_ERROR(backlight_ledc_init(), TAG_DISPLAY, "backlight init failed");

    /* 2. LVGL port task */
    lvgl_port_cfg_t lvgl_cfg = display_port_cfg();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG_DISPLAY, "lvgl_port_init failed");

    /* 3. ST7701S + RGB panel (3-wire SPI init â†’ RGB DMA engine) */
    ESP_RETURN_ON_ERROR(panel_create(), TAG_DISPLAY, "panel_create failed");

    /* 4. Register with LVGL */
    ESP_RETURN_ON_ERROR(lvgl_display_add(), TAG_DISPLAY, "lvgl_display_add failed");

    /* 5. Dim timer (non-fatal) */
    (void)display_dim_timer_init();

    s_display_ready = true;
    ESP_LOGI(TAG_DISPLAY, "Display init OK â€” ST7701S RGB 480x480, LEDC backlight GPIO%d",
        PANELS3_BL_GPIO);

    /* 6. Keep the backlight dark until the boot splash has rendered once. */
    return ESP_OK;
}

bool display_is_ready(void)                    { return s_display_ready; }
bool display_lock(uint32_t timeout_ms)         { return lvgl_port_lock(timeout_ms); }
void display_unlock(void)                      { lvgl_port_unlock(); }

/* ESP-BSP API shim: api_settings.c / api_ota.c call this before destructive actions */
esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG_DISPLAY, "bsp_display_backlight_off");
    return display_set_brightness_percent(0);
}
