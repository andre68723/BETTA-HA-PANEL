/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 * Original-JC transfer timing. Preserve all adapter callbacks and user context.
 */
#include "drivers/display_timing.h"
#include "esp_attr.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"

static DRAM_ATTR portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR esp_lcd_dpi_panel_color_trans_done_cb_t s_color;
static DRAM_ATTR esp_lcd_panel_handle_t s_panel;

static bool IRAM_ATTR transfer_done(esp_lcd_panel_handle_t panel,
                                   esp_lcd_dpi_panel_event_data_t *event, void *user)
{
    display_timing_transfer_done_isr();
    portENTER_CRITICAL_ISR(&s_lock);
    esp_lcd_dpi_panel_color_trans_done_cb_t forward = s_color;
    portEXIT_CRITICAL_ISR(&s_lock);
    return forward ? forward(panel, event, user) : false;
}

esp_err_t __real_esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
    const esp_lcd_dpi_panel_event_callbacks_t *callbacks, void *user);

/* The IDF registration replaces all callbacks at once. Interpose only on
 * this build so installing the observer never replaces the flush handshake. */
esp_err_t __wrap_esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
    const esp_lcd_dpi_panel_event_callbacks_t *callbacks, void *user)
{
    if (!panel || !callbacks || (s_panel && s_panel != panel))
        return __real_esp_lcd_dpi_panel_register_event_callbacks(panel, callbacks, user);
#if CONFIG_LCD_DSI_ISR_CACHE_SAFE
    if (callbacks->on_vsync && !esp_ptr_in_iram(callbacks->on_vsync)) return ESP_ERR_INVALID_ARG;
    if (callbacks->on_color_trans_done && !esp_ptr_in_iram(callbacks->on_color_trans_done)) return ESP_ERR_INVALID_ARG;
#endif
    bool installing = callbacks->on_vsync || callbacks->on_color_trans_done || callbacks->on_frame_buf_complete;
    esp_lcd_dpi_panel_event_callbacks_t next = *callbacks;
    if (callbacks->on_color_trans_done) next.on_color_trans_done = transfer_done;
    portENTER_CRITICAL(&s_lock);
    esp_lcd_dpi_panel_color_trans_done_cb_t previous_color = s_color;
    s_color = callbacks->on_color_trans_done;
    portEXIT_CRITICAL(&s_lock);
    esp_err_t err = __real_esp_lcd_dpi_panel_register_event_callbacks(panel, &next, user);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_color = previous_color;
        portEXIT_CRITICAL(&s_lock);
        return err;
    }
    if (installing) s_panel = panel;
    return ESP_OK;
}
