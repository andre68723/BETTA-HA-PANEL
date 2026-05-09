/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Todo list widget. Displays the items of a HA `todo.*` entity as a
 * touch-friendly scrollable list. Items are pulled on demand via the
 * `todo.get_items` service call (return_response=true) and periodically
 * refreshed. Tapping a row toggles the item's completion state via
 * `todo.update_item`.
 *
 * Adding or removing items is intentionally NOT supported from the panel –
 * that is expected to be driven via HA voice assist.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "drivers/display_init.h"
#include "ha/ha_client.h"
#include "ui/fonts/app_text_fonts.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_i18n.h"

#define W_TODO_TAG "w_todo"
/* Focus is on showing all *open* items; completed ones are appended at the
 * bottom and reachable by scrolling.  Cap is generous enough to cover
 * typical shopping lists without keeping every historic completion
 * in RAM. */
#define W_TODO_MAX_ITEMS 48
#define W_TODO_UID_LEN 80
#define W_TODO_SUMMARY_LEN 96
#define W_TODO_REFRESH_PERIOD_MS 300000
#define W_TODO_REFRESH_AFTER_UPDATE_MS 800
#define W_TODO_RETRY_AFTER_FAILURE_MS 30000
#define W_TODO_TICK_PERIOD_MS 1000
#define W_TODO_ROW_H 58
#define W_TODO_ROW_GAP 8
#define W_TODO_BADGE_SIZE 34
#define W_TODO_ROW_POOL 14

typedef struct {
    char uid[W_TODO_UID_LEN];
    char summary[W_TODO_SUMMARY_LEN];
    bool completed;
} w_todo_item_t;

typedef struct w_todo_ctx {
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    lv_obj_t *list_container;
    lv_obj_t *placeholder_label;
    lv_obj_t *scroll_spacer;
    lv_obj_t *rows[W_TODO_ROW_POOL];
    lv_obj_t *row_badges[W_TODO_ROW_POOL];
    lv_obj_t *row_badge_labels[W_TODO_ROW_POOL];
    lv_obj_t *row_texts[W_TODO_ROW_POOL];
    size_t row_bound_item_index1[W_TODO_ROW_POOL];
    lv_coord_t row_bound_width[W_TODO_ROW_POOL];
    bool row_bound_completed[W_TODO_ROW_POOL];
    lv_coord_t scroll_spacer_width;
    lv_coord_t scroll_spacer_total_h;
    char entity_id[APP_MAX_ENTITY_ID_LEN];

    /* Displayed items (accessed from LVGL context only). */
    w_todo_item_t display_items[W_TODO_MAX_ITEMS];
    size_t display_item_count;
    bool unavailable;
    bool fetching;
    int64_t fetch_started_unix_ms;
    int64_t last_fetch_unix_ms;
    int64_t next_fetch_unix_ms;
    char last_entity_state[APP_MAX_STATE_LEN];
    int64_t last_entity_state_changed_unix_ms;

    /* Staging slot filled by the WS response callback, drained by the
     * refresh timer which runs in LVGL task context. */
    SemaphoreHandle_t staging_mutex;
    w_todo_item_t pending_items[W_TODO_MAX_ITEMS];
    size_t pending_item_count;
    bool pending_valid;
    bool pending_failure;

    lv_timer_t *tick_timer;
    bool scrolling;
    bool destroyed;
} w_todo_ctx_t;

static int64_t w_todo_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static lv_color_t w_todo_surface_fill(uint8_t mix)
{
    return lv_color_mix(lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BG), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), mix);
}

static lv_color_t w_todo_pressed_fill(lv_color_t color)
{
    return lv_color_brightness(color) > 127 ? lv_color_darken(color, 24) : lv_color_lighten(color, 24);
}

static lv_color_t w_todo_contrast_text(lv_color_t bg)
{
    return lv_color_brightness(bg) > 150 ? lv_color_hex(0x101418) : lv_color_hex(0xFFFFFF);
}

static void w_todo_request_items(w_todo_ctx_t *ctx);
static void w_todo_render(w_todo_ctx_t *ctx);
static void w_todo_update_status_label(w_todo_ctx_t *ctx);
static void w_todo_layout_visible_rows(w_todo_ctx_t *ctx);

static bool w_todo_is_visible(const w_todo_ctx_t *ctx)
{
    return ctx != NULL && ctx->card != NULL && lv_obj_is_visible(ctx->card);
}

static void *w_todo_calloc(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = calloc(count, size);
    }
    return ptr;
}

static void w_todo_reset_row_bindings(w_todo_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    for (size_t i = 0; i < W_TODO_ROW_POOL; i++) {
        ctx->row_bound_item_index1[i] = 0;
        ctx->row_bound_width[i] = 0;
        ctx->row_bound_completed[i] = false;
    }
}

