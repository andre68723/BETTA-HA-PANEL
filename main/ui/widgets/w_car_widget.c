/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ui/fonts/app_text_fonts.h"
#include "ui/ui_memory.h"
#include "ui/theme/theme_default.h"

#define CAR_CHARGE_POWER_MAX_W 11000.0f

typedef struct {
    lv_obj_t *card;
    lv_obj_t *outer_arc;
    lv_obj_t *inner_arc;
    lv_obj_t *range_label;
    lv_obj_t *soc_label;
    lv_obj_t *soc_unit_label;
    lv_obj_t *power_label;
    lv_obj_t *icon_label;
    lv_obj_t *title_label;
    char battery_entity_id[APP_MAX_ENTITY_ID_LEN];
    char power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char range_entity_id[APP_MAX_ENTITY_ID_LEN];
    float soc;
    float power_w;
    float range;
    char range_unit[APP_MAX_UNIT_LEN];
    char power_unit[APP_MAX_UNIT_LEN];
    bool has_soc;
    bool has_power;
    bool has_range;
} w_car_widget_ctx_t;

static bool car_state_is_unavailable(const char *state)
{
    return state == NULL || state[0] == '\0' || strcmp(state, "unknown") == 0 || strcmp(state, "unavailable") == 0;
}

static bool car_parse_float(const char *text, float *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    char *end = NULL;
    float value = strtof(text, &end);
    if (end == text || value != value || value > 1000000000.0f || value < -1000000000.0f) {
        return false;
    }
    *out = value;
    return true;
}

static void car_state_unit(const ha_state_t *state, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (state == NULL || state->attributes_json[0] == '\0') {
        return;
    }
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs == NULL) {
        return;
    }
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
    if (cJSON_IsString(unit) && unit->valuestring != NULL) {
        snprintf(out, out_len, "%s", unit->valuestring);
    }
    cJSON_Delete(attrs);
}

static int car_clamped_percent(float value)
{
    if (value != value) {
        return 0;
    }
    if (value < 0.0f) {
        return 0;
    }
    if (value > 100.0f) {
        return 100;
    }
    return (int)(value + 0.5f);
}

static float car_power_as_watts(const w_car_widget_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0.0f;
    }
    if ((strcmp(ctx->power_unit, "kW") == 0) || (strcmp(ctx->power_unit, "kw") == 0)) {
        return ctx->power_w * 1000.0f;
    }
    return ctx->power_w;
}

static void car_layout(w_car_widget_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return;
    }
    lv_obj_update_layout(ctx->card);
    lv_coord_t w = lv_obj_get_width(ctx->card);
    lv_coord_t h = lv_obj_get_height(ctx->card);
    lv_coord_t min_dim = (w < h) ? w : h;
    lv_coord_t arc_size = min_dim - 28;
    if (arc_size > h - 72) {
        arc_size = h - 72;
    }
    if (arc_size < 130) {
        arc_size = min_dim - 14;
    }
    if (arc_size < 96) {
        arc_size = 96;
    }
    lv_coord_t arc_top = 12;
    lv_coord_t center_x = w / 2;
    lv_coord_t arc_x = center_x - (arc_size / 2);

    lv_obj_set_size(ctx->outer_arc, arc_size, arc_size);
    lv_obj_set_pos(ctx->outer_arc, arc_x, arc_top);
    lv_obj_set_size(ctx->inner_arc, arc_size - 28, arc_size - 28);
    lv_obj_set_pos(ctx->inner_arc, arc_x + 14, arc_top + 14);

    lv_coord_t center_y = arc_top + arc_size / 2;
    lv_obj_set_width(ctx->range_label, w - 20);
    lv_obj_align(ctx->range_label, LV_ALIGN_TOP_MID, 0, center_y - 56);
    lv_obj_set_width(ctx->soc_label, w - 64);
    lv_obj_align(ctx->soc_label, LV_ALIGN_TOP_MID, -8, center_y - 24);
    lv_obj_align_to(ctx->soc_unit_label, ctx->soc_label, LV_ALIGN_OUT_RIGHT_MID, -2, 2);
    lv_obj_set_width(ctx->power_label, w - 36);
    lv_obj_align(ctx->power_label, LV_ALIGN_TOP_MID, 0, center_y + 36);
    lv_obj_align(ctx->icon_label, LV_ALIGN_TOP_MID, 0, center_y + 64);
    lv_obj_set_width(ctx->title_label, w - 20);
    lv_obj_align(ctx->title_label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void car_render(w_car_widget_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    lv_arc_set_value(ctx->outer_arc, ctx->has_soc ? car_clamped_percent(ctx->soc) : 0);
    float power_pct = ctx->has_power ? (car_power_as_watts(ctx) * 100.0f / CAR_CHARGE_POWER_MAX_W) : 0.0f;
    lv_arc_set_value(ctx->inner_arc, car_clamped_percent(power_pct));

    char text[48] = {0};
    if (ctx->has_range) {
        snprintf(text, sizeof(text), "%.0f %s", (double)ctx->range, ctx->range_unit[0] ? ctx->range_unit : "km");
    } else {
        snprintf(text, sizeof(text), "-- km");
    }
    lv_label_set_text(ctx->range_label, text);

    snprintf(text, sizeof(text), "%d", ctx->has_soc ? car_clamped_percent(ctx->soc) : 0);
    lv_label_set_text(ctx->soc_label, ctx->has_soc ? text : "--");

    if (ctx->has_power) {
        float power_w = car_power_as_watts(ctx);
        float abs_power = power_w < 0.0f ? -power_w : power_w;
        if (abs_power >= 10000.0f) {
            snprintf(text, sizeof(text), "%.1f kW", (double)(power_w / 1000.0f));
        } else {
            snprintf(text, sizeof(text), "%.0f W", (double)power_w);
        }
    } else {
        snprintf(text, sizeof(text), "-- W");
    }
    lv_label_set_text(ctx->power_label, text);
    car_layout(ctx);
}

static void car_event_cb(lv_event_t *event)
{
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        lv_free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        car_layout(ctx);
    }
}

