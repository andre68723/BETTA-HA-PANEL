/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ui/ui_widget_factory.h"
#include "ui/theme/ui_style.h"
#include "ui/fonts/app_symbols.h"
#include "ui/ui_option_picker.h"
#include "ui/ui_pin_pad.h"
#include "popup_contract_regression.h"

esp_err_t w_light_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_light_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_light_tile_mark_unavailable(ui_widget_instance_t *);
void w_light_tile_set_visible(ui_widget_instance_t *, bool);
esp_err_t w_slider_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_slider_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_slider_mark_unavailable(ui_widget_instance_t *);
esp_err_t w_vacuum_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_vacuum_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_vacuum_mark_unavailable(ui_widget_instance_t *);
void w_vacuum_set_visible(ui_widget_instance_t *, bool);

static const char *phase;
static unsigned checks;
static uint32_t now;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Controls regression line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint32_t tick(void) { return now; }
static void weather_delay(uint32_t ms) { now += ms; lv_timer_handler(); }
static void advance(void) { now += 100; lv_timer_handler(); sim_popup_contract_check(); }
static lv_obj_t *label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char *shown = lv_label_get_text(root);
        const size_t n = strlen(shown);
        if (strcmp(shown, text) == 0 || (n > 3 && strcmp(shown + n - 3, "...") == 0 &&
            strncmp(shown, text, n - 3) == 0)) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}
static void dump_labels(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_label_class)) fprintf(stderr, "Label: [%s]\n", lv_label_get_text(root));
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) dump_labels(lv_obj_get_child(root, i));
}
static void click(lv_obj_t *root, const char *text)
{
    lv_obj_t *found = label(root, text);
    if (found == NULL) { fprintf(stderr, "Missing control label: %s (%s)\n", text, phase); dump_labels(root); }
    CHECK(found != NULL);
    lv_obj_t *button = lv_obj_get_parent(found);
    CHECK(lv_obj_check_type(button, &lv_button_class));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    advance();
}
static void sent(const char *domain, const char *service, const char *entity, const char *key, const char *value)
{
    const sim_service_calls_t *calls = sim_host_service_calls();
    CHECK(calls->count == 1 && strcmp(calls->domain, domain) == 0 && strcmp(calls->service, service) == 0);
    cJSON *data = cJSON_Parse(calls->data);
    CHECK(data != NULL);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "entity_id")), entity) == 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, key)), value) == 0);
    CHECK(cJSON_GetArraySize(data) == 2);
    cJSON_Delete(data);
}
static void apply(ui_widget_instance_t *widget, bool light, const char *entity, const char *value, const char *attrs)
{
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", entity);
    snprintf(state.state, sizeof(state.state), "%s", value);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "%s", attrs);
    sim_host_state_add(&state);
    if (light) w_light_tile_apply_state(widget, &state); else w_vacuum_apply_state(widget, &state);
    lv_obj_update_layout(widget->obj);
}
static bool same_color(lv_color_t a, lv_color_t b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}
static void semantic_controls_test(void)
{
    const ui_style_kind_t original_style = ui_style_get();
    char original_theme[32];
    snprintf(original_theme, sizeof(original_theme), "%s", theme_palette_active_id());
    const char *themes[] = {"classic_v1", "dark_v2", "light", "ocean", "contrast", "black"};
    const ui_style_kind_t styles[] = {UI_STYLE_FLAT, UI_STYLE_GLOW, UI_STYLE_GLASS, UI_STYLE_MATTE};
    for (size_t t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
        CHECK(theme_palette_activate_by_id(themes[t]) == ESP_OK);
        for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
            ui_style_set(styles[i]);
            lv_obj_t *btn = lv_button_create(lv_screen_active());
            lv_obj_t *text = lv_label_create(btn);
            ui_style_action_button(btn, text, UI_CONTROL_ACTIVE, true);
            const lv_color_t active = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
            ui_style_action_button(btn, text, UI_CONTROL_SELECTED, true);
            CHECK(same_color(active, lv_obj_get_style_bg_color(btn, LV_PART_MAIN)));
            ui_style_action_button(btn, text, UI_CONTROL_PRIMARY, true);
            CHECK(same_color(active, lv_obj_get_style_bg_color(btn, LV_PART_MAIN)));
            ui_style_action_button(btn, text, UI_CONTROL_ACTION, true);
            const lv_color_t idle = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
            CHECK(!same_color(active, idle));
            ui_style_action_button(btn, text, UI_CONTROL_PRIMARY, false);
            CHECK(same_color(idle, lv_obj_get_style_bg_color(btn, LV_PART_MAIN)));
            CHECK(lv_obj_has_state(btn, LV_STATE_DISABLED));
            ui_style_action_button(btn, text, UI_CONTROL_ACTION, true);
            CHECK(!lv_obj_has_state(btn, LV_STATE_DISABLED));
            lv_obj_delete(btn);
        }
    }
    CHECK(theme_palette_activate_by_id(original_theme) == ESP_OK);
    ui_style_set(original_style);
}
static void process_primary_test(void)
{
    ui_widget_def_t def = {.w = 300, .h = 300};
    snprintf(def.type, sizeof(def.type), "media_player");
    snprintf(def.entity_id, sizeof(def.entity_id), "media_player.semantic");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", def.entity_id);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"supported_features\":65535}");
    snprintf(state.state, sizeof(state.state), "playing");
    ui_widget_factory_apply_state(&widget, &state);
    lv_obj_t *pause = label(widget.obj, APP_SYMBOL_PAUSE);
    CHECK(pause != NULL);
    const lv_color_t running = lv_obj_get_style_bg_color(lv_obj_get_parent(pause), LV_PART_MAIN);
    lv_obj_t *next = label(widget.obj, APP_SYMBOL_NEXT);
    CHECK(next != NULL);
    const lv_color_t secondary = lv_obj_get_style_bg_color(lv_obj_get_parent(next), LV_PART_MAIN);
    CHECK(!same_color(running, secondary));
    const char *states[] = {"paused", "idle", "off", "unavailable", "paused", "playing"};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        snprintf(state.state, sizeof(state.state), "%s", states[i]);
        ui_widget_factory_apply_state(&widget, &state);
        lv_obj_t *symbol = label(widget.obj, strcmp(states[i], "playing") == 0 ? APP_SYMBOL_PAUSE : APP_SYMBOL_PLAY);
        CHECK(symbol != NULL);
        const bool available = strcmp(states[i], "unavailable") != 0;
        CHECK(same_color(running, lv_obj_get_style_bg_color(lv_obj_get_parent(symbol), LV_PART_MAIN)) == available);
        CHECK((lv_obj_get_style_opa(lv_obj_get_parent(symbol), LV_PART_MAIN) == LV_OPA_COVER) == available);
        next = label(widget.obj, APP_SYMBOL_NEXT);
        CHECK(next != NULL);
        CHECK(same_color(secondary, lv_obj_get_style_bg_color(lv_obj_get_parent(next), LV_PART_MAIN)));
    }
    lv_obj_delete(widget.obj);
}