/* ----- Response callback (runs on HA WS RX task!) ----------------------- */
static void w_todo_response_cb(bool success, cJSON *result, void *user)
{
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)user;
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }

    /* Keep the scratch buffer off the WS RX task stack — with
     * W_TODO_MAX_ITEMS=48 and ~180 bytes per entry we would otherwise
     * burn ~9 KB of stack every response. */
    w_todo_item_t *parsed = w_todo_calloc(W_TODO_MAX_ITEMS, sizeof(*parsed));
    if (parsed == NULL) {
        ESP_LOGW(W_TODO_TAG, "get_items: out of memory staging %d items",
            (int)W_TODO_MAX_ITEMS);
        return;
    }
    size_t parsed_count = 0;
    bool any_items_found = false;

    if (success && result != NULL) {
        /* Response shape: { "response": { "<entity_id>": { "items": [...] } } } */
        cJSON *response = cJSON_GetObjectItemCaseSensitive(result, "response");
        cJSON *entity_obj = NULL;
        if (cJSON_IsObject(response)) {
            /* Try exact entity_id key first, then first child. */
            entity_obj = cJSON_GetObjectItemCaseSensitive(response, ctx->entity_id);
            if (entity_obj == NULL || !cJSON_IsObject(entity_obj)) {
                entity_obj = response->child;
            }
        }
        cJSON *items = NULL;
        if (cJSON_IsObject(entity_obj)) {
            items = cJSON_GetObjectItemCaseSensitive(entity_obj, "items");
        }
        if (!cJSON_IsArray(items)) {
            /* Older HA versions return items at top level. */
            items = cJSON_GetObjectItemCaseSensitive(result, "items");
        }

        if (!cJSON_IsArray(items)) {
            /* Diagnostic: dump top-level keys so we can see the real shape. */
            char keys[160] = {0};
            size_t off = 0;
            if (cJSON_IsObject(result)) {
                for (cJSON *k = result->child; k != NULL && off + 32 < sizeof(keys); k = k->next) {
                    off += snprintf(keys + off, sizeof(keys) - off, "%s%s",
                        off > 0 ? "," : "", k->string ? k->string : "?");
                }
            }
            ESP_LOGW(W_TODO_TAG, "get_items: no items array found (success=%d, result keys=[%s])",
                (int)success, keys);
        }

        if (cJSON_IsArray(items)) {
            any_items_found = true;
            int n = cJSON_GetArraySize(items);
            ESP_LOGI(W_TODO_TAG, "get_items ok: %d items for %s", n, ctx->entity_id);
            /* Two-pass parse: collect open (needs_action) items first, then
             * fill the remaining slots with completed items.  HA commonly
             * returns completed items before open ones, so a single pass
             * with W_TODO_MAX_ITEMS cap would hide all active items. */
            for (int pass = 0; pass < 2 && parsed_count < W_TODO_MAX_ITEMS; pass++) {
                const bool want_completed = (pass == 1);
                for (int i = 0; i < n && parsed_count < W_TODO_MAX_ITEMS; i++) {
                    cJSON *it = cJSON_GetArrayItem(items, i);
                    if (!cJSON_IsObject(it)) {
                        continue;
                    }
                    cJSON *status = cJSON_GetObjectItemCaseSensitive(it, "status");
                    bool is_completed = (cJSON_IsString(status)
                        && status->valuestring != NULL
                        && strcmp(status->valuestring, "completed") == 0);
                    if (is_completed != want_completed) {
                        continue;
                    }
                    cJSON *summary = cJSON_GetObjectItemCaseSensitive(it, "summary");
                    cJSON *uid = cJSON_GetObjectItemCaseSensitive(it, "uid");

                    w_todo_item_t *dst = &parsed[parsed_count];
                    memset(dst, 0, sizeof(*dst));
                    if (cJSON_IsString(uid) && uid->valuestring != NULL) {
                        snprintf(dst->uid, sizeof(dst->uid), "%s", uid->valuestring);
                    }
                    if (cJSON_IsString(summary) && summary->valuestring != NULL) {
                        snprintf(dst->summary, sizeof(dst->summary), "%s", summary->valuestring);
                    }
                    dst->completed = is_completed;
                    if (dst->summary[0] != '\0' || dst->uid[0] != '\0') {
                        parsed_count++;
                    }
                }
            }
        }
    } else {
        ESP_LOGW(W_TODO_TAG, "get_items callback: success=%d result=%p",
            (int)success, (void *)result);
    }

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    if (!ctx->destroyed) {
        if (parsed_count > 0) {
            memcpy(ctx->pending_items, parsed,
                sizeof(ctx->pending_items[0]) * parsed_count);
        }
        ctx->pending_item_count = parsed_count;
        ctx->pending_valid = success && any_items_found;
        ctx->pending_failure = !success;
    }
    xSemaphoreGive(ctx->staging_mutex);
    free(parsed);
}

/* ----- UI rendering (LVGL context only) -------------------------------- */
static void w_todo_item_event_cb(lv_event_t *e);

static bool w_todo_items_equal(const w_todo_item_t *a, const w_todo_item_t *b, size_t count)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (a[i].completed != b[i].completed) {
            return false;
        }
        if (strcmp(a[i].uid, b[i].uid) != 0) {
            return false;
        }
        if (strcmp(a[i].summary, b[i].summary) != 0) {
            return false;
        }
    }
    return true;
}

