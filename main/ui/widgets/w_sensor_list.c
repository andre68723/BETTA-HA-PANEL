/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ui/widgets/w_sensor_list.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ui/ui_memory.h"
#include "ui/text_font.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_typography.h"

void w_sensor_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_sensor_mark_unavailable(ui_widget_instance_t *instance);

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *list;
    int count;
    ui_widget_instance_t row[UI_SENSOR_LIST_MAX];
} sensor_list_ctx_t;

static void list_layout(sensor_list_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const int width = lv_obj_get_content_width(ctx->card);
    const int height = lv_obj_get_content_height(ctx->card);
    const int top = lv_obj_is_hidden(ctx->title) ? 0 : lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL)) + 6;
    lv_obj_set_pos(ctx->title, 0, 0);
    lv_obj_set_size(ctx->title, width, top > 0 ? top - 6 : 1);
    lv_obj_set_pos(ctx->list, 0, top);
    lv_obj_set_size(ctx->list, width > 0 ? width : 1, height > top ? height - top : 1);
}

static void list_event(lv_event_t *event)
{
    sensor_list_ctx_t *ctx = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) free(ctx);
    else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) list_layout(ctx);
}

esp_err_t w_sensor_list_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out)
{
    sensor_list_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    ui_widget_def_t *row_def = ui_calloc_prefer_psram(1, sizeof(*row_def));
    if (ctx == NULL || row_def == NULL) { free(ctx); free(row_def); return ESP_ERR_NO_MEM; }
    ctx->card = lv_obj_create(parent);
    lv_obj_set_pos(ctx->card, def->x, def->y);
    lv_obj_set_size(ctx->card, def->w, def->h);
    theme_default_style_card(ctx->card);
    lv_obj_set_scrollable(ctx->card, false);
    lv_obj_set_style_pad_all(ctx->card, 10, LV_PART_MAIN);
    ctx->title = ui_widget_name_create(ctx->card, !def->hide_name);
    ui_text_set(ctx->title, UI_TEXT_LABEL, false);
    lv_obj_set_style_text_color(ctx->title, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->id);
    ctx->list = lv_obj_create(ctx->card);
    lv_obj_remove_style_all(ctx->list);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->list, 6, LV_PART_MAIN);
    lv_obj_set_scroll_dir(ctx->list, LV_DIR_VER);
    lv_obj_set_scrollable(ctx->list, true);
    lv_obj_add_event_cb(ctx->card, list_event, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(ctx->card, list_event, LV_EVENT_SIZE_CHANGED, ctx);
    list_layout(ctx);
    *row_def = *def;
    row_def->display_mode[0] = '\0';
    row_def->sensor_format[0] = '\0';
    row_def->sensor_unit[0] = '\0';
    row_def->hide_name = false; /* Rows always identify their entity, independently of the widget heading. */
    row_def->x = row_def->y = 0;
    row_def->w = lv_obj_get_content_width(ctx->list);
    row_def->h = 80;
    for (int i = 0; i < UI_SENSOR_LIST_MAX; i++) {
        const char *id = i == 0 ? def->entity_id : def->extra_entity_id[i - 1];
        if (id[0] == '\0') continue;
        bool duplicate = false;
        for (int r = 0; r < ctx->count; r++) duplicate |= strcmp(id, ctx->row[r].entity_id) == 0;
        if (duplicate) continue;
        snprintf(row_def->entity_id, sizeof(row_def->entity_id), "%s", id);
        if (!ha_model_get_entity_name(id, row_def->title, sizeof(row_def->title)))
            snprintf(row_def->title, sizeof(row_def->title), "%.*s", (int)sizeof(row_def->title) - 1, id);
        ui_widget_instance_t *row = &ctx->row[ctx->count];
        snprintf(row->entity_id, sizeof(row->entity_id), "%s", id);
        const esp_err_t err = w_sensor_row_create(row_def, ctx->list, row, ctx->card);
        if (err != ESP_OK) { free(row_def); lv_obj_delete(ctx->card); return err; }
        w_sensor_mark_unavailable(row);
        ctx->count++;
    }
    free(row_def);
    out->obj = ctx->card;
    out->ctx = ctx;
    return ESP_OK;
}

void w_sensor_list_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    sensor_list_ctx_t *ctx = instance->ctx;
    if (ctx == NULL) return;
    for (int r = 0; r < ctx->count; r++) {
        if (strcmp(ctx->row[r].entity_id, state->entity_id) == 0) w_sensor_apply_state(&ctx->row[r], state);
    }
}

void w_sensor_list_mark_unavailable(ui_widget_instance_t *instance)
{
    sensor_list_ctx_t *ctx = instance->ctx;
    if (ctx == NULL) return;
    for (int r = 0; r < ctx->count; r++) w_sensor_mark_unavailable(&ctx->row[r]);
}
