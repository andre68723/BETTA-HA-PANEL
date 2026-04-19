/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/ui_ota_progress.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "ui/fonts/app_text_fonts.h"
#include "ui/theme/theme_default.h"

#if APP_HAVE_SMART86OS_BETTA_IMAGE
extern const lv_image_dsc_t SMART86OS_Betta;
#endif

#define OTA_PROGRESS_UPDATE_MIN_MS 220
#define OTA_PROGRESS_BG_HEX 0x050A10
#define OTA_PROGRESS_TRACK_HEX 0x1B2A3A
#define OTA_PROGRESS_ACCENT_HEX 0x53E5FF

typedef struct {
    lv_obj_t *root;
    lv_obj_t *logo;
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *bar;
    lv_obj_t *percent;
    lv_obj_t *detail;
    bool active;
    uint8_t last_percent;
    int64_t last_update_ms;
} ui_ota_progress_state_t;

static ui_ota_progress_state_t s_ota_ui = {0};

static int64_t ota_ui_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void ota_ui_format_bytes(size_t bytes, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    if (bytes >= (1024U * 1024U)) {
        unsigned whole = (unsigned)(bytes / (1024U * 1024U));
        unsigned frac = (unsigned)(((bytes % (1024U * 1024U)) * 10U) / (1024U * 1024U));
        snprintf(out, out_len, "%u.%u MB", whole, frac);
    } else if (bytes >= 1024U) {
        unsigned whole = (unsigned)(bytes / 1024U);
        unsigned frac = (unsigned)(((bytes % 1024U) * 10U) / 1024U);
        snprintf(out, out_len, "%u.%u KB", whole, frac);
    } else {
        snprintf(out, out_len, "%u B", (unsigned)bytes);
    }
}

static uint8_t ota_ui_percent(size_t written, size_t total)
{
    if (total == 0) {
        return 0;
    }
    uint64_t scaled = ((uint64_t)written * 100ULL) / (uint64_t)total;
    if (scaled > 100ULL) {
        scaled = 100ULL;
    }
    return (uint8_t)scaled;
}

static void ota_ui_set_text_locked(lv_obj_t *label, const char *text)
{
    if (label != NULL) {
        lv_label_set_text(label, (text != NULL && text[0] != '\0') ? text : "");
    }
}

static void ota_ui_update_detail_locked(size_t written, size_t total)
{
    if (s_ota_ui.detail == NULL || s_ota_ui.percent == NULL || s_ota_ui.bar == NULL) {
        return;
    }

    uint8_t percent = ota_ui_percent(written, total);
    char percent_text[12] = {0};
    if (total > 0) {
        snprintf(percent_text, sizeof(percent_text), "%u %%", (unsigned)percent);
    } else {
        snprintf(percent_text, sizeof(percent_text), "-- %%");
    }
    lv_label_set_text(s_ota_ui.percent, percent_text);
    lv_bar_set_value(s_ota_ui.bar, (int)percent, LV_ANIM_OFF);

    char written_text[24] = {0};
    char total_text[24] = {0};
    ota_ui_format_bytes(written, written_text, sizeof(written_text));
    if (total > 0) {
        ota_ui_format_bytes(total, total_text, sizeof(total_text));
        char detail[64] = {0};
        snprintf(detail, sizeof(detail), "%s / %s", written_text, total_text);
        lv_label_set_text(s_ota_ui.detail, detail);
    } else {
        char detail[64] = {0};
        snprintf(detail, sizeof(detail), "%s / unknown", written_text);
        lv_label_set_text(s_ota_ui.detail, detail);
    }

    s_ota_ui.last_percent = percent;
    s_ota_ui.last_update_ms = ota_ui_now_ms();
}

static lv_obj_t *ota_ui_create_logo_locked(lv_obj_t *parent)
{
#if APP_HAVE_SMART86OS_BETTA_IMAGE
    lv_obj_t *logo = lv_image_create(parent);
    lv_image_set_src(logo, &SMART86OS_Betta);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 34);
    return logo;
#else
    lv_obj_t *logo = lv_label_create(parent);
    lv_label_set_text(logo, "BETTA86");
    lv_obj_set_style_text_color(logo, lv_color_hex(OTA_PROGRESS_ACCENT_HEX), LV_PART_MAIN);
    lv_obj_set_style_text_font(logo, APP_FONT_DISPLAY_40, LV_PART_MAIN);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 140);
    return logo;
#endif
}

