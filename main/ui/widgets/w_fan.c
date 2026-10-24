/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Fan tile. Flat (height below FAN_LARGE_MIN_H): name, state and speed on the
 * left; slower / power / faster buttons on the right. Large: a rotor picture
 * (turns while the fan runs), speed step chips and a row with oscillation and
 * the preset modes the device reports.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#define W_FAN_FEAT_SET_SPEED 1U
#define W_FAN_FEAT_OSCILLATE 2U
#define W_FAN_FEAT_PRESET_MODE 8U

#define FAN_LARGE_MIN_H 220
#define FAN_MAX_STEPS 6
#define FAN_MAX_PRESETS 3
#define FAN_PRESET_LEN 24
#define FAN_SPIN_PERIOD_MS 150
#define FAN_BLADES 7
#define FAN_BLADE_SPAN 26 /* degrees of one blade; the gap between blades is the rest of 360 / FAN_BLADES */

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *state;
    lv_obj_t *bar;
    lv_obj_t *btn_minus;
    lv_obj_t *btn_power;
    lv_obj_t *btn_plus;
    lv_obj_t *rotor;
    lv_obj_t *blades[FAN_BLADES];
    lv_obj_t *hub;
    lv_obj_t *step_btn[FAN_MAX_STEPS];
    lv_obj_t *osc_btn;
    lv_obj_t *preset_btn[FAN_MAX_PRESETS];
    lv_timer_t *spin_timer;
    int spin_angle;
    bool large;
    bool on;
    bool oscillating;
    float step; /* percentage_step of the device, 0 unknown */
    int step_count;
    char preset_mode[FAN_PRESET_LEN];
    char presets[FAN_MAX_PRESETS][FAN_PRESET_LEN];
    int preset_count;
    int percentage; /* -1 unknown */
    uint32_t features;
    bool features_known;
    bool unavailable;
} w_fan_ctx_t;

static void fan_call(w_fan_ctx_t *ctx, const char *service)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->unavailable) {
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity_id);
    (void)ha_client_call_service("fan", service, payload);
}

static void fan_call_json(w_fan_ctx_t *ctx, const char *service, const char *key, cJSON *value)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->unavailable) {
        cJSON_Delete(value);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(value);
        return;
    }
    cJSON_AddStringToObject(root, "entity_id", ctx->entity_id);
    cJSON_AddItemToObject(root, key, value);
    char *payload = cJSON_PrintUnformatted(root);
    if (payload != NULL) {
        (void)ha_client_call_service("fan", service, payload);
        cJSON_free(payload);
    }
    cJSON_Delete(root);
}

static void fan_step_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    const int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)) + 1;
    if (ctx == NULL || ctx->step_count < 1) {
        return;
    }
    int pct = (int)(idx * ctx->step + 0.5f);
    if (pct > 100 || idx >= ctx->step_count) pct = 100;
    fan_call_json(ctx, "set_percentage", "percentage", cJSON_CreateNumber(pct));
}

static void fan_osc_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    if (ctx != NULL) {
        fan_call_json(ctx, "oscillate", "oscillating", cJSON_CreateBool(!ctx->oscillating));
    }
}

static void fan_preset_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    const int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (ctx != NULL && idx >= 0 && idx < ctx->preset_count) {
        fan_call_json(ctx, "set_preset_mode", "preset_mode", cJSON_CreateString(ctx->presets[idx]));
    }
}

static void fan_minus_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    fan_call(ctx, "decrease_speed");
}

static void fan_plus_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    fan_call(ctx, "increase_speed");
}

static void fan_power_cb(lv_event_t *e)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(e);
    fan_call(ctx, ctx != NULL && ctx->on ? "turn_off" : "turn_on");
}

