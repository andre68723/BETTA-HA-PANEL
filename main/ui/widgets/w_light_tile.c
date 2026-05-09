/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

#include "ha/ha_light_capabilities.h"
#include "ui/fonts/app_text_fonts.h"
#include "ui/ui_bindings.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/theme/theme_default.h"

#ifndef APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES
#define APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES 0
#endif

#ifndef APP_HAVE_LIGHT_RGB_SLIDER_IMAGE
#define APP_HAVE_LIGHT_RGB_SLIDER_IMAGE 0
#endif

#if APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES
LV_IMAGE_DECLARE(Temp);
LV_IMAGE_DECLARE(RGB);
#endif

#if APP_HAVE_LIGHT_RGB_SLIDER_IMAGE
LV_IMAGE_DECLARE(visiblecolor);
#endif

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *popup_overlay;
    bool is_on;
    int brightness;
    bool unavailable;
    bool can_dim;
    bool can_color;
    bool can_color_temp;
    bool has_rgb_color;
    uint32_t rgb_color;
    bool has_color_temp_kelvin;
    int color_temp_kelvin;
    int min_color_temp_kelvin;
    int max_color_temp_kelvin;
    lv_coord_t configured_min_dim;
} w_light_tile_ctx_t;

#define ICON_CP_MDI_LIGHTBULB_ON 0xF06E8U
/* The RGB band is linear red-to-violet, so it intentionally does not wrap back to red. */
#define LIGHT_RGB_HUE_MIN 0
#define LIGHT_RGB_HUE_MAX 285
static const char *TAG = "w_light_tile";

typedef enum {
    LIGHT_TILE_CLASS_COMPACT = 0,
    LIGHT_TILE_CLASS_S,
    LIGHT_TILE_CLASS_M,
    LIGHT_TILE_CLASS_L,
} light_tile_class_t;

typedef struct {
    lv_coord_t card_pad;
    lv_coord_t title_bottom;
    lv_coord_t top_label_y;
    lv_coord_t slider_side_margin;
    lv_coord_t slider_height;
    lv_coord_t slider_bottom;
    lv_coord_t icon_top;
    lv_coord_t icon_gap;
    lv_coord_t icon_bias_y;
    const lv_font_t *title_font;
    const lv_font_t *top_font;
} light_tile_layout_t;

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *state_label;
    lv_obj_t *slider;
    lv_obj_t *value_label;
    lv_obj_t *color_button;
    lv_obj_t *color_button_image;
} light_tile_widgets_t;

static const light_tile_layout_t LIGHT_LAYOUT_COMPACT = {
    .card_pad = 12,
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    .title_bottom = -28,
#else
    .title_bottom = -40,
#endif
    .top_label_y = 0,
    .slider_side_margin = 14,
    .slider_height = 12,
    .slider_bottom = -10,
    .icon_top = 10,
    .icon_gap = 4,
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    .icon_bias_y = -2,
    .title_font = APP_FONT_TEXT_14,
    .top_font = APP_FONT_TEXT_14,
#else
    .icon_bias_y = 0,
    .title_font = APP_FONT_TEXT_16,
    .top_font = APP_FONT_TEXT_16,
#endif
};

static const light_tile_layout_t LIGHT_LAYOUT_S = {
    .card_pad = 14,
    .title_bottom = -46,
    .top_label_y = 2,
    .slider_side_margin = 18,
    .slider_height = 13,
    .slider_bottom = -12,
    .icon_top = 9,
    .icon_gap = 8,
    .icon_bias_y = 2,
    .title_font = APP_FONT_TEXT_16,
    .top_font = APP_FONT_TEXT_16,
};

static const light_tile_layout_t LIGHT_LAYOUT_M = {
    .card_pad = 16,
    .title_bottom = -54,
    .top_label_y = 2,
    .slider_side_margin = 22,
    .slider_height = 15,
    .slider_bottom = -16,
    .icon_top = 8,
    .icon_gap = 10,
    .icon_bias_y = 8,
    .title_font = APP_FONT_TEXT_18,
    .top_font = APP_FONT_TEXT_16,
};

static const light_tile_layout_t LIGHT_LAYOUT_L = {
    .card_pad = 18,
    .title_bottom = -62,
    .top_label_y = 2,
    .slider_side_margin = 26,
    .slider_height = 16,
    .slider_bottom = -18,
    .icon_top = 8,
    .icon_gap = 14,
    .icon_bias_y = 12,
    .title_font = APP_FONT_TEXT_18,
    .top_font = APP_FONT_TEXT_16,
};

static lv_coord_t light_color_button_size_for_class(light_tile_class_t tile_class)
{
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    switch (tile_class) {
        case LIGHT_TILE_CLASS_COMPACT:
            return 36;
        case LIGHT_TILE_CLASS_S:
            return 38;
        default:
            return 42;
    }
#else
    (void)tile_class;
    return 42;
#endif
}

static lv_coord_t light_slider_min_width_for_class(light_tile_class_t tile_class)
{
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    return (tile_class == LIGHT_TILE_CLASS_COMPACT) ? 52 : 60;
#else
    (void)tile_class;
    return 60;
#endif
}

static lv_coord_t light_icon_bias_x_for_button_size(lv_coord_t button_size)
{
    return -(button_size / 2) - 1;
}

static bool light_font_has_icon(const lv_font_t *font)
{
    if (font == NULL) {
        return false;
    }

    lv_font_glyph_dsc_t dsc = {0};
    return lv_font_get_glyph_dsc(font, &dsc, ICON_CP_MDI_LIGHTBULB_ON, 0);
}

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static bool light_state_is_on(const char *state)
{
    if (state == NULL) {
        return false;
    }
    return strcmp(state, "on") == 0;
}

static const char *light_translate_status(const char *status_text)
{
    if (status_text == NULL || status_text[0] == '\0') {
        return ui_i18n_get("common.off", "OFF");
    }
    if (strcmp(status_text, "ON") == 0 || strcmp(status_text, "on") == 0) {
        return ui_i18n_get("common.on", "ON");
    }
    if (strcmp(status_text, "OFF") == 0 || strcmp(status_text, "off") == 0) {
        return ui_i18n_get("common.off", "OFF");
    }
    if (strcmp(status_text, "unavailable") == 0) {
        return ui_i18n_get("common.unavailable", "unavailable");
    }
    return status_text;
}

static void light_set_value_label(lv_obj_t *label, int value)
{
    if (label == NULL) {
        return;
    }
    char text[16] = {0};
    snprintf(text, sizeof(text), "%d %%", clamp_percent(value));
    lv_label_set_text(label, text);
}

