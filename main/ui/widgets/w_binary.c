/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Binary sensor tile: an indicator ring (filled while the sensor is on), the
 * name and the state in words that fit the device class (Home / Away for
 * presence, Open / Closed for doors and windows, Wet / Dry for moisture, ...).
 * Asked for in cptkirki/BETTA-HA-PANEL issue 3 (a cat flap's presence sensor).
 *
 * An event entity (a doorbell ring, a button press: "event.<name>_ding") works too: the tile lights up for a minute
 * after the last event and otherwise shows how long ago it happened.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_history_popup.h"

typedef enum {
    BIN_GENERIC = 0,
    BIN_PRESENCE,   /* presence, occupancy: Home / Away */
    BIN_MOTION,     /* motion, moving, vibration */
    BIN_OPENING,    /* door, window, opening, garage_door */
    BIN_LOCK,       /* lock: on = unlocked */
    BIN_ALERT,      /* smoke, gas, co, moisture, safety, problem, tamper: on = bad */
    BIN_BATTERY,    /* battery: on = low */
    BIN_CONNECTIVITY,
    BIN_POWER,      /* plug, power, running */
    BIN_EVENT,      /* event.* entity: "ringing" for a minute after the last event */
} bin_kind_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *ring;
    lv_obj_t *dot;
    lv_obj_t *title;
    lv_obj_t *state;
    bin_kind_t kind;
    char device_class[24];
    char mode[APP_MAX_UI_OPTION_LEN]; /* "" / "text": state + since, "since": only the duration, "icon": ring only */
    int64_t changed_s;                /* Home Assistant last changed, Unix seconds, 0 = unknown */
    lv_timer_t *age_timer;
    bool on;
    bool unavailable;
    bool is_event;
    bool history_popup;
} w_binary_ctx_t;

#define BINARY_EVENT_ACTIVE_S 60

static bin_kind_t binary_kind(const char *dc)
{
    if (strcmp(dc, "presence") == 0 || strcmp(dc, "occupancy") == 0) return BIN_PRESENCE;
    if (strcmp(dc, "motion") == 0 || strcmp(dc, "moving") == 0 || strcmp(dc, "vibration") == 0) return BIN_MOTION;
    if (strcmp(dc, "door") == 0 || strcmp(dc, "window") == 0 || strcmp(dc, "opening") == 0 ||
        strcmp(dc, "garage_door") == 0) return BIN_OPENING;
    if (strcmp(dc, "lock") == 0) return BIN_LOCK;
    if (strcmp(dc, "smoke") == 0 || strcmp(dc, "gas") == 0 || strcmp(dc, "carbon_monoxide") == 0 ||
        strcmp(dc, "moisture") == 0 || strcmp(dc, "safety") == 0 || strcmp(dc, "problem") == 0 ||
        strcmp(dc, "tamper") == 0 || strcmp(dc, "heat") == 0) return BIN_ALERT;
    if (strcmp(dc, "battery") == 0) return BIN_BATTERY;
    if (strcmp(dc, "connectivity") == 0) return BIN_CONNECTIVITY;
    if (strcmp(dc, "plug") == 0 || strcmp(dc, "power") == 0 || strcmp(dc, "running") == 0) return BIN_POWER;
    return BIN_GENERIC;
}

static const char *binary_kind_text(bin_kind_t kind, const char *device_class, bool on)
{
    switch (kind) {
    case BIN_PRESENCE:
        return on ? ui_i18n_get("binary.home", "Home") : ui_i18n_get("binary.away", "Away");
    case BIN_MOTION:
        return on ? ui_i18n_get("binary.motion", "Motion") : ui_i18n_get("binary.clear", "Clear");
    case BIN_OPENING:
        return on ? ui_i18n_get("contact.open", "Open") : ui_i18n_get("contact.closed", "Closed");
    case BIN_LOCK:
        return on ? ui_i18n_get("binary.unlocked", "Unlocked") : ui_i18n_get("binary.locked", "Locked");
    case BIN_ALERT:
        if (strcmp(device_class, "moisture") == 0) {
            return on ? ui_i18n_get("binary.wet", "Wet") : ui_i18n_get("binary.dry", "Dry");
        }
        return on ? ui_i18n_get("binary.alarm", "Alarm") : ui_i18n_get("binary.ok", "OK");
    case BIN_BATTERY:
        return on ? ui_i18n_get("binary.low", "Low") : ui_i18n_get("binary.ok", "OK");
    case BIN_CONNECTIVITY:
        return on ? ui_i18n_get("binary.connected", "Connected") : ui_i18n_get("binary.disconnected", "Disconnected");
    case BIN_POWER:
    case BIN_GENERIC:
    default:
        return on ? ui_i18n_get("binary.on", "On") : ui_i18n_get("binary.off", "Off");
    }
}