static void fan_selection_test(void)
{
    ui_widget_def_t def = {.w = 300, .h = 300};
    snprintf(def.type, sizeof(def.type), "fan_tile");
    snprintf(def.entity_id, sizeof(def.entity_id), "fan.semantic");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", def.entity_id);
    snprintf(state.state, sizeof(state.state), "off");
    snprintf(state.attributes_json, sizeof(state.attributes_json),
        "{\"percentage\":50,\"percentage_step\":50,\"preset_mode\":\"Auto\",\"preset_modes\":[\"Auto\",\"Sleep\"]}");
    ui_widget_factory_apply_state(&widget, &state);
    lv_obj_t *selected = label(widget.obj, "Auto"), *other = label(widget.obj, "Sleep");
    CHECK(selected != NULL && other != NULL);
    CHECK(!same_color(lv_obj_get_style_bg_color(lv_obj_get_parent(selected), LV_PART_MAIN),
                      lv_obj_get_style_bg_color(lv_obj_get_parent(other), LV_PART_MAIN)));
    snprintf(state.state, sizeof(state.state), "unavailable");
    ui_widget_factory_apply_state(&widget, &state);
    CHECK(lv_obj_has_state(lv_obj_get_parent(selected), LV_STATE_DISABLED));
    CHECK(same_color(lv_obj_get_style_bg_color(lv_obj_get_parent(selected), LV_PART_MAIN),
                     lv_obj_get_style_bg_color(lv_obj_get_parent(other), LV_PART_MAIN)));
    lv_obj_delete(widget.obj);
}

