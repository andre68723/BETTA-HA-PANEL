/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Home Assistant attribute compaction and forecast extraction.
 * No connection, task, or model state is accessed here.
 */
#include "ha/ha_attr_serialize.h"
#include "ha/ha_control_options.h"
#include "ha/ha_price_series.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"

static const int HA_WEATHER_COMPACT_FORECAST_MAX_ITEMS = 6;

static bool ha_client_copy_attr_dup(cJSON *dst_obj, const char *dst_key, cJSON *src_obj, const char *src_key)
{
    if (!cJSON_IsObject(dst_obj) || dst_key == NULL || !cJSON_IsObject(src_obj) || src_key == NULL) {
        return false;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(src_obj, src_key);
    if (item == NULL) {
        return false;
    }

    cJSON *dup = cJSON_Duplicate(item, true);
    if (dup == NULL) {
        return false;
    }

    cJSON_AddItemToObject(dst_obj, dst_key, dup);
    return true;
}

static cJSON *ha_client_build_compact_forecast_array(cJSON *src_forecast)
{
    if (!cJSON_IsArray(src_forecast)) {
        return NULL;
    }

    cJSON *dst_forecast = cJSON_CreateArray();
    if (dst_forecast == NULL) {
        return NULL;
    }

    int count = cJSON_GetArraySize(src_forecast);
    for (int i = 0; i < count && i < HA_WEATHER_COMPACT_FORECAST_MAX_ITEMS; i++) {
        cJSON *src_item = cJSON_GetArrayItem(src_forecast, i);
        if (!cJSON_IsObject(src_item)) {
            continue;
        }

        cJSON *dst_item = cJSON_CreateObject();
        if (dst_item == NULL) {
            continue;
        }

        bool copied = false;
        bool has_datetime = ha_client_copy_attr_dup(dst_item, "datetime", src_item, "datetime");
        if (!has_datetime) {
            has_datetime = ha_client_copy_attr_dup(dst_item, "datetime", src_item, "date");
        }
        copied |= has_datetime;
        copied |= ha_client_copy_attr_dup(dst_item, "condition", src_item, "condition");

        if (!ha_client_copy_attr_dup(dst_item, "temperature", src_item, "temperature")) {
            copied |= ha_client_copy_attr_dup(dst_item, "temperature", src_item, "native_temperature");
        } else {
            copied = true;
        }
        if (!ha_client_copy_attr_dup(dst_item, "templow", src_item, "templow")) {
            copied |= ha_client_copy_attr_dup(dst_item, "templow", src_item, "native_templow");
        } else {
            copied = true;
        }
        if (!ha_client_copy_attr_dup(dst_item, "precipitation", src_item, "precipitation")) {
            (void)ha_client_copy_attr_dup(dst_item, "precipitation", src_item, "native_precipitation");
        }
        (void)ha_client_copy_attr_dup(dst_item, "precipitation_probability", src_item, "precipitation_probability");

        if (copied) {
            cJSON_AddItemToArray(dst_forecast, dst_item);
        } else {
            cJSON_Delete(dst_item);
        }
    }

    if (cJSON_GetArraySize(dst_forecast) == 0) {
        cJSON_Delete(dst_forecast);
        return NULL;
    }

    return dst_forecast;
}

static void ha_client_compact_weather_forecast(cJSON *dst_attrs, cJSON *src_attrs)
{
    if (!cJSON_IsObject(dst_attrs) || !cJSON_IsObject(src_attrs)) {
        return;
    }

    cJSON *src_forecast = cJSON_GetObjectItemCaseSensitive(src_attrs, "forecast");
    if (!cJSON_IsArray(src_forecast)) {
        src_forecast = cJSON_GetObjectItemCaseSensitive(src_attrs, "forecast_daily");
    }
    if (!cJSON_IsArray(src_forecast)) {
        return;
    }

    cJSON *dst_forecast = ha_client_build_compact_forecast_array(src_forecast);
    if (dst_forecast != NULL) {
        cJSON_AddItemToObject(dst_attrs, "forecast", dst_forecast);
    }
}

static cJSON *ha_client_find_compact_forecast_in_entity_obj(cJSON *entity_obj)
{
    if (!cJSON_IsObject(entity_obj)) {
        return NULL;
    }

    cJSON *forecast = cJSON_GetObjectItemCaseSensitive(entity_obj, "forecast");
    cJSON *compact = ha_client_build_compact_forecast_array(forecast);
    if (compact != NULL) {
        return compact;
    }

    cJSON *forecast_daily = cJSON_GetObjectItemCaseSensitive(entity_obj, "forecast_daily");
    return ha_client_build_compact_forecast_array(forecast_daily);
}

static cJSON *ha_client_find_compact_forecast_in_container(cJSON *container, const char *entity_id)
{
    if (!cJSON_IsObject(container) || entity_id == NULL || entity_id[0] == '\0') {
        return NULL;
    }

    cJSON *compact =
        ha_client_find_compact_forecast_in_entity_obj(cJSON_GetObjectItemCaseSensitive(container, entity_id));
    if (compact != NULL) {
        return compact;
    }

    cJSON *service_response = cJSON_GetObjectItemCaseSensitive(container, "service_response");
    if (cJSON_IsObject(service_response)) {
        compact = ha_client_find_compact_forecast_in_entity_obj(
            cJSON_GetObjectItemCaseSensitive(service_response, entity_id));
        if (compact != NULL) {
            return compact;
        }
    }

    cJSON *response = cJSON_GetObjectItemCaseSensitive(container, "response");
    if (cJSON_IsObject(response)) {
        compact = ha_client_find_compact_forecast_in_container(response, entity_id);
        if (compact != NULL) {
            return compact;
        }
    }

    return NULL;
}

static cJSON *ha_client_find_compact_forecast_recursive(cJSON *node, int depth)
{
    if (node == NULL || depth > 10) {
        return NULL;
    }

    if (cJSON_IsObject(node)) {
        cJSON *compact = ha_client_find_compact_forecast_in_entity_obj(node);
        if (compact != NULL) {
            return compact;
        }

        for (cJSON *child = node->child; child != NULL; child = child->next) {
            compact = ha_client_find_compact_forecast_recursive(child, depth + 1);
            if (compact != NULL) {
                return compact;
            }
        }
        return NULL;
    }

    if (cJSON_IsArray(node)) {
        int n = cJSON_GetArraySize(node);
        for (int i = 0; i < n; i++) {
            cJSON *compact = ha_client_find_compact_forecast_recursive(cJSON_GetArrayItem(node, i), depth + 1);
            if (compact != NULL) {
                return compact;
            }
        }
    }

    return NULL;
}

cJSON *ha_client_find_compact_weather_forecast(cJSON *node, const char *entity_id)
{
    cJSON *compact = ha_client_find_compact_forecast_in_container(node, entity_id);
    if (compact != NULL) {
        return compact;
    }

    return ha_client_find_compact_forecast_recursive(node, 0);
}

bool ha_client_serialize_weather_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    bool has_temperature = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "temperature");
    if (!has_temperature) {
        has_temperature = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "native_temperature");
    }
    any |= has_temperature;
    any |= ha_client_copy_attr_dup(compact, "current_temperature", src_attrs, "current_temperature");
    any |= ha_client_copy_attr_dup(compact, "native_temperature", src_attrs, "native_temperature");
    bool has_temperature_unit = ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "temperature_unit");
    if (!has_temperature_unit) {
        has_temperature_unit = ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "native_temperature_unit");
    }
    any |= has_temperature_unit;
    any |= ha_client_copy_attr_dup(compact, "native_temperature_unit", src_attrs, "native_temperature_unit");
    if (!ha_client_copy_attr_dup(compact, "precipitation_unit", src_attrs, "precipitation_unit")) {
        (void)ha_client_copy_attr_dup(compact, "precipitation_unit", src_attrs, "native_precipitation_unit");
    }
    any |= ha_client_copy_attr_dup(compact, "humidity", src_attrs, "humidity");
