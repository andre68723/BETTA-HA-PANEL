/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

static void *weather_calloc(size_t count, size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return calloc(count, size);
}

lv_coord_t weather_condition_text_width(lv_obj_t *card)
{
    if (card == NULL) {
        return 0;
    }
    lv_coord_t width = lv_obj_get_width(card) - 32;
    return (width > 0) ? width : 0;
}

lv_coord_t weather_card_min_dim(lv_obj_t *card)
{
    if (card == NULL) {
        return 0;
    }
    lv_obj_update_layout(card);
    lv_coord_t w = lv_obj_get_width(card);
    lv_coord_t h = lv_obj_get_height(card);

    if (w <= 0) {
        w = lv_obj_get_style_width(card, LV_PART_MAIN);
    }
    if (h <= 0) {
        h = lv_obj_get_style_height(card, LV_PART_MAIN);
    }

    if (w <= 0 || h <= 0) {
        return 0;
    }
    return (w < h) ? w : h;
}

lv_coord_t weather_effective_min_dim(lv_obj_t *card, const w_weather_tile_ctx_t *ctx)
{
    lv_coord_t min_dim = weather_card_min_dim(card);
    if (min_dim > 0) {
        return min_dim;
    }
    if (ctx != NULL && ctx->configured_min_dim > 0) {
        return ctx->configured_min_dim;
    }
    return 0;
}

const lv_font_t *weather_pick_temp_font(lv_obj_t *card)
{
    lv_coord_t min_dim = weather_card_min_dim(card);
    if (min_dim >= 300) {
        return WEATHER_TEMP_FONT_LARGE;
    }
    return WEATHER_TEMP_FONT;
}

const lv_font_t *weather_pick_meta_font(lv_obj_t *card)
{
    lv_coord_t min_dim = weather_card_min_dim(card);
    if (min_dim >= 300) {
        return WEATHER_META_FONT_LARGE;
    }
    return WEATHER_CONDITION_FONT;
}

static void w_weather_tile_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }
    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx != NULL) {
        if (ctx->popup != NULL) {
            lv_obj_del(ctx->popup); /* its delete callback clears ctx->popup */
        }
        weather_hourly_stop(ctx);
        weather_free_lottie(ctx);
        free(ctx);
    }
}