static void audit_state(ui_widget_instance_t *widget, const char *id, const char *raw, const char *attrs)
{
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", id);
    snprintf(state.state, sizeof(state.state), "%s", raw);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "%s", attrs);
    sim_host_state_add(&state);
    ui_widget_factory_apply_state(widget, &state);
    lv_obj_update_layout(widget->obj);
}
static void option_text(void *user, size_t i, char *out, size_t len)
{
    (void)user;
    snprintf(out, len, "%s", i == 0 ? "Off" : "On");
}
static void picker_roles_test(void)
{
    ui_option_picker_cfg_t cfg = {.title = "Selection", .count = 2, .current = 0, .label = option_text};
    lv_obj_t *picker = ui_option_picker_open(&cfg);
    CHECK(picker != NULL);
    sim_popup_contract_check();
    lv_obj_t *off = label(picker, "Off"), *on = label(picker, "On");
    CHECK(off != NULL && on != NULL);
    CHECK(!same_color(lv_obj_get_style_bg_color(lv_obj_get_parent(off), LV_PART_MAIN),
                      lv_obj_get_style_bg_color(lv_obj_get_parent(on), LV_PART_MAIN)));
    ui_option_picker_close(picker);
    ui_pin_pad_cfg_t pin = {.title = "Code"};
    lv_obj_t *pad = ui_pin_pad_open(&pin);
    CHECK(pad != NULL);
    sim_popup_contract_check();
    lv_obj_t *ok = label(pad, "OK"), *one = label(pad, "1");
    CHECK(ok != NULL && one != NULL);
    CHECK(same_color(lv_obj_get_style_bg_color(lv_obj_get_parent(ok), LV_PART_MAIN),
                     lv_obj_get_style_bg_color(lv_obj_get_parent(one), LV_PART_MAIN)));
    ui_pin_pad_close(pad);
}
static void stale_controls_test(void)
{
    /* An open selector and a queued target change must not outlive availability. */
    ui_widget_def_t def = {.w = 320, .h = 300};
    snprintf(def.type, sizeof(def.type), "climate_tile");
    snprintf(def.entity_id, sizeof(def.entity_id), "climate.audit");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *attrs = "{\"temperature\":22,\"hvac_modes\":[\"off\",\"heat\"]}";
    audit_state(&widget, def.entity_id, "heat", attrs);
    click(widget.obj, "Heat");
    CHECK(lv_obj_get_child_count(lv_layer_top()) > 0);
    audit_state(&widget, def.entity_id, "unavailable", attrs);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_t *minus = label(widget.obj, APP_SYMBOL_MINUS);
    CHECK(minus != NULL && lv_obj_has_state(lv_obj_get_parent(minus), LV_STATE_DISABLED));
    audit_state(&widget, def.entity_id, "off", attrs);
    CHECK(!lv_obj_has_state(lv_obj_get_parent(minus), LV_STATE_DISABLED));
    lv_obj_delete(widget.obj);

    snprintf(def.type, sizeof(def.type), "datetime_tile");
    snprintf(def.entity_id, sizeof(def.entity_id), "time.audit");
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    audit_state(&widget, def.entity_id, "06:30:00", "{}");
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) > 0);
    audit_state(&widget, def.entity_id, "unavailable", "{}");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_delete(widget.obj);

    snprintf(def.type, sizeof(def.type), "heating_tile");
    snprintf(def.entity_id, sizeof(def.entity_id), "climate.heating_audit");
    snprintf(def.style_variant, sizeof(def.style_variant), "arc_semi");
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    audit_state(&widget, def.entity_id, "heat", attrs);
    minus = label(widget.obj, "-");
    CHECK(minus != NULL && !lv_obj_has_state(lv_obj_get_parent(minus), LV_STATE_DISABLED));
    audit_state(&widget, def.entity_id, "unknown", attrs);
    CHECK(lv_obj_has_state(lv_obj_get_parent(minus), LV_STATE_DISABLED));
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(!ui_style_card_is_active(widget.obj));
    audit_state(&widget, def.entity_id, "off", attrs);
    CHECK(!lv_obj_has_state(lv_obj_get_parent(minus), LV_STATE_DISABLED));
    lv_obj_delete(widget.obj);

    /* A trigger's unknown timestamp is valid; a missing/unavailable trigger is not. */
    snprintf(def.type, sizeof(def.type), "scene_tile");
    snprintf(def.entity_id, sizeof(def.entity_id), "scene.audit");
    def.style_variant[0] = '\0';
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(sim_host_service_calls()->count == 0);
    audit_state(&widget, def.entity_id, "unknown", "{}");
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(sim_host_service_calls()->count == 1);
    audit_state(&widget, def.entity_id, "unavailable", "{}");
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(sim_host_service_calls()->count == 0);
    lv_obj_delete(widget.obj);
}

