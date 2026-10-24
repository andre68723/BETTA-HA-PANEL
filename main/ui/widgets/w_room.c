/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Room card: the state of one room at a glance (temperature, humidity, open
 * windows and doors, lights on, presence) and a popup with all its entities,
 * where lights, switches, covers, fans and media players can be toggled. Up to 8 entities per card; their
 * role follows from the domain and device class, so the editor only has to
 * collect them (entity_id plus the "room_entities" array of the layout).
 * An optional extra field (room_extra_mode) makes the humidity prominent, or gives a cover or a media player of the
 * card a compact status and one direct action (util/room_extra.c decides which). The card itself stays a popup target.
 * Inspired by the RoomCard for Home Assistant (lop1505/RoomCard), which
 * populates a room from a Home Assistant area; nothing was taken from it.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/widgets/w_room_controls.h"
#include "util/room_extra.h"

#define ROOM_MAX_ENTITIES 8
#define ROOM_ACTION_SIZE 44 /* the action is a touch target of at least 44x44 */
/* Theme colors (evaluated when used, so a theme change applies to rebuilt tiles). */
#define ROOM_OPEN_COLOR APP_UI_COLOR_LIGHT_ICON_ON
#define ROOM_CLOSED_COLOR APP_UI_COLOR_OK
#define ROOM_LIGHT_COLOR APP_UI_COLOR_LIGHT_ICON_ON
#define ROOM_PRESENT_COLOR APP_UI_COLOR_CARD_ICON_ON
#define ROOM_MUTED_COLOR APP_UI_COLOR_TEXT_MUTED
/* Same token media_player.c uses for its own "accent_color": not tied to any on/off state,
 * just the generic highlight color of the active theme. The temperature ring uses it flat
 * (no gold-to-green fill gradient): unlike a charge level, a higher room temperature is not
 * simply "better", so it only shows where the value sits in a fixed comfort band, not a
 * verdict on it. */
#define ROOM_TEMP_RING_COLOR APP_UI_COLOR_CARD_ICON_ON
#define ROOM_TEMP_RING_STROKE 8 /* arc width in px */
#define ROOM_TEMP_RING_MIN 5   /* deliberately wide: an unheated room or a cold cellar should not pin the ring */
#define ROOM_TEMP_RING_MAX 32

typedef struct {
    char ids[ROOM_MAX_ENTITIES][APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *temp_arc;
    lv_obj_t *temp_label;
    lv_obj_t *hum_label;
    lv_obj_t *chip[4];       /* windows, lights, presence, batteries */
    lv_obj_t *chip_label[4];
    bool chip_wanted[4];     /* the state asks for the chip; layout may still hide it */
    lv_obj_t *popup;
    bool has_temp;
    float temp;
    char temp_unit[8];
    bool has_hum;
    float hum;
    /* the extra field: a mode and one entity of the card (ids[]); anything else falls back to the default card */
    room_extra_mode_t extra_mode;
    char extra_id[APP_MAX_ENTITY_ID_LEN];
    bool extra_usable;       /* the entity has a state other than unavailable/unknown */
    char extra_state[24];
    int extra_position;      /* cover: current_position, -1 when unknown */
    uint32_t extra_features;
    bool extra_features_known;
    bool extra_hum_ok;       /* humidity mode: the selected sensor has a numeric value */
    float extra_hum;
    room_action_t extra_action;   /* the single toggle button (compact card, media player) */
    room_action_t extra_up;       /* cover on a roomy card: one button per direction */
    room_action_t extra_down;
    lv_obj_t *extra_btn;
    lv_obj_t *extra_icon;
    lv_obj_t *extra_btn2;    /* the "down" button of the two-button cover control */
    lv_obj_t *extra_icon2;
    bool extra_dual;         /* layout: two buttons (up, down) instead of one */
    lv_obj_t *extra_label;   /* the status text: beside the temperature (expanded) or inside the button (compact) */
    bool extra_compact;      /* layout: the action sits in the bottom row */
    bool extra_short;        /* layout: the room only fits the status word, not the cover position */
    bool extra_shown;        /* layout: there is room for it */
    room_controls_t *controls; /* the control row (layout option room_controls), NULL when off or nothing to control */
    int contacts_total;
    int contacts_open;
    int lights_total;
    int lights_on;
    int presence_total;
    int presence_on;
    int batteries_low;
} w_room_ctx_t;

static bool room_domain_is(const char *id, const char *domain)
{
    const size_t n = strlen(domain);
    return strncmp(id, domain, n) == 0 && id[n] == '.';
}

static bool room_parse_float(const char *s, float *out)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const float v = strtof(s, &end);
    if (end == s) {
        return false;
    }
    *out = v;
    return true;
}

static const char *room_attr_string(cJSON *attrs, const char *key)
{
    cJSON *item = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, key) : NULL;
    return (cJSON_IsString(item) && item->valuestring != NULL) ? item->valuestring : "";
}