static const char *binary_state_text(const w_binary_ctx_t *ctx)
{
    if (ctx->unavailable) {
        return ui_i18n_get("common.unavailable", "unavailable");
    }
    return binary_kind_text(ctx->kind, ctx->device_class, ctx->on);
}

const char *w_binary_class_state_text(const char *device_class, bool on)
{
    const char *dc = device_class != NULL ? device_class : "";
    return binary_kind_text(binary_kind(dc), dc, on);
}

/* Colour of the active state: calm for presence, warm for open/low, red for alarms. */
static uint32_t binary_active_color(const w_binary_ctx_t *ctx)
{
    switch (ctx->kind) {
    case BIN_PRESENCE: return 0x5AB0F6;
    case BIN_EVENT: return 0xF2A93B;
    case BIN_MOTION: return 0x5AB0F6;
    case BIN_OPENING: return 0xF2A93B;
    case BIN_LOCK: return 0xF2A93B;
    case BIN_ALERT: return 0xE5484D;
    case BIN_BATTERY: return 0xF2A93B;
    case BIN_CONNECTIVITY: return 0x4FCB7E;
    default: return APP_UI_COLOR_STATE_ON;
    }
}

static void binary_layout(w_binary_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    lv_coord_t side = h - 28;
    if (side > 44) side = 44;
    if (side < 22) side = 22;
    const lv_coord_t pad = 14;
    lv_obj_set_size(ctx->ring, side, side);
    lv_obj_set_pos(ctx->ring, pad, (h - side) / 2);
    lv_obj_set_size(ctx->dot, side - 14, side - 14);
    lv_obj_center(ctx->dot);

    const lv_coord_t text_x = pad + side + 14;
    lv_coord_t text_w = w - text_x - pad;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title, 2) + state_h)) / 2;
    lv_obj_set_size(ctx->title, text_w, title_h);
    lv_obj_set_size(ctx->state, text_w, state_h);
    lv_obj_set_pos(ctx->title, text_x, top);
    lv_obj_set_pos(ctx->state, text_x, top + title_h + ui_widget_name_gap(ctx->title, 2));
}

/* Seconds since the last change, -1 while unknown or the clock is not set. */
static int64_t binary_age_seconds(const w_binary_ctx_t *ctx)
{
    const time_t now = time(NULL);
    if (ctx->changed_s <= 0 || now < 1700000000 || now < ctx->changed_s) {
        return -1;
    }
    return (int64_t)now - ctx->changed_s;
}

/* "2 h 5 min" style age of the last change; false while unknown or the clock is not set. */
static bool binary_age_text(const w_binary_ctx_t *ctx, char *out, size_t out_len)
{
    const time_t now = time(NULL);
    if (ctx->changed_s <= 0 || now < 1700000000 || now < ctx->changed_s) {
        return false;
    }
    const int64_t s = (int64_t)now - ctx->changed_s;
    if (s < 60) {
        snprintf(out, out_len, "%s", ui_i18n_get("binary.just_now", "just now"));
    } else if (s < 3600) {
        snprintf(out, out_len, "%d min", (int)(s / 60));
    } else if (s < 86400) {
        snprintf(out, out_len, "%d h %d min", (int)(s / 3600), (int)((s % 3600) / 60));
    } else {
        snprintf(out, out_len, "%d d %d h", (int)(s / 86400), (int)((s % 86400) / 3600));
    }
    return true;
}

