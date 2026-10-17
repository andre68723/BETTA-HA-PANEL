/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * House page: an automatic grid of room cards, grouped by floor with a small heading per floor.
 * The rooms come from page.house.rooms (typically filled by the editor's import from Home
 * Assistant areas); every room becomes a room_tile widget definition handed to a callback, so the
 * firmware and the desktop simulator place the same page. Moved out of ui_runtime.c.
 */
#include "ui/display_geometry.h"
#include "ui/ui_house_page.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_memory.h"

static ui_widget_def_t *house_def_scratch(void)
{
    static ui_widget_def_t *s_def = NULL;
    if (s_def == NULL) {
        s_def = ui_calloc_prefer_psram(1, sizeof(*s_def));
    }
    if (s_def != NULL) {
        memset(s_def, 0, sizeof(*s_def));
    }
    return s_def;
}

static void house_copy_json_string(const cJSON *obj, const char *key, char *dst, size_t dst_size)
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

void ui_house_page_build(const cJSON *page, lv_obj_t *page_container, ui_house_room_cb_t create_room, void *user)
{
    if (page == NULL || page_container == NULL || create_room == NULL) {
        return;
    }
    const cJSON *house = cJSON_GetObjectItemCaseSensitive(page, "house");
    const cJSON *rooms = cJSON_IsObject(house) ? cJSON_GetObjectItemCaseSensitive(house, "rooms") : NULL;
    if (!cJSON_IsArray(rooms)) {
        return;
    }
    const int room_count = cJSON_GetArraySize(rooms);
    const int gap = 10;
    const int header_h = 30;
    int cols = APP_VIEW_CONTENT_WIDTH / 230;
    if (cols < 2) cols = 2;
    if (cols > 6) cols = 6;
    const int cell_w = (APP_VIEW_CONTENT_WIDTH - gap * (cols - 1) - 2 * gap) / cols;

    /* Floors in order of first appearance; rooms of a floor keep their order. */
    char floors[16][APP_MAX_NAME_LEN] = {{0}};
    int floor_of[APP_MAX_WIDGETS_PER_PAGE] = {0};
    int floor_count = 0;
    for (int r = 0; r < room_count && r < APP_MAX_WIDGETS_PER_PAGE; r++) {
        const cJSON *fl = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rooms, r), "floor");
        const char *name = cJSON_IsString(fl) && fl->valuestring != NULL ? fl->valuestring : "";
        int idx = -1;
        for (int f = 0; f < floor_count; f++) {
            if (strcmp(floors[f], name) == 0) {
                idx = f;
                break;
            }
        }
        if (idx < 0 && floor_count < 16) {
            strlcpy(floors[floor_count], name, sizeof(floors[0]));
            idx = floor_count++;
        }
        floor_of[r] = idx < 0 ? 0 : idx;
    }
    const bool show_headers = floor_count > 1 || (floor_count == 1 && floors[0][0] != '\0');

    /* Height: header rows plus card rows; shrink the cards (never below 120) to fit. */
    int card_rows = 0;
    int headers = 0;
    for (int f = 0; f < floor_count; f++) {
        int n = 0;
        for (int r = 0; r < room_count && r < APP_MAX_WIDGETS_PER_PAGE; r++) {
            if (floor_of[r] == f) n++;
        }
        if (n > 0) {
            card_rows += (n + cols - 1) / cols;
            if (show_headers) headers++;
        }
    }
    int cell_h = 150;
    const int avail_h = APP_VIEW_CONTENT_HEIGHT - 2 * gap;
    while (cell_h > 120 && card_rows * cell_h + (card_rows - 1) * gap + headers * header_h > avail_h) {
        cell_h -= 5;
    }
    /* Small panels cannot show every room: the page scrolls vertically instead of cutting rooms off. */
    if (card_rows * cell_h + (card_rows - 1) * gap + headers * header_h > avail_h) {
        lv_obj_set_scrollable(page_container, true);
        lv_obj_set_scroll_dir(page_container, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_bottom(page_container, gap, LV_PART_MAIN);
    }

    int y = gap;
    for (int f = 0; f < floor_count; f++) {
        int in_floor = 0;
        for (int r = 0; r < room_count && r < APP_MAX_WIDGETS_PER_PAGE; r++) {
            if (floor_of[r] != f) continue;
            if (in_floor == 0 && show_headers) {
                lv_obj_t *label = lv_label_create(page_container);
                lv_label_set_text(label, floors[f][0] != '\0' ? floors[f] : "-");
                lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
                lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
                lv_obj_set_pos(label, gap + 4, y);
                y += header_h;
            }
            const int col = in_floor % cols;
            if (in_floor > 0 && col == 0) {
                y += cell_h + gap;
            }
            const cJSON *room = cJSON_GetArrayItem(rooms, r);
            ui_widget_def_t *def = house_def_scratch();
            if (def == NULL) {
                break;
            }
            strlcpy(def->type, "room_tile", sizeof(def->type));
            snprintf(def->id, sizeof(def->id), "house_room_%d", r);
            house_copy_json_string(room, "name", def->title, sizeof(def->title));
            house_copy_json_string(room, "entity_id", def->entity_id, sizeof(def->entity_id));
            house_copy_json_string(room, "room_extra_mode", def->display_mode, sizeof(def->display_mode));
            house_copy_json_string(room, "room_extra_entity_id", def->secondary_entity_id, sizeof(def->secondary_entity_id));
            int n = 0;
            const cJSON *extra = NULL;
            cJSON_ArrayForEach(extra, cJSON_GetObjectItemCaseSensitive(room, "room_entities")) {
                if (n < UI_WIDGET_EXTRA_ENTITIES && cJSON_IsString(extra) && extra->valuestring != NULL) {
                    snprintf(def->extra_entity_id[n++], APP_MAX_ENTITY_ID_LEN, "%s", extra->valuestring);
                }
            }
            def->x = gap + col * (cell_w + gap);
            def->y = y;
            def->w = cell_w;
            def->h = cell_h;
            (void)create_room(def, page_container, user);
            in_floor++;
        }
        if (in_floor > 0) {
            y += cell_h + gap;
        }
    }
}
