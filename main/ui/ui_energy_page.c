/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/display_geometry.h"
#include "ui/ui_energy_page.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

#include "ha/ha_client.h"
#include "ha/ha_energy_model.h"
#include "ha/ha_history_week.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_days_view.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#ifdef BETTA_SIMULATOR
#define ha_history_days_free free /* the simulator has no worker: its synthetic results are plain allocations */
#endif

#define ENERGY_ICON_FONT (mdi_font_energy_42())

/* Arrow codepoints of the arrow font (mdi_font_arrows_20): U+F0045 arrow-down, U+F004D arrow-left, U+F0054 arrow-right, U+F005D arrow-up */
#define ENERGY_ARROW_DOWN  "\xF3\xB0\x81\x85"  /* U+F0045 */
#define ENERGY_ARROW_LEFT  "\xF3\xB0\x81\x8D"  /* U+F004D */
#define ENERGY_ARROW_RIGHT "\xF3\xB0\x81\x94"  /* U+F0054 */
#define ENERGY_ARROW_UP    "\xF3\xB0\x81\x9D"  /* U+F005D */
/* Non-const wrapper so we can chain a fallback at runtime */
static lv_font_t s_arrow_value_font;
static bool s_arrow_value_font_ready = false;

/* The value font of the battery and grid nodes: the arrow glyphs of the arrow font followed by the value font as its fallback (the
 * struct copy shares the lv_tiny_ttf descriptor). Rebuilt for the current text step, which may change between UI builds. */
static int s_arrow_value_font_px;
static void energy_arrow_value_font_prepare(void)
{
    const lv_font_t *value_font = ui_font_role(UI_TEXT_VALUE);
    const lv_font_t *arrows = mdi_font_arrows_20();
    if (arrows == NULL) return;
    if (s_arrow_value_font_ready && s_arrow_value_font_px == ui_font_text_px_of(value_font)) return;
    s_arrow_value_font = *arrows;
    s_arrow_value_font.fallback = value_font;
    s_arrow_value_font.line_height = value_font->line_height;
    s_arrow_value_font.base_line = value_font->base_line;
    s_arrow_value_font_px = ui_font_text_px_of(value_font);
    s_arrow_value_font_ready = true;
}

/* MDI codepoints for energy node icons */
#define ENERGY_ICON_HOME      "\xF3\xB0\x8B\x9C"  /* U+F02DC home */
#define ENERGY_ICON_SOLAR     "\xF3\xB0\xA9\xB2"  /* U+F0A72 solar-power */
#define ENERGY_ICON_GRID      "\xF3\xB0\xB4\xBE"  /* U+F0D3E transmission-tower */
#define ENERGY_ICON_BATTERY   "\xF3\xB1\x8A\xA3"  /* U+F12A3 battery-high */
#define ENERGY_ICON_GAS       "\xF3\xB0\x88\xB8"  /* U+F0238 fire */
#define ENERGY_ICON_WATER     "\xF3\xB0\x96\x8E"  /* U+F058E water */

#define ENERGY_FLOW_COUNT 8
#define ENERGY_HOME_ARC_COUNT 3

#define ENERGY_NODE_GRID 0
#define ENERGY_NODE_SOLAR 1
#define ENERGY_NODE_HOME 2
#define ENERGY_NODE_BATTERY 3
#define ENERGY_NODE_GAS 4
#define ENERGY_NODE_WATER 5
#define ENERGY_NODE_COUNT 6

#define ENERGY_FLOW_GRID_HOME 0
#define ENERGY_FLOW_SOLAR_HOME 1
#define ENERGY_FLOW_SOLAR_GRID 2
#define ENERGY_FLOW_SOLAR_BATTERY 3
#define ENERGY_FLOW_BATTERY_HOME 4
#define ENERGY_FLOW_BATTERY_GRID 5
#define ENERGY_FLOW_GAS_HOME 6
#define ENERGY_FLOW_WATER_HOME 7

#define ENERGY_ARC_SOLAR 0
#define ENERGY_ARC_BATTERY 1
#define ENERGY_ARC_GRID 2

#define ENERGY_NODE_GRID_X 130
#define ENERGY_NODE_SOLAR_X 360
#define ENERGY_NODE_HOME_X 560
#define ENERGY_NODE_BATTERY_X 360
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280) /* the screen is 720 high instead of 800: the nodes sit closer together */
#define ENERGY_NODE_GRID_Y 281
#define ENERGY_NODE_SOLAR_Y 102 /* 98 before the node titles were placed by their line height: the title above needs the room */
#define ENERGY_NODE_HOME_Y 281
#define ENERGY_NODE_BATTERY_Y 462
#else
#define ENERGY_NODE_GRID_Y 300
#define ENERGY_NODE_SOLAR_Y 100
#define ENERGY_NODE_HOME_Y 300
#define ENERGY_NODE_BATTERY_Y 500
#endif
#define ENERGY_NODE_SIZE 128
#define ENERGY_HOME_SIZE 139
#define ENERGY_DOT_SIZE 16
#define ENERGY_ARC_RADIUS 50.0f
#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720) || defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
#define ENERGY_NODE_TITLE_WIDTH 190
#define ENERGY_NODE_TITLE_TOP_OFFSET 34
#define ENERGY_NODE_TITLE_BOTTOM_OFFSET 4
#else
#define ENERGY_NODE_TITLE_WIDTH 146
#define ENERGY_NODE_TITLE_TOP_OFFSET 26
#define ENERGY_NODE_TITLE_BOTTOM_OFFSET 8
#endif
#define ENERGY_NODE_BORDER_WIDTH 3
#define ENERGY_VALUE_INSET (APP_VIEW_WIDTH == 720 ? 8 : 18)
#define ENERGY_HOME_ARC_PAD 14
#define ENERGY_HOME_ARC_WIDTH 6
#define ENERGY_FLOW_LINE_WIDTH 3
#define ENERGY_VALUE_FONT ui_font_role(UI_TEXT_VALUE)
#define ENERGY_EXPONENT_FONT ui_font_role(UI_TEXT_META)
#define ENERGY_TITLE_FONT ui_font_role(UI_TEXT_LABEL)
#define ENERGY_STATS_FONT ui_font_role(UI_TEXT_META)
#define ENERGY_STATS_X 24
#define ENERGY_STATS_WIDTH 300
#define ENERGY_STATS_TODAY_Y (APP_VIEW_CONTENT_HEIGHT - 56)
#define ENERGY_STATS_AUTARKY_Y (APP_VIEW_CONTENT_HEIGHT - 32)

#define ENERGY_NODE_GAS_X    ENERGY_NODE_HOME_X
#define ENERGY_NODE_GAS_Y    ENERGY_NODE_SOLAR_Y
#define ENERGY_NODE_WATER_X  ENERGY_NODE_HOME_X
#define ENERGY_NODE_WATER_Y  ENERGY_NODE_BATTERY_Y

#define ENERGY_FLOW_MIN_VISIBLE_W 1.0f
#define ENERGY_KWH_MIN_VISIBLE 0.001f
#define ENERGY_KWH_VISUAL_SCALE 1000.0f

#define ENERGY_COLOR_GRID 0x039BEF
#define ENERGY_COLOR_RETURN 0xE45E65
#define ENERGY_COLOR_SOLAR 0xFF9800
#define ENERGY_COLOR_BATTERY_OUT 0x26A69A
#define ENERGY_COLOR_BATTERY_IN 0xE84393
#define ENERGY_COLOR_GAS      0xA0522D
#define ENERGY_COLOR_WATER    0x00BCD4
#define ENERGY_COLOR_LINE_IDLE 0x435566

#define ENERGY_SOURCE_HA "ha_energy"
#define ENERGY_SOURCE_MANUAL "manual_live"

typedef enum {
    ENERGY_SLOT_HOME_POWER = 0,
    ENERGY_SLOT_SOLAR_POWER,
    ENERGY_SLOT_GRID_POWER,
    ENERGY_SLOT_GRID_IMPORT_POWER,
    ENERGY_SLOT_GRID_EXPORT_POWER,
    ENERGY_SLOT_BATTERY_POWER,
    ENERGY_SLOT_BATTERY_CHARGE_POWER,
    ENERGY_SLOT_BATTERY_DISCHARGE_POWER,
    ENERGY_SLOT_BATTERY_SOC,
    ENERGY_SLOT_GAS,
    ENERGY_SLOT_WATER,
    ENERGY_SLOT_COUNT,
} energy_slot_id_t;

typedef struct {
    bool configured;
    bool valid;
    float value;
    char unit[APP_MAX_UNIT_LEN];
} energy_value_slot_t;

typedef struct {
    lv_obj_t *circle;
    lv_obj_t *icon_label;
    lv_obj_t *value_label;
    lv_obj_t *value_exponent_label;
    lv_obj_t *title_label;
    lv_font_t fitted_font; /* Per-node MDI wrapper: text fallback changes independently. */
} energy_node_t;

typedef struct {
    lv_obj_t *line;       /* straight flow: full line.  L-flow: pre-arc straight segment. */
    lv_obj_t *arc;        /* L-flow only: the rounded corner.  NULL-equivalent when hidden. */
    lv_obj_t *post_line;  /* L-flow only: post-arc straight segment. */
    lv_obj_t *dot;
    lv_point_precise_t points[28];
    lv_point_precise_t post_points[2];
    uint8_t point_count;
    bool has_arc;
    bool visible;
    bool reverse;
    float value_w;
    float phase;
    float length;
    lv_color_t color;
} energy_flow_t;

typedef struct {
    float used_solar;
    float used_grid;
    float used_battery;
    float used_total;
    float grid_to_battery;
    float battery_to_grid;
    float solar_to_battery;
    float solar_to_grid;
} energy_flow_values_t;

typedef struct {
    ui_energy_page_config_t config;
    lv_obj_t *root;
    lv_obj_t *stat_today_label;
    lv_obj_t *stat_autarky_label;
    energy_node_t nodes[ENERGY_NODE_COUNT];
    lv_obj_t *home_arcs[ENERGY_HOME_ARC_COUNT];
    energy_flow_t flows[ENERGY_FLOW_COUNT];
    energy_value_slot_t slots[ENERGY_SLOT_COUNT];
    lv_timer_t *anim_timer;
    int64_t last_anim_ms;
    /* seven-day details of Gas, Water and Solar: a popup, or (wide displays, config details "side") this panel */
    lv_obj_t *detail_panel;
    lv_obj_t *detail_title;
    lv_obj_t *detail_status;
    lv_obj_t *detail_body;
    int detail_node;            /* selected node, -1 for none */
    uint32_t detail_generation; /* the request whose answer may still be drawn */
} energy_page_ctx_t;

static bool energy_state_is_unavailable(const char *state_text)
{
    if (state_text == NULL || state_text[0] == '\0') {
        return true;
    }
    return strcmp(state_text, "unavailable") == 0 || strcmp(state_text, "unknown") == 0;
}

static bool energy_parse_float_relaxed(const char *text, float *out_value)
{
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return false;
    }

    char buf[40] = {0};
    size_t n = strnlen(text, sizeof(buf) - 1U);
    for (size_t i = 0; i < n; i++) {
        buf[i] = (text[i] == ',') ? '.' : text[i];
    }
    buf[n] = '\0';

    char *end = NULL;
    float parsed = strtof(buf, &end);
    if (end == buf) {
        return false;
    }
    *out_value = parsed;
    return true;
}

static void energy_copy_unit_from_attrs(char *dst, size_t dst_size, const char *attrs_json)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (attrs_json == NULL || attrs_json[0] == '\0') {
        return;
    }

    cJSON *attrs = cJSON_Parse(attrs_json);
    if (attrs == NULL) {
        return;
    }
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
        snprintf(dst, dst_size, "%s", unit->valuestring);
    }
    cJSON_Delete(attrs);
}