/* Recompute the summary from the current states of all entities of the card. */
static void room_evaluate(w_room_ctx_t *ctx)
{
    ctx->has_temp = false;
    ctx->has_hum = false;
    ctx->contacts_total = ctx->contacts_open = 0;
    ctx->lights_total = ctx->lights_on = 0;
    ctx->presence_total = ctx->presence_on = 0;
    ctx->batteries_low = 0;
    ctx->temp_unit[0] = '\0';

    ha_state_t *st = ui_calloc_prefer_psram(1, sizeof(*st));
    if (st == NULL) {
        return;
    }
    for (int i = 0; i < ROOM_MAX_ENTITIES; i++) {
        const char *id = ctx->ids[i];
        if (id[0] == '\0' || !ha_model_get_state(id, st)) {
            continue;
        }
        const bool usable = strcmp(st->state, "unavailable") != 0 && strcmp(st->state, "unknown") != 0;
        const bool on = strcmp(st->state, "on") == 0 || strcmp(st->state, "open") == 0;
        cJSON *attrs = st->attributes_json[0] != '\0' ? cJSON_Parse(st->attributes_json) : NULL;
        const char *dc = room_attr_string(attrs, "device_class");
        const char *unit = room_attr_string(attrs, "unit_of_measurement");

        if (room_domain_is(id, "light")) {
            ctx->lights_total++;
            if (usable && on) {
                ctx->lights_on++;
            }
        } else if (room_domain_is(id, "binary_sensor")) {
            if (strcmp(dc, "window") == 0 || strcmp(dc, "door") == 0 || strcmp(dc, "opening") == 0 ||
                strcmp(dc, "garage_door") == 0) {
                ctx->contacts_total++;
                if (usable && on) {
                    ctx->contacts_open++;
                }
            } else if (strcmp(dc, "battery") == 0) {
                if (usable && on) {
                    ctx->batteries_low++; /* binary battery sensor: on = low */
                }
            } else if (strcmp(dc, "motion") == 0 || strcmp(dc, "occupancy") == 0 || strcmp(dc, "presence") == 0 ||
                       strcmp(dc, "moving") == 0) {
                ctx->presence_total++;
                if (usable && on) {
                    ctx->presence_on++;
                }
            }
        } else if (room_domain_is(id, "sensor")) {
            float v = 0.0f;
            if (usable && room_parse_float(st->state, &v)) {
                if (strcmp(dc, "battery") == 0) {
                    if (v <= 20.0f) {
                        ctx->batteries_low++;
                    }
                } else if (!ctx->has_temp && (strcmp(dc, "temperature") == 0 || strstr(unit, "C") != NULL || strstr(unit, "F") != NULL)) {
                    ctx->has_temp = true;
                    ctx->temp = v;
                    snprintf(ctx->temp_unit, sizeof(ctx->temp_unit), "%s", unit[0] != '\0' ? unit : "\xC2\xB0" "C");
                } else if (!ctx->has_hum && strcmp(dc, "humidity") == 0) {
                    ctx->has_hum = true;
                    ctx->hum = v;
                }
            }
        } else if (room_domain_is(id, "climate") && usable && !ctx->has_temp && attrs != NULL) {
            cJSON *cur = cJSON_GetObjectItemCaseSensitive(attrs, "current_temperature");
            if (cJSON_IsNumber(cur)) {
                ctx->has_temp = true;
                ctx->temp = (float)cur->valuedouble;
                snprintf(ctx->temp_unit, sizeof(ctx->temp_unit), "\xC2\xB0" "C");
            }
        }
        cJSON_Delete(attrs);
    }

    /* The extra field's entity: its state, and what its action would do. */
    ctx->extra_usable = false;
    ctx->extra_state[0] = '\0';
    ctx->extra_position = -1;
    ctx->extra_features = 0;
    ctx->extra_features_known = false;
    ctx->extra_hum_ok = false;
    ctx->extra_action = ROOM_ACTION_NONE;
    ctx->extra_up = ROOM_ACTION_NONE;
    ctx->extra_down = ROOM_ACTION_NONE;
    if (ctx->extra_mode != ROOM_EXTRA_DEFAULT && ha_model_get_state(ctx->extra_id, st)) {
        ctx->extra_usable = strcmp(st->state, "unavailable") != 0 && strcmp(st->state, "unknown") != 0;
        strlcpy(ctx->extra_state, st->state, sizeof(ctx->extra_state));
        cJSON *attrs = st->attributes_json[0] != '\0' ? cJSON_Parse(st->attributes_json) : NULL;
        cJSON *feat = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, "supported_features") : NULL;
        if (cJSON_IsNumber(feat)) {
            ctx->extra_features = (uint32_t)feat->valuedouble;
            ctx->extra_features_known = true;
        }
        cJSON *pos = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, "current_position") : NULL;
        if (cJSON_IsNumber(pos)) {
            ctx->extra_position = (int)lroundf((float)pos->valuedouble);
        }
        cJSON_Delete(attrs);
        if (ctx->extra_mode == ROOM_EXTRA_HUMIDITY) {
            float v = 0.0f;
            ctx->extra_hum_ok = ctx->extra_usable && room_parse_float(st->state, &v);
            ctx->extra_hum = v;
        } else if (ctx->extra_mode == ROOM_EXTRA_COVER) {
            ctx->extra_action = room_extra_cover_action(ctx->extra_state, ctx->extra_features, ctx->extra_features_known);
            ctx->extra_up = room_extra_cover_up(ctx->extra_state, ctx->extra_position, ctx->extra_features, ctx->extra_features_known);
            ctx->extra_down = room_extra_cover_down(ctx->extra_state, ctx->extra_position, ctx->extra_features, ctx->extra_features_known);
        } else if (ctx->extra_mode == ROOM_EXTRA_MEDIA_PLAYER) {
            ctx->extra_action = room_extra_media_action(ctx->extra_state, ctx->extra_features, ctx->extra_features_known);
        }
    }
    free(st);
    room_controls_refresh(ctx->controls);
}

