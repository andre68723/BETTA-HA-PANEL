/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * PNG icon table for the weather tile. Firmware uses embedded binaries; the
 * desktop simulator loads the same PNG files from disk.
 */
#include "ui/assets/weather/weather_icons.h"

#include <stdint.h>
#include <string.h>
#if defined(BETTA_SIMULATOR)
#include <stdio.h>
#include <stdlib.h>
#endif

/* One row per icon: id name (file name part) and enum constant. */
#define WX_ICON_LIST(WX_ICON) \
    WX_ICON(clear_day, CLEAR_DAY) \
    WX_ICON(clear_night, CLEAR_NIGHT) \
    WX_ICON(partly_cloudy_day, PARTLY_CLOUDY_DAY) \
    WX_ICON(partly_cloudy_night, PARTLY_CLOUDY_NIGHT) \
    WX_ICON(overcast, OVERCAST) \
    WX_ICON(fog, FOG) \
    WX_ICON(hail, HAIL) \
    WX_ICON(thunderstorms, THUNDERSTORMS) \
    WX_ICON(thunderstorms_rain, THUNDERSTORMS_RAIN) \
    WX_ICON(rain, RAIN) \
    WX_ICON(extreme_rain, EXTREME_RAIN) \
    WX_ICON(snow, SNOW) \
    WX_ICON(sleet, SLEET) \
    WX_ICON(wind, WIND) \
    WX_ICON(extreme, EXTREME)

#if !defined(BETTA_SIMULATOR)
#define WX_ICON(name, id) \
    extern const uint8_t _binary_wxicon_##name##_64_png_start[] asm("_binary_wxicon_" #name "_64_png_start"); \
    extern const uint8_t _binary_wxicon_##name##_64_png_end[] asm("_binary_wxicon_" #name "_64_png_end"); \
    extern const uint8_t _binary_wxicon_##name##_112_png_start[] asm("_binary_wxicon_" #name "_112_png_start"); \
    extern const uint8_t _binary_wxicon_##name##_112_png_end[] asm("_binary_wxicon_" #name "_112_png_end");
WX_ICON_LIST(WX_ICON)
#undef WX_ICON
#endif

static lv_image_dsc_t s_icons[WEATHER_ICON_COUNT][2]; /* [id][0] small, [id][1] large */
static bool s_icons_ready = false;

#if defined(BETTA_SIMULATOR)
static void wx_set_file(lv_image_dsc_t *dsc, const char *name, int px)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/wxicon_%s_%d.png", SIM_WEATHER_ICON_DIR, name, px);
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Cannot open weather icon %s\n", path);
        if (file != NULL) fclose(file);
        return;
    }
    long size = ftell(file);
    if (size <= 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return;
    }
    uint8_t *data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return;
    }
    fclose(file);
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.cf = LV_COLOR_FORMAT_RAW;
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.w = (uint32_t)px;
    dsc->header.h = (uint32_t)px;
    dsc->data = data;
    dsc->data_size = (uint32_t)size;
}
#else
static void wx_set(lv_image_dsc_t *dsc, const uint8_t *start, const uint8_t *end, int px)
{
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.cf = LV_COLOR_FORMAT_RAW;
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.w = (uint32_t)px;
    dsc->header.h = (uint32_t)px;
    dsc->data = start;
    dsc->data_size = (uint32_t)(end - start);
}
#endif