static float energy_normalize_power_w(float value, const char *unit)
{
    if (unit == NULL || unit[0] == '\0' || strcmp(unit, "W") == 0) {
        return value;
    }
    if (strcmp(unit, "kW") == 0 || strcmp(unit, "KW") == 0) {
        return value * 1000.0f;
    }
    if (strcmp(unit, "MW") == 0) {
        return value * 1000000.0f;
    }
    if (strcmp(unit, "mW") == 0) {
        return value / 1000.0f;
    }
    return value;
}

static void energy_format_power(char *dst, size_t dst_size, float watts)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    float abs_w = fabsf(watts);
    if (abs_w >= 10000.0f) {
        snprintf(dst, dst_size, "%.0f kW", (double)(watts / 1000.0f));
    } else if (abs_w >= 1000.0f) {
        snprintf(dst, dst_size, "%.1f kW", (double)(watts / 1000.0f));
    } else {
        snprintf(dst, dst_size, "%.0f W", (double)watts);
    }
}

static void energy_format_kwh(char *dst, size_t dst_size, float kwh)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    float abs_kwh = fabsf(kwh);
    if (abs_kwh >= 100.0f) {
        snprintf(dst, dst_size, "%.0f kWh", (double)kwh);
    } else if (abs_kwh >= 10.0f) {
        snprintf(dst, dst_size, "%.1f kWh", (double)kwh);
    } else if (abs_kwh >= 1.0f) {
        snprintf(dst, dst_size, "%.2f kWh", (double)kwh);
    } else {
        /* Below 1 kWh switch to Wh so small consumers stay readable
           (e.g. "45 Wh" instead of "0.05 kWh"). */
        float wh = kwh * 1000.0f;
        float abs_wh = fabsf(wh);
        if (abs_wh >= 100.0f) {
            snprintf(dst, dst_size, "%.0f Wh", (double)wh);
        } else if (abs_wh >= 10.0f) {
            snprintf(dst, dst_size, "%.1f Wh", (double)wh);
        } else {
            snprintf(dst, dst_size, "%.2f Wh", (double)wh);
        }
    }
}

static void energy_format_value_unit(char *dst, size_t dst_size, float value, const char *unit)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    const char *u = (unit != NULL && unit[0] != '\0') ? unit : "";
    float abs_val = fabsf(value);
    if (abs_val >= 100.0f) {
        snprintf(dst, dst_size, "%.0f %s", (double)value, u);
    } else if (abs_val >= 10.0f) {
        snprintf(dst, dst_size, "%.1f %s", (double)value, u);
    } else {
        snprintf(dst, dst_size, "%.2f %s", (double)value, u);
    }
}

static void energy_format_cubic_meter_value(char *dst, size_t dst_size, float value)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    float abs_val = fabsf(value);
    if (abs_val >= 100.0f) {
        snprintf(dst, dst_size, "%.0f m", (double)value);
    } else if (abs_val >= 10.0f) {
        snprintf(dst, dst_size, "%.1f m", (double)value);
    } else {
        snprintf(dst, dst_size, "%.2f m", (double)value);
    }
}

static const char *energy_entity_for_slot(const energy_page_ctx_t *ctx, energy_slot_id_t slot_id)
{
    if (ctx == NULL) {
        return "";
    }
    switch (slot_id) {
    case ENERGY_SLOT_HOME_POWER:
        return ctx->config.home_power_entity_id;
    case ENERGY_SLOT_SOLAR_POWER:
        return ctx->config.solar_power_entity_id;
    case ENERGY_SLOT_GRID_POWER:
        return ctx->config.grid_power_entity_id;
    case ENERGY_SLOT_GRID_IMPORT_POWER:
        return ctx->config.grid_import_power_entity_id;
    case ENERGY_SLOT_GRID_EXPORT_POWER:
        return ctx->config.grid_export_power_entity_id;
    case ENERGY_SLOT_BATTERY_POWER:
        return ctx->config.battery_power_entity_id;
    case ENERGY_SLOT_BATTERY_CHARGE_POWER:
        return ctx->config.battery_charge_power_entity_id;
    case ENERGY_SLOT_BATTERY_DISCHARGE_POWER:
        return ctx->config.battery_discharge_power_entity_id;
    case ENERGY_SLOT_BATTERY_SOC:
        return ctx->config.battery_soc_entity_id;
    case ENERGY_SLOT_GAS:
        return ctx->config.gas_entity_id;
    case ENERGY_SLOT_WATER:
        return ctx->config.water_entity_id;
    case ENERGY_SLOT_COUNT:
    default:
        return "";
    }
}

static bool energy_slot_is_configured(const energy_page_ctx_t *ctx, energy_slot_id_t slot_id)
{
    if (ctx == NULL) {
        return false;
    }
    /* With the Home Assistant energy source only the battery state of charge and
     * the live solar power are used (the flows come from HA's energy dashboard). */
    if (strcmp(ctx->config.source, ENERGY_SOURCE_MANUAL) != 0 && slot_id != ENERGY_SLOT_BATTERY_SOC &&
        slot_id != ENERGY_SLOT_SOLAR_POWER) {
        return false;
    }
    const char *entity_id = energy_entity_for_slot(ctx, slot_id);
    return entity_id != NULL && entity_id[0] != '\0';
}

static bool energy_has_grid_config(const energy_page_ctx_t *ctx)
{
    return energy_slot_is_configured(ctx, ENERGY_SLOT_GRID_POWER) ||
           energy_slot_is_configured(ctx, ENERGY_SLOT_GRID_IMPORT_POWER) ||
           energy_slot_is_configured(ctx, ENERGY_SLOT_GRID_EXPORT_POWER);
}

static bool energy_has_solar_config(const energy_page_ctx_t *ctx)
{
    return energy_slot_is_configured(ctx, ENERGY_SLOT_SOLAR_POWER);
}

static bool energy_has_battery_config(const energy_page_ctx_t *ctx)
{
    return energy_slot_is_configured(ctx, ENERGY_SLOT_BATTERY_POWER) ||
           energy_slot_is_configured(ctx, ENERGY_SLOT_BATTERY_CHARGE_POWER) ||
           energy_slot_is_configured(ctx, ENERGY_SLOT_BATTERY_DISCHARGE_POWER) ||
           energy_slot_is_configured(ctx, ENERGY_SLOT_BATTERY_SOC);
}

static void energy_set_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_set_hidden(obj, true);
    } else {
        lv_obj_set_hidden(obj, false);
    }
}

/* State of charge next to the battery node title ("Battery 87 %"). */
static void energy_update_battery_title(energy_page_ctx_t *ctx)
{
    char title[48];
    const energy_value_slot_t *soc = &ctx->slots[ENERGY_SLOT_BATTERY_SOC];
    if (soc->configured && soc->valid) {
        snprintf(title, sizeof(title), "%s %.0f %%", ui_i18n_get("energy.battery", "Battery"), (double)soc->value);
    } else {
        snprintf(title, sizeof(title), "%s", ui_i18n_get("energy.battery", "Battery"));
    }
    lv_label_set_text(ctx->nodes[ENERGY_NODE_BATTERY].title_label, title);
}

/* Live PV production next to the solar node title ("Solar 4230 W"), Home Assistant source only. */
static void energy_update_solar_title(energy_page_ctx_t *ctx)
{
    char title[48];
    const energy_value_slot_t *pv = &ctx->slots[ENERGY_SLOT_SOLAR_POWER];
    if (pv->configured && pv->valid) {
        snprintf(title, sizeof(title), "%s %.0f W", ui_i18n_get("energy.solar", "Solar"), (double)fmaxf(pv->value, 0.0f));
    } else {
        snprintf(title, sizeof(title), "%s", ui_i18n_get("energy.solar", "Solar"));
    }
    lv_label_set_text(ctx->nodes[ENERGY_NODE_SOLAR].title_label, title);
}

static void energy_set_node_hidden(energy_node_t *node, bool hidden)
{
    if (node == NULL) {
        return;
    }
    energy_set_obj_hidden(node->circle, hidden);
    energy_set_obj_hidden(node->title_label, hidden);
}

static bool energy_unit_is_cubic_meter(const char *unit)
{
    return unit != NULL && (strcmp(unit, "m\xC2\xB3") == 0 || strcmp(unit, "m3") == 0);
}

static void energy_style_text(lv_obj_t *label, lv_color_t color, const lv_font_t *font, lv_text_align_t align)
{
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
}

/* The energy page never went through any of the three rendering styles at all (found via the
 * widget gallery: Flat and Glow rendered pixel-identical, Glass differed only by the shared
 * page-background gradient every special page picks up, not by anything of its own) -- this is
 * the one place that matters most, since the node circles are the dominant visual element. Flat
 * keeps its original flat fill and plain border; Glow gets a raised look (a light-to-dark
 * gradient fill and a drop shadow, the same visual language as ui_style_button()); Glass
 * gets a white-tinted translucent fill and a soft shadow, matching the Glass card frosting of
 * ui_style_apply_card(). The accent color border that identifies which node this is (grid, solar, ...) stays
 * the same color in all styles -- only the fill/shadow treatment around it changes. */