static void binary_render(w_binary_ctx_t *ctx)
{
    if (ctx->is_event) {
        const int64_t age = binary_age_seconds(ctx);
        ctx->on = age >= 0 && age < BINARY_EVENT_ACTIVE_S;
        ctx->kind = BIN_EVENT;
    }
    const bool active = ctx->on && !ctx->unavailable;
    const lv_color_t accent = lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED
                                           : (active ? binary_active_color(ctx) : 0x8896A6));
    lv_obj_set_style_border_color(ctx->ring, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->dot, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->dot, active ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->state, accent, LV_PART_MAIN);
    char age[32];
    char text[96];
    const bool has_age = binary_age_text(ctx, age, sizeof(age));
    if (ctx->is_event && !ctx->unavailable) {
        if (strcmp(ctx->mode, "icon") == 0) {
            text[0] = '\0';
        } else if (active) {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("binary.ringing", "Triggered"));
        } else if (has_age && strcmp(ctx->mode, "since") == 0) {
            snprintf(text, sizeof(text), "%s", age);
        } else if (has_age) {
            snprintf(text, sizeof(text), "%s \xC2\xB7 %s", ui_i18n_get("binary.last", "Last"), age);
        } else {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("binary.no_event", "No event"));
        }
    } else if (strcmp(ctx->mode, "icon") == 0) {
        text[0] = '\0';
    } else if (strcmp(ctx->mode, "since") == 0 && has_age && !ctx->unavailable) {
        snprintf(text, sizeof(text), "%s", age);
    } else if (has_age && !ctx->unavailable) {
        snprintf(text, sizeof(text), "%s \xC2\xB7 %s", binary_state_text(ctx), age);
    } else {
        snprintf(text, sizeof(text), "%s", binary_state_text(ctx));
    }
    lv_label_set_text(ctx->state, text);
    lv_obj_set_style_border_color(ctx->card, active ? lv_color_mix(accent, lv_color_hex(APP_UI_COLOR_CARD_BORDER), 110)
                                                    : lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    binary_layout(ctx);
}

static void binary_age_timer_cb(lv_timer_t *timer)
{
    w_binary_ctx_t *ctx = (w_binary_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx != NULL) {
        binary_render(ctx);
    }
}

static void binary_event_cb(lv_event_t *event)
{
    w_binary_ctx_t *ctx = (w_binary_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx != NULL && ctx->age_timer != NULL) {
            lv_timer_delete(ctx->age_timer);
        }
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        binary_layout(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_CLICKED && ctx != NULL && ctx->history_popup) {
        ui_history_popup_open(ctx->entity_id, lv_label_get_text(ctx->title),
                              lv_label_get_text(ctx->state), false, ctx->device_class);
    }
}

static lv_obj_t *binary_round(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    return o;
}

esp_err_t w_binary_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_binary_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->mode, def->display_mode, sizeof(ctx->mode));
    ctx->is_event = strncmp(def->entity_id, "event.", 6) == 0;
    ctx->history_popup = def->history_popup;
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
    lv_obj_set_clickable(card, ctx->history_popup);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->ring = binary_round(card);
    lv_obj_set_style_bg_opa(ctx->ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->ring, 3, LV_PART_MAIN);
    ctx->dot = binary_round(ctx->ring);

    ctx->title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->entity_id);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);
    ctx->state = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->state, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->state, LV_LABEL_LONG_DOT);

    lv_obj_add_event_cb(card, binary_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, binary_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    if (ctx->history_popup) lv_obj_add_event_cb(card, binary_event_cb, LV_EVENT_CLICKED, ctx);
    /* keep the age text current; an event tile also has to end its "ringing" minute on time */
    ctx->age_timer = lv_timer_create(binary_age_timer_cb, ctx->is_event ? 5000 : 30000, ctx);
    binary_render(ctx);
    return ESP_OK;
}

void w_binary_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_binary_ctx_t *ctx = (w_binary_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    /* An event entity is "unknown" until its first event; that is not a fault. */
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || (!ctx->is_event && strcmp(state->state, "unknown") == 0);
    ctx->on = strcmp(state->state, "on") == 0;
    ctx->changed_s = state->ha_changed_s;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        cJSON *dc = cJSON_GetObjectItemCaseSensitive(attrs, "device_class");
        if (cJSON_IsString(dc) && dc->valuestring != NULL) {
            strlcpy(ctx->device_class, dc->valuestring, sizeof(ctx->device_class));
            ctx->kind = binary_kind(ctx->device_class);
        }
        cJSON_Delete(attrs);
    }
    binary_render(ctx);
}

void w_binary_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_binary_ctx_t *ctx = (w_binary_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    binary_render(ctx);
}
