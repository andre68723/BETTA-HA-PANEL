/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
 * Minimal evcc loadpoint controls through Home Assistant; every displayed state is HA-confirmed. */
#include "ui/ui_widget_factory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ha/ha_client.h"
#include "ha/ha_evcc_value.h"
#include "ha/ha_evcc_sessions.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_evcc_sessions_view.h"
#include "layout/evcc_slots.h"
#include "layout/energy_timeline_colors.h"
#include "ui/ui_i18n.h"
#include "ui/fonts/app_symbols.h"
#include "ui/ui_memory.h"
#include "ui/ui_option_picker.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"

typedef struct {
    char entity[EVCC_ROLE_COUNT][APP_MAX_ENTITY_ID_LEN];
    char raw[EVCC_ROLE_COUNT][APP_MAX_STATE_LEN], unit[EVCC_ROLE_COUNT][12];
    char vehicle_name[APP_MAX_NAME_LEN];
    ha_select_options_t modes, vehicles, targets;
    bool target_number, bounds, visible;
    float min, max, step, draft;
    int pending, picker_role;
    char expected[HA_SELECT_OPTION_LEN];
    uint32_t started;
    bool notice, history_requested, force_history;
    evcc_session_t latest;
    evcc_sessions_status_t sessions_status;
    bool has_latest, rendered_online;
    lv_obj_t *energy, *remaining, *metric[3], *soc_title, *target_title, *last, *last_date, *sessions, *sessions_caption, *vehicle_arrow;
    lv_timer_t *timer;
    lv_obj_t *card, *name, *status, *power, *soc, *bar[2];
    lv_obj_t *button[5], *caption[5], *picker, *number_popup, *slider, *draft_label;
    /* The optional header (evcc_header): the charging power as a bar, or the site's energy flow. */
    int header;
    bool header_shown;
    float max_kw;
    char site[EVCC_SITE_COUNT][APP_MAX_ENTITY_ID_LEN];
    char site_raw[EVCC_SITE_COUNT][APP_MAX_STATE_LEN], site_unit[EVCC_SITE_COUNT][12];
    lv_obj_t *head_caption, *head_value, *head_bar;
    lv_obj_t *flow_track[2], *flow_seg[EVCC_FLOW_IN_COUNT + EVCC_FLOW_OUT_COUNT];
    lv_obj_t *flow_dot[EVCC_FLOW_IN_COUNT + EVCC_FLOW_OUT_COUNT], *flow_label[EVCC_FLOW_IN_COUNT + EVCC_FLOW_OUT_COUNT];
    int flow_row_y[2];
} wallbox_t;
enum { HEADER_OFF, HEADER_POWER, HEADER_FLOW };
#define FLOW_ITEMS (EVCC_FLOW_IN_COUNT + EVCC_FLOW_OUT_COUNT)
#define HEADER_BAR_H 12
#define FLOW_BAR_H 10
/* PV, battery discharge, grid import; home, the car, battery charge, export: the energy timeline's colours, the car in the
 * evcc green of the charge bar, home in the theme's text colour (flow_color()). */
static const uint32_t k_flow_color[FLOW_ITEMS] = {ETL_COLOR_SOLAR, ETL_COLOR_BATTERY_OUT, ETL_COLOR_GRID,
                                                  0, 0x43BF79, ETL_COLOR_BATTERY_IN, ETL_COLOR_EXPORT};