static void energy_style_node_circle(lv_obj_t *circle, lv_color_t color)
{
    switch (ui_style_get()) {
    case UI_STYLE_FLAT:
    case UI_STYLE_MATTE:
        lv_obj_set_style_bg_color(circle, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(circle, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(circle, 0, LV_PART_MAIN);
        break;
    case UI_STYLE_GLASS:
        lv_obj_set_style_bg_color(circle, lv_color_mix(lv_color_white(), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 40), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(circle, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(circle, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(circle, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_offset_y(circle, 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(circle, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(circle, LV_OPA_30, LV_PART_MAIN);
        break;
    case UI_STYLE_GLOW:
        lv_obj_set_style_bg_color(circle, lv_color_mix(lv_color_white(), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 40), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(circle, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(circle, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(circle, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_offset_y(circle, 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(circle, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(circle, LV_OPA_40, LV_PART_MAIN);
        break;
    }
    lv_obj_set_style_border_color(circle, color, LV_PART_MAIN);
    lv_obj_set_style_border_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
}

static void energy_create_node(energy_page_ctx_t *ctx, int node_id, int cx, int cy, int size, const char *title,
    const char *icon_text, lv_color_t color)
{
    energy_node_t *node = &ctx->nodes[node_id];
    lv_obj_t *circle = lv_obj_create(ctx->root);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, size, size);
    lv_obj_set_pos(circle, cx - (size / 2), cy - (size / 2));
    lv_obj_set_style_radius(circle, size / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(circle, ENERGY_NODE_BORDER_WIDTH, LV_PART_MAIN);
    energy_style_node_circle(circle, color);
    lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(circle, false);
    lv_obj_set_clickable(circle, false);

    /* Layout icon and value as a vertically centered group inside the circle. */
    lv_obj_set_flex_flow(circle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(circle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(circle, 2, LV_PART_MAIN);

    lv_obj_t *icon = lv_label_create(circle);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_width(icon, size - 8);
    energy_style_text(icon, color, ENERGY_ICON_FONT, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *value = lv_label_create(circle);
    lv_label_set_text(value, "--");
    ui_text_record(value, UI_TEXT_VALUE, true);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(value, size - ENERGY_VALUE_INSET);
    energy_arrow_value_font_prepare();
    const lv_font_t *value_font = (node_id == ENERGY_NODE_BATTERY || node_id == ENERGY_NODE_GRID)
        ? &s_arrow_value_font
        : ENERGY_VALUE_FONT;
    energy_style_text(value, theme_default_color_text_primary(), value_font, LV_TEXT_ALIGN_CENTER);
    /* Battery and Grid values use per-line recolor markup to color the
     * direction arrow + value independently (HA-style). */
    if (node_id == ENERGY_NODE_BATTERY || node_id == ENERGY_NODE_GRID) {
        lv_label_set_recolor(value, true);
    }

    lv_obj_t *exponent = lv_label_create(circle);
    lv_label_set_text(exponent, "");
    lv_obj_set_ignore_layout(exponent, true);
    lv_obj_set_hidden(exponent, true);
    energy_style_text(exponent, theme_default_color_text_primary(), ENERGY_EXPONENT_FONT, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *title_label = lv_label_create(ctx->root);
    lv_label_set_text(title_label, title);
    ui_text_record(title_label, UI_TEXT_LABEL, true);
    lv_obj_set_width(title_label, ENERGY_NODE_TITLE_WIDTH);
    energy_style_text(title_label, theme_default_color_text_muted(), ENERGY_TITLE_FONT, LV_TEXT_ALIGN_CENTER);
    /* a title above its node sits one line plus 2 px higher (34 px at the former 32 px line), so larger text never touches the circle */
    const int title_above = (int)lv_font_get_line_height(ENERGY_TITLE_FONT) + 2 > ENERGY_NODE_TITLE_TOP_OFFSET
                                ? (int)lv_font_get_line_height(ENERGY_TITLE_FONT) + 2 : ENERGY_NODE_TITLE_TOP_OFFSET;
    int title_y = (cy < ENERGY_NODE_HOME_Y) ? (cy - (size / 2) - title_above)
                                            : (cy + (size / 2) + ENERGY_NODE_TITLE_BOTTOM_OFFSET);
    if (title_y < 0) title_y = 0; /* on the 720 px screen of the Tab5 the top nodes sit close to the edge: the title may touch its circle, it never leaves the page */
    lv_obj_set_pos(title_label, cx - (ENERGY_NODE_TITLE_WIDTH / 2), title_y);

    node->circle = circle;
    node->icon_label = icon;
    node->value_label = value;
    node->value_exponent_label = exponent;
    node->title_label = title_label;
}

static void energy_fit_node_value(energy_node_t *node, const char *text)
{
    /* fits on every board: the value shrinks from the value size through label and meta down to the board's minimum text size, which is below the meta size
     * of the Tab5 and the larger user steps (the former ladder ended at 20 px) */
    char plain[160]; size_t out = 0;
    for (size_t i = 0; text[i] && out + 1 < sizeof(plain); i++) {
        if (text[i] == '#') { if (strlen(text + i) >= 8 && text[i + 7] == ' ') i += 7; continue; }
        plain[out++] = text[i];
    }
    plain[out] = '\0';
    const lv_font_t *current = lv_obj_get_style_text_font(node->value_label, LV_PART_MAIN);
    const bool arrows = current == &s_arrow_value_font || current == &node->fitted_font;
    const lv_font_t *fonts[] = {ui_font_role(UI_TEXT_VALUE), ui_font_role(UI_TEXT_LABEL), ui_font_role(UI_TEXT_META),
                                ui_font_text_px(text_scale_floor_px(ui_fonts_board()))};
    const lv_font_t *chosen = ui_font_role(UI_TEXT_META);
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        const lv_font_t *font = fonts[i];
        if (arrows) {
            node->fitted_font = s_arrow_value_font;
            node->fitted_font.fallback = font;
            node->fitted_font.line_height = font->line_height;
            node->fitted_font.base_line = font->base_line;
            font = &node->fitted_font;
        }
        lv_point_t size; lv_text_get_size(&size, plain, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        chosen = fonts[i];
        if (size.x <= lv_obj_get_width(node->circle) - ENERGY_VALUE_INSET &&
            size.y <= lv_obj_get_height(node->circle) - 54) break;
    }
    if (arrows) {
        node->fitted_font.fallback = chosen;
        node->fitted_font.line_height = chosen->line_height;
        node->fitted_font.base_line = chosen->base_line;
    }
    lv_obj_set_style_text_font(node->value_label, arrows ? &node->fitted_font : chosen, LV_PART_MAIN);
    ui_text_compact(node->value_label, ui_font_at_floor(chosen));
}

static void energy_set_node_value_text(energy_node_t *node, const char *text)
{
    if (node == NULL || node->value_label == NULL) {
        return;
    }
    energy_fit_node_value(node, text != NULL ? text : "");
    lv_label_set_text(node->value_label, text != NULL ? text : "");
    energy_set_obj_hidden(node->value_exponent_label, true);
}

static void energy_set_node_value_text_with_exponent(energy_node_t *node, const char *text, const char *exponent)
{
    if (node == NULL || node->value_label == NULL) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    energy_fit_node_value(node, text);
    lv_label_set_text(node->value_label, text);
    if (node->value_exponent_label == NULL || exponent == NULL || exponent[0] == '\0') {
        energy_set_obj_hidden(node->value_exponent_label, true);
        return;
    }

    lv_label_set_text(node->value_exponent_label, exponent);
    energy_set_obj_hidden(node->value_exponent_label, false);

    lv_obj_update_layout(node->circle);
    lv_obj_update_layout(node->value_label);

    lv_point_t text_size = {0};
    lv_text_get_size(&text_size, text, ENERGY_VALUE_FONT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    int32_t label_x = lv_obj_get_x(node->value_label);
    int32_t label_y = lv_obj_get_y(node->value_label);
    int32_t label_w = lv_obj_get_width(node->value_label);
    int32_t x = label_x + ((label_w - text_size.x) / 2) + text_size.x - 1;
    int32_t y = label_y - (ENERGY_VALUE_FONT->line_height / 4);
    lv_obj_set_pos(node->value_exponent_label, x, y);
}

static void energy_create_home_arc(energy_page_ctx_t *ctx, int index, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(ctx->root);
    lv_obj_set_size(arc, ENERGY_HOME_SIZE + ENERGY_HOME_ARC_PAD, ENERGY_HOME_SIZE + ENERGY_HOME_ARC_PAD);
    lv_obj_set_pos(arc, ENERGY_NODE_HOME_X - ((ENERGY_HOME_SIZE + ENERGY_HOME_ARC_PAD) / 2),
                   ENERGY_NODE_HOME_Y - ((ENERGY_HOME_SIZE + ENERGY_HOME_ARC_PAD) / 2));
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_obj_set_style_arc_opa(arc, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, ENERGY_HOME_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    ctx->home_arcs[index] = arc;
}

static void energy_set_home_idle_ring(energy_page_ctx_t *ctx, bool idle)
{
    if (ctx == NULL || ctx->nodes[ENERGY_NODE_HOME].circle == NULL) {
        return;
    }
    lv_obj_set_style_border_width(ctx->nodes[ENERGY_NODE_HOME].circle, idle ? ENERGY_NODE_BORDER_WIDTH : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        ctx->nodes[ENERGY_NODE_HOME].circle, lv_color_hex(ENERGY_COLOR_LINE_IDLE), LV_PART_MAIN);
}

static void energy_set_flow_points(energy_flow_t *flow, lv_point_precise_t p0, lv_point_precise_t p1,
    lv_point_precise_t p2, uint8_t point_count)
{
    if (flow == NULL) {
        return;
    }
    flow->points[0] = p0;
    flow->points[1] = p1;
    flow->points[2] = p2;
    flow->point_count = point_count;
    flow->has_arc = false;
    if (flow->line != NULL) {
        lv_line_set_points(flow->line, flow->points, point_count);
    }
    if (flow->arc != NULL) {
        lv_obj_set_hidden(flow->arc, true);
    }
    if (flow->post_line != NULL) {
        lv_obj_set_hidden(flow->post_line, true);
    }
}

#define ENERGY_ARC_STEPS 16

static void energy_set_L_path(energy_flow_t *flow,
    lv_point_precise_t start, lv_point_precise_t corner, lv_point_precise_t end, float radius)
{
    if (flow == NULL) {
        return;
    }
    float dx_in = (float)(corner.x - start.x);
    float dy_in = (float)(corner.y - start.y);
    float dx_out = (float)(end.x - corner.x);
    float dy_out = (float)(end.y - corner.y);
    float len_in = sqrtf(dx_in * dx_in + dy_in * dy_in);
    float len_out = sqrtf(dx_out * dx_out + dy_out * dy_out);
    if (len_in < 1.0f || len_out < 1.0f) {
        return;
    }
    float nx_in = dx_in / len_in;
    float ny_in = dy_in / len_in;
    float nx_out = dx_out / len_out;
    float ny_out = dy_out / len_out;
    float asx = (float)corner.x - radius * nx_in;
    float asy = (float)corner.y - radius * ny_in;
    float aex = (float)corner.x + radius * nx_out;
    float aey = (float)corner.y + radius * ny_out;
    float cx, cy;
    if (fabsf(nx_in) < 0.01f) {
        cx = aex;
        cy = asy;
    } else {
        cx = asx;
        cy = aey;
    }
    float sa = atan2f(asy - cy, asx - cx);
    float ea = atan2f(aey - cy, aex - cx);
    float diff = ea - sa;
    while (diff > (float)M_PI) { diff -= 2.0f * (float)M_PI; }
    while (diff < -(float)M_PI) { diff += 2.0f * (float)M_PI; }

    /* Tessellated points[] stays populated for the dot animation which walks
     * along the flow by arc-length.  The visual line however is split into
     * (straight -> lv_arc -> straight) so the corner is a mathematically
     * perfect arc rather than a faceted polyline. */
    int idx = 0;
    flow->points[idx++] = start;
    for (int i = 0; i <= ENERGY_ARC_STEPS; i++) {
        float t = (float)i / (float)ENERGY_ARC_STEPS;
        float angle = sa + diff * t;
        flow->points[idx++] = (lv_point_precise_t){
            (lv_value_precise_t)(cx + radius * cosf(angle)),
            (lv_value_precise_t)(cy + radius * sinf(angle))
        };
    }
    flow->points[idx++] = end;
    flow->point_count = (uint8_t)idx;
    flow->has_arc = true;

    /* Pre-arc straight segment uses points[0] (= start) and points[1] (= arc
     * entry).  The tessellation loop above already populated those two slots
     * with the correct coordinates (points[1] == first arc sample == asx,asy
     * by construction), so we just tell the line widget to render only the
     * first two points.  lv_line stores the pointer, not a copy, but the
     * remaining tessellated entries follow in memory and are not touched. */
    if (flow->line != NULL) {
        lv_line_set_points(flow->line, flow->points, 2);
    }
    /* Post-arc straight segment: arc exit -> end */
    flow->post_points[0] = (lv_point_precise_t){(lv_value_precise_t)aex, (lv_value_precise_t)aey};
    flow->post_points[1] = end;
    if (flow->post_line != NULL) {
        lv_line_set_points(flow->post_line, flow->post_points, 2);
        lv_obj_set_hidden(flow->post_line, false);
    }

    /* Configure the lv_arc widget to render the corner.
     *
     * LVGL computes the drawn arc radius as (min(w,h) - arc_width) / 2 using
     * integer math, and places the arc center at the widget's pixel center
     * (pos + size/2 with integer division).  To get pixel-perfect alignment
     * with the straight line endpoints (asx/asy, aex/aey), we therefore need
     *   size       = 2*radius + arc_width           (odd => clean center)
     *   pos        = center_pixel - size/2          (integer math, same as LVGL)
     * For axis-aligned L-corners cx/cy are integer, so the arc's drawn ends
     * land exactly on asx/asy and aex/aey. */
    if (flow->arc != NULL) {
        const int stroke_w = ENERGY_FLOW_LINE_WIDTH;
        int s = 2 * (int)lroundf(radius) + stroke_w;  /* odd */
        int cxi = (int)lroundf(cx);
        int cyi = (int)lroundf(cy);
        lv_obj_set_size(flow->arc, s, s);
        lv_obj_set_pos(flow->arc, cxi - s / 2, cyi - s / 2);

        const float rad_to_deg = 57.29577951308232f;
        float start_rad = (diff >= 0.0f) ? sa : ea;
        float sweep_rad = fabsf(diff);
        int start_deg = ((int)lroundf(start_rad * rad_to_deg) % 360 + 360) % 360;
        int end_deg = start_deg + (int)lroundf(sweep_rad * rad_to_deg);
        lv_arc_set_bg_angles(flow->arc, (uint32_t)start_deg, (uint32_t)end_deg);
        lv_obj_set_hidden(flow->arc, false);
    }
}

static void energy_create_flow(energy_page_ctx_t *ctx, int flow_id, lv_color_t color)
{
    energy_flow_t *flow = &ctx->flows[flow_id];
    lv_obj_t *line = lv_line_create(ctx->root);
    lv_obj_set_size(line, APP_VIEW_CONTENT_WIDTH, APP_VIEW_CONTENT_HEIGHT);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_clickable(line, false);
    lv_obj_set_scrollable(line, false);
    lv_obj_set_style_line_width(line, ENERGY_FLOW_LINE_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, color, LV_PART_MAIN);
    lv_obj_set_style_line_opa(line, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, false, LV_PART_MAIN);

    /* Second straight segment after the arc (used only for L-flows).
     * Created as a zero-sized line widget; will be positioned/shown by
     * energy_set_L_path when the static paths are applied. */
    lv_obj_t *post_line = lv_line_create(ctx->root);
    lv_obj_set_size(post_line, APP_VIEW_CONTENT_WIDTH, APP_VIEW_CONTENT_HEIGHT);
    lv_obj_set_pos(post_line, 0, 0);
    lv_obj_set_clickable(post_line, false);
    lv_obj_set_scrollable(post_line, false);
    lv_obj_set_style_line_width(post_line, ENERGY_FLOW_LINE_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_line_color(post_line, color, LV_PART_MAIN);
    lv_obj_set_style_line_opa(post_line, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(post_line, false, LV_PART_MAIN);
    lv_obj_set_hidden(post_line, true);

    /* Real arc widget for the corner (used only for L-flows).
     * We use the MAIN part as the actual drawn arc (background arc in LVGL
     * terminology) and hide the INDICATOR and KNOB parts. */
    lv_obj_t *arc = lv_arc_create(ctx->root);
    lv_obj_set_pos(arc, 0, 0);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_obj_set_gesture_bubble(arc, false);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ENERGY_FLOW_LINE_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_value(arc, 0);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 0, 0);
    lv_obj_set_hidden(arc, true);

    lv_obj_t *dot = lv_obj_create(ctx->root);
    lv_obj_set_size(dot, ENERGY_DOT_SIZE, ENERGY_DOT_SIZE);
    lv_obj_set_style_radius(dot, ENERGY_DOT_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_scrollable(dot, false);
    lv_obj_set_hidden(dot, true);

    flow->line = line;
    flow->arc = arc;
    flow->post_line = post_line;
    flow->dot = dot;
    flow->color = color;
    flow->has_arc = false;
    flow->visible = false;
    flow->reverse = false;
    flow->value_w = 0.0f;
    flow->phase = 0.0f;
    flow->length = 0.0f;
}

static float energy_point_distance(lv_point_precise_t a, lv_point_precise_t b)
{
    float dx = (float)(b.x - a.x);
    float dy = (float)(b.y - a.y);
    return sqrtf(dx * dx + dy * dy);
}

static void energy_recompute_flow_length(energy_flow_t *flow)
{
    if (flow == NULL || flow->point_count < 2) {
        return;
    }
    float len = 0.0f;
    for (uint8_t i = 0; i + 1 < flow->point_count; i++) {
        len += energy_point_distance(flow->points[i], flow->points[i + 1]);
    }
    flow->length = len;
}

static void energy_apply_static_flow_paths(energy_page_ctx_t *ctx)
{
    energy_flow_t *flow = NULL;

    flow = &ctx->flows[ENERGY_FLOW_GRID_HOME];
    energy_set_flow_points(flow,
        (lv_point_precise_t){ENERGY_NODE_GRID_X + (ENERGY_NODE_SIZE / 2), ENERGY_NODE_GRID_Y},
        (lv_point_precise_t){ENERGY_NODE_HOME_X - (ENERGY_HOME_SIZE / 2), ENERGY_NODE_HOME_Y},
        (lv_point_precise_t){0, 0},
        2);

    flow = &ctx->flows[ENERGY_FLOW_SOLAR_HOME];
    energy_set_L_path(flow,
        (lv_point_precise_t){ENERGY_NODE_SOLAR_X + 15, ENERGY_NODE_SOLAR_Y + (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_SOLAR_X + 15, ENERGY_NODE_HOME_Y - 12},
        (lv_point_precise_t){ENERGY_NODE_HOME_X - (ENERGY_HOME_SIZE / 2), ENERGY_NODE_HOME_Y - 12},
        ENERGY_ARC_RADIUS);

    flow = &ctx->flows[ENERGY_FLOW_SOLAR_GRID];
    energy_set_L_path(flow,
        (lv_point_precise_t){ENERGY_NODE_SOLAR_X - 15, ENERGY_NODE_SOLAR_Y + (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_SOLAR_X - 15, ENERGY_NODE_GRID_Y - 12},
        (lv_point_precise_t){ENERGY_NODE_GRID_X + (ENERGY_NODE_SIZE / 2), ENERGY_NODE_GRID_Y - 12},
        ENERGY_ARC_RADIUS);

    flow = &ctx->flows[ENERGY_FLOW_SOLAR_BATTERY];
    energy_set_flow_points(flow,
        (lv_point_precise_t){ENERGY_NODE_SOLAR_X, ENERGY_NODE_SOLAR_Y + (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_BATTERY_X, ENERGY_NODE_BATTERY_Y - (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){0, 0},
        2);

    flow = &ctx->flows[ENERGY_FLOW_BATTERY_HOME];
    energy_set_L_path(flow,
        (lv_point_precise_t){ENERGY_NODE_BATTERY_X + 15, ENERGY_NODE_BATTERY_Y - (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_BATTERY_X + 15, ENERGY_NODE_HOME_Y + 12},
        (lv_point_precise_t){ENERGY_NODE_HOME_X - (ENERGY_HOME_SIZE / 2), ENERGY_NODE_HOME_Y + 12},
        ENERGY_ARC_RADIUS);

    flow = &ctx->flows[ENERGY_FLOW_BATTERY_GRID];
    energy_set_L_path(flow,
        (lv_point_precise_t){ENERGY_NODE_BATTERY_X - 15, ENERGY_NODE_BATTERY_Y - (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_BATTERY_X - 15, ENERGY_NODE_GRID_Y + 12},
        (lv_point_precise_t){ENERGY_NODE_GRID_X + (ENERGY_NODE_SIZE / 2), ENERGY_NODE_GRID_Y + 12},
        ENERGY_ARC_RADIUS);

    /* Gas → Home: vertical line down */
    flow = &ctx->flows[ENERGY_FLOW_GAS_HOME];
    energy_set_flow_points(flow,
        (lv_point_precise_t){ENERGY_NODE_GAS_X, ENERGY_NODE_GAS_Y + (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_HOME_X, ENERGY_NODE_HOME_Y - (ENERGY_HOME_SIZE / 2)},
        (lv_point_precise_t){0, 0},
        2);

    /* Water → Home: vertical line up */
    flow = &ctx->flows[ENERGY_FLOW_WATER_HOME];
    energy_set_flow_points(flow,
        (lv_point_precise_t){ENERGY_NODE_WATER_X, ENERGY_NODE_WATER_Y - (ENERGY_NODE_SIZE / 2)},
        (lv_point_precise_t){ENERGY_NODE_HOME_X, ENERGY_NODE_HOME_Y + (ENERGY_HOME_SIZE / 2)},
        (lv_point_precise_t){0, 0},
        2);

    for (int i = 0; i < ENERGY_FLOW_COUNT; i++) {
        energy_recompute_flow_length(&ctx->flows[i]);
    }
}

static bool energy_apply_slot_state(energy_page_ctx_t *ctx, energy_slot_id_t slot_id, const ha_state_t *state)
{
    if (ctx == NULL || state == NULL) {
        return false;
    }
    const char *entity_id = energy_entity_for_slot(ctx, slot_id);
    if (entity_id == NULL || entity_id[0] == '\0' ||
        strncmp(entity_id, state->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return false;
    }

    energy_value_slot_t *slot = &ctx->slots[slot_id];
    slot->configured = true;
    slot->valid = false;
    slot->value = 0.0f;
    slot->unit[0] = '\0';

    if (energy_state_is_unavailable(state->state)) {
        return true;
    }

    float parsed = 0.0f;
    if (!energy_parse_float_relaxed(state->state, &parsed)) {
        return true;
    }

    energy_copy_unit_from_attrs(slot->unit, sizeof(slot->unit), state->attributes_json);
    slot->valid = true;
    if (slot_id == ENERGY_SLOT_BATTERY_SOC || slot_id == ENERGY_SLOT_GAS || slot_id == ENERGY_SLOT_WATER) {
        slot->value = parsed; /* percent, or a meter reading shown in its own unit */
    } else {
        slot->value = energy_normalize_power_w(parsed, slot->unit);
    }
    return true;
}

static void energy_mark_missing_slots(energy_page_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    for (int i = 0; i < ENERGY_SLOT_COUNT; i++) {
        ctx->slots[i].configured = energy_slot_is_configured(ctx, (energy_slot_id_t)i);
        if (!ctx->slots[i].configured) {
            ctx->slots[i].valid = false;
            ctx->slots[i].value = 0.0f;
            ctx->slots[i].unit[0] = '\0';
        }
    }
}

static float energy_positive_slot_value(const energy_page_ctx_t *ctx, energy_slot_id_t slot_id)
{
    if (ctx == NULL || slot_id >= ENERGY_SLOT_COUNT || !ctx->slots[slot_id].valid) {
        return 0.0f;
    }
    return fmaxf(ctx->slots[slot_id].value, 0.0f);
}

static float energy_signed_slot_value(const energy_page_ctx_t *ctx, energy_slot_id_t slot_id)
{
    if (ctx == NULL || slot_id >= ENERGY_SLOT_COUNT || !ctx->slots[slot_id].valid) {
        return 0.0f;
    }
    return ctx->slots[slot_id].value;
}

static energy_flow_values_t energy_compute_flows(
    float from_grid, float to_grid, float solar, float to_battery, float from_battery)
{
    energy_flow_values_t out = {0};

    from_grid = fmaxf(from_grid, 0.0f);
    to_grid = fmaxf(to_grid, 0.0f);
    solar = fmaxf(solar, 0.0f);
    to_battery = fmaxf(to_battery, 0.0f);
    from_battery = fmaxf(from_battery, 0.0f);

    const float used_total = from_grid + solar + from_battery - to_grid - to_battery;
    float used_total_remaining = fmaxf(used_total, 0.0f);

    float excess_grid_in_after_consumption = fmaxf(0.0f, fminf(to_battery, from_grid - used_total_remaining));
    out.grid_to_battery += excess_grid_in_after_consumption;
    to_battery -= excess_grid_in_after_consumption;
    from_grid -= excess_grid_in_after_consumption;

    out.solar_to_battery = fminf(solar, to_battery);
    to_battery -= out.solar_to_battery;
    solar -= out.solar_to_battery;

    out.solar_to_grid = fminf(solar, to_grid);
    to_grid -= out.solar_to_grid;
    solar -= out.solar_to_grid;

    out.battery_to_grid = fminf(from_battery, to_grid);
    from_battery -= out.battery_to_grid;
    to_grid -= out.battery_to_grid;

    float grid_to_battery_2 = fminf(from_grid, to_battery);
    out.grid_to_battery += grid_to_battery_2;
    from_grid -= grid_to_battery_2;
    to_battery -= grid_to_battery_2;

    out.used_solar = fminf(used_total_remaining, solar);
    used_total_remaining -= out.used_solar;
    solar -= out.used_solar;

    out.used_battery = fminf(from_battery, used_total_remaining);
    from_battery -= out.used_battery;
    used_total_remaining -= out.used_battery;

    out.used_grid = fminf(used_total_remaining, from_grid);
    from_grid -= out.used_grid;
    used_total_remaining -= out.used_grid;

    out.used_total = used_total;
    return out;
}

static void energy_update_home_arcs(energy_page_ctx_t *ctx, const energy_flow_values_t *flows, float display_home_w)
{
    if (ctx == NULL || flows == NULL) {
        return;
    }
    (void)display_home_w;

    float values[ENERGY_HOME_ARC_COUNT] = {
        fmaxf(flows->used_solar, 0.0f),
        fmaxf(flows->used_battery, 0.0f),
        fmaxf(flows->used_grid, 0.0f),
    };
    float total = values[ENERGY_ARC_SOLAR] + values[ENERGY_ARC_BATTERY] + values[ENERGY_ARC_GRID];
    if (total <= ENERGY_FLOW_MIN_VISIBLE_W) {
        for (int i = 0; i < ENERGY_HOME_ARC_COUNT; i++) {
            energy_set_obj_hidden(ctx->home_arcs[i], true);
        }
        energy_set_home_idle_ring(ctx, true);
        return;
    }

    bool active[ENERGY_HOME_ARC_COUNT] = {0};
    int active_count = 0;
    for (int i = 0; i < ENERGY_HOME_ARC_COUNT; i++) {
        active[i] = values[i] > ENERGY_FLOW_MIN_VISIBLE_W;
        if (active[i]) {
            active_count++;
        }
    }
    if (active_count == 0) {
        for (int i = 0; i < ENERGY_HOME_ARC_COUNT; i++) {
            energy_set_obj_hidden(ctx->home_arcs[i], true);
        }
        energy_set_home_idle_ring(ctx, true);
        return;
    }

    energy_set_home_idle_ring(ctx, false);
    int start = 0;
    int active_seen = 0;
    for (int i = 0; i < ENERGY_HOME_ARC_COUNT; i++) {
        if (!active[i]) {
            energy_set_obj_hidden(ctx->home_arcs[i], true);
            continue;
        }
        active_seen++;
        int end = 360;
        if (active_seen < active_count) {
            int remaining_segments = active_count - active_seen;
            int span = (int)((values[i] / total) * 360.0f + 0.5f);
            if (span < 1) {
                span = 1;
            }
            int max_span = 360 - start - remaining_segments;
            if (span > max_span) {
                span = max_span;
            }
            end = start + span;
        }
        lv_arc_set_angles(ctx->home_arcs[i], start, end);
        energy_set_obj_hidden(ctx->home_arcs[i], false);
        start = end;
    }
}

static void energy_set_flow_active(energy_flow_t *flow, bool line_visible, float value_w, bool reverse, lv_color_t color)
{
    if (flow == NULL || flow->line == NULL || flow->dot == NULL) {
        return;
    }

    flow->value_w = fmaxf(value_w, 0.0f);
    flow->visible = line_visible && (flow->value_w > ENERGY_FLOW_MIN_VISIBLE_W);
    flow->reverse = reverse;
    flow->color = color;

    bool active = flow->visible;
    lv_opa_t line_opa = active ? LV_OPA_70 : LV_OPA_30;

    energy_set_obj_hidden(flow->line, !active);
    lv_obj_set_style_line_color(flow->line, color, LV_PART_MAIN);
    lv_obj_set_style_line_opa(flow->line, line_opa, LV_PART_MAIN);
    lv_obj_set_style_bg_color(flow->dot, color, LV_PART_MAIN);

    if (flow->has_arc) {
        if (flow->arc != NULL) {
            energy_set_obj_hidden(flow->arc, !active);
            lv_obj_set_style_arc_color(flow->arc, color, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(flow->arc, line_opa, LV_PART_MAIN);
        }
        if (flow->post_line != NULL) {
            energy_set_obj_hidden(flow->post_line, !active);
            lv_obj_set_style_line_color(flow->post_line, color, LV_PART_MAIN);
            lv_obj_set_style_line_opa(flow->post_line, line_opa, LV_PART_MAIN);
        }
    }

    if (!active) {
        lv_obj_set_hidden(flow->dot, true);
    } else {
        lv_obj_set_hidden(flow->dot, false);
    }
}

static void energy_update_flow_visuals(energy_page_ctx_t *ctx, const energy_flow_values_t *flows,
    bool has_grid, bool has_solar, bool has_battery, bool has_gas, bool has_water)
{
    if (ctx == NULL || flows == NULL) {
        return;
    }

    lv_color_t grid_color = lv_color_hex(ENERGY_COLOR_GRID);
    lv_color_t return_color = lv_color_hex(ENERGY_COLOR_RETURN);
    lv_color_t solar_color = lv_color_hex(ENERGY_COLOR_SOLAR);
    lv_color_t battery_out_color = lv_color_hex(ENERGY_COLOR_BATTERY_OUT);
    lv_color_t battery_in_color = lv_color_hex(ENERGY_COLOR_BATTERY_IN);

    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_GRID_HOME], has_grid, flows->used_grid, false, grid_color);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_SOLAR_HOME], has_solar, flows->used_solar, false, solar_color);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_SOLAR_GRID], has_solar && has_grid, flows->solar_to_grid, false, return_color);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_SOLAR_BATTERY], has_solar && has_battery, flows->solar_to_battery, false, battery_in_color);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_BATTERY_HOME], has_battery, flows->used_battery, false, battery_out_color);

    bool battery_grid_reverse = flows->grid_to_battery > flows->battery_to_grid;
    float battery_grid_value = battery_grid_reverse ? flows->grid_to_battery : flows->battery_to_grid;
    lv_color_t battery_grid_color = battery_grid_reverse ? grid_color : return_color;
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_BATTERY_GRID], has_battery && has_grid, battery_grid_value, battery_grid_reverse, battery_grid_color);

    lv_color_t gas_color = lv_color_hex(ENERGY_COLOR_GAS);
    lv_color_t water_color = lv_color_hex(ENERGY_COLOR_WATER);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_GAS_HOME], has_gas, has_gas ? 500.0f : 0.0f, false, gas_color);
    energy_set_flow_active(
        &ctx->flows[ENERGY_FLOW_WATER_HOME], has_water, has_water ? 500.0f : 0.0f, false, water_color);
}

/* Update Grid/Battery node: builds the two-line value text with per-line
 * recolor markup and sets the circle border color based on the dominant
 * direction (HA-style).  Pass min_visible in the same unit as the values
 * (W for power mode, kWh for energy mode). */
static void energy_update_bidir_node(energy_node_t *node,
    const char *arrow_in, const char *arrow_out,
    const char *in_text, const char *out_text,
    float in_val, float out_val,
    uint32_t color_in_hex, uint32_t color_out_hex,
    float min_visible)
{
    if (node == NULL || node->value_label == NULL) {
        return;
    }
    char buf[160] = {0};
    snprintf(buf, sizeof(buf),
        "#%06X %s %s#\n#%06X %s %s#",
        (unsigned int)(color_in_hex & 0xFFFFFFu), arrow_in, in_text,
        (unsigned int)(color_out_hex & 0xFFFFFFu), arrow_out, out_text);
    energy_fit_node_value(node, buf);
    lv_label_set_text(node->value_label, buf);

    /* Dominant direction determines the ring color. */
    uint32_t border_hex;
    if (out_val > in_val && out_val > min_visible) {
        border_hex = color_out_hex;
    } else if (in_val > min_visible) {
        border_hex = color_in_hex;
    } else {
        /* No meaningful flow: fall back to the "in" color as neutral. */
        border_hex = color_in_hex;
    }
    if (node->circle != NULL) {
        lv_obj_set_style_border_color(node->circle, lv_color_hex(border_hex), LV_PART_MAIN);
    }
}

static void energy_update_node_values(energy_page_ctx_t *ctx, float solar_w, float grid_import_w, float grid_export_w,
    float battery_charge_w, float battery_discharge_w, float display_home_w){
    if (ctx == NULL) {
        return;
    }

    char buf[96] = {0};

    if (ctx->slots[ENERGY_SLOT_HOME_POWER].configured && !ctx->slots[ENERGY_SLOT_HOME_POWER].valid) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        energy_format_power(buf, sizeof(buf), fmaxf(display_home_w, 0.0f));
    }
    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_HOME], buf);

    if (energy_has_solar_config(ctx)) {
        if (!ctx->slots[ENERGY_SLOT_SOLAR_POWER].valid) {
            snprintf(buf, sizeof(buf), "--");
        } else {
            energy_format_power(buf, sizeof(buf), solar_w);
        }
        energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_SOLAR], buf);
    }

    if (energy_has_grid_config(ctx)) {
        char in_text[32] = {0};
        char out_text[32] = {0};
        energy_format_power(in_text, sizeof(in_text), grid_import_w);
        energy_format_power(out_text, sizeof(out_text), grid_export_w);
        energy_update_bidir_node(&ctx->nodes[ENERGY_NODE_GRID],
            ENERGY_ARROW_RIGHT, ENERGY_ARROW_LEFT,
            in_text, out_text,
            grid_import_w, grid_export_w,
            ENERGY_COLOR_GRID, ENERGY_COLOR_RETURN,
            ENERGY_FLOW_MIN_VISIBLE_W);
    }

    if (energy_has_battery_config(ctx)) {
        char in_text[32] = {0};
        char out_text[32] = {0};
        energy_format_power(in_text, sizeof(in_text), battery_charge_w);
        energy_format_power(out_text, sizeof(out_text), battery_discharge_w);
        energy_update_bidir_node(&ctx->nodes[ENERGY_NODE_BATTERY],
            ENERGY_ARROW_DOWN, ENERGY_ARROW_UP,
            in_text, out_text,
            battery_charge_w, battery_discharge_w,
            ENERGY_COLOR_BATTERY_IN, ENERGY_COLOR_BATTERY_OUT,
            ENERGY_FLOW_MIN_VISIBLE_W);
    }
}

static void energy_recompute_ha_energy_placeholder(energy_page_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GRID], false);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_SOLAR], false);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_BATTERY], false);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GAS], true);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_WATER], true);

    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_HOME], "-- kWh");
    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_SOLAR], "-- kWh");
    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_GRID], "-- kWh");
    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_BATTERY], "-- kWh");

    for (int i = 0; i < ENERGY_HOME_ARC_COUNT; i++) {
        energy_set_obj_hidden(ctx->home_arcs[i], true);
    }
    energy_set_home_idle_ring(ctx, true);

    energy_flow_values_t flows = {0};
    energy_update_flow_visuals(ctx, &flows, true, true, true, false, false);
    lv_label_set_text(ctx->stat_today_label, ui_i18n_get("energy.ha_waiting", "HA Energy data"));
    lv_label_set_text(ctx->stat_autarky_label, "");
}

