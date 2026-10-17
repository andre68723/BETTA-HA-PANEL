/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Date/time tile. The popup edits local calendar fields and sends one HA service call on Save;
 * the tile itself changes only after a state update from Home Assistant.
 */
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ha/ha_client.h"
#include "ha/ha_datetime_value.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"

static const char *TAG = "w_datetime";

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    ha_datetime_value_t value;
    bool unavailable;
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *popup;
    lv_obj_t *year_roller;
    lv_obj_t *month_roller;
    lv_obj_t *day_roller;
    lv_obj_t *hour_roller;
    lv_obj_t *minute_roller;
    lv_obj_t *second_roller;
    lv_obj_t *error_label;
    int first_year;
} w_datetime_ctx_t;

static void datetime_layout(w_datetime_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    int w = lv_obj_get_width(ctx->card);
    int h = lv_obj_get_height(ctx->card);
    const lv_font_t *font = w >= 220 && h >= 100 ? ui_font_role(UI_TEXT_VALUE) : ui_font_role(UI_TEXT_META);
    const int title_h = ui_widget_name_height(ctx->title_label, ui_font_role(UI_TEXT_META));
    const int value_h = lv_font_get_line_height(font);
    int text_w = w - 24;
    if (text_w < 40) text_w = 40;
    int y = (h - title_h - value_h - ui_widget_name_gap(ctx->title_label, 6)) / 2;
    if (y < 4) y = 4;
    lv_obj_set_size(ctx->title_label, text_w, title_h);
    lv_obj_set_pos(ctx->title_label, 12, y);
    lv_obj_set_style_text_font(ctx->value_label, font, LV_PART_MAIN);
    lv_obj_set_size(ctx->value_label, text_w, value_h);
    lv_obj_set_pos(ctx->value_label, 12, y + title_h + ui_widget_name_gap(ctx->title_label, 6));
}

static void datetime_render(w_datetime_ctx_t *ctx)
{
    char text[48] = "--";
    if (!ctx->unavailable) {
        (void)ha_datetime_format(&ctx->value, ui_i18n_get_language(), text, sizeof(text));
    }
    lv_label_set_text(ctx->value_label, ctx->unavailable ? ui_i18n_get("common.unavailable", "unavailable") : text);
    lv_obj_set_style_text_color(ctx->value_label,
        lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED : APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
}

static void popup_deleted(lv_event_t *event)
{
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)lv_event_get_user_data(event);
    ctx->popup = NULL;
    ctx->year_roller = NULL;
    ctx->month_roller = NULL;
    ctx->day_roller = NULL;
    ctx->hour_roller = NULL;
    ctx->minute_roller = NULL;
    ctx->second_roller = NULL;
    ctx->error_label = NULL;
}

static void popup_close(lv_event_t *event)
{
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)lv_event_get_user_data(event);
    if (ctx->popup != NULL) lv_obj_delete_async(ctx->popup);
}

static void popup_backdrop(lv_event_t *event)
{
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) popup_close(event);
}

/* Large enough to read and to scroll with a finger on the 4-inch panel (roller digits 38 px, three rows). */
#define DT_ROLLER_H 190
#define DT_ROLLER_W 128
#define DT_YEAR_W 180
#define DT_ROLLER_GAP 12
#define DT_BUTTON_W 160
#define DT_BUTTON_H 60
#define DT_FONT_CAPTION ui_font_role(UI_TEXT_LABEL)