static void w_todo_move_item(w_todo_ctx_t *ctx, size_t from, size_t to)
{
    if (ctx == NULL || from >= ctx->display_item_count || to >= ctx->display_item_count || from == to) {
        return;
    }

    w_todo_item_t moved = ctx->display_items[from];
    if (from < to) {
        memmove(&ctx->display_items[from], &ctx->display_items[from + 1],
            sizeof(ctx->display_items[0]) * (to - from));
    } else {
        memmove(&ctx->display_items[to + 1], &ctx->display_items[to],
            sizeof(ctx->display_items[0]) * (from - to));
    }
    ctx->display_items[to] = moved;
}

static size_t w_todo_reposition_toggled_item(w_todo_ctx_t *ctx, size_t idx)
{
    if (ctx == NULL || idx >= ctx->display_item_count) {
        return idx;
    }

    size_t target = idx;
    if (ctx->display_items[idx].completed) {
        while (target + 1 < ctx->display_item_count && !ctx->display_items[target + 1].completed) {
            target++;
        }
    } else {
        while (target > 0 && ctx->display_items[target - 1].completed) {
            target--;
        }
    }

    w_todo_move_item(ctx, idx, target);
    return target;
}

static bool w_todo_find_item_index_by_uid(const w_todo_ctx_t *ctx, const char *uid, size_t *out_idx)
{
    if (ctx == NULL || uid == NULL || uid[0] == '\0' || out_idx == NULL) {
        return false;
    }

    for (size_t i = 0; i < ctx->display_item_count; i++) {
        if (strcmp(ctx->display_items[i].uid, uid) == 0) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

static void w_todo_style_item_row(lv_obj_t *row, bool completed)
{
    if (row == NULL) {
        return;
    }

    lv_obj_t *badge = lv_obj_get_child(row, 0);
    lv_obj_t *text = lv_obj_get_child(row, 1);
    lv_obj_t *badge_label = (badge != NULL) ? lv_obj_get_child(badge, 0) : NULL;

    lv_color_t row_bg = completed ? w_todo_surface_fill(172) : w_todo_surface_fill(228);
    lv_color_t row_bg_pressed = w_todo_pressed_fill(row_bg);
    lv_color_t row_border = completed ? lv_color_hex(APP_UI_COLOR_CARD_BORDER)
                                      : lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_color_t badge_bg = completed ? lv_color_hex(APP_UI_COLOR_OK) : w_todo_surface_fill(148);
    lv_color_t badge_border = completed ? lv_color_hex(APP_UI_COLOR_OK)
                                        : lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_color_t badge_text = completed ? w_todo_contrast_text(badge_bg) : lv_color_hex(APP_UI_COLOR_TEXT_MUTED);

    lv_obj_set_style_bg_color(row, row_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(row, row_bg_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, row_border, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, row_border, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(row, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(row, 0, LV_PART_MAIN | LV_STATE_PRESSED);

    if (badge != NULL) {
        lv_obj_set_style_bg_color(badge, badge_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, completed ? LV_OPA_COVER : LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, completed ? 0 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(badge, badge_border, LV_PART_MAIN);
        lv_obj_set_style_border_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(badge, 0, LV_PART_MAIN);
    }

    if (badge_label != NULL) {
        lv_label_set_text(badge_label, completed ? LV_SYMBOL_OK : "");
        lv_obj_set_style_text_color(badge_label, badge_text, LV_PART_MAIN);
    }

    if (text != NULL) {
        lv_obj_set_style_text_color(
            text, completed ? theme_default_color_text_muted() : theme_default_color_text_primary(), LV_PART_MAIN);
        lv_obj_set_style_text_decor(
            text, completed ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, LV_PART_MAIN);
    }
}

static lv_obj_t *w_todo_ensure_placeholder_label(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return NULL;
    }
    if (ctx->placeholder_label == NULL) {
        lv_obj_t *lbl = lv_label_create(ctx->list_container);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, APP_FONT_TEXT_18, LV_PART_MAIN);
        ctx->placeholder_label = lbl;
    }
    return ctx->placeholder_label;
}

static void w_todo_layout_placeholder(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL || ctx->placeholder_label == NULL) {
        return;
    }
    lv_coord_t w = lv_obj_get_content_width(ctx->list_container);
    if (w <= 0) {
        w = lv_obj_get_width(ctx->list_container);
    }
    if (w < 40) {
        w = 40;
    }
    lv_obj_set_width(ctx->placeholder_label, w);
    lv_obj_center(ctx->placeholder_label);
}

static lv_obj_t *w_todo_ensure_scroll_spacer(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return NULL;
    }
    if (ctx->scroll_spacer != NULL) {
        return ctx->scroll_spacer;
    }

    lv_obj_t *spacer = lv_obj_create(ctx->list_container);
    lv_obj_remove_style_all(spacer);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
    ctx->scroll_spacer = spacer;
    return spacer;
}

static void w_todo_update_scroll_spacer(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return;
    }
    lv_obj_t *spacer = w_todo_ensure_scroll_spacer(ctx);
    if (spacer == NULL) {
        return;
    }

    if (ctx->display_item_count == 0) {
        lv_obj_add_flag(spacer, LV_OBJ_FLAG_HIDDEN);
        ctx->scroll_spacer_width = 0;
        ctx->scroll_spacer_total_h = 0;
        return;
    }

    lv_coord_t row_w = lv_obj_get_content_width(ctx->list_container);
    if (row_w <= 0) {
        row_w = lv_obj_get_width(ctx->list_container);
    }
    if (row_w < 1) {
        row_w = 1;
    }
    lv_coord_t total_h = (lv_coord_t)(ctx->display_item_count * (W_TODO_ROW_H + W_TODO_ROW_GAP));
    if (total_h > 0) {
        total_h -= W_TODO_ROW_GAP;
    }
    if (total_h < 1) {
        total_h = 1;
    }

    if (ctx->scroll_spacer_width != row_w || ctx->scroll_spacer_total_h != total_h) {
        lv_obj_set_pos(spacer, 0, total_h - 1);
        lv_obj_set_size(spacer, row_w, 1);
        ctx->scroll_spacer_width = row_w;
        ctx->scroll_spacer_total_h = total_h;
    }
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(spacer);
}

static lv_obj_t *w_todo_ensure_row(w_todo_ctx_t *ctx, size_t slot)
{
    if (ctx == NULL || ctx->list_container == NULL || slot >= W_TODO_ROW_POOL) {
        return NULL;
    }
    if (ctx->rows[slot] != NULL) {
        return ctx->rows[slot];
    }

    lv_obj_t *row = lv_btn_create(ctx->list_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, W_TODO_ROW_H);
    lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row, w_todo_item_event_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, W_TODO_BADGE_SIZE, W_TODO_BADGE_SIZE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *badge_label = lv_label_create(badge);
    lv_label_set_text(badge_label, "");
    lv_obj_set_style_text_font(badge_label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_center(badge_label);

    lv_obj_t *text = lv_label_create(row);
    lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(text, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    ctx->rows[slot] = row;
    ctx->row_badges[slot] = badge;
    ctx->row_badge_labels[slot] = badge_label;
    ctx->row_texts[slot] = text;
    return row;
}

static lv_coord_t w_todo_get_row_width(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return 120;
    }
    lv_coord_t row_w = lv_obj_get_content_width(ctx->list_container);
    if (row_w <= 0) {
        row_w = lv_obj_get_width(ctx->list_container);
    }
    if (row_w < 120) {
        row_w = 120;
    }
    return row_w;
}

static void w_todo_layout_row(w_todo_ctx_t *ctx, size_t slot, size_t item_index, lv_coord_t row_w)
{
    if (ctx == NULL || slot >= W_TODO_ROW_POOL) {
        return;
    }
    lv_obj_t *row = ctx->rows[slot];
    lv_obj_t *badge = ctx->row_badges[slot];
    lv_obj_t *text = ctx->row_texts[slot];
    if (row == NULL || badge == NULL || text == NULL) {
        return;
    }

    lv_obj_set_size(row, row_w, W_TODO_ROW_H);
    lv_obj_set_pos(row, 0, (lv_coord_t)(item_index * (W_TODO_ROW_H + W_TODO_ROW_GAP)));
    lv_coord_t badge_y = (W_TODO_ROW_H - W_TODO_BADGE_SIZE) / 2;
    if (badge_y < 0) {
        badge_y = 0;
    }
    lv_obj_set_pos(badge, 10, badge_y);

    lv_coord_t text_x = 10 + W_TODO_BADGE_SIZE + 12;
    lv_coord_t text_w = row_w - text_x - 14;
    if (text_w < 32) {
        text_w = 32;
    }
    const lv_font_t *text_font = lv_obj_get_style_text_font(text, LV_PART_MAIN);
    lv_coord_t text_h = (text_font != NULL) ? (lv_coord_t)lv_font_get_line_height(text_font) : W_TODO_ROW_H;
    if (text_h > W_TODO_ROW_H) {
        text_h = W_TODO_ROW_H;
    }
    lv_coord_t text_y = (W_TODO_ROW_H - text_h) / 2;
    if (text_y < 0) {
        text_y = 0;
    }
    lv_obj_set_width(text, text_w);
    lv_obj_set_height(text, text_h);
    lv_obj_set_pos(text, text_x, text_y);
}

static void w_todo_layout_visible_rows(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return;
    }

    w_todo_update_scroll_spacer(ctx);

    if (ctx->display_item_count == 0) {
        for (size_t i = 0; i < W_TODO_ROW_POOL; i++) {
            if (ctx->rows[i] != NULL) {
                lv_obj_add_flag(ctx->rows[i], LV_OBJ_FLAG_HIDDEN);
            }
            ctx->row_bound_item_index1[i] = 0;
            ctx->row_bound_width[i] = 0;
        }
        return;
    }

    lv_coord_t row_w = w_todo_get_row_width(ctx);
    lv_coord_t scroll_y = lv_obj_get_scroll_y(ctx->list_container);
    if (scroll_y < 0) {
        scroll_y = 0;
    }
    lv_coord_t viewport_h = lv_obj_get_height(ctx->list_container);
    if (viewport_h <= 0) {
        viewport_h = W_TODO_ROW_H * 4;
    }
    const lv_coord_t row_step = W_TODO_ROW_H + W_TODO_ROW_GAP;
    size_t first = (size_t)(scroll_y / row_step);
    if (first > 0) {
        first--;
    }
    if (first >= ctx->display_item_count) {
        first = ctx->display_item_count - 1U;
    }
    size_t wanted = (size_t)(viewport_h / row_step) + 4U;
    if (wanted > W_TODO_ROW_POOL) {
        wanted = W_TODO_ROW_POOL;
    }
    if (wanted > ctx->display_item_count - first) {
        wanted = ctx->display_item_count - first;
    }

    for (size_t slot = 0; slot < W_TODO_ROW_POOL; slot++) {
        if (slot >= wanted) {
            if (ctx->rows[slot] != NULL) {
                lv_obj_add_flag(ctx->rows[slot], LV_OBJ_FLAG_HIDDEN);
            }
            ctx->row_bound_item_index1[slot] = 0;
            ctx->row_bound_width[slot] = 0;
            continue;
        }

        size_t item_index = first + slot;
        if (item_index >= ctx->display_item_count) {
            if (ctx->rows[slot] != NULL) {
                lv_obj_add_flag(ctx->rows[slot], LV_OBJ_FLAG_HIDDEN);
            }
            ctx->row_bound_item_index1[slot] = 0;
            ctx->row_bound_width[slot] = 0;
            continue;
        }
        lv_obj_t *row = w_todo_ensure_row(ctx, slot);
        if (row == NULL) {
            continue;
        }
        const w_todo_item_t *it = &ctx->display_items[item_index];
        size_t item_index1 = item_index + 1U;
        bool binding_changed = ctx->row_bound_item_index1[slot] != item_index1;
        bool completed_changed = binding_changed || ctx->row_bound_completed[slot] != it->completed;
        if (binding_changed) {
            lv_label_set_text(ctx->row_texts[slot], it->summary[0] != '\0' ? it->summary : it->uid);
            lv_obj_set_user_data(row, (void *)(uintptr_t)item_index1);
            ctx->row_bound_item_index1[slot] = item_index1;
        }
        if (completed_changed) {
            w_todo_style_item_row(row, it->completed);
            ctx->row_bound_completed[slot] = it->completed;
        }
        if (binding_changed || ctx->row_bound_width[slot] != row_w) {
            w_todo_layout_row(ctx, slot, item_index, row_w);
            ctx->row_bound_width[slot] = row_w;
        }
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void w_todo_render(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return;
    }

    if (ctx->unavailable) {
        lv_obj_t *lbl = w_todo_ensure_placeholder_label(ctx);
        if (lbl != NULL) {
            lv_label_set_text(lbl, ui_i18n_get("common.unavailable", "unavailable"));
            lv_obj_set_style_text_color(lbl, theme_default_color_text_muted(), LV_PART_MAIN);
            lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            w_todo_layout_placeholder(ctx);
        }
        if (ctx->scroll_spacer != NULL) {
            lv_obj_add_flag(ctx->scroll_spacer, LV_OBJ_FLAG_HIDDEN);
            ctx->scroll_spacer_width = 0;
            ctx->scroll_spacer_total_h = 0;
        }
        for (size_t i = 0; i < W_TODO_ROW_POOL; i++) {
            if (ctx->rows[i] != NULL) {
                lv_obj_add_flag(ctx->rows[i], LV_OBJ_FLAG_HIDDEN);
            }
            ctx->row_bound_item_index1[i] = 0;
            ctx->row_bound_width[i] = 0;
        }
        return;
    }

    if (ctx->display_item_count == 0) {
        lv_obj_t *lbl = w_todo_ensure_placeholder_label(ctx);
        if (lbl != NULL) {
            const char *txt = ctx->last_fetch_unix_ms > 0
                ? ui_i18n_get("todo.empty", "no open items")
                : ui_i18n_get("todo.loading", "loading...");
            lv_label_set_text(lbl, txt);
            lv_obj_set_style_text_color(lbl, theme_default_color_text_muted(), LV_PART_MAIN);
            lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            w_todo_layout_placeholder(ctx);
        }
        if (ctx->scroll_spacer != NULL) {
            lv_obj_add_flag(ctx->scroll_spacer, LV_OBJ_FLAG_HIDDEN);
            ctx->scroll_spacer_width = 0;
            ctx->scroll_spacer_total_h = 0;
        }
        for (size_t i = 0; i < W_TODO_ROW_POOL; i++) {
            if (ctx->rows[i] != NULL) {
                lv_obj_add_flag(ctx->rows[i], LV_OBJ_FLAG_HIDDEN);
            }
            ctx->row_bound_item_index1[i] = 0;
            ctx->row_bound_width[i] = 0;
        }
        return;
    }

    if (ctx->placeholder_label != NULL) {
        lv_obj_add_flag(ctx->placeholder_label, LV_OBJ_FLAG_HIDDEN);
    }

    w_todo_reset_row_bindings(ctx);
    w_todo_layout_visible_rows(ctx);
}

static void w_todo_update_status_label(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->status_label == NULL) {
        return;
    }
    size_t open = 0;
    for (size_t i = 0; i < ctx->display_item_count; i++) {
        if (!ctx->display_items[i].completed) {
            open++;
        }
    }
    /* Focus on the active count — the completed items are extras the user
     * can scroll to but aren't the point of the widget. */
    const char *text = NULL;
    char buf[24];
    lv_color_t chip_bg = w_todo_surface_fill(184);
    lv_color_t chip_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_color_t chip_text = theme_default_color_text_primary();

    if (ctx->unavailable) {
        text = "-";
        chip_bg = w_todo_surface_fill(132);
        chip_border = lv_color_hex(APP_UI_COLOR_CARD_BORDER);
        chip_text = theme_default_color_text_muted();
    } else if (ctx->last_fetch_unix_ms <= 0 && ctx->display_item_count == 0) {
        text = "...";
        chip_bg = w_todo_surface_fill(148);
        chip_text = theme_default_color_text_muted();
    } else {
        snprintf(buf, sizeof(buf), "%u", (unsigned)open);
        text = buf;
        if (open == 0) {
            chip_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_OK), lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BG), 72);
            chip_border = lv_color_hex(APP_UI_COLOR_OK);
            chip_text = w_todo_contrast_text(chip_bg);
        }
    }

    lv_label_set_text(ctx->status_label, text);
    lv_obj_set_style_text_color(ctx->status_label, chip_text, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->status_label, chip_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->status_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->status_label, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->status_label, chip_border, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ctx->status_label, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->status_label, LV_RADIUS_CIRCLE, LV_PART_MAIN);
}