static void light_test(void)
{
    ui_widget_def_t def = {.w = 240, .h = 240};
    snprintf(def.entity_id, sizeof(def.entity_id), "light.test");
    ui_widget_instance_t widget = {0};
    CHECK(w_light_tile_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *effects = "{\"supported_color_modes\":[\"onoff\"],\"effect\":\"None\",\"effect_list\":[\"None\",\"A\\\"B\\\\C\",\"Rainbow\"]}";
    apply(&widget, true, "light.test", "off", effects);
    lv_obj_t *button = lv_obj_get_child(widget.obj, 5);
    CHECK(!lv_obj_is_hidden(button));
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    click(lv_layer_top(), "A\"B\\C");
    sent("light", "turn_on", "light.test", "effect", "A\"B\\C");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    /* A stale picker is dismissed when capabilities change. */
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    apply(&widget, true, "light.test", "on", "{\"supported_color_modes\":[\"onoff\"]}");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && lv_obj_is_hidden(button));
    /* Color lights keep their controls and offer effects below their presets. */
    const char *color = "{\"supported_color_modes\":[\"rgb\"],\"rgb_color\":[255,0,0],\"effect\":\"None\",\"effect_list\":[\"None\",\"Rainbow\"]}";
    apply(&widget, true, "light.test", "on", color);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    click(lv_layer_top(), "Effect: None");
    sim_host_service_reset(ESP_FAIL);
    click(lv_layer_top(), "Rainbow");
    CHECK(label(lv_layer_top(), "Effect: None") != NULL);
    sim_host_service_reset(ESP_OK);
    click(lv_layer_top(), "Effect: None");
    click(lv_layer_top(), "Rainbow");
    sent("light", "turn_on", "light.test", "effect", "Rainbow");
    CHECK(label(lv_layer_top(), "Effect: Rainbow") != NULL);
    click(lv_layer_top(), "Effect: Rainbow");
    w_light_tile_set_visible(&widget, false);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    w_light_tile_mark_unavailable(&widget);
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_host_service_calls()->count == 0);
    apply(&widget, true, "light.test", "on", effects);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    sim_host_set_online(false);
    click(lv_layer_top(), "Rainbow");
    CHECK(sim_host_service_calls()->count == 0);
    sim_host_set_online(true);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    lv_obj_delete(widget.obj);
    advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