static lv_obj_t *fan_make_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, w_fan_ctx_t *ctx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *fan_make_chip(lv_obj_t *parent, lv_event_cb_t cb, w_fan_ctx_t *ctx, int idx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, ui_style_radius(12), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_user_data(btn, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

static void fan_style_button(lv_obj_t *btn, bool active, bool enabled)
{
    ui_style_action_button(btn, lv_obj_get_child(btn, 0), active ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, enabled);
}

static void fan_show(lv_obj_t *obj, bool show)
{
    if (obj == NULL) {
        return;
    }
    if (show) {
        lv_obj_set_hidden(obj, false);
    } else {
        lv_obj_set_hidden(obj, true);
    }
}

static void fan_layout_flat(w_fan_ctx_t *ctx, lv_coord_t w, lv_coord_t h)
{
    const bool speed = !ctx->features_known || (ctx->features & W_FAN_FEAT_SET_SPEED) != 0U;
    lv_coord_t btn = h - 24;
    if (btn > 44) btn = 44;
    if (btn < 32) btn = 32;
    const lv_coord_t gap = 10;
    const int shown = speed ? 3 : 1;
    const lv_coord_t buttons_w = shown * btn + (shown - 1) * gap;
    const lv_coord_t pad = 14;
    lv_coord_t x = w - pad - buttons_w;
    const lv_coord_t y = (h - btn) / 2;

    lv_obj_t *btns[3] = {ctx->btn_minus, ctx->btn_power, ctx->btn_plus};
    for (int i = 0; i < 3; i++) {
        if (!speed && i != 1) {
            lv_obj_set_hidden(btns[i], true);
            continue;
        }
        lv_obj_set_hidden(btns[i], false);
        lv_obj_set_size(btns[i], btn, btn);
        lv_obj_set_pos(btns[i], x, y);
        x += btn + gap;
    }

    lv_coord_t text_w = w - pad - buttons_w - 2 * pad;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t bar_h = 6;
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title, 2) + state_h + 6 + bar_h)) / 2;
    lv_obj_set_size(ctx->title, text_w, title_h);
    lv_obj_set_size(ctx->state, text_w, state_h);
    lv_obj_set_pos(ctx->title, pad, top);
    lv_obj_set_pos(ctx->state, pad, top + title_h + ui_widget_name_gap(ctx->title, 2));
    lv_obj_set_size(ctx->bar, text_w, bar_h);
    lv_obj_set_pos(ctx->bar, pad, top + title_h + ui_widget_name_gap(ctx->title, 2) + state_h + 6);
    fan_show(ctx->bar, true);
    fan_show(ctx->btn_minus, speed);
    fan_show(ctx->btn_plus, speed);
    fan_show(ctx->rotor, false);
    fan_show(ctx->osc_btn, false);
    for (int i = 0; i < FAN_MAX_STEPS; i++) fan_show(ctx->step_btn[i], false);
    for (int i = 0; i < FAN_MAX_PRESETS; i++) fan_show(ctx->preset_btn[i], false);
}