esp_err_t w_weather_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    #if UI_TEXT_ADAPTIVE
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
#else
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
#endif

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(title, def->title[0] ? def->title : def->id);
    lv_obj_set_width(title, def->w - 32);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    const lv_font_t *title_font = strcmp(def->type, "weather_3day") == 0
        ? ui_font_role(UI_TEXT_LABEL) : ui_font_role(UI_TEXT_META);
    lv_obj_set_style_text_font(title, title_font, LV_PART_MAIN);
    ui_text_record(title, UI_TEXT_LABEL, false);
    lv_obj_set_height(title, title_font->line_height);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *condition = lv_label_create(card);
    lv_label_set_text(condition, "--");
    lv_obj_set_width(condition, def->w - 32);
    lv_obj_set_style_text_align(condition, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(condition, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
    lv_obj_set_style_text_font(condition, WEATHER_CONDITION_FONT, LV_PART_MAIN);
    lv_obj_align(condition, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *temp = lv_label_create(card);
    lv_label_set_text(temp, "--");
    lv_obj_set_width(temp, def->w - 32);
    lv_obj_set_style_text_align(temp, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(temp, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(temp, WEATHER_TEMP_FONT, LV_PART_MAIN);
    lv_obj_align(temp, LV_ALIGN_TOP_MID, 0, 76);

    lv_obj_t *meta = lv_label_create(card);
    lv_label_set_text(meta, "");
    lv_obj_set_width(meta, def->w - 32);
    lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(meta, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_align(meta, LV_ALIGN_TOP_MID, 0, 130);

    w_weather_tile_ctx_t *ctx = weather_calloc(1, sizeof(w_weather_tile_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    ctx->card = card;
    ctx->show_hourly = def->weather_hourly && strcmp(def->type, "weather_tile") == 0;
    ctx->hide_name = def->hide_name;
    ctx->show_forecast = (strcmp(def->type, "weather_3day") == 0);
    ctx->show_precip = def->show_precipitation;
    ctx->forecast_days = def->forecast_days;
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    if (strcmp(def->type, "weather_3day") == 0 || strcmp(def->type, "weather_tile") == 0) {
        snprintf(ctx->report_entity_id, sizeof(ctx->report_entity_id), "%s", def->secondary_entity_id);
    }
    ctx->condition_label = condition;
    ctx->temp_label = temp;
    ctx->meta_label = meta;
    ctx->lottie_icon = NULL;
    ctx->lottie_buf = NULL;
    ctx->lottie_buf_size = 0U;
    ctx->lottie_size = 0;
    ctx->last_lottie_src = NULL;
    ctx->last_lottie_src_size = 0U;
    ctx->configured_min_dim = (def->w < def->h) ? def->w : def->h;
    ctx->last_icon_cp = 0U;
    ctx->last_icon_font = NULL;
    ctx->last_condition_key[0] = '\0';
    ctx->last_condition_text[0] = '\0';

    if (ctx->show_forecast) {
        for (int i = 0; i < WEATHER_3DAY_ROWS; i++) {
            weather_3day_row_widgets_t *row = &ctx->rows[i];
            row->container = lv_obj_create(card);
            lv_obj_set_scrollable(row->container, false);
            lv_obj_set_style_bg_opa(row->container, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row->container, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row->container, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(row->container, 0, LV_PART_MAIN);

            row->day_label = lv_label_create(row->container);
            lv_label_set_text(row->day_label, "--");
            lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            lv_label_set_long_mode(row->day_label, LV_LABEL_LONG_CLIP);

            row->icon_label = lv_label_create(row->container);
            lv_label_set_text(row->icon_label, "-");
            lv_obj_set_style_text_align(row->icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_label_set_long_mode(row->icon_label, LV_LABEL_LONG_CLIP);

            row->low_label = lv_label_create(row->container);
            lv_label_set_text(row->low_label, "--");
            lv_obj_set_style_text_align(row->low_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

            row->bar_track = lv_obj_create(row->container);
            lv_obj_set_scrollable(row->bar_track, false);
            lv_obj_set_style_pad_all(row->bar_track, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(row->bar_track, 0, LV_PART_MAIN);

            row->bar_fill = lv_obj_create(row->bar_track);
            lv_obj_set_scrollable(row->bar_fill, false);
            lv_obj_set_style_pad_all(row->bar_fill, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(row->bar_fill, 0, LV_PART_MAIN);

            row->bar_marker = lv_obj_create(row->bar_track);
            lv_obj_set_scrollable(row->bar_marker, false);
            lv_obj_set_style_pad_all(row->bar_marker, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(row->bar_marker, 2, LV_PART_MAIN);
            lv_obj_set_hidden(row->bar_marker, true);

            row->high_label = lv_label_create(row->container);
            lv_label_set_text(row->high_label, "--");
            lv_obj_set_style_text_align(row->high_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        }
    }

#if APP_UI_WEATHER_LOTTIE_ENABLED
    lv_obj_t *lottie_icon = lv_lottie_create(card);
    lv_obj_set_style_bg_opa(lottie_icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lottie_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lottie_icon, 0, LV_PART_MAIN);
    lv_obj_set_hidden(lottie_icon, true);
    lv_obj_set_scrollable(lottie_icon, false);
    ctx->lottie_icon = lottie_icon;
#endif

    lv_obj_add_event_cb(card, w_weather_tile_event_cb, LV_EVENT_DELETE, ctx);
    if (!ctx->show_forecast) {
        lv_obj_set_clickable(card, true);
        lv_obj_add_event_cb(card, weather_now_clicked_cb, LV_EVENT_CLICKED, ctx);
    }
    if (ctx->show_hourly) weather_hourly_start(ctx);
    out_instance->ctx = ctx;
    out_instance->obj = card;
    weather_render(card, ctx, NULL, false);
    return ESP_OK;
}

void w_weather_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }

    /* Weather tile is driven by its primary weather entity only.
     * Secondary entity updates (if configured) must not override icon/condition rendering. */
    if (strncmp(state->entity_id, instance->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        /* The optional DWD weather report sensor only feeds the summary text. */
        w_weather_tile_ctx_t *report_ctx = (w_weather_tile_ctx_t *)instance->ctx;
        if (report_ctx != NULL && report_ctx->report_entity_id[0] != '\0' &&
            strncmp(state->entity_id, report_ctx->report_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            weather_report_parse_state(report_ctx, state);
            weather_report_update_label(report_ctx);
        }
        return;
    }

    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    if (!state->state[0] || !strcmp(state->state, "unknown") || !strcmp(state->state, "unavailable")) {
        weather_render(instance->obj, ctx, NULL, false);
        return;
    }

    /* Deterministic icon behavior:
     * As soon as a weather condition maps to an icon, keep showing that icon
     * until a new valid weather condition arrives. */
    weather_update_icon_cache_from_state(ctx, state->state);

    weather_values_t *values = weather_calloc(1, sizeof(*values));
    if (values == NULL) {
        weather_render(instance->obj, ctx, NULL, false);
        return;
    }

    weather_extract_values(state, true, values);

    if (ctx->last_condition_text[0] == '\0' && weather_has_alpha(values->condition)) {
        weather_copy_text(ctx->last_condition_text, sizeof(ctx->last_condition_text), values->condition);
    }
    weather_render(instance->obj, ctx, values, true);
    free(values);
}

void w_weather_tile_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }
    w_weather_tile_ctx_t *ctx = (w_weather_tile_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }
    weather_render(instance->obj, ctx, NULL, false);
}

#ifdef BETTA_SIMULATOR
/* Simulator service replies stay outside the firmware's bounded state-attribute buffer. */
void w_weather_tile_simulator_set_hourly(ui_widget_instance_t *instance, const cJSON *forecast)
{
    w_weather_tile_ctx_t *ctx = instance->ctx;
    if (ctx == NULL || !ctx->show_hourly || !cJSON_IsArray(forecast)) return;
    cJSON *result = cJSON_CreateObject();
    cJSON *response = cJSON_AddObjectToObject(result, "response");
    cJSON *entity = cJSON_AddObjectToObject(response, ctx->entity_id);
    cJSON_AddItemReferenceToObject(entity, "forecast", (cJSON *)forecast);
    weather_hourly_simulator_reply(ctx->hourly_token, result);
    cJSON_Delete(result);
}
#endif