#if APP_HA_WEATHER_DETAILS
    /* Current conditions for the weather detail popup. */
    (void)ha_client_copy_attr_dup(compact, "pressure", src_attrs, "pressure");
    (void)ha_client_copy_attr_dup(compact, "pressure_unit", src_attrs, "pressure_unit");
    (void)ha_client_copy_attr_dup(compact, "wind_speed", src_attrs, "wind_speed");
    (void)ha_client_copy_attr_dup(compact, "wind_speed_unit", src_attrs, "wind_speed_unit");
    (void)ha_client_copy_attr_dup(compact, "wind_bearing", src_attrs, "wind_bearing");
    (void)ha_client_copy_attr_dup(compact, "visibility", src_attrs, "visibility");
    (void)ha_client_copy_attr_dup(compact, "visibility_unit", src_attrs, "visibility_unit");
#endif

    ha_client_compact_weather_forecast(compact, src_attrs);
    if (cJSON_GetObjectItemCaseSensitive(compact, "forecast") != NULL) {
        any = true;
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

bool ha_client_serialize_climate_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    bool has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "temperature");
    if (!has_target_temp) {
        has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "target_temperature");
    }
    if (!has_target_temp) {
        has_target_temp = ha_client_copy_attr_dup(compact, "temperature", src_attrs, "target_temp");
    }
    any |= has_target_temp;

    any |= ha_client_copy_attr_dup(compact, "current_temperature", src_attrs, "current_temperature");
    any |= ha_client_copy_attr_dup(compact, "temperature_unit", src_attrs, "temperature_unit");
    any |= ha_client_copy_attr_dup(compact, "hvac_action", src_attrs, "hvac_action");
    any |= ha_client_copy_attr_dup(compact, "hvac_mode", src_attrs, "hvac_mode");
    any |= ha_client_copy_attr_dup(compact, "preset_mode", src_attrs, "preset_mode");
    any |= ha_client_copy_attr_dup(compact, "min_temp", src_attrs, "min_temp");
    any |= ha_client_copy_attr_dup(compact, "max_temp", src_attrs, "max_temp");
    any |= ha_client_copy_attr_dup(compact, "target_temp_low", src_attrs, "target_temp_low");
    any |= ha_client_copy_attr_dup(compact, "target_temp_high", src_attrs, "target_temp_high");
    any |= ha_client_copy_attr_dup(compact, "humidity", src_attrs, "humidity");
    /* Mode lists and fan mode for the climate tile. */
    (void)ha_client_copy_attr_dup(compact, "hvac_modes", src_attrs, "hvac_modes");
    (void)ha_client_copy_attr_dup(compact, "fan_mode", src_attrs, "fan_mode");
    (void)ha_client_copy_attr_dup(compact, "fan_modes", src_attrs, "fan_modes");
    (void)ha_client_copy_attr_dup(compact, "swing_mode", src_attrs, "swing_mode");
    (void)ha_client_copy_attr_dup(compact, "swing_modes", src_attrs, "swing_modes");

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

