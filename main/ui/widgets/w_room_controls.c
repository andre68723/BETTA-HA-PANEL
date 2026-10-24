/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/w_room_controls.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ui/ui_widget_factory.h"

#include "ha/ha_client.h"
#include "ha/ha_model.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "util/room_extra.h"

#define RC_MAX 8
#define RC_HEIGHT 44 /* every control is a touch target of at least 44 px */
#define RC_GAP 8
#define RC_TOGGLE_MIN_W 90
#define RC_COVER_MIN_W (2 * RC_HEIGHT + 84)

typedef enum { RC_LIGHTS = 0, RC_SWITCHES, RC_COVERS, RC_GROUPS } rc_group_kind_t;

typedef struct {
    char ids[RC_MAX][APP_MAX_ENTITY_ID_LEN];
    int count;
    int usable;
    int on;     /* lights, switches: on; covers: open or opening */
    int moving; /* covers: opening or closing */
    int position; /* covers, a single one: current_position, -1 unknown */
    room_action_t up, down; /* covers: merged actions of all members */
    lv_obj_t *box;   /* the pill (a button for lights and switches) */
    char status[40]; /* the text of the label: a label in dot mode rewrites its own text, so keep it here */
    lv_obj_t *label;
    lv_obj_t *up_btn, *up_icon, *down_btn, *down_icon; /* covers */
    bool shown;
} rc_group_t;

struct room_controls {
    rc_group_t group[RC_GROUPS];
    bool shown;
};

static const char *const kDomain[RC_GROUPS] = {"light", "switch", "cover"};

static bool rc_domain_is(const char *id, const char *domain)
{
    const size_t n = strlen(domain);
    return strncmp(id, domain, n) == 0 && id[n] == '.';
}

/* A service for every member at once: {"entity_id":["a","b"]}. */
static void rc_call(const rc_group_t *g, const char *domain, const char *service)
{
    if (service == NULL || g->count == 0) {
        return;
    }
    char payload[RC_MAX * (APP_MAX_ENTITY_ID_LEN + 4) + 24];
    size_t n = (size_t)snprintf(payload, sizeof(payload), "{\"entity_id\":[");
    for (int i = 0; i < g->count && n < sizeof(payload) - APP_MAX_ENTITY_ID_LEN - 8; i++) {
        n += (size_t)snprintf(payload + n, sizeof(payload) - n, "%s\"%s\"", i > 0 ? "," : "", g->ids[i]);
    }
    snprintf(payload + n, sizeof(payload) - n, "]}");
    (void)ha_client_call_service(domain, service, payload);
}

typedef struct {
    room_controls_t *controls;
    rc_group_kind_t kind;
} rc_event_t;

static void rc_toggle_cb(lv_event_t *e)
{
    rc_event_t *ev = (rc_event_t *)lv_event_get_user_data(e);
    if (ev == NULL) {
        return;
    }
    room_controls_refresh(ev->controls); /* decide on the state now, not on the last render */
    const rc_group_t *g = &ev->controls->group[ev->kind];
    rc_call(g, kDomain[ev->kind], room_group_toggle_service(g->usable, g->on));
}

static void rc_cover_cb(lv_event_t *e, bool up)
{
    rc_event_t *ev = (rc_event_t *)lv_event_get_user_data(e);
    if (ev == NULL) {
        return;
    }
    room_controls_refresh(ev->controls);
    const rc_group_t *g = &ev->controls->group[RC_COVERS];
    const room_action_t action = up ? g->up : g->down;
    if (action != ROOM_ACTION_NONE) {
        rc_call(g, room_action_domain(action), room_action_service(action));
    }
}

static void rc_up_cb(lv_event_t *e) { rc_cover_cb(e, true); }
static void rc_down_cb(lv_event_t *e) { rc_cover_cb(e, false); }

static void rc_event_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static lv_obj_t *rc_box(lv_obj_t *card, bool clickable)
{
    lv_obj_t *box = lv_obj_create(card);
    lv_obj_remove_style_all(box);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, clickable);
    lv_obj_set_event_bubble(box, false); /* a tap here must not also open the room popup */
    lv_obj_set_style_radius(box, ui_style_radius(12), LV_PART_MAIN);
    lv_obj_set_style_bg_color(box, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_hidden(box, true);
    return box;
}

