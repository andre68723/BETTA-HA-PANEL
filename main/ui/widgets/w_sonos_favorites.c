/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Sonos favorites widget. Uses a Sonos media_player.* as target and the
 * Home Assistant sensor.sonos_favorites entity as source for favorite IDs.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "app_config.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_bindings.h"
#include "ui/ui_i18n.h"

#define W_SONOS_TAG "w_sonos_favorites"
#define W_SONOS_DEFAULT_SENSOR "sensor.sonos_favorites"
#define W_SONOS_MAX_FAVORITES 16
#define W_SONOS_ROW_POOL W_SONOS_MAX_FAVORITES
#define W_SONOS_ID_LEN 96
#define W_SONOS_NAME_LEN 72
#define W_SONOS_STATION_BUTTON_H 44
#define W_SONOS_ROW_GAP 10
/* Fixed column for the play symbol in front of each favorite name. */
#define W_SONOS_ICON_COL_W 24
#define W_SONOS_ICON_GAP 8

typedef struct {
    char item_id[W_SONOS_ID_LEN];
    char name[W_SONOS_NAME_LEN];
} w_sonos_favorite_t;

typedef struct {
    char player_entity_id[APP_MAX_ENTITY_ID_LEN];
    char favorites_entity_id[APP_MAX_ENTITY_ID_LEN];
    char player_state[APP_MAX_STATE_LEN];
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    lv_obj_t *list_container;
    lv_obj_t *placeholder_label;
    lv_obj_t *rows[W_SONOS_ROW_POOL];
    lv_obj_t *row_labels[W_SONOS_ROW_POOL];
    lv_obj_t *row_icons[W_SONOS_ROW_POOL];
    size_t row_bound_index1[W_SONOS_ROW_POOL];
    w_sonos_favorite_t favorites[W_SONOS_MAX_FAVORITES];
    size_t favorite_count;
    bool favorites_seen;
    bool unavailable;
    int list_w;
} w_sonos_ctx_t;

static void *w_sonos_calloc(size_t count, size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return calloc(count, size);
}

static void w_sonos_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static bool w_sonos_state_matches(const char *state_entity_id, const char *entity_id)
{
    return state_entity_id != NULL && entity_id != NULL && entity_id[0] != '\0' &&
           strncmp(state_entity_id, entity_id, APP_MAX_ENTITY_ID_LEN) == 0;
}

static void w_sonos_style_card(lv_obj_t *card)
{
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
    lv_obj_set_scrollable(card, false);
}

static void w_sonos_style_station_button(lv_obj_t *button)
{
    lv_obj_set_style_radius(button, ui_style_radius(16), LV_PART_MAIN);
    const lv_color_t btn_bg = lv_color_hex(APP_UI_COLOR_CARD_BG_ON);
    ui_style_button(button, btn_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER),
                            lv_color_hex(APP_UI_COLOR_STATE_ON));
    if (ui_style_is_flat()) {
        lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    }
    lv_obj_set_style_pad_left(button, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(button, 14, LV_PART_MAIN);
    const lv_color_t btn_icon = ui_style_button_icon_color(btn_bg);
    lv_obj_set_style_text_color(button, btn_icon, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, btn_icon, LV_STATE_PRESSED);
    lv_obj_set_scrollable(button, false);
}

/* One line per favorite: two columns when every name fits its column at the
 * normal size, otherwise one full-width column; names that still do not fit
 * are set one size smaller and finally cut with dots. */
static void w_sonos_apply_layout(w_sonos_ctx_t *ctx)
{
    const int list_w = ctx->list_w;
    const int label_extra = 28 + W_SONOS_ICON_COL_W + W_SONOS_ICON_GAP;
    const int col_w2 = (list_w - W_SONOS_ROW_GAP) / 2;

    int max_w = 0;
    for (size_t i = 0; i < ctx->favorite_count; i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, ctx->favorites[i].name, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > max_w) {
            max_w = sz.x;
        }
    }
    const bool two_cols = list_w >= 300 && max_w <= col_w2 - label_extra;
    const int station_w = two_cols ? col_w2 : list_w;
    const int label_w = station_w - label_extra;
    const lv_font_t *font = (max_w > label_w) ? ui_font_role(UI_TEXT_META) : ui_font_role(UI_TEXT_META);

    for (size_t i = 0; i < W_SONOS_ROW_POOL; i++) {
        lv_obj_set_size(ctx->rows[i], station_w, W_SONOS_STATION_BUTTON_H);
        lv_obj_t *label = ctx->row_labels[i];
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_size(label, label_w, lv_font_get_line_height(font));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, W_SONOS_ICON_COL_W + W_SONOS_ICON_GAP, 0);
    }
}