/* Place `count` equally wide buttons in one row. */
static void fan_place_row(lv_obj_t *const *btns, int count, lv_coord_t x, lv_coord_t y, lv_coord_t row_w, lv_coord_t bh)
{
    const lv_coord_t gap = 8;
    if (count < 1) {
        return;
    }
    const lv_coord_t bw = (row_w - (count - 1) * gap) / count;
    for (int i = 0; i < count; i++) {
        lv_obj_set_size(btns[i], bw, bh);
        lv_obj_set_pos(btns[i], x + i * (bw + gap), y);
        /* fan_make_chip() sets LV_LABEL_LONG_DOT but never a width to apply it against, so a
         * preset name too long for its share of the row (a narrow tile, or just more presets
         * than fit) overflowed the button and got clipped by it from both sides instead of
         * cleanly ending in "..." -- found via the widget gallery ("Schwenke" rendered as
         * "chwenke"). The label stays centered in the button via the lv_obj_center() already
         * set at creation; only its width needs to track the button's on every relayout.
         * A width alone was not enough either: LV_LABEL_LONG_DOT only truncates to one line if
         * the label also has an explicit single-line height, otherwise LVGL wraps across as many
         * lines as an auto-sized height allows before ever truncating -- found on a real device
         * ("Schwenken" wrapping into "Schwe"/"nken" instead of truncating), the same bug found
         * and fixed this way in the slider and gauge widgets. */
        lv_obj_t *label = lv_obj_get_child(btns[i], 0);
        if (label != NULL) {
            lv_obj_set_width(label, bw > 8 ? bw - 8 : bw);
            lv_obj_set_height(label, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
        }
    }
}

static void fan_layout_large(w_fan_ctx_t *ctx, lv_coord_t w, lv_coord_t h)
{
    const bool speed = ctx->step_count > 0 && (!ctx->features_known || (ctx->features & W_FAN_FEAT_SET_SPEED) != 0U);
    const bool osc = ctx->features_known && (ctx->features & W_FAN_FEAT_OSCILLATE) != 0U;
    const int presets = ctx->preset_count;
    const bool feat = osc || presets > 0;
    const lv_coord_t pad = 12;
    const lv_coord_t chip_h = 36;
    const lv_coord_t pbtn = 40;

    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t text_w = w - 3 * pad - pbtn;
    lv_obj_set_size(ctx->title, text_w, title_h);
    lv_obj_set_pos(ctx->title, pad, pad);
    lv_obj_set_size(ctx->state, text_w, state_h);
    lv_obj_set_pos(ctx->state, pad, pad + title_h + ui_widget_name_gap(ctx->title, 2));
    lv_obj_set_size(ctx->btn_power, pbtn, pbtn);
    lv_obj_set_pos(ctx->btn_power, w - pad - pbtn, pad);
    fan_show(ctx->btn_power, true);
    fan_show(ctx->btn_minus, false);
    fan_show(ctx->btn_plus, false);
    fan_show(ctx->bar, false);

    const lv_coord_t top_end = pad + title_h + ui_widget_name_gap(ctx->title, 2) + state_h + 6;
    lv_coord_t bottom = h - pad;
    lv_coord_t feat_y = 0;
    lv_coord_t speed_y = 0;
    if (feat) {
        feat_y = bottom - chip_h;
        bottom = feat_y - 8;
    }
    if (speed) {
        speed_y = bottom - chip_h;
        bottom = speed_y - 8;
    }

    lv_coord_t d = bottom - top_end;
    if (d > w - 2 * pad) d = w - 2 * pad;
    if (d < 60) d = 60;
    lv_obj_set_size(ctx->rotor, d, d);
    lv_obj_set_pos(ctx->rotor, (w - d) / 2, top_end + (bottom - top_end - d) / 2);
    fan_show(ctx->rotor, true);
    const lv_coord_t bd = d - 16;
    for (int i = 0; i < FAN_BLADES; i++) {
        lv_obj_set_size(ctx->blades[i], bd, bd);
        lv_obj_set_pos(ctx->blades[i], 8 - 3, 8 - 3); /* the rotor border is 3 px */
        lv_obj_set_style_arc_width(ctx->blades[i], bd / 2, LV_PART_INDICATOR);
    }
    lv_obj_set_size(ctx->hub, 18, 18);
    lv_obj_set_pos(ctx->hub, d / 2 - 9 - 3, d / 2 - 9 - 3);

    for (int i = 0; i < FAN_MAX_STEPS; i++) {
        fan_show(ctx->step_btn[i], speed && i < ctx->step_count);
    }
    if (speed) {
        fan_place_row(ctx->step_btn, ctx->step_count, pad, speed_y, w - 2 * pad, chip_h);
    }

    /* Oscillation first, then the presets; the row shows at most FAN_MAX_PRESETS + 1 buttons. */
    lv_obj_t *row[FAN_MAX_PRESETS + 1];
    int n = 0;
    if (osc) row[n++] = ctx->osc_btn;
    for (int i = 0; i < presets && i < FAN_MAX_PRESETS; i++) row[n++] = ctx->preset_btn[i];
    fan_show(ctx->osc_btn, osc);
    for (int i = 0; i < FAN_MAX_PRESETS; i++) fan_show(ctx->preset_btn[i], i < presets);
    if (feat) {
        fan_place_row(row, n, pad, feat_y, w - 2 * pad, chip_h);
    }
}

static void fan_layout(w_fan_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    ctx->large = h >= FAN_LARGE_MIN_H && w >= 200;
    if (ctx->large) {
        fan_layout_large(ctx, w, h);
    } else {
        fan_layout_flat(ctx, w, h);
    }
}

static void fan_apply_blade_angles(w_fan_ctx_t *ctx)
{
    for (int i = 0; i < FAN_BLADES; i++) {
        lv_arc_set_rotation(ctx->blades[i], (ctx->spin_angle + i * (360 / FAN_BLADES)) % 360);
    }
}

static void fan_spin_cb(lv_timer_t *timer)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || !ctx->large || !ctx->on || ctx->unavailable || !lv_obj_is_visible(ctx->card)) {
        return;
    }
    const int pct = ctx->percentage >= 0 ? ctx->percentage : 50;
    ctx->spin_angle = (ctx->spin_angle + 8 + pct / 5) % 360;
    fan_apply_blade_angles(ctx);
}