static void w_todo_drain_pending(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }
    bool have_update = false;
    bool failure = false;
    size_t snapshot_count = 0;
    bool had_fetch_before = ctx->last_fetch_unix_ms > 0;
    bool was_unavailable = ctx->unavailable;
    bool changed = false;

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    if (ctx->pending_valid || ctx->pending_failure) {
        snapshot_count = ctx->pending_item_count;
        have_update = ctx->pending_valid;
        failure = ctx->pending_failure;
        if (have_update && !failure) {
            changed = was_unavailable || !had_fetch_before || ctx->display_item_count != snapshot_count;
            if (!changed && snapshot_count > 0) {
                changed = !w_todo_items_equal(ctx->display_items, ctx->pending_items, snapshot_count);
            }
            if (changed) {
                if (snapshot_count > 0) {
                    memcpy(ctx->display_items, ctx->pending_items,
                        sizeof(ctx->display_items[0]) * snapshot_count);
                }
                ctx->display_item_count = snapshot_count;
            }
        }
        ctx->pending_valid = false;
        ctx->pending_failure = false;
        ctx->pending_item_count = 0;
    }
    xSemaphoreGive(ctx->staging_mutex);

    if (!have_update && !failure) {
        return;
    }

    ctx->fetching = false;
    ctx->fetch_started_unix_ms = 0;
    ctx->last_fetch_unix_ms = w_todo_now_ms();

    if (failure) {
        bool needs_render = !ctx->unavailable || ctx->display_item_count != 0;
        ctx->unavailable = true;
        ctx->display_item_count = 0;
        ctx->next_fetch_unix_ms = ctx->last_fetch_unix_ms + W_TODO_RETRY_AFTER_FAILURE_MS;
        if (needs_render) {
            w_todo_render(ctx);
        }
        w_todo_update_status_label(ctx);
        return;
    }

    ctx->unavailable = false;
    if (changed) {
        w_todo_render(ctx);
    }
    w_todo_update_status_label(ctx);
}