bool ha_client_serialize_media_player_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "supported_features", src_attrs, "supported_features"); /* the room card gates Play/Pause */
    cJSON *volume_level = cJSON_GetObjectItemCaseSensitive(src_attrs, "volume_level");
    if (cJSON_IsNumber(volume_level)) {
        double volume = volume_level->valuedouble;
        if (volume < 0.0) {
            volume = 0.0;
        } else if (volume > 1.0) {
            volume = 1.0;
        }
        cJSON_AddNumberToObject(compact, "volume_level", volume);
        any = true;
    }

    cJSON *is_volume_muted = cJSON_GetObjectItemCaseSensitive(src_attrs, "is_volume_muted");
    if (cJSON_IsBool(is_volume_muted)) {
        cJSON_AddBoolToObject(compact, "is_volume_muted", cJSON_IsTrue(is_volume_muted));
        any = true;
    }

    cJSON *shuffle = cJSON_GetObjectItemCaseSensitive(src_attrs, "shuffle");
    if (cJSON_IsBool(shuffle)) {
        cJSON_AddBoolToObject(compact, "shuffle", cJSON_IsTrue(shuffle));
        any = true;
    }

    cJSON *repeat = cJSON_GetObjectItemCaseSensitive(src_attrs, "repeat");
    if (cJSON_IsString(repeat) && repeat->valuestring != NULL && repeat->valuestring[0] != '\0') {
        cJSON_AddStringToObject(compact, "repeat", repeat->valuestring);
        any = true;
    }

    cJSON *media_title = cJSON_GetObjectItemCaseSensitive(src_attrs, "media_title");
    if (cJSON_IsString(media_title) && media_title->valuestring != NULL && media_title->valuestring[0] != '\0') {
        cJSON_AddStringToObject(compact, "media_title", media_title->valuestring);
        any = true;
    }

    cJSON *media_artist = cJSON_GetObjectItemCaseSensitive(src_attrs, "media_artist");
    if (cJSON_IsString(media_artist) && media_artist->valuestring != NULL && media_artist->valuestring[0] != '\0') {
        cJSON_AddStringToObject(compact, "media_artist", media_artist->valuestring);
        any = true;
    }

    cJSON *media_duration = cJSON_GetObjectItemCaseSensitive(src_attrs, "media_duration");
    if (cJSON_IsNumber(media_duration)) {
        cJSON_AddNumberToObject(compact, "media_duration", media_duration->valuedouble);
        any = true;
    }

    cJSON *media_position = cJSON_GetObjectItemCaseSensitive(src_attrs, "media_position");
    if (cJSON_IsNumber(media_position)) {
        cJSON_AddNumberToObject(compact, "media_position", media_position->valuedouble);
        any = true;
    }

    cJSON *media_position_updated_at = cJSON_GetObjectItemCaseSensitive(src_attrs, "media_position_updated_at");
    if (cJSON_IsString(media_position_updated_at) && media_position_updated_at->valuestring != NULL &&
        media_position_updated_at->valuestring[0] != '\0') {
        cJSON_AddStringToObject(compact, "media_position_updated_at", media_position_updated_at->valuestring);
        any = true;
    }

    cJSON *entity_picture = cJSON_GetObjectItemCaseSensitive(src_attrs, "entity_picture");
    if (cJSON_IsString(entity_picture) && entity_picture->valuestring != NULL &&
        entity_picture->valuestring[0] != '\0') {
        cJSON_AddStringToObject(compact, "entity_picture", entity_picture->valuestring);
        any = true;
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

bool ha_client_serialize_sonos_favorites_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *src_items = cJSON_GetObjectItemCaseSensitive(src_attrs, "items");
    if (!cJSON_IsObject(src_items)) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }
    cJSON *items = cJSON_AddObjectToObject(compact, "items");
    if (items == NULL) {
        cJSON_Delete(compact);
        return false;
    }

    bool any = false;
    bool fits = false;
    for (cJSON *item = src_items->child; item != NULL; item = item->next) {
        if (item->string == NULL || !cJSON_IsString(item) || item->valuestring == NULL ||
            item->string[0] == '\0' || item->valuestring[0] == '\0') {
            continue;
        }

        if (cJSON_AddStringToObject(items, item->string, item->valuestring) == NULL) {
            break;
        }

        char *compact_json = cJSON_PrintUnformatted(compact);
        if (compact_json == NULL) {
            cJSON_DeleteItemFromObjectCaseSensitive(items, item->string);
            break;
        }

        size_t len = strlen(compact_json);
        if (len < out_json_size) {
            memcpy(out_json, compact_json, len + 1U);
            fits = true;
            any = true;
            cJSON_free(compact_json);
        } else {
            cJSON_free(compact_json);
            cJSON_DeleteItemFromObjectCaseSensitive(items, item->string);
            break;
        }
    }

    cJSON_Delete(compact);
    return any && fits;
}