static lv_obj_t *rc_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, lv_font_get_line_height(ui_font_role(UI_TEXT_META))); /* one line: a long status is cut with dots, not wrapped */
    lv_obj_set_clickable(label, false);
    return label;
}

static void rc_attach(room_controls_t *c, rc_group_kind_t kind, lv_obj_t *obj, lv_event_cb_t cb)
{
    rc_event_t *ev = malloc(sizeof(*ev));
    if (ev == NULL) {
        return;
    }
    ev->controls = c;
    ev->kind = kind;
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, ev);
    lv_obj_add_event_cb(obj, rc_event_delete_cb, LV_EVENT_DELETE, ev);
}

room_controls_t *room_controls_create(lv_obj_t *card, const char ids[][APP_MAX_ENTITY_ID_LEN], int count)
{
    room_controls_t *c = ui_calloc_prefer_psram(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    bool any = false;
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < RC_GROUPS; k++) {
            if (ids[i][0] != '\0' && rc_domain_is(ids[i], kDomain[k]) && c->group[k].count < RC_MAX) {
                snprintf(c->group[k].ids[c->group[k].count++], APP_MAX_ENTITY_ID_LEN, "%s", ids[i]);
                any = true;
            }
        }
    }
    if (!any) {
        free(c);
        return NULL;
    }
    for (int k = 0; k < RC_GROUPS; k++) {
        rc_group_t *g = &c->group[k];
        if (g->count == 0) {
            continue;
        }
        g->position = -1;
        g->box = rc_box(card, k != RC_COVERS);
        g->label = rc_label(g->box);
        if (k != RC_COVERS) {
            rc_attach(c, (rc_group_kind_t)k, g->box, rc_toggle_cb);
        } else {
            g->up_btn = rc_box(card, true);
            g->up_icon = rc_label(g->up_btn);
            lv_obj_set_style_text_font(g->up_icon, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
            g->down_btn = rc_box(card, true);
            g->down_icon = rc_label(g->down_btn);
            lv_obj_set_style_text_font(g->down_icon, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
            rc_attach(c, RC_COVERS, g->up_btn, rc_up_cb);
            rc_attach(c, RC_COVERS, g->down_btn, rc_down_cb);
        }
    }
    room_controls_refresh(c);
    return c;
}

void room_controls_destroy(room_controls_t *controls)
{
    free(controls); /* the LVGL objects belong to the card and go with it */
}

bool room_controls_has_lights(const room_controls_t *c) { return c != NULL && c->group[RC_LIGHTS].count > 0; }
bool room_controls_has_covers(const room_controls_t *c) { return c != NULL && c->group[RC_COVERS].count > 0; }
bool room_controls_shown(const room_controls_t *c) { return c != NULL && c->shown; }

static void rc_text(const room_controls_t *c, rc_group_kind_t kind, char *out, size_t out_len)
{
    const rc_group_t *g = &c->group[kind];
    if (g->usable == 0) {
        snprintf(out, out_len, "--");
    } else if (kind == RC_LIGHTS) {
        if (g->on == 0) snprintf(out, out_len, "%s", ui_i18n_get("room.lights_off", "Lights off"));
        else if (g->on == 1) snprintf(out, out_len, "%s", ui_i18n_get("room.light_on", "Light on"));
        else snprintf(out, out_len, ui_i18n_get("room.lights_on_n", "%d lights on"), g->on);
    } else if (kind == RC_SWITCHES) {
        if (g->on == 0) snprintf(out, out_len, "%s", ui_i18n_get("room.switches_off", "Switches off"));
        else if (g->on == 1) snprintf(out, out_len, "%s", ui_i18n_get("room.switch_on", "Switch on"));
        else snprintf(out, out_len, ui_i18n_get("room.switches_on_n", "%d switches on"), g->on);
    } else if (g->moving > 0) {
        snprintf(out, out_len, "%s", ui_i18n_get("cover.moving", "Moving"));
    } else if (g->usable == 1 && g->position >= 0 && g->on > 0) {
        snprintf(out, out_len, "%s %d %%", ui_i18n_get("cover.open", "Open"), g->position);
    } else if (g->on == 0) {
        snprintf(out, out_len, "%s", ui_i18n_get("cover.closed", "Closed"));
    } else if (g->usable == 1) {
        snprintf(out, out_len, "%s", ui_i18n_get("cover.open", "Open"));
    } else {
        snprintf(out, out_len, "%d/%d %s", g->on, g->usable, ui_i18n_get("cover.open", "Open"));
    }
}

void room_controls_refresh(room_controls_t *c)
{
    if (c == NULL) {
        return;
    }
    ha_state_t *st = ui_calloc_prefer_psram(1, sizeof(*st));
    if (st == NULL) {
        return;
    }
    for (int k = 0; k < RC_GROUPS; k++) {
        rc_group_t *g = &c->group[k];
        g->usable = g->on = g->moving = 0;
        g->position = -1;
        g->up = g->down = ROOM_ACTION_NONE;
        for (int i = 0; i < g->count; i++) {
            if (!ha_model_get_state(g->ids[i], st) || strcmp(st->state, "unavailable") == 0 || strcmp(st->state, "unknown") == 0) {
                continue;
            }
            g->usable++;
            if (k != RC_COVERS) {
                g->on += strcmp(st->state, "on") == 0 ? 1 : 0;
                continue;
            }
            int position = -1;
            uint32_t features = 0;
            bool features_known = false;
            cJSON *attrs = st->attributes_json[0] != '\0' ? cJSON_Parse(st->attributes_json) : NULL;
            cJSON *pos = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, "current_position") : NULL;
            cJSON *feat = attrs != NULL ? cJSON_GetObjectItemCaseSensitive(attrs, "supported_features") : NULL;
            if (cJSON_IsNumber(pos)) position = (int)lroundf((float)pos->valuedouble);
            if (cJSON_IsNumber(feat)) {
                features = (uint32_t)feat->valuedouble;
                features_known = true;
            }
            cJSON_Delete(attrs);
            g->position = position;
            g->on += (strcmp(st->state, "open") == 0 || strcmp(st->state, "opening") == 0) ? 1 : 0;
            g->moving += (strcmp(st->state, "opening") == 0 || strcmp(st->state, "closing") == 0) ? 1 : 0;
            g->up = room_cover_group_merge(g->up, room_extra_cover_up(st->state, position, features, features_known));
            g->down = room_cover_group_merge(g->down, room_extra_cover_down(st->state, position, features, features_known));
        }
        if (g->count == 0 || g->box == NULL) {
            continue;
        }
        rc_text(c, (rc_group_kind_t)k, g->status, sizeof(g->status));
        lv_label_set_text(g->label, g->status);
        if (k == RC_COVERS) {
            const char *up_sym = g->up == ROOM_ACTION_COVER_STOP ? APP_SYMBOL_STOP : APP_SYMBOL_ARROW_UP;
            const char *down_sym = g->down == ROOM_ACTION_COVER_STOP ? APP_SYMBOL_STOP : APP_SYMBOL_ARROW_DOWN;
            lv_label_set_text(g->up_icon, up_sym);
            lv_label_set_text(g->down_icon, down_sym);
            ui_style_action_button(g->up_btn, g->up_icon, g->up == ROOM_ACTION_COVER_STOP ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, g->up != ROOM_ACTION_NONE);
            ui_style_action_button(g->down_btn, g->down_icon, g->down == ROOM_ACTION_COVER_STOP ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, g->down != ROOM_ACTION_NONE);
        } else {
            const bool on = room_group_is_on(g->usable, g->on);
            ui_style_action_button(g->box, g->label, on ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, g->usable > 0);
        }
    }
    free(st);
}

