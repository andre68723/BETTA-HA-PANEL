/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Todo list widget.  Displays the items of a HA `todo.*` entity as a
 * scrollable list of checkboxes.  Items are pulled on demand via the
 * `todo.get_items` service call (return_response=true) and periodically
 * refreshed.  Ticking / unticking a box fires `todo.update_item`.
 *
 * Adding or removing items is intentionally NOT supported from the panel –
 * that is expected to be driven via HA voice assist.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
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
#define W_TODO_REFRESH_PERIOD_MS 60000
#define W_TODO_REFRESH_AFTER_UPDATE_MS 800

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
    char entity_id[APP_MAX_ENTITY_ID_LEN];

    /* Displayed items (accessed from LVGL context only). */
    w_todo_item_t display_items[W_TODO_MAX_ITEMS];
    size_t display_item_count;
    bool unavailable;
    bool fetching;
    int64_t last_fetch_unix_ms;
    int64_t next_fetch_unix_ms;

    /* Staging slot filled by the WS response callback, drained by the
     * refresh timer which runs in LVGL task context. */
    SemaphoreHandle_t staging_mutex;
    w_todo_item_t pending_items[W_TODO_MAX_ITEMS];
    size_t pending_item_count;
    bool pending_valid;
    bool pending_failure;

    lv_timer_t *tick_timer;
    bool destroyed;
} w_todo_ctx_t;

static int64_t w_todo_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void w_todo_request_items(w_todo_ctx_t *ctx);

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
    w_todo_item_t *parsed = calloc(W_TODO_MAX_ITEMS, sizeof(*parsed));
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

    /* Sort: open (needs_action) first, completed at the end.  HA's
     * `todo.get_items` may return completed items first which, combined with
     * a limited card height, hides the active ones below the fold.  Stable
     * insertion sort keeps HA's relative order within each group. */
    if (parsed_count > 1) {
        for (size_t i = 1; i < parsed_count; i++) {
            if (!parsed[i].completed) {
                size_t j = i;
                while (j > 0 && parsed[j - 1].completed) {
                    w_todo_item_t tmp = parsed[j - 1];
                    parsed[j - 1] = parsed[j];
                    parsed[j] = tmp;
                    j--;
                }
            }
        }
    }

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    if (!ctx->destroyed) {
        memcpy(ctx->pending_items, parsed,
            sizeof(ctx->pending_items[0]) * W_TODO_MAX_ITEMS);
        ctx->pending_item_count = parsed_count;
        ctx->pending_valid = success && any_items_found;
        ctx->pending_failure = !success;
    }
    xSemaphoreGive(ctx->staging_mutex);
    free(parsed);
}

/* ----- UI rendering (LVGL context only) -------------------------------- */
static void w_todo_clear_list(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return;
    }
    lv_obj_clean(ctx->list_container);
}

static void w_todo_checkbox_event_cb(lv_event_t *e);