/* ---- DWD weather report (sensor "<station>_wetterbericht") ------------------
 * The dwd_weather integration puts the whole regional report (about 2000
 * characters of markdown) into the attribute "data". The panel keeps the
 * summary sentence and one short text per weekday: {"s":"...","d":{"6":"..."}},
 * keyed by weekday number (0 = Sunday). */
static const char *const k_report_weekdays[7] = {
    "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};

/* Finds the end of the paragraph starting at `s`: a line break followed by another one, with spaces
 * and carriage returns allowed in between ("\r\n \r\n" works too). `*next` gets the start of the
 * following text. Returns NULL if this is the last paragraph. */
static const char *ha_client_report_paragraph_end(const char *s, const char **next)
{
    for (const char *c = s; *c != '\0'; c++) {
        if (*c != '\n') {
            continue;
        }
        const char *j = c + 1;
        while (*j == ' ' || *j == '\r') {
            j++;
        }
        if (*j == '\n') {
            *next = j + 1;
            return c;
        }
    }
    return NULL;
}

bool ha_client_attrs_is_weather_report(cJSON *attrs)
{
    cJSON *data = cJSON_IsObject(attrs) ? cJSON_GetObjectItemCaseSensitive(attrs, "data") : NULL;
    return cJSON_IsString(data) && data->valuestring != NULL && strlen(data->valuestring) > 400 &&
           strstr(data->valuestring, "Wetter") != NULL;
}

/* Copies whole sentences (up to `cap` bytes) from a markdown paragraph. */
static void ha_client_report_take_sentences(const char *src, size_t len, size_t cap, char *out, size_t out_size)
{
    char buf[512];
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(buf) - 1; i++) {
        char c = src[i];
        if (c == '*') {
            continue;
        }
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
        if (c == ' ' && (n == 0 || buf[n - 1] == ' ')) {
            continue;
        }
        buf[n++] = c;
    }
    while (n > 0 && buf[n - 1] == ' ') {
        n--;
    }
    buf[n] = '\0';
    if (cap >= out_size) {
        cap = out_size - 1;
    }
    if (n <= cap) {
        memcpy(out, buf, n + 1);
        return;
    }
    size_t cut = 0;
    for (size_t i = 1; i < cap && i + 1 < n; i++) {
        if (buf[i] == '.' && buf[i + 1] == ' ') {
            cut = i + 1;
        }
    }
    if (cut > 0) {
        memcpy(out, buf, cut);
        out[cut] = '\0';
        return;
    }
    size_t sp = 0;
    for (size_t i = 1; i + 4 < cap; i++) {
        if (buf[i] == ' ') {
            sp = i;
        }
    }
    if (sp == 0) {
        sp = cap > 4 ? cap - 4 : cap;
    }
    memcpy(out, buf, sp);
    memcpy(out + sp, "...", 4);
}