esp_err_t w_car_widget_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    w_car_widget_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(ctx->battery_entity_id, sizeof(ctx->battery_entity_id), "%s", def->entity_id);
    snprintf(ctx->power_entity_id, sizeof(ctx->power_entity_id), "%s", def->secondary_entity_id);
    snprintf(ctx->range_entity_id, sizeof(ctx->range_entity_id), "%s", def->tertiary_entity_id);
    snprintf(ctx->range_unit, sizeof(ctx->range_unit), "km");
    snprintf(ctx->power_unit, sizeof(ctx->power_unit), "W");

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);

    ctx->outer_arc = lv_arc_create(card);
    ctx->inner_arc = lv_arc_create(card);
    lv_obj_t *arcs[] = { ctx->outer_arc, ctx->inner_arc };
    for (size_t i = 0; i < 2U; i++) {
        lv_obj_t *arc = arcs[i];
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc, i == 0 ? 8 : 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, i == 0 ? 8 : 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(i == 0 ? APP_UI_COLOR_STATE_ON : APP_UI_COLOR_WEATHER_ICON), LV_PART_INDICATOR);
    }

    ctx->range_label = lv_label_create(card);
    ctx->soc_label = lv_label_create(card);
    ctx->soc_unit_label = lv_label_create(card);
    ctx->power_label = lv_label_create(card);
    ctx->icon_label = lv_label_create(card);
    ctx->title_label = lv_label_create(card);

    lv_obj_set_style_text_color(ctx->range_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->soc_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->soc_unit_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->power_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(APP_UI_COLOR_STATE_ON), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->range_label, APP_FONT_TEXT_24, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->soc_label, APP_FONT_TEXT_34, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->soc_unit_label, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->power_label, APP_FONT_TEXT_22, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->icon_label, APP_FONT_TEXT_28, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->title_label, APP_FONT_TEXT_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->range_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->soc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->power_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(ctx->soc_unit_label, "%");
    lv_label_set_text(ctx->icon_label, "%");
    lv_label_set_text(ctx->title_label, def->title[0] ? def->title : "Car");
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(ctx->range_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(ctx->power_label, LV_LABEL_LONG_CLIP);

    lv_obj_add_event_cb(card, car_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, car_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    car_render(ctx);
    return ESP_OK;
}

void w_car_widget_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)instance->ctx;
    float value = 0.0f;
    if (strncmp(state->entity_id, ctx->battery_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        ctx->has_soc = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        if (ctx->has_soc) {
            ctx->soc = value;
        }
    } else if (ctx->power_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->power_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        ctx->has_power = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        if (ctx->has_power) {
            ctx->power_w = value;
            car_state_unit(state, ctx->power_unit, sizeof(ctx->power_unit));
            if (ctx->power_unit[0] == '\0') {
                snprintf(ctx->power_unit, sizeof(ctx->power_unit), "W");
            }
        }
    } else if (ctx->range_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->range_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        ctx->has_range = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        if (ctx->has_range) {
            ctx->range = value;
            car_state_unit(state, ctx->range_unit, sizeof(ctx->range_unit));
            if (ctx->range_unit[0] == '\0') {
                snprintf(ctx->range_unit, sizeof(ctx->range_unit), "km");
            }
        }
    }
    car_render(ctx);
}

void w_car_widget_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)instance->ctx;
    ctx->has_soc = false;
    ctx->has_power = false;
    ctx->has_range = false;
    car_render(ctx);
}