static energy_flow_values_t energy_scale_flow_values(const energy_flow_values_t *flows, float scale)
{
    energy_flow_values_t out = {0};
    if (flows == NULL) {
        return out;
    }
    out.used_solar = flows->used_solar * scale;
    out.used_grid = flows->used_grid * scale;
    out.used_battery = flows->used_battery * scale;
    out.used_total = flows->used_total * scale;
    out.grid_to_battery = flows->grid_to_battery * scale;
    out.battery_to_grid = flows->battery_to_grid * scale;
    out.solar_to_battery = flows->solar_to_battery * scale;
    out.solar_to_grid = flows->solar_to_grid * scale;
    return out;
}

static void energy_recompute_ha_energy(energy_page_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ha_energy_snapshot_t snapshot = {0};
    if (!ha_energy_model_get_snapshot(&snapshot) || !snapshot.available) {
        energy_recompute_ha_energy_placeholder(ctx);
        return;
    }

    bool has_grid = snapshot.has_grid;
    bool has_solar = snapshot.has_solar;
    bool has_battery = snapshot.has_battery;

    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GRID], !has_grid);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_SOLAR], !has_solar);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_BATTERY], !has_battery);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GAS], !snapshot.has_gas);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_WATER], !snapshot.has_water);

    energy_flow_values_t flows = energy_compute_flows(snapshot.from_grid_kwh,
        snapshot.to_grid_kwh,
        snapshot.solar_kwh,
        snapshot.to_battery_kwh,
        snapshot.from_battery_kwh);

    char buf[96] = {0};
    energy_format_kwh(buf, sizeof(buf), fmaxf(flows.used_total, 0.0f));
    energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_HOME], buf);

    if (has_solar) {
        energy_format_kwh(buf, sizeof(buf), fmaxf(snapshot.solar_kwh, 0.0f));
        energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_SOLAR], buf);
    }

    if (has_grid) {
        char in_text[32] = {0};
        char out_text[32] = {0};
        float in_kwh = fmaxf(snapshot.from_grid_kwh, 0.0f);
        float out_kwh = fmaxf(snapshot.to_grid_kwh, 0.0f);
        energy_format_kwh(in_text, sizeof(in_text), in_kwh);
        energy_format_kwh(out_text, sizeof(out_text), out_kwh);
        energy_update_bidir_node(&ctx->nodes[ENERGY_NODE_GRID],
            ENERGY_ARROW_RIGHT, ENERGY_ARROW_LEFT,
            in_text, out_text,
            in_kwh, out_kwh,
            ENERGY_COLOR_GRID, ENERGY_COLOR_RETURN,
            ENERGY_KWH_MIN_VISIBLE);
    }

    if (has_battery) {
        char in_text[32] = {0};
        char out_text[32] = {0};
        float in_kwh = fmaxf(snapshot.to_battery_kwh, 0.0f);
        float out_kwh = fmaxf(snapshot.from_battery_kwh, 0.0f);
        energy_format_kwh(in_text, sizeof(in_text), in_kwh);
        energy_format_kwh(out_text, sizeof(out_text), out_kwh);
        energy_update_bidir_node(&ctx->nodes[ENERGY_NODE_BATTERY],
            ENERGY_ARROW_DOWN, ENERGY_ARROW_UP,
            in_text, out_text,
            in_kwh, out_kwh,
            ENERGY_COLOR_BATTERY_IN, ENERGY_COLOR_BATTERY_OUT,
            ENERGY_KWH_MIN_VISIBLE);
        energy_update_battery_title(ctx);
    }
    energy_update_solar_title(ctx);

    if (snapshot.has_gas) {
        const char *gas_unit = snapshot.gas_unit[0] ? snapshot.gas_unit : "m\xC2\xB3";
        if (energy_unit_is_cubic_meter(gas_unit)) {
            energy_format_cubic_meter_value(buf, sizeof(buf), snapshot.gas_value);
            energy_set_node_value_text_with_exponent(&ctx->nodes[ENERGY_NODE_GAS], buf, "3");
        } else {
            energy_format_value_unit(buf, sizeof(buf), snapshot.gas_value, gas_unit);
            energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_GAS], buf);
        }
    }

    if (snapshot.has_water) {
        const char *water_unit = snapshot.water_unit[0] ? snapshot.water_unit : "L";
        energy_format_value_unit(buf, sizeof(buf), snapshot.water_value, water_unit);
        energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_WATER], buf);
    }

    energy_flow_values_t visual_flows = energy_scale_flow_values(&flows, ENERGY_KWH_VISUAL_SCALE);
    energy_update_home_arcs(ctx, &visual_flows, visual_flows.used_total);
    energy_update_flow_visuals(ctx, &visual_flows, has_grid, has_solar, has_battery, snapshot.has_gas, snapshot.has_water);

    char home_text[32] = {0};
    energy_format_kwh(home_text, sizeof(home_text), fmaxf(flows.used_total, 0.0f));
    snprintf(buf, sizeof(buf), "%s %s", ui_i18n_get("energy.today", "Today"), home_text);
    lv_label_set_text(ctx->stat_today_label, buf);

    /* Autarky (self-sufficiency): share of the home's consumption covered by solar and
     * battery, not to be confused with self-consumption (share of solar production used). */
    if (has_solar && flows.used_total > ENERGY_KWH_MIN_VISIBLE) {
        float self_produced = flows.used_solar + flows.used_battery;
        float autarky_pct = (self_produced / flows.used_total) * 100.0f;
        if (autarky_pct > 100.0f) { autarky_pct = 100.0f; }
        snprintf(buf, sizeof(buf), "%s %d%%", ui_i18n_get("energy.autarky", "Autarky"), (int)(autarky_pct + 0.5f));
        lv_label_set_text(ctx->stat_autarky_label, buf);
    } else {
        lv_label_set_text(ctx->stat_autarky_label, "");
    }
}