static void w_sonos_render(w_sonos_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return;
    }

    /* Just the player's state (e.g. "idle"), not "<entity_id> - idle": the card's own title
     * already says which player this is, so the raw entity id here was pure technical clutter. */
    const char *state = ctx->player_state[0] != '\0' ? ctx->player_state : ui_i18n_get("common.unavailable", "unavailable");
    lv_label_set_text(ctx->status_label, state);

    const bool has_favorites = ctx->favorite_count > 0U;
    if (has_favorites) {
        lv_obj_set_hidden(ctx->placeholder_label, true);
    } else {
        lv_obj_set_hidden(ctx->placeholder_label, false);
        if (ctx->unavailable) {
            lv_label_set_text(ctx->placeholder_label, ui_i18n_get("common.unavailable", "unavailable"));
        } else {
            lv_label_set_text(ctx->placeholder_label, ui_i18n_get("sonos_favorites.empty", "No favorites found."));
        }
    }

    w_sonos_apply_layout(ctx);
    for (size_t i = 0; i < W_SONOS_ROW_POOL; i++) {
        if (i < ctx->favorite_count) {
            lv_obj_set_hidden(ctx->rows[i], false);
            lv_label_set_text(ctx->row_labels[i], ctx->favorites[i].name);
            ctx->row_bound_index1[i] = i + 1U;
        } else {
            lv_obj_set_hidden(ctx->rows[i], true);
            ctx->row_bound_index1[i] = 0U;
        }
    }
}

static void w_sonos_parse_favorites(w_sonos_ctx_t *ctx, const ha_state_t *state)
{
    if (ctx == NULL || state == NULL) {
        return;
    }

    ctx->favorite_count = 0U;
    ctx->favorites_seen = true;
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;

    if (state->attributes_json[0] == '\0') {
        return;
    }

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        return;
    }

    cJSON *items = cJSON_GetObjectItemCaseSensitive(attrs, "items");
    if (cJSON_IsObject(items)) {
        for (cJSON *item = items->child; item != NULL && ctx->favorite_count < W_SONOS_MAX_FAVORITES; item = item->next) {
            if (item->string == NULL || !cJSON_IsString(item) || item->valuestring == NULL ||
                item->string[0] == '\0' || item->valuestring[0] == '\0') {
                continue;
            }
            w_sonos_copy_text(ctx->favorites[ctx->favorite_count].item_id,
                sizeof(ctx->favorites[ctx->favorite_count].item_id), item->string);
            w_sonos_copy_text(ctx->favorites[ctx->favorite_count].name,
                sizeof(ctx->favorites[ctx->favorite_count].name), item->valuestring);
            ctx->favorite_count++;
        }
    }

    cJSON_Delete(attrs);
}

static void w_sonos_row_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    w_sonos_ctx_t *ctx = (w_sonos_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *row = lv_event_get_target(event);
    if (ctx == NULL || row == NULL) {
        return;
    }

    for (size_t i = 0; i < W_SONOS_ROW_POOL; i++) {
        if (ctx->rows[i] != row || ctx->row_bound_index1[i] == 0U) {
            continue;
        }
        size_t favorite_index = ctx->row_bound_index1[i] - 1U;
        if (favorite_index < ctx->favorite_count) {
            esp_err_t err = ui_bindings_media_player_play_favorite(
                ctx->player_entity_id, ctx->favorites[favorite_index].item_id);
            if (err != ESP_OK) {
                ESP_LOGW(W_SONOS_TAG, "play favorite failed player=%s favorite=%s err=%s",
                    ctx->player_entity_id, ctx->favorites[favorite_index].item_id, esp_err_to_name(err));
            }
        }
        return;
    }
}

static void w_sonos_card_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }
    w_sonos_ctx_t *ctx = (w_sonos_ctx_t *)lv_event_get_user_data(event);
    if (ctx != NULL) {
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
        heap_caps_free(ctx);
#else
        free(ctx);
#endif
    }
}