static void fan_render(w_fan_ctx_t *ctx)
{
    ui_style_card_set_active(ctx->card, ctx->on && !ctx->unavailable);
    char text[64];
    if (ctx->unavailable) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("common.unavailable", "unavailable"));
    } else if (!ctx->on) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("common.off", "OFF"));
    } else if (ctx->percentage >= 0) {
        snprintf(text, sizeof(text), "%s  %d %%", ui_i18n_get("common.on", "ON"), ctx->percentage);
    } else {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("common.on", "ON"));
    }
    if (ctx->on && !ctx->unavailable && ctx->preset_mode[0] != '\0' && ctx->large) {
        const size_t len = strlen(text);
        snprintf(text + len, sizeof(text) - len, "  %s", ctx->preset_mode);
    }
    lv_label_set_text(ctx->state, text);
    lv_bar_set_value(ctx->bar, ctx->on && ctx->percentage >= 0 ? ctx->percentage : 0, LV_ANIM_OFF);
    const bool show_bar = ctx->on && ctx->percentage >= 0 && !ctx->unavailable;
    lv_obj_set_style_opa(ctx->bar, show_bar ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->bar, show_bar ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    fan_style_button(ctx->btn_minus, false, !ctx->unavailable);
    fan_style_button(ctx->btn_power, ctx->on, !ctx->unavailable);
    fan_style_button(ctx->btn_plus, false, !ctx->unavailable);
    const lv_opa_t opa = ctx->unavailable ? LV_OPA_40 : LV_OPA_COVER;
    lv_obj_set_style_opa(ctx->btn_minus, opa, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->btn_power, opa, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->btn_plus, opa, LV_PART_MAIN);

    /* Rotor colours and the chips of the large look. */
    const bool running = ctx->on && !ctx->unavailable;
    const lv_color_t blade = running ? lv_color_hex(APP_UI_COLOR_STATE_ON)
                                     : lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 60);
    for (int i = 0; i < FAN_BLADES; i++) {
        lv_obj_set_style_arc_color(ctx->blades[i], blade, LV_PART_INDICATOR);
    }
    lv_obj_set_style_bg_color(ctx->hub, blade, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->rotor, running ? blade : lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    const int cur = (ctx->step > 0.0f && ctx->percentage >= 0) ? (int)(ctx->percentage / ctx->step + 0.5f) : -1;
    for (int i = 0; i < FAN_MAX_STEPS; i++) {
        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        lv_label_set_text(lv_obj_get_child(ctx->step_btn[i], 0), num);
        ui_style_action_button(ctx->step_btn[i], lv_obj_get_child(ctx->step_btn[i], 0), cur == i + 1 ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, !ctx->unavailable);
        lv_obj_set_style_opa(ctx->step_btn[i], opa, LV_PART_MAIN);
    }
    lv_label_set_text(lv_obj_get_child(ctx->osc_btn, 0), ui_i18n_get("fan.oscillate", "Oscillate"));
    fan_style_button(ctx->osc_btn, ctx->oscillating, !ctx->unavailable);
    lv_obj_set_style_opa(ctx->osc_btn, opa, LV_PART_MAIN);
    for (int i = 0; i < FAN_MAX_PRESETS; i++) {
        lv_label_set_text(lv_obj_get_child(ctx->preset_btn[i], 0), i < ctx->preset_count ? ctx->presets[i] : "");
        ui_style_action_button(ctx->preset_btn[i], lv_obj_get_child(ctx->preset_btn[i], 0),
            i < ctx->preset_count && strcmp(ctx->preset_mode, ctx->presets[i]) == 0 ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, !ctx->unavailable);
        lv_obj_set_style_opa(ctx->preset_btn[i], opa, LV_PART_MAIN);
    }
    fan_layout(ctx);
}

static void fan_event_cb(lv_event_t *event)
{
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx != NULL && ctx->spin_timer != NULL) {
            lv_timer_delete(ctx->spin_timer);
        }
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        fan_layout(ctx);
    }
}

static void fan_build_rotor(w_fan_ctx_t *ctx)
{
    ctx->rotor = lv_obj_create(ctx->card);
    lv_obj_remove_style_all(ctx->rotor);
    lv_obj_set_scrollable(ctx->rotor, false);
    lv_obj_set_clickable(ctx->rotor, true);
    lv_obj_set_style_radius(ctx->rotor, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->rotor, 3, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ctx->rotor, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->rotor, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(ctx->rotor, fan_power_cb, LV_EVENT_CLICKED, ctx);
    for (int i = 0; i < FAN_BLADES; i++) {
        lv_obj_t *arc = lv_arc_create(ctx->rotor);
        ctx->blades[i] = arc;
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_clickable(arc, false);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, 0, FAN_BLADE_SPAN);
        lv_arc_set_rotation(arc, i * (360 / FAN_BLADES));
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_80, LV_PART_INDICATOR);
    }
    ctx->hub = lv_obj_create(ctx->rotor);
    lv_obj_remove_style_all(ctx->hub);
    lv_obj_set_clickable(ctx->hub, false);
    lv_obj_set_style_radius(ctx->hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->hub, LV_OPA_COVER, LV_PART_MAIN);
}