static void w_todo_render(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->list_container == NULL) {
        return;
    }

    w_todo_clear_list(ctx);

    if (ctx->unavailable) {
        lv_obj_t *lbl = lv_label_create(ctx->list_container);
        lv_label_set_text(lbl, ui_i18n_get("common.unavailable", "unavailable"));
        lv_obj_set_style_text_color(lbl, theme_default_color_text_muted(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, APP_FONT_TEXT_20, LV_PART_MAIN);
        return;
    }

    if (ctx->display_item_count == 0) {
        lv_obj_t *lbl = lv_label_create(ctx->list_container);
        const char *txt = ctx->last_fetch_unix_ms > 0
            ? ui_i18n_get("todo.empty", "no open items")
            : ui_i18n_get("todo.loading", "loading...");
        lv_label_set_text(lbl, txt);
        lv_obj_set_style_text_color(lbl, theme_default_color_text_muted(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, APP_FONT_TEXT_20, LV_PART_MAIN);
        return;
    }

    for (size_t i = 0; i < ctx->display_item_count; i++) {
        const w_todo_item_t *it = &ctx->display_items[i];
        lv_obj_t *cb = lv_checkbox_create(ctx->list_container);
        lv_checkbox_set_text(cb, it->summary[0] != '\0' ? it->summary : it->uid);
        if (it->completed) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_set_style_text_font(cb, APP_FONT_TEXT_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(cb, theme_default_color_text_primary(), LV_PART_MAIN);
        if (it->completed) {
            lv_obj_set_style_text_decor(cb, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
            lv_obj_set_style_text_color(cb, theme_default_color_text_muted(), LV_PART_MAIN);
        }
        lv_obj_set_width(cb, LV_PCT(100));
        /* Store item index via user data. */
        lv_obj_add_event_cb(cb, w_todo_checkbox_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_set_user_data(cb, (void *)(uintptr_t)(i + 1));
    }
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
    char buf[24];
    snprintf(buf, sizeof(buf), "%u", (unsigned)open);
    lv_label_set_text(ctx->status_label, buf);
}

static void w_todo_drain_pending(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }
    bool have_update = false;
    bool failure = false;
    w_todo_item_t snapshot[W_TODO_MAX_ITEMS];
    size_t snapshot_count = 0;

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    if (ctx->pending_valid || ctx->pending_failure) {
        memcpy(snapshot, ctx->pending_items, sizeof(snapshot));
        snapshot_count = ctx->pending_item_count;
        have_update = ctx->pending_valid;
        failure = ctx->pending_failure;
        ctx->pending_valid = false;
        ctx->pending_failure = false;
        ctx->pending_item_count = 0;
    }
    xSemaphoreGive(ctx->staging_mutex);

    if (!have_update && !failure) {
        return;
    }

    ctx->fetching = false;
    ctx->last_fetch_unix_ms = w_todo_now_ms();

    if (failure) {
        ctx->unavailable = true;
        ctx->display_item_count = 0;
        w_todo_render(ctx);
        w_todo_update_status_label(ctx);
        return;
    }

    ctx->unavailable = false;
    memcpy(ctx->display_items, snapshot, sizeof(snapshot));
    ctx->display_item_count = snapshot_count;
    w_todo_render(ctx);
    w_todo_update_status_label(ctx);
}

/* ----- Request issuing -------------------------------------------------- */
static void w_todo_request_items(w_todo_ctx_t *ctx)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->fetching) {
        return;
    }

    /* Don't pile a heavy get_items roundtrip on top of the initial layout
     * subscribe burst — that's what triggers the TLS BAD_INPUT_DATA on WS.
     * Wait until the client reports initial sync done, then add a small
     * grace so the follow-up state_changed bursts have drained too. */
    if (!ha_client_is_connected() || !ha_client_is_initial_sync_done()) {
        ctx->next_fetch_unix_ms = w_todo_now_ms() + 1500;
        return;
    }

    ctx->fetching = true;
    ctx->next_fetch_unix_ms = w_todo_now_ms() + W_TODO_REFRESH_PERIOD_MS;

    ESP_LOGI(W_TODO_TAG, "Requesting todo.get_items for %s", ctx->entity_id);
    esp_err_t err = ha_client_call_service_with_response(
        "todo", "get_items", ctx->entity_id, NULL, w_todo_response_cb, ctx);
    if (err != ESP_OK) {
        ctx->fetching = false;
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

/* ----- Checkbox toggle handler ----------------------------------------- */
static void w_todo_checkbox_event_cb(lv_event_t *e)
{
    if (e == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *cb = lv_event_get_target(e);
    if (ctx == NULL || cb == NULL) {
        return;
    }
    uintptr_t idx1 = (uintptr_t)lv_obj_get_user_data(cb);
    if (idx1 == 0 || idx1 > ctx->display_item_count) {
        return;
    }
    size_t idx = (size_t)(idx1 - 1);
    w_todo_item_t *it = &ctx->display_items[idx];
    if (it->uid[0] == '\0') {
        return;
    }

    bool new_completed = lv_obj_has_state(cb, LV_STATE_CHECKED);

    /* Optimistic local update for immediate feedback. */
    it->completed = new_completed;
    if (new_completed) {
        lv_obj_set_style_text_decor(cb, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
        lv_obj_set_style_text_color(cb, theme_default_color_text_muted(), LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_decor(cb, LV_TEXT_DECOR_NONE, LV_PART_MAIN);
        lv_obj_set_style_text_color(cb, theme_default_color_text_primary(), LV_PART_MAIN);
    }
    w_todo_update_status_label(ctx);

    /* Build payload: { "entity_id": "...", "item": "<uid>", "status": "needs_action"|"completed" }
     * HA todo.update_item also accepts `rename`, but we only toggle status. */
    char payload[384];
    snprintf(payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"item\":\"%s\",\"status\":\"%s\"}",
        ctx->entity_id, it->uid, new_completed ? "completed" : "needs_action");

    esp_err_t err = ha_client_call_service("todo", "update_item", payload);
    if (err != ESP_OK) {
        ESP_LOGW(W_TODO_TAG, "update_item failed (%s): %s",
            ctx->entity_id, esp_err_to_name(err));
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

    w_todo_drain_pending(ctx);

    int64_t now = w_todo_now_ms();
    if (!ctx->fetching && now >= ctx->next_fetch_unix_ms) {
        w_todo_request_items(ctx);
    }

    /* If a fetch has been outstanding for >12 s, clear the flag so the next
     * tick can retry.  The registry itself times out at 15 s. */
    if (ctx->fetching && ctx->last_fetch_unix_ms > 0 &&
        (now - ctx->last_fetch_unix_ms) > 15000) {
        ctx->fetching = false;
        ctx->next_fetch_unix_ms = now + 2000;
    }
}

/* ----- Event / lifecycle ------------------------------------------------ */
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
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 4, LV_PART_MAIN);

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
    lv_obj_set_style_text_color(status, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_font(status, APP_FONT_TEXT_18, LV_PART_MAIN);

    /* Scrollable list container. */
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    w_todo_ctx_t *ctx = calloc(1, sizeof(w_todo_ctx_t));
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

    /* Initial placeholder render. */
    w_todo_render(ctx);

    /* Periodic tick drives both refresh and response draining.  1 Hz keeps
     * it cheap and responsive enough. */
    ctx->tick_timer = lv_timer_create(w_todo_tick_cb, 1000, ctx);

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
    ctx->next_fetch_unix_ms = w_todo_now_ms() + 250;
}

void w_todo_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_todo_ctx_t *ctx = (w_todo_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    ctx->display_item_count = 0;
    w_todo_render(ctx);
    w_todo_update_status_label(ctx);
}