static void vacuum_test(void)
{
    ui_widget_def_t def = {.w = 240, .h = 220};
    snprintf(def.entity_id, sizeof(def.entity_id), "vacuum.test");
    snprintf(def.tertiary_entity_id, sizeof(def.tertiary_entity_id), "select.test_mode");
    ui_widget_instance_t widget = {0};
    CHECK(w_vacuum_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *attrs = "{\"supported_features\":40,\"fan_speed\":\"Normal\",\"fan_speed_list\":[\"Normal\",\"Turbo\"]}";
    apply(&widget, false, "vacuum.test", "docked", attrs);
    apply(&widget, false, "select.test_mode", "Auto", "{\"options\":[\"Auto\",\"Mop\"]}");
    lv_obj_t *primary = lv_obj_get_parent(label(widget.obj, APP_SYMBOL_PLAY));
    CHECK(primary != NULL);
    const lv_color_t idle = lv_obj_get_style_bg_color(primary, LV_PART_MAIN);
    CHECK(same_color(idle, lv_obj_get_style_bg_color(lv_obj_get_parent(label(widget.obj, "Auto")), LV_PART_MAIN)));
    apply(&widget, false, "vacuum.test", "cleaning", attrs);
    CHECK(label(primary, APP_SYMBOL_PAUSE) != NULL);
    CHECK(!same_color(idle, lv_obj_get_style_bg_color(primary, LV_PART_MAIN)));
    apply(&widget, false, "vacuum.test", "paused", attrs);
    CHECK(same_color(idle, lv_obj_get_style_bg_color(primary, LV_PART_MAIN)));
    apply(&widget, false, "vacuum.test", "returning", attrs);
    CHECK(!same_color(idle, lv_obj_get_style_bg_color(primary, LV_PART_MAIN)));
    /* A Home press that cannot be sent says so instead of doing nothing; the next state replaces the notice. */
    apply(&widget, false, "vacuum.test", "cleaning", attrs);
    sim_host_service_reset(ESP_ERR_INVALID_STATE);
    phase = "vacuum home not sent";
    click(widget.obj, APP_SYMBOL_HOME);
    CHECK(label(widget.obj, "Home Assistant not connected") != NULL);
    CHECK(label(widget.obj, "Returning") == NULL);
    apply(&widget, false, "vacuum.test", "cleaning", attrs);
    CHECK(label(widget.obj, "Home Assistant not connected") == NULL);
    apply(&widget, false, "vacuum.test", "docked", attrs);
    sim_host_service_reset(ESP_OK);
    phase = "vacuum initial fan";
    click(widget.obj, "Normal");
    click(lv_layer_top(), "Turbo");
    sent("vacuum", "set_fan_speed", "vacuum.test", "fan_speed", "Turbo");
    CHECK(label(widget.obj, "Turbo") != NULL);
    /* The separate cleaning-mode entity still receives its own action. */
    sim_host_service_reset(ESP_OK);
    phase = "vacuum mode";
    click(widget.obj, "Auto");
    click(lv_layer_top(), "Mop");
    sent("select", "select_option", "select.test_mode", "option", "Mop");
    apply(&widget, false, "vacuum.test", "docked", attrs);
    sim_host_service_reset(ESP_FAIL);
    click(widget.obj, "Normal"); click(lv_layer_top(), "Turbo");
    CHECK(label(widget.obj, "Normal") != NULL);
    click(widget.obj, "Normal");
    w_vacuum_set_visible(&widget, false);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    click(widget.obj, "Normal");
    w_vacuum_mark_unavailable(&widget);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    sim_host_service_reset(ESP_OK);
    click(widget.obj, "Normal");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_host_service_calls()->count == 0);
    apply(&widget, false, "vacuum.test", "docked", "{\"supported_features\":8,\"fan_speed\":\"Normal\",\"fan_speed_list\":[\"Normal\"]}");
    CHECK(label(widget.obj, "Normal") != NULL && lv_obj_is_hidden(lv_obj_get_parent(label(widget.obj, "Normal"))));
    apply(&widget, false, "vacuum.test", "docked", attrs);
    click(widget.obj, "Normal");
    lv_obj_delete(widget.obj); advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
static void cached_favorites_test(void)
{
    ui_widget_def_t def = {.w = 400, .h = 400};
    snprintf(def.type, sizeof(def.type), "sonos_favorites");
    snprintf(def.entity_id, sizeof(def.entity_id), "media_player.synthetic");
    snprintf(def.secondary_entity_id, sizeof(def.secondary_entity_id), "sensor.synthetic_favorites");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", def.secondary_entity_id);
    snprintf(state.state, sizeof(state.state), "1");
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"items\":{\"FV:1\":\"Old station\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    CHECK(label(widget.obj, "Old station") != NULL);
    ui_widget_factory_set_visible(&widget, false);
    lv_obj_set_hidden(widget.obj, true);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"items\":{\"FV:2\":\"New station\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    ui_widget_factory_set_visible(&widget, true);
    lv_obj_set_hidden(widget.obj, false);
    CHECK(label(widget.obj, "New station") != NULL && label(widget.obj, "Old station") == NULL);
    lv_obj_delete(widget.obj);
}

static lv_obj_t *find_slider(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_slider_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_slider(lv_obj_get_child(root, i));
        if (found != NULL) return found;
    }
    return NULL;
}
/* A finger on the slider: press, move to `value`, release. */
static void drag_slider(lv_obj_t *slider, int value)
{
    lv_obj_send_event(slider, LV_EVENT_PRESSED, NULL);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(slider, LV_EVENT_RELEASED, NULL);
    advance();
}
static void slider_unavailable_test(void)
{
    lv_screen_load(lv_obj_create(NULL));
    ui_widget_def_t def = {.w = 360, .h = 240};
    strcpy(def.type, "slider"); strcpy(def.entity_id, "light.slider_test"); strcpy(def.title, "Slider");
    ui_widget_instance_t widget = {0};
    CHECK(w_slider_create(&def, lv_screen_active(), &widget) == ESP_OK);
    lv_obj_t *slider = find_slider(widget.obj);
    CHECK(slider != NULL);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "light.slider_test");
    snprintf(state.state, sizeof(state.state), "on");
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"brightness_pct\":40}");
    w_slider_apply_state(&widget, &state);
    advance();
    const unsigned before = sim_host_slider_sets();
    CHECK(!lv_obj_has_state(slider, LV_STATE_DISABLED));
    drag_slider(slider, 70);
    CHECK(sim_host_slider_sets() == before + 1 && sim_host_slider_last() == 70);

    /* unavailable: no send, no value shown, the slider is disabled and the state stays unavailable */
    snprintf(state.state, sizeof(state.state), "unavailable");
    state.attributes_json[0] = '\0';
    w_slider_apply_state(&widget, &state);
    advance();
    CHECK(lv_obj_has_state(slider, LV_STATE_DISABLED));
    drag_slider(slider, 30);
    CHECK(sim_host_slider_sets() == before + 1);
    CHECK(lv_slider_get_value(slider) == 0 && label(widget.obj, "unavailable") != NULL);

    /* going unavailable in the middle of a drag drops the rest of that drag */
    snprintf(state.state, sizeof(state.state), "on");
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"brightness_pct\":40}");
    w_slider_apply_state(&widget, &state);
    advance();
    CHECK(!lv_obj_has_state(slider, LV_STATE_DISABLED) && lv_slider_get_value(slider) == 40);
    lv_obj_send_event(slider, LV_EVENT_PRESSED, NULL);
    w_slider_mark_unavailable(&widget);
    lv_slider_set_value(slider, 90, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_send_event(slider, LV_EVENT_RELEASED, NULL);
    advance();
    CHECK(sim_host_slider_sets() == before + 1 && lv_slider_get_value(slider) == 0 && label(widget.obj, "unavailable") != NULL);

    /* the entity is back: the slider works again and the stale 90 % is not replayed */
    w_slider_apply_state(&widget, &state);
    advance();
    CHECK(!lv_obj_has_state(slider, LV_STATE_DISABLED));
    drag_slider(slider, 55);
    CHECK(sim_host_slider_sets() == before + 2 && sim_host_slider_last() == 55);
}