/* ----- Request issuing -------------------------------------------------- */
static void w_todo_request_items(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->fetching) {
        /* Log once so we can tell from the serial monitor whether the
         * widget is even trying and, if not, which guard is holding it
         * back (empty entity vs already in flight). */
        static bool s_logged_skip_empty = false;
        if (ctx != NULL && ctx->entity_id[0] == '\0' && !s_logged_skip_empty) {
            s_logged_skip_empty = true;
            ESP_LOGW(W_TODO_TAG, "skip: no entity_id configured");
        }
        return;
    }

    /* Don't pile a heavy get_items roundtrip on top of the initial layout
     * subscribe burst — that's what triggers the TLS BAD_INPUT_DATA on WS.
     * Wait until the client reports initial sync done, then add a small
     * grace so the follow-up state_changed bursts have drained too. */
    if (!ha_client_is_connected() || !ha_client_is_initial_sync_done()) {
        ctx->next_fetch_unix_ms = w_todo_now_ms() + 1500;
        /* Surface this once per widget so a forever-loading tile is
         * traceable: the HA client never flipped to "initial sync done". */
        static bool s_logged_gate = false;
        if (!s_logged_gate) {
            s_logged_gate = true;
            ESP_LOGI(W_TODO_TAG,
                "waiting for HA: connected=%d initial_sync_done=%d (%s)",
                (int)ha_client_is_connected(),
                (int)ha_client_is_initial_sync_done(),
                ctx->entity_id);
        }
        return;
    }

    ctx->fetching = true;
    ctx->fetch_started_unix_ms = w_todo_now_ms();
    ctx->next_fetch_unix_ms = w_todo_now_ms() + W_TODO_REFRESH_PERIOD_MS;

    ESP_LOGI(W_TODO_TAG, "Requesting todo.get_items for %s", ctx->entity_id);
    esp_err_t err = ha_client_call_service_with_response(
        "todo", "get_items", ctx->entity_id, NULL, w_todo_response_cb, ctx);
    if (err != ESP_OK) {
        ctx->fetching = false;
        ctx->fetch_started_unix_ms = 0;
        if (err == ESP_ERR_INVALID_STATE) {
            /* WS not connected yet, or heavy send gate currently closed.
             * Retry soon and stay quiet in the log — this is expected. */
            ESP_LOGD(W_TODO_TAG, "get_items deferred (%s): gate/ws busy",
                ctx->entity_id);
            ctx->next_fetch_unix_ms = w_todo_now_ms() + 1500;
        } else {
            ESP_LOGW(W_TODO_TAG, "get_items request failed (%s): %s",
                ctx->entity_id, esp_err_to_name(err));
            ctx->next_fetch_unix_ms = w_todo_now_ms() + 5000;
        }
    }
}