esp_err_t w_fan_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_fan_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->percentage = -1;
    ctx->unavailable = true;

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
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->entity_id);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);

    ctx->state = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->state, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->state, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->state, LV_LABEL_LONG_DOT);

    ctx->bar = lv_bar_create(card);
    lv_bar_set_range(ctx->bar, 0, 100);
    ui_style_inset_track(ctx->bar, lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 26));
    lv_obj_set_style_bg_color(ctx->bar, lv_color_hex(APP_UI_COLOR_STATE_ON), LV_PART_INDICATOR);
    if (ui_style_is_flat()) {
        lv_obj_set_style_bg_color(ctx->bar, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    }

    ctx->btn_minus = fan_make_button(card, APP_SYMBOL_MINUS, fan_minus_cb, ctx);
    ctx->btn_power = fan_make_button(card, APP_SYMBOL_POWER, fan_power_cb, ctx);
    ctx->btn_plus = fan_make_button(card, APP_SYMBOL_PLUS, fan_plus_cb, ctx);

    fan_build_rotor(ctx);
    for (int i = 0; i < FAN_MAX_STEPS; i++) {
        ctx->step_btn[i] = fan_make_chip(card, fan_step_cb, ctx, i);
    }
    ctx->osc_btn = fan_make_chip(card, fan_osc_cb, ctx, 0);
    for (int i = 0; i < FAN_MAX_PRESETS; i++) {
        ctx->preset_btn[i] = fan_make_chip(card, fan_preset_cb, ctx, i);
    }
    ctx->spin_timer = lv_timer_create(fan_spin_cb, FAN_SPIN_PERIOD_MS, ctx);

    lv_obj_add_event_cb(card, fan_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, fan_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    fan_render(ctx);
    return ESP_OK;
}

void w_fan_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    ctx->on = strcmp(state->state, "on") == 0;
    ctx->percentage = -1;
    ctx->oscillating = false;
    ctx->preset_mode[0] = '\0';
    ctx->preset_count = 0;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        cJSON *pct = cJSON_GetObjectItemCaseSensitive(attrs, "percentage");
        if (cJSON_IsNumber(pct)) {
            int p = (int)(pct->valuedouble + 0.5);
            ctx->percentage = p < 0 ? 0 : (p > 100 ? 100 : p);
        }
        cJSON *feat = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
        if (cJSON_IsNumber(feat)) {
            ctx->features = (uint32_t)feat->valuedouble;
            ctx->features_known = true;
        }
        cJSON *step = cJSON_GetObjectItemCaseSensitive(attrs, "percentage_step");
        ctx->step = cJSON_IsNumber(step) && step->valuedouble > 0.0 ? (float)step->valuedouble : 0.0f;
        /* Speed chips: one per device step, three when the device does not say (33 / 67 / 100). */
        int steps = ctx->step > 0.0f ? (int)(100.0f / ctx->step + 0.5f) : 3;
        if (ctx->step <= 0.0f) ctx->step = 100.0f / 3.0f;
        if (steps < 2) steps = 2;
        if (steps > FAN_MAX_STEPS) {
            steps = FAN_MAX_STEPS;
            ctx->step = 100.0f / steps;
        }
        ctx->step_count = steps;
        cJSON *osc = cJSON_GetObjectItemCaseSensitive(attrs, "oscillating");
        ctx->oscillating = cJSON_IsTrue(osc);
        cJSON *pm = cJSON_GetObjectItemCaseSensitive(attrs, "preset_mode");
        if (cJSON_IsString(pm) && pm->valuestring != NULL) {
            snprintf(ctx->preset_mode, sizeof(ctx->preset_mode), "%s", pm->valuestring);
        }
        cJSON *modes = cJSON_GetObjectItemCaseSensitive(attrs, "preset_modes");
        const bool preset_ok = !ctx->features_known || (ctx->features & W_FAN_FEAT_PRESET_MODE) != 0U;
        if (cJSON_IsArray(modes) && preset_ok) {
            cJSON *m = NULL;
            cJSON_ArrayForEach(m, modes)
            {
                if (cJSON_IsString(m) && m->valuestring != NULL && ctx->preset_count < FAN_MAX_PRESETS) {
                    snprintf(ctx->presets[ctx->preset_count++], FAN_PRESET_LEN, "%s", m->valuestring);
                }
            }
        }
        cJSON_Delete(attrs);
    }
    fan_render(ctx);
}

void w_fan_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_fan_ctx_t *ctx = (w_fan_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    fan_render(ctx);
}