static lv_color_t flow_color(int item)
{
    return item == EVCC_FLOW_IN_COUNT + EVCC_FLOW_HOME ? lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY) : lv_color_hex(k_flow_color[item]);
}
_Static_assert(EVCC_ROLE_COUNT - 3 <= UI_WIDGET_EXTRA_ENTITIES, "wallbox slots exceed capacity");
static const char *tr(const char *key, const char *fallback) { return ui_i18n_get(key, fallback); }
static void render(wallbox_t *ctx);
static bool live(const char *s) { return s && *s && strcmp(s, "unknown") && strcmp(s, "unavailable"); }
static bool online(wallbox_t *ctx)
{
    return ha_client_is_connected() && strcmp(ctx->raw[EVCC_DISABLED], "on");
}
static ha_select_options_t *choices(wallbox_t *ctx, int role)
{
    return role == EVCC_MODE ? &ctx->modes : role == EVCC_VEHICLE ? &ctx->vehicles : &ctx->targets;
}
static bool usable(wallbox_t *ctx, int role)
{
    if (!ctx->visible || !online(ctx) || ctx->pending >= 0 || !live(ctx->raw[role]) || !ctx->entity[role][0]) return false;
    if (role != EVCC_MODE && !strcmp(ctx->raw[EVCC_CONNECTED], "off")) return false;
    if (role == EVCC_TARGET && ctx->target_number) return ctx->bounds;
    if (role == EVCC_TARGET) {
        if (ctx->unit[role][0] && strcmp(ctx->unit[role], "%")) return false;
        for (size_t i = 0; i < ctx->targets.count; i++) { float v; if (!evcc_soc(ctx->targets.option[i], &v)) return false; }
    }
    return choices(ctx, role)->count > 0 && !choices(ctx, role)->missing;
}
static const char *mode_label(const char *raw)
{
    if (!strcmp(raw, "off")) return tr("evcc.off", "Off");
    if (!strcmp(raw, "smart")) return tr("evcc.smart", "Smart");
    if (!strcmp(raw, "now")) return tr("evcc.fast", "Fast");
    return raw;
}
static void close_popups(wallbox_t *ctx)
{
    ui_history_popup_close_evcc(ctx->entity[EVCC_MODE]);
    if (ctx->picker) ui_option_picker_close(ctx->picker);
    if (ctx->number_popup) lv_obj_delete(ctx->number_popup);
}
static void send(wallbox_t *ctx, int role, const char *value, float number)
{
    if (!usable(ctx, role)) return;
    char payload[APP_MAX_ENTITY_ID_LEN + HA_SELECT_OPTION_LEN + 64];
    bool numeric = role == EVCC_TARGET && ctx->target_number;
    if (numeric) {
        if (!evcc_target_valid(number, ctx->min, ctx->max, ctx->step) ||
            !evcc_number_payload(ctx->entity[role], number, payload, sizeof(payload))) return;
        float actual;
        if (evcc_number(ctx->raw[role], &actual) && fabsf(actual - number) < 0.001f) return;
    } else {
        if (!value || ha_select_options_find(choices(ctx, role), value) < 0 || !strcmp(value, ctx->raw[role])) return;
        if (!ha_select_option_payload(ctx->entity[role], value, payload, sizeof(payload))) return;
    }
    esp_err_t result = ha_client_call_service(numeric ? "number" : "select", numeric ? "set_value" : "select_option", payload);
    ctx->notice = result != ESP_OK; ctx->started = lv_tick_get();
    if (result == ESP_OK) {
        ctx->pending = role;
        if (numeric) snprintf(ctx->expected, sizeof(ctx->expected), "%.5g", (double)number);
        else snprintf(ctx->expected, sizeof(ctx->expected), "%s", value);
    }
    render(ctx);
}
static void picker_closed(void *user) { ((wallbox_t *)user)->picker = NULL; }
static void picker_label(void *user, size_t index, char *out, size_t length)
{
    wallbox_t *ctx = user; const char *value = choices(ctx, ctx->picker_role)->option[index];
    if (ctx->picker_role == EVCC_MODE) value = mode_label(value);
    else if (ctx->picker_role == EVCC_VEHICLE) {
        if (!strcmp(value, "null")) value = tr("evcc.none", "No vehicle");
        else if (!strcmp(value, ctx->raw[EVCC_VEHICLE]) && ctx->vehicle_name[0]) value = ctx->vehicle_name;
    }
    snprintf(out, length, "%s", value);
}
static void picker_chosen(void *user, size_t index)
{
    wallbox_t *ctx = user; ha_select_options_t *opts = choices(ctx, ctx->picker_role);
    if (index < opts->count) send(ctx, ctx->picker_role, opts->option[index], 0);
}
static void open_picker(wallbox_t *ctx, int role)
{
    if (!usable(ctx, role) || ctx->picker || ctx->number_popup) return;
    ctx->picker_role = role; ha_select_options_t *opts = choices(ctx, role);
    int current = ha_select_options_find(opts, ctx->raw[role]);
    ui_option_picker_cfg_t cfg = {
        .title = role == EVCC_MODE ? tr("evcc.mode", "Charge mode") : role == EVCC_VEHICLE ?
            tr("evcc.vehicle", "Vehicle") : tr("evcc.target", "Target SOC"),
        .count = opts->count, .current = current < 0 ? UI_OPTION_PICKER_NONE : (size_t)current,
        .label = picker_label, .chosen = picker_chosen, .closed = picker_closed, .user = ctx,
    };
    ctx->picker = ui_option_picker_open(&cfg);
}
static void number_deleted(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event);
    ctx->number_popup = ctx->slider = ctx->draft_label = NULL;
}
static void number_close(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event);
    if (ctx->number_popup) lv_obj_delete_async(ctx->number_popup);
}
static void number_apply(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event);
    send(ctx, EVCC_TARGET, NULL, ctx->draft); number_close(event);
}
static void number_changed(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event);
    ctx->draft = ctx->min + lv_slider_get_value(ctx->slider) * ctx->step;
    char text[96]; snprintf(text, sizeof(text), "%.4g %%", (double)ctx->draft);
    lv_label_set_text(ctx->draft_label, text);
}
static void number_open(wallbox_t *ctx)
{
    if (!usable(ctx, EVCC_TARGET) || ctx->picker || ctx->number_popup) return;
    float value = ctx->min;
    (void)evcc_number(ctx->raw[EVCC_TARGET], &value);
    int ticks = (int)floorf((ctx->max - ctx->min) / ctx->step + 0.0001f);
    if (ticks < 1 || ticks > 10000) return;
    lv_obj_t *overlay = lv_obj_create(lv_layer_top()); ctx->number_popup = overlay;
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100)); lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0); lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0); lv_obj_set_style_pad_all(overlay, 0, 0); lv_obj_set_scrollable(overlay, false);
    lv_obj_add_event_cb(overlay, number_deleted, LV_EVENT_DELETE, ctx);
    lv_obj_t *card = lv_obj_create(overlay);
    int card_w = lv_obj_get_width(lv_screen_active()) - 32; if (card_w > 420) card_w = 420;
    int value_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
    int meta_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    int slider_y = 62 + value_h + 24, hint_y = slider_y + 40;
    int card_h = hint_y + meta_h + 90; if (card_h < 300) card_h = 300;
    lv_obj_set_size(card, card_w, card_h); lv_obj_center(card); lv_obj_set_scrollable(card, false);
    lv_obj_set_style_text_color(card, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), 0); ui_popup_style_frame(card);
    lv_obj_t *title = lv_label_create(card); ui_text_set(title, UI_TEXT_LABEL, true);
    lv_label_set_text(title, tr("evcc.target", "Target SOC")); lv_obj_set_pos(title, 0, 0);
    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), number_close, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    ctx->draft_label = lv_label_create(card); ui_text_set(ctx->draft_label, UI_TEXT_VALUE, true);
    lv_obj_align(ctx->draft_label, LV_ALIGN_TOP_MID, 0, 62);
    ctx->slider = lv_slider_create(card); lv_obj_set_size(ctx->slider, card_w - 48, 22);
    lv_obj_align(ctx->slider, LV_ALIGN_TOP_MID, 0, slider_y); lv_slider_set_range(ctx->slider, 0, ticks);
    int tick = (int)lroundf((value - ctx->min) / ctx->step);
    if (tick < 0) tick = 0;
    if (tick > ticks) tick = ticks;
    lv_slider_set_value(ctx->slider, tick, LV_ANIM_OFF);
    lv_obj_add_event_cb(ctx->slider, number_changed, LV_EVENT_VALUE_CHANGED, ctx);
    ctx->draft = ctx->min + tick * ctx->step;
    char text[96]; snprintf(text, sizeof(text), "%.4g %%", (double)ctx->draft); lv_label_set_text(ctx->draft_label, text);
    lv_obj_t *hint = lv_label_create(card); ui_text_set(hint, UI_TEXT_META, true);
    float effective;
    if (evcc_soc(ctx->raw[EVCC_EFFECTIVE], &effective)) snprintf(text, sizeof(text), "%s %.0f%%", tr("evcc.effective", "Effective"), (double)effective);
    else snprintf(text, sizeof(text), "%.4g–%.4g %%", (double)ctx->min, (double)ctx->max);
    lv_label_set_text(hint, text); lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, hint_y);
    lv_obj_t *apply = lv_button_create(card); lv_obj_set_size(apply, card_w - 48, 48); lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(apply, number_apply, LV_EVENT_CLICKED, ctx);
    lv_obj_t *caption = lv_label_create(apply); ui_text_set(caption, UI_TEXT_LABEL, true);
    lv_label_set_text(caption, tr("evcc.apply", "Apply")); lv_obj_center(caption);
}
static void clicked(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event); lv_obj_t *obj = lv_event_get_current_target(event);
    if (obj == ctx->sessions) { if (ctx->visible && ha_client_is_connected()) ui_history_popup_open_evcc(ctx->entity[EVCC_MODE]); return; }
    for (int i = 0; i < 5; i++) if (obj == ctx->button[i]) {
        if (i == 0) send(ctx, EVCC_MODE, "off", 0);
        else if (i == 1) {
            if (ctx->modes.count == 3 && ha_select_options_find(&ctx->modes, "smart") >= 0 && ha_select_options_find(&ctx->modes, "now") >= 0)
                send(ctx, EVCC_MODE, "smart", 0);
            else open_picker(ctx, EVCC_MODE);
        } else if (i == 2) send(ctx, EVCC_MODE, "now", 0);
        else if (i == 3) open_picker(ctx, EVCC_VEHICLE);
        else if (ctx->target_number) number_open(ctx); else open_picker(ctx, EVCC_TARGET);
    }
}
static void button(wallbox_t *ctx, int i, const char *text, bool enabled, bool selected)
{
    lv_color_t bg = lv_color_hex(selected ? APP_UI_COLOR_NAV_BTN_BG_ACTIVE : APP_UI_COLOR_NAV_BTN_BG_IDLE);
    ui_style_button_auto(ctx->button[i], bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_set_style_opa(ctx->button[i], enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_label_set_text(ctx->caption[i], text);
    const lv_font_t *font = ui_font_role(UI_TEXT_LABEL);
    int padding = i == 3 && strcmp(ctx->raw[EVCC_CONNECTED], "off") ? 48 : 12;
    lv_obj_set_style_text_font(ctx->caption[i], ui_text_fit(text, font, lv_obj_get_width(ctx->button[i]) - padding, lv_obj_get_height(ctx->button[i]) - 8), 0);
    lv_obj_set_width(ctx->caption[i], lv_obj_get_width(ctx->button[i]) - padding);
    lv_obj_set_style_text_align(ctx->caption[i], i == 4 ? LV_TEXT_ALIGN_RIGHT : i == 3 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ctx->caption[i], ui_style_button_icon_color(bg), 0); if (i == 3) lv_obj_align(ctx->caption[i], LV_ALIGN_LEFT_MID, 12, 0); else lv_obj_align(ctx->caption[i], LV_ALIGN_CENTER, 0, 0);
}
static const char *flow_name(int item)
{
    static const char *const keys[FLOW_ITEMS][2] = {
        {"evcc.flow_pv", "PV"}, {"evcc.flow_battery", "Battery"}, {"evcc.flow_grid", "Grid"},
        {"evcc.flow_home", "Home"}, {"evcc.flow_car", "Car"}, {"evcc.flow_battery", "Battery"}, {"evcc.flow_export", "Export"},
    };
    return tr(keys[item][0], keys[item][1]);
}
/* The header's readings: the charging power against its maximum, or the energy flow as two bars on one scale (in above,
 * out below) with a dot and value per source and consumer that is running; a row keeps the items that fit. */
static void render_header(wallbox_t *ctx)
{
    if (!ctx->header_shown) return;
    const bool active = online(ctx);
    float car = 0.0f;
    const bool has_car = active && evcc_power_kw(ctx->raw[EVCC_POWER], ctx->unit[EVCC_POWER], &car) && car >= 0.0f;
    char text[48];
    if (ctx->header == HEADER_POWER) {
        if (has_car) snprintf(text, sizeof(text), "%.1f / %.0f kW", (double)car, (double)ctx->max_kw);
        else snprintf(text, sizeof(text), "-- / %.0f kW", (double)ctx->max_kw);
        lv_label_set_text(ctx->head_value, text);
        float share = has_car && ctx->max_kw > 0.0f ? car / ctx->max_kw : 0.0f;
        if (share > 1.0f) share = 1.0f;
        lv_bar_set_value(ctx->head_bar, (int32_t)lroundf(share * 1000.0f), LV_ANIM_OFF);
        return;
    }
    float site[EVCC_SITE_COUNT];
    for (int i = 0; i < EVCC_SITE_COUNT; i++)
        if (!active || !evcc_power_kw(ctx->site_raw[i], ctx->site_unit[i], &site[i])) site[i] = 0.0f;
    evcc_flow_t flow;
    evcc_flow_compute(site[EVCC_SITE_PV], site[EVCC_SITE_BATTERY], site[EVCC_SITE_GRID], site[EVCC_SITE_HOME],
                      has_car ? car : 0.0f, &flow);
    const int track_w = lv_obj_get_width(ctx->flow_track[0]);
    const int card_w = lv_obj_get_width(ctx->card) - 24;
    const int unit_w = lv_obj_get_width(ctx->head_value) + 8; /* the sources row ends before the "kW" */
    const lv_font_t *font = ui_font_role(UI_TEXT_META);
    const int line = lv_font_get_line_height(font);
    for (int row = 0; row < 2; row++) {
        const int first = row ? EVCC_FLOW_IN_COUNT : 0, count = row ? EVCC_FLOW_OUT_COUNT : EVCC_FLOW_IN_COUNT;
        int x = 0, label_x = 12;
        for (int k = 0; k < count; k++) {
            const int item = first + k;
            const float value = row ? flow.out[k] : flow.in[k];
            const int w = flow.scale > 0.0f ? (int)lroundf((float)track_w * value / flow.scale) : 0;
            lv_obj_set_hidden(ctx->flow_seg[item], w < 1);
            lv_obj_set_pos(ctx->flow_seg[item], x, 0);
            lv_obj_set_size(ctx->flow_seg[item], w > 0 ? w : 1, FLOW_BAR_H);
            x += w;
            bool shown = value >= 0.05f;
            if (shown) {
                /* the word and the value, or only the value beside its coloured dot where the row is full */
                lv_point_t size;
                snprintf(text, sizeof(text), "%s %.1f", flow_name(item), (double)value);
                lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                const int row_end = 12 + card_w - (row ? 0 : unit_w);
                if (label_x + 12 + size.x > row_end) {
                    snprintf(text, sizeof(text), "%.1f", (double)value);
                    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                }
                shown = label_x + 12 + size.x <= row_end;
                if (shown) {
                    lv_label_set_text(ctx->flow_label[item], text);
                    lv_obj_set_pos(ctx->flow_dot[item], label_x, ctx->flow_row_y[row] + (line - 8) / 2);
                    lv_obj_set_pos(ctx->flow_label[item], label_x + 12, ctx->flow_row_y[row]);
                    lv_obj_set_size(ctx->flow_label[item], size.x + 2, line);
                    label_x += 12 + size.x + 10;
                }
            }
            lv_obj_set_hidden(ctx->flow_dot[item], !shown);
            lv_obj_set_hidden(ctx->flow_label[item], !shown);
        }
    }
}
static void render(wallbox_t *ctx)
{
    /* the buttons and readings fit their text to the sizes layout() set; apply those first */
    lv_obj_update_layout(ctx->card);
    bool active = online(ctx);
    ctx->rendered_online = active;
    const char *status = !active ? tr("evcc.unavailable", "Unavailable") : !strcmp(ctx->raw[EVCC_CHARGING], "on") ?
        tr("evcc.charging", "Charging") : !strcmp(ctx->raw[EVCC_CONNECTED], "on") ? tr("evcc.connected", "Connected") :
        !strcmp(ctx->raw[EVCC_CONNECTED], "off") ? tr("evcc.ready", "Not connected") : "--";
    if (ctx->pending >= 0) status = tr("evcc.pending", "Waiting…");
    else if (ctx->notice) status = tr("evcc.failed", "Not confirmed");
    lv_label_set_text(ctx->status, status);
    char text[128]; float value;
    if (active && evcc_number(ctx->raw[EVCC_POWER], &value) && value >= 0 &&
        (!strcmp(ctx->unit[EVCC_POWER], "W") || !strcmp(ctx->unit[EVCC_POWER], "kW")))
        snprintf(text, sizeof(text), "%.1f kW", (double)(!strcmp(ctx->unit[EVCC_POWER], "W") ? value / 1000 : value));
    else strcpy(text, "-- kW");
    lv_label_set_text(ctx->power, text);
    bool soc = active && evcc_soc(ctx->raw[EVCC_SOC], &value) && !strcmp(ctx->unit[EVCC_SOC], "%");
    if (soc) snprintf(text, sizeof(text), "%.0f %%", (double)value); else strcpy(text, "-- %");
    lv_label_set_text(ctx->soc, text);
    int fill = soc ? (int)lroundf(value * 100) : 0;
    lv_bar_set_value(ctx->bar[0], fill > 2000 ? 2000 : fill, LV_ANIM_OFF);
    lv_bar_set_value(ctx->bar[1], fill > 2000 ? fill - 2000 : 0, LV_ANIM_OFF);
    bool current_modes = ctx->modes.count == 3 && ha_select_options_find(&ctx->modes, "smart") >= 0 && ha_select_options_find(&ctx->modes, "now") >= 0;
    bool mode = usable(ctx, EVCC_MODE);
    button(ctx, 0, tr("evcc.off", "Off"), mode && ha_select_options_find(&ctx->modes, "off") >= 0, !strcmp(ctx->raw[EVCC_MODE], "off"));
    button(ctx, 1, current_modes || !ctx->modes.count ? tr("evcc.smart", "Smart") : live(ctx->raw[EVCC_MODE]) ? mode_label(ctx->raw[EVCC_MODE]) : tr("evcc.mode", "Charge mode"), mode, current_modes && !strcmp(ctx->raw[EVCC_MODE], "smart"));
    button(ctx, 2, tr("evcc.fast", "Fast"), mode && ha_select_options_find(&ctx->modes, "now") >= 0, !strcmp(ctx->raw[EVCC_MODE], "now"));
    lv_obj_set_hidden(ctx->button[2], false);
    int width = lv_obj_get_width(ctx->card) - 24;
    int mode_w = (width - 16) / 3;
    lv_obj_set_width(ctx->button[1], mode_w);
    bool disconnected = active && !strcmp(ctx->raw[EVCC_CONNECTED], "off");
    const char *vehicle = disconnected || !strcmp(ctx->raw[EVCC_VEHICLE], "null") ? tr("evcc.none", "No vehicle") :
        live(ctx->raw[EVCC_VEHICLE]) ? (ctx->vehicle_name[0] ? ctx->vehicle_name : ctx->raw[EVCC_VEHICLE]) : "--";
    lv_point_t measured;
    const lv_font_t *vehicle_font = ui_font_role(UI_TEXT_LABEL);
    lv_text_get_size(&measured, vehicle, vehicle_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    (void)measured;
    /* As wide as the status line and the charge bar below it, with the name left-aligned like they are. */
    lv_obj_set_width(ctx->button[3], width);
    lv_obj_set_x(ctx->button[3], 12);
    lv_obj_set_hidden(ctx->vehicle_arrow, disconnected);
    lv_obj_align(ctx->vehicle_arrow, LV_ALIGN_RIGHT_MID, -8, 0);
    button(ctx, 3, vehicle, usable(ctx, EVCC_VEHICLE), false);
    lv_obj_set_clickable(ctx->button[3], !disconnected);
    if (disconnected) {
        lv_obj_set_style_opa(ctx->button[3], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(ctx->button[3], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ctx->button[3], 0, 0);
        lv_obj_set_style_shadow_width(ctx->button[3], 0, 0);
        lv_obj_set_style_bg_grad_dir(ctx->button[3], LV_GRAD_DIR_NONE, 0);
    }
    if (active && evcc_soc(ctx->raw[EVCC_TARGET], &value)) snprintf(text, sizeof(text), "%.0f %%", (double)value);
    else strcpy(text, "-- %");
    button(ctx, 4, text, usable(ctx, EVCC_TARGET), false);
    if (disconnected && ctx->has_latest) snprintf(text, sizeof(text), "%.1f kWh", ctx->latest.energy);
    else if (active && !disconnected && evcc_number(ctx->raw[EVCC_ENERGY], &value) && value >= 0 &&
        (!strcmp(ctx->unit[EVCC_ENERGY], "kWh") || !strcmp(ctx->unit[EVCC_ENERGY], "Wh")))
        snprintf(text, sizeof(text), "%.1f kWh", (double)(!strcmp(ctx->unit[EVCC_ENERGY], "Wh") ? value / 1000 : value));
    else strcpy(text, "-- kWh");
    lv_label_set_text(ctx->energy, text);
    if (active && !disconnected && evcc_number(ctx->raw[EVCC_REMAINING], &value) && value >= 0 &&
        (!strcmp(ctx->unit[EVCC_REMAINING], "s") || !strcmp(ctx->unit[EVCC_REMAINING], "min") || !strcmp(ctx->unit[EVCC_REMAINING], "h"))) {
        double seconds = value * (!strcmp(ctx->unit[EVCC_REMAINING], "min") ? 60 : !strcmp(ctx->unit[EVCC_REMAINING], "h") ? 3600 : 1);
        if (seconds < 36000000) { unsigned minutes = (unsigned)ceil(seconds / 60); snprintf(text, sizeof(text), "%u:%02u h", minutes / 60, minutes % 60); }
        else strcpy(text, "--");
    } else strcpy(text, "--");
    lv_label_set_text(ctx->remaining, text);
    lv_obj_t *readings[] = {ctx->power,ctx->energy,ctx->remaining};
    for (int i = 0; i < 3; i++) lv_obj_set_style_text_font(readings[i], ui_text_fit(lv_label_get_text(readings[i]), ui_font_role(UI_TEXT_LABEL), lv_obj_get_width(readings[i]), lv_obj_get_height(readings[i])), 0);
    if (disconnected) lv_label_set_text(ctx->power, "0.0 kW");
    lv_obj_t *connected_parts[] = {ctx->status, ctx->soc, ctx->bar[0], ctx->bar[1], ctx->button[4], ctx->soc_title, ctx->target_title};
    for (size_t i = 0; i < sizeof(connected_parts)/sizeof(connected_parts[0]); i++) lv_obj_set_hidden(connected_parts[i], disconnected);
    lv_obj_set_hidden(ctx->last, !disconnected); lv_obj_set_hidden(ctx->last_date, !disconnected);
    if (disconnected && ctx->has_latest) {
        snprintf(text, sizeof(text), "%s · %s", tr("evcc.last", "Last session"), ctx->latest.vehicle[0] ? ctx->latest.vehicle : "--");
        lv_label_set_text(ctx->last, text); ui_evcc_session_date(ctx->latest.start, text, sizeof(text)); lv_label_set_text(ctx->last_date, text);
    } else if (disconnected) {
        lv_label_set_text(ctx->last, tr("evcc.last", "Last session"));
        lv_label_set_text(ctx->last_date, ui_evcc_sessions_status(ctx->sessions_status, true));
    }
    lv_obj_set_style_opa(ctx->sessions, ha_client_is_connected() ? LV_OPA_COVER : LV_OPA_40, 0);
    render_header(ctx);
}
/* The header's height, and whether it is shown: only when the card still holds every row below it. */
static int header_height(const wallbox_t *ctx, int meta)
{
    if (ctx->header == HEADER_POWER) return meta + 4 + HEADER_BAR_H + 12;
    if (ctx->header == HEADER_FLOW) return meta + 4 + FLOW_BAR_H + 3 + FLOW_BAR_H + 4 + meta + 12;
    return 0;
}
static void layout_header(wallbox_t *ctx, int y, int width, int meta)
{
    lv_obj_t *power[] = {ctx->head_caption, ctx->head_bar};
    for (int i = 0; i < 2; i++) if (power[i]) lv_obj_set_hidden(power[i], !(ctx->header_shown && ctx->header == HEADER_POWER));
    if (ctx->head_value) lv_obj_set_hidden(ctx->head_value, !ctx->header_shown);
    for (int i = 0; i < 2; i++) if (ctx->flow_track[i]) lv_obj_set_hidden(ctx->flow_track[i], !(ctx->header_shown && ctx->header == HEADER_FLOW));
    for (int i = 0; i < FLOW_ITEMS; i++) {
        if (ctx->flow_dot[i] && !(ctx->header_shown && ctx->header == HEADER_FLOW)) {
            lv_obj_set_hidden(ctx->flow_dot[i], true);
            lv_obj_set_hidden(ctx->flow_label[i], true);
        }
    }
    if (!ctx->header_shown) return;
    if (ctx->header == HEADER_POWER) {
        lv_obj_set_pos(ctx->head_caption, 12, y); lv_obj_set_size(ctx->head_caption, width / 2, meta);
        lv_obj_set_pos(ctx->head_value, 12 + width / 2, y); lv_obj_set_size(ctx->head_value, width - width / 2, meta);
        lv_obj_set_pos(ctx->head_bar, 12, y + meta + 4); lv_obj_set_size(ctx->head_bar, width, HEADER_BAR_H);
        return;
    }
    ctx->flow_row_y[0] = y;
    lv_point_t unit;
    lv_text_get_size(&unit, "kW", ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(ctx->head_value, 12 + width - unit.x - 2, y); lv_obj_set_size(ctx->head_value, unit.x + 2, meta);
    lv_obj_set_pos(ctx->flow_track[0], 12, y + meta + 4); lv_obj_set_size(ctx->flow_track[0], width, FLOW_BAR_H);
    lv_obj_set_pos(ctx->flow_track[1], 12, y + meta + 4 + FLOW_BAR_H + 3); lv_obj_set_size(ctx->flow_track[1], width, FLOW_BAR_H);
    ctx->flow_row_y[1] = y + meta + 4 + 2 * FLOW_BAR_H + 3 + 4;
}
static void layout(wallbox_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    int width = lv_obj_get_width(ctx->card) - 24;
    int meta = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    int label = lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    int value = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
    int row = label + 12; if (row < 42) row = 42;
    int history_w = width * 42 / 100;
    lv_obj_set_pos(ctx->name, 12, 12); lv_obj_set_size(ctx->name, width-history_w-8, meta);
    lv_obj_set_pos(ctx->sessions, 12+width-history_w, 8); lv_obj_set_size(ctx->sessions, history_w, meta+8);
    lv_obj_set_size(ctx->sessions_caption, history_w-8, meta); lv_obj_center(ctx->sessions_caption);
    int y = 12+meta+12;
    /* the rows below the header, from the mode buttons to the charge level */
    const int body = row+10 + meta+label+12 + row+10 + meta+6 + 18 + meta+2 + value;
    const int head = header_height(ctx, meta);
    ctx->header_shown = head > 0 && y + head + body + 8 <= lv_obj_get_height(ctx->card);
    layout_header(ctx, y, width, meta);
    if (ctx->header_shown) y += head;
    for (int i = 0; i < 3; i++) { lv_obj_set_pos(ctx->button[i], 12+i*(width+8)/3, y); lv_obj_set_size(ctx->button[i], (width-16)/3, row); }
    y += row+10;
    lv_obj_t *values[] = {ctx->power,ctx->energy,ctx->remaining};
    for (int i = 0; i < 3; i++) {
        int x = 12+i*width/3, w = width/3-4;
        lv_obj_set_pos(ctx->metric[i], x, y); lv_obj_set_size(ctx->metric[i], w, meta);
        lv_obj_set_pos(values[i], x, y+meta+2); lv_obj_set_size(values[i], w, label);
    }
    y += meta+label+12;
    lv_obj_set_pos(ctx->button[3], 12, y); lv_obj_set_size(ctx->button[3], width, row);
    y += row+10;
    lv_obj_set_pos(ctx->status, 12, y); lv_obj_set_size(ctx->status, width, meta);
    lv_obj_set_pos(ctx->last, 12, y); lv_obj_set_size(ctx->last, width, label);
    lv_obj_set_pos(ctx->last_date, 12, y+label+8); lv_obj_set_size(ctx->last_date, width, label);
    y += meta+6;
    for (int i = 0; i < 2; i++) { lv_obj_set_pos(ctx->bar[i], 12+i*(width/5), y); lv_obj_set_size(ctx->bar[i], i ? width-width/5 : width/5, 10); }
    y += 18;
    lv_obj_set_pos(ctx->soc_title, 12, y); lv_obj_set_size(ctx->soc_title, width/2-4, meta);
    lv_obj_set_pos(ctx->target_title, 12+width/2, y); lv_obj_set_size(ctx->target_title, width/2, meta);
    y += meta+2;
    lv_obj_set_pos(ctx->soc, 12, y); lv_obj_set_size(ctx->soc, width/2-4, value);
    lv_obj_set_pos(ctx->button[4], 12+width/2, y-2); lv_obj_set_size(ctx->button[4], width/2, value+6);
    render(ctx);
}

static void timer(lv_timer_t *timer)
{
    wallbox_t *ctx = lv_timer_get_user_data(timer);
    const bool pending = ctx->pending >= 0, notice = ctx->notice;
    if (ctx->pending >= 0 && (!online(ctx) || lv_tick_elaps(ctx->started) >= 10000)) {
        ctx->pending = -1; ctx->notice = true; ctx->started = lv_tick_get();
    } else if (ctx->notice && lv_tick_elaps(ctx->started) >= 6000) ctx->notice = false;
    if (!online(ctx)) { close_popups(ctx); ctx->history_requested = false; }
    if (ctx->visible && online(ctx) && !strcmp(ctx->raw[EVCC_CONNECTED], "off") && !ctx->history_requested) {
        ha_evcc_sessions_request(ctx->entity[EVCC_MODE], ctx->force_history); ctx->history_requested = true; ctx->force_history = false;
    }
    evcc_session_t latest = {0};
    evcc_sessions_status_t status = ctx->sessions_status;
    bool exists = false;
    ha_evcc_sessions_latest(ctx->entity[EVCC_MODE], &latest, &status, &exists);
    const bool sessions = exists != ctx->has_latest || status != ctx->sessions_status ||
        (exists && (latest.start != ctx->latest.start || latest.energy != ctx->latest.energy || strcmp(latest.vehicle, ctx->latest.vehicle)));
    ctx->latest = latest; ctx->sessions_status = status; ctx->has_latest = exists;
    /* Render only on a change: a render sets every label and button style again, which redraws the whole card; three
     * wallboxes on one page kept a core at 100 % and starved the screenshot endpoint. */
    if (sessions || pending != (ctx->pending >= 0) || notice != ctx->notice || online(ctx) != ctx->rendered_online) render(ctx);
}
static void event(lv_event_t *event)
{
    wallbox_t *ctx = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) { close_popups(ctx); if (ctx->timer) lv_timer_delete(ctx->timer); free(ctx); }
    else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) { close_popups(ctx); layout(ctx); }
}
esp_err_t w_wallbox_evcc_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *instance)
{
    if (!def || !parent || !instance) return ESP_ERR_INVALID_ARG;
    wallbox_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx)); if (!ctx) return ESP_ERR_NO_MEM;
    const char *first[] = {def->entity_id, def->secondary_entity_id, def->tertiary_entity_id};
    for (int i = 0; i < EVCC_ROLE_COUNT; i++) snprintf(ctx->entity[i], APP_MAX_ENTITY_ID_LEN, "%s", i < 3 ? first[i] : def->extra_entity_id[i - 3]);
    ctx->target_number = !strncmp(ctx->entity[EVCC_TARGET], "number.", 7); ctx->visible = true; ctx->pending = -1;
    /* the header in the fields the wallbox does not use otherwise (layout/evcc_slots.h) */
    ctx->header = !strcmp(def->style_variant, "power") ? HEADER_POWER : !strcmp(def->style_variant, "flow") ? HEADER_FLOW : HEADER_OFF;
    ctx->max_kw = def->gauge_max >= EVCC_POWER_MAX_KW_MIN && def->gauge_max <= EVCC_POWER_MAX_KW_MAX ? def->gauge_max : EVCC_POWER_MAX_KW_DEFAULT;
    const char *site[EVCC_SITE_COUNT] = {def->forecast_today_entity_id, def->forecast_tomorrow_entity_id,
                                         def->forecast_day_3_entity_id, def->forecast_day_4_entity_id};
    for (int i = 0; i < EVCC_SITE_COUNT; i++) snprintf(ctx->site[i], sizeof(ctx->site[i]), "%s", ctx->header == HEADER_FLOW ? site[i] : "");
    ctx->card = lv_obj_create(parent); lv_obj_remove_style_all(ctx->card);
    lv_obj_set_scrollable(ctx->card, false); lv_obj_set_size(ctx->card, def->w, def->h); lv_obj_set_pos(ctx->card, def->x, def->y);
    lv_obj_set_style_text_color(ctx->card, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 0); lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ctx->card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), 0); lv_obj_set_style_border_width(ctx->card, 1, 0); lv_obj_set_style_radius(ctx->card, APP_UI_CARD_RADIUS, 0);
    ctx->name = lv_label_create(ctx->card); ui_text_set(ctx->name, UI_TEXT_META, true);
    lv_label_set_text(ctx->name, def->title[0] ? def->title : tr("evcc.wallbox", "Wallbox")); lv_label_set_long_mode(ctx->name, LV_LABEL_LONG_DOT); lv_obj_set_hidden(ctx->name, def->hide_name);
    ctx->status = lv_label_create(ctx->card); ui_text_set(ctx->status, UI_TEXT_META, true); lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ctx->status, LV_TEXT_ALIGN_LEFT, 0);
    ctx->power = lv_label_create(ctx->card); ui_text_set(ctx->power, UI_TEXT_LABEL, true);
    ctx->soc = lv_label_create(ctx->card); ui_text_set(ctx->soc, UI_TEXT_VALUE, true); lv_obj_set_style_text_align(ctx->soc, LV_TEXT_ALIGN_LEFT, 0);
    /* Two joined 20%/80% sections, one solid green like the evcc app's charge bar; the split only keeps the
     * 20 % mark where the former red/yellow/green scale had it. */
    for (int i = 0; i < 2; i++) {
        ctx->bar[i] = lv_bar_create(ctx->card); lv_obj_remove_style_all(ctx->bar[i]); lv_obj_set_clickable(ctx->bar[i], false);
        lv_bar_set_range(ctx->bar[i], 0, i ? 8000 : 2000); lv_bar_set_orientation(ctx->bar[i], LV_BAR_ORIENTATION_HORIZONTAL);
        lv_obj_set_style_bg_color(ctx->bar[i], lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN); lv_obj_set_style_bg_opa(ctx->bar[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->bar[i], lv_color_hex(0x43BF79), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(ctx->bar[i], LV_GRAD_DIR_NONE, LV_PART_INDICATOR); lv_obj_set_style_bg_opa(ctx->bar[i], LV_OPA_COVER, LV_PART_INDICATOR);
    }
    for (int i = 0; i < 5; i++) {
        ctx->button[i] = lv_button_create(ctx->card); lv_obj_remove_style_all(ctx->button[i]);
        ctx->caption[i] = lv_label_create(ctx->button[i]); ui_text_set(ctx->caption[i], UI_TEXT_LABEL, true); lv_label_set_long_mode(ctx->caption[i], LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(ctx->button[i], clicked, LV_EVENT_CLICKED, ctx);
    }
    ctx->energy = lv_label_create(ctx->card); ui_text_set(ctx->energy, UI_TEXT_LABEL, true);
    ctx->remaining = lv_label_create(ctx->card); ui_text_set(ctx->remaining, UI_TEXT_LABEL, true);
    const char *metrics[] = {tr("evcc.power_label", "Power"), tr("evcc.charged", "Charged"), tr("evcc.remaining", "Remaining")};
    for (int i = 0; i < 3; i++) {
        ctx->metric[i] = lv_label_create(ctx->card); ui_text_set(ctx->metric[i], UI_TEXT_META, true); lv_label_set_text(ctx->metric[i], metrics[i]);
        lv_label_set_long_mode(ctx->metric[i], LV_LABEL_LONG_DOT);
    }
    ctx->soc_title = lv_label_create(ctx->card); ui_text_set(ctx->soc_title, UI_TEXT_META, true); lv_label_set_text(ctx->soc_title, tr("evcc.soc_label", "SOC"));
    ctx->target_title = lv_label_create(ctx->card); ui_text_set(ctx->target_title, UI_TEXT_META, true); lv_label_set_text(ctx->target_title, tr("evcc.target_label", "Target SOC"));
    lv_obj_set_style_text_align(ctx->target_title, LV_TEXT_ALIGN_RIGHT, 0);
    ctx->last = lv_label_create(ctx->card); ui_text_set(ctx->last, UI_TEXT_LABEL, true); lv_label_set_long_mode(ctx->last, LV_LABEL_LONG_DOT);
    ctx->last_date = lv_label_create(ctx->card); ui_text_set(ctx->last_date, UI_TEXT_LABEL, true); lv_label_set_long_mode(ctx->last_date, LV_LABEL_LONG_DOT);
    ctx->sessions = lv_button_create(ctx->card); lv_obj_remove_style_all(ctx->sessions);
    lv_obj_add_event_cb(ctx->sessions, clicked, LV_EVENT_CLICKED, ctx);
    ctx->sessions_caption = lv_label_create(ctx->sessions); ui_text_set(ctx->sessions_caption, UI_TEXT_META, true);
    lv_label_set_text(ctx->sessions_caption, tr("evcc.sessions", "Sessions")); lv_obj_set_style_text_align(ctx->sessions_caption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_decor(ctx->sessions_caption, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_set_style_text_color(ctx->sessions_caption, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
    ctx->vehicle_arrow = lv_label_create(ctx->button[3]);
    lv_label_set_text(ctx->vehicle_arrow, APP_SYMBOL_CHEVRON_DOWN);
    lv_obj_set_style_text_font(ctx->vehicle_arrow, APP_FONT_SYMBOL_MEDIUM, 0);
    lv_obj_set_style_text_color(ctx->vehicle_arrow, ui_style_button_icon_color(lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE)), 0);
    if (ctx->header == HEADER_POWER) {
        ctx->head_caption = lv_label_create(ctx->card); ui_text_set(ctx->head_caption, UI_TEXT_META, true);
        lv_label_set_text(ctx->head_caption, tr("evcc.header_power", "Charging power")); lv_label_set_long_mode(ctx->head_caption, LV_LABEL_LONG_DOT);
        ctx->head_value = lv_label_create(ctx->card); ui_text_set(ctx->head_value, UI_TEXT_META, true);
        lv_obj_set_style_text_align(ctx->head_value, LV_TEXT_ALIGN_RIGHT, 0);
        ctx->head_bar = lv_bar_create(ctx->card); lv_obj_remove_style_all(ctx->head_bar); lv_obj_set_clickable(ctx->head_bar, false);
        lv_bar_set_range(ctx->head_bar, 0, 1000);
        lv_obj_set_style_radius(ctx->head_bar, ui_style_track_radius(), LV_PART_MAIN); lv_obj_set_style_radius(ctx->head_bar, ui_style_track_radius(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ctx->head_bar, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN); lv_obj_set_style_bg_opa(ctx->head_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->head_bar, lv_color_hex(0x43BF79), LV_PART_INDICATOR); lv_obj_set_style_bg_opa(ctx->head_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    } else if (ctx->header == HEADER_FLOW) {
        /* the unit of every value, once, at the end of the sources row */
        ctx->head_value = lv_label_create(ctx->card); ui_text_set(ctx->head_value, UI_TEXT_META, true);
        lv_label_set_text(ctx->head_value, "kW"); lv_obj_set_style_text_align(ctx->head_value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(ctx->head_value, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), 0);
        for (int i = 0; i < 2; i++) {
            ctx->flow_track[i] = lv_obj_create(ctx->card); lv_obj_remove_style_all(ctx->flow_track[i]);
            lv_obj_set_scrollable(ctx->flow_track[i], false); lv_obj_set_clickable(ctx->flow_track[i], false);
            lv_obj_set_style_radius(ctx->flow_track[i], ui_style_track_radius(), 0); lv_obj_set_style_clip_corner(ctx->flow_track[i], true, 0);
            lv_obj_set_style_bg_color(ctx->flow_track[i], lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), 0); lv_obj_set_style_bg_opa(ctx->flow_track[i], LV_OPA_COVER, 0);
        }
        for (int i = 0; i < FLOW_ITEMS; i++) {
            ctx->flow_seg[i] = lv_obj_create(ctx->flow_track[i < EVCC_FLOW_IN_COUNT ? 0 : 1]); lv_obj_remove_style_all(ctx->flow_seg[i]);
            lv_obj_set_clickable(ctx->flow_seg[i], false);
            lv_obj_set_style_bg_color(ctx->flow_seg[i], flow_color(i), 0); lv_obj_set_style_bg_opa(ctx->flow_seg[i], LV_OPA_COVER, 0);
            ctx->flow_dot[i] = lv_obj_create(ctx->card); lv_obj_remove_style_all(ctx->flow_dot[i]); lv_obj_set_clickable(ctx->flow_dot[i], false);
            lv_obj_set_size(ctx->flow_dot[i], 8, 8); lv_obj_set_style_radius(ctx->flow_dot[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(ctx->flow_dot[i], flow_color(i), 0); lv_obj_set_style_bg_opa(ctx->flow_dot[i], LV_OPA_COVER, 0);
            ctx->flow_label[i] = lv_label_create(ctx->card); ui_text_set(ctx->flow_label[i], UI_TEXT_META, true);
            lv_obj_set_hidden(ctx->flow_dot[i], true); lv_obj_set_hidden(ctx->flow_label[i], true);
        }
    }
    instance->obj = ctx->card; instance->ctx = ctx; instance->visible = true;
    layout(ctx); lv_obj_add_event_cb(ctx->card, event, LV_EVENT_DELETE, ctx); lv_obj_add_event_cb(ctx->card, event, LV_EVENT_SIZE_CHANGED, ctx);
    ctx->timer = lv_timer_create(timer, 250, ctx);
    if (!ctx->timer) { lv_obj_delete(ctx->card); instance->obj = NULL; instance->ctx = NULL; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
void w_wallbox_evcc_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    wallbox_t *ctx = instance->ctx; if (!ctx || !state) return;
    for (int i = 0; i < EVCC_SITE_COUNT; i++) if (ctx->site[i][0] && !strcmp(ctx->site[i], state->entity_id)) {
        snprintf(ctx->site_raw[i], sizeof(ctx->site_raw[i]), "%s", state->state);
        cJSON *attrs = cJSON_Parse(state->attributes_json);
        const char *unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement"));
        snprintf(ctx->site_unit[i], sizeof(ctx->site_unit[i]), "%s", unit ? unit : "");
        cJSON_Delete(attrs);
    }
    for (int i = 0; i < EVCC_ROLE_COUNT; i++) if (ctx->entity[i][0] && !strcmp(ctx->entity[i], state->entity_id)) {
        if (i == EVCC_CONNECTED && strcmp(ctx->raw[i], state->state)) { ctx->force_history = !strcmp(ctx->raw[i], "on") && !strcmp(state->state, "off"); ctx->history_requested = false; close_popups(ctx); }
        bool changed_vehicle = i == EVCC_VEHICLE && strcmp(ctx->raw[i], state->state);
        if ((ctx->picker && ctx->picker_role == i) || (ctx->number_popup && i == EVCC_TARGET)) close_popups(ctx);
        snprintf(ctx->raw[i], sizeof(ctx->raw[i]), "%s", state->state);
        cJSON *attrs = cJSON_Parse(state->attributes_json);
        const char *unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement"));
        snprintf(ctx->unit[i], sizeof(ctx->unit[i]), "%s", unit ? unit : "");
        if (i == EVCC_MODE || i == EVCC_VEHICLE || (i == EVCC_TARGET && !ctx->target_number))
            (void)evcc_select_parse(state->attributes_json, choices(ctx, i));
        if (i == EVCC_TARGET && ctx->target_number) ctx->bounds = evcc_number_bounds(state->attributes_json, &ctx->min, &ctx->max, &ctx->step);
        if (i == EVCC_VEHICLE) {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(attrs, "vehicle"), "name"));
            snprintf(ctx->vehicle_name, sizeof(ctx->vehicle_name), "%s", strcmp(state->state, "null") && name ? name : "");
            if (changed_vehicle) { ctx->raw[EVCC_SOC][0] = 0; ctx->raw[EVCC_EFFECTIVE][0] = 0; }
        }
        cJSON_Delete(attrs);
        if (ctx->pending == i) {
            float a, b;
            bool matched = i == EVCC_TARGET && ctx->target_number ? evcc_number(ctx->expected, &a) && evcc_number(state->state, &b) && fabsf(a - b) < 0.001f : !strcmp(state->state, ctx->expected);
            if (matched || !live(state->state)) { ctx->pending = -1; ctx->notice = !matched; ctx->started = lv_tick_get(); }
        }
    }
    render(ctx);
}
void w_wallbox_evcc_mark_unavailable(ui_widget_instance_t *instance)
{
    wallbox_t *ctx = instance->ctx; if (!ctx) return;
    ctx->history_requested = false;
    memset(ctx->raw, 0, sizeof(ctx->raw)); memset(ctx->site_raw, 0, sizeof(ctx->site_raw)); ctx->vehicle_name[0] = 0; ctx->bounds = false;
    ctx->modes.count = ctx->vehicles.count = ctx->targets.count = 0; ctx->pending = -1; close_popups(ctx); render(ctx);
}
void w_wallbox_evcc_set_visible(ui_widget_instance_t *instance, bool visible)
{
    wallbox_t *ctx = instance->ctx; if (!ctx) return;
    ctx->visible = visible;
    if (!visible) { close_popups(ctx); return; }
    ctx->history_requested = false;
    render(ctx);
}