static const char *light_icon_utf8_from_codepoint(uint32_t codepoint)
{
    static char utf8[5] = {0};

    if (codepoint <= 0x7FU) {
        utf8[0] = (char)codepoint;
        utf8[1] = '\0';
        return utf8;
    }
    if (codepoint <= 0x7FFU) {
        utf8[0] = (char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        utf8[1] = (char)(0x80U | (codepoint & 0x3FU));
        utf8[2] = '\0';
        return utf8;
    }
    if (codepoint <= 0xFFFFU) {
        utf8[0] = (char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        utf8[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        utf8[2] = (char)(0x80U | (codepoint & 0x3FU));
        utf8[3] = '\0';
        return utf8;
    }

    utf8[0] = (char)(0xF0U | ((codepoint >> 18) & 0x07U));
    utf8[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
    utf8[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
    utf8[3] = (char)(0x80U | (codepoint & 0x3FU));
    utf8[4] = '\0';
    return utf8;
}

static const lv_font_t *light_icon_font_for_min_dim(lv_coord_t min_dim)
{
    const lv_font_t *font = NULL;
    if (min_dim >= 300) {
        font = mdi_font_icon_72();
    } else if (min_dim >= 240) {
        font = mdi_font_icon_56();
    } else {
        font = mdi_font_icon_42();
    }

    if (!light_font_has_icon(font)) {
        font = mdi_font_large();
    }
    if (!light_font_has_icon(font)) {
        font = mdi_font_weather();
    }
    if (light_font_has_icon(font)) {
        return font;
    }
    return LV_FONT_DEFAULT;
}

static const char *light_icon_text_for_font(const lv_font_t *font)
{
    static bool warned = false;
    if (light_font_has_icon(font)) {
        return light_icon_utf8_from_codepoint(ICON_CP_MDI_LIGHTBULB_ON);
    }
    if (!warned) {
        warned = true;
        ESP_LOGW(TAG, "MDI icon glyph U+%05" PRIX32 " not available, using LV_SYMBOL_POWER fallback", ICON_CP_MDI_LIGHTBULB_ON);
    }
    return LV_SYMBOL_POWER;
}

static lv_coord_t light_card_width(lv_obj_t *card)
{
    if (card == NULL) {
        return 0;
    }
    lv_obj_update_layout(card);
    lv_coord_t width = lv_obj_get_width(card);
    if (width <= 0) {
        width = lv_obj_get_style_width(card, LV_PART_MAIN);
    }
    return (width > 0) ? width : 0;
}

static lv_coord_t light_card_min_dim(lv_obj_t *card)
{
    if (card == NULL) {
        return 0;
    }
    lv_obj_update_layout(card);
    lv_coord_t w = lv_obj_get_width(card);
    lv_coord_t h = lv_obj_get_height(card);

    if (w <= 0) {
        w = lv_obj_get_style_width(card, LV_PART_MAIN);
    }
    if (h <= 0) {
        h = lv_obj_get_style_height(card, LV_PART_MAIN);
    }
    if (w <= 0 || h <= 0) {
        return 0;
    }
    return (w < h) ? w : h;
}

static lv_coord_t light_effective_min_dim(lv_obj_t *card, const w_light_tile_ctx_t *ctx)
{
    lv_coord_t min_dim = light_card_min_dim(card);
    if (min_dim > 0) {
        return min_dim;
    }
    if (ctx != NULL && ctx->configured_min_dim > 0) {
        return ctx->configured_min_dim;
    }
    return 0;
}

static bool light_get_widgets(lv_obj_t *card, light_tile_widgets_t *out)
{
    if (card == NULL || out == NULL) {
        return false;
    }
    out->icon = lv_obj_get_child(card, 0);
    out->title = lv_obj_get_child(card, 1);
    out->state_label = lv_obj_get_child(card, 2);
    out->slider = lv_obj_get_child(card, 3);
    out->value_label = lv_obj_get_child(card, 4);
    out->color_button = lv_obj_get_child(card, 5);
    out->color_button_image = out->color_button != NULL ? lv_obj_get_child(out->color_button, 0) : NULL;
    return out->icon != NULL && out->title != NULL && out->state_label != NULL && out->slider != NULL &&
           out->value_label != NULL && out->color_button != NULL;
}

static light_tile_class_t light_tile_class_from_dim(lv_coord_t min_dim)
{
    if (min_dim <= 0) {
        return LIGHT_TILE_CLASS_S;
    }
    if (min_dim <= 180) {
        return LIGHT_TILE_CLASS_COMPACT;
    }
    if (min_dim < 240) {
        return LIGHT_TILE_CLASS_S;
    }
    if (min_dim < 300) {
        return LIGHT_TILE_CLASS_M;
    }
    return LIGHT_TILE_CLASS_L;
}

static const light_tile_layout_t *light_pick_layout(lv_obj_t *card, const w_light_tile_ctx_t *ctx)
{
    switch (light_tile_class_from_dim(light_effective_min_dim(card, ctx))) {
        case LIGHT_TILE_CLASS_COMPACT:
            return &LIGHT_LAYOUT_COMPACT;
        case LIGHT_TILE_CLASS_M:
            return &LIGHT_LAYOUT_M;
        case LIGHT_TILE_CLASS_L:
            return &LIGHT_LAYOUT_L;
        case LIGHT_TILE_CLASS_S:
        default:
            return &LIGHT_LAYOUT_S;
    }
}

static void light_apply_layout(lv_obj_t *card, const w_light_tile_ctx_t *ctx, const light_tile_layout_t *layout)
{
    if (card == NULL || layout == NULL) {
        return;
    }

    light_tile_widgets_t w = {0};
    if (!light_get_widgets(card, &w)) {
        return;
    }

    lv_coord_t card_w = light_card_width(card);
    if (card_w <= 0) {
        return;
    }

    const light_tile_class_t tile_class = light_tile_class_from_dim(light_effective_min_dim(card, ctx));
    const lv_coord_t slider_min_w = light_slider_min_width_for_class(tile_class);
    const lv_coord_t color_button_size = light_color_button_size_for_class(tile_class);

    lv_obj_set_style_pad_all(card, layout->card_pad, LV_PART_MAIN);

    lv_coord_t content_w = card_w - (layout->card_pad * 2);
    if (content_w < 40) {
        content_w = 40;
    }

    lv_coord_t slider_w = card_w - (layout->slider_side_margin * 2);
    if (slider_w < slider_min_w) {
        slider_w = slider_min_w;
    }

    lv_obj_set_width(w.icon, content_w);
    lv_obj_set_width(w.title, content_w);
    lv_obj_set_width(w.slider, slider_w);
    lv_obj_set_height(w.slider, layout->slider_height);
    lv_obj_set_size(w.color_button, color_button_size, color_button_size);
    if (w.color_button_image != NULL) {
        lv_obj_set_size(w.color_button_image, color_button_size, color_button_size);
        lv_obj_center(w.color_button_image);
    }

    lv_obj_set_style_text_font(w.title, layout->title_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(w.state_label, layout->top_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(w.value_label, layout->top_font, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(w.icon, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(w.icon, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_zoom(w.icon, 256, LV_PART_MAIN);

    lv_obj_align(w.icon, LV_ALIGN_TOP_MID, 0, layout->icon_top);
    lv_obj_align(w.title, LV_ALIGN_BOTTOM_MID, 0, layout->title_bottom);
    lv_obj_align(w.state_label, LV_ALIGN_TOP_LEFT, 0, layout->top_label_y);
    lv_obj_align(w.value_label, LV_ALIGN_TOP_RIGHT, 0, layout->top_label_y);
    lv_obj_align(w.color_button, LV_ALIGN_TOP_RIGHT, 0, layout->top_label_y + 28);
    lv_obj_align(w.slider, LV_ALIGN_BOTTOM_MID, 0, layout->slider_bottom);
}

static void light_position_icon_between_state_and_title(lv_obj_t *card, lv_coord_t gap, lv_coord_t bias_y, lv_coord_t bias_x)
{
    if (card == NULL) {
        return;
    }

    lv_obj_t *icon = lv_obj_get_child(card, 0);
    lv_obj_t *title = lv_obj_get_child(card, 1);
    lv_obj_t *state_label = lv_obj_get_child(card, 2);
    if (icon == NULL || title == NULL || state_label == NULL) {
        return;
    }

    /* Layout once so child coordinates/heights are valid before calculating placement. */
    lv_obj_update_layout(card);

    if (gap < 0) {
        gap = 0;
    }
    lv_coord_t top = lv_obj_get_y(state_label) + lv_obj_get_height(state_label) + gap;
    lv_coord_t bottom = lv_obj_get_y(title) - gap;
    lv_coord_t icon_h = lv_obj_get_height(icon);

    if (icon_h < 1) {
        const lv_font_t *font = lv_obj_get_style_text_font(icon, LV_PART_MAIN);
        if (font != NULL) {
            icon_h = font->line_height;
        }
    }

    lv_coord_t y = top;
    lv_coord_t room = bottom - top;
    if (room >= icon_h) {
        y = top + (room - icon_h) / 2;
    }

    lv_coord_t max_y = bottom - icon_h;
    if (max_y < top) {
        max_y = top;
    }
    y += bias_y;
    if (y < top) {
        y = top;
    }
    if (y > max_y) {
        y = max_y;
    }

    lv_obj_align(icon, LV_ALIGN_TOP_MID, bias_x, y);
}

static void light_apply_visual(lv_obj_t *card, const w_light_tile_ctx_t *ctx, bool is_on, int brightness, const char *status_text)
{
    if (card == NULL) {
        return;
    }
    light_tile_widgets_t w = {0};
    if (!light_get_widgets(card, &w)) {
        return;
    }
    const light_tile_layout_t *layout = light_pick_layout(card, ctx);
    const light_tile_class_t tile_class = light_tile_class_from_dim(light_effective_min_dim(card, ctx));
    const lv_coord_t color_button_size = light_color_button_size_for_class(tile_class);
    light_apply_layout(card, ctx, layout);
    const lv_coord_t min_dim = light_effective_min_dim(card, ctx);
    const lv_font_t *icon_font = light_icon_font_for_min_dim(min_dim);

    lv_obj_set_style_bg_color(
        card, is_on ? lv_color_hex(APP_UI_COLOR_CARD_BG_ON) : lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    const bool can_dim = (ctx == NULL) ? true : ctx->can_dim;
    const bool can_adjust_color = ctx != NULL && !ctx->unavailable && (ctx->can_color || ctx->can_color_temp);
    const bool has_rgb_color = ctx != NULL && ctx->has_rgb_color;
    const uint32_t icon_on_color = (is_on && has_rgb_color) ? ctx->rgb_color : APP_UI_COLOR_LIGHT_ICON_ON;

    lv_obj_set_style_text_color(
        w.icon, is_on ? lv_color_hex(icon_on_color) : lv_color_hex(APP_UI_COLOR_CARD_ICON_OFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(w.title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(
        w.state_label, is_on ? lv_color_hex(APP_UI_COLOR_STATE_ON) : lv_color_hex(APP_UI_COLOR_STATE_OFF), LV_PART_MAIN);

    if (can_dim) {
        lv_obj_clear_flag(w.slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(w.value_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(
            w.slider, is_on ? lv_color_hex(APP_UI_COLOR_LIGHT_TRACK_ON) : lv_color_hex(APP_UI_COLOR_LIGHT_TRACK_OFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w.slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            w.slider, is_on ? lv_color_hex(APP_UI_COLOR_LIGHT_IND_ON) : lv_color_hex(APP_UI_COLOR_LIGHT_IND_OFF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(w.slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            w.slider, is_on ? lv_color_hex(APP_UI_COLOR_LIGHT_KNOB_ON) : lv_color_hex(APP_UI_COLOR_LIGHT_KNOB_OFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(w.slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_text_color(
            w.value_label, is_on ? lv_color_hex(APP_UI_COLOR_STATE_ON) : lv_color_hex(APP_UI_COLOR_STATE_OFF), LV_PART_MAIN);
    } else {
        lv_obj_add_flag(w.slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(w.value_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (can_adjust_color) {
        const bool show_rgb_button = ctx != NULL && ctx->can_color;

        lv_obj_clear_flag(w.color_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(w.color_button, color_button_size, color_button_size);
        lv_obj_set_style_radius(w.color_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
#if APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES
        lv_obj_set_style_bg_opa(w.color_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(w.color_button, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_width(w.color_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(w.color_button, 0, LV_PART_MAIN);
        if (w.color_button_image != NULL) {
            lv_obj_clear_flag(w.color_button_image, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(w.color_button_image, show_rgb_button ? &RGB : &Temp);
            lv_obj_set_size(w.color_button_image, color_button_size, color_button_size);
            lv_image_set_inner_align(w.color_button_image, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_center(w.color_button_image);
        }
#else
        const bool show_temp_button = !show_rgb_button;
        const bool show_color_value = ctx != NULL && ctx->has_rgb_color;
        uint32_t button_bg = APP_UI_COLOR_NAV_BTN_BG_ACTIVE;
        if (show_temp_button) {
            button_bg = 0xFFF8EE;
        } else if (show_color_value) {
            button_bg = ctx->rgb_color & 0xFFFFFFU;
        } else if (!is_on) {
            button_bg = APP_UI_COLOR_NAV_BTN_BG_IDLE;
        }

        lv_obj_set_style_bg_color(w.color_button, lv_color_hex(button_bg), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(w.color_button, lv_color_hex(0xFF9828), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(w.color_button, show_temp_button ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w.color_button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(w.color_button, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(w.color_button, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_opa(w.color_button, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(w.color_button, 0, LV_PART_MAIN);
        if (w.color_button_image != NULL) {
            lv_obj_add_flag(w.color_button_image, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    } else {
        lv_obj_add_flag(w.color_button, LV_OBJ_FLAG_HIDDEN);
        if (w.color_button_image != NULL) {
            lv_obj_add_flag(w.color_button_image, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_set_style_text_font(w.icon, icon_font, LV_PART_MAIN);
    lv_slider_set_value(w.slider, can_dim ? clamp_percent(brightness) : (is_on ? 100 : 0), LV_ANIM_OFF);
    light_set_value_label(w.value_label, brightness);
    lv_label_set_text(w.icon, light_icon_text_for_font(icon_font));
    lv_label_set_text(w.state_label, light_translate_status(status_text != NULL ? status_text : (is_on ? "ON" : "OFF")));
    light_position_icon_between_state_and_title(
        card, layout->icon_gap, layout->icon_bias_y,
        can_adjust_color ? light_icon_bias_x_for_button_size(color_button_size) : 0);
}

typedef enum {
    LIGHT_POPUP_BAND_KELVIN = 0,
    LIGHT_POPUP_BAND_RGB,
} light_popup_band_mode_t;

typedef enum {
    LIGHT_POPUP_PRESET_KELVIN = 0,
    LIGHT_POPUP_PRESET_RGB,
} light_popup_preset_type_t;

typedef struct {
    light_popup_preset_type_t type;
    uint32_t display_color;
    uint32_t rgb;
    int kelvin_percent;
} light_popup_preset_t;

typedef struct {
    w_light_tile_ctx_t *tile;
    lv_obj_t *overlay;
    lv_obj_t *band_panel;
    lv_obj_t *preset_panel;
    lv_obj_t *band_label;
    lv_obj_t *kelvin_label;
    lv_obj_t *rgb_label;
    lv_obj_t *warm_label;
    lv_obj_t *cool_label;
    lv_obj_t *preview;
    lv_obj_t *temp_button;
    lv_obj_t *rgb_button;
    lv_obj_t *temp_slider;
    lv_obj_t *rgb_band;
    lv_obj_t *rgb_slider;
    light_popup_band_mode_t band_mode;
    uint32_t rgb;
    int hue;
    int kelvin;
    int min_kelvin;
    int max_kelvin;
} light_popup_ctx_t;

static int light_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint8_t light_rgb_component(uint32_t rgb, int shift)
{
    return (uint8_t)((rgb >> shift) & 0xFFU);
}

static int light_rgb_to_hue(uint32_t rgb)
{
    int r = (int)light_rgb_component(rgb, 16);
    int g = (int)light_rgb_component(rgb, 8);
    int b = (int)light_rgb_component(rgb, 0);
    int max = r;
    if (g > max) {
        max = g;
    }
    if (b > max) {
        max = b;
    }
    int min = r;
    if (g < min) {
        min = g;
    }
    if (b < min) {
        min = b;
    }

    int delta = max - min;
    if (delta <= 0) {
        return 0;
    }

    int hue = 0;
    if (max == r) {
        hue = (60 * (g - b)) / delta;
        if (hue < 0) {
            hue += 360;
        }
    } else if (max == g) {
        hue = 120 + ((60 * (b - r)) / delta);
    } else {
        hue = 240 + ((60 * (r - g)) / delta);
    }

    return light_clamp_int(hue, LIGHT_RGB_HUE_MIN, LIGHT_RGB_HUE_MAX);
}

static uint32_t light_hue_to_rgb(int hue)
{
    hue = light_clamp_int(hue, LIGHT_RGB_HUE_MIN, LIGHT_RGB_HUE_MAX);

    int region = hue / 60;
    int remainder = hue % 60;
    int ramp = (255 * remainder) / 60;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (region) {
        case 0:
            r = 255;
            g = (uint8_t)ramp;
            break;
        case 1:
            r = (uint8_t)(255 - ramp);
            g = 255;
            break;
        case 2:
            g = 255;
            b = (uint8_t)ramp;
            break;
        case 3:
            g = (uint8_t)(255 - ramp);
            b = 255;
            break;
        case 4:
            r = (uint8_t)ramp;
            b = 255;
            break;
        case 5:
        default:
            r = 255;
            b = (uint8_t)(255 - ramp);
            break;
    }

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void light_popup_style_round_button(lv_obj_t *button, uint32_t bg_color, uint32_t text_color)
{
    if (button == NULL) {
        return;
    }
    lv_obj_clear_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(button, 46, 46);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_hex(text_color), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
}

static void light_popup_set_round_button_active(lv_obj_t *button, bool active)
{
    if (button == NULL) {
        return;
    }
    lv_obj_set_style_border_width(button, active ? 3 : 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        button, lv_color_hex(active ? APP_UI_COLOR_NAV_TAB_ACTIVE : APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, active ? LV_OPA_COVER : LV_OPA_80, LV_PART_MAIN);
}

static void light_popup_update_kelvin_label(light_popup_ctx_t *popup)
{
    if (popup == NULL || popup->kelvin_label == NULL) {
        return;
    }
    char text[24] = {0};
    snprintf(text, sizeof(text), "%d K", popup->kelvin);
    lv_label_set_text(popup->kelvin_label, text);
}

static void light_popup_update_rgb_preview(light_popup_ctx_t *popup)
{
    if (popup == NULL) {
        return;
    }

    if (popup->preview != NULL) {
        lv_obj_set_style_bg_color(popup->preview, lv_color_hex(popup->rgb), LV_PART_MAIN);
    }
    if (popup->rgb_button != NULL) {
        uint32_t contrast =
            ((uint32_t)light_rgb_component(popup->rgb, 16) * 299U) +
            ((uint32_t)light_rgb_component(popup->rgb, 8) * 587U) +
            ((uint32_t)light_rgb_component(popup->rgb, 0) * 114U);
        uint32_t text_color = (contrast > 150000U) ? 0x1B1B1B : APP_UI_COLOR_TEXT_PRIMARY;
        lv_obj_set_style_bg_color(popup->rgb_button, lv_color_hex(popup->rgb), LV_PART_MAIN);
        lv_obj_set_style_text_color(popup->rgb_button, lv_color_hex(text_color), LV_PART_MAIN);
        lv_obj_t *label = lv_obj_get_child(popup->rgb_button, 0);
        if (label != NULL) {
            lv_obj_set_style_text_color(label, lv_color_hex(text_color), LV_PART_MAIN);
        }
    }
    if (popup->rgb_label != NULL) {
        char text[40] = {0};
        snprintf(text, sizeof(text), "#%06" PRIX32, popup->rgb & 0xFFFFFFU);
        lv_label_set_text(popup->rgb_label, text);
    }
    if (popup->rgb_slider != NULL) {
        lv_slider_set_value(
            popup->rgb_slider, light_clamp_int(popup->hue, LIGHT_RGB_HUE_MIN, LIGHT_RGB_HUE_MAX), LV_ANIM_OFF);
    }
}

static void light_popup_show_band(light_popup_ctx_t *popup, light_popup_band_mode_t mode)
{
    if (popup == NULL) {
        return;
    }
    if (mode == LIGHT_POPUP_BAND_RGB && (popup->tile == NULL || !popup->tile->can_color)) {
        mode = LIGHT_POPUP_BAND_KELVIN;
    }
    if (mode == LIGHT_POPUP_BAND_KELVIN && (popup->tile == NULL || !popup->tile->can_color_temp)) {
        mode = LIGHT_POPUP_BAND_RGB;
    }

    popup->band_mode = mode;
    bool show_kelvin = mode == LIGHT_POPUP_BAND_KELVIN;
    bool show_rgb = mode == LIGHT_POPUP_BAND_RGB;

    if (popup->band_label != NULL) {
        lv_label_set_text(popup->band_label, show_kelvin ? ui_i18n_get("light.color_temperature", "Color temperature") :
                                                           ui_i18n_get("light.rgb_color", "RGB color"));
    }
    if (popup->kelvin_label != NULL) {
        if (show_kelvin) {
            lv_obj_clear_flag(popup->kelvin_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->kelvin_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->rgb_label != NULL) {
        if (show_rgb) {
            lv_obj_clear_flag(popup->rgb_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->rgb_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->temp_slider != NULL) {
        if (show_kelvin) {
            lv_obj_clear_flag(popup->temp_slider, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->temp_slider, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->warm_label != NULL) {
        if (show_kelvin) {
            lv_obj_clear_flag(popup->warm_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->warm_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->cool_label != NULL) {
        if (show_kelvin) {
            lv_obj_clear_flag(popup->cool_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->cool_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->rgb_band != NULL) {
        if (show_rgb) {
            lv_obj_clear_flag(popup->rgb_band, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->rgb_band, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (popup->rgb_slider != NULL) {
        if (show_rgb) {
            lv_obj_clear_flag(popup->rgb_slider, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(popup->rgb_slider, LV_OBJ_FLAG_HIDDEN);
        }
    }

    light_popup_set_round_button_active(popup->temp_button, show_kelvin);
    light_popup_set_round_button_active(popup->rgb_button, show_rgb);
}

static void light_popup_apply_kelvin(light_popup_ctx_t *popup)
{
    if (popup == NULL || popup->tile == NULL) {
        return;
    }
    esp_err_t err = ui_bindings_set_light_color_temp_kelvin(popup->tile->entity_id, popup->kelvin);
    if (err == ESP_OK) {
        popup->tile->is_on = true;
        popup->tile->unavailable = false;
        if (popup->tile->brightness <= 0) {
            popup->tile->brightness = 100;
        }
        popup->tile->has_rgb_color = false;
        popup->tile->rgb_color = APP_UI_COLOR_LIGHT_ICON_ON;
        popup->tile->has_color_temp_kelvin = true;
        popup->tile->color_temp_kelvin = popup->kelvin;
        if (popup->tile->card != NULL) {
            light_apply_visual(popup->tile->card, popup->tile, true, popup->tile->brightness, "ON");
        }
    }
}

static void light_popup_apply_rgb(light_popup_ctx_t *popup)
{
    if (popup == NULL || popup->tile == NULL) {
        return;
    }
    uint8_t r = light_rgb_component(popup->rgb, 16);
    uint8_t g = light_rgb_component(popup->rgb, 8);
    uint8_t b = light_rgb_component(popup->rgb, 0);
    esp_err_t err = ui_bindings_set_light_rgb_color(popup->tile->entity_id, r, g, b);
    if (err == ESP_OK) {
        popup->tile->is_on = true;
        popup->tile->unavailable = false;
        if (popup->tile->brightness <= 0) {
            popup->tile->brightness = 100;
        }
        popup->tile->has_rgb_color = true;
        popup->tile->rgb_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        if (popup->tile->card != NULL) {
            light_apply_visual(popup->tile->card, popup->tile, true, popup->tile->brightness, "ON");
        }
    }
}

static void light_popup_delete_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }
    light_popup_ctx_t *popup = (light_popup_ctx_t *)lv_event_get_user_data(event);
    if (popup == NULL) {
        return;
    }
    if (popup->tile != NULL && popup->tile->popup_overlay == popup->overlay) {
        popup->tile->popup_overlay = NULL;
    }
    free(popup);
}

static void light_popup_close_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    light_popup_ctx_t *popup = (light_popup_ctx_t *)lv_event_get_user_data(event);
    if (popup != NULL && popup->overlay != NULL) {
        lv_obj_del(popup->overlay);
    }
}

static void light_popup_apply_kelvin_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        light_popup_show_band((light_popup_ctx_t *)lv_event_get_user_data(event), LIGHT_POPUP_BAND_KELVIN);
    }
}

static void light_popup_apply_rgb_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        light_popup_show_band((light_popup_ctx_t *)lv_event_get_user_data(event), LIGHT_POPUP_BAND_RGB);
    }
}

static void light_popup_temp_slider_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }
    light_popup_ctx_t *popup = (light_popup_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    if (popup == NULL || slider == NULL) {
        return;
    }
    popup->kelvin = light_clamp_int(lv_slider_get_value(slider), popup->min_kelvin, popup->max_kelvin);
    light_popup_update_kelvin_label(popup);
    if (code == LV_EVENT_RELEASED) {
        light_popup_apply_kelvin(popup);
    }
}

static void light_popup_rgb_slider_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }
    light_popup_ctx_t *popup = (light_popup_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    if (popup == NULL || slider == NULL) {
        return;
    }
    popup->hue = light_clamp_int(lv_slider_get_value(slider), LIGHT_RGB_HUE_MIN, LIGHT_RGB_HUE_MAX);
    popup->rgb = light_hue_to_rgb(popup->hue);
    light_popup_update_rgb_preview(popup);
    if (code == LV_EVENT_RELEASED) {
        light_popup_apply_rgb(popup);
    }
}

static void light_popup_set_kelvin_percent(light_popup_ctx_t *popup, int percent)
{
    if (popup == NULL) {
        return;
    }
    percent = light_clamp_int(percent, 0, 100);
    int span = popup->max_kelvin - popup->min_kelvin;
    popup->kelvin = popup->min_kelvin + ((span * percent + 50) / 100);
    popup->kelvin = light_clamp_int(popup->kelvin, popup->min_kelvin, popup->max_kelvin);
    if (popup->temp_slider != NULL) {
        lv_slider_set_value(popup->temp_slider, popup->kelvin, LV_ANIM_OFF);
    }
    light_popup_update_kelvin_label(popup);
}

static void light_popup_swatch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    light_popup_ctx_t *popup = (light_popup_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *swatch = lv_event_get_target(event);
    if (popup == NULL || swatch == NULL) {
        return;
    }
    const light_popup_preset_t *preset = (const light_popup_preset_t *)lv_obj_get_user_data(swatch);
    if (preset == NULL) {
        return;
    }
    if (preset->type == LIGHT_POPUP_PRESET_KELVIN) {
        light_popup_set_kelvin_percent(popup, preset->kelvin_percent);
        light_popup_show_band(popup, LIGHT_POPUP_BAND_KELVIN);
        light_popup_apply_kelvin(popup);
        return;
    }

    popup->rgb = preset->rgb & 0xFFFFFFU;
    popup->hue = light_rgb_to_hue(popup->rgb);
    light_popup_update_rgb_preview(popup);
    light_popup_show_band(popup, LIGHT_POPUP_BAND_RGB);
    light_popup_apply_rgb(popup);
}

static lv_obj_t *light_popup_make_round_label_button(lv_obj_t *parent, const char *text, uint32_t bg_color, uint32_t text_color)
{
    lv_obj_t *button = lv_btn_create(parent);
    light_popup_style_round_button(button, bg_color, text_color);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *light_popup_make_swatch(lv_obj_t *parent, const light_popup_preset_t *preset, light_popup_ctx_t *popup)
{
    lv_obj_t *swatch = lv_btn_create(parent);
    lv_obj_set_size(swatch, 42, 42);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(preset != NULL ? preset->display_color : 0xFFFFFFU), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(swatch, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(swatch, 0, LV_PART_MAIN);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_user_data(swatch, (void *)preset);
    lv_obj_add_event_cb(swatch, light_popup_swatch_event_cb, LV_EVENT_CLICKED, popup);
    return swatch;
}

#if !APP_HAVE_LIGHT_RGB_SLIDER_IMAGE
static void light_popup_add_rgb_band_segments(lv_obj_t *band, int panel_w)
{
    if (band == NULL || panel_w <= 0) {
        return;
    }
    static const uint32_t colors[] = {
        0xFF3B30, 0xFFCC00, 0x34C759, 0x00C7FF, 0x387BFF, 0xC76BFF,
    };
    int segment_count = (int)(sizeof(colors) / sizeof(colors[0]));
    for (int i = 0; i < segment_count; i++) {
        lv_obj_t *segment = lv_obj_create(band);
        int x = (panel_w * i) / segment_count;
        int next_x = (panel_w * (i + 1)) / segment_count;
        lv_obj_set_size(segment, next_x - x + 1, 76);
        lv_obj_set_pos(segment, x, 0);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(segment, lv_color_hex(colors[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(segment, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(segment, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(segment, 0, LV_PART_MAIN);
    }
}
#endif

static void light_popup_add_band_panel(light_popup_ctx_t *popup, lv_obj_t *card, int panel_w, int y)
{
    popup->band_panel = lv_obj_create(card);
    lv_obj_set_size(popup->band_panel, panel_w, 142);
    lv_obj_align(popup->band_panel, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_clear_flag(popup->band_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(popup->band_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(popup->band_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup->band_panel, 0, LV_PART_MAIN);

    popup->band_label = lv_label_create(popup->band_panel);
    lv_obj_set_style_text_font(popup->band_label, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(popup->band_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(popup->band_label, LV_ALIGN_TOP_LEFT, 0, 0);

    popup->kelvin_label = lv_label_create(popup->band_panel);
    lv_obj_set_style_text_font(popup->kelvin_label, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(popup->kelvin_label, lv_color_hex(APP_UI_COLOR_LIGHT_IND_ON), LV_PART_MAIN);
    lv_obj_align(popup->kelvin_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    light_popup_update_kelvin_label(popup);

    if (popup->tile != NULL && popup->tile->can_color) {
        popup->rgb_label = lv_label_create(popup->band_panel);
        lv_obj_set_style_text_font(popup->rgb_label, APP_FONT_TEXT_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(popup->rgb_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_obj_align(popup->rgb_label, LV_ALIGN_TOP_RIGHT, 0, 2);
    }

    if (popup->tile != NULL && popup->tile->can_color_temp) {
        popup->temp_slider = lv_slider_create(popup->band_panel);
        lv_obj_set_size(popup->temp_slider, panel_w, 76);
        lv_obj_align(popup->temp_slider, LV_ALIGN_TOP_MID, 0, 46);
        lv_obj_clear_flag(popup->temp_slider, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_slider_set_range(popup->temp_slider, popup->min_kelvin, popup->max_kelvin);
        lv_slider_set_value(popup->temp_slider, popup->kelvin, LV_ANIM_OFF);
        lv_obj_set_style_radius(popup->temp_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(popup->temp_slider, lv_color_hex(0xFF9828), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(popup->temp_slider, lv_color_hex(0xFFF8EE), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(popup->temp_slider, LV_GRAD_DIR_HOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->temp_slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->temp_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(popup->temp_slider, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(popup->temp_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(popup->temp_slider, LV_OPA_90, LV_PART_KNOB);
        lv_obj_set_style_radius(popup->temp_slider, 10, LV_PART_KNOB);
        lv_obj_set_style_transform_width(popup->temp_slider, -4, LV_PART_KNOB);
        lv_obj_set_style_border_width(popup->temp_slider, 0, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(popup->temp_slider, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(popup->temp_slider, light_popup_temp_slider_event_cb, LV_EVENT_VALUE_CHANGED, popup);
        lv_obj_add_event_cb(popup->temp_slider, light_popup_temp_slider_event_cb, LV_EVENT_RELEASED, popup);

        popup->warm_label = lv_label_create(popup->band_panel);
        lv_label_set_text(popup->warm_label, ui_i18n_get("light.warm", "Warm"));
        lv_obj_set_style_text_font(popup->warm_label, APP_FONT_TEXT_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(popup->warm_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_align(popup->warm_label, LV_ALIGN_TOP_LEFT, 0, 126);

        popup->cool_label = lv_label_create(popup->band_panel);
        lv_label_set_text(popup->cool_label, ui_i18n_get("light.cool", "Cool"));
        lv_obj_set_style_text_font(popup->cool_label, APP_FONT_TEXT_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(popup->cool_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_align(popup->cool_label, LV_ALIGN_TOP_RIGHT, 0, 126);
    }

    if (popup->tile != NULL && popup->tile->can_color) {
#if APP_HAVE_LIGHT_RGB_SLIDER_IMAGE
        popup->rgb_band = lv_obj_create(popup->band_panel);
        lv_obj_set_size(popup->rgb_band, panel_w, 76);
        lv_obj_align(popup->rgb_band, LV_ALIGN_TOP_MID, 0, 46);
        lv_obj_clear_flag(popup->rgb_band, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(popup->rgb_band, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(popup->rgb_band, true, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->rgb_band, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(popup->rgb_band, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(popup->rgb_band, 0, LV_PART_MAIN);

        lv_obj_t *rgb_band_image = lv_image_create(popup->rgb_band);
        lv_obj_set_size(rgb_band_image, panel_w, 76);
        lv_obj_clear_flag(rgb_band_image, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(rgb_band_image, &visiblecolor);
        lv_image_set_inner_align(rgb_band_image, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_center(rgb_band_image);
#else
        popup->rgb_band = lv_obj_create(popup->band_panel);
        lv_obj_set_size(popup->rgb_band, panel_w, 76);
        lv_obj_align(popup->rgb_band, LV_ALIGN_TOP_MID, 0, 46);
        lv_obj_clear_flag(popup->rgb_band, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(popup->rgb_band, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(popup->rgb_band, true, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->rgb_band, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(popup->rgb_band, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(popup->rgb_band, 0, LV_PART_MAIN);
        light_popup_add_rgb_band_segments(popup->rgb_band, panel_w);
#endif

        popup->rgb_slider = lv_slider_create(popup->band_panel);
        lv_obj_set_size(popup->rgb_slider, panel_w, 76);
        lv_obj_align(popup->rgb_slider, LV_ALIGN_TOP_MID, 0, 46);
        lv_obj_clear_flag(popup->rgb_slider, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_slider_set_range(popup->rgb_slider, LIGHT_RGB_HUE_MIN, LIGHT_RGB_HUE_MAX);
        lv_slider_set_value(popup->rgb_slider, popup->hue, LV_ANIM_OFF);
        lv_obj_set_style_radius(popup->rgb_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->rgb_slider, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(popup->rgb_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(popup->rgb_slider, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(popup->rgb_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(popup->rgb_slider, LV_OPA_90, LV_PART_KNOB);
        lv_obj_set_style_radius(popup->rgb_slider, 10, LV_PART_KNOB);
        lv_obj_set_style_transform_width(popup->rgb_slider, -4, LV_PART_KNOB);
        lv_obj_set_style_border_width(popup->rgb_slider, 0, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(popup->rgb_slider, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(popup->rgb_slider, light_popup_rgb_slider_event_cb, LV_EVENT_VALUE_CHANGED, popup);
        lv_obj_add_event_cb(popup->rgb_slider, light_popup_rgb_slider_event_cb, LV_EVENT_RELEASED, popup);
    }

    light_popup_update_rgb_preview(popup);
}

static void light_popup_add_preset_panel(light_popup_ctx_t *popup, lv_obj_t *card, int panel_w, int y, int panel_h)
{
    popup->preset_panel = lv_obj_create(card);
    lv_obj_set_size(popup->preset_panel, panel_w, panel_h);
    lv_obj_align(popup->preset_panel, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_clear_flag(popup->preset_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(popup->preset_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(popup->preset_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup->preset_panel, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(popup->preset_panel);
    lv_label_set_text(label, ui_i18n_get("light.presets", "Presets"));
    lv_obj_set_style_text_font(label, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 7);

    if (popup->tile != NULL && popup->tile->can_color) {
        popup->preview = lv_obj_create(popup->preset_panel);
        lv_obj_set_size(popup->preview, 44, 44);
        lv_obj_align(popup->preview, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_radius(popup->preview, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(popup->preview, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(popup->preview, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
        lv_obj_clear_flag(popup->preview, LV_OBJ_FLAG_SCROLLABLE);
    }

    static const light_popup_preset_t kelvin_presets[] = {
        {LIGHT_POPUP_PRESET_KELVIN, 0xFF8500, 0x000000, 0},
        {LIGHT_POPUP_PRESET_KELVIN, 0xFDB77E, 0x000000, 33},
        {LIGHT_POPUP_PRESET_KELVIN, 0xF5DDCE, 0x000000, 66},
        {LIGHT_POPUP_PRESET_KELVIN, 0xFFFDF7, 0x000000, 100},
    };
    static const light_popup_preset_t rgb_presets[] = {
        {LIGHT_POPUP_PRESET_RGB, 0x7BAAFF, 0x7BAAFF, 0},
        {LIGHT_POPUP_PRESET_RGB, 0xCB7BFA, 0xCB7BFA, 0},
        {LIGHT_POPUP_PRESET_RGB, 0xF58CE0, 0xF58CE0, 0},
        {LIGHT_POPUP_PRESET_RGB, 0xFF6656, 0xFF6656, 0},
    };
    const int swatch_size = 42;
    const int swatch_step = 56;
    int swatch_y = 62;
    if (popup->tile != NULL && popup->tile->can_color_temp) {
        const size_t kelvin_count = sizeof(kelvin_presets) / sizeof(kelvin_presets[0]);
        const int row_w = ((int)kelvin_count - 1) * swatch_step + swatch_size;
        int row_x = (panel_w - row_w) / 2;
        if (row_x < 0) {
            row_x = 0;
        }
        for (size_t i = 0; i < kelvin_count; i++) {
            lv_obj_t *swatch = light_popup_make_swatch(popup->preset_panel, &kelvin_presets[i], popup);
            lv_obj_align(swatch, LV_ALIGN_TOP_LEFT, row_x + ((int)i * swatch_step), swatch_y);
        }
        swatch_y += swatch_step;
    }
    if (popup->tile != NULL && popup->tile->can_color) {
        const size_t rgb_count = sizeof(rgb_presets) / sizeof(rgb_presets[0]);
        const int row_w = ((int)rgb_count - 1) * swatch_step + swatch_size;
        int row_x = (panel_w - row_w) / 2;
        if (row_x < 0) {
            row_x = 0;
        }
        for (size_t i = 0; i < rgb_count; i++) {
            lv_obj_t *swatch = light_popup_make_swatch(popup->preset_panel, &rgb_presets[i], popup);
            lv_obj_align(swatch, LV_ALIGN_TOP_LEFT, row_x + ((int)i * swatch_step), swatch_y);
        }
    }

    light_popup_update_rgb_preview(popup);
}

static void w_light_tile_open_color_popup(w_light_tile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->unavailable || (!ctx->can_color && !ctx->can_color_temp)) {
        return;
    }
    if (ctx->popup_overlay != NULL) {
        return;
    }

    light_popup_ctx_t *popup = ui_calloc_prefer_psram(1, sizeof(*popup));
    if (popup == NULL) {
        return;
    }
    popup->tile = ctx;
    popup->min_kelvin = ctx->min_color_temp_kelvin > 0 ? ctx->min_color_temp_kelvin : 2000;
    popup->max_kelvin = ctx->max_color_temp_kelvin > 0 ? ctx->max_color_temp_kelvin : 6500;
    if (popup->min_kelvin > popup->max_kelvin) {
        int tmp = popup->min_kelvin;
        popup->min_kelvin = popup->max_kelvin;
        popup->max_kelvin = tmp;
    }
    popup->kelvin = ctx->has_color_temp_kelvin ? ctx->color_temp_kelvin : ((popup->min_kelvin + popup->max_kelvin) / 2);
    popup->kelvin = light_clamp_int(popup->kelvin, popup->min_kelvin, popup->max_kelvin);
    popup->rgb = ctx->has_rgb_color ? (ctx->rgb_color & 0xFFFFFFU) : 0xFF8500U;
    popup->hue = light_rgb_to_hue(popup->rgb);

    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen);
    int screen_h = lv_obj_get_height(screen);
    if (screen_w <= 0) {
        screen_w = 720;
    }
    if (screen_h <= 0) {
        screen_h = 720;
    }

    popup->overlay = lv_obj_create(lv_layer_top());
    ctx->popup_overlay = popup->overlay;
    lv_obj_set_size(popup->overlay, screen_w, screen_h);
    lv_obj_align(popup->overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(popup->overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(popup->overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(popup->overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(popup->overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup->overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(popup->overlay, light_popup_delete_event_cb, LV_EVENT_DELETE, popup);

    int card_w = screen_w - 48;
    if (card_w > 560) {
        card_w = 560;
    }
    if (card_w < 280) {
        card_w = 280;
    }
    if (card_w > screen_w - 16) {
        card_w = screen_w - 16;
    }

    const int preset_panel_h = (ctx->can_color && ctx->can_color_temp) ? 180 : 124;
    int card_h = 142 + 156 + preset_panel_h + 16;
    if (card_h < 250) {
        card_h = 250;
    }
    if (card_h > screen_h - 16) {
        card_h = screen_h - 16;
    }

    lv_obj_t *card = lv_obj_create(popup->overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ui_i18n_get("light.color_title", "Light color"));
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *close = light_popup_make_round_label_button(card, "X", APP_UI_COLOR_NAV_BTN_BG_IDLE, APP_UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_size(close, 38, 38);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -3);
    lv_obj_add_event_cb(close, light_popup_close_event_cb, LV_EVENT_CLICKED, popup);

    int controls_x = 0;
    const int controls_y = 48;
    if (ctx->can_color_temp) {
        popup->temp_button = light_popup_make_round_label_button(card, "K", 0xFFF8EE, 0x1B1B1B);
        lv_obj_align(popup->temp_button, LV_ALIGN_TOP_LEFT, controls_x, controls_y);
        lv_obj_set_style_bg_grad_color(popup->temp_button, lv_color_hex(0xFF9828), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(popup->temp_button, LV_GRAD_DIR_HOR, LV_PART_MAIN);
        lv_obj_add_event_cb(popup->temp_button, light_popup_apply_kelvin_event_cb, LV_EVENT_CLICKED, popup);
        controls_x += 58;
    }
    if (ctx->can_color) {
        popup->rgb_button = light_popup_make_round_label_button(card, "RGB", popup->rgb, APP_UI_COLOR_TEXT_PRIMARY);
        lv_obj_align(popup->rgb_button, LV_ALIGN_TOP_LEFT, controls_x, controls_y);
        lv_obj_add_event_cb(popup->rgb_button, light_popup_apply_rgb_event_cb, LV_EVENT_CLICKED, popup);
    }

    int panel_w = card_w - 36;
    if (panel_w < 220) {
        panel_w = 220;
    }
    int content_y = 112;
    light_popup_add_band_panel(popup, card, panel_w, content_y);
    content_y += 156;
    light_popup_add_preset_panel(popup, card, panel_w, content_y, preset_panel_h);
    light_popup_show_band(popup, ctx->can_color_temp ? LIGHT_POPUP_BAND_KELVIN : LIGHT_POPUP_BAND_RGB);
}

static void w_light_tile_color_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        w_light_tile_open_color_popup((w_light_tile_ctx_t *)lv_event_get_user_data(event));
    }
}

static void w_light_tile_card_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    w_light_tile_ctx_t *ctx = (w_light_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (ctx->unavailable) {
            return;
        }

        lv_obj_t *card = lv_event_get_target(event);
        bool prev_is_on = ctx->is_on;
        int prev_brightness = ctx->brightness;
        bool prev_unavailable = ctx->unavailable;

        bool next_is_on = !ctx->is_on;
        int next_brightness = ctx->brightness;
        if (next_is_on && next_brightness <= 0) {
            next_brightness = 100;
        }
        if (next_brightness < 0) {
            next_brightness = 0;
        }
        next_brightness = clamp_percent(next_brightness);

        esp_err_t err = ui_bindings_set_entity_power(ctx->entity_id, next_is_on);
        if (err == ESP_OK) {
            ctx->is_on = next_is_on;
            ctx->brightness = next_brightness;
            ctx->unavailable = false;
            light_apply_visual(card, ctx, ctx->is_on, ctx->brightness, ctx->is_on ? "ON" : "OFF");
        } else {
            ctx->is_on = prev_is_on;
            ctx->brightness = prev_brightness;
            ctx->unavailable = prev_unavailable;
            light_apply_visual(card, ctx, ctx->is_on, ctx->brightness, ctx->unavailable ? "unavailable" :
                (ctx->is_on ? "ON" : "OFF"));
        }
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        lv_obj_t *card = lv_event_get_target(event);
        light_apply_visual(card, ctx, ctx->is_on, ctx->brightness, ctx->unavailable ? "unavailable" : (ctx->is_on ? "ON" : "OFF"));
    } else if (code == LV_EVENT_DELETE) {
        if (ctx->popup_overlay != NULL) {
            lv_obj_t *overlay = ctx->popup_overlay;
            ctx->popup_overlay = NULL;
            lv_obj_del(overlay);
        }
        free(ctx);
    }
}

static void w_light_tile_slider_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }

    w_light_tile_ctx_t *ctx = (w_light_tile_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);
    lv_obj_t *card = (slider != NULL) ? lv_obj_get_parent(slider) : NULL;
    lv_obj_t *value_label = (card != NULL) ? lv_obj_get_child(card, 4) : NULL;
    int value = (slider != NULL) ? lv_slider_get_value(slider) : 0;

    if (ctx != NULL && !ctx->can_dim) {
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (ctx != NULL) {
            ctx->brightness = clamp_percent(value);
        }
        light_set_value_label(value_label, value);
        return;
    }

    if (ctx != NULL) {
        bool prev_is_on = ctx->is_on;
        int prev_brightness = ctx->brightness;
        bool prev_unavailable = ctx->unavailable;

        int next_brightness = clamp_percent(value);
        bool next_is_on = (next_brightness > 0);

        esp_err_t err = ui_bindings_set_slider_value(ctx->entity_id, next_brightness);
        if (err == ESP_OK) {
            ctx->brightness = next_brightness;
            ctx->is_on = next_is_on;
            ctx->unavailable = false;
            if (card != NULL) {
                light_apply_visual(card, ctx, ctx->is_on, ctx->brightness, ctx->is_on ? "ON" : "OFF");
            }
        } else {
            ctx->is_on = prev_is_on;
            ctx->brightness = prev_brightness;
            ctx->unavailable = prev_unavailable;
            if (card != NULL) {
                light_apply_visual(card, ctx, ctx->is_on, ctx->brightness, ctx->unavailable ? "unavailable" :
                    (ctx->is_on ? "ON" : "OFF"));
            }
        }
    }
}

esp_err_t w_light_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
#if APP_UI_REWORK_V2
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
#else
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
#endif
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    lv_obj_t *icon = lv_label_create(card);
    lv_coord_t configured_min_dim = (def->w < def->h) ? def->w : def->h;
    const lv_font_t *icon_font = light_icon_font_for_min_dim(configured_min_dim);
    lv_label_set_text(icon, light_icon_text_for_font(icon_font));
    lv_obj_set_width(icon, def->w);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(icon, icon_font, LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    lv_obj_set_width(title, def->w);
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -46);

    lv_obj_t *state_label = lv_label_create(card);
    lv_label_set_text(state_label, ui_i18n_get("common.off", "OFF"));
    lv_obj_set_style_text_font(state_label, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_align(state_label, LV_ALIGN_TOP_LEFT, 0, 2);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_width(slider, def->w);
    lv_obj_set_height(slider, 13);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "0 %");
    lv_obj_set_style_text_font(value_label, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, 0, 2);

    lv_obj_t *color_button = lv_btn_create(card);
    lv_obj_set_size(color_button, light_color_button_size_for_class(light_tile_class_from_dim(configured_min_dim)),
        light_color_button_size_for_class(light_tile_class_from_dim(configured_min_dim)));
    lv_obj_set_style_radius(color_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_flag(color_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(color_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(color_button, LV_ALIGN_TOP_RIGHT, 0, 30);

    lv_obj_t *color_button_image = lv_image_create(color_button);
    lv_coord_t create_button_size = light_color_button_size_for_class(light_tile_class_from_dim(configured_min_dim));
    lv_obj_set_size(color_button_image, create_button_size, create_button_size);
    lv_obj_clear_flag(color_button_image, LV_OBJ_FLAG_CLICKABLE);
#if APP_HAVE_LIGHT_COLOR_BUTTON_IMAGES
    lv_image_set_src(color_button_image, &Temp);
    lv_image_set_inner_align(color_button_image, LV_IMAGE_ALIGN_CONTAIN);
#else
    lv_obj_add_flag(color_button_image, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_center(color_button_image);

    w_light_tile_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_light_tile_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->card = card;
    ctx->popup_overlay = NULL;
    ctx->is_on = false;
    ctx->brightness = 0;
    ctx->unavailable = false;
    ctx->can_dim = true;
    ctx->can_color = false;
    ctx->can_color_temp = false;
    ctx->has_rgb_color = false;
    ctx->rgb_color = APP_UI_COLOR_LIGHT_ICON_ON;
    ctx->has_color_temp_kelvin = false;
    ctx->color_temp_kelvin = 3000;
    ctx->min_color_temp_kelvin = 2000;
    ctx->max_color_temp_kelvin = 6500;
    ctx->configured_min_dim = configured_min_dim;

    lv_obj_add_event_cb(card, w_light_tile_card_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(card, w_light_tile_card_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, w_light_tile_card_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(color_button, w_light_tile_color_button_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(slider, w_light_tile_slider_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(slider, w_light_tile_slider_event_cb, LV_EVENT_RELEASED, ctx);

    light_apply_visual(card, ctx, false, 0, "OFF");
    out_instance->ctx = ctx;
    out_instance->obj = card;
    return ESP_OK;
}

void w_light_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }
    bool is_on = light_state_is_on(state->state);
    ha_light_capabilities_t caps = {0};
    ha_light_capabilities_from_state(state, &caps);
    int brightness = caps.brightness_percent;
    w_light_tile_ctx_t *ctx = (w_light_tile_ctx_t *)instance->ctx;
    if (ctx != NULL) {
        ctx->is_on = is_on;
        ctx->brightness = brightness;
        ctx->unavailable = false;
        ctx->can_dim = caps.can_dim;
        ctx->can_color = caps.can_color;
        ctx->can_color_temp = caps.can_color_temp;
        ctx->has_rgb_color = caps.has_rgb_color;
        ctx->rgb_color = caps.has_rgb_color ? caps.rgb_color : APP_UI_COLOR_LIGHT_ICON_ON;
        ctx->has_color_temp_kelvin = caps.has_color_temp_kelvin;
        ctx->color_temp_kelvin = caps.has_color_temp_kelvin ? caps.color_temp_kelvin : ctx->color_temp_kelvin;
        ctx->min_color_temp_kelvin = caps.min_color_temp_kelvin > 0 ? caps.min_color_temp_kelvin : 2000;
        ctx->max_color_temp_kelvin = caps.max_color_temp_kelvin > 0 ? caps.max_color_temp_kelvin : 6500;
    }
    light_apply_visual(instance->obj, ctx, is_on, brightness, is_on ? "ON" : "OFF");
}

void w_light_tile_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }
    w_light_tile_ctx_t *ctx = (w_light_tile_ctx_t *)instance->ctx;
    if (ctx != NULL) {
        ctx->is_on = false;
        ctx->brightness = 0;
        ctx->unavailable = true;
    }
    light_apply_visual(instance->obj, ctx, false, 0, "unavailable");
}