static void ota_ui_create_locked(const char *status_text, size_t total_bytes)
{
    if (s_ota_ui.root != NULL) {
        lv_obj_move_foreground(s_ota_ui.root);
        ota_ui_set_text_locked(s_ota_ui.status, status_text);
        ota_ui_update_detail_locked(0, total_bytes);
        return;
    }

    lv_obj_t *top_layer = lv_layer_top();
    s_ota_ui.root = lv_obj_create(top_layer);
    lv_obj_set_size(s_ota_ui.root, APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT);
    lv_obj_set_pos(s_ota_ui.root, 0, 0);
    lv_obj_add_flag(s_ota_ui.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ota_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_ota_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ota_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ota_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_ui.root, lv_color_hex(OTA_PROGRESS_BG_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ota_ui.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_foreground(s_ota_ui.root);

    s_ota_ui.logo = ota_ui_create_logo_locked(s_ota_ui.root);

    s_ota_ui.title = lv_label_create(s_ota_ui.root);
    lv_label_set_text(s_ota_ui.title, "Firmware Update");
    lv_obj_set_style_text_color(s_ota_ui.title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_ui.title, APP_FONT_DISPLAY_34, LV_PART_MAIN);
    lv_obj_align(s_ota_ui.title, LV_ALIGN_TOP_MID, 0, 388);

    s_ota_ui.status = lv_label_create(s_ota_ui.root);
    lv_label_set_text(s_ota_ui.status, status_text != NULL ? status_text : "Preparing update");
    lv_obj_set_width(s_ota_ui.status, 560);
    lv_obj_set_style_text_align(s_ota_ui.status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ota_ui.status, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_ui.status, APP_FONT_TEXT_20, LV_PART_MAIN);
    lv_obj_align(s_ota_ui.status, LV_ALIGN_TOP_MID, 0, 438);

    s_ota_ui.bar = lv_bar_create(s_ota_ui.root);
    lv_obj_set_size(s_ota_ui.bar, 440, 12);
    lv_obj_align(s_ota_ui.bar, LV_ALIGN_TOP_MID, 0, 500);
    lv_bar_set_range(s_ota_ui.bar, 0, 100);
    lv_bar_set_value(s_ota_ui.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_ota_ui.bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ota_ui.bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_ota_ui.bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ota_ui.bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_ui.bar, lv_color_hex(OTA_PROGRESS_TRACK_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ota_ui.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ota_ui.bar, lv_color_hex(OTA_PROGRESS_ACCENT_HEX), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ota_ui.bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_ota_ui.percent = lv_label_create(s_ota_ui.root);
    lv_label_set_text(s_ota_ui.percent, "0 %");
    lv_obj_set_style_text_color(s_ota_ui.percent, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_ui.percent, APP_FONT_DISPLAY_28, LV_PART_MAIN);
    lv_obj_align(s_ota_ui.percent, LV_ALIGN_TOP_MID, 0, 532);

    s_ota_ui.detail = lv_label_create(s_ota_ui.root);
    lv_label_set_text(s_ota_ui.detail, "");
    lv_obj_set_style_text_color(s_ota_ui.detail, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ota_ui.detail, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_align(s_ota_ui.detail, LV_ALIGN_TOP_MID, 0, 574);

    s_ota_ui.active = true;
    s_ota_ui.last_percent = 255U;
    s_ota_ui.last_update_ms = 0;
    ota_ui_update_detail_locked(0, total_bytes);
}

void ui_ota_progress_begin(const char *status_text, size_t total_bytes)
{
    if (!display_is_ready()) {
        return;
    }
    if (!display_lock(120)) {
        return;
    }
    ota_ui_create_locked(status_text, total_bytes);
    display_unlock();
}

void ui_ota_progress_set_status(const char *status_text)
{
    if (!display_is_ready() || s_ota_ui.root == NULL) {
        return;
    }
    if (!display_lock(30)) {
        return;
    }
    ota_ui_set_text_locked(s_ota_ui.status, status_text);
    display_unlock();
}

void ui_ota_progress_update(size_t written_bytes, size_t total_bytes)
{
    if (!display_is_ready() || s_ota_ui.root == NULL) {
        return;
    }

    uint8_t next_percent = ota_ui_percent(written_bytes, total_bytes);
    int64_t now_ms = ota_ui_now_ms();
    if (next_percent == s_ota_ui.last_percent &&
        (now_ms - s_ota_ui.last_update_ms) < OTA_PROGRESS_UPDATE_MIN_MS) {
        return;
    }

    if (!display_lock(20)) {
        return;
    }
    ota_ui_update_detail_locked(written_bytes, total_bytes);
    display_unlock();
}

void ui_ota_progress_success(const char *status_text)
{
    if (!display_is_ready() || s_ota_ui.root == NULL) {
        return;
    }
    if (!display_lock(80)) {
        return;
    }
    ota_ui_set_text_locked(s_ota_ui.status, status_text != NULL ? status_text : "Update written. Restarting...");
    if (s_ota_ui.status != NULL) {
        lv_obj_set_style_text_color(s_ota_ui.status, lv_color_hex(APP_UI_COLOR_OK), LV_PART_MAIN);
    }
    if (s_ota_ui.bar != NULL) {
        lv_bar_set_value(s_ota_ui.bar, 100, LV_ANIM_OFF);
    }
    if (s_ota_ui.percent != NULL) {
        lv_label_set_text(s_ota_ui.percent, "100 %");
    }
    display_unlock();
}

void ui_ota_progress_error(const char *error_text)
{
    if (!display_is_ready() || s_ota_ui.root == NULL) {
        return;
    }
    if (!display_lock(80)) {
        return;
    }
    ota_ui_set_text_locked(s_ota_ui.status, error_text != NULL ? error_text : "OTA failed");
    if (s_ota_ui.status != NULL) {
        lv_obj_set_style_text_color(s_ota_ui.status, lv_color_hex(APP_UI_COLOR_ERROR), LV_PART_MAIN);
    }
    display_unlock();
}

bool ui_ota_progress_is_active(void)
{
    return s_ota_ui.active && s_ota_ui.root != NULL;
}