/* ----- Touch row toggle handler ---------------------------------------- */
static void w_todo_item_event_cb(lv_event_t *e)
{
    if (e == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_target(e);
    if (ctx == NULL || row == NULL) {
        return;
    }
    uintptr_t idx1 = (uintptr_t)lv_obj_get_user_data(row);
    if (idx1 == 0 || idx1 > ctx->display_item_count) {
        return;
    }
    size_t idx = (size_t)(idx1 - 1);
    w_todo_item_t *it = &ctx->display_items[idx];
    if (it->uid[0] == '\0') {
        return;
    }

    char uid[W_TODO_UID_LEN];
    snprintf(uid, sizeof(uid), "%s", it->uid);
    bool was_completed = it->completed;
    bool new_completed = !it->completed;

    /* Optimistic local update for immediate feedback. */
    it->completed = new_completed;
    w_todo_reposition_toggled_item(ctx, idx);
    w_todo_render(ctx);
    w_todo_update_status_label(ctx);

    /* Build payload: { "entity_id": "...", "item": "<uid>", "status":
     * "needs_action"|"completed" } */
    char payload[384];
    snprintf(payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"item\":\"%s\",\"status\":\"%s\"}",
        ctx->entity_id, uid, new_completed ? "completed" : "needs_action");

    esp_err_t err = ha_client_call_service("todo", "update_item", payload);
    if (err != ESP_OK) {
        ESP_LOGW(W_TODO_TAG, "update_item failed (%s): %s",
            ctx->entity_id, esp_err_to_name(err));
        size_t current_idx = 0;
        if (w_todo_find_item_index_by_uid(ctx, uid, &current_idx)) {
            ctx->display_items[current_idx].completed = was_completed;
            w_todo_move_item(ctx, current_idx, idx);
        }
        w_todo_render(ctx);
        w_todo_update_status_label(ctx);
        return;
    }

    /* Schedule a refresh shortly after to reconcile with server. */
    ctx->next_fetch_unix_ms = w_todo_now_ms() + W_TODO_REFRESH_AFTER_UPDATE_MS;
}

/* ----- Periodic tick ---------------------------------------------------- */
static void w_todo_tick_cb(lv_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx->destroyed) {
        return;
    }

    int64_t now = w_todo_now_ms();
    if (!w_todo_is_visible(ctx)) {
        if (ctx->fetching && ctx->fetch_started_unix_ms > 0 &&
            (now - ctx->fetch_started_unix_ms) > 15000) {
            ctx->fetching = false;
            ctx->fetch_started_unix_ms = 0;
            ctx->next_fetch_unix_ms = now + W_TODO_RETRY_AFTER_FAILURE_MS;
        }
        return;
    }

    if (ctx->scrolling) {
        if (ctx->fetching && ctx->fetch_started_unix_ms > 0 &&
            (now - ctx->fetch_started_unix_ms) > 15000) {
            ctx->fetching = false;
            ctx->fetch_started_unix_ms = 0;
            ctx->next_fetch_unix_ms = now + W_TODO_RETRY_AFTER_FAILURE_MS;
        }
        return;
    }

    w_todo_drain_pending(ctx);

    if (!ctx->fetching && now >= ctx->next_fetch_unix_ms) {
        w_todo_request_items(ctx);
    }

    /* If a fetch has been outstanding for >12 s, clear the flag so the next
     * tick can retry.  The registry itself times out at 15 s. */
    if (ctx->fetching && ctx->fetch_started_unix_ms > 0 &&
        (now - ctx->fetch_started_unix_ms) > 15000) {
        ctx->fetching = false;
        ctx->fetch_started_unix_ms = 0;
        ctx->next_fetch_unix_ms = now + W_TODO_RETRY_AFTER_FAILURE_MS;
        ESP_LOGW(W_TODO_TAG, "get_items timed out for %s, retry in %d ms",
            ctx->entity_id, W_TODO_RETRY_AFTER_FAILURE_MS);
    }
}