static void room_set_chip(w_room_ctx_t *ctx, int index, bool visible, const char *text, uint32_t color)
{
    if (ctx->chip[index] == NULL) {
        return;
    }
    ctx->chip_wanted[index] = visible;
    if (!visible) {
        lv_obj_set_hidden(ctx->chip[index], true);
        return;
    }
    lv_obj_set_hidden(ctx->chip[index], false);
    lv_label_set_text(ctx->chip_label[index], text);
    lv_obj_set_style_text_color(ctx->chip_label[index], lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->chip[index], lv_color_hex(color), LV_PART_MAIN);
    /* Flat keeps the plain outline pill; glow/glass fill it with a soft tint of the same
     * color, matching how those two styles treat every other status pill in the app. */
    if (ui_style_has_depth()) {
        lv_obj_set_style_bg_color(ctx->chip[index], lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->chip[index], LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->chip[index], 0, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_opa(ctx->chip[index], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->chip[index], 1, LV_PART_MAIN);
    }
}

/* The status word of the extra field and the symbol of what the button would do. */
static const char *room_extra_state_text(const w_room_ctx_t *ctx, char *out, size_t out_len, bool with_position)
{
    const char *st = ctx->extra_state;
    if (!ctx->extra_usable) {
        return "--";
    }
    if (ctx->extra_mode == ROOM_EXTRA_COVER) {
        const char *word = strcmp(st, "open") == 0 ? ui_i18n_get("cover.open", "Open")
                         : strcmp(st, "closed") == 0 ? ui_i18n_get("cover.closed", "Closed")
                         : strcmp(st, "opening") == 0 ? ui_i18n_get("cover.opening", "Opening")
                         : strcmp(st, "closing") == 0 ? ui_i18n_get("cover.closing", "Closing") : "--";
        if (with_position && ctx->extra_position >= 0 && strcmp(st, "closed") != 0) {
            snprintf(out, out_len, "%s %d %%", word, ctx->extra_position);
            return out;
        }
        return word;
    }
    return strcmp(st, "playing") == 0 ? ui_i18n_get("common.playing", "playing")
         : strcmp(st, "paused") == 0 ? ui_i18n_get("common.paused", "paused")
         : strcmp(st, "off") == 0 ? ui_i18n_get("common.off", "OFF")
         : ui_i18n_get("room.idle", "Idle");
}

/* Widest the status with a position can get (any moving or open state at 100 %): the layout decides on it once, so a
 * state change never overflows a card that was sized for "Closed". */
static lv_coord_t room_extra_widest_status(const w_room_ctx_t *ctx, const lv_font_t *font)
{
    if (ctx->extra_mode != ROOM_EXTRA_COVER) {
        return 0;
    }
    const char *words[] = {ui_i18n_get("cover.open", "Open"), ui_i18n_get("cover.opening", "Opening"),
                           ui_i18n_get("cover.closing", "Closing")};
    lv_coord_t widest = 0;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        char text[40];
        snprintf(text, sizeof(text), "%s 100 %%", words[i]);
        lv_point_t size = {0, 0};
        lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > widest) {
            widest = size.x;
        }
    }
    return widest;
}

static const char *room_action_symbol(room_action_t action)
{
    switch (action) {
    case ROOM_ACTION_COVER_OPEN: return APP_SYMBOL_ARROW_UP;
    case ROOM_ACTION_COVER_CLOSE: return APP_SYMBOL_ARROW_DOWN;
    case ROOM_ACTION_COVER_STOP: return APP_SYMBOL_STOP;
    case ROOM_ACTION_MEDIA_PLAY: return APP_SYMBOL_PLAY;
    case ROOM_ACTION_MEDIA_PAUSE: return APP_SYMBOL_PAUSE;
    case ROOM_ACTION_NONE: break;
    }
    return NULL;
}

static const char *room_extra_symbol(const w_room_ctx_t *ctx)
{
    switch (ctx->extra_action) {
    case ROOM_ACTION_COVER_OPEN: return APP_SYMBOL_ARROW_UP;
    case ROOM_ACTION_COVER_CLOSE: return APP_SYMBOL_ARROW_DOWN;
    case ROOM_ACTION_COVER_STOP: return APP_SYMBOL_STOP;
    case ROOM_ACTION_MEDIA_PLAY: return APP_SYMBOL_PLAY;
    case ROOM_ACTION_MEDIA_PAUSE: return APP_SYMBOL_PAUSE;
    case ROOM_ACTION_NONE: break;
    }
    return ctx->extra_mode == ROOM_EXTRA_COVER ? APP_SYMBOL_ARROW_UP : APP_SYMBOL_PLAY; /* shown disabled */
}

static void room_render_extra(w_room_ctx_t *ctx)
{
    if (ctx->extra_btn == NULL) {
        return;
    }
    const bool action_mode = ctx->extra_mode == ROOM_EXTRA_COVER || ctx->extra_mode == ROOM_EXTRA_MEDIA_PLAYER;
    if (!action_mode) {
        return;
    }
    char text[40];
    lv_label_set_text(ctx->extra_label, room_extra_state_text(ctx, text, sizeof(text), !ctx->extra_short));
    lv_obj_set_style_text_color(ctx->extra_label,
        lv_color_hex(ctx->extra_usable ? APP_UI_COLOR_TEXT_PRIMARY : ROOM_MUTED_COLOR), LV_PART_MAIN);
    if (ctx->extra_dual && ctx->extra_btn2 != NULL) {
        /* two directions: up keeps its arrow (or Stop while opening), down likewise; a direction that cannot move is dimmed */
        const room_action_t up = ctx->extra_up;
        const room_action_t down = ctx->extra_down;
        const char *up_sym = room_action_symbol(up);
        const char *down_sym = room_action_symbol(down);
        lv_label_set_text(ctx->extra_icon, up_sym != NULL ? up_sym : APP_SYMBOL_ARROW_UP);
        lv_label_set_text(ctx->extra_icon2, down_sym != NULL ? down_sym : APP_SYMBOL_ARROW_DOWN);
        ui_style_action_button(ctx->extra_btn, ctx->extra_icon, up == ROOM_ACTION_COVER_STOP ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, up != ROOM_ACTION_NONE);
        ui_style_action_button(ctx->extra_btn2, ctx->extra_icon2, down == ROOM_ACTION_COVER_STOP ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, down != ROOM_ACTION_NONE);
        return;
    }
    lv_label_set_text(ctx->extra_icon, room_extra_symbol(ctx));
    const bool enabled = ctx->extra_action != ROOM_ACTION_NONE;
    const bool active = ctx->extra_action == ROOM_ACTION_MEDIA_PAUSE || ctx->extra_action == ROOM_ACTION_COVER_STOP;
    ui_style_action_button(ctx->extra_btn, ctx->extra_icon, active ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, enabled);
}