bool ha_client_serialize_weather_report_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    cJSON *data_item = cJSON_GetObjectItemCaseSensitive(src_attrs, "data");
    if (!cJSON_IsString(data_item) || data_item->valuestring == NULL || out_json == NULL || out_json_size < 64) {
        return false;
    }
    const char *data = data_item->valuestring;

    /* Summary: the first paragraph after the "issued ... Uhr**" block. */
    char summary[200] = {0};
    const char *p = strstr(data, "Uhr**");
    p = p != NULL ? p + 5 : data;
    while (*p == '\n' || *p == '\r' || *p == ' ') {
        p++;
    }
    const char *sum_next = NULL;
    const char *sum_end = ha_client_report_paragraph_end(p, &sum_next);
    size_t sum_len = sum_end != NULL ? (size_t)(sum_end - p) : strlen(p);
    ha_client_report_take_sentences(p, sum_len, 170, summary, sizeof(summary));

    /* One paragraph per day from "Detaillierter Wetterablauf": "Heute ..." and "Am <Wochentag> ...". */
    time_t now = time(NULL);
    struct tm local_now;
    localtime_r(&now, &local_now);
    static char days[7][224];
    memset(days, 0, sizeof(days));
    const char *paragraph_text[7] = {0};
    size_t paragraph_len[7] = {0};
    const char *det = strstr(data, "Detaillierter Wetterablauf");
    if (det != NULL) {
        const char *q = strchr(det, '\n');
        q = q != NULL ? q + 1 : det;
        while (*q != '\0') {
            while (*q == '\n' || *q == '\r' || *q == ' ') {
                q++;
            }
            if (*q == '\0') {
                break;
            }
            const char *para = q;
            const char *next = NULL;
            const char *end = ha_client_report_paragraph_end(q, &next);
            size_t len = end != NULL ? (size_t)(end - q) : strlen(q);
            q = end != NULL ? next : q + len;
            int wd = -1;
            if (strncmp(para, "Heute", 5) == 0) {
                wd = local_now.tm_wday;
            } else if (strncmp(para, "Am ", 3) == 0) {
                for (int w = 0; w < 7; w++) {
                    if (strncmp(para + 3, k_report_weekdays[w], strlen(k_report_weekdays[w])) == 0) {
                        wd = w;
                        break;
                    }
                }
            }
            if (wd >= 0 && paragraph_text[wd] == NULL) {
                paragraph_text[wd] = para;
                paragraph_len[wd] = len;
            }
        }
    }

    /* Shrink the per-day text until the JSON fits the attribute buffer. */
    for (size_t cap = 200; cap >= 60; cap -= 30) {
        cJSON *root = cJSON_CreateObject();
        cJSON *day_obj = cJSON_CreateObject();
        if (root == NULL || day_obj == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(day_obj);
            return false;
        }
        cJSON_AddStringToObject(root, "s", summary);
        for (int w = 0; w < 7; w++) {
            if (paragraph_text[w] == NULL) {
                continue;
            }
            ha_client_report_take_sentences(paragraph_text[w], paragraph_len[w], cap, days[w], sizeof(days[w]));
            char key[4];
            snprintf(key, sizeof(key), "%d", w);
            cJSON_AddStringToObject(day_obj, key, days[w]);
        }
        cJSON_AddItemToObject(root, "d", day_obj);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json == NULL) {
            return false;
        }
        size_t json_len = strlen(json);
        bool fits = json_len < out_json_size;
        if (fits) {
            memcpy(out_json, json, json_len + 1U);
        }
        cJSON_free(json);
        if (fits) {
            return true;
        }
    }
    return false;
}

bool ha_client_serialize_light_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "supported_color_modes", src_attrs, "supported_color_modes");
    any |= ha_client_copy_attr_dup(compact, "color_mode", src_attrs, "color_mode");
    any |= ha_client_copy_attr_dup(compact, "brightness", src_attrs, "brightness");
    any |= ha_client_copy_attr_dup(compact, "brightness_pct", src_attrs, "brightness_pct");
    any |= ha_client_copy_attr_dup(compact, "rgb_color", src_attrs, "rgb_color");
    any |= ha_client_copy_attr_dup(compact, "hs_color", src_attrs, "hs_color");
    any |= ha_client_copy_attr_dup(compact, "xy_color", src_attrs, "xy_color");
    any |= ha_client_copy_attr_dup(compact, "color_temp_kelvin", src_attrs, "color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "min_color_temp_kelvin", src_attrs, "min_color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "max_color_temp_kelvin", src_attrs, "max_color_temp_kelvin");
    any |= ha_client_copy_attr_dup(compact, "supported_features", src_attrs, "supported_features");

    const char *effect = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(src_attrs, "effect"));
    if (effect != NULL && strlen(effect) < HA_CONTROL_OPTION_LEN)
        any |= ha_client_copy_attr_dup(compact, "effect", src_attrs, "effect");
    ha_control_options_copy(compact, src_attrs, "effect_list");
    any |= cJSON_GetObjectItemCaseSensitive(compact, "effect_list") != NULL;

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    bool fits = ha_control_options_print(compact, "effect_list", out_json, out_json_size);
    cJSON_Delete(compact);
    return fits;
}