static lv_obj_t *roller(lv_obj_t *parent, int x, int y, int width, const char *options, int selected)
{
    lv_obj_t *obj = lv_roller_create(parent);
    lv_roller_set_options(obj, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(obj, 3);
    lv_obj_set_size(obj, width, DT_ROLLER_H);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, ui_font_role(UI_TEXT_KEY), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, ui_style_radius(10), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_roller_set_selected(obj, (uint32_t)selected, LV_ANIM_OFF);
    return obj;
}

static void numeric_options(char *out, size_t len, int first, int last, bool pad)
{
    size_t used = 0;
    for (int n = first; n <= last && used < len; n++) {
        int wrote = snprintf(out + used, len - used, pad ? "%s%02d" : "%s%d", n == first ? "" : "\n", n);
        if (wrote < 0 || (size_t)wrote >= len - used) break;
        used += (size_t)wrote;
    }
}

static void popup_save(lv_event_t *event)
{
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)lv_event_get_user_data(event);
    ha_datetime_value_t edited = ctx->value;
    if (edited.has_date) {
        edited.year = ctx->first_year + (int)lv_roller_get_selected(ctx->year_roller);
        edited.month = 1 + (int)lv_roller_get_selected(ctx->month_roller);
        edited.day = 1 + (int)lv_roller_get_selected(ctx->day_roller);
    }
    if (edited.has_time) {
        edited.hour = (int)lv_roller_get_selected(ctx->hour_roller);
        edited.minute = (int)lv_roller_get_selected(ctx->minute_roller);
        if (ctx->second_roller != NULL) edited.second = (int)lv_roller_get_selected(ctx->second_roller);
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 128];
    const char *domain = NULL;
    const char *service = NULL;
    if (!ha_datetime_service_payload(ctx->entity_id, &edited, &domain, &service, payload, sizeof(payload))) {
        lv_label_set_text(ctx->error_label, ui_i18n_get("datetime.invalid", "Invalid date or time"));
        return;
    }
    esp_err_t err = ha_client_call_service(domain, service, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s.%s failed for %s: %s", domain, service, ctx->entity_id, esp_err_to_name(err));
        lv_label_set_text(ctx->error_label, ui_i18n_get("datetime.failed", "Could not save"));
        return;
    }
    lv_obj_delete_async(ctx->popup);
}

