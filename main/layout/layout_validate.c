/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_alarm_state.h"
#include "ha/ha_sensor_value.h"
#include "layout/layout_validate.h"

#include "layout/bambu_printer_slots.h"
#include "layout/evcc_slots.h"
#include "layout/energy_timeline_colors.h"
#include "util/chime_synth.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "app_config.h"
#include "layout/widget_types.h"
#include "layout/layout_orientation.h"
#include "util/entity_id.h"
#include "util/room_extra.h"

#define GRAPH_POINT_COUNT_MIN 16
#define GRAPH_POINT_COUNT_MAX 64
#define GRAPH_TIME_WINDOW_MIN_MIN 1
#define GRAPH_TIME_WINDOW_MIN_MAX 1440

static const char *const GRAPH_DISPLAY_MODES[] = {
    "line",
    "line_smooth",
    "line_smooth_points",
    "bars",
};

static const int GRAPH_BAR_BUCKET_MIN_ALLOWED[] = {5, 10, 15, 30};

static const char *const SOLAR_FORECAST_BAR_ORIENTATIONS[] = {
    "horizontal",
    "vertical",
};

/* "roborock_tile" is the legacy name of the vacuum tile. */
static bool is_vacuum_type(const char *type)
{
    return type != NULL && (strcmp(type, "vacuum_tile") == 0 || strcmp(type, "roborock_tile") == 0);
}