bool ha_client_serialize_vacuum_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) return false;
    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) return false;
    static const char *const keys[] = {"supported_features", "fan_speed"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(src_attrs, keys[i]));
        if (value != NULL && strlen(value) >= HA_CONTROL_OPTION_LEN) continue;
        (void)ha_client_copy_attr_dup(compact, keys[i], src_attrs, keys[i]);
    }
    ha_control_options_copy(compact, src_attrs, "fan_speed_list");
    bool fits = ha_control_options_print(compact, "fan_speed_list", out_json, out_json_size);
    cJSON_Delete(compact);
    return fits;
}

/* Only current_position and supported_features are read anywhere (w_cover.c, and w_slider.c for a
 * cover-domain slider); inventoried before adding this, see the truncation-plan development note. */
bool ha_client_serialize_cover_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "current_position", src_attrs, "current_position");
    any |= ha_client_copy_attr_dup(compact, "supported_features", src_attrs, "supported_features");

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

/* Fields read by w_fan.c: percentage, its step, oscillating, the preset mode and its list, and the
 * feature bitmask that gates which of those apply. Inventoried before adding this, see the
 * truncation-plan development note. */
bool ha_client_serialize_fan_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "percentage", src_attrs, "percentage");
    any |= ha_client_copy_attr_dup(compact, "percentage_step", src_attrs, "percentage_step");
    any |= ha_client_copy_attr_dup(compact, "oscillating", src_attrs, "oscillating");
    any |= ha_client_copy_attr_dup(compact, "preset_mode", src_attrs, "preset_mode");
    any |= ha_client_copy_attr_dup(compact, "preset_modes", src_attrs, "preset_modes");
    any |= ha_client_copy_attr_dup(compact, "supported_features", src_attrs, "supported_features");

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

/* Fields read by w_alarm.c: the feature bitmask that gates the arm modes, the code format (a string,
 * null when no code is needed) and whether arming needs the code. changed_by is not read. */