lv_coord_t room_controls_layout(room_controls_t *c, lv_coord_t card_w, lv_coord_t pad, lv_coord_t bottom_y, lv_coord_t max_h)
{
    if (c == NULL) {
        return 0;
    }
    /* the groups present, widest need first dropped: switches, then lights; covers stay */
    bool want[RC_GROUPS];
    lv_coord_t min_w[RC_GROUPS];
    int n = 0;
    lv_coord_t need = 0;
    for (int k = 0; k < RC_GROUPS; k++) {
        want[k] = c->group[k].count > 0;
        min_w[k] = k == RC_COVERS ? RC_COVER_MIN_W : RC_TOGGLE_MIN_W;
        if (want[k]) {
            need += min_w[k] + (n > 0 ? RC_GAP : 0);
            n++;
        }
    }
    const lv_coord_t avail = card_w - 2 * pad;
    const int drop_order[] = {RC_SWITCHES, RC_LIGHTS};
    for (size_t d = 0; d < 2 && need > avail; d++) {
        if (want[drop_order[d]]) {
            need -= min_w[drop_order[d]] + (n > 1 ? RC_GAP : 0);
            want[drop_order[d]] = false;
            n--;
        }
    }
    c->shown = n > 0 && need <= avail && max_h >= RC_HEIGHT;
    for (int k = 0; k < RC_GROUPS; k++) {
        rc_group_t *g = &c->group[k];
        g->shown = c->shown && want[k];
        if (g->box == NULL) {
            continue;
        }
        lv_obj_set_hidden(g->box, !g->shown);
        if (g->up_btn != NULL) {
            lv_obj_set_hidden(g->up_btn, !g->shown);
            lv_obj_set_hidden(g->down_btn, !g->shown);
        }
    }
    if (!c->shown) {
        return 0;
    }
    /* the cover pill needs its minimum for the status and two buttons; lights and switches share the rest */
    const lv_coord_t share = (avail - RC_GAP * (n - 1)) / n;
    const bool cover_shown = c->group[RC_COVERS].shown;
    const lv_coord_t cover_w = cover_shown ? (share > RC_COVER_MIN_W ? share : RC_COVER_MIN_W) : 0;
    const int toggles = n - (cover_shown ? 1 : 0);
    const lv_coord_t toggle_w = toggles > 0 ? (avail - RC_GAP * (n - 1) - cover_w) / toggles : 0;
    lv_coord_t x = pad;
    const lv_coord_t y = bottom_y - RC_HEIGHT;
    for (int k = 0; k < RC_GROUPS; k++) {
        rc_group_t *g = &c->group[k];
        if (!g->shown) {
            continue;
        }
        const lv_coord_t w_each = k == RC_COVERS ? cover_w : toggle_w;
        lv_obj_set_size(g->box, w_each, RC_HEIGHT);
        lv_obj_set_pos(g->box, x, y);
        if (k != RC_COVERS) {
            lv_obj_set_width(g->label, w_each - 12);
            lv_obj_set_style_text_align(g->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_align(g->label, LV_ALIGN_CENTER, 0, 0);
        } else {
            /* the status on the left, the two buttons flush right inside the pill */
            const lv_coord_t label_w = w_each - 2 * RC_HEIGHT - 14;
            lv_obj_set_width(g->label, label_w);
            /* too narrow for "1/2 Open": drop the trailing word and keep "1/2" */
            char text[40];
            snprintf(text, sizeof(text), "%s", g->status);
            lv_point_t size = {0, 0};
            lv_text_get_size(&size, text, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            char *space = strrchr(text, ' ');
            if (size.x + 10 > label_w && space != NULL) {
                *space = '\0';
            }
            lv_label_set_text(g->label, text);
            lv_obj_set_style_text_align(g->label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_obj_align(g->label, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_set_size(g->up_btn, RC_HEIGHT, RC_HEIGHT);
            lv_obj_set_size(g->down_btn, RC_HEIGHT, RC_HEIGHT);
            lv_obj_set_pos(g->down_btn, x + w_each - RC_HEIGHT, y);
            lv_obj_set_pos(g->up_btn, x + w_each - 2 * RC_HEIGHT, y);
            lv_obj_center(g->up_icon);
            lv_obj_center(g->down_icon);
        }
        x += w_each + RC_GAP;
    }
    return RC_HEIGHT;
}
