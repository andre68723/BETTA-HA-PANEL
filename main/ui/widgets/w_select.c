/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Select tile (select_tile) for select.* and input_select.* entities, e.g. an evcc charge mode:
 * the title, the current option large and a chevron. A tap opens the shared option picker
 * (ui_option_picker.c) with the entity's "options"; picking one calls select_option. The tile
 * shows the new option only when Home Assistant reports it, so a rejected call leaves the old one.
 * State handling after kruzio's w_select.c (same widget API); the in-tile dropdown is replaced by
 * the popup list, which is easier to hit on the 4-inch panel.
 */
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ha/ha_client.h"
#include "ha/ha_select_options.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_option_picker.h"

static const char *TAG = "w_select";

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    char current[HA_SELECT_OPTION_LEN];
    ha_select_options_t options;
    bool unavailable;
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *chevron;
    lv_obj_t *picker;
} w_select_ctx_t;

static void select_layout(w_select_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 14;
    const lv_coord_t chevron_w = 30;
    const lv_font_t *value_font = h >= 110 ? ui_font_role(UI_TEXT_VALUE) : ui_font_role(UI_TEXT_META);
    lv_obj_set_style_text_font(ctx->value_label, value_font, LV_PART_MAIN);
    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, ui_font_role(UI_TEXT_META));
    const lv_coord_t value_h = lv_font_get_line_height(value_font);
    lv_coord_t text_w = w - 2 * pad - chevron_w;
    if (text_w < 40) text_w = 40;
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title_label, 4) + value_h)) / 2;
    lv_obj_set_size(ctx->title_label, text_w, title_h);
    lv_obj_set_pos(ctx->title_label, pad, top);
    lv_obj_set_size(ctx->value_label, text_w, value_h);
    lv_obj_set_pos(ctx->value_label, pad, top + title_h + ui_widget_name_gap(ctx->title_label, 4));
    lv_obj_align(ctx->chevron, LV_ALIGN_RIGHT_MID, -pad + 4, 0);
}

static void select_render(w_select_ctx_t *ctx)
{
    const lv_color_t muted = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    lv_label_set_text(ctx->value_label, ctx->unavailable ? ui_i18n_get("common.unavailable", "unavailable")
                                                         : (ctx->current[0] ? ctx->current : "--"));
    lv_obj_set_style_text_color(ctx->value_label, ctx->unavailable ? muted : lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY),
                                LV_PART_MAIN);
    /* No options to offer (none, or dropped with oversized attributes): no chevron, and a tap
     * does nothing, instead of an empty picker. */
    lv_obj_set_hidden(ctx->chevron, ctx->unavailable || ctx->options.count == 0);
}

static void select_picker_label(void *user, size_t index, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", ((w_select_ctx_t *)user)->options.option[index]);
}

static void select_picker_chosen(void *user, size_t index)
{
    w_select_ctx_t *ctx = (w_select_ctx_t *)user;
    if (index >= ctx->options.count || strcmp(ctx->options.option[index], ctx->current) == 0) {
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + HA_SELECT_OPTION_LEN + 48];
    if (!ha_select_option_payload(ctx->entity_id, ctx->options.option[index], payload, sizeof(payload))) {
        return;
    }
    esp_err_t err = ha_client_call_service(ha_select_service_domain(ctx->entity_id), "select_option", payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "select_option failed (%s): %s", ctx->entity_id, esp_err_to_name(err));
    }
}

static void select_picker_closed(void *user)
{
    ((w_select_ctx_t *)user)->picker = NULL;
}

static void select_clicked_cb(lv_event_t *event)
{
    w_select_ctx_t *ctx = (w_select_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable || ctx->options.count == 0 || ctx->picker != NULL) {
        return;
    }
    const int current = ha_select_options_find(&ctx->options, ctx->current);
    const ui_option_picker_cfg_t cfg = {
        .title = ctx->title,
        .count = ctx->options.count,
        .current = current >= 0 ? (size_t)current : UI_OPTION_PICKER_NONE,
        .label = select_picker_label,
        .chosen = select_picker_chosen,
        .closed = select_picker_closed,
        .user = ctx,
    };
    ctx->picker = ui_option_picker_open(&cfg);
}

static void select_event_cb(lv_event_t *event)
{
    w_select_ctx_t *ctx = (w_select_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->picker != NULL) {
            ui_option_picker_close(ctx->picker); /* clears ctx->picker through the closed callback */
        }
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        select_layout(ctx);
    }
}

esp_err_t w_select_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_select_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->title, def->title[0] ? def->title : def->entity_id, sizeof(ctx->title));
    ctx->options.missing = true;

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
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(ctx->title_label, ctx->title);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

    ctx->value_label = lv_label_create(card);
    lv_label_set_text(ctx->value_label, "--");
    lv_label_set_long_mode(ctx->value_label, LV_LABEL_LONG_DOT);

    ctx->chevron = lv_label_create(card);
    lv_label_set_text(ctx->chevron, APP_SYMBOL_CHEVRON_DOWN);
    lv_obj_set_style_text_font(ctx->chevron, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->chevron, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

    lv_obj_add_event_cb(card, select_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, select_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, select_clicked_cb, LV_EVENT_CLICKED, ctx);
    select_layout(ctx);
    select_render(ctx);
    return ESP_OK;
}

void w_select_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_select_ctx_t *ctx = (w_select_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    strlcpy(ctx->current, state->state, sizeof(ctx->current));
    ha_select_options_parse(state->attributes_json, &ctx->options);
    select_render(ctx);
}

void w_select_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_select_ctx_t *ctx = (w_select_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    select_render(ctx);
}
