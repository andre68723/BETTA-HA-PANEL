/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "util/distance_units.h"
#include "ui/ui_memory.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"

#define CAR_CHARGE_POWER_MAX_W 11000.0f
/* The optional extras (a status line with lock and interior temperature, the pre-conditioning button below it) need
 * this much room. */
#define CAR_EXTRAS_MIN_H 330
#define CAR_EXTRAS_MIN_W 200
#define CAR_EXTRAS_BTN_H 44 /* a touch target, as wide as the card */
#define CAR_EXTRAS_GAP 6
#define CAR_STATE_LEN 24
/* A cloud-backed car can take tens of seconds to report the new mode after Home Assistant accepted the call. The button
 * shows a neutral pending word meanwhile; this cap ends it if no state change ever arrives. */
#define CAR_PRECOND_PENDING_MS 60000

typedef struct {
    lv_obj_t *card;
    lv_obj_t *outer_arc;
    lv_obj_t *inner_arc;
    lv_obj_t *range_label;
    lv_obj_t *soc_label;
    lv_obj_t *soc_unit_label;
    lv_obj_t *power_label;
    lv_obj_t *title_label;
    lv_obj_t *lock_label;
    lv_obj_t *temp_label;
    lv_obj_t *precond_btn;
    lv_obj_t *precond_label;
    char precond_entity_id[APP_MAX_ENTITY_ID_LEN];
    char lock_entity_id[APP_MAX_ENTITY_ID_LEN];
    char temp_entity_id[APP_MAX_ENTITY_ID_LEN];
    char precond_state[CAR_STATE_LEN];
    bool has_precond;
    bool precond_pending;         /* a call was sent and the entity has not changed its on/off meaning yet */
    bool precond_pending_from_on; /* the on/off meaning when it was sent */
    lv_timer_t *precond_timer;
    int lock_kind; /* 0 unknown, 1 locked, 2 unlocked, 3 closed, 4 open */
    float interior_temp;
    char temp_unit[APP_MAX_UNIT_LEN];
    bool has_temp;
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
    /* Units come from entity attributes and never change in practice; once
     * extracted, skip the per-update attributes JSON parse. */
    bool power_unit_from_attrs;
    bool range_unit_from_attrs;
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

static bool car_has_extras(const w_car_widget_ctx_t *ctx)
{
    return ctx->precond_entity_id[0] != '\0' || ctx->lock_entity_id[0] != '\0' || ctx->temp_entity_id[0] != '\0';
}

static bool car_extras_visible(const w_car_widget_ctx_t *ctx, lv_coord_t w, lv_coord_t h)
{
    return car_has_extras(ctx) && h >= CAR_EXTRAS_MIN_H && w >= CAR_EXTRAS_MIN_W;
}

/* Height of the extras block: the status line (lock, interior temperature) and the pre-conditioning button, each when set. */
static lv_coord_t car_extras_height(const w_car_widget_ctx_t *ctx)
{
    const bool status = ctx->lock_entity_id[0] != '\0' || ctx->temp_entity_id[0] != '\0';
    const bool button = ctx->precond_entity_id[0] != '\0';
    return (status ? lv_font_get_line_height(ui_font_role(UI_TEXT_META)) : 0) + (status && button ? CAR_EXTRAS_GAP : 0) +
           (button ? CAR_EXTRAS_BTN_H : 0);
}

/* The status line holds the lock state on the left and the interior temperature on the right (one of them alone is
 * centred); the pre-conditioning button spans the card below it. Words such as "Entriegelt" or "Vorklimatisieren" no
 * longer share a third of the width each. */
static void car_layout_extras(w_car_widget_ctx_t *ctx, lv_coord_t w, lv_coord_t h, bool visible)
{
    const bool lock = visible && ctx->lock_entity_id[0] != '\0';
    const bool temp = visible && ctx->temp_entity_id[0] != '\0';
    const bool button = visible && ctx->precond_entity_id[0] != '\0';
    lv_obj_set_hidden(ctx->lock_label, !lock);
    lv_obj_set_hidden(ctx->temp_label, !temp);
    lv_obj_set_hidden(ctx->precond_btn, !button);
    if (!lock && !temp && !button) {
        return;
    }
    /* Essential: the layout check reports them when they are cut (the German words were, unnoticed). */
    ui_text_record(ctx->lock_label, UI_TEXT_META, true);
    ui_text_record(ctx->temp_label, UI_TEXT_META, true);
    ui_text_record(ctx->precond_label, UI_TEXT_META, true);
    const lv_coord_t pad = 12;
    const lv_coord_t inner_w = w - 2 * pad;
    lv_coord_t bottom = h - 12;
    if (button) {
        lv_obj_set_size(ctx->precond_btn, inner_w, CAR_EXTRAS_BTN_H);
        lv_obj_set_pos(ctx->precond_btn, pad, bottom - CAR_EXTRAS_BTN_H);
        /* "Vorklimatisieren" nearly fills a 200 px card's button: 4 px padding, and a step smaller where the board allows it. */
        const lv_font_t *font = ui_text_fit(lv_label_get_text(ctx->precond_label), ui_font_role(UI_TEXT_META), inner_w - 8,
                                            CAR_EXTRAS_BTN_H);
        lv_obj_set_style_text_font(ctx->precond_label, font, LV_PART_MAIN);
        lv_obj_set_width(ctx->precond_label, inner_w - 8);
        lv_obj_center(ctx->precond_label);
        bottom -= CAR_EXTRAS_BTN_H + CAR_EXTRAS_GAP;
    }
    if (!lock && !temp) {
        return;
    }
    const lv_coord_t line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t y = bottom - line;
    if (lock && temp) {
        /* The temperature keeps the width of a widest reading (a label in dot mode rewrites its own text, so it is not
         * measured); the lock word takes the rest and ends in dots if it must. */
        lv_point_t temp_size;
        lv_text_get_size(&temp_size, "-88.8°C", ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_coord_t temp_w = temp_size.x + 2 < inner_w / 2 ? temp_size.x + 2 : inner_w / 2;
        lv_obj_set_size(ctx->temp_label, temp_w, line);
        lv_obj_set_style_text_align(ctx->temp_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_pos(ctx->temp_label, pad + inner_w - temp_w, y);
        lv_obj_set_size(ctx->lock_label, inner_w - temp_w - 8, line);
        lv_obj_set_style_text_align(ctx->lock_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_pos(ctx->lock_label, pad, y);
        return;
    }
    lv_obj_t *only = lock ? ctx->lock_label : ctx->temp_label;
    lv_obj_set_size(only, inner_w, line);
    lv_obj_set_style_text_align(only, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(only, pad, y);
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
    const bool extras = car_extras_visible(ctx, w, h);
#if UI_TEXT_ADAPTIVE
    if (min_dim >= 190) {
        ui_text_set(ctx->range_label, UI_TEXT_VALUE, true);
        ui_text_set(ctx->soc_label, ctx->power_entity_id[0] ? UI_TEXT_VALUE : UI_TEXT_KEY, true);
        ui_text_set(ctx->soc_unit_label, UI_TEXT_LABEL, true);
        ui_text_set(ctx->title_label, UI_TEXT_LABEL, false);
    }
#endif
    /* The title sits in the ring's open segment, so the ring takes the whole card but the extras row. */
    const lv_coord_t extras_h = extras ? car_extras_height(ctx) + 12 : 0;
    lv_coord_t arc_size = min_dim - 24;
    if (arc_size > h - 24 - extras_h) {
        arc_size = h - 24 - extras_h;
    }
    if (arc_size < 96) {
        arc_size = 96;
    }
    lv_coord_t arc_top = 12;
    lv_coord_t center_x = w / 2;
    lv_coord_t arc_x = center_x - (arc_size / 2);

    lv_obj_set_size(ctx->outer_arc, arc_size, arc_size);
    lv_obj_set_pos(ctx->outer_arc, arc_x, arc_top);
    /* Inner band 6 px inside the outer one. */
    const lv_coord_t inset = lv_obj_get_style_arc_width(ctx->outer_arc, LV_PART_MAIN) + 6;
    lv_obj_set_size(ctx->inner_arc, arc_size - 2 * inset, arc_size - 2 * inset);
    lv_obj_set_pos(ctx->inner_arc, arc_x + inset, arc_top + inset);

    const lv_coord_t r = arc_size / 2;
    lv_coord_t center_y = arc_top + r;
    lv_obj_set_width(ctx->range_label, w - 20);
    lv_obj_align(ctx->range_label, LV_ALIGN_TOP_MID, 0, center_y - r * 3 / 5);
    /* Number and "%" as one centered group: the "%" sits right behind the digits. */
    lv_obj_set_width(ctx->soc_label, LV_SIZE_CONTENT);
    lv_obj_set_width(ctx->soc_unit_label, LV_SIZE_CONTENT);
    lv_obj_update_layout(ctx->card);
    const lv_coord_t soc_w = lv_obj_get_width(ctx->soc_label);
    const lv_coord_t unit_w = lv_obj_get_width(ctx->soc_unit_label);
    const lv_coord_t group_x = (w - (soc_w + 3 + unit_w)) / 2;
    lv_obj_align(ctx->soc_label, LV_ALIGN_TOP_LEFT, group_x, center_y - 24);
    lv_obj_align(ctx->soc_unit_label, LV_ALIGN_TOP_LEFT, group_x + soc_w + 3, center_y - 24 + 8);
    lv_obj_set_width(ctx->power_label, w - 36);
    lv_obj_align(ctx->power_label, LV_ALIGN_TOP_MID, 0, center_y + r * 3 / 10);
    /* The open segment is about 1.3 r wide at 0.74 r below the centre. */
    lv_obj_set_style_text_font(ctx->title_label, min_dim >= 190 ? ui_font_role(UI_TEXT_LABEL) : (arc_size >= 130 ? ui_font_role(UI_TEXT_META) : ui_font_role(UI_TEXT_META)), LV_PART_MAIN);
    const lv_coord_t title_h = lv_font_get_line_height(lv_obj_get_style_text_font(ctx->title_label, LV_PART_MAIN));
    lv_obj_set_width(ctx->title_label, r * 13 / 10);
    ui_widget_name_one_line(ctx->title_label);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_MID, 0, center_y + r * 37 / 50 - title_h / 2);
#if UI_TEXT_ADAPTIVE
    if (min_dim >= 190) {
        lv_obj_align(ctx->range_label, LV_ALIGN_TOP_MID, 0, center_y - 61);
        /* the "%" ends with the digits and the power starts below both, whatever their sizes */
        const lv_coord_t soc_y = center_y - 18;
        const lv_coord_t soc_h = lv_obj_get_height(ctx->soc_label);
        const lv_coord_t power_y = soc_y + soc_h > center_y + 20 ? soc_y + soc_h : center_y + 20;
        lv_obj_align(ctx->soc_label, LV_ALIGN_TOP_LEFT, group_x, soc_y);
        lv_obj_align(ctx->soc_unit_label, LV_ALIGN_TOP_LEFT, group_x + soc_w + 3, soc_y + soc_h - lv_obj_get_height(ctx->soc_unit_label));
        lv_obj_align(ctx->power_label, LV_ALIGN_TOP_MID, 0, power_y);
        /* a ring too small for the power above the name (larger text on a small tile) leaves the power out */
        const bool power_fits = !ui_widget_name_enabled(ctx->title_label) ||
            power_y + lv_obj_get_height(ctx->power_label) <= center_y + r * 37 / 50 - title_h / 2;
        lv_obj_set_hidden(ctx->power_label, !power_fits || ctx->power_entity_id[0] == '\0');
    }
#endif
    car_layout_extras(ctx, w, h, extras);
}

static bool car_state_is_off(const char *state)
{
    return state == NULL || state[0] == '\0' || strcasecmp(state, "off") == 0 || strcasecmp(state, "none") == 0 ||
           strcasecmp(state, "idle") == 0 || strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

static bool car_precond_active(const w_car_widget_ctx_t *ctx)
{
    const bool is_toggle = strncmp(ctx->precond_entity_id, "switch.", 7) == 0 ||
                           strncmp(ctx->precond_entity_id, "input_boolean.", 14) == 0;
    const bool is_climate = strncmp(ctx->precond_entity_id, "climate.", 8) == 0;
    const bool climate_on = strcmp(ctx->precond_state, "heat") == 0 || strcmp(ctx->precond_state, "cool") == 0 ||
        strcmp(ctx->precond_state, "heat_cool") == 0 || strcmp(ctx->precond_state, "auto") == 0 ||
        strcmp(ctx->precond_state, "dry") == 0 || strcmp(ctx->precond_state, "fan_only") == 0;
    return ctx->has_precond && ((is_toggle && strcmp(ctx->precond_state, "on") == 0) || (is_climate && climate_on));
}

/* Only entities whose on/off state follows the call can show a pending state; select and button have no such end. */
static bool car_precond_can_pend(const w_car_widget_ctx_t *ctx)
{
    return strncmp(ctx->precond_entity_id, "climate.", 8) == 0 || strncmp(ctx->precond_entity_id, "switch.", 7) == 0 ||
           strncmp(ctx->precond_entity_id, "input_boolean.", 14) == 0;
}

static void car_precond_pending_clear(w_car_widget_ctx_t *ctx)
{
    ctx->precond_pending = false;
    if (ctx->precond_timer != NULL) {
        lv_timer_delete(ctx->precond_timer);
        ctx->precond_timer = NULL;
    }
}

static void car_render(w_car_widget_ctx_t *ctx);

static void car_precond_timeout_cb(lv_timer_t *timer)
{
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL) {
        return;
    }
    ctx->precond_timer = NULL; /* one-shot: LVGL deletes it after this run */
    if (ctx->precond_pending) {
        ctx->precond_pending = false;
        car_render(ctx);
    }
}

static void car_render_extras(w_car_widget_ctx_t *ctx)
{
    /* Lock / door status */
    const char *lock_text = "--";
    lv_color_t lock_color = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    switch (ctx->lock_kind) {
    case 1:
        lock_text = ui_i18n_get("binary.locked", "Locked");
        lock_color = lv_color_hex(APP_UI_COLOR_OK);
        break;
    case 2:
        lock_text = ui_i18n_get("binary.unlocked", "Unlocked");
        lock_color = lv_color_hex(APP_UI_COLOR_LIGHT_ICON_ON);
        break;
    case 3:
        lock_text = ui_i18n_get("contact.closed", "Closed");
        lock_color = lv_color_hex(APP_UI_COLOR_OK);
        break;
    case 4:
        lock_text = ui_i18n_get("contact.open", "Open");
        lock_color = lv_color_hex(APP_UI_COLOR_LIGHT_ICON_ON);
        break;
    default:
        break;
    }
    lv_label_set_text(ctx->lock_label, lock_text);
    lv_obj_set_style_text_color(ctx->lock_label, lock_color, LV_PART_MAIN);

    char text[32];
    if (ctx->has_temp) {
        const char *unit = ctx->temp_unit[0] ? ctx->temp_unit : "°C";
        if (strcmp(unit, "°C") == 0) {
            ui_format_temp_c(text, sizeof(text), true, ctx->interior_temp);
        } else {
            snprintf(text, sizeof(text), "%.1f%s", (double)ctx->interior_temp, unit);
        }
    } else {
        snprintf(text, sizeof(text), "--");
    }
    lv_label_set_text(ctx->temp_label, text);

    /* Pre-conditioning button: a select shows its option, everything else a fixed word. */
    const bool is_select = strncmp(ctx->precond_entity_id, "select.", 7) == 0;
    const bool active = car_precond_active(ctx);
    lv_label_set_text(ctx->precond_label,
        ctx->precond_pending ? ui_i18n_get("car.precond_pending", "Sending...")
        : is_select && ctx->has_precond ? ctx->precond_state : ui_i18n_get("car.precond", "Precondition"));
    const bool available = ctx->has_precond && strcmp(ctx->precond_state, "unavailable") != 0 &&
                           (strncmp(ctx->precond_entity_id, "button.", 7) == 0 ||
                            strncmp(ctx->precond_entity_id, "input_button.", 13) == 0 || strcmp(ctx->precond_state, "unknown") != 0);
    ui_style_action_button(ctx->precond_btn, ctx->precond_label, active ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, available);
}

static void car_precond_cb(lv_event_t *event)
{
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->precond_entity_id[0] == '\0' || ctx->precond_pending) {
        return; /* a second tap while the first is unanswered would toggle it back */
    }
    const char *id = ctx->precond_entity_id;
    const char *domain = "switch";
    const char *service = "toggle";
    if (strncmp(id, "select.", 7) == 0) {
        domain = "select";
        service = "select_next";
    } else if (strncmp(id, "climate.", 8) == 0) {
        domain = "climate";
        service = car_state_is_off(ctx->precond_state) ? "turn_on" : "turn_off";
    } else if (strncmp(id, "input_boolean.", 14) == 0) {
        domain = "input_boolean";
    } else if (strncmp(id, "button.", 7) == 0) {
        domain = "button";
        service = "press";
    } else if (strncmp(id, "input_button.", 13) == 0) {
        domain = "input_button";
        service = "press";
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 40];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"%s}", id, strcmp(domain, "select") == 0 ? ",\"cycle\":true" : "");
    if (ha_client_call_service(domain, service, payload) == ESP_OK && ctx->has_precond && car_precond_can_pend(ctx)) {
        ctx->precond_pending = true;
        ctx->precond_pending_from_on = car_precond_active(ctx);
        if (ctx->precond_timer != NULL) {
            lv_timer_delete(ctx->precond_timer);
        }
        ctx->precond_timer = lv_timer_create(car_precond_timeout_cb, CAR_PRECOND_PENDING_MS, ctx);
        if (ctx->precond_timer != NULL) {
            lv_timer_set_repeat_count(ctx->precond_timer, 1);
        }
        car_render(ctx);
    }
}

static void car_render(w_car_widget_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    const int32_t soc_pct = ctx->has_soc ? car_clamped_percent(ctx->soc) : 0;
    lv_arc_set_value(ctx->outer_arc, soc_pct);
    lv_obj_set_style_arc_color(ctx->outer_arc, ui_style_fill_level_color(soc_pct, 0, 100), LV_PART_INDICATOR);
    /* No charge-power entity at all: hide the ring itself, not just its label (an empty ring
     * stuck at 0% is still a ring, and confusingly reads as "always at zero" rather than "off"). */
    lv_obj_set_hidden(ctx->inner_arc, ctx->power_entity_id[0] == '\0');
    float power_pct = ctx->has_power ? (car_power_as_watts(ctx) * 100.0f / CAR_CHARGE_POWER_MAX_W) : 0.0f;
    const int32_t power_pct_i32 = car_clamped_percent(power_pct);
    lv_arc_set_value(ctx->inner_arc, power_pct_i32);
    lv_obj_set_style_arc_color(ctx->inner_arc, ui_style_fill_level_color(power_pct_i32, 0, 100), LV_PART_INDICATOR);

    char text[48] = {0};
    if (ctx->has_range) {
        /* Always kilometres: some cars report metres (id4) or miles. */
        double range = (double)ctx->range;
        const char *unit = ctx->range_unit[0] ? ctx->range_unit : "km";
        if (distance_to_km(unit, &range)) {
            unit = "km";
        }
        snprintf(text, sizeof(text), "%.0f %s", range, unit);
    } else {
        snprintf(text, sizeof(text), "-- km");
    }
    lv_label_set_text(ctx->range_label, text);

    snprintf(text, sizeof(text), "%d", ctx->has_soc ? car_clamped_percent(ctx->soc) : 0);
    lv_label_set_text(ctx->soc_label, ctx->has_soc ? text : "--");

    if (ctx->power_entity_id[0] == '\0') {
        /* No charge-power entity configured for this car at all: hide the placeholder
         * instead of a permanent "-- W" that can never turn into a real reading. */
        lv_obj_set_hidden(ctx->power_label, true);
    } else {
        lv_obj_set_hidden(ctx->power_label, false);
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
    }
    car_render_extras(ctx);
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
        car_precond_pending_clear(ctx);
        free(ctx);
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
    /* Optional extras: pre-conditioning, lock / door status, interior temperature. */
    snprintf(ctx->precond_entity_id, sizeof(ctx->precond_entity_id), "%s", def->extra_entity_id[UI_CAR_EXTRA_PRECOND]);
    snprintf(ctx->lock_entity_id, sizeof(ctx->lock_entity_id), "%s", def->extra_entity_id[UI_CAR_EXTRA_LOCK]);
    snprintf(ctx->temp_entity_id, sizeof(ctx->temp_entity_id), "%s", def->extra_entity_id[UI_CAR_EXTRA_TEMP]);

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_scrollable(card, false);
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
        lv_obj_set_clickable(arc, false);
        ui_style_gauge_arc(arc, i == 0 ? 16 : 12, i == 0 ? 14 : 10,
                           lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 30),
                           lv_color_hex(APP_UI_COLOR_CARD_BORDER),
                           lv_color_hex(i == 0 ? APP_UI_COLOR_STATE_ON : APP_UI_COLOR_WEATHER_ICON));
    }

    ctx->range_label = lv_label_create(card);
    ctx->soc_label = lv_label_create(card);
    ctx->soc_unit_label = lv_label_create(card);
    ctx->power_label = lv_label_create(card);
    ctx->title_label = ui_widget_name_create(card, !def->hide_name);

    lv_obj_set_style_text_color(ctx->range_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->soc_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->soc_unit_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->power_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->range_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->soc_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->soc_unit_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->power_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->range_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->soc_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->power_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    ctx->lock_label = lv_label_create(card);
    ctx->temp_label = lv_label_create(card);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = i == 0 ? ctx->lock_label : ctx->temp_label;
        lv_obj_set_style_text_font(l, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_style_text_color(ctx->temp_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    ctx->precond_btn = lv_btn_create(card);
    lv_obj_set_scrollable(ctx->precond_btn, false);
    lv_obj_set_style_radius(ctx->precond_btn, ui_style_radius(12), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->precond_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(ctx->precond_btn, car_precond_cb, LV_EVENT_CLICKED, ctx);
    ctx->precond_label = lv_label_create(ctx->precond_btn);
    lv_obj_set_style_text_font(ctx->precond_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->precond_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ctx->precond_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(ctx->precond_label);
    lv_label_set_text(ctx->soc_unit_label, "%");
    lv_label_set_text(ctx->title_label, def->title[0] ? def->title : "Car");
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(ctx->range_label, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(ctx->power_label, LV_LABEL_LONG_CLIP);

    lv_obj_add_event_cb(card, car_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, car_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    car_render(ctx);
    return ESP_OK;
}

/* lock.* entities and binary sensors of class lock (on = unlocked) or door / window (on = open). */
static void car_apply_lock(w_car_widget_ctx_t *ctx, const ha_state_t *state)
{
    ctx->lock_kind = 0;
    if (car_state_is_unavailable(state->state)) {
        return;
    }
    if (strncmp(ctx->lock_entity_id, "lock.", 5) == 0) {
        ctx->lock_kind = strcmp(state->state, "locked") == 0 ? 1 : 2;
        return;
    }
    bool door_like = false;
    if (state->attributes_json[0] != '\0') {
        cJSON *attrs = cJSON_Parse(state->attributes_json);
        if (attrs != NULL) {
            cJSON *cls = cJSON_GetObjectItemCaseSensitive(attrs, "device_class");
            if (cJSON_IsString(cls) && cls->valuestring != NULL) {
                door_like = strcmp(cls->valuestring, "door") == 0 || strcmp(cls->valuestring, "window") == 0 ||
                            strcmp(cls->valuestring, "opening") == 0 || strcmp(cls->valuestring, "garage_door") == 0;
            }
            cJSON_Delete(attrs);
        }
    }
    const bool on = strcmp(state->state, "on") == 0;
    if (door_like) {
        ctx->lock_kind = on ? 4 : 3;
    } else {
        ctx->lock_kind = on ? 2 : 1;
    }
}

static bool car_set_reading(bool *valid, float *stored, bool next_valid, float next)
{
    const bool changed = *valid != next_valid || (next_valid && *stored != next);
    *valid = next_valid;
    if (next_valid) *stored = next;
    return changed;
}

void w_car_widget_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)instance->ctx;
    float value = 0.0f;
    bool changed = false;
    if (ctx->lock_entity_id[0] != '\0' && strncmp(state->entity_id, ctx->lock_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const int previous = ctx->lock_kind;
        car_apply_lock(ctx, state);
        changed = previous != ctx->lock_kind;
    } else if (ctx->temp_entity_id[0] != '\0' && strncmp(state->entity_id, ctx->temp_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const bool valid = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        changed = car_set_reading(&ctx->has_temp, &ctx->interior_temp, valid, value);
        if (ctx->has_temp) {
            char previous[APP_MAX_UNIT_LEN];
            strlcpy(previous, ctx->temp_unit, sizeof(previous));
            car_state_unit(state, ctx->temp_unit, sizeof(ctx->temp_unit));
            changed |= strcmp(previous, ctx->temp_unit) != 0;
        }
    } else if (ctx->precond_entity_id[0] != '\0' && strncmp(state->entity_id, ctx->precond_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const bool valid = !car_state_is_unavailable(state->state);
        char next[CAR_STATE_LEN];
        strlcpy(next, state->state, sizeof(next));
        changed = ctx->has_precond != valid || strcmp(ctx->precond_state, next) != 0;
        ctx->has_precond = valid;
        strlcpy(ctx->precond_state, next, sizeof(ctx->precond_state));
        if (ctx->precond_pending && (!valid || car_precond_active(ctx) != ctx->precond_pending_from_on)) {
            car_precond_pending_clear(ctx); /* the entity answered, or went away */
            changed = true;
        }
    } else if (strncmp(state->entity_id, ctx->battery_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const bool valid = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        changed = car_set_reading(&ctx->has_soc, &ctx->soc, valid, value);
    } else if (ctx->power_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->power_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const bool valid = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        changed = car_set_reading(&ctx->has_power, &ctx->power_w, valid, value);
        if (ctx->has_power) {
            if (!ctx->power_unit_from_attrs) {
                char previous[APP_MAX_UNIT_LEN];
                strlcpy(previous, ctx->power_unit, sizeof(previous));
                car_state_unit(state, ctx->power_unit, sizeof(ctx->power_unit));
                if (ctx->power_unit[0] != '\0') {
                    ctx->power_unit_from_attrs = true;
                } else {
                    snprintf(ctx->power_unit, sizeof(ctx->power_unit), "W");
                }
                changed |= strcmp(previous, ctx->power_unit) != 0;
            }
        }
    } else if (ctx->range_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->range_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        const bool valid = !car_state_is_unavailable(state->state) && car_parse_float(state->state, &value);
        changed = car_set_reading(&ctx->has_range, &ctx->range, valid, value);
        if (ctx->has_range) {
            if (!ctx->range_unit_from_attrs) {
                char previous[APP_MAX_UNIT_LEN];
                strlcpy(previous, ctx->range_unit, sizeof(previous));
                car_state_unit(state, ctx->range_unit, sizeof(ctx->range_unit));
                if (ctx->range_unit[0] != '\0') {
                    ctx->range_unit_from_attrs = true;
                } else {
                    snprintf(ctx->range_unit, sizeof(ctx->range_unit), "km");
                }
                changed |= strcmp(previous, ctx->range_unit) != 0;
            }
        }
    }
    if (changed) car_render(ctx);
}

void w_car_widget_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_car_widget_ctx_t *ctx = (w_car_widget_ctx_t *)instance->ctx;
    const bool changed = ctx->has_soc || ctx->has_power || ctx->has_range || ctx->has_temp ||
                         ctx->has_precond || ctx->lock_kind != 0;
    ctx->has_soc = false;
    ctx->has_power = false;
    ctx->has_range = false;
    ctx->has_temp = false;
    ctx->has_precond = false;
    ctx->lock_kind = 0;
    if (ctx->precond_pending) {
        car_precond_pending_clear(ctx);
    }
    if (changed) car_render(ctx);
}
