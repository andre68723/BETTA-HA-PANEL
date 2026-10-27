/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "popup_contract_regression.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ui/ui_popup.h"
#include "ui/ui_conn_popup.h"
#include "ui/ui_energy_timeline_popup.h"
#include "ui/ui_widget_factory.h"
#include "ui/widgets/weather_tile_internal.h"
#include "host_adapters.h"
#include "ha/ha_auth_state.h"
#include "ui/ui_i18n.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/fonts/app_symbols.h"

static unsigned checks, cards;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "popup contract line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static double linear(double x) { x /= 255.0; return x <= .04045 ? x / 12.92 : pow((x + .055) / 1.055, 2.4); }
static double luminance(lv_color_t c) { return .2126 * linear(c.red) + .7152 * linear(c.green) + .0722 * linear(c.blue); }
static double contrast(lv_color_t a, lv_color_t b)
{
    const double x = luminance(a), y = luminance(b);
    return (fmax(x, y) + .05) / (fmin(x, y) + .05);
}
static bool inside(lv_obj_t *object, lv_obj_t *parent)
{
    lv_area_t a, b; lv_obj_get_coords(object, &a); lv_obj_get_coords(parent, &b);
    return a.x1 >= b.x1 - 1 && a.y1 >= b.y1 - 1 && a.x2 <= b.x2 + 1 && a.y2 <= b.y2 + 1;
}
static void labels(lv_obj_t *object, lv_obj_t *card, bool scrolling, lv_color_t background)
{
    if (lv_obj_is_hidden(object)) return;
    const lv_opa_t opacity = lv_obj_get_style_bg_opa(object, LV_PART_MAIN);
    if (opacity > LV_OPA_TRANSP)
        background = lv_color_mix(lv_obj_get_style_bg_color(object, LV_PART_MAIN), background, opacity);
    scrolling |= lv_obj_is_scrollable(object);
    if (lv_obj_check_type(object, &lv_label_class) && lv_label_get_text(object)[0]) {
        const double ratio = contrast(lv_obj_get_style_text_color(object, LV_PART_MAIN), background);
        /* Near-invisible-text guard, not a complete accessibility assessment. */
        if (ratio < 1.5) fprintf(stderr, "popup unreadable label [%s], contrast %.2f\n", lv_label_get_text(object), ratio);
        CHECK(ratio >= 1.5);
        CHECK(lv_obj_get_width(object) > 0 && lv_obj_get_height(object) > 0);
        if (ui_text_role(object) == UI_TEXT_VALUE && ui_text_is_essential(object) &&
            lv_label_get_long_mode(object) == LV_LABEL_LONG_CLIP) {
            lv_point_t extent;
            lv_text_get_size(&extent, lv_label_get_text(object), lv_obj_get_style_text_font(object, LV_PART_MAIN),
                             lv_obj_get_style_text_letter_space(object, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            CHECK(extent.x <= lv_obj_get_content_width(object));
        }
        if (!scrolling) {
            if (!inside(object, card)) fprintf(stderr, "popup outside card: [%s]\n", lv_label_get_text(object));
            CHECK(inside(object, card));
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) labels(lv_obj_get_child(object, i), card, scrolling, background);
}
static void visit(lv_obj_t *object)
{
    if (lv_obj_is_hidden(object)) return;
    if (lv_obj_get_user_flag(object, UI_POPUP_CARD_FLAG_INDEX)) {
        cards++;
        CHECK(inside(object, lv_layer_top()));
        CHECK(lv_obj_get_style_bg_opa(object, LV_PART_MAIN) == LV_OPA_COVER);
        CHECK(lv_color_eq(lv_obj_get_style_text_color(object, LV_PART_MAIN), theme_default_color_text_primary()));
        labels(object, object, false, lv_obj_get_style_bg_color(object, LV_PART_MAIN));
    } else {
        for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) visit(lv_obj_get_child(object, i));
    }
}
void sim_popup_contract_check(void)
{
    if (lv_obj_get_child_count(lv_layer_top()) == 0) return;
    lv_obj_update_layout(lv_layer_top());
    visit(lv_layer_top());
}
static void close_card(lv_event_t *event) { lv_obj_delete(lv_event_get_user_data(event)); }
static lv_obj_t *find_close(lv_obj_t *object)
{
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), APP_SYMBOL_CLOSE))
        return lv_obj_get_parent(object);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) {
        lv_obj_t *found = find_close(lv_obj_get_child(object, i)); if (found != NULL) return found;
    }
    return NULL;
}
static void close_current(void)
{
    lv_obj_t *close = find_close(lv_layer_top()); CHECK(close != NULL);
    lv_obj_send_event(close, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    if (lv_obj_get_child_count(lv_layer_top()) != 0) fprintf(stderr, "popup close left %u top-layer objects\n", lv_obj_get_child_count(lv_layer_top()));
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
static void additional_dialogs(void)
{
    const int original_step = ui_fonts_user_step();
    ui_fonts_set_user_step(2);
    ui_widget_def_t def = {.w = 600, .h = 400};
    strcpy(def.type, "room_tile"); strcpy(def.entity_id, "sensor.popup_temperature");
    strcpy(def.title, "Room details");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    sim_popup_contract_check();
    lv_obj_t *card = lv_obj_get_child(lv_obj_get_child(lv_layer_top(), 0), 0);
    lv_obj_t *list = lv_obj_get_child(card, 2);
    lv_obj_t *row_name = lv_obj_get_child(lv_obj_get_child(list, 0), 0);
    CHECK(lv_obj_check_type(row_name, &lv_label_class));
    CHECK(lv_obj_get_height(row_name) == lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    close_current(); lv_obj_delete(widget.obj);
    ui_fonts_set_user_step(original_step);

    memset(&def, 0, sizeof(def)); def.w = 600; def.h = 400;
    strcpy(def.type, "weather_3day"); strcpy(def.entity_id, "weather.popup_test");
    strcpy(def.secondary_entity_id, "sensor.popup_report");
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0}; strcpy(state.entity_id, def.entity_id); strcpy(state.state, "sunny");
    strcpy(state.attributes_json, "{\"temperature\":21,\"temperature_unit\":\"°C\",\"humidity\":50}");
    ui_widget_factory_apply_state(&widget, &state);
    strcpy(state.entity_id, def.secondary_entity_id); strcpy(state.state, "Synthetic weather report");
    strcpy(state.attributes_json, "{\"s\":\"Synthetic weather report\",\"d\":{\"0\":\"Cloudy\",\"1\":\"Cloudy\",\"2\":\"Cloudy\",\"3\":\"Cloudy\",\"4\":\"Cloudy\",\"5\":\"Cloudy\",\"6\":\"Cloudy\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    w_weather_tile_ctx_t *ctx = widget.ctx;
    CHECK(ctx->report_label != NULL);
    lv_obj_send_event(ctx->report_label, LV_EVENT_CLICKED, NULL);
    CHECK(ctx->popup != NULL); sim_popup_contract_check(); close_current();
    CHECK(ctx->cols[0].box != NULL);
    lv_obj_send_event(ctx->cols[0].box, LV_EVENT_CLICKED, NULL);
    CHECK(ctx->popup != NULL); sim_popup_contract_check(); close_current();
    lv_obj_delete(widget.obj);

    ui_energy_timeline_popup_info_t info = {0}; strcpy(info.title, "Energy details");
    ha_energy_timeline_snapshot_t *snapshot = calloc(1, sizeof(*snapshot)); CHECK(snapshot != NULL);
    ui_energy_timeline_popup_open(&info, snapshot);
    sim_popup_contract_check(); close_current(); free(snapshot);
}

static lv_obj_t *find_text(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_text(lv_obj_get_child(root, i), text);
        if (found) return found;
    }
    return NULL;
}

static void reconnect_dialogs(void (*advance)(uint32_t))
{
    const bool online = ha_client_is_connected();
    char original_language[APP_UI_LANGUAGE_MAX_LEN];
    snprintf(original_language, sizeof(original_language), "%s", ui_i18n_get_language());
    const char *languages[] = {"en", "de"};
    const char *themes[] = {"dark_v2", "light"};
    for (size_t language = 0; language < 2; language++) {
        CHECK(ui_i18n_init(languages[language]) == ESP_OK);
        for (int step = 0; step <= 2; step += 2) {
            ui_fonts_set_user_step(step);
            for (size_t i = 0; i < 2; i++) {
                sim_host_set_online(false);
                ui_conn_popup_set_api_status(false, false);
                CHECK(theme_palette_activate_by_id(themes[i]) == ESP_OK);
                ha_auth_state_reset();
                CHECK(ha_auth_state_report_http(403, false, false));
                ui_conn_popup_show_ha();
                sim_popup_contract_check();
                lv_obj_t *label = find_text(lv_layer_top(), ui_i18n_get("conn.reconnect", "Reconnect"));
                CHECK(label != NULL);
                lv_obj_t *button = lv_obj_get_parent(label);
                CHECK(!lv_obj_has_state(button, LV_STATE_DISABLED));
                lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
                CHECK(lv_obj_has_state(button, LV_STATE_DISABLED));
                CHECK(ha_auth_state_blocked()); /* the UI never clears the guard itself */
                CHECK(ha_auth_state_take_retry());
                lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
                CHECK(!ha_auth_state_take_retry());
                ha_auth_state_begin_retry(); /* the HA task has stopped the old transport */
                CHECK(!ha_auth_state_blocked());
                sim_popup_contract_check();
                sim_host_set_online(true);
                ui_conn_popup_set_api_status(true, true);
                advance(2100);
                CHECK(lv_obj_has_state(button, LV_STATE_DISABLED));
                CHECK(strcmp(lv_label_get_text(label), ui_i18n_get("conn.reconnect", "Reconnect")) == 0);
                sim_popup_contract_check();
                close_current();
            }
        }
    }
    ha_auth_state_reset();
    sim_host_set_online(online);
    ui_conn_popup_set_api_status(online, online);
    CHECK(ui_i18n_init(original_language) == ESP_OK);
}

void sim_popup_contract_regression(void (*advance)(uint32_t))
{
    const ui_style_kind_t original = ui_style_get();
    const int original_step = ui_fonts_user_step();
    char original_theme[32]; snprintf(original_theme, sizeof(original_theme), "%s", theme_palette_active_id());
    const char *themes[] = {"classic_v1", "dark_v2", "light", "ocean", "contrast", "black"};
    const ui_style_kind_t styles[] = {UI_STYLE_FLAT, UI_STYLE_GLOW, UI_STYLE_GLASS, UI_STYLE_MATTE};
    const ui_popup_label_role_t roles[] = {UI_POPUP_LABEL_TITLE, UI_POPUP_LABEL_BODY, UI_POPUP_LABEL_CAPTION,
                                         UI_POPUP_LABEL_HINT, UI_POPUP_LABEL_VALUE};
    for (int step = 0; step <= 2; step += 2) {
        ui_fonts_set_user_step(step);
        for (size_t t = 0; t < 6; t++) {
            CHECK(theme_palette_activate_by_id(themes[t]) == ESP_OK);
            for (size_t s = 0; s < 4; s++) {
                ui_style_set(styles[s]);
                const uint32_t before = lv_obj_get_child_count(lv_layer_top());
                lv_obj_t *card = ui_popup_card_create(lv_layer_top());
                lv_obj_set_size(card, lv_obj_get_width(lv_layer_top()) - 32, lv_obj_get_height(lv_layer_top()) - 48);
                lv_obj_center(card); lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
                int y = 0;
                for (size_t i = 0; i < 5; i++) {
                    lv_obj_t *text = ui_popup_label_create(card, "Forecast 24.5 kWh", roles[i]);
                    lv_obj_set_pos(text, 0, y);
                    y += lv_obj_get_style_text_font(text, LV_PART_MAIN)->line_height + 8;
                }
                lv_obj_t *fallback = lv_label_create(card); lv_label_set_text(fallback, "Unstyled legacy label");
                lv_obj_set_pos(fallback, 0, y);
                CHECK(lv_color_eq(lv_obj_get_style_text_color(fallback, LV_PART_MAIN), theme_default_color_text_primary()));
                CHECK(lv_obj_get_style_text_font(fallback, LV_PART_MAIN) == ui_font_role(UI_TEXT_LABEL));
                lv_obj_t *choice = lv_button_create(card);
                lv_obj_set_size(choice, 240, UI_POPUP_CLOSE_SIZE);
                lv_obj_set_pos(choice, 0, y + ui_font_role(UI_TEXT_LABEL)->line_height + 8);
                lv_obj_t *choice_text = ui_popup_label_create(choice, "Selected", UI_POPUP_LABEL_BODY);
                ui_style_action_button(choice, choice_text, UI_CONTROL_SELECTED, true);
                lv_obj_center(choice_text);
                lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), close_card, card);
                lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
                sim_popup_contract_check();
                lv_obj_send_event(close, LV_EVENT_CLICKED, NULL);
                CHECK(lv_obj_get_child_count(lv_layer_top()) == before);
            }
        }
    }
    ui_fonts_set_user_step(original_step);
    CHECK(theme_palette_activate_by_id(original_theme) == ESP_OK); ui_style_set(original);
    void (*connections[])(void) = {ui_conn_popup_show_wifi, ui_conn_popup_show_ha, ui_conn_popup_show_mqtt};
    for (size_t i = 0; i < 3; i++) {
        connections[i](); sim_popup_contract_check();
        close_current();
    }
    additional_dialogs();
    reconnect_dialogs(advance);
    ui_fonts_set_user_step(original_step);
    CHECK(theme_palette_activate_by_id(original_theme) == ESP_OK); ui_style_set(original);
    CHECK(cards >= 55);
    printf("popup contract: %u checks, %u card inspections\n", checks, cards);
}