static void room_render(w_room_ctx_t *ctx)
{
    char text[48];
    if (ctx->has_temp) {
        /* Was "%.1f%s" followed by an unconditional '.' -> ',' swap, applied regardless of the
         * active UI language (not even gated on German) -- the only place in the app that shows
         * a temperature with a comma decimal separator, found via the widget gallery next to
         * every other temperature-showing widget using a period. ui_format_temp_c() covers the
         * plain-°C case (ctx->temp_unit is a real "°C" literal, see ctx->temp_unit's fallback in
         * room_collect_data() above); a non-temperature unit (e.g. a Fahrenheit sensor's "°F")
         * still falls back to the old formatting so it is not silently mislabelled as Celsius. */
        if (strcmp(ctx->temp_unit, "°C") == 0) {
            ui_format_temp_c(text, sizeof(text), true, ctx->temp);
        } else {
            snprintf(text, sizeof(text), "%.1f%s", (double)ctx->temp, ctx->temp_unit);
        }
    } else {
        snprintf(text, sizeof(text), "--");
    }
    lv_label_set_text(ctx->temp_label, text);
    if (ctx->temp_arc != NULL && ctx->has_temp) {
        int32_t clamped = (int32_t)lroundf(ctx->temp);
        if (clamped < ROOM_TEMP_RING_MIN) {
            clamped = ROOM_TEMP_RING_MIN;
        } else if (clamped > ROOM_TEMP_RING_MAX) {
            clamped = ROOM_TEMP_RING_MAX;
        }
        lv_arc_set_value(ctx->temp_arc, clamped);
    }
    if (ctx->extra_mode == ROOM_EXTRA_HUMIDITY) {
        /* the chosen sensor, prominent; never a zero when it has no value */
        if (ctx->extra_hum_ok) {
            snprintf(text, sizeof(text), "%.0f %%", (double)ctx->extra_hum);
        } else {
            snprintf(text, sizeof(text), "-- %%");
        }
        lv_label_set_text(ctx->hum_label, text);
        lv_obj_set_style_text_color(ctx->hum_label,
            lv_color_hex(ctx->extra_hum_ok ? APP_UI_COLOR_TEXT_PRIMARY : ROOM_MUTED_COLOR), LV_PART_MAIN);
        lv_obj_set_hidden(ctx->hum_label, false);
    } else if (ctx->has_hum) {
        snprintf(text, sizeof(text), "%.0f %%", (double)ctx->hum);
        lv_label_set_text(ctx->hum_label, text);
        lv_obj_set_hidden(ctx->hum_label, false);
    } else {
        lv_obj_set_hidden(ctx->hum_label, true);
    }
    room_render_extra(ctx);

    if (ctx->contacts_total > 0) {
        if (ctx->contacts_open == 0) {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("room.closed", "Closed"));
        } else {
            snprintf(text, sizeof(text), ui_i18n_get("room.open_n", "%d open"), ctx->contacts_open);
        }
    }
    room_set_chip(ctx, 0, ctx->contacts_total > 0, text, ctx->contacts_open > 0 ? ROOM_OPEN_COLOR : ROOM_CLOSED_COLOR);

    if (ctx->lights_total > 0) {
        if (ctx->lights_on == 0) {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("room.lights_off", "Lights off"));
        } else if (ctx->lights_on == 1) {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("room.light_on", "Light on"));
        } else {
            snprintf(text, sizeof(text), ui_i18n_get("room.lights_on_n", "%d lights on"), ctx->lights_on);
        }
    }
    room_set_chip(ctx, 1, ctx->lights_total > 0, text, ctx->lights_on > 0 ? ROOM_LIGHT_COLOR : ROOM_MUTED_COLOR);

    if (ctx->batteries_low > 0) {
        snprintf(text, sizeof(text), ui_i18n_get("room.battery_n", "%d battery low"), ctx->batteries_low);
    }
    room_set_chip(ctx, 3, ctx->batteries_low > 0, text, ROOM_OPEN_COLOR);

    room_set_chip(ctx, 2, ctx->presence_total > 0,
        ctx->presence_on > 0 ? ui_i18n_get("room.present", "Present") : ui_i18n_get("room.empty", "Empty"),
        ctx->presence_on > 0 ? ROOM_PRESENT_COLOR : ROOM_MUTED_COLOR);
}

