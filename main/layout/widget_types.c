/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/widget_types.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"

#define CBW APP_CONTENT_BOX_WIDTH
#define CBH APP_CONTENT_BOX_HEIGHT


typedef struct {
    const char *type;
    widget_size_limits_t limits;
    const char *domains; /* '|'-separated entity domains of the primary entity, "*" = any */
} widget_type_info_t;

static const widget_type_info_t s_types[] = {
    /* type              min_w       min_h       max_w       max_h       domains */
    {"sensor",          {120,  80,  CBW,        CBH},       "sensor|binary_sensor"},
    {"button",          {100,  100, 480, 320}, "switch|media_player"},
    {"slider",          {100,         100, CBW,        CBH},       "*"},
    {"gauge",           {80,          160, CBW,       CBH},       "sensor"},
    {"graph",           {220, 140, CBW,       CBH},       "*"},
    {"empty_tile",      {120, 80,  CBW,        CBH},       "*"},
    {"light_tile",      {180, 180, 480,       480},       "light"},
    {"heating_tile",    {220, 200, 480,       480},       "climate"},
    {"weather_tile",    {220, 200, 480,       480},       "weather"},
    {"weather_3day",    {260, 220, CBW,       CBH},       "weather"},
    {"solar_forecast",  {260, 220, 640,       480},       "sensor"},
    /* A day of electricity prices (hourly or quarter-hourly) from the list attribute of a price sensor. */
    {"price_forecast",  {240, 150, CBW,       CBH},       "sensor"},
    /* A portfolio from the Parqet Companion sensors: value, gain, XIRR, dividends and the largest holdings. */
    {"parqet_portfolio", {240, 150, CBW,       CBH},       "sensor"},
    /* Stocks & ETFs: up to eight quotes of Yahoo Finance or Easy Stock as a list or stacked charts. */
    {"stock_quotes",    {240, 120, CBW,       CBH},       "sensor"},
    {"car_widget",      {200, 200, 480,       560},       "sensor"},
    {"wallbox_evcc", {320, 360, CBW, CBH}, "select"},
    {"bambu_printer",   {320, 320, CBW,       CBH},       "sensor"},
    {"todo_list",       {220, 200, 640,       640},       "todo"},
    {"media_player",    {260, 220, CBW,       CBH},       "media_player"},
    {"sonos_favorites", {280, 240, CBW,       CBH},       "media_player"},
    {"cover_tile",      {240,         80,         CBW,        200},       "cover"},
    {"fan_tile",        {240,         80,         CBW,        CBH},       "fan"},
    {"contact_tile",    {180,         70,         CBW,        160},       "binary_sensor"},
    {"camera_tile",     {160,         120,        CBW,        CBH},       "camera|image"},
    {"climate_tile",    {240,         180,        480,        480},       "climate"},
    {"scene_tile",      {160,         70,         CBW,        160},       "scene|script|button|input_button|lock"},
    {"select_tile",     {160,         80,         CBW,        200},       "select|input_select"},
    {"datetime_tile",   {180, 90, CBW,       220},       "input_datetime|time|date|datetime"},
    {"lock_tile",       {180,         80,         CBW,        200},       "lock"},
    {"alarm_tile",      {180,         80,         CBW,        200},       "alarm_control_panel"},
    /* Rings above a 24 h timeline: scales from a 300x200 card up to a full page. entity_id = grid import counter. */
    {"energy_timeline", {300,         200,        CBW,        CBH},       "sensor"},
    {"binary_tile",     {160,         70,         CBW,        160},       "binary_sensor|event"},
    {"room_tile",       {220,         120,        640,        320},       "*"},
    {"vacuum_tile",     {240, 220, CBW,       CBH},       "vacuum"},
    {"roborock_tile",   {240, 220, CBW,       CBH},       "vacuum"}, /* legacy name of the vacuum tile */
};

/* Looks whose shape needs other limits than their type's (not widget types of their own). */
static const struct {
    const char *type;
    const char *style_variant;
    widget_size_limits_t limits;
} s_variant_sizes[] = {
    /* type    style_variant  min_w min_h max_w max_h */
    {"gauge", "horizontal",  {100,  70,   CBW,  CBH}},
};

static const widget_type_info_t *find_type(const char *type)
{
    if (type == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_types) / sizeof(s_types[0]); i++) {
        if (strcmp(s_types[i].type, type) == 0) {
            return &s_types[i];
        }
    }
    return NULL;
}

bool widget_type_known(const char *type)
{
    return find_type(type) != NULL;
}

bool widget_type_supports_name(const char *type)
{
    return find_type(type) != NULL;
}

bool widget_show_name(const cJSON *widget)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(widget, "show_name");
    if (cJSON_IsBool(value)) return cJSON_IsTrue(value);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(widget, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "sensor") == 0) {
        value = cJSON_GetObjectItemCaseSensitive(widget, "sensor_show_label");
        if (cJSON_IsBool(value)) return cJSON_IsTrue(value);
    }
    /* The weather tiles show the weather, not the place's name: more room for the forecast. */
    if (cJSON_IsString(type) &&
        (strcmp(type->valuestring, "weather_tile") == 0 || strcmp(type->valuestring, "weather_3day") == 0)) {
        return false;
    }
    return true;
}

widget_size_limits_t widget_type_size_limits(const char *type)
{
    widget_size_limits_t limits = {60, 60, CBW, CBH};
    const widget_type_info_t *info = find_type(type);
    if (info != NULL) {
        limits = info->limits;
    }
    if (limits.max_w > CBW) {
        limits.max_w = CBW;
    }
    if (limits.max_h > CBH) {
        limits.max_h = CBH;
    }
    return limits;
}

widget_size_limits_t widget_size_limits_for(const char *type, const char *style_variant)
{
    for (size_t i = 0; type != NULL && style_variant != NULL && i < sizeof(s_variant_sizes) / sizeof(s_variant_sizes[0]); i++) {
        if (strcmp(s_variant_sizes[i].type, type) == 0 && strcmp(s_variant_sizes[i].style_variant, style_variant) == 0) {
            return s_variant_sizes[i].limits;
        }
    }
    return widget_type_size_limits(type);
}

bool widget_type_entity_domain_ok(const char *type, const char *entity_id)
{
    if (type == NULL || entity_id == NULL) {
        return false;
    }
    const widget_type_info_t *info = find_type(type);
    if (info == NULL || strcmp(info->domains, "*") == 0) {
        return true; /* an unknown type is reported separately; "*" takes any entity */
    }
    const char *p = info->domains;
    while (*p != '\0') {
        const char *end = strchr(p, '|');
        const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (strncmp(entity_id, p, len) == 0 && entity_id[len] == '.') {
            return true;
        }
        p += len + (end != NULL ? 1 : 0);
    }
    return false;
}

void widget_type_domain_hint(const char *type, char *out, unsigned out_len)
{
    const widget_type_info_t *info = find_type(type);
    if (out == NULL || out_len == 0) {
        return;
    }
    if (info == NULL) {
        snprintf(out, out_len, "?.*");
        return;
    }
    size_t n = 0;
    const char *p = info->domains;
    while (*p != '\0' && n + 1 < out_len) {
        const char *end = strchr(p, '|');
        const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        n += (size_t)snprintf(out + n, out_len - n, "%s%.*s.*", n > 0 ? " or " : "", (int)len, p);
        p += len + (end != NULL ? 1 : 0);
    }
}