esp_err_t w_sonos_favorites_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    w_sonos_ctx_t *ctx = (w_sonos_ctx_t *)w_sonos_calloc(1, sizeof(w_sonos_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    w_sonos_copy_text(ctx->player_entity_id, sizeof(ctx->player_entity_id), def->entity_id);
    w_sonos_copy_text(ctx->favorites_entity_id,
        sizeof(ctx->favorites_entity_id),
        def->secondary_entity_id[0] != '\0' ? def->secondary_entity_id : W_SONOS_DEFAULT_SENSOR);
    w_sonos_copy_text(out_instance->secondary_entity_id, sizeof(out_instance->secondary_entity_id), ctx->favorites_entity_id);
    w_sonos_copy_text(ctx->player_state, sizeof(ctx->player_state), "unknown");

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    w_sonos_style_card(card);
    lv_obj_add_event_cb(card, w_sonos_card_event_cb, LV_EVENT_DELETE, ctx);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title_label, def->title[0] != '\0' ? def->title : "Sonos Favorites");
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    ui_widget_name_one_line(ctx->title_label);
    lv_obj_set_pos(ctx->title_label, 0, 0);
    lv_obj_set_width(ctx->title_label, def->w - 32);

    ctx->status_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->status_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->status_label, LV_LABEL_LONG_DOT);
    /* title, status and list follow the line height of the text in use (25 px at the reference size: the former 34 / 64 / 30 offsets) */
    const int line_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int status_y = def->hide_name ? 0 : line_h + 9;
    const int list_y = def->hide_name ? line_h + 5 : status_y + line_h + 5;
    lv_obj_set_pos(ctx->status_label, 0, status_y);
    lv_obj_set_width(ctx->status_label, def->w - 32);

    const int list_w = def->w - 32;
    ctx->list_w = list_w;
    const int station_cols = list_w >= 300 ? 2 : 1;
    const int station_w = station_cols > 1 ? ((list_w - W_SONOS_ROW_GAP) / 2) : list_w;

    ctx->list_container = lv_obj_create(card);
    lv_obj_set_pos(ctx->list_container, 0, list_y);
    lv_obj_set_size(ctx->list_container, list_w, def->h - list_y - 16);
    lv_obj_set_style_bg_opa(ctx->list_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->list_container, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ctx->list_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->list_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(ctx->list_container, W_SONOS_ROW_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(ctx->list_container, W_SONOS_ROW_GAP, LV_PART_MAIN);
    lv_obj_set_layout(ctx->list_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ctx->list_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(ctx->list_container, LV_DIR_VER);

    ctx->placeholder_label = lv_label_create(ctx->list_container);
    lv_obj_set_width(ctx->placeholder_label, list_w);
    lv_obj_set_style_text_font(ctx->placeholder_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->placeholder_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->placeholder_label, LV_LABEL_LONG_WRAP);

    for (size_t i = 0; i < W_SONOS_ROW_POOL; i++) {
        lv_obj_t *row = lv_btn_create(ctx->list_container);
        ctx->rows[i] = row;
        lv_obj_set_size(row, station_w, W_SONOS_STATION_BUTTON_H);
        w_sonos_style_station_button(row);
        lv_obj_add_event_cb(row, w_sonos_row_event_cb, LV_EVENT_CLICKED, ctx);

        /* The play symbol lives in its own label with the MDI symbol font
         * (app_symbols.h): the Poppins text fonts carry no symbols, so a
         * symbol embedded in the name label would not render -- same
         * pattern as the media player's icon buttons. */
        lv_obj_t *icon = lv_label_create(row);
        ctx->row_icons[i] = icon;
        lv_label_set_text(icon, APP_SYMBOL_PLAY);
        lv_obj_set_style_text_font(icon, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_width(icon, W_SONOS_ICON_COL_W);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *label = lv_label_create(row);
        ctx->row_labels[i] = label;
        lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(label, station_w - 28 - W_SONOS_ICON_COL_W - W_SONOS_ICON_GAP,
                        lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, W_SONOS_ICON_COL_W + W_SONOS_ICON_GAP, 0);
        lv_obj_set_hidden(row, true);
    }

    w_sonos_render(ctx);
    return ESP_OK;
}

void w_sonos_favorites_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || state == NULL || instance->ctx == NULL) {
        return;
    }
    w_sonos_ctx_t *ctx = (w_sonos_ctx_t *)instance->ctx;
    if (w_sonos_state_matches(state->entity_id, ctx->player_entity_id)) {
        w_sonos_copy_text(ctx->player_state, sizeof(ctx->player_state), state->state);
    } else if (w_sonos_state_matches(state->entity_id, ctx->favorites_entity_id)) {
        w_sonos_parse_favorites(ctx, state);
    } else {
        return;
    }
    w_sonos_render(ctx);
}

void w_sonos_favorites_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_sonos_ctx_t *ctx = (w_sonos_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    w_sonos_copy_text(ctx->player_state, sizeof(ctx->player_state), "unavailable");
    w_sonos_render(ctx);
}
