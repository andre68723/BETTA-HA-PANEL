/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

static void weather_values_default(weather_values_t *values)
{
    if (values == NULL) {
        return;
    }
    memset(values, 0, sizeof(*values));
    values->humidity = -1;
    values->condition_key[0] = '\0';
    values->today_condition_key[0] = '\0';
    weather_copy_text(values->condition, sizeof(values->condition), "--");
    weather_copy_text(values->unit, sizeof(values->unit), "C");
    weather_copy_text(values->precip_unit, sizeof(values->precip_unit), "mm");
    values->wind_bearing = -1;
    values->today_precip_mm = -1.0f;
    values->today_precip_prob = -1;
    for (size_t i = 0; i < WEATHER_3DAY_MAX_FORECAST; i++) {
        values->forecast[i].precip_mm = -1.0f;
        values->forecast[i].precip_prob = -1;
        weather_copy_text(values->forecast[i].day, sizeof(values->forecast[i].day), "--");
        values->forecast[i].condition_key[0] = '\0';
        weather_copy_text(values->forecast[i].condition, sizeof(values->forecast[i].condition), "--");
    }
}

void weather_extract_values(const ha_state_t *state, bool want_forecast, weather_values_t *out)
{
    if (state == NULL || out == NULL) {
        return;
    }
    weather_values_default(out);
    weather_normalize_condition_key(state->state, out->condition_key, sizeof(out->condition_key));
    weather_humanize_condition(state->state, out->condition, sizeof(out->condition));

    const char *attrs_json = state->attributes_json;
    cJSON *attrs = cJSON_Parse(attrs_json);
    if (attrs != NULL) {
        cJSON *temperature = cJSON_GetObjectItemCaseSensitive(attrs, "temperature");
        cJSON *current_temperature = cJSON_GetObjectItemCaseSensitive(attrs, "current_temperature");
        cJSON *native_temperature = cJSON_GetObjectItemCaseSensitive(attrs, "native_temperature");
        if (weather_json_item_to_float(temperature, &out->temp) ||
            weather_json_item_to_float(current_temperature, &out->temp) ||
            weather_json_item_to_float(native_temperature, &out->temp)) {
            out->has_temp = true;
        }

        cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "temperature_unit");
        if (cJSON_IsString(unit) && unit->valuestring != NULL && unit->valuestring[0] != '\0') {
            weather_copy_text(out->unit, sizeof(out->unit), unit->valuestring);
        } else {
            cJSON *native_unit = cJSON_GetObjectItemCaseSensitive(attrs, "native_temperature_unit");
            if (cJSON_IsString(native_unit) && native_unit->valuestring != NULL && native_unit->valuestring[0] != '\0') {
                weather_copy_text(out->unit, sizeof(out->unit), native_unit->valuestring);
            }
        }

        cJSON *humidity = cJSON_GetObjectItemCaseSensitive(attrs, "humidity");
        weather_json_item_to_int(humidity, &out->humidity);

        if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "pressure"), &out->pressure)) {
            out->has_pressure = true;
            cJSON *u = cJSON_GetObjectItemCaseSensitive(attrs, "pressure_unit");
            if (cJSON_IsString(u) && u->valuestring != NULL) {
                weather_copy_text(out->pressure_unit, sizeof(out->pressure_unit), u->valuestring);
            }
        }
        if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "wind_speed"), &out->wind_speed)) {
            out->has_wind = true;
            cJSON *u = cJSON_GetObjectItemCaseSensitive(attrs, "wind_speed_unit");
            if (cJSON_IsString(u) && u->valuestring != NULL) {
                weather_copy_text(out->wind_unit, sizeof(out->wind_unit), u->valuestring);
            }
            float bearing = 0.0f;
            if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "wind_bearing"), &bearing)) {
                out->wind_bearing = (int)(bearing + 0.5f);
            }
        }
        if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "visibility"), &out->visibility)) {
            out->has_visibility = true;
            cJSON *u = cJSON_GetObjectItemCaseSensitive(attrs, "visibility_unit");
            if (cJSON_IsString(u) && u->valuestring != NULL) {
                weather_copy_text(out->visibility_unit, sizeof(out->visibility_unit), u->valuestring);
            }
        }
    }

    if (!out->has_temp && attrs_json != NULL && attrs_json[0] != '\0') {
        if (weather_extract_raw_number_attr(attrs_json, "temperature", &out->temp) ||
            weather_extract_raw_number_attr(attrs_json, "current_temperature", &out->temp) ||
            weather_extract_raw_number_attr(attrs_json, "native_temperature", &out->temp)) {
            out->has_temp = true;
        }
    }

    if (out->unit[0] == '\0' || strcmp(out->unit, "C") == 0) {
        char unit_raw[12] = {0};
        if ((attrs_json != NULL && attrs_json[0] != '\0') &&
            (weather_extract_raw_string_attr(attrs_json, "temperature_unit", unit_raw, sizeof(unit_raw)) ||
             weather_extract_raw_string_attr(attrs_json, "native_temperature_unit", unit_raw, sizeof(unit_raw)))) {
            weather_copy_text(out->unit, sizeof(out->unit), unit_raw);
        }
    }

    if (out->humidity < 0 && attrs_json != NULL && attrs_json[0] != '\0') {
        int humidity = -1;
        if (weather_extract_raw_int_attr(attrs_json, "humidity", &humidity)) {
            out->humidity = humidity;
        }
    }

    if (!out->has_temp && state->state[0] != '\0') {
        float state_temp = 0.0f;
        if (weather_parse_float_token(state->state, &state_temp)) {
            out->temp = state_temp;
            out->has_temp = true;
        }
    }

    if (want_forecast) {
        cJSON *punit = (attrs != NULL) ? cJSON_GetObjectItemCaseSensitive(attrs, "precipitation_unit") : NULL;
        if (cJSON_IsString(punit) && punit->valuestring != NULL && punit->valuestring[0] != '\0') {
            weather_copy_text(out->precip_unit, sizeof(out->precip_unit), punit->valuestring);
        }
        cJSON *forecast = (attrs != NULL) ? cJSON_GetObjectItemCaseSensitive(attrs, "forecast") : NULL;
        if (cJSON_IsArray(forecast)) {
            int count = cJSON_GetArraySize(forecast);
            int out_idx = 0;
            for (int i = 0; i < count && out_idx < WEATHER_3DAY_MAX_FORECAST; i++) {
                cJSON *item = cJSON_GetArrayItem(forecast, i);
                if (!cJSON_IsObject(item)) {
                    continue;
                }

                cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "date");
                cJSON *datetime = cJSON_GetObjectItemCaseSensitive(item, "datetime");

                const char *forecast_day = NULL;
                if (cJSON_IsString(date) && date->valuestring != NULL) {
                    forecast_day = date->valuestring;
                } else if (cJSON_IsString(datetime) && datetime->valuestring != NULL) {
                    forecast_day = datetime->valuestring;
                }

                bool is_today = (forecast_day != NULL) && weather_datetime_is_today(forecast_day);
                bool is_before_today = (forecast_day != NULL) && weather_datetime_is_before_today(forecast_day);
                if (is_before_today) {
                    continue;
                }

                bool has_high = false;
                float high_temp = 0.0f;
                cJSON *high = cJSON_GetObjectItemCaseSensitive(item, "temperature");
                if (!weather_json_item_to_float(high, &high_temp)) {
                    high = cJSON_GetObjectItemCaseSensitive(item, "native_temperature");
                }
                if (weather_json_item_to_float(high, &high_temp)) {
                    has_high = true;
                }

                bool has_low = false;
                float low_temp = 0.0f;
                cJSON *low = cJSON_GetObjectItemCaseSensitive(item, "templow");
                if (!weather_json_item_to_float(low, &low_temp)) {
                    low = cJSON_GetObjectItemCaseSensitive(item, "native_templow");
                }
                if (weather_json_item_to_float(low, &low_temp)) {
                    has_low = true;
                }

                cJSON *condition = cJSON_GetObjectItemCaseSensitive(item, "condition");
                char condition_key[32] = {0};
                char condition_human[sizeof(out->forecast[0].condition)] = {0};
                if (cJSON_IsString(condition) && condition->valuestring != NULL) {
                    weather_normalize_condition_key(condition->valuestring, condition_key, sizeof(condition_key));
                    weather_humanize_condition(condition->valuestring, condition_human, sizeof(condition_human));
                }

                float precip_mm = -1.0f;
                int precip_prob = -1;
                float tmp = 0.0f;
                if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(item, "precipitation"), &tmp) && tmp >= 0.0f) {
                    precip_mm = tmp;
                }
                if (weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(item, "precipitation_probability"), &tmp) &&
                    tmp >= 0.0f && tmp <= 100.0f) {
                    precip_prob = (int)(tmp + 0.5f);
                }

                if (is_today) {
                    if (forecast_day != NULL) {
                        ui_day_grid_local_date(forecast_day, out->today_date);
                    }
                    out->today_precip_mm = precip_mm;
                    out->today_precip_prob = precip_prob;
                    if (has_high) {
                        out->today_high_temp = high_temp;
                        out->today_has_high = true;
                    }
                    if (has_low) {
                        out->today_low_temp = low_temp;
                        out->today_has_low = true;
                    }
                    if (condition_key[0] != '\0') {
                        weather_copy_text(out->today_condition_key, sizeof(out->today_condition_key), condition_key);
                    }
                    continue;
                }

                weather_forecast_t *slot = &out->forecast[out_idx];
                slot->valid = true;
                if (forecast_day != NULL) {
                    ui_day_grid_local_date(forecast_day, slot->date);
                }
                slot->precip_mm = precip_mm;
                slot->precip_prob = precip_prob;
                if (forecast_day != NULL) {
                    weather_day_from_datetime(forecast_day, slot->day, sizeof(slot->day));
                }
                if (has_high) {
                    slot->has_high = true;
                    slot->high_temp = high_temp;
                }
                if (has_low) {
                    slot->has_low = true;
                    slot->low_temp = low_temp;
                }
                if (condition_key[0] != '\0') {
                    weather_copy_text(slot->condition_key, sizeof(slot->condition_key), condition_key);
                }
                if (condition_human[0] != '\0') {
                    weather_copy_text(slot->condition, sizeof(slot->condition), condition_human);
                }
                out_idx++;
            }
        }
    }

    if (attrs != NULL) {
        cJSON_Delete(attrs);
    }
}