bool ha_client_serialize_alarm_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(src_attrs, "supported_features");
    if (cJSON_IsNumber(features)) {
        cJSON_AddNumberToObject(compact, "supported_features", features->valuedouble);
        any = true;
    }
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(src_attrs, "code_format");
    if (cJSON_IsString(format) && format->valuestring != NULL) {
        cJSON_AddStringToObject(compact, "code_format", format->valuestring);
        any = true;
    }
    const cJSON *arm_required = cJSON_GetObjectItemCaseSensitive(src_attrs, "code_arm_required");
    if (cJSON_IsBool(arm_required)) {
        cJSON_AddBoolToObject(compact, "code_arm_required", cJSON_IsTrue(arm_required));
        any = true;
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

bool ha_client_serialize_image_attrs_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size == 0) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    any |= ha_client_copy_attr_dup(compact, "entity_picture", src_attrs, "entity_picture");
    any |= ha_client_copy_attr_dup(compact, "entity_picture_local", src_attrs, "entity_picture_local");
    any |= ha_client_copy_attr_dup(compact, "access_token", src_attrs, "access_token");

    cJSON *calibration_points = cJSON_GetObjectItemCaseSensitive(src_attrs, "calibration_points");
    if (cJSON_IsArray(calibration_points)) {
        cJSON *dst_points = cJSON_CreateArray();
        if (dst_points != NULL) {
            cJSON *src_point = NULL;
            cJSON_ArrayForEach(src_point, calibration_points)
            {
                cJSON *vacuum = cJSON_GetObjectItemCaseSensitive(src_point, "vacuum");
                cJSON *map = cJSON_GetObjectItemCaseSensitive(src_point, "map");
                cJSON *vacuum_x = cJSON_IsObject(vacuum) ? cJSON_GetObjectItemCaseSensitive(vacuum, "x") : NULL;
                cJSON *vacuum_y = cJSON_IsObject(vacuum) ? cJSON_GetObjectItemCaseSensitive(vacuum, "y") : NULL;
                cJSON *map_x = cJSON_IsObject(map) ? cJSON_GetObjectItemCaseSensitive(map, "x") : NULL;
                cJSON *map_y = cJSON_IsObject(map) ? cJSON_GetObjectItemCaseSensitive(map, "y") : NULL;
                if (!cJSON_IsNumber(vacuum_x) || !cJSON_IsNumber(vacuum_y) ||
                    !cJSON_IsNumber(map_x) || !cJSON_IsNumber(map_y)) {
                    continue;
                }

                cJSON *dst_point = cJSON_CreateObject();
                cJSON *dst_vacuum = cJSON_CreateObject();
                cJSON *dst_map = cJSON_CreateObject();
                if (dst_point == NULL || dst_vacuum == NULL || dst_map == NULL) {
                    cJSON_Delete(dst_point);
                    cJSON_Delete(dst_vacuum);
                    cJSON_Delete(dst_map);
                    continue;
                }

                cJSON_AddNumberToObject(dst_vacuum, "x", vacuum_x->valuedouble);
                cJSON_AddNumberToObject(dst_vacuum, "y", vacuum_y->valuedouble);
                cJSON_AddNumberToObject(dst_map, "x", map_x->valuedouble);
                cJSON_AddNumberToObject(dst_map, "y", map_y->valuedouble);
                cJSON_AddItemToObject(dst_point, "vacuum", dst_vacuum);
                cJSON_AddItemToObject(dst_point, "map", dst_map);
                cJSON_AddItemToArray(dst_points, dst_point);
            }

            if (cJSON_GetArraySize(dst_points) > 0) {
                cJSON_AddItemToObject(compact, "calibration_points", dst_points);
                any = true;
            } else {
                cJSON_Delete(dst_points);
            }
        }
    }

    cJSON *rooms = cJSON_GetObjectItemCaseSensitive(src_attrs, "rooms");
    if (cJSON_IsObject(rooms)) {
        cJSON *dst_rooms = cJSON_CreateObject();
        if (dst_rooms != NULL) {
            for (cJSON *room = rooms->child; room != NULL; room = room->next) {
                if (room->string == NULL || room->string[0] == '\0' || !cJSON_IsObject(room)) {
                    continue;
                }

                cJSON *x0 = cJSON_GetObjectItemCaseSensitive(room, "x0");
                cJSON *y0 = cJSON_GetObjectItemCaseSensitive(room, "y0");
                cJSON *x1 = cJSON_GetObjectItemCaseSensitive(room, "x1");
                cJSON *y1 = cJSON_GetObjectItemCaseSensitive(room, "y1");
                if (!cJSON_IsNumber(x0) || !cJSON_IsNumber(y0) ||
                    !cJSON_IsNumber(x1) || !cJSON_IsNumber(y1)) {
                    continue;
                }

                cJSON *dst_room = cJSON_CreateObject();
                if (dst_room == NULL) {
                    continue;
                }
                cJSON_AddNumberToObject(dst_room, "x0", x0->valuedouble);
                cJSON_AddNumberToObject(dst_room, "y0", y0->valuedouble);
                cJSON_AddNumberToObject(dst_room, "x1", x1->valuedouble);
                cJSON_AddNumberToObject(dst_room, "y1", y1->valuedouble);

                cJSON *name = cJSON_GetObjectItemCaseSensitive(room, "name");
                if (cJSON_IsString(name) && name->valuestring != NULL && name->valuestring[0] != '\0') {
                    cJSON_AddStringToObject(dst_room, "name", name->valuestring);
                }

                cJSON *iot_name = cJSON_GetObjectItemCaseSensitive(room, "iot_name");
                if (cJSON_IsString(iot_name) && iot_name->valuestring != NULL && iot_name->valuestring[0] != '\0') {
                    cJSON_AddStringToObject(dst_room, "iot_name", iot_name->valuestring);
                }

                cJSON *room_name = cJSON_GetObjectItemCaseSensitive(room, "room_name");
                if (cJSON_IsString(room_name) && room_name->valuestring != NULL && room_name->valuestring[0] != '\0') {
                    cJSON_AddStringToObject(dst_room, "room_name", room_name->valuestring);
                }

                cJSON_AddItemToObject(dst_rooms, room->string, dst_room);
            }

            if (dst_rooms->child != NULL) {
                cJSON_AddItemToObject(compact, "rooms", dst_rooms);
                any = true;
            } else {
                cJSON_Delete(dst_rooms);
            }
        }
    }

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = (len < out_json_size);
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    }
    cJSON_free(compact_json);
    return fits;
}