/* ----- Event / lifecycle ------------------------------------------------ */
static void w_todo_list_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        ctx->scrolling = true;
        return;
    }
    if (code == LV_EVENT_SCROLL) {
        w_todo_layout_visible_rows(ctx);
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        ctx->scrolling = false;
        if (!w_todo_is_visible(ctx)) {
            return;
        }
        w_todo_drain_pending(ctx);
        int64_t now = w_todo_now_ms();
        if (!ctx->fetching && now >= ctx->next_fetch_unix_ms) {
            w_todo_request_items(ctx);
        }
        return;
    }
    if (code == LV_EVENT_SIZE_CHANGED) {
        if (ctx->display_item_count > 0) {
            w_todo_layout_visible_rows(ctx);
        } else {
            w_todo_layout_placeholder(ctx);
        }
    }
}

static void w_todo_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        /* Unregister any pending HA response callback that still points at
         * this ctx.  MUST happen before the ctx is freed, otherwise the WS
         * RX task may invoke w_todo_response_cb on a dangling pointer when
         * the WS later disconnects (expire_all path) or a late response
         * arrives. */
        ha_client_cancel_pending_responses_for_user(ctx);

        if (ctx->tick_timer != NULL) {
            lv_timer_del(ctx->tick_timer);
            ctx->tick_timer = NULL;
        }
        /* Mark destroyed so any late response callback becomes a no-op. */
        if (ctx->staging_mutex != NULL) {
            xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
            ctx->destroyed = true;
            xSemaphoreGive(ctx->staging_mutex);
            vSemaphoreDelete(ctx->staging_mutex);
            ctx->staging_mutex = NULL;
        }
        free(ctx);
    }
}

