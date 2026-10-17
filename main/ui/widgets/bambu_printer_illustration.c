/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/bambu_printer_illustration.h"

#include <stdlib.h>

#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_memory.h"

/* The drawing is 1000 x 800 units. */
#define ART_W 1000
#define ART_H 800

typedef enum {
    PART_BASE,
    PART_BED,
    PART_POST_L,
    PART_POST_R,
    PART_GANTRY,
    PART_HEAD,
    PART_NOZZLE,
    PART_SPOOL,
    PART_SPOOL_HUB,
    PART_FOOT_L,
    PART_FOOT_R,
    PART_COUNT,
} part_t;

/* x, y, w, h in drawing units */
static const struct {
    int16_t x, y, w, h;
    int16_t radius; /* in drawing units, < 0 = circle */
} k_parts[PART_COUNT] = {
    [PART_BASE] = {50, 690, 900, 90, 30},
    [PART_BED] = {200, 600, 600, 46, 12},
    [PART_POST_L] = {80, 170, 64, 530, 14},
    [PART_POST_R] = {856, 170, 64, 530, 14},
    [PART_GANTRY] = {80, 190, 840, 58, 14},
    [PART_HEAD] = {410, 248, 180, 150, 22},
    [PART_NOZZLE] = {478, 398, 44, 58, 8},
    [PART_SPOOL] = {660, 20, 220, 150, -1},
    [PART_SPOOL_HUB] = {748, 78, 44, 34, -1},
    [PART_FOOT_L] = {110, 780, 140, 20, 8},
    [PART_FOOT_R] = {750, 780, 140, 20, 8},
};

struct bambu_illustration {
    lv_obj_t *root;
    lv_obj_t *part[PART_COUNT];
    bool nozzle_hot;
    bool bed_hot;
    bool active;
};

static lv_color_t line_color(const bambu_illustration_t *ill)
{
    const lv_color_t base = lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 150);
    return ill->active ? base : lv_color_mix(base, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 110);
}

static lv_color_t warm(lv_color_t base, bool hot)
{
    return hot ? lv_color_mix(lv_color_hex(0xF28B30), base, 215) : base;
}