static void energy_recompute(energy_page_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (strcmp(ctx->config.source, ENERGY_SOURCE_MANUAL) != 0) {
        energy_recompute_ha_energy(ctx);
        return;
    }

    bool has_grid = energy_has_grid_config(ctx);
    bool has_solar = energy_has_solar_config(ctx);
    bool has_battery = energy_has_battery_config(ctx);

    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GRID], !has_grid);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_SOLAR], !has_solar);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_BATTERY], !has_battery);
    const bool has_gas = energy_slot_is_configured(ctx, ENERGY_SLOT_GAS);
    const bool has_water = energy_slot_is_configured(ctx, ENERGY_SLOT_WATER);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GAS], !has_gas);
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_WATER], !has_water);
    if (has_gas) {
        const energy_value_slot_t *gas = &ctx->slots[ENERGY_SLOT_GAS];
        char text[48] = "-";
        if (gas->valid) {
            const char *unit = gas->unit[0] ? gas->unit : "m\xC2\xB3";
            if (energy_unit_is_cubic_meter(unit)) {
                energy_format_cubic_meter_value(text, sizeof(text), gas->value);
                energy_set_node_value_text_with_exponent(&ctx->nodes[ENERGY_NODE_GAS], text, "3");
            } else {
                energy_format_value_unit(text, sizeof(text), gas->value, unit);
                energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_GAS], text);
            }
        } else {
            energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_GAS], text);
        }
    }
    if (has_water) {
        const energy_value_slot_t *water = &ctx->slots[ENERGY_SLOT_WATER];
        char text[48] = "-";
        if (water->valid) {
            energy_format_value_unit(text, sizeof(text), water->value, water->unit[0] ? water->unit : "L");
        }
        energy_set_node_value_text(&ctx->nodes[ENERGY_NODE_WATER], text);
    }

    float solar_w = energy_positive_slot_value(ctx, ENERGY_SLOT_SOLAR_POWER);

    float grid_signed = energy_signed_slot_value(ctx, ENERGY_SLOT_GRID_POWER);
    float grid_import_w = energy_positive_slot_value(ctx, ENERGY_SLOT_GRID_IMPORT_POWER);
    float grid_export_w = energy_positive_slot_value(ctx, ENERGY_SLOT_GRID_EXPORT_POWER);
    if (grid_import_w <= ENERGY_FLOW_MIN_VISIBLE_W && grid_export_w <= ENERGY_FLOW_MIN_VISIBLE_W &&
        ctx->slots[ENERGY_SLOT_GRID_POWER].valid) {
        if (grid_signed >= 0.0f) {
            grid_import_w = grid_signed;
        } else {
            grid_export_w = -grid_signed;
        }
    }

    float battery_signed = energy_signed_slot_value(ctx, ENERGY_SLOT_BATTERY_POWER);
    float battery_charge_w = energy_positive_slot_value(ctx, ENERGY_SLOT_BATTERY_CHARGE_POWER);
    float battery_discharge_w = energy_positive_slot_value(ctx, ENERGY_SLOT_BATTERY_DISCHARGE_POWER);
    if (battery_charge_w <= ENERGY_FLOW_MIN_VISIBLE_W && battery_discharge_w <= ENERGY_FLOW_MIN_VISIBLE_W &&
        ctx->slots[ENERGY_SLOT_BATTERY_POWER].valid) {
        if (battery_signed >= 0.0f) {
            battery_discharge_w = battery_signed;
        } else {
            battery_charge_w = -battery_signed;
        }
    }

    energy_flow_values_t flows = energy_compute_flows(grid_import_w, grid_export_w, solar_w, battery_charge_w, battery_discharge_w);

    float display_home_w = flows.used_total;
    if (ctx->slots[ENERGY_SLOT_HOME_POWER].valid) {
        display_home_w = fmaxf(ctx->slots[ENERGY_SLOT_HOME_POWER].value, 0.0f);
    }

    energy_update_node_values(ctx, solar_w, grid_import_w, grid_export_w, battery_charge_w, battery_discharge_w, display_home_w);
    energy_update_home_arcs(ctx, &flows, display_home_w);
    energy_update_flow_visuals(ctx, &flows, has_grid, has_solar, has_battery, has_gas, has_water);
    energy_update_battery_title(ctx);

    char subtitle[96] = {0};
    char home_text[32] = {0};
    energy_format_power(home_text, sizeof(home_text), fmaxf(display_home_w, 0.0f));
    snprintf(subtitle, sizeof(subtitle), "%s %s", ui_i18n_get("energy.home_now", "Home now"), home_text);
    lv_label_set_text(ctx->stat_today_label, subtitle);
    lv_label_set_text(ctx->stat_autarky_label, "");
}