esp_err_t w_todo_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    theme_default_style_card(card);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);

    /* Header row: title + status. */
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    lv_obj_set_style_text_color(title, theme_default_color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_22, LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(title, 1);

    lv_obj_t *status = lv_label_create(header);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, APP_FONT_TEXT_18, LV_PART_MAIN);
    lv_obj_set_style_pad_left(status, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(status, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(status, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(status, 4, LV_PART_MAIN);

    /* Scrollable list container. */
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    w_todo_ctx_t *ctx = w_todo_calloc(1, sizeof(w_todo_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    ctx->card = card;
    ctx->title_label = title;
    ctx->status_label = status;
    ctx->list_container = list;
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->staging_mutex = xSemaphoreCreateMutex();
    if (ctx->staging_mutex == NULL) {
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    lv_obj_add_event_cb(card, w_todo_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(list, w_todo_list_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(list, w_todo_list_event_cb, LV_EVENT_SCROLL, ctx);
    lv_obj_add_event_cb(list, w_todo_list_event_cb, LV_EVENT_SCROLL_BEGIN, ctx);
    lv_obj_add_event_cb(list, w_todo_list_event_cb, LV_EVENT_SCROLL_END, ctx);

    /* Initial placeholder render. */
    w_todo_render(ctx);
    w_todo_update_status_label(ctx);

    /* Periodic tick drives both refresh and response draining.  Entity state
     * changes wake reconciliation quickly; the slow periodic poll is only a
     * safety net, so keep the timer gentle on LVGL/TLS. */
    ctx->tick_timer = lv_timer_create(w_todo_tick_cb, W_TODO_TICK_PERIOD_MS, ctx);

    /* First fetch is deferred: we want the HA WS to be fully idle after
     * the initial subscribe burst (which also delivers state for our todo
     * entity) so the large get_items response doesn't land mid state-burst
     * and trip the TLS receive path.  The tick + is_initial_sync_done gate
     * in w_todo_request_items() keeps blocking until the client is really
     * ready; this 5 s floor adds a margin for any follow-up heavy requests
     * (weather forecast, energy stats) that run right after initial sync. */
    ctx->next_fetch_unix_ms = w_todo_now_ms() + 5000;

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_todo_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    /* The widget doesn't derive anything from the todo entity's state
     * string (the "count of open items" attribute is kept in sync by our
     * own item fetches).  However, a state change is a strong hint that
     * we should refresh soon. */
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)instance->ctx;
    bool changed = (strncmp(ctx->last_entity_state, state->state, sizeof(ctx->last_entity_state)) != 0) ||
                   (ctx->last_entity_state_changed_unix_ms != state->last_changed_unix_ms);
    snprintf(ctx->last_entity_state, sizeof(ctx->last_entity_state), "%s", state->state);
    ctx->last_entity_state_changed_unix_ms = state->last_changed_unix_ms;

    /* ui_runtime periodically reapplies all widget states during global
     * model reconcile. Ignore those no-op replays and only accelerate a
     * refresh when the todo entity itself actually changed. */
    if (!changed) {
        return;
    }

    /* Preserve the deliberate startup grace until the first full list
     * snapshot has been fetched. */
    if (ctx->last_fetch_unix_ms <= 0) {
        return;
    }

    int64_t target_ms = w_todo_now_ms() + (w_todo_is_visible(ctx) ? 250 : 0);
    if (ctx->next_fetch_unix_ms <= 0 || ctx->next_fetch_unix_ms > target_ms) {
        ctx->next_fetch_unix_ms = target_ms;
    }
}

void w_todo_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    ctx->display_item_count = 0;
    if (w_todo_is_visible(ctx)) {
        w_todo_render(ctx);
        w_todo_update_status_label(ctx);
    }
}