static void wx_init(void)
{
    if (s_icons_ready) {
        return;
    }
#if defined(BETTA_SIMULATOR)
#define WX_ICON(name, id) \
    wx_set_file(&s_icons[WEATHER_ICON_##id][0], #name, WEATHER_ICON_SMALL_PX); \
    wx_set_file(&s_icons[WEATHER_ICON_##id][1], #name, WEATHER_ICON_LARGE_PX);
#else
#define WX_ICON(name, id) \
    wx_set(&s_icons[WEATHER_ICON_##id][0], _binary_wxicon_##name##_64_png_start, _binary_wxicon_##name##_64_png_end, \
        WEATHER_ICON_SMALL_PX); \
    wx_set(&s_icons[WEATHER_ICON_##id][1], _binary_wxicon_##name##_112_png_start, _binary_wxicon_##name##_112_png_end, \
        WEATHER_ICON_LARGE_PX);
#endif
    WX_ICON_LIST(WX_ICON)
#undef WX_ICON
    s_icons_ready = true;
}

const lv_image_dsc_t *weather_icon_image(weather_icon_id_t id, bool large)
{
    if (id <= WEATHER_ICON_NONE || id >= WEATHER_ICON_COUNT) {
        return NULL;
    }
    wx_init();
    const lv_image_dsc_t *image = &s_icons[id][large ? 1 : 0];
    return image->data != NULL ? image : NULL;
}

static bool wx_has(const char *key, const char *token)
{
    return strstr(key, token) != NULL;
}

weather_icon_id_t weather_icon_from_condition(const char *key, bool night)
{
    if (key == NULL || key[0] == '\0') {
        return WEATHER_ICON_NONE;
    }

    /* Home Assistant's conditions (exact). */
    if (strcmp(key, "sunny") == 0 || strcmp(key, "clear") == 0 || strcmp(key, "clear-day") == 0) {
        return night ? WEATHER_ICON_CLEAR_NIGHT : WEATHER_ICON_CLEAR_DAY;
    }
    if (strcmp(key, "clear-night") == 0) {
        return WEATHER_ICON_CLEAR_NIGHT;
    }
    if (strcmp(key, "partlycloudy") == 0 || strcmp(key, "partly-cloudy") == 0) {
        return night ? WEATHER_ICON_PARTLY_CLOUDY_NIGHT : WEATHER_ICON_PARTLY_CLOUDY_DAY;
    }
    if (strcmp(key, "cloudy") == 0) {
        return WEATHER_ICON_OVERCAST;
    }
    if (strcmp(key, "fog") == 0) {
        return WEATHER_ICON_FOG;
    }
    if (strcmp(key, "hail") == 0) {
        return WEATHER_ICON_HAIL;
    }
    if (strcmp(key, "lightning") == 0) {
        return WEATHER_ICON_THUNDERSTORMS;
    }
    if (strcmp(key, "lightning-rainy") == 0) {
        return WEATHER_ICON_THUNDERSTORMS_RAIN;
    }
    if (strcmp(key, "rainy") == 0) {
        return WEATHER_ICON_RAIN;
    }
    if (strcmp(key, "pouring") == 0) {
        return WEATHER_ICON_EXTREME_RAIN;
    }
    if (strcmp(key, "snowy") == 0) {
        return WEATHER_ICON_SNOW;
    }
    if (strcmp(key, "snowy-rainy") == 0) {
        return WEATHER_ICON_SLEET;
    }
    if (strcmp(key, "windy") == 0 || strcmp(key, "windy-variant") == 0) {
        return WEATHER_ICON_WIND;
    }
    if (strcmp(key, "exceptional") == 0) {
        return WEATHER_ICON_EXTREME;
    }

    /* Spellings of other providers, matched loosely like the MDI icon mapping. */
    if (wx_has(key, "lightning") || wx_has(key, "thunder")) {
        return wx_has(key, "rain") ? WEATHER_ICON_THUNDERSTORMS_RAIN : WEATHER_ICON_THUNDERSTORMS;
    }
    if (wx_has(key, "sleet")) {
        return WEATHER_ICON_SLEET;
    }
    if (wx_has(key, "snow")) {
        return WEATHER_ICON_SNOW;
    }
    if (wx_has(key, "pouring")) {
        return WEATHER_ICON_EXTREME_RAIN;
    }
    if (wx_has(key, "rain") || wx_has(key, "drizzle") || wx_has(key, "shower")) {
        return WEATHER_ICON_RAIN;
    }
    if (wx_has(key, "partly")) {
        return night ? WEATHER_ICON_PARTLY_CLOUDY_NIGHT : WEATHER_ICON_PARTLY_CLOUDY_DAY;
    }
    if (wx_has(key, "fog") || wx_has(key, "mist") || wx_has(key, "haze") || wx_has(key, "smoke")) {
        return WEATHER_ICON_FOG;
    }
    if (wx_has(key, "wind")) {
        return WEATHER_ICON_WIND;
    }
    if (wx_has(key, "cloud") || wx_has(key, "overcast")) {
        return WEATHER_ICON_OVERCAST;
    }
    if (wx_has(key, "night")) {
        return WEATHER_ICON_CLEAR_NIGHT;
    }
    if (wx_has(key, "sunny") || wx_has(key, "clear")) {
        return night ? WEATHER_ICON_CLEAR_NIGHT : WEATHER_ICON_CLEAR_DAY;
    }
    return WEATHER_ICON_NONE;
}