static void energy_update_dot_position(energy_flow_t *flow)
{
    if (flow == NULL || flow->dot == NULL || flow->point_count < 2 || flow->length <= 0.0f ||
        flow->value_w <= ENERGY_FLOW_MIN_VISIBLE_W || !flow->visible) {
        return;
    }

    float target = flow->reverse ? (1.0f - flow->phase) : flow->phase;
    if (target < 0.0f) {
        target = 0.0f;
    }
    if (target > 1.0f) {
        target = 1.0f;
    }

    float distance = target * flow->length;
    lv_point_precise_t p = flow->points[0];
    for (uint8_t i = 0; i + 1 < flow->point_count; i++) {
        lv_point_precise_t a = flow->points[i];
        lv_point_precise_t b = flow->points[i + 1];
        float seg = energy_point_distance(a, b);
        if (seg <= 0.0f) {
            continue;
        }
        if (distance <= seg || i + 2 == flow->point_count) {
            float t = distance / seg;
            p.x = (lv_value_precise_t)((float)a.x + ((float)(b.x - a.x) * t));
            p.y = (lv_value_precise_t)((float)a.y + ((float)(b.y - a.y) * t));
            break;
        }
        distance -= seg;
    }

    lv_obj_set_pos(flow->dot, (lv_coord_t)p.x - (ENERGY_DOT_SIZE / 2), (lv_coord_t)p.y - (ENERGY_DOT_SIZE / 2));
}