void bambu_illustration_restyle(bambu_illustration_t *ill)
{
    if (ill == NULL) {
        return;
    }
    const lv_color_t line = line_color(ill);
    const lv_color_t card = lv_color_hex(APP_UI_COLOR_CARD_BG_OFF);
    const lv_color_t frame = lv_color_mix(line, card, 190);
    const lv_color_t soft = lv_color_mix(line, card, 90);
    for (int i = 0; i < PART_COUNT; i++) {
        lv_obj_t *obj = ill->part[i];
        lv_color_t fill = frame;
        lv_color_t border = line;
        switch ((part_t)i) {
        case PART_BASE:
        case PART_FOOT_L:
        case PART_FOOT_R:
            fill = soft;
            break;
        case PART_BED:
            fill = warm(lv_color_mix(line, card, 230), ill->bed_hot);
            border = warm(line, ill->bed_hot);
            break;
        case PART_HEAD:
            fill = lv_color_mix(line, card, 170);
            break;
        case PART_NOZZLE:
            fill = warm(line, ill->nozzle_hot);
            border = fill;
            break;
        case PART_SPOOL:
            fill = card;
            border = lv_color_mix(line, card, 150);
            break;
        case PART_SPOOL_HUB:
            fill = soft;
            break;
        default:
            break;
        }
        lv_obj_set_style_bg_color(obj, fill, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, i == PART_SPOOL ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
    }
}

static void root_deleted_cb(lv_event_t *event)
{
    free(lv_event_get_user_data(event));
}

bambu_illustration_t *bambu_illustration_create(lv_obj_t *parent)
{
    bambu_illustration_t *ill = ui_calloc_prefer_psram(1, sizeof(*ill));
    if (ill == NULL) {
        return NULL;
    }
    ill->active = true;
    ill->root = lv_obj_create(parent);
    lv_obj_remove_style_all(ill->root);
    lv_obj_set_scrollable(ill->root, false);
    lv_obj_set_clickable(ill->root, false);
    lv_obj_add_event_cb(ill->root, root_deleted_cb, LV_EVENT_DELETE, ill);
    for (int i = 0; i < PART_COUNT; i++) {
        lv_obj_t *obj = lv_obj_create(ill->root);
        lv_obj_remove_style_all(obj);
        lv_obj_set_scrollable(obj, false);
        lv_obj_set_clickable(obj, false);
        lv_obj_set_style_border_width(obj, i == PART_SPOOL ? 3 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
        ill->part[i] = obj;
    }
    bambu_illustration_restyle(ill);
    return ill;
}

lv_obj_t *bambu_illustration_obj(const bambu_illustration_t *ill)
{
    return ill != NULL ? ill->root : NULL;
}

void bambu_illustration_layout(bambu_illustration_t *ill, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                               bambu_illustration_anchors_t *anchors)
{
    if (ill == NULL || w <= 0 || h <= 0) {
        return;
    }
    lv_obj_set_pos(ill->root, x, y);
    lv_obj_set_size(ill->root, w, h);
    /* One uniform scale in 1/1024 steps: the picture is never stretched. */
    int32_t scale = (int32_t)w * 1024 / ART_W;
    const int32_t scale_h = (int32_t)h * 1024 / ART_H;
    if (scale_h < scale) {
        scale = scale_h;
    }
    const lv_coord_t art_w = (lv_coord_t)((int32_t)ART_W * scale / 1024);
    const lv_coord_t art_h = (lv_coord_t)((int32_t)ART_H * scale / 1024);
    const lv_coord_t off_x = (w - art_w) / 2;
    const lv_coord_t off_y = (h - art_h) / 2;
    for (int i = 0; i < PART_COUNT; i++) {
        const lv_coord_t px = off_x + (lv_coord_t)(k_parts[i].x * scale / 1024);
        const lv_coord_t py = off_y + (lv_coord_t)(k_parts[i].y * scale / 1024);
        lv_coord_t pw = (lv_coord_t)(k_parts[i].w * scale / 1024);
        lv_coord_t ph = (lv_coord_t)(k_parts[i].h * scale / 1024);
        pw = pw < 2 ? 2 : pw;
        ph = ph < 2 ? 2 : ph;
        lv_obj_set_pos(ill->part[i], px, py);
        lv_obj_set_size(ill->part[i], pw, ph);
        lv_obj_set_style_radius(ill->part[i],
                                k_parts[i].radius < 0 ? LV_RADIUS_CIRCLE : (lv_coord_t)(k_parts[i].radius * scale / 1024),
                                LV_PART_MAIN);
    }
    if (anchors != NULL) {
        anchors->art_x = off_x;
        anchors->art_y = off_y;
        anchors->art_w = art_w;
        anchors->art_h = art_h;
        anchors->nozzle_x = off_x + (lv_coord_t)((k_parts[PART_HEAD].x + k_parts[PART_HEAD].w) * scale / 1024);
        anchors->nozzle_y = off_y + (lv_coord_t)((k_parts[PART_HEAD].y + k_parts[PART_HEAD].h / 2) * scale / 1024);
        anchors->bed_x = off_x + (lv_coord_t)((k_parts[PART_BED].x + k_parts[PART_BED].w / 2) * scale / 1024);
        anchors->bed_y = off_y + (lv_coord_t)((k_parts[PART_BASE].y + k_parts[PART_BASE].h / 2) * scale / 1024);
    }
}

void bambu_illustration_set_heat(bambu_illustration_t *ill, bool nozzle_hot, bool bed_hot, bool active)
{
    if (ill == NULL) {
        return;
    }
    if (ill->nozzle_hot == nozzle_hot && ill->bed_hot == bed_hot && ill->active == active) {
        return;
    }
    ill->nozzle_hot = nozzle_hot;
    ill->bed_hot = bed_hot;
    ill->active = active;
    bambu_illustration_restyle(ill);
}