static bool is_valid_solar_forecast_bar_orientation(const char *value)
{
    if (value == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(SOLAR_FORECAST_BAR_ORIENTATIONS) / sizeof(SOLAR_FORECAST_BAR_ORIENTATIONS[0]); i++) {
        if (strcmp(value, SOLAR_FORECAST_BAR_ORIENTATIONS[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool str_in_list(const char *value, const void *list, size_t entry_size, size_t list_len)
{
    if (value == NULL || list == NULL || entry_size == 0) {
        return false;
    }
    const char *base = (const char *)list;
    for (size_t i = 0; i < list_len; i++) {
        const char *entry = base + (i * entry_size);
        if (strncmp(value, entry, entry_size) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_valid_entity_id(const char *entity_id)
{
    return entity_id_valid(entity_id);
}

/* The room card's extra field (a widget or a house.rooms[] entry): a known mode, and for every mode but the default an
 * entity of the right domain that already belongs to the card (its own entity_id or one of room_entities), so the field
 * never adds a subscription. Returns an error text, or NULL when the fields are fine (or absent: an old layout). */
static const char *room_extra_error(const cJSON *owner, const char *primary_id, const cJSON *extras)
{
    const cJSON *mode_json = cJSON_GetObjectItemCaseSensitive(owner, "room_extra_mode");
    const cJSON *entity_json = cJSON_GetObjectItemCaseSensitive(owner, "room_extra_entity_id");
    room_extra_mode_t mode = ROOM_EXTRA_DEFAULT;
    if (mode_json != NULL && (!cJSON_IsString(mode_json) || !room_extra_mode_parse(mode_json->valuestring, &mode))) {
        return "room_extra_mode must be default|humidity|cover|media_player";
    }
    if (mode == ROOM_EXTRA_DEFAULT) {
        return NULL;
    }
    if (!cJSON_IsString(entity_json) || entity_json->valuestring == NULL || entity_json->valuestring[0] == '\0') {
        return "room_extra_entity_id is required for this room_extra_mode";
    }
    const char *entity = entity_json->valuestring;
    if (!is_valid_entity_id(entity) || !room_extra_entity_fits(mode, entity)) {
        return "room_extra_entity_id does not fit room_extra_mode (humidity: sensor.*, cover: cover.*, media_player: media_player.*)";
    }
    bool on_card = primary_id != NULL && strcmp(primary_id, entity) == 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, extras) {
        on_card = on_card || (cJSON_IsString(item) && item->valuestring != NULL && strcmp(item->valuestring, entity) == 0);
    }
    return on_card ? NULL : "room_extra_entity_id must be the room's entity_id or one of its room_entities";
}

static bool entity_in_domain(const char *entity_id, const char *domain)
{
    if (entity_id == NULL || domain == NULL) {
        return false;
    }
    size_t domain_len = strlen(domain);
    return strncmp(entity_id, domain, domain_len) == 0 && entity_id[domain_len] == '.';
}

static bool is_supported_page_type(const char *type)
{
    if (type == NULL || type[0] == '\0') {
        return true;
    }
    return strcmp(type, "dashboard") == 0 || strcmp(type, "energy_dashboard") == 0 ||
           strcmp(type, "house_dashboard") == 0;
}

static bool widget_requires_primary_entity(const char *type)
{
    return type == NULL || strcmp(type, "empty_tile") != 0;
}

static bool is_hex_digit_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_valid_hex_rgb_color(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    const char *p = text;
    if (p[0] == '#') {
        p++;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    if (strlen(p) != 6) {
        return false;
    }

    for (size_t i = 0; i < 6; i++) {
        if (!is_hex_digit_char(p[i])) {
            return false;
        }
    }
    return true;
}

static bool is_valid_slider_direction(const char *direction)
{
    if (direction == NULL || direction[0] == '\0') {
        return false;
    }
    return strcmp(direction, "auto") == 0 || strcmp(direction, "left_to_right") == 0 ||
           strcmp(direction, "right_to_left") == 0 || strcmp(direction, "bottom_to_top") == 0 ||
           strcmp(direction, "top_to_bottom") == 0;
}

static bool is_valid_button_mode(const char *mode)
{
    if (mode == NULL || mode[0] == '\0') {
        return false;
    }
    return strcmp(mode, "auto") == 0 || strcmp(mode, "play_pause") == 0 || strcmp(mode, "stop") == 0 ||
           strcmp(mode, "next") == 0 || strcmp(mode, "previous") == 0;
}

static bool button_mode_requires_media_player(const char *mode)
{
    if (mode == NULL || mode[0] == '\0') {
        return false;
    }
    return strcmp(mode, "play_pause") == 0 || strcmp(mode, "stop") == 0 || strcmp(mode, "next") == 0 ||
           strcmp(mode, "previous") == 0;
}

static bool is_valid_energy_source(const char *source)
{
    if (source == NULL || source[0] == '\0') {
        return false;
    }
    return strcmp(source, "ha_energy") == 0 || strcmp(source, "manual_live") == 0;
}

static void validate_energy_entity_field(cJSON *energy, const char *key, const char *page_id,
    layout_validation_result_t *result)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(energy, key);
    if (item == NULL) {
        return;
    }

    char msg[128];
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        snprintf(msg, sizeof(msg), "page %s energy.%s must be a string", page_id != NULL ? page_id : "?", key);
        layout_validation_add(result, msg);
        return;
    }

    if (item->valuestring[0] == '\0') {
        return;
    }

    if (!is_valid_entity_id(item->valuestring) || !entity_in_domain(item->valuestring, "sensor")) {
        snprintf(msg, sizeof(msg), "page %s energy.%s must be sensor.*", page_id != NULL ? page_id : "?", key);
        layout_validation_add(result, msg);
    }
}

static void validate_energy_page(cJSON *page, const char *page_id, layout_validation_result_t *result)
{
    cJSON *energy = cJSON_GetObjectItemCaseSensitive(page, "energy");
    if (energy == NULL) {
        return;
    }
    if (!cJSON_IsObject(energy)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "page %s: energy must be object", page_id != NULL ? page_id : "?");
        layout_validation_add(result, msg);
        return;
    }

    cJSON *source = cJSON_GetObjectItemCaseSensitive(energy, "source");
    if (source != NULL) {
        char msg[128];
        if (!cJSON_IsString(source) || source->valuestring == NULL) {
            snprintf(msg, sizeof(msg), "page %s energy.source must be a string", page_id != NULL ? page_id : "?");
            layout_validation_add(result, msg);
        } else if (source->valuestring[0] != '\0' && !is_valid_energy_source(source->valuestring)) {
            snprintf(msg,
                sizeof(msg),
                "page %s energy.source must be ha_energy or manual_live",
                page_id != NULL ? page_id : "?");
            layout_validation_add(result, msg);
        }
    }

    cJSON *details = cJSON_GetObjectItemCaseSensitive(energy, "details");
    if (details != NULL && (!cJSON_IsString(details) || details->valuestring == NULL ||
                            (strcmp(details->valuestring, "popup") != 0 && strcmp(details->valuestring, "side") != 0))) {
        char msg[128];
        snprintf(msg, sizeof(msg), "page %s energy.details must be popup or side", page_id != NULL ? page_id : "?");
        layout_validation_add(result, msg);
    }

    static const char *keys[] = {
        "home_power_entity_id",
        "solar_power_entity_id",
        "grid_power_entity_id",
        "grid_import_power_entity_id",
        "grid_export_power_entity_id",
        "battery_power_entity_id",
        "battery_charge_power_entity_id",
        "battery_discharge_power_entity_id",
        "battery_soc_entity_id",
        "gas_entity_id",
        "water_entity_id",
    };
    for (size_t i = 0; i < (sizeof(keys) / sizeof(keys[0])); i++) {
        validate_energy_entity_field(energy, keys[i], page_id, result);
    }
}

void layout_validation_clear(layout_validation_result_t *result)
{
    if (result == NULL) {
        return;
    }
    result->count = 0;
    memset(result->messages, 0, sizeof(result->messages));
}

void layout_validation_add(layout_validation_result_t *result, const char *msg)
{
    if (result == NULL || msg == NULL) {
        return;
    }
    if (result->count >= APP_LAYOUT_MAX_ERRORS) {
        return;
    }
    snprintf(result->messages[result->count], sizeof(result->messages[result->count]), "%.*s",
             (int)sizeof(result->messages[result->count]) - 1, msg);
    result->count++;
}

static bool validate_widget(cJSON *widget, const char *known_widget_ids, size_t known_ids_len,
    size_t page_index, size_t widget_index, layout_validation_result_t *result)
{
    char msg[96];

    cJSON *id = cJSON_GetObjectItemCaseSensitive(widget, "id");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
    cJSON *entity_id = cJSON_GetObjectItemCaseSensitive(widget, "entity_id");
    cJSON *secondary_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "secondary_entity_id");
    cJSON *tertiary_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "tertiary_entity_id");
    cJSON *forecast_today_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_today_entity_id");
    cJSON *forecast_tomorrow_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_tomorrow_entity_id");
    cJSON *forecast_day_3_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_day_3_entity_id");
    cJSON *forecast_day_4_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_day_4_entity_id");
    cJSON *forecast_day_5_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_day_5_entity_id");
    cJSON *forecast_day_6_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_day_6_entity_id");
    cJSON *forecast_day_7_entity_id = cJSON_GetObjectItemCaseSensitive(widget, "forecast_day_7_entity_id");
    cJSON *solar_forecast_bar_max_kwh = cJSON_GetObjectItemCaseSensitive(widget, "solar_forecast_bar_max_kwh");
    cJSON *solar_forecast_bar_orientation = cJSON_GetObjectItemCaseSensitive(widget, "solar_forecast_bar_orientation");
    cJSON *gauge_min = cJSON_GetObjectItemCaseSensitive(widget, "gauge_min");
    cJSON *gauge_max = cJSON_GetObjectItemCaseSensitive(widget, "gauge_max");
    cJSON *gauge_bar_style = cJSON_GetObjectItemCaseSensitive(widget, "gauge_bar_style");
    cJSON *gauge_segment_count = cJSON_GetObjectItemCaseSensitive(widget, "gauge_segment_count");
    cJSON *gauge_color_mode = cJSON_GetObjectItemCaseSensitive(widget, "gauge_color_mode");
    cJSON *gauge_color_low = cJSON_GetObjectItemCaseSensitive(widget, "gauge_color_low");
    cJSON *gauge_color_high = cJSON_GetObjectItemCaseSensitive(widget, "gauge_color_high");
    cJSON *gauge_target_color = cJSON_GetObjectItemCaseSensitive(widget, "gauge_target_color");
    cJSON *gauge_target_low = cJSON_GetObjectItemCaseSensitive(widget, "gauge_target_low");
    cJSON *gauge_target_high = cJSON_GetObjectItemCaseSensitive(widget, "gauge_target_high");
    cJSON *show_name = cJSON_GetObjectItemCaseSensitive(widget, "show_name");
    cJSON *sensor_show_label = cJSON_GetObjectItemCaseSensitive(widget, "sensor_show_label");
    cJSON *sensor_show_age = cJSON_GetObjectItemCaseSensitive(widget, "sensor_show_age");
    cJSON *lock_confirm = cJSON_GetObjectItemCaseSensitive(widget, "lock_confirm");
    cJSON *alarm_actions = cJSON_GetObjectItemCaseSensitive(widget, "alarm_actions");
    cJSON *history_popup = cJSON_GetObjectItemCaseSensitive(widget, "history_popup");
    cJSON *slider_direction = cJSON_GetObjectItemCaseSensitive(widget, "slider_direction");
    cJSON *slider_accent_color = cJSON_GetObjectItemCaseSensitive(widget, "slider_accent_color");
    cJSON *button_accent_color = cJSON_GetObjectItemCaseSensitive(widget, "button_accent_color");
    cJSON *button_mode = cJSON_GetObjectItemCaseSensitive(widget, "button_mode");
    cJSON *graph_line_color = cJSON_GetObjectItemCaseSensitive(widget, "graph_line_color");
    cJSON *graph_point_count = cJSON_GetObjectItemCaseSensitive(widget, "graph_point_count");
    cJSON *graph_time_window_min = cJSON_GetObjectItemCaseSensitive(widget, "graph_time_window_min");
    cJSON *graph_display_mode = cJSON_GetObjectItemCaseSensitive(widget, "graph_display_mode");
    cJSON *graph_bar_bucket_min = cJSON_GetObjectItemCaseSensitive(widget, "graph_bar_bucket_min");
    cJSON *graph_ha_history = cJSON_GetObjectItemCaseSensitive(widget, "graph_ha_history");
    cJSON *rect = cJSON_GetObjectItemCaseSensitive(widget, "rect");

    if (graph_ha_history != NULL && !cJSON_IsBool(graph_ha_history)) {
        snprintf(msg, sizeof(msg), "widget graph_ha_history must be true or false");
        layout_validation_add(result, msg);
    }

    if (show_name != NULL && !cJSON_IsBool(show_name)) {
        snprintf(msg, sizeof(msg), "widget show_name must be true or false");
        layout_validation_add(result, msg);
    }

    if (!cJSON_IsString(id) || id->valuestring == NULL || strlen(id->valuestring) == 0U) {
        snprintf(msg, sizeof(msg), "page[%u] widget[%u]: invalid id", (unsigned)page_index, (unsigned)widget_index);
        layout_validation_add(result, msg);
    } else if (strlen(id->valuestring) >= APP_MAX_WIDGET_ID_LEN) {
        snprintf(msg, sizeof(msg), "widget id too long: %.75s", id->valuestring);
        layout_validation_add(result, msg);
    } else if (str_in_list(id->valuestring, known_widget_ids, APP_MAX_WIDGET_ID_LEN, known_ids_len)) {
        snprintf(msg, sizeof(msg), "duplicate widget id: %s", id->valuestring);
        layout_validation_add(result, msg);
    }

    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        snprintf(msg, sizeof(msg), "widget %s: missing type", cJSON_IsString(id) ? id->valuestring : "?");
        layout_validation_add(result, msg);
    } else if (!widget_type_known(type->valuestring)) {
        snprintf(msg, sizeof(msg), "widget %s: unsupported type %s", cJSON_IsString(id) ? id->valuestring : "?",
            type->valuestring);
        layout_validation_add(result, msg);
    }

    bool requires_entity = true;
    if (cJSON_IsString(type) && type->valuestring != NULL) {
        requires_entity = widget_requires_primary_entity(type->valuestring);
    }

    if (requires_entity) {
        if (!cJSON_IsString(entity_id) || !is_valid_entity_id(entity_id->valuestring)) {
            snprintf(msg, sizeof(msg), "widget %s: invalid entity_id", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }

        if (cJSON_IsString(type) && type->valuestring != NULL && cJSON_IsString(entity_id) && entity_id->valuestring != NULL) {
            if (!widget_type_entity_domain_ok(type->valuestring, entity_id->valuestring)) {
                char hint[64];
                widget_type_domain_hint(type->valuestring, hint, sizeof(hint));
                snprintf(msg, sizeof(msg), "widget %s: entity_id must be %s", cJSON_IsString(id) ? id->valuestring : "?", hint);
                layout_validation_add(result, msg);
            }
        }
    } else if (cJSON_IsString(entity_id) && entity_id->valuestring != NULL && entity_id->valuestring[0] != '\0' &&
               !is_valid_entity_id(entity_id->valuestring)) {
        snprintf(msg, sizeof(msg), "widget %s: invalid entity_id", cJSON_IsString(id) ? id->valuestring : "?");
        layout_validation_add(result, msg);
    }

    cJSON *hourly = cJSON_GetObjectItemCaseSensitive(widget, "weather_hourly");
    if (hourly != NULL && (!cJSON_IsBool(hourly) || !cJSON_IsString(type) || strcmp(type->valuestring, "weather_tile") != 0)) {
        layout_validation_add(result, "weather_hourly must be a boolean on weather_tile");
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && (strcmp(type->valuestring, "weather_3day") == 0 || strcmp(type->valuestring, "weather_tile") == 0) &&
        cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL &&
        secondary_entity_id->valuestring[0] != '\0') {
        /* Optional DWD weather report sensor. */
        if (!is_valid_entity_id(secondary_entity_id->valuestring) ||
            !entity_in_domain(secondary_entity_id->valuestring, "sensor")) {
            snprintf(msg, sizeof(msg), "widget %s: secondary_entity_id must be sensor.*",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "energy_price_forecast") == 0) {
        /* Optional average sensor and the price card's options. */
        cJSON *price_selector = cJSON_GetObjectItemCaseSensitive(widget, "price_selector");
        cJSON *price_cheap_times = cJSON_GetObjectItemCaseSensitive(widget, "price_cheap_times");
        cJSON *price_line = cJSON_GetObjectItemCaseSensitive(widget, "price_line");
        cJSON *price_average = cJSON_GetObjectItemCaseSensitive(widget, "price_average");
        if (cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL &&
            secondary_entity_id->valuestring[0] != '\0' &&
            (!is_valid_entity_id(secondary_entity_id->valuestring) || !entity_in_domain(secondary_entity_id->valuestring, "sensor"))) {
            snprintf(msg, sizeof(msg), "widget %s: secondary_entity_id must be sensor.*", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if ((price_selector != NULL && !cJSON_IsBool(price_selector)) || (price_cheap_times != NULL && !cJSON_IsBool(price_cheap_times)) ||
            (price_line != NULL && !cJSON_IsBool(price_line))) {
            snprintf(msg, sizeof(msg), "widget %s: price_selector, price_cheap_times and price_line must be true or false",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (price_average != NULL && (!cJSON_IsNumber(price_average) || price_average->valuedouble < 0.0 || price_average->valuedouble > 5.0)) {
            snprintf(msg, sizeof(msg), "widget %s: price_average must be a price in EUR/kWh between 0 and 5",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "parqet_portfolio") == 0) {
        /* Optional gain (secondary), XIRR (tertiary), dividends and return sensors, and the percentages-only option. */
        const char *keys[] = {"secondary_entity_id", "tertiary_entity_id", "dividends_entity_id", "return_entity_id"};
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            cJSON *item = cJSON_GetObjectItemCaseSensitive(widget, keys[k]);
            if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0' &&
                (!is_valid_entity_id(item->valuestring) || !entity_in_domain(item->valuestring, "sensor"))) {
                snprintf(msg, sizeof(msg), "widget %s: %s must be sensor.*", cJSON_IsString(id) ? id->valuestring : "?", keys[k]);
                layout_validation_add(result, msg);
            }
        }
        cJSON *hide = cJSON_GetObjectItemCaseSensitive(widget, "parqet_hide_amounts");
        cJSON *selector = cJSON_GetObjectItemCaseSensitive(widget, "parqet_selector");
        if ((hide != NULL && !cJSON_IsBool(hide)) || (selector != NULL && !cJSON_IsBool(selector))) {
            snprintf(msg, sizeof(msg), "widget %s: parqet_hide_amounts and parqet_selector must be true or false", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        cJSON *view = cJSON_GetObjectItemCaseSensitive(widget, "parqet_view");
        if (view != NULL && (!cJSON_IsString(view) || view->valuestring == NULL ||
                             (strcmp(view->valuestring, "performance") != 0 && strcmp(view->valuestring, "holdings") != 0))) {
            snprintf(msg, sizeof(msg), "widget %s: parqet_view must be performance or holdings", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        cJSON *interval = cJSON_GetObjectItemCaseSensitive(widget, "parqet_interval");
        if (interval != NULL && (!cJSON_IsString(interval) || interval->valuestring == NULL ||
                                 (strcmp(interval->valuestring, "1d") != 0 && strcmp(interval->valuestring, "1w") != 0 &&
                                  strcmp(interval->valuestring, "1m") != 0 && strcmp(interval->valuestring, "ytd") != 0 &&
                                  strcmp(interval->valuestring, "max") != 0))) {
            snprintf(msg, sizeof(msg), "widget %s: parqet_interval must be 1d, 1w, 1m, ytd or max", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "stock_quotes") == 0) {
        /* Stocks & ETFs: entity_id is the first quote, stock_entities up to seven more (sensors of Yahoo Finance or Easy
         * Stock; which one is known from the registry at run time). */
        const char *wid = cJSON_IsString(id) ? id->valuestring : "?";
        cJSON *more = cJSON_GetObjectItemCaseSensitive(widget, "stock_entities");
        if (more != NULL && (!cJSON_IsArray(more) || cJSON_GetArraySize(more) > 7)) {
            snprintf(msg, sizeof(msg), "widget %s: stock_entities must be an array of at most 7 sensor.* ids", wid);
            layout_validation_add(result, msg);
        } else if (more != NULL) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, more) {
                if (!cJSON_IsString(item) || !is_valid_entity_id(item->valuestring) || !entity_in_domain(item->valuestring, "sensor")) {
                    snprintf(msg, sizeof(msg), "widget %s: stock_entities must be sensor.* ids", wid);
                    layout_validation_add(result, msg);
                    break;
                }
            }
        }
        static const char *const choices[][6] = {
            {"stock_view", "list", "chart", "grid", NULL},
            {"stock_period", "1d", "1w", "30d", "ytd", "1y"},
            {"stock_delta", "percent", "absolute", "both", NULL},
        };
        for (size_t k = 0; k < sizeof(choices) / sizeof(choices[0]); k++) {
            cJSON *item = cJSON_GetObjectItemCaseSensitive(widget, choices[k][0]);
            if (item == NULL) continue;
            bool ok = cJSON_IsString(item) && item->valuestring != NULL;
            if (ok) {
                ok = false;
                for (size_t c = 1; c < 6 && choices[k][c] != NULL; c++) ok = ok || strcmp(item->valuestring, choices[k][c]) == 0;
            }
            if (!ok) {
                snprintf(msg, sizeof(msg), "widget %s: invalid %s", wid, choices[k][0]);
                layout_validation_add(result, msg);
            }
        }
        cJSON *ticker = cJSON_GetObjectItemCaseSensitive(widget, "stock_ticker");
        if (ticker != NULL && !cJSON_IsBool(ticker)) {
            snprintf(msg, sizeof(msg), "widget %s: stock_ticker must be true or false", wid);
            layout_validation_add(result, msg);
        }
        cJSON *stock_selector = cJSON_GetObjectItemCaseSensitive(widget, "stock_selector");
        if (stock_selector != NULL && !cJSON_IsBool(stock_selector)) {
            snprintf(msg, sizeof(msg), "widget %s: stock_selector must be true or false", wid);
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "heating_tile") == 0) {
        if (cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL &&
            secondary_entity_id->valuestring[0] != '\0') {
            if (!is_valid_entity_id(secondary_entity_id->valuestring) ||
                !entity_in_domain(secondary_entity_id->valuestring, "sensor")) {
                snprintf(msg, sizeof(msg), "widget %s: invalid secondary_entity_id", cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }

        cJSON *style_variant = cJSON_GetObjectItemCaseSensitive(widget, "style_variant");
        if (cJSON_IsString(style_variant) && style_variant->valuestring != NULL &&
            style_variant->valuestring[0] != '\0') {
            if (strcmp(style_variant->valuestring, "default") != 0 &&
                strcmp(style_variant->valuestring, "arc_semi") != 0) {
                snprintf(msg, sizeof(msg), "widget %s: style_variant must be default|arc_semi",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
        cJSON *arc_opening = cJSON_GetObjectItemCaseSensitive(widget, "arc_opening");
        if (cJSON_IsString(arc_opening) && arc_opening->valuestring != NULL &&
            arc_opening->valuestring[0] != '\0') {
            const char *v = arc_opening->valuestring;
            if (strcmp(v, "left") != 0 && strcmp(v, "right") != 0 && strcmp(v, "top") != 0 && strcmp(v, "bottom") != 0) {
                snprintf(msg, sizeof(msg), "widget %s: arc_opening must be left|right|top|bottom",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && is_vacuum_type(type->valuestring)) {
        if (cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL &&
            secondary_entity_id->valuestring[0] != '\0') {
            if (!is_valid_entity_id(secondary_entity_id->valuestring) ||
                !entity_in_domain(secondary_entity_id->valuestring, "image")) {
                snprintf(msg, sizeof(msg), "widget %s: secondary_entity_id must be image.*",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
        if (cJSON_IsString(tertiary_entity_id) && tertiary_entity_id->valuestring != NULL &&
            tertiary_entity_id->valuestring[0] != '\0') {
            if (!is_valid_entity_id(tertiary_entity_id->valuestring) ||
                !entity_in_domain(tertiary_entity_id->valuestring, "select")) {
                snprintf(msg, sizeof(msg), "widget %s: tertiary_entity_id must be select.*",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "sonos_favorites") == 0) {
        if (cJSON_IsString(secondary_entity_id) && secondary_entity_id->valuestring != NULL &&
            secondary_entity_id->valuestring[0] != '\0') {
            if (!is_valid_entity_id(secondary_entity_id->valuestring) ||
                !entity_in_domain(secondary_entity_id->valuestring, "sensor")) {
                snprintf(msg, sizeof(msg), "widget %s: secondary_entity_id must be sensor.*",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
                return false;
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "binary_tile") == 0) {
        cJSON *display_mode = cJSON_GetObjectItemCaseSensitive(widget, "display_mode");
        if (cJSON_IsString(display_mode) && display_mode->valuestring != NULL && display_mode->valuestring[0] != '\0' &&
            strcmp(display_mode->valuestring, "icon") != 0 && strcmp(display_mode->valuestring, "since") != 0 &&
            strcmp(display_mode->valuestring, "text") != 0 && strcmp(display_mode->valuestring, "door") != 0) {
            snprintf(msg, sizeof(msg), "widget %s: display_mode must be text|since|icon|door",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "room_tile") == 0) {
        cJSON *room_entities = cJSON_GetObjectItemCaseSensitive(widget, "room_entities");
        {
            const cJSON *own = cJSON_GetObjectItemCaseSensitive(widget, "entity_id");
            const char *extra_error = room_extra_error(widget, cJSON_IsString(own) ? own->valuestring : NULL, room_entities);
            if (extra_error != NULL) {
                snprintf(msg, sizeof(msg), "widget %s: %s", cJSON_IsString(id) ? id->valuestring : "?", extra_error);
                layout_validation_add(result, msg);
            }
        }
        cJSON *room_controls = cJSON_GetObjectItemCaseSensitive(widget, "room_controls");
        if (room_controls != NULL && !cJSON_IsBool(room_controls)) {
            snprintf(msg, sizeof(msg), "widget %s: room_controls must be true or false",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (room_entities != NULL && (!cJSON_IsArray(room_entities) || cJSON_GetArraySize(room_entities) > 7)) {
            snprintf(msg, sizeof(msg), "widget %s: room_entities must be an array of at most 7 entity ids",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        } else if (room_entities != NULL) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, room_entities) {
                if (!cJSON_IsString(item) || !is_valid_entity_id(item->valuestring)) {
                    snprintf(msg, sizeof(msg), "widget %s: invalid entity id in room_entities",
                        cJSON_IsString(id) ? id->valuestring : "?");
                    layout_validation_add(result, msg);
                    break;
                }
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "energy_timeline") == 0) {
        /* Recorder statistics ids of cumulative kWh counters: grid import (entity_id, required), grid export
         * (secondary_entity_id) and solar production (tertiary_entity_id), all optional except the first. */
        const cJSON *roles[] = { cJSON_GetObjectItemCaseSensitive(widget, "entity_id"), secondary_entity_id, tertiary_entity_id };
        const char *role_keys[] = { "entity_id (grid import)", "secondary_entity_id (grid export)",
                                    "tertiary_entity_id (solar)" };
        for (size_t i = 0; i < 3; i++) {
            const bool set = cJSON_IsString(roles[i]) && roles[i]->valuestring != NULL && roles[i]->valuestring[0] != '\0';
            if ((i == 0 && !set) || (set && (!is_valid_entity_id(roles[i]->valuestring) ||
                                              !entity_in_domain(roles[i]->valuestring, "sensor")))) {
                snprintf(msg, sizeof(msg), "widget %s: %s must be a sensor.* energy counter",
                    cJSON_IsString(id) ? id->valuestring : "?", role_keys[i]);
                layout_validation_add(result, msg);
            }
        }
        const cJSON *variant = cJSON_GetObjectItemCaseSensitive(widget, "style_variant");
        if (cJSON_IsString(variant) && variant->valuestring != NULL && variant->valuestring[0] != '\0' &&
            strcmp(variant->valuestring, "default") != 0 && strcmp(variant->valuestring, "nested") != 0) {
            snprintf(msg, sizeof(msg), "widget %s: style_variant must be default|nested", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        const cJSON *colors = cJSON_GetObjectItemCaseSensitive(widget, "energy_colors");
        if (colors != NULL && !cJSON_IsObject(colors)) {
            layout_validation_add(result, "energy_colors must be an object");
        } else if (colors != NULL) {
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, colors) {
                bool known = false;
                for (int i = 0; i < ETL_CUSTOM_COLOR_COUNT; i++)
                    known = known || (item->string && strcmp(item->string, ETL_COLOR_KEYS[i]) == 0);
                if (!known || !cJSON_IsString(item) || !is_valid_hex_rgb_color(item->valuestring)) {
                    layout_validation_add(result, "energy_colors requires known flow names and six-digit RGB colors");
                }
            }
        }
        /* Battery: the two energy counters go together; the state of charge and the forecast are optional. */
        static const char *const etl_extras[] = { "battery_charge_entity_id", "battery_discharge_entity_id",
                                                  "battery_soc_entity_id", "forecast_today_entity_id" };
        bool etl_extra_set[4] = { false, false, false, false };
        for (size_t i = 0; i < 4; i++) {
            const cJSON *extra = cJSON_GetObjectItemCaseSensitive(widget, etl_extras[i]);
            etl_extra_set[i] = cJSON_IsString(extra) && extra->valuestring != NULL && extra->valuestring[0] != '\0';
            if (etl_extra_set[i] && (!is_valid_entity_id(extra->valuestring) || !entity_in_domain(extra->valuestring, "sensor"))) {
                snprintf(msg, sizeof(msg), "widget %s: %s must be a sensor.*", cJSON_IsString(id) ? id->valuestring : "?",
                    etl_extras[i]);
                layout_validation_add(result, msg);
            }
        }
        if (etl_extra_set[0] != etl_extra_set[1]) {
            snprintf(msg, sizeof(msg), "widget %s: battery_charge_entity_id and battery_discharge_entity_id go together",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL &&
        (strcmp(type->valuestring, "vacuum_tile") == 0 || strcmp(type->valuestring, "roborock_tile") == 0)) {
        const cJSON *battery = cJSON_GetObjectItemCaseSensitive(widget, "battery_entity_id");
        if (battery != NULL && !(cJSON_IsString(battery) && battery->valuestring[0] == '\0') &&
            (!cJSON_IsString(battery) || strlen(battery->valuestring) >= APP_MAX_ENTITY_ID_LEN ||
             !is_valid_entity_id(battery->valuestring) || !entity_in_domain(battery->valuestring, "sensor"))) {
            snprintf(msg, sizeof(msg), "widget %s: battery_entity_id must be a sensor.* entity",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "bambu_printer") == 0) {
        /* Explicit bindings (layout/bambu_printer_slots.h): sensors, three action buttons and an optional camera.
         * Overlong ids are rejected rather than truncated; the three buttons must be distinct. */
        const char *actions[3] = {NULL, NULL, NULL};
        size_t action_count = 0;
        for (size_t i = 0; i < k_bambu_printer_binding_count; i++) {
            const bambu_printer_binding_t *binding = &k_bambu_printer_bindings[i];
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(widget, binding->key);
            if (item == NULL || (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] == '\0')) {
                continue;
            }
            if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= APP_MAX_ENTITY_ID_LEN ||
                !is_valid_entity_id(item->valuestring) || !entity_in_domain(item->valuestring, binding->domain)) {
                snprintf(msg, sizeof(msg), "widget %s: %s must be a valid %s.* entity",
                    cJSON_IsString(id) ? id->valuestring : "?", binding->key, binding->domain);
                layout_validation_add(result, msg);
                continue;
            }
            if (binding->action && action_count < 3) {
                for (size_t j = 0; j < action_count; j++) {
                    if (strcmp(actions[j], item->valuestring) == 0) {
                        snprintf(msg, sizeof(msg), "widget %s: pause, resume and stop need three different buttons",
                            cJSON_IsString(id) ? id->valuestring : "?");
                        layout_validation_add(result, msg);
                        break;
                    }
                }
                actions[action_count++] = item->valuestring;
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && !strcmp(type->valuestring, "wallbox_evcc")) {
        for (size_t i = 0; i < EVCC_ROLE_COUNT; i++) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(widget, k_evcc_bindings[i].key);
            if (!value || (cJSON_IsString(value) && value->valuestring && !*value->valuestring)) continue;
            if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= APP_MAX_ENTITY_ID_LEN ||
                !is_valid_entity_id(value->valuestring) || !evcc_binding_domain(i, value->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: invalid %s binding", cJSON_IsString(id) ? id->valuestring : "?", k_evcc_bindings[i].key);
                layout_validation_add(result, msg);
            }
        }
        /* The optional header: "power" (a bar up to evcc_power_max_kw) or "flow" (the site's four power sensors). */
        for (size_t i = 0; i < EVCC_SITE_COUNT; i++) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(widget, k_evcc_site_bindings[i].key);
            if (!value || (cJSON_IsString(value) && value->valuestring && !*value->valuestring)) continue;
            if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= APP_MAX_ENTITY_ID_LEN ||
                !is_valid_entity_id(value->valuestring) || !entity_in_domain(value->valuestring, "sensor")) {
                snprintf(msg, sizeof(msg), "widget %s: invalid %s", cJSON_IsString(id) ? id->valuestring : "?", k_evcc_site_bindings[i].key);
                layout_validation_add(result, msg);
            }
        }
        const cJSON *header = cJSON_GetObjectItemCaseSensitive(widget, "evcc_header");
        if (header && (!cJSON_IsString(header) || (strcmp(header->valuestring, "power") && strcmp(header->valuestring, "flow")))) {
            snprintf(msg, sizeof(msg), "widget %s: evcc_header must be power or flow", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        const cJSON *max_kw = cJSON_GetObjectItemCaseSensitive(widget, "evcc_power_max_kw");
        if (max_kw && (!cJSON_IsNumber(max_kw) || max_kw->valuedouble < EVCC_POWER_MAX_KW_MIN || max_kw->valuedouble > EVCC_POWER_MAX_KW_MAX)) {
            snprintf(msg, sizeof(msg), "widget %s: evcc_power_max_kw must be 1-50", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "car_widget") == 0) {
        /* Optional extras: pre-conditioning control, lock / door status, interior temperature. */
        static const struct {
            const char *key;
            const char *domains[6];
        } car_extras[] = {
            { "precond_entity_id", { "switch", "input_boolean", "climate", "button", "input_button", "select" } },
            { "lock_entity_id", { "binary_sensor", "lock", NULL } },
            { "interior_temp_entity_id", { "sensor", NULL } },
        };
        for (size_t i = 0; i < sizeof(car_extras) / sizeof(car_extras[0]); i++) {
            const cJSON *extra = cJSON_GetObjectItemCaseSensitive(widget, car_extras[i].key);
            if (!cJSON_IsString(extra) || extra->valuestring == NULL || extra->valuestring[0] == '\0') {
                continue;
            }
            bool ok = is_valid_entity_id(extra->valuestring);
            bool in_domain = false;
            for (size_t d = 0; ok && d < 6 && car_extras[i].domains[d] != NULL; d++) {
                in_domain = in_domain || entity_in_domain(extra->valuestring, car_extras[i].domains[d]);
            }
            if (!ok || !in_domain) {
                snprintf(msg, sizeof(msg), "widget %s: invalid %s", cJSON_IsString(id) ? id->valuestring : "?",
                    car_extras[i].key);
                layout_validation_add(result, msg);
            }
        }
        const cJSON *extra_entities[] = { secondary_entity_id, tertiary_entity_id };
        const char *extra_keys[] = { "secondary_entity_id", "tertiary_entity_id" };
        for (size_t i = 0; i < sizeof(extra_entities) / sizeof(extra_entities[0]); i++) {
            const cJSON *extra = extra_entities[i];
            if (cJSON_IsString(extra) && extra->valuestring != NULL && extra->valuestring[0] != '\0') {
                if (!is_valid_entity_id(extra->valuestring) || !entity_in_domain(extra->valuestring, "sensor")) {
                    snprintf(msg, sizeof(msg), "widget %s: %s must be sensor.*",
                        cJSON_IsString(id) ? id->valuestring : "?", extra_keys[i]);
                    layout_validation_add(result, msg);
                }
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring &&
        (!strcmp(type->valuestring, "weather_3day") || !strcmp(type->valuestring, "solar_forecast"))) {
        cJSON *days = cJSON_GetObjectItemCaseSensitive(widget, "forecast_days");
        if (days != NULL && (!cJSON_IsNumber(days) || days->valuedouble != days->valueint || days->valueint < 0 || days->valueint > 7)) {
            snprintf(msg, sizeof(msg), "widget %s: forecast_days must be an integer from 0 (auto) to 7", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        cJSON *aligned = cJSON_GetObjectItemCaseSensitive(widget, "align_forecast");
        if (aligned != NULL && !cJSON_IsBool(aligned)) {
            snprintf(msg, sizeof(msg), "widget %s: align_forecast must be true or false", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        cJSON *show_precip = cJSON_GetObjectItemCaseSensitive(widget, "show_precipitation");
        if (show_precip != NULL && !cJSON_IsBool(show_precip)) {
            snprintf(msg, sizeof(msg), "widget %s: show_precipitation must be true or false",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "solar_forecast") == 0) {
        const cJSON *forecast_entities[] = {
            forecast_today_entity_id,
            forecast_tomorrow_entity_id,
            forecast_day_3_entity_id,
            forecast_day_4_entity_id,
            forecast_day_5_entity_id,
            forecast_day_6_entity_id,
            forecast_day_7_entity_id,
        };
        const char *forecast_keys[] = {
            "forecast_today_entity_id",
            "forecast_tomorrow_entity_id",
            "forecast_day_3_entity_id",
            "forecast_day_4_entity_id",
            "forecast_day_5_entity_id",
            "forecast_day_6_entity_id",
            "forecast_day_7_entity_id",
        };
        for (size_t i = 0; i < sizeof(forecast_entities) / sizeof(forecast_entities[0]); i++) {
            const cJSON *item = forecast_entities[i];
            if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
                if (!is_valid_entity_id(item->valuestring) || !entity_in_domain(item->valuestring, "sensor")) {
                    snprintf(msg, sizeof(msg), "widget %s: %s must be sensor.*",
                        cJSON_IsString(id) ? id->valuestring : "?", forecast_keys[i]);
                    layout_validation_add(result, msg);
                }
            }
        }
        if (solar_forecast_bar_max_kwh != NULL &&
            (!cJSON_IsNumber(solar_forecast_bar_max_kwh) ||
             solar_forecast_bar_max_kwh->valuedouble < 0.0 ||
             solar_forecast_bar_max_kwh->valuedouble > 100000.0)) {
            snprintf(msg, sizeof(msg), "widget %s: solar_forecast_bar_max_kwh must be a positive number",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (solar_forecast_bar_orientation != NULL &&
            (!cJSON_IsString(solar_forecast_bar_orientation) || solar_forecast_bar_orientation->valuestring == NULL ||
             !is_valid_solar_forecast_bar_orientation(solar_forecast_bar_orientation->valuestring))) {
            snprintf(msg, sizeof(msg), "widget %s: solar_forecast_bar_orientation must be horizontal|vertical",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "gauge") == 0) {
        cJSON *gauge_style_variant = cJSON_GetObjectItemCaseSensitive(widget, "style_variant");
        if (cJSON_IsString(gauge_style_variant) && gauge_style_variant->valuestring != NULL &&
            gauge_style_variant->valuestring[0] != '\0' && strcmp(gauge_style_variant->valuestring, "linear") != 0 &&
            strcmp(gauge_style_variant->valuestring, "horizontal") != 0 &&
            strcmp(gauge_style_variant->valuestring, "radial") != 0) {
            snprintf(msg, sizeof(msg), "widget %s: style_variant must be linear|horizontal|radial",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_min != NULL && !cJSON_IsNumber(gauge_min)) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_min must be a number", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_max != NULL && !cJSON_IsNumber(gauge_max)) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_max must be a number", cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_min != NULL && gauge_max != NULL && cJSON_IsNumber(gauge_min) && cJSON_IsNumber(gauge_max) &&
            gauge_max->valuedouble <= gauge_min->valuedouble) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_max must be greater than gauge_min",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_bar_style != NULL && cJSON_IsString(gauge_bar_style) && gauge_bar_style->valuestring != NULL &&
            gauge_bar_style->valuestring[0] != '\0' && strcmp(gauge_bar_style->valuestring, "smooth") != 0 &&
            strcmp(gauge_bar_style->valuestring, "segmented") != 0) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_bar_style must be smooth|segmented",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_segment_count != NULL &&
            (!cJSON_IsNumber(gauge_segment_count) ||
             gauge_segment_count->valuedouble < (double)GAUGE_SEGMENT_COUNT_MIN ||
             gauge_segment_count->valuedouble > (double)GAUGE_SEGMENT_COUNT_MAX ||
             (double)gauge_segment_count->valueint != gauge_segment_count->valuedouble)) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_segment_count must be integer %d..%d",
                cJSON_IsString(id) ? id->valuestring : "?", GAUGE_SEGMENT_COUNT_MIN, GAUGE_SEGMENT_COUNT_MAX);
            layout_validation_add(result, msg);
        }
        /* Color coding by value, on top of the default fill-level gold->green: "gradient" is a
         * custom two-color version of the same idea (e.g. cold blue to hot red across the full
         * gauge_min..gauge_max range), "range" highlights a target band distinctly from outside
         * it (e.g. a humidity "normal" range in one color, too dry/too humid in another). Colors
         * not relevant to the chosen mode may still be present (switching modes in the editor
         * keeps them) and are simply unused by the renderer. */
        const char *mode = (gauge_color_mode != NULL && cJSON_IsString(gauge_color_mode) && gauge_color_mode->valuestring != NULL)
                                ? gauge_color_mode->valuestring
                                : "";
        if (mode[0] != '\0' && strcmp(mode, "fill") != 0 && strcmp(mode, "gradient") != 0 && strcmp(mode, "range") != 0) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_color_mode must be fill|gradient|range",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_color_low != NULL) {
            if (!cJSON_IsString(gauge_color_low) || gauge_color_low->valuestring == NULL ||
                !is_valid_hex_rgb_color(gauge_color_low->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: gauge_color_low must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
        if (gauge_color_high != NULL) {
            if (!cJSON_IsString(gauge_color_high) || gauge_color_high->valuestring == NULL ||
                !is_valid_hex_rgb_color(gauge_color_high->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: gauge_color_high must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
        if (gauge_target_color != NULL) {
            if (!cJSON_IsString(gauge_target_color) || gauge_target_color->valuestring == NULL ||
                !is_valid_hex_rgb_color(gauge_target_color->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: gauge_target_color must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
        if (gauge_target_low != NULL && !cJSON_IsNumber(gauge_target_low)) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_target_low must be a number",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_target_high != NULL && !cJSON_IsNumber(gauge_target_high)) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_target_high must be a number",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (gauge_target_low != NULL && gauge_target_high != NULL && cJSON_IsNumber(gauge_target_low) &&
            cJSON_IsNumber(gauge_target_high) && gauge_target_high->valuedouble <= gauge_target_low->valuedouble) {
            snprintf(msg, sizeof(msg), "widget %s: gauge_target_high must be greater than gauge_target_low",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (alarm_actions != NULL) {
        bool ok = cJSON_IsString(type) && strcmp(type->valuestring, "alarm_tile") == 0 &&
                  cJSON_IsArray(alarm_actions) && cJSON_GetArraySize(alarm_actions) > 0 &&
                  cJSON_GetArraySize(alarm_actions) <= HA_ALARM_ACTION_COUNT;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, alarm_actions) {
            ok = ok && cJSON_IsString(item) && ha_alarm_action_from_name(item->valuestring, NULL);
        }
        if (!ok) {
            snprintf(msg, sizeof(msg), "widget %s: alarm_actions must list known alarm actions",
                     cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }
    const cJSON *camera_live = cJSON_GetObjectItemCaseSensitive(widget, "camera_live");
    if (camera_live && (!cJSON_IsBool(camera_live) ||
        (cJSON_IsTrue(camera_live) && (!cJSON_IsString(type) || strcmp(type->valuestring, "camera_tile"))))) {
        layout_validation_add(result, "camera_live must be a boolean on a camera_tile");
    }
    if (cJSON_IsTrue(camera_live) && cJSON_IsString(entity_id) &&
        !entity_in_domain(entity_id->valuestring, "camera")) {
        layout_validation_add(result, "camera_live requires a camera.* entity (MJPEG)");
    }
    if (lock_confirm != NULL && !cJSON_IsBool(lock_confirm)) {
        snprintf(msg, sizeof(msg), "widget %s: lock_confirm must be true or false",
            cJSON_IsString(id) ? id->valuestring : "?");
        layout_validation_add(result, msg);
    }
    if (history_popup != NULL) {
        bool allowed = cJSON_IsString(type) &&
            (strcmp(type->valuestring, "sensor") == 0 || strcmp(type->valuestring, "binary_tile") == 0 ||
             strcmp(type->valuestring, "contact_tile") == 0 || strcmp(type->valuestring, "lock_tile") == 0 ||
             strcmp(type->valuestring, "alarm_tile") == 0 || strcmp(type->valuestring, "vacuum_tile") == 0);
        if (!cJSON_IsBool(history_popup) || !allowed) {
            snprintf(msg, sizeof(msg), "widget %s: history_popup only accepts a boolean on history-capable tiles",
                     cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "sensor") == 0) {
        if (sensor_show_label != NULL && !cJSON_IsBool(sensor_show_label)) {
            snprintf(msg, sizeof(msg), "widget %s: sensor_show_label must be true or false",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        if (sensor_show_age != NULL && !cJSON_IsBool(sensor_show_age)) {
            snprintf(msg, sizeof(msg), "widget %s: sensor_show_age must be true or false",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        const cJSON *view = cJSON_GetObjectItemCaseSensitive(widget, "sensor_view");
        if (view != NULL && (!cJSON_IsString(view) ||
            (strcmp(view->valuestring, "single") != 0 && strcmp(view->valuestring, "list") != 0))) {
            layout_validation_add(result, "sensor_view must be single or list");
        }
        const cJSON *more = cJSON_GetObjectItemCaseSensitive(widget, "sensor_entities");
        if (more != NULL && (!cJSON_IsArray(more) || cJSON_GetArraySize(more) > 7)) {
            layout_validation_add(result, "sensor_entities must contain at most seven additional sensors");
        } else if (more != NULL) {
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, more) {
                if (!cJSON_IsString(item) || !is_valid_entity_id(item->valuestring) ||
                    (!entity_in_domain(item->valuestring, "sensor") && !entity_in_domain(item->valuestring, "binary_sensor"))) {
                    layout_validation_add(result, "sensor_entities must contain sensor.* or binary_sensor.* IDs");
                    break;
                }
            }
        }
        /* The device class decides which values the editor offers; any of them is valid for any sensor. */
        cJSON *format = cJSON_GetObjectItemCaseSensitive(widget, "sensor_format");
        if (format != NULL && (!cJSON_IsString(format) || !ha_sv_format_valid(format->valuestring))) {
            snprintf(msg, sizeof(msg), "widget %s: sensor_format must be datetime, date, time, relative, text, clock or raw",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(widget, "sensor_unit");
        ha_sv_unit_t unit_mode;
        if (unit != NULL && (!cJSON_IsString(unit) || unit->valuestring == NULL || unit->valuestring[0] == '\0' ||
                             !ha_sv_unit_from_string(unit->valuestring, &unit_mode))) {
            snprintf(msg, sizeof(msg), "widget %s: sensor_unit must be hide, degree, auto or code",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "slider") == 0) {
        if (slider_direction != NULL) {
            if (!cJSON_IsString(slider_direction) || slider_direction->valuestring == NULL ||
                !is_valid_slider_direction(slider_direction->valuestring)) {
                snprintf(msg, sizeof(msg),
                    "widget %s: slider_direction must be auto|left_to_right|right_to_left|bottom_to_top|top_to_bottom",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }

        if (slider_accent_color != NULL) {
            if (!cJSON_IsString(slider_accent_color) || slider_accent_color->valuestring == NULL ||
                !is_valid_hex_rgb_color(slider_accent_color->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: slider_accent_color must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "button") == 0) {
        if (button_accent_color != NULL) {
            if (!cJSON_IsString(button_accent_color) || button_accent_color->valuestring == NULL ||
                !is_valid_hex_rgb_color(button_accent_color->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: button_accent_color must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }

        if (button_mode != NULL) {
            if (!cJSON_IsString(button_mode) || button_mode->valuestring == NULL ||
                !is_valid_button_mode(button_mode->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: button_mode must be auto|play_pause|stop|next|previous",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            } else if (button_mode_requires_media_player(button_mode->valuestring) &&
                       cJSON_IsString(entity_id) && entity_id->valuestring != NULL &&
                       !entity_in_domain(entity_id->valuestring, "media_player")) {
                snprintf(msg, sizeof(msg), "widget %s: button_mode %s requires media_player.* entity_id",
                    cJSON_IsString(id) ? id->valuestring : "?",
                    button_mode->valuestring);
                layout_validation_add(result, msg);
            }
        }
    }

    if (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "graph") == 0) {
        if (graph_line_color != NULL) {
            if (!cJSON_IsString(graph_line_color) || graph_line_color->valuestring == NULL ||
                !is_valid_hex_rgb_color(graph_line_color->valuestring)) {
                snprintf(msg, sizeof(msg), "widget %s: graph_line_color must be hex RGB",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }

        if (graph_point_count != NULL) {
            if (!cJSON_IsNumber(graph_point_count) ||
                graph_point_count->valuedouble < (double)GRAPH_POINT_COUNT_MIN ||
                graph_point_count->valuedouble > (double)GRAPH_POINT_COUNT_MAX ||
                (double)graph_point_count->valueint != graph_point_count->valuedouble) {
                snprintf(msg, sizeof(msg), "widget %s: graph_point_count must be integer %d..%d",
                    cJSON_IsString(id) ? id->valuestring : "?",
                    GRAPH_POINT_COUNT_MIN,
                    GRAPH_POINT_COUNT_MAX);
                layout_validation_add(result, msg);
            }
        }

        if (graph_time_window_min != NULL) {
            if (!cJSON_IsNumber(graph_time_window_min) ||
                graph_time_window_min->valuedouble < (double)GRAPH_TIME_WINDOW_MIN_MIN ||
                graph_time_window_min->valuedouble > (double)GRAPH_TIME_WINDOW_MIN_MAX ||
                (double)graph_time_window_min->valueint != graph_time_window_min->valuedouble) {
                snprintf(msg, sizeof(msg), "widget %s: graph_time_window_min must be integer %d..%d",
                    cJSON_IsString(id) ? id->valuestring : "?",
                    GRAPH_TIME_WINDOW_MIN_MIN,
                    GRAPH_TIME_WINDOW_MIN_MAX);
                layout_validation_add(result, msg);
            }
        }

        if (graph_display_mode != NULL) {
            bool valid_mode = false;
            if (cJSON_IsString(graph_display_mode) && graph_display_mode->valuestring != NULL) {
                for (size_t i = 0; i < sizeof(GRAPH_DISPLAY_MODES) / sizeof(GRAPH_DISPLAY_MODES[0]); ++i) {
                    if (strcmp(graph_display_mode->valuestring, GRAPH_DISPLAY_MODES[i]) == 0) {
                        valid_mode = true;
                        break;
                    }
                }
            }
            if (!valid_mode) {
                snprintf(msg, sizeof(msg),
                    "widget %s: graph_display_mode must be line|line_smooth|line_smooth_points|bars",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }

        if (graph_bar_bucket_min != NULL) {
            bool valid_bucket = false;
            if (cJSON_IsNumber(graph_bar_bucket_min) &&
                (double)graph_bar_bucket_min->valueint == graph_bar_bucket_min->valuedouble) {
                for (size_t i = 0; i < sizeof(GRAPH_BAR_BUCKET_MIN_ALLOWED) / sizeof(GRAPH_BAR_BUCKET_MIN_ALLOWED[0]); ++i) {
                    if (graph_bar_bucket_min->valueint == GRAPH_BAR_BUCKET_MIN_ALLOWED[i]) {
                        valid_bucket = true;
                        break;
                    }
                }
            }
            if (!valid_bucket) {
                snprintf(msg, sizeof(msg),
                    "widget %s: graph_bar_bucket_min must be one of 5|10|15|30",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            }
        }
    }

    if (!cJSON_IsObject(rect)) {
        snprintf(msg, sizeof(msg), "widget %s: missing rect", cJSON_IsString(id) ? id->valuestring : "?");
        layout_validation_add(result, msg);
    } else {
        cJSON *x = cJSON_GetObjectItemCaseSensitive(rect, "x");
        cJSON *y = cJSON_GetObjectItemCaseSensitive(rect, "y");
        cJSON *w = cJSON_GetObjectItemCaseSensitive(rect, "w");
        cJSON *h = cJSON_GetObjectItemCaseSensitive(rect, "h");
        if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsNumber(w) || !cJSON_IsNumber(h)) {
            snprintf(msg, sizeof(msg), "widget %s: rect values must be numbers",
                cJSON_IsString(id) ? id->valuestring : "?");
            layout_validation_add(result, msg);
        } else {
            int rx = x->valueint;
            int ry = y->valueint;
            int rw = w->valueint;
            int rh = h->valueint;
            const cJSON *look = cJSON_GetObjectItemCaseSensitive(widget, "style_variant");
            widget_size_limits_t limits = widget_size_limits_for(cJSON_IsString(type) ? type->valuestring : NULL,
                                                                 cJSON_IsString(look) ? look->valuestring : NULL);
            if (rw <= 0 || rh <= 0 || rx < 0 || ry < 0 || (rx + rw) > APP_CONTENT_BOX_WIDTH ||
                (ry + rh) > APP_CONTENT_BOX_HEIGHT) {
                snprintf(msg, sizeof(msg), "widget %s: rect out of bounds for content box",
                    cJSON_IsString(id) ? id->valuestring : "?");
                layout_validation_add(result, msg);
            } else if (rw < limits.min_w || rh < limits.min_h || rw > limits.max_w || rh > limits.max_h) {
                snprintf(msg, sizeof(msg), "widget %s: size must be %dx%d..%dx%d",
                    cJSON_IsString(id) ? id->valuestring : "?",
                    limits.min_w,
                    limits.min_h,
                    limits.max_w,
                    limits.max_h);
                layout_validation_add(result, msg);
            }
        }
    }

    const cJSON *portrait = cJSON_GetObjectItemCaseSensitive(widget, "portrait_rect");
    if (portrait && !layout_orientation_rect_valid(widget, portrait, true)) {
        snprintf(msg, sizeof(msg), "widget %s: invalid portrait_rect", cJSON_IsString(id) ? id->valuestring : "?");
        layout_validation_add(result, msg);
    }
    return true;
}

/* Alerts (Settings > Alerts): up to 4, each with a trigger entity, optionally an action and a camera. The ids end up
 * in service calls, so they are checked like the widget entities and the action domains are limited. */
static void validate_alerts(const cJSON *root, layout_validation_result_t *result)
{
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts");
    if (alerts == NULL) {
        return;
    }
    if (!cJSON_IsArray(alerts)) {
        layout_validation_add(result, "alerts must be an array");
        return;
    }
    char msg[96];
    int index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, alerts)
    {
        index++;
        if (index > 4) {
            layout_validation_add(result, "too many alerts (at most 4)");
            return;
        }
        if (!cJSON_IsObject(item)) {
            snprintf(msg, sizeof(msg), "alert %d must be an object", index);
            layout_validation_add(result, msg);
            continue;
        }
        const cJSON *sound = cJSON_GetObjectItemCaseSensitive(item, "sound");
        chime_kind_t kind;
        if (sound && (!cJSON_IsString(sound) || !chime_kind_parse(sound->valuestring, &kind)))
            layout_validation_add(result, "alert sound must be chime, notify or none");
        const cJSON *trigger = cJSON_GetObjectItemCaseSensitive(item, "entity_id");
        if (!cJSON_IsString(trigger) || !entity_id_valid(trigger->valuestring)) {
            snprintf(msg, sizeof(msg), "alert %d: invalid entity_id", index);
            layout_validation_add(result, msg);
        }
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(item, "action_entity_id");
        if (cJSON_IsString(action) && action->valuestring[0] != '\0' && !entity_id_action_allowed(action->valuestring)) {
            snprintf(msg, sizeof(msg), "alert %d: action_entity_id must be a button, lock, scene, script, switch, light or fan", index);
            layout_validation_add(result, msg);
        }
        const cJSON *camera = cJSON_GetObjectItemCaseSensitive(item, "camera_entity_id");
        if (cJSON_IsString(camera) && camera->valuestring[0] != '\0' && !entity_id_in_domain(camera->valuestring, "camera")) {
            snprintf(msg, sizeof(msg), "alert %d: camera_entity_id must be camera.*", index);
            layout_validation_add(result, msg);
        }
    }
}

bool layout_validate_json(const char *json, layout_validation_result_t *result)
{
    layout_validation_clear(result);
    if (json == NULL) {
        layout_validation_add(result, "layout json is null");
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        layout_validation_add(result, "layout json parse error");
        return false;
    }

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");

    if (!cJSON_IsNumber(version) || version->valueint != 1) {
        layout_validation_add(result, "layout version must be 1");
    }

    validate_alerts(root, result);

    if (!cJSON_IsArray(pages)) {
        layout_validation_add(result, "pages must be an array");
        cJSON_Delete(root);
        return false;
    }

    int page_count = cJSON_GetArraySize(pages);
    if (page_count <= 0) {
        layout_validation_add(result, "at least one page required");
    }
    if (page_count > APP_MAX_PAGES) {
        layout_validation_add(result, "too many pages");
    }

    char *known_page_ids = calloc(APP_MAX_PAGES, APP_MAX_PAGE_ID_LEN);
    char *known_widget_ids = calloc(APP_MAX_WIDGETS_TOTAL, APP_MAX_WIDGET_ID_LEN);
    if (known_page_ids == NULL || known_widget_ids == NULL) {
        free(known_page_ids);
        free(known_widget_ids);
        layout_validation_add(result, "out of memory during validation");
        cJSON_Delete(root);
        return false;
    }

    size_t known_page_ids_len = 0;
    size_t known_widget_ids_len = 0;

    for (int i = 0; i < page_count; i++) {
        cJSON *page = cJSON_GetArrayItem(pages, i);
        if (!cJSON_IsObject(page)) {
            layout_validation_add(result, "page entry must be object");
            continue;
        }

        cJSON *page_id = cJSON_GetObjectItemCaseSensitive(page, "id");
        cJSON *page_type = cJSON_GetObjectItemCaseSensitive(page, "type");
        cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
        char msg[96];
        bool is_energy_dashboard_page = false;
        bool is_house_page = false;

        if (!cJSON_IsString(page_id) || page_id->valuestring == NULL || strlen(page_id->valuestring) == 0U) {
            snprintf(msg, sizeof(msg), "page[%u]: invalid id", (unsigned)i);
            layout_validation_add(result, msg);
        } else if (strlen(page_id->valuestring) >= APP_MAX_PAGE_ID_LEN) {
            snprintf(msg, sizeof(msg), "page id too long: %.77s", page_id->valuestring);
            layout_validation_add(result, msg);
        } else if (str_in_list(page_id->valuestring, known_page_ids, APP_MAX_PAGE_ID_LEN, known_page_ids_len)) {
            snprintf(msg, sizeof(msg), "duplicate page id: %s", page_id->valuestring);
            layout_validation_add(result, msg);
        } else if (known_page_ids_len < APP_MAX_PAGES) {
            char *dst = known_page_ids + (known_page_ids_len * APP_MAX_PAGE_ID_LEN);
            snprintf(dst, APP_MAX_PAGE_ID_LEN, "%s", page_id->valuestring);
            known_page_ids_len++;
        }

        if (page_type != NULL) {
            if (!cJSON_IsString(page_type) || page_type->valuestring == NULL ||
                !is_supported_page_type(page_type->valuestring)) {
                snprintf(msg, sizeof(msg), "page %s: unsupported type", cJSON_IsString(page_id) ? page_id->valuestring : "?");
                layout_validation_add(result, msg);
            } else if (strcmp(page_type->valuestring, "energy_dashboard") == 0) {
                is_energy_dashboard_page = true;
            } else if (strcmp(page_type->valuestring, "house_dashboard") == 0) {
                is_house_page = true;
            }
        }

        if (is_house_page) {
            /* Rooms only: {"house":{"rooms":[{"name","floor","entity_id","room_entities":[..]}]}}, at most 32. */
            const char *pid = cJSON_IsString(page_id) ? page_id->valuestring : "?";
            cJSON *house = cJSON_GetObjectItemCaseSensitive(page, "house");
            cJSON *rooms = cJSON_IsObject(house) ? cJSON_GetObjectItemCaseSensitive(house, "rooms") : NULL;
            if (!cJSON_IsArray(rooms) || cJSON_GetArraySize(rooms) > APP_MAX_WIDGETS_PER_PAGE) {
                snprintf(msg, sizeof(msg), "page %s: house.rooms must be an array of at most %d rooms", pid,
                    APP_MAX_WIDGETS_PER_PAGE);
                layout_validation_add(result, msg);
            } else {
                cJSON *room = NULL;
                cJSON_ArrayForEach(room, rooms) {
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(room, "name");
                    cJSON *eid = cJSON_GetObjectItemCaseSensitive(room, "entity_id");
                    cJSON *extras = cJSON_GetObjectItemCaseSensitive(room, "room_entities");
                    bool ok = cJSON_IsObject(room) && cJSON_IsString(name) && name->valuestring != NULL &&
                              cJSON_IsString(eid) && is_valid_entity_id(eid->valuestring) &&
                              (extras == NULL || (cJSON_IsArray(extras) && cJSON_GetArraySize(extras) <= 7));
                    if (ok && extras != NULL) {
                        cJSON *extra = NULL;
                        cJSON_ArrayForEach(extra, extras) {
                            if (!cJSON_IsString(extra) || !is_valid_entity_id(extra->valuestring)) {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if (!ok) {
                        snprintf(msg, sizeof(msg), "page %s: invalid room in house.rooms", pid);
                        layout_validation_add(result, msg);
                        break;
                    }
                    const char *extra_error = room_extra_error(room, eid->valuestring, extras);
                    if (extra_error != NULL) {
                        snprintf(msg, sizeof(msg), "page %s: room %s: %s", pid, name->valuestring, extra_error);
                        layout_validation_add(result, msg);
                        break;
                    }
                }
            }
            if (cJSON_IsArray(widgets) && cJSON_GetArraySize(widgets) > 0) {
                snprintf(msg, sizeof(msg), "page %s: house_dashboard pages cannot contain widgets", pid);
                layout_validation_add(result, msg);
            }
            continue;
        }

        if (is_energy_dashboard_page) {
            validate_energy_page(page, cJSON_IsString(page_id) ? page_id->valuestring : "?", result);
            if (widgets != NULL && !cJSON_IsArray(widgets)) {
                snprintf(msg, sizeof(msg), "page %s: widgets must be array", cJSON_IsString(page_id) ? page_id->valuestring : "?");
                layout_validation_add(result, msg);
            } else if (cJSON_IsArray(widgets) && cJSON_GetArraySize(widgets) > 0) {
                snprintf(msg, sizeof(msg), "page %s: energy_dashboard pages cannot contain widgets",
                    cJSON_IsString(page_id) ? page_id->valuestring : "?");
                layout_validation_add(result, msg);
            }
            continue;
        }

        if (!cJSON_IsArray(widgets)) {
            snprintf(msg, sizeof(msg), "page %s: widgets must be array", cJSON_IsString(page_id) ? page_id->valuestring : "?");
            layout_validation_add(result, msg);
            continue;
        }

        int widget_count = cJSON_GetArraySize(widgets);
        if (widget_count > APP_MAX_WIDGETS_PER_PAGE) {
            snprintf(msg, sizeof(msg), "page %s: too many widgets", cJSON_IsString(page_id) ? page_id->valuestring : "?");
            layout_validation_add(result, msg);
        }

        for (int w = 0; w < widget_count; w++) {
            cJSON *widget = cJSON_GetArrayItem(widgets, w);
            if (!cJSON_IsObject(widget)) {
                layout_validation_add(result, "widget entry must be object");
                continue;
            }
            validate_widget(widget, known_widget_ids, known_widget_ids_len, (size_t)i, (size_t)w, result);
            cJSON *id = cJSON_GetObjectItemCaseSensitive(widget, "id");
            if (cJSON_IsString(id) && id->valuestring != NULL && strlen(id->valuestring) > 0 &&
                strlen(id->valuestring) < APP_MAX_WIDGET_ID_LEN &&
                !str_in_list(id->valuestring, known_widget_ids, APP_MAX_WIDGET_ID_LEN, known_widget_ids_len) &&
                known_widget_ids_len < APP_MAX_WIDGETS_TOTAL) {
                char *dst = known_widget_ids + (known_widget_ids_len * APP_MAX_WIDGET_ID_LEN);
                snprintf(dst, APP_MAX_WIDGET_ID_LEN, "%s", id->valuestring);
                known_widget_ids_len++;
            }
        }
    }

    free(known_page_ids);
    free(known_widget_ids);
    cJSON_Delete(root);
    return result->count == 0;
}