static void energy_anim_timer_cb(lv_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }
    energy_page_ctx_t *ctx = (energy_page_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx->root == NULL) {
        return;
    }
    lv_obj_t *parent = lv_obj_get_parent(ctx->root);
    if (parent != NULL && lv_obj_is_hidden(parent)) {
        return;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    if (ctx->last_anim_ms == 0) {
        ctx->last_anim_ms = now_ms;
        return;
    }

    float dt = (float)(now_ms - ctx->last_anim_ms) / 1000.0f;
    ctx->last_anim_ms = now_ms;
    if (dt <= 0.0f || dt > 1.0f) {
        dt = 0.05f;
    }

    float total = 0.0f;
    for (int i = 0; i < ENERGY_FLOW_COUNT; i++) {
        if (ctx->flows[i].visible && ctx->flows[i].value_w > ENERGY_FLOW_MIN_VISIBLE_W) {
            total += ctx->flows[i].value_w;
        }
    }
    if (total <= ENERGY_FLOW_MIN_VISIBLE_W) {
        return;
    }

    for (int i = 0; i < ENERGY_FLOW_COUNT; i++) {
        energy_flow_t *flow = &ctx->flows[i];
        if (!flow->visible || flow->value_w <= ENERGY_FLOW_MIN_VISIBLE_W) {
            continue;
        }
        float share = flow->value_w / total;
        float duration = 5.5f - (share * 4.4f);
        if (duration < 1.1f) {
            duration = 1.1f;
        }
        if (duration > 5.5f) {
            duration = 5.5f;
        }
        flow->phase += dt / duration;
        while (flow->phase > 1.0f) {
            flow->phase -= 1.0f;
        }
        energy_update_dot_position(flow);
    }
}

/* ---- Seven-day details of Gas, Water and Solar ---- */

#define ENERGY_DETAIL_X (ENERGY_NODE_HOME_X + ENERGY_NODE_TITLE_WIDTH / 2 + 30) /* right of the Gas/Water titles */
#define ENERGY_DETAIL_PAD 20
#define ENERGY_DETAIL_MIN_COLUMN_TENTHS 22 /* a bar's value label ("3.01", "1210") needs about 2.2 line heights of width */

/* The page whose right-hand panel awaits an answer, or NULL. Answers carry only a generation, never the page. */
static energy_page_ctx_t *s_detail_ctx;
static uint32_t s_detail_generation_counter;

static bool energy_node_has_details(int node_id)
{
    return node_id == ENERGY_NODE_GAS || node_id == ENERGY_NODE_WATER || node_id == ENERGY_NODE_SOLAR;
}

static uint32_t energy_node_color(int node_id)
{
    return node_id == ENERGY_NODE_GAS ? ENERGY_COLOR_GAS : node_id == ENERGY_NODE_WATER ? ENERGY_COLOR_WATER
                                                                                          : ENERGY_COLOR_SOLAR;
}

/* The statistic IDs behind a node: the Energy dashboard's sources (Home Assistant source) or the configured Gas/Water
 * entity (manual source: its Solar value is a power sensor and has no consumption history). */
static size_t energy_node_sources(const energy_page_ctx_t *ctx, int node_id, char ids[][APP_MAX_ENTITY_ID_LEN],
    size_t max_ids)
{
    if (ctx == NULL || !energy_node_has_details(node_id) || max_ids == 0) {
        return 0;
    }
    if (strcmp(ctx->config.source, ENERGY_SOURCE_MANUAL) == 0) {
        const char *entity = node_id == ENERGY_NODE_GAS ? ctx->config.gas_entity_id
                             : node_id == ENERGY_NODE_WATER ? ctx->config.water_entity_id : "";
        if (entity[0] == '\0') {
            return 0;
        }
        snprintf(ids[0], APP_MAX_ENTITY_ID_LEN, "%s", entity);
        return 1;
    }
    const ha_energy_resource_t resource = node_id == ENERGY_NODE_GAS ? HA_ENERGY_RES_GAS
                                          : node_id == ENERGY_NODE_WATER ? HA_ENERGY_RES_WATER : HA_ENERGY_RES_SOLAR;
    return ha_client_energy_source_ids(resource, ids, max_ids);
}

/* Is there room for the right-hand panel? Decided from the page's content width and height and the text metrics, never
 * from the board name: a narrow page (4-inch, Tab5 portrait) keeps the popup. */
static bool energy_side_panel_fits(void)
{
    const int width = APP_VIEW_CONTENT_WIDTH - ENERGY_DETAIL_X - ENERGY_DETAIL_PAD;
    const int column = ENERGY_DETAIL_MIN_COLUMN_TENTHS * (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META)) / 10;
    return width >= 7 * column + 16 && APP_VIEW_CONTENT_HEIGHT >= 420;
}

static void energy_days_received(uint32_t generation, ha_history_days_t *result)
{
    energy_page_ctx_t *ctx = s_detail_ctx;
    if (ctx == NULL || ctx->detail_generation != generation || ctx->detail_body == NULL || result == NULL) {
        ha_history_days_free(result); /* an answer for a closed page or an older selection */
        return;
    }
    const ui_days_view_t view = {.body = ctx->detail_body, .status = ctx->detail_status,
        .color = energy_node_color(ctx->detail_node)};
    ui_days_view_show(&view, result);
    ha_history_days_free(result);
}

static void energy_detail_mark_selected(energy_page_ctx_t *ctx)
{
    for (int i = 0; i < ENERGY_NODE_COUNT; i++) {
        if (ctx->nodes[i].circle == NULL) {
            continue;
        }
        const bool selected = i == ctx->detail_node;
        lv_obj_set_style_outline_width(ctx->nodes[i].circle, selected ? 4 : 0, LV_PART_MAIN);
        lv_obj_set_style_outline_color(ctx->nodes[i].circle, theme_default_color_text_primary(), LV_PART_MAIN);
        lv_obj_set_style_outline_pad(ctx->nodes[i].circle, 3, LV_PART_MAIN);
    }
}

static void energy_detail_select(energy_page_ctx_t *ctx, int node_id, char ids[][APP_MAX_ENTITY_ID_LEN], size_t count)
{
    ctx->detail_node = node_id;
    ctx->detail_generation = ++s_detail_generation_counter;
    s_detail_ctx = ctx;
    energy_detail_mark_selected(ctx);
    lv_label_set_text(ctx->detail_title, lv_label_get_text(ctx->nodes[node_id].title_label));
    lv_obj_clean(ctx->detail_body);
    lv_label_set_text(ctx->detail_status, ui_i18n_get("history.loading", "Loading history..."));
#ifdef BETTA_SIMULATOR
    ha_history_days_t *result = ui_days_view_simulated(node_id == ENERGY_NODE_GAS);
    if (result != NULL) {
        energy_days_received(ctx->detail_generation, result);
    }
    (void)ids;
    (void)count;
#else
    const char *list[HA_DAYS_MAX_SOURCES];
    for (size_t i = 0; i < count; i++) {
        list[i] = ids[i];
    }
    if (ha_history_request_days(list, count, ctx->detail_generation, energy_days_received) != ESP_OK) {
        lv_label_set_text(ctx->detail_status, ui_i18n_get("history.week_error", "Could not load statistics"));
    }
#endif
}

static void energy_node_clicked(lv_event_t *event)
{
    energy_page_ctx_t *ctx = (energy_page_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    int node_id = -1;
    for (int i = 0; i < ENERGY_NODE_COUNT; i++) {
        if (ctx->nodes[i].circle == lv_event_get_target(event)) {
            node_id = i;
        }
    }
    char ids[HA_DAYS_MAX_SOURCES][APP_MAX_ENTITY_ID_LEN];
    const size_t count = energy_node_sources(ctx, node_id, ids, HA_DAYS_MAX_SOURCES);
    if (count == 0) {
        return; /* no source behind this node: it stays informational */
    }
    if (ctx->detail_panel != NULL) {
        energy_detail_select(ctx, node_id, ids, count);
        return;
    }
    const char *list[HA_DAYS_MAX_SOURCES];
    for (size_t i = 0; i < count; i++) {
        list[i] = ids[i];
    }
    ui_history_popup_open_days(lv_label_get_text(ctx->nodes[node_id].title_label), energy_node_color(node_id), list, count);
}

static void energy_create_detail_panel(energy_page_ctx_t *ctx)
{
    const int x = ENERGY_DETAIL_X;
    const int w = APP_VIEW_CONTENT_WIDTH - x - ENERGY_DETAIL_PAD;
    const int h = APP_VIEW_CONTENT_HEIGHT - 2 * ENERGY_DETAIL_PAD;
    lv_obj_t *panel = lv_obj_create(ctx->root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, ENERGY_DETAIL_PAD);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_bg_color(panel, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, ui_style_radius(18), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 14, LV_PART_MAIN);
    ctx->detail_panel = panel;

    ctx->detail_title = lv_label_create(panel);
    lv_label_set_text(ctx->detail_title, "");
    lv_label_set_long_mode(ctx->detail_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ctx->detail_title, w - 28);
    energy_style_text(ctx->detail_title, theme_default_color_text_primary(), ENERGY_TITLE_FONT, LV_TEXT_ALIGN_LEFT);
    ui_text_record(ctx->detail_title, UI_TEXT_LABEL, true);
    lv_obj_set_pos(ctx->detail_title, 0, 0);

    const int title_h = (int)lv_font_get_line_height(ENERGY_TITLE_FONT);
    ctx->detail_status = lv_label_create(panel);
    lv_label_set_text(ctx->detail_status, ui_i18n_get("energy.details_prompt", "Select Gas, Water or Solar"));
    lv_label_set_long_mode(ctx->detail_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ctx->detail_status, w - 28);
    energy_style_text(ctx->detail_status, theme_default_color_text_muted(), ENERGY_STATS_FONT, LV_TEXT_ALIGN_LEFT);
    ui_text_record(ctx->detail_status, UI_TEXT_META, true);
    lv_obj_set_pos(ctx->detail_status, 0, title_h + 2);

    const int body_y = title_h + 2 + (int)lv_font_get_line_height(ENERGY_STATS_FONT) + 8;
    ctx->detail_body = lv_obj_create(panel);
    lv_obj_remove_style_all(ctx->detail_body);
    lv_obj_set_pos(ctx->detail_body, 0, body_y);
    lv_obj_set_size(ctx->detail_body, w - 28, h - 28 - body_y);
    lv_obj_set_scrollable(ctx->detail_body, false);
    lv_obj_update_layout(panel);
}

/* Makes the Gas, Water and Solar circles tappable and, on a wide display with the "side" choice, adds the panel. */
static void energy_setup_details(energy_page_ctx_t *ctx)
{
    ctx->detail_node = -1;
    for (int i = 0; i < ENERGY_NODE_COUNT; i++) {
        if (energy_node_has_details(i) && ctx->nodes[i].circle != NULL) {
            lv_obj_set_clickable(ctx->nodes[i].circle, true);
            lv_obj_add_event_cb(ctx->nodes[i].circle, energy_node_clicked, LV_EVENT_CLICKED, ctx);
        }
    }
    if (ctx->config.details_side && energy_side_panel_fits()) {
        energy_create_detail_panel(ctx);
    }
}

static void energy_page_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    energy_page_ctx_t *ctx = (energy_page_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->anim_timer != NULL) {
            lv_timer_del(ctx->anim_timer);
            ctx->anim_timer = NULL;
        }
        if (s_detail_ctx == ctx) {
            s_detail_ctx = NULL; /* a late answer finds no page and is freed */
        }
        free(ctx);
    }
}