int sim_controls_regression(void)
{
    now = lv_tick_get(); lv_tick_set_cb(tick);
    sim_host_set_online(true);
    phase = "semantic roles"; semantic_controls_test();
    phase = "popup contracts"; sim_popup_contract_regression(weather_delay);
    void sim_weather_now_regression(void (*delay)(uint32_t));
    sim_weather_now_regression(weather_delay);
    phase = "media primary"; process_primary_test();
    phase = "fan selection"; fan_selection_test();
    phase = "picker roles"; picker_roles_test();
    phase = "unavailable actions"; stale_controls_test();
    phase = "light"; light_test(); phase = "vacuum"; vacuum_test();
    phase = "cached favorites"; cached_favorites_test();
    phase = "slider unavailable"; slider_unavailable_test();
    /* A fixture can contain more entities than widget slots. Keep the firmware model's capacity. */
    ha_state_t capacity_state = {0};
    snprintf(capacity_state.state, sizeof(capacity_state.state), "1");
    for (int i = 0; i <= APP_MAX_WIDGETS_TOTAL; i++) {
        snprintf(capacity_state.entity_id, sizeof(capacity_state.entity_id), "sensor.capacity_%d", i);
        sim_host_state_add(&capacity_state);
    }
    CHECK(ha_model_get_state(capacity_state.entity_id, NULL));
    printf("controls regression: %u checks passed\n", checks);
    int sim_camera_regression(void);
    sim_host_service_reset(ESP_OK);
    return sim_camera_regression();
}