static void popup_open(w_datetime_ctx_t *ctx)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen) > 0 ? lv_obj_get_width(screen) : 720;
    int screen_h = lv_obj_get_height(screen) > 0 ? lv_obj_get_height(screen) : 720;
    int card_w = screen_w - 48;
    if (card_w > 500) card_w = 500;
    /* padding + title row, then per field group a label and a roller, then the button row */
    const int groups = (ctx->value.has_date ? 1 : 0) + (ctx->value.has_time ? 1 : 0);
    int card_h = 14 + 48 + groups * (32 + DT_ROLLER_H + 10) + 14 + DT_BUTTON_H + 14;
    if (card_h > screen_h - 48) card_h = screen_h - 48;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    ctx->popup = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_center(overlay);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, popup_deleted, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(overlay, popup_backdrop, LV_EVENT_CLICKED, ctx);

    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_event_bubble(card, false);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    ui_popup_style_frame(card);
    lv_obj_set_style_radius(card, ui_style_radius(18), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ctx->title);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(title, card_w - 90, lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)));
    lv_obj_set_pos(title, 0, 0);
    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), popup_close, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    char options[800];
    int y = 48;
    if (ctx->value.has_date) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, ui_i18n_get("datetime.date", "Date"));
        lv_obj_set_style_text_font(label, DT_FONT_CAPTION, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_set_pos(label, 0, y);
        y += 32;
        numeric_options(options, sizeof(options), 1, 31, true);
        ctx->day_roller = roller(card, 0, y, DT_ROLLER_W, options, ctx->value.day - 1);
        numeric_options(options, sizeof(options), 1, 12, true);
        ctx->month_roller = roller(card, DT_ROLLER_W + DT_ROLLER_GAP, y, DT_ROLLER_W, options, ctx->value.month - 1);
        ctx->first_year = ctx->value.year - 60;
        if (ctx->first_year < 1970) ctx->first_year = 1970;
        int last_year = ctx->first_year + 120;
        if (last_year > 9999) last_year = 9999;
        numeric_options(options, sizeof(options), ctx->first_year, last_year, false);
        ctx->year_roller = roller(card, 2 * (DT_ROLLER_W + DT_ROLLER_GAP), y, DT_YEAR_W, options,
                                  ctx->value.year - ctx->first_year);
        y += DT_ROLLER_H + 10;
    }
    if (ctx->value.has_time) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, ui_i18n_get("datetime.time", "Time"));
        lv_obj_set_style_text_font(label, DT_FONT_CAPTION, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_set_pos(label, 0, y);
        y += 32;
        numeric_options(options, sizeof(options), 0, 23, true);
        ctx->hour_roller = roller(card, 0, y, DT_ROLLER_W, options, ctx->value.hour);
        numeric_options(options, sizeof(options), 0, 59, true);
        ctx->minute_roller = roller(card, DT_ROLLER_W + DT_ROLLER_GAP, y, DT_ROLLER_W, options, ctx->value.minute);
        if (ctx->value.second != 0) {
            ctx->second_roller = roller(card, 2 * (DT_ROLLER_W + DT_ROLLER_GAP), y, DT_ROLLER_W, options, ctx->value.second);
        }
    }

    ctx->error_label = lv_label_create(card);
    lv_label_set_text(ctx->error_label, "");
    lv_obj_set_style_text_font(ctx->error_label, DT_FONT_CAPTION, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->error_label, lv_color_hex(APP_UI_COLOR_ERROR), LV_PART_MAIN);
    lv_obj_align(ctx->error_label, LV_ALIGN_BOTTOM_LEFT, 0, -(DT_BUTTON_H + 8));
    lv_obj_t *save = lv_btn_create(card);
    lv_obj_set_size(save, DT_BUTTON_W, DT_BUTTON_H);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    ui_style_button_auto(save, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), lv_color_hex(APP_UI_COLOR_CONTENT_BORDER));
    lv_obj_add_event_cb(save, popup_save, LV_EVENT_CLICKED, ctx);
    lv_obj_t *save_label = lv_label_create(save);
    lv_label_set_text(save_label, ui_i18n_get("datetime.save", "Save"));
    lv_obj_set_style_text_font(save_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_center(save_label);
    lv_obj_t *cancel = lv_btn_create(card);
    lv_obj_set_size(cancel, DT_BUTTON_W, DT_BUTTON_H);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -(DT_BUTTON_W + 10), 0);
    ui_style_button_auto(cancel, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), lv_color_hex(APP_UI_COLOR_CONTENT_BORDER));
    lv_obj_add_event_cb(cancel, popup_close, LV_EVENT_CLICKED, ctx);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, ui_i18n_get("datetime.cancel", "Cancel"));
    lv_obj_set_style_text_font(cancel_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_center(cancel_label);
}

static void card_clicked(lv_event_t *event)
{
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)lv_event_get_user_data(event);
    if (!ctx->unavailable && ctx->popup == NULL) popup_open(ctx);
}

static void card_event(lv_event_t *event)
{
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) datetime_layout(ctx);
    else if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->popup != NULL) lv_obj_delete(ctx->popup);
        free(ctx);
    }
}

esp_err_t w_datetime_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) return ESP_ERR_INVALID_ARG;
    w_datetime_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) return ESP_ERR_NO_MEM;
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->title, def->title[0] ? def->title : def->entity_id, sizeof(ctx->title));
    ctx->unavailable = true;
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
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(ctx->title_label, ctx->title);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    ctx->value_label = lv_label_create(card);
    lv_label_set_long_mode(ctx->value_label, LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(card, card_event, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, card_event, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, card_clicked, LV_EVENT_CLICKED, ctx);
    datetime_layout(ctx);
    datetime_render(ctx);
    return ESP_OK;
}

void w_datetime_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) return;
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) return;
    ha_datetime_value_t parsed;
    ctx->unavailable = !ha_datetime_parse(ctx->entity_id, state->state, state->attributes_json, &parsed);
    if (!ctx->unavailable) ctx->value = parsed;
    datetime_render(ctx);
}

void w_datetime_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) return;
    w_datetime_ctx_t *ctx = (w_datetime_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    datetime_render(ctx);
}
