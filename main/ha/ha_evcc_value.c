/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_evcc_value.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool evcc_number(const char *raw, float *value)
{
    if (!raw || !*raw || !value) return false;
    char *end; float v = strtof(raw, &end);
    if (end == raw || *end || !isfinite(v)) return false;
    *value = v; return true;
}
bool evcc_soc(const char *raw, float *value)
{
    return evcc_number(raw, value) && *value >= 0 && *value <= 100;
}
static bool complete_options(const cJSON *list)
{
    int count = cJSON_GetArraySize(list);
    if (!cJSON_IsArray(list) || count < 1 || count > HA_SELECT_MAX_OPTIONS) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (!cJSON_IsString(item) || !item->valuestring || !*item->valuestring ||
            strlen(item->valuestring) >= HA_SELECT_OPTION_LEN) return false;
    }
    return true;
}
bool evcc_select_parse(const char *attributes, ha_select_options_t *options)
{
    memset(options, 0, sizeof(*options)); options->missing = true;
    cJSON *root = cJSON_Parse(attributes);
    bool ok = complete_options(cJSON_GetObjectItemCaseSensitive(root, "options"));
    cJSON_Delete(root);
    if (ok) ha_select_options_parse(attributes, options);
    return ok && !options->missing;
}
bool evcc_number_bounds(const char *attributes, float *min, float *max, float *step)
{
    cJSON *root = cJSON_Parse(attributes);
    const cJSON *lo = cJSON_GetObjectItemCaseSensitive(root, "min");
    const cJSON *hi = cJSON_GetObjectItemCaseSensitive(root, "max");
    const cJSON *by = cJSON_GetObjectItemCaseSensitive(root, "step");
    const char *unit = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "unit_of_measurement"));
    bool ok = cJSON_IsNumber(lo) && cJSON_IsNumber(hi) && cJSON_IsNumber(by) && unit && !strcmp(unit, "%") &&
        isfinite(lo->valuedouble) && isfinite(hi->valuedouble) && isfinite(by->valuedouble) &&
        lo->valuedouble >= 0 && hi->valuedouble <= 100 && lo->valuedouble < hi->valuedouble && by->valuedouble > 0 &&
        (hi->valuedouble - lo->valuedouble) / by->valuedouble >= 1 &&
        (hi->valuedouble - lo->valuedouble) / by->valuedouble <= 10000;
    if (ok) { *min = lo->valuedouble; *max = hi->valuedouble; *step = by->valuedouble; }
    cJSON_Delete(root); return ok;
}
bool evcc_target_valid(float value, float min, float max, float step)
{
    if (!isfinite(value) || !isfinite(min) || !isfinite(max) || !isfinite(step) ||
        step <= 0 || min < 0 || max > 100 || min >= max || value < min || value > max) return false;
    float ticks = (value - min) / step;
    return fabsf(ticks - roundf(ticks)) < 0.001f;
}
bool evcc_number_payload(const char *entity, float value, char *out, size_t capacity)
{
    if (!entity || strncmp(entity, "number.", 7) || !isfinite(value)) return false;
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "entity_id", entity); cJSON_AddNumberToObject(root, "value", value);
    char *json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    bool ok = json && strlen(json) < capacity;
    if (ok) strcpy(out, json);
    cJSON_free(json); return ok;
}
static void copy(cJSON *dst, const cJSON *src, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(src, key);
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) >= APP_MAX_NAME_LEN) return;
    if (item) cJSON_AddItemToObject(dst, key, cJSON_Duplicate(item, true));
}
bool ha_evcc_control_attrs(const cJSON *attributes, bool number, char *out, size_t capacity)
{
    if (!out || capacity < 3) return false;
    strcpy(out, "{}");
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    const char *keys[] = {"friendly_name", "unit_of_measurement", "device_class", "min", "max", "step"};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) copy(root, attributes, keys[i]);
    if (!number) {
        const cJSON *options = cJSON_GetObjectItemCaseSensitive(attributes, "options");
        if (complete_options(options)) copy(root, attributes, "options");
        const cJSON *vehicle = cJSON_GetObjectItemCaseSensitive(attributes, "vehicle");
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vehicle, "name"));
        if (name && strlen(name) < APP_MAX_NAME_LEN) {
            cJSON *v = cJSON_AddObjectToObject(root, "vehicle");
            if (v) cJSON_AddStringToObject(v, "name", name);
        }
    }
    char *json = cJSON_PrintUnformatted(root);
    if (json && strlen(json) >= capacity && !number) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "options");
        cJSON_free(json); json = cJSON_PrintUnformatted(root);
    }
    bool ok = json && strlen(json) < capacity;
    if (ok) strcpy(out, json);
    cJSON_free(json); cJSON_Delete(root); return ok;
}

bool evcc_power_kw(const char *raw, const char *unit, float *kw)
{
    float value;
    if (!kw || !unit || !evcc_number(raw, &value)) return false;
    if (!strcmp(unit, "W")) *kw = value / 1000.0f;
    else if (!strcmp(unit, "kW")) *kw = value;
    else if (!strcmp(unit, "MW")) *kw = value * 1000.0f;
    else return false;
    return true;
}

static float positive(float value) { return value > 0.0f && isfinite(value) ? value : 0.0f; }

void evcc_flow_compute(float pv, float battery, float grid, float home, float car, evcc_flow_t *flow)
{
    if (!flow) return;
    flow->in[EVCC_FLOW_PV] = positive(pv);
    flow->in[EVCC_FLOW_BATTERY_OUT] = positive(battery);
    flow->in[EVCC_FLOW_GRID_IN] = positive(grid);
    flow->out[EVCC_FLOW_HOME] = positive(home);
    flow->out[EVCC_FLOW_CAR] = positive(car);
    flow->out[EVCC_FLOW_BATTERY_IN] = positive(-battery);
    flow->out[EVCC_FLOW_EXPORT] = positive(-grid);
    float in = 0.0f, out = 0.0f;
    for (int i = 0; i < EVCC_FLOW_IN_COUNT; i++) in += flow->in[i];
    for (int i = 0; i < EVCC_FLOW_OUT_COUNT; i++) out += flow->out[i];
    flow->scale = in > out ? in : out;
}