static void room_layout(w_room_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 14;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, ui_font_role(UI_TEXT_META));
    const lv_coord_t temp_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
    const lv_coord_t chip_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 10;

    lv_obj_set_size(ctx->title_label, w - 2 * pad, title_h);
    lv_obj_set_pos(ctx->title_label, pad, 10);

    /* The ring needs enough free height between the title and the chip row, and enough width
     * next to it for the humidity label, to be worth drawing; a very small card falls back to
     * the plain number it always had instead of a ring too cramped to read. */
    const lv_coord_t ring_top = 10 + title_h + ui_widget_name_gap(ctx->title_label, 6);
    const lv_coord_t chip_row_top = h - chip_h - 12;
    /* The control row (lights, switches, covers) sits above the status chips when the ring still keeps 56 px beside it. */
    lv_coord_t controls_h = 0;
    if (ctx->controls != NULL) {
        const lv_coord_t spare = chip_row_top - ring_top - 6 - 56 - 6;
        controls_h = room_controls_layout(ctx->controls, w, pad, chip_row_top - 6, spare);
    }
    const lv_coord_t avail_h = chip_row_top - ring_top - 6 - (controls_h > 0 ? controls_h + 6 : 0);
    const lv_coord_t avail_w = w - 2 * pad;
    lv_coord_t ring_d = avail_h < (avail_w * 42) / 100 ? avail_h : (avail_w * 42) / 100;
    /* The reading sits inside the ring, between its two arc strokes, and must stay on one line: a ring that cannot hold
     * the widest reading at its size ("88.8°C", measured here so a changing value never flips the layout) gives way to
     * the plain number instead of a wrapped one. */
    lv_point_t ref_size;
    lv_text_get_size(&ref_size, "88.8\xC2\xB0" "C", ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_coord_t ring_min_d = ref_size.x + 2 * ROOM_TEMP_RING_STROKE + 6;
    const bool show_ring = ctx->temp_arc != NULL && ring_d >= (ring_min_d > 56 ? ring_min_d : 56) && avail_w >= 140;
    if (ring_d > 108) {
        ring_d = 108;
    }

    if (show_ring) {
        lv_obj_set_hidden(ctx->temp_arc, !ctx->has_temp);
        lv_obj_set_size(ctx->temp_arc, ring_d, ring_d);
        lv_obj_set_pos(ctx->temp_arc, pad, ring_top);
        lv_obj_set_style_text_font(ctx->temp_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_width(ctx->temp_label, ring_d - 2 * ROOM_TEMP_RING_STROKE);
        lv_obj_set_style_text_align(ctx->temp_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align_to(ctx->temp_label, ctx->temp_arc, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(ctx->hum_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->hum_label, ctx->extra_mode == ROOM_EXTRA_HUMIDITY
            ? (ring_d >= 90 ? ui_font_role(UI_TEXT_VALUE) : ui_font_role(UI_TEXT_META)) : ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_width(ctx->hum_label, avail_w - ring_d - 10);
        lv_obj_align_to(ctx->hum_label, ctx->temp_arc, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    } else {
        if (ctx->temp_arc != NULL) {
            lv_obj_set_hidden(ctx->temp_arc, true);
        }
        lv_obj_set_style_text_font(ctx->temp_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
        lv_obj_set_width(ctx->temp_label, LV_SIZE_CONTENT); /* its own width: the humidity label follows it */
        lv_obj_set_style_text_align(ctx->temp_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_pos(ctx->temp_label, pad, 10 + title_h + ui_widget_name_gap(ctx->title_label, 2));
        lv_obj_update_layout(ctx->temp_label);
        const lv_font_t *hum_font = ctx->extra_mode == ROOM_EXTRA_HUMIDITY ? ui_font_role(UI_TEXT_META) : ui_font_role(UI_TEXT_META);
        lv_obj_set_style_text_align(ctx->hum_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->hum_label, hum_font, LV_PART_MAIN);
        lv_obj_set_width(ctx->hum_label, avail_w);
        /* after the temperature, by its actual width, not after a full-width label */
        lv_obj_set_pos(ctx->hum_label, pad + lv_obj_get_width(ctx->temp_label) + 14,
            10 + title_h + ui_widget_name_gap(ctx->title_label, 2) + temp_h - lv_font_get_line_height(hum_font) - 6);
    }

    /* The extra field's action: beside the temperature when the ring leaves room for the status and a 44x44 target, else
     * as a compact control at the right end of the bottom row (the chips give way), else not at all: a card too small
     * keeps its popup and never overlaps or clips the title, the temperature or a warning. */
    const lv_coord_t gap = 8;
    lv_coord_t chips_w = w - 2 * pad;
    ctx->extra_shown = false;
    ctx->extra_compact = false;
    ctx->extra_short = false;
    ctx->extra_dual = false;
    if (ctx->extra_btn != NULL) {
        /* the control row already steers every cover of the card: the single cover action would only repeat it */
        const bool covered_by_row = room_controls_shown(ctx->controls) && room_controls_has_covers(ctx->controls) &&
                                    ctx->extra_mode == ROOM_EXTRA_COVER;
        const bool action_mode = !covered_by_row &&
            (ctx->extra_mode == ROOM_EXTRA_COVER || ctx->extra_mode == ROOM_EXTRA_MEDIA_PLAYER);
        const lv_coord_t right_w = avail_w - ring_d - 10;
        if (action_mode && show_ring && right_w >= 130 && ring_d >= 56) {
            ctx->extra_shown = true;
            const lv_coord_t btn = ROOM_ACTION_SIZE;
            /* a cover gets one button per direction when the status word still fits beside both */
            ctx->extra_dual = ctx->extra_mode == ROOM_EXTRA_COVER && ctx->extra_btn2 != NULL &&
                              right_w - 2 * btn - 16 >= 56;
            const lv_coord_t buttons_w = ctx->extra_dual ? 2 * btn + 8 : btn;
            lv_obj_set_size(ctx->extra_btn, btn, btn);
            lv_obj_set_pos(ctx->extra_btn, pad + avail_w - buttons_w, ring_top + (ring_d - btn) / 2);
            lv_obj_center(ctx->extra_icon);
            lv_obj_set_hidden(ctx->extra_icon, false);
            if (ctx->extra_dual) {
                lv_obj_set_size(ctx->extra_btn2, btn, btn);
                lv_obj_set_pos(ctx->extra_btn2, pad + avail_w - btn, ring_top + (ring_d - btn) / 2);
                lv_obj_center(ctx->extra_icon2);
            }
            const lv_coord_t line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
            lv_obj_set_style_text_font(ctx->extra_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
            lv_obj_set_style_text_align(ctx->extra_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_set_size(ctx->extra_label, right_w - buttons_w - 8, line);
            char word_buf[40];
            /* measure the word itself (a dot-mode label rewrites its text): the position only when it fits */
            const char *status = room_extra_state_text(ctx, word_buf, sizeof(word_buf), true);
            lv_point_t status_size = {0, 0};
            lv_text_get_size(&status_size, status, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (room_extra_widest_status(ctx, ui_font_role(UI_TEXT_META)) > right_w - buttons_w - 8 ||
                status_size.x > right_w - buttons_w - 8) {
                ctx->extra_short = true;
                status = room_extra_state_text(ctx, word_buf, sizeof(word_buf), false);
            }
            lv_label_set_text(ctx->extra_label, status);
            lv_obj_set_pos(ctx->extra_label, pad + ring_d + 10, ring_top + (ring_d - line) / 2);
            /* the small humidity label would sit in the same place */
            lv_obj_set_hidden(ctx->hum_label, true);
        } else if (action_mode && h >= 100 && w >= 200) {
            ctx->extra_shown = true;
            ctx->extra_compact = true;
            const lv_font_t *fnt = ui_font_role(UI_TEXT_META);
            const lv_coord_t line = lv_font_get_line_height(fnt);
            /* measure the word itself: a label in dot mode rewrites its own text once it was too narrow */
            char word_buf[40];
            const lv_coord_t max_w = (w - 2 * pad) * 52 / 100;
            /* the word with the position if it fits, the word alone otherwise, and the icon alone as the last resort */
            const char *word = room_extra_state_text(ctx, word_buf, sizeof(word_buf), true);
            lv_point_t tsize = {0, 0};
            lv_text_get_size(&tsize, word, fnt, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (8 + 24 + 4 + tsize.x + 8 > max_w ||
                8 + 24 + 4 + room_extra_widest_status(ctx, fnt) + 8 > max_w) {
                ctx->extra_short = true;
                word = room_extra_state_text(ctx, word_buf, sizeof(word_buf), false);
                lv_text_get_size(&tsize, word, fnt, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            }
            lv_coord_t btn_w = 8 + 24 + 4 + tsize.x + 8; /* icon, status word, padding */
            const bool with_text = btn_w <= max_w;
            if (!with_text) {
                btn_w = ROOM_ACTION_SIZE;
            }
            if (btn_w < ROOM_ACTION_SIZE) {
                btn_w = ROOM_ACTION_SIZE;
            }
            lv_obj_set_size(ctx->extra_btn, btn_w, ROOM_ACTION_SIZE);
            lv_obj_set_pos(ctx->extra_btn, w - pad - btn_w, h - ROOM_ACTION_SIZE - 6);
            lv_obj_set_hidden(ctx->extra_icon, false);
            if (with_text) {
                lv_obj_align(ctx->extra_icon, LV_ALIGN_LEFT_MID, 8, 0);
                lv_obj_set_style_text_font(ctx->extra_label, fnt, LV_PART_MAIN);
                lv_obj_set_style_text_align(ctx->extra_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                lv_obj_set_size(ctx->extra_label, btn_w - 8 - 24 - 4 - 8, line);
                lv_label_set_text(ctx->extra_label, word);
                /* a sibling above the button and not clickable itself: a tap on the word reaches the button */
                lv_obj_set_pos(ctx->extra_label, w - pad - btn_w + 8 + 24 + 4, h - ROOM_ACTION_SIZE - 6 + (ROOM_ACTION_SIZE - line) / 2);
            } else {
                lv_obj_center(ctx->extra_icon);
            }
            lv_obj_set_hidden(ctx->extra_label, !with_text);
            chips_w = w - 2 * pad - btn_w - gap;
        }
        lv_obj_set_hidden(ctx->extra_btn, !ctx->extra_shown);
        if (ctx->extra_btn2 != NULL) {
            lv_obj_set_hidden(ctx->extra_btn2, !(ctx->extra_shown && ctx->extra_dual));
        }
        room_render_extra(ctx); /* the icons follow the layout (one toggle or two directions) */
        if (!ctx->extra_shown || ctx->extra_compact == false) {
            lv_obj_set_hidden(ctx->extra_label, !ctx->extra_shown);
        }
    }

    /* Chips share the bottom row equally. In a narrow card the less important ones
     * (presence, then lights) give way so that the text stays readable. */
    if (room_controls_shown(ctx->controls) && room_controls_has_lights(ctx->controls)) {
        ctx->chip_wanted[1] = false; /* the lights pill in the control row says it better */
    }
    static const int drop_order[] = {2, 1};
    int visible = 0;
    for (int i = 0; i < 4; i++) {
        if (ctx->chip_wanted[i]) {
            visible++;
        }
    }
    for (size_t d = 0; d < sizeof(drop_order) / sizeof(drop_order[0]); d++) {
        if (visible > 2 && (chips_w - gap * (visible - 1)) / visible < 100 && ctx->chip_wanted[drop_order[d]]) {
            ctx->chip_wanted[drop_order[d]] = false;
            visible--;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (ctx->chip_wanted[i]) {
            lv_obj_set_hidden(ctx->chip[i], false);
        } else {
            lv_obj_set_hidden(ctx->chip[i], true);
        }
    }
    if (visible == 0) {
        return;
    }
    const lv_coord_t cw = (chips_w - gap * (visible - 1)) / visible;
    lv_coord_t x = pad;
    for (int i = 0; i < 4; i++) {
        if (!ctx->chip_wanted[i]) {
            continue;
        }
        lv_obj_set_size(ctx->chip[i], cw, chip_h);
        lv_obj_set_pos(ctx->chip[i], x, h - chip_h - 12);
        lv_obj_set_style_text_font(ctx->chip_label[i], ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_width(ctx->chip_label[i], cw - 8);
        lv_obj_set_height(ctx->chip_label[i], lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
        lv_obj_set_style_text_align(ctx->chip_label[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(ctx->chip_label[i]);
        x += cw + gap;
    }
}

/* ---- popup ------------------------------------------------------------ */

static void room_popup_close(w_room_ctx_t *ctx)
{
    if (ctx->popup != NULL) {
        lv_obj_t *p = ctx->popup;
        ctx->popup = NULL;
        lv_obj_del_async(p);
    }
}

static void room_popup_close_cb(lv_event_t *e)
{
    room_popup_close((w_room_ctx_t *)lv_event_get_user_data(e));
}

static void room_popup_toggle_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (id == NULL) {
        return;
    }
    const char *dot = strchr(id, '.');
    if (dot == NULL) {
        return;
    }
    char domain[16];
    snprintf(domain, sizeof(domain), "%.*s", (int)(dot - id) < 15 ? (int)(dot - id) : 15, id);
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", id);
    (void)ha_client_call_service(domain, strcmp(domain, "media_player") == 0 ? "media_play_pause" : "toggle", payload);
}

static void room_show_popup(w_room_ctx_t *ctx)
{
    if (ctx->popup != NULL) {
        return;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int sw = lv_obj_get_width(screen);
    int sh = lv_obj_get_height(screen);
    if (sw <= 0) sw = 720;
    if (sh <= 0) sh = 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    ctx->popup = overlay;
    lv_obj_set_size(overlay, sw, sh);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, room_popup_close_cb, LV_EVENT_CLICKED, ctx);

    int cw = sw - 48 < 520 ? sw - 48 : 520;
    int ch = sh - 48 < 560 ? sh - 48 : 560;
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, cw, ch);
    lv_obj_center(card);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_event_bubble(card, false);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    ui_popup_style_frame(card);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ctx->title[0] != '\0' ? ctx->title : ctx->ids[0]);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_pos(title, 0, 8);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), room_popup_close_cb, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -6);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 52);
    lv_obj_set_size(list, cw - 32, ch - 32 - 52);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    ha_state_t *st = ui_calloc_prefer_psram(1, sizeof(*st));
    for (int i = 0; i < ROOM_MAX_ENTITIES && st != NULL; i++) {
        const char *id = ctx->ids[i];
        if (id[0] == '\0') {
            continue;
        }
        char name[APP_MAX_NAME_LEN] = {0};
        char value[APP_MAX_STATE_LEN + APP_MAX_UNIT_LEN + 2] = {0};
        if (ha_model_get_state(id, st)) {
            cJSON *attrs = st->attributes_json[0] != '\0' ? cJSON_Parse(st->attributes_json) : NULL;
            snprintf(name, sizeof(name), "%s", room_attr_string(attrs, "friendly_name"));
            const char *unit = room_attr_string(attrs, "unit_of_measurement");
            snprintf(value, sizeof(value), "%s%s%s", st->state, unit[0] != '\0' ? " " : "", unit);
            cJSON_Delete(attrs);
        }
        if (name[0] == '\0') {
            strlcpy(name, id, sizeof(name));
        }
        const bool toggle = room_domain_is(id, "light") || room_domain_is(id, "switch") || room_domain_is(id, "cover") ||
                            room_domain_is(id, "fan") || room_domain_is(id, "media_player");
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_style_radius(row, ui_style_radius(10), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_t *nl = lv_label_create(row);
        lv_label_set_text(nl, name);
        lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nl, LV_PCT(62));
        lv_obj_set_style_text_font(nl, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(nl, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_align(nl, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text(vl, value);
        lv_obj_set_style_text_font(vl, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(vl, lv_color_hex(ROOM_MUTED_COLOR), LV_PART_MAIN);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, -12, 0);
        if (toggle) {
            lv_obj_set_clickable(row, true);
            lv_obj_add_event_cb(row, room_popup_toggle_cb, LV_EVENT_CLICKED, (void *)ctx->ids[i]);
        }
    }
    free(st);
}

static void room_extra_call(const w_room_ctx_t *ctx, room_action_t action)
{
    if (action == ROOM_ACTION_NONE) {
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->extra_id);
    (void)ha_client_call_service(room_action_domain(action), room_action_service(action), payload);
}

/* The action of the extra field: the explicit Home Assistant service for what the state allows, or nothing (a disabled
 * control never calls). The state is re-read here, not taken from the button's last render. */
static void room_extra_clicked_cb(lv_event_t *e)
{
    w_room_ctx_t *ctx = (w_room_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL || ctx->extra_mode == ROOM_EXTRA_DEFAULT || ctx->extra_id[0] == '\0') {
        return;
    }
    room_evaluate(ctx);
    const room_action_t action = ctx->extra_dual ? ctx->extra_up : ctx->extra_action;
    room_extra_call(ctx, action);
}

static void room_extra2_clicked_cb(lv_event_t *e)
{
    w_room_ctx_t *ctx = (w_room_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL || ctx->extra_mode != ROOM_EXTRA_COVER || ctx->extra_id[0] == '\0' || !ctx->extra_dual) {
        return;
    }
    room_evaluate(ctx);
    room_extra_call(ctx, ctx->extra_down);
}

static void room_clicked_cb(lv_event_t *e)
{
    room_show_popup((w_room_ctx_t *)lv_event_get_user_data(e));
}

static void room_event_cb(lv_event_t *e)
{
    w_room_ctx_t *ctx = (w_room_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        if (ctx->popup != NULL) {
            lv_obj_del(ctx->popup);
            ctx->popup = NULL;
        }
        room_controls_destroy(ctx->controls);
        free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED) {
        room_layout(ctx);
    }
}

/* Same ring language as the gauge widget's radial style (rotation/gap/knob removal), but a
 * fixed color instead of ui_style_fill_level_color()'s gold-to-green fill: see
 * ROOM_TEMP_RING_COLOR above for why a temperature reading does not get a "full = good"
 * verdict color. */
static void room_style_temp_arc(lv_obj_t *arc)
{
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, ROOM_TEMP_RING_MIN, ROOM_TEMP_RING_MAX);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    ui_style_gauge_arc(arc, ROOM_TEMP_RING_STROKE, 6,
        lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 30),
        lv_color_hex(APP_UI_COLOR_CARD_BORDER), lv_color_hex(ROOM_TEMP_RING_COLOR));
}

static lv_obj_t *room_chip(lv_obj_t *card, lv_obj_t **label_out)
{
    lv_obj_t *chip = lv_obj_create(card);
    lv_obj_remove_style_all(chip);
    lv_obj_set_scrollable(chip, false);
    lv_obj_set_clickable(chip, false);
    lv_obj_set_style_radius(chip, ui_style_radius(LV_RADIUS_CIRCLE), LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(chip, LV_OPA_70, LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    *label_out = label;
    return chip;
}

esp_err_t w_room_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_room_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* The room's own sensor first, then up to ROOM_MAX_ENTITIES - 1 more (extra_entity_id[]). */
    snprintf(ctx->ids[0], sizeof(ctx->ids[0]), "%s", def->entity_id);
    for (int i = 1; i < ROOM_MAX_ENTITIES; i++) {
        snprintf(ctx->ids[i], sizeof(ctx->ids[i]), "%s", def->extra_entity_id[i - 1]);
    }
    snprintf(ctx->title, sizeof(ctx->title), "%s", def->title);
    /* The extra field only counts when its entity is one the card already has and fits the mode. */
    room_extra_mode_t mode = ROOM_EXTRA_DEFAULT;
    if (room_extra_mode_parse(def->display_mode, &mode) && mode != ROOM_EXTRA_DEFAULT &&
        room_extra_entity_fits(mode, def->secondary_entity_id)) {
        for (int i = 0; i < ROOM_MAX_ENTITIES; i++) {
            if (ctx->ids[i][0] != '\0' && strcmp(ctx->ids[i], def->secondary_entity_id) == 0) {
                ctx->extra_mode = mode;
                strlcpy(ctx->extra_id, def->secondary_entity_id, sizeof(ctx->extra_id));
                break;
            }
        }
    }

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title_label, ctx->title[0] != '\0' ? ctx->title : ctx->ids[0]);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    ctx->temp_arc = lv_arc_create(card);
    room_style_temp_arc(ctx->temp_arc);
    lv_obj_set_hidden(ctx->temp_arc, true);
    ctx->temp_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->temp_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->temp_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    ctx->hum_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->hum_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->hum_label, lv_color_hex(ROOM_MUTED_COLOR), LV_PART_MAIN);
    for (int i = 0; i < 4; i++) {
        ctx->chip[i] = room_chip(card, &ctx->chip_label[i]);
        lv_obj_set_hidden(ctx->chip[i], true);
    }
    if (ctx->extra_mode == ROOM_EXTRA_COVER || ctx->extra_mode == ROOM_EXTRA_MEDIA_PLAYER) {
        ctx->extra_btn = lv_obj_create(card);
        lv_obj_remove_style_all(ctx->extra_btn);
        lv_obj_set_scrollable(ctx->extra_btn, false);
        lv_obj_set_clickable(ctx->extra_btn, true);
        lv_obj_set_event_bubble(ctx->extra_btn, false); /* a tap here must not also open the room popup */
        lv_obj_set_style_radius(ctx->extra_btn, ui_style_radius(12), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->extra_btn, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->extra_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->extra_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(ctx->extra_btn, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
        lv_obj_set_hidden(ctx->extra_btn, true);
        ctx->extra_icon = lv_label_create(ctx->extra_btn);
        lv_obj_set_style_text_font(ctx->extra_icon, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->extra_icon, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_label_set_text(ctx->extra_icon, room_extra_symbol(ctx));
        lv_obj_center(ctx->extra_icon);
        ctx->extra_label = lv_label_create(card);
        lv_obj_set_style_text_font(ctx->extra_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_label_set_long_mode(ctx->extra_label, LV_LABEL_LONG_DOT);
        lv_obj_set_clickable(ctx->extra_label, false);
        lv_obj_set_hidden(ctx->extra_label, true);
        lv_obj_add_event_cb(ctx->extra_btn, room_extra_clicked_cb, LV_EVENT_CLICKED, ctx);
        if (ctx->extra_mode == ROOM_EXTRA_COVER) {
            ctx->extra_btn2 = lv_obj_create(card);
            lv_obj_remove_style_all(ctx->extra_btn2);
            lv_obj_set_scrollable(ctx->extra_btn2, false);
            lv_obj_set_clickable(ctx->extra_btn2, true);
            lv_obj_set_event_bubble(ctx->extra_btn2, false);
            lv_obj_set_style_radius(ctx->extra_btn2, ui_style_radius(12), LV_PART_MAIN);
            lv_obj_set_style_bg_color(ctx->extra_btn2, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ctx->extra_btn2, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(ctx->extra_btn2, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(ctx->extra_btn2, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
            lv_obj_set_hidden(ctx->extra_btn2, true);
            ctx->extra_icon2 = lv_label_create(ctx->extra_btn2);
            lv_obj_set_style_text_font(ctx->extra_icon2, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
            lv_obj_set_style_text_color(ctx->extra_icon2, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
            lv_label_set_text(ctx->extra_icon2, APP_SYMBOL_ARROW_DOWN);
            lv_obj_center(ctx->extra_icon2);
            lv_obj_add_event_cb(ctx->extra_btn2, room_extra2_clicked_cb, LV_EVENT_CLICKED, ctx);
        }
    }

    if (def->room_controls) {
        ctx->controls = room_controls_create(card, ctx->ids, ROOM_MAX_ENTITIES);
    }
    lv_obj_add_event_cb(card, room_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, room_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, room_clicked_cb, LV_EVENT_CLICKED, ctx);
    room_evaluate(ctx);
    room_render(ctx);
    room_layout(ctx);
    return ESP_OK;
}

void w_room_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_room_ctx_t *ctx = (w_room_ctx_t *)instance->ctx;
    room_evaluate(ctx);
    room_render(ctx);
    room_layout(ctx);
}

void w_room_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_room_ctx_t *ctx = (w_room_ctx_t *)instance->ctx;
    room_evaluate(ctx);
    room_render(ctx);
}