static void energy_copy_json_string(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    const cJSON *item = (obj != NULL) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

void ui_energy_page_config_from_json(const cJSON *page_json, const char *page_id, const char *page_title,
    ui_energy_page_config_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->page_id, sizeof(out->page_id), "%s", page_id != NULL ? page_id : "");
    snprintf(out->title, sizeof(out->title), "%s", (page_title != NULL && page_title[0] != '\0') ? page_title : "Energy");
    snprintf(out->source, sizeof(out->source), "%s", "ha_energy");

    const cJSON *energy = (page_json != NULL) ? cJSON_GetObjectItemCaseSensitive(page_json, "energy") : NULL;
    if (!cJSON_IsObject(energy)) {
        return;
    }

    const cJSON *source_item = cJSON_GetObjectItemCaseSensitive(energy, "source");
    bool source_was_configured =
        cJSON_IsString(source_item) && source_item->valuestring != NULL && source_item->valuestring[0] != '\0';
    energy_copy_json_string(energy, "source", out->source, sizeof(out->source));
    if (strcmp(out->source, "manual_live") != 0 && strcmp(out->source, "ha_energy") != 0) {
        snprintf(out->source, sizeof(out->source), "%s", "ha_energy");
    }

    energy_copy_json_string(
        energy, "home_power_entity_id", out->home_power_entity_id, sizeof(out->home_power_entity_id));
    energy_copy_json_string(
        energy, "solar_power_entity_id", out->solar_power_entity_id, sizeof(out->solar_power_entity_id));
    energy_copy_json_string(
        energy, "grid_power_entity_id", out->grid_power_entity_id, sizeof(out->grid_power_entity_id));
    energy_copy_json_string(
        energy, "grid_import_power_entity_id", out->grid_import_power_entity_id, sizeof(out->grid_import_power_entity_id));
    energy_copy_json_string(
        energy, "grid_export_power_entity_id", out->grid_export_power_entity_id, sizeof(out->grid_export_power_entity_id));
    energy_copy_json_string(
        energy, "battery_power_entity_id", out->battery_power_entity_id, sizeof(out->battery_power_entity_id));
    energy_copy_json_string(energy,
        "battery_charge_power_entity_id",
        out->battery_charge_power_entity_id,
        sizeof(out->battery_charge_power_entity_id));
    energy_copy_json_string(energy,
        "battery_discharge_power_entity_id",
        out->battery_discharge_power_entity_id,
        sizeof(out->battery_discharge_power_entity_id));
    energy_copy_json_string(
        energy, "battery_soc_entity_id", out->battery_soc_entity_id, sizeof(out->battery_soc_entity_id));
    energy_copy_json_string(energy, "gas_entity_id", out->gas_entity_id, sizeof(out->gas_entity_id));
    energy_copy_json_string(energy, "water_entity_id", out->water_entity_id, sizeof(out->water_entity_id));
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(energy, "details");
    out->details_side = cJSON_IsString(details) && details->valuestring != NULL && strcmp(details->valuestring, "side") == 0;

    if (!source_was_configured &&
        (out->home_power_entity_id[0] != '\0' || out->solar_power_entity_id[0] != '\0' ||
            out->grid_power_entity_id[0] != '\0' || out->grid_import_power_entity_id[0] != '\0' ||
            out->grid_export_power_entity_id[0] != '\0' || out->battery_power_entity_id[0] != '\0' ||
            out->battery_charge_power_entity_id[0] != '\0' || out->battery_discharge_power_entity_id[0] != '\0' ||
            out->battery_soc_entity_id[0] != '\0' || out->gas_entity_id[0] != '\0' ||
            out->water_entity_id[0] != '\0')) {
        snprintf(out->source, sizeof(out->source), "%s", "manual_live");
    }
}

static esp_err_t ui_energy_page_create_inner(
    const ui_energy_page_config_t *config, lv_obj_t *parent, ui_energy_page_instance_t *out_instance)
{
    if (config == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    energy_page_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(energy_page_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->config = *config;

    energy_arrow_value_font_prepare();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, APP_VIEW_CONTENT_WIDTH, APP_VIEW_CONTENT_HEIGHT);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_bg_color(root, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    ctx->root = root;

    /* Stats area — bottom-left corner */
    ctx->stat_today_label = lv_label_create(root);
    lv_label_set_text(ctx->stat_today_label, "");
    lv_obj_set_width(ctx->stat_today_label, ENERGY_STATS_WIDTH);
    energy_style_text(ctx->stat_today_label, theme_default_color_text_muted(), ENERGY_STATS_FONT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(ctx->stat_today_label, ENERGY_STATS_X, ENERGY_STATS_TODAY_Y);

    ctx->stat_autarky_label = lv_label_create(root);
    lv_label_set_text(ctx->stat_autarky_label, "");
    lv_obj_set_width(ctx->stat_autarky_label, ENERGY_STATS_WIDTH);
    energy_style_text(ctx->stat_autarky_label, theme_default_color_text_muted(), ENERGY_STATS_FONT, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(ctx->stat_autarky_label, ENERGY_STATS_X, ENERGY_STATS_AUTARKY_Y);

    for (int i = 0; i < ENERGY_FLOW_COUNT; i++) {
        energy_create_flow(ctx, i, lv_color_hex(ENERGY_COLOR_LINE_IDLE));
    }

    energy_create_home_arc(ctx, ENERGY_ARC_SOLAR, lv_color_hex(ENERGY_COLOR_SOLAR));
    energy_create_home_arc(ctx, ENERGY_ARC_BATTERY, lv_color_hex(ENERGY_COLOR_BATTERY_OUT));
    energy_create_home_arc(ctx, ENERGY_ARC_GRID, lv_color_hex(ENERGY_COLOR_GRID));

    energy_create_node(ctx, ENERGY_NODE_GRID, ENERGY_NODE_GRID_X, ENERGY_NODE_GRID_Y, ENERGY_NODE_SIZE,
        ui_i18n_get("energy.grid", "Grid"), ENERGY_ICON_GRID, lv_color_hex(ENERGY_COLOR_GRID));
    energy_create_node(ctx, ENERGY_NODE_SOLAR, ENERGY_NODE_SOLAR_X, ENERGY_NODE_SOLAR_Y, ENERGY_NODE_SIZE,
        ui_i18n_get("energy.solar", "Solar"), ENERGY_ICON_SOLAR, lv_color_hex(ENERGY_COLOR_SOLAR));
    energy_create_node(ctx, ENERGY_NODE_HOME, ENERGY_NODE_HOME_X, ENERGY_NODE_HOME_Y, ENERGY_HOME_SIZE,
        "", ENERGY_ICON_HOME, theme_default_color_text_primary());
    energy_set_home_idle_ring(ctx, true);
    energy_create_node(ctx, ENERGY_NODE_BATTERY, ENERGY_NODE_BATTERY_X, ENERGY_NODE_BATTERY_Y, ENERGY_NODE_SIZE,
        ui_i18n_get("energy.battery", "Battery"), ENERGY_ICON_BATTERY, lv_color_hex(ENERGY_COLOR_BATTERY_IN));

    energy_create_node(ctx, ENERGY_NODE_GAS, ENERGY_NODE_GAS_X, ENERGY_NODE_GAS_Y, ENERGY_NODE_SIZE,
        ui_i18n_get("energy.gas", "Gas"), ENERGY_ICON_GAS, lv_color_hex(ENERGY_COLOR_GAS));
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_GAS], true);

    energy_create_node(ctx, ENERGY_NODE_WATER, ENERGY_NODE_WATER_X, ENERGY_NODE_WATER_Y, ENERGY_NODE_SIZE,
        ui_i18n_get("energy.water", "Water"), ENERGY_ICON_WATER, lv_color_hex(ENERGY_COLOR_WATER));
    energy_set_node_hidden(&ctx->nodes[ENERGY_NODE_WATER], true);

    energy_apply_static_flow_paths(ctx);
    energy_mark_missing_slots(ctx);
    energy_recompute(ctx);
    energy_setup_details(ctx);

    lv_obj_add_event_cb(root, energy_page_event_cb, LV_EVENT_DELETE, ctx);
    ctx->anim_timer = lv_timer_create(energy_anim_timer_cb, 50, ctx);

    memset(out_instance, 0, sizeof(*out_instance));
    snprintf(out_instance->page_id, sizeof(out_instance->page_id), "%s", config->page_id);
    out_instance->obj = root;
    out_instance->ctx = ctx;
    return ESP_OK;
}

static bool ui_energy_page_apply_state_inner(ui_energy_page_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return false;
    }

    energy_page_ctx_t *ctx = (energy_page_ctx_t *)instance->ctx;
    if (strcmp(ctx->config.source, ENERGY_SOURCE_MANUAL) != 0) {
        /* Home Assistant energy source: only battery state of charge and live solar power are ours. */
        bool ha_matched = energy_apply_slot_state(ctx, ENERGY_SLOT_BATTERY_SOC, state);
        if (energy_apply_slot_state(ctx, ENERGY_SLOT_SOLAR_POWER, state)) {
            ha_matched = true;
        }
        if (ha_matched) {
            energy_recompute(ctx);
        }
        return ha_matched;
    }
    bool matched = false;
    for (int i = 0; i < ENERGY_SLOT_COUNT; i++) {
        if (energy_apply_slot_state(ctx, (energy_slot_id_t)i, state)) {
            matched = true;
        }
    }
    if (matched) {
        energy_recompute(ctx);
    }
    return matched;
}

static void ui_energy_page_apply_all_states_inner(ui_energy_page_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }

    energy_page_ctx_t *ctx = (energy_page_ctx_t *)instance->ctx;
    if (strcmp(ctx->config.source, ENERGY_SOURCE_MANUAL) != 0) {
        const char *soc_entity = ctx->config.battery_soc_entity_id;
        ctx->slots[ENERGY_SLOT_BATTERY_SOC].configured = soc_entity[0] != '\0';
        ctx->slots[ENERGY_SLOT_BATTERY_SOC].valid = false;
        ha_state_t soc_state = {0};
        if (soc_entity[0] != '\0' && ha_model_get_state(soc_entity, &soc_state)) {
            (void)energy_apply_slot_state(ctx, ENERGY_SLOT_BATTERY_SOC, &soc_state);
        }
        const char *pv_entity = ctx->config.solar_power_entity_id;
        ctx->slots[ENERGY_SLOT_SOLAR_POWER].configured = pv_entity[0] != '\0';
        ctx->slots[ENERGY_SLOT_SOLAR_POWER].valid = false;
        ha_state_t pv_state = {0};
        if (pv_entity[0] != '\0' && ha_model_get_state(pv_entity, &pv_state)) {
            (void)energy_apply_slot_state(ctx, ENERGY_SLOT_SOLAR_POWER, &pv_state);
        }
        energy_recompute(ctx);
        return;
    }
    energy_mark_missing_slots(ctx);

    for (int i = 0; i < ENERGY_SLOT_COUNT; i++) {
        const char *entity_id = energy_entity_for_slot(ctx, (energy_slot_id_t)i);
        if (entity_id == NULL || entity_id[0] == '\0') {
            continue;
        }
        ha_state_t state = {0};
        if (ha_model_get_state(entity_id, &state)) {
            (void)energy_apply_slot_state(ctx, (energy_slot_id_t)i, &state);
        } else {
            ctx->slots[i].configured = true;
            ctx->slots[i].valid = false;
            ctx->slots[i].value = 0.0f;
            ctx->slots[i].unit[0] = '\0';
        }
    }
    energy_recompute(ctx);
}

/* The energy page lays its nodes out on fixed circles: its text grows by one step at most, and only on a wide screen (the tile cap of
 * "energy_page" is read from the display size). */
esp_err_t ui_energy_page_create(const ui_energy_page_config_t *config, lv_obj_t *parent, ui_energy_page_instance_t *out_instance)
{
    ui_fonts_tile_begin("energy_page", (int)lv_display_get_horizontal_resolution(NULL), (int)lv_display_get_vertical_resolution(NULL));
    const esp_err_t err = ui_energy_page_create_inner(config, parent, out_instance);
    ui_fonts_tile_end();
    return err;
}

bool ui_energy_page_apply_state(ui_energy_page_instance_t *instance, const ha_state_t *state)
{
    ui_fonts_tile_begin("energy_page", (int)lv_display_get_horizontal_resolution(NULL), (int)lv_display_get_vertical_resolution(NULL));
    const bool applied = ui_energy_page_apply_state_inner(instance, state);
    ui_fonts_tile_end();
    return applied;
}

void ui_energy_page_apply_all_states(ui_energy_page_instance_t *instance)
{
    ui_fonts_tile_begin("energy_page", (int)lv_display_get_horizontal_resolution(NULL), (int)lv_display_get_vertical_resolution(NULL));
    ui_energy_page_apply_all_states_inner(instance);
    ui_fonts_tile_end();
}