cJSON *ha_client_extract_compact_forecast_from_attrs_json(const char *attrs_json)
{
    if (attrs_json == NULL || attrs_json[0] == '\0') {
        return NULL;
    }

    cJSON *attrs = cJSON_Parse(attrs_json);
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        return NULL;
    }

    cJSON *forecast = cJSON_GetObjectItemCaseSensitive(attrs, "forecast");
    if (!cJSON_IsArray(forecast)) {
        forecast = cJSON_GetObjectItemCaseSensitive(attrs, "forecast_daily");
    }

    cJSON *compact = ha_client_build_compact_forecast_array(forecast);
    cJSON_Delete(attrs);
    return compact;
}

bool ha_client_weather_attrs_has_forecast_json(const char *attrs_json)
{
    cJSON *compact = ha_client_extract_compact_forecast_from_attrs_json(attrs_json);
    if (compact == NULL) {
        return false;
    }
    cJSON_Delete(compact);
    return true;
}

bool ha_client_append_compact_forecast_to_attrs_json(char *attrs_json, size_t attrs_json_size, cJSON *forecast)
{
    if (attrs_json == NULL || attrs_json_size == 0 || !cJSON_IsArray(forecast)) {
        cJSON_Delete(forecast);
        return false;
    }

    cJSON *attrs = cJSON_Parse(attrs_json);
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        cJSON_Delete(forecast);
        return false;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "forecast");
    cJSON_AddItemToObject(attrs, "forecast", forecast);

    char *merged_json = cJSON_PrintUnformatted(attrs);
    cJSON_Delete(attrs);
    if (merged_json == NULL) {
        return false;
    }

    size_t len = strlen(merged_json);
    bool fits = (len < attrs_json_size);
    if (fits) {
        memcpy(attrs_json, merged_json, len + 1U);
    }
    cJSON_free(merged_json);
    return fits;
}

bool ha_client_attrs_is_price_series(cJSON *attrs)
{
    return cJSON_IsObject(attrs) && ha_price_series_detect(attrs);
}

/* A price list (the attribute `data` of ha_epex_spot, the Day Ahead Optimizer and the Tibber/Nord Pool template sensors) is far larger
 * than the model's attribute buffer: it becomes the regular grid of ha_price_series.c under "pf". If even the steps of a quarter-hour series
 * do not fit, whole hours are averaged (nothing is invented, a gap stays a gap). The scalars other widgets of the same entity read (a Sensor
 * tile shows the unit) follow in the order unit, device class, state class, name for as long as they fit. False when the list is not
 * representable exactly or does not fit even hourly; the caller then keeps only the generic metadata and the widget reports
 * "no usable prices". */
bool ha_client_serialize_price_series_compact(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    static const char *const keep[] = {"unit_of_measurement", "device_class", "state_class", "friendly_name"};
    if (!cJSON_IsObject(src_attrs) || out_json == NULL || out_json_size < 8) {
        return false;
    }
    ha_price_series_t *series = (ha_price_series_t *)calloc(1, sizeof(*series));
    ha_price_series_t *hourly = (ha_price_series_t *)calloc(1, sizeof(*hourly));
    char *pf = (char *)malloc(out_json_size);
    bool ok = false;
    if (series == NULL || hourly == NULL || pf == NULL) {
        goto done;
    }
    if (!ha_price_series_from_attrs(src_attrs, series)) {
        goto done;
    }
    if (!ha_price_series_to_json(series, pf, out_json_size)) {
        if (!ha_price_series_to_hourly(series, hourly) || !ha_price_series_to_json(hourly, pf, out_json_size)) {
            goto done;
        }
    }
    {
        /* pf is {"pf":{...}}: its member opens the output object. */
        const size_t pf_len = strlen(pf);
        if (pf_len < 3 || 1 + (pf_len - 2) + 1 >= out_json_size) {
            goto done;
        }
        size_t used = 0;
        out_json[used++] = '{';
        memcpy(out_json + used, pf + 1, pf_len - 2);
        used += pf_len - 2;
        for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
            cJSON *item = cJSON_GetObjectItemCaseSensitive(src_attrs, keep[i]);
            if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) > 48) {
                continue;
            }
            cJSON *quoted = cJSON_CreateString(item->valuestring);
            char *text = quoted != NULL ? cJSON_PrintUnformatted(quoted) : NULL;
            cJSON_Delete(quoted);
            if (text == NULL) {
                continue;
            }
            const size_t need = 1 + strlen(keep[i]) + 3 + strlen(text); /* ,"key":"value" */
            if (used + need + 2 <= out_json_size) {
                used += (size_t)snprintf(out_json + used, out_json_size - used, ",\"%s\":%s", keep[i], text);
            }
            cJSON_free(text);
        }
        out_json[used++] = '}';
        out_json[used] = '\0';
        ok = true;
    }
done:
    free(pf);
    free(hourly);
    free(series);
    return ok;
}
