/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ha/ha_light_capabilities.h"

#include <string.h>

#include "cJSON.h"

#define HA_LIGHT_FEATURE_BRIGHTNESS 0x00000001U
#define HA_LIGHT_FEATURE_COLOR_TEMP 0x00000002U
#define HA_LIGHT_FEATURE_EFFECT 0x00000004U
#define HA_LIGHT_FEATURE_LEGACY_COLOR 0x00000010U
#define HA_LIGHT_FEATURE_LEGACY_XY_COLOR 0x00000040U
#define HA_LIGHT_FEATURE_LEGACY_WHITE_VALUE 0x00000080U

static bool light_entity_id_is_light(const char *entity_id)
{
    return entity_id != NULL && strncmp(entity_id, "light.", 6) == 0;
}

static bool state_is_on(const char *state)
{
    return state != NULL && strcmp(state, "on") == 0;
}

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static int clamp_kelvin(int value)
{
    if (value < 1000) {
        return 1000;
    }
    if (value > 12000) {
        return 12000;
    }
    return value;
}

static int kelvin_from_mired(double mired)
{
    if (mired <= 0.0) {
        return 0;
    }
    return clamp_kelvin((int)((1000000.0 / mired) + 0.5));
}

static bool attrs_have_number(cJSON *attrs, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(attrs, name);
    return cJSON_IsNumber(item);
}

static bool attrs_have_array(cJSON *attrs, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(attrs, name);
    return cJSON_IsArray(item);
}

static int brightness_percent_from_attrs(cJSON *attrs, bool is_on)
{
    int value = -1;
    if (attrs != NULL) {
        cJSON *brightness_pct = cJSON_GetObjectItemCaseSensitive(attrs, "brightness_pct");
        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(attrs, "brightness");
        if (cJSON_IsNumber(brightness_pct)) {
            value = (int)(brightness_pct->valuedouble + 0.5);
        } else if (cJSON_IsNumber(brightness)) {
            int raw_255 = (int)(brightness->valuedouble + 0.5);
            if (raw_255 < 0) {
                raw_255 = 0;
            }
            if (raw_255 > 255) {
                raw_255 = 255;
            }
            value = (raw_255 * 100 + 127) / 255;
        }
    }
    if (value < 0) {
        value = is_on ? 100 : 0;
    }
    return clamp_percent(value);
}

static void apply_color_mode(ha_light_capabilities_t *caps, const char *mode)
{
    if (caps == NULL || mode == NULL || mode[0] == '\0') {
        return;
    }
    if (strcmp(mode, "onoff") == 0 || strcmp(mode, "unknown") == 0) {
        return;
    }

    caps->can_dim = true;
    if (strcmp(mode, "color_temp") == 0) {
        caps->can_color_temp = true;
    } else if (strcmp(mode, "hs") == 0 || strcmp(mode, "rgb") == 0 || strcmp(mode, "rgbw") == 0 ||
               strcmp(mode, "rgbww") == 0 || strcmp(mode, "xy") == 0) {
        caps->can_color = true;
    }
}

static bool apply_supported_color_modes(cJSON *attrs, ha_light_capabilities_t *caps)
{
    cJSON *modes = cJSON_GetObjectItemCaseSensitive(attrs, "supported_color_modes");
    if (!cJSON_IsArray(modes)) {
        return false;
    }

    cJSON *mode = NULL;
    cJSON_ArrayForEach(mode, modes)
    {
        if (cJSON_IsString(mode) && mode->valuestring != NULL) {
            apply_color_mode(caps, mode->valuestring);
        }
    }
    return true;
}

static void apply_current_color_mode(cJSON *attrs, ha_light_capabilities_t *caps)
{
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(attrs, "color_mode");
    if (cJSON_IsString(mode) && mode->valuestring != NULL) {
        apply_color_mode(caps, mode->valuestring);
    }
}

static void apply_legacy_supported_features(cJSON *attrs, ha_light_capabilities_t *caps)
{
    cJSON *supported_features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
    if (!cJSON_IsNumber(supported_features)) {
        return;
    }
    uint32_t features = (uint32_t)supported_features->valuedouble;
    if ((features & HA_LIGHT_FEATURE_BRIGHTNESS) != 0U) {
        caps->can_dim = true;
    }
    if ((features & HA_LIGHT_FEATURE_COLOR_TEMP) != 0U) {
        caps->can_color_temp = true;
    }
    if ((features & HA_LIGHT_FEATURE_EFFECT) != 0U) {
        caps->can_effect = true;
    }
    if ((features & (HA_LIGHT_FEATURE_LEGACY_COLOR | HA_LIGHT_FEATURE_LEGACY_XY_COLOR |
                    HA_LIGHT_FEATURE_LEGACY_WHITE_VALUE)) != 0U) {
        caps->can_color = true;
        caps->can_dim = true;
    }
}

static void apply_attribute_fallbacks(cJSON *attrs, ha_light_capabilities_t *caps)
{
    if (attrs_have_number(attrs, "brightness") || attrs_have_number(attrs, "brightness_pct")) {
        caps->can_dim = true;
    }
    if (attrs_have_array(attrs, "rgb_color") || attrs_have_array(attrs, "hs_color") || attrs_have_array(attrs, "xy_color")) {
        caps->can_color = true;
    }
    if (attrs_have_number(attrs, "color_temp") || attrs_have_number(attrs, "color_temp_kelvin") ||
        attrs_have_number(attrs, "min_color_temp_kelvin") || attrs_have_number(attrs, "max_color_temp_kelvin") ||
        attrs_have_number(attrs, "min_mireds") || attrs_have_number(attrs, "max_mireds")) {
        caps->can_color_temp = true;
    }
    if (attrs_have_array(attrs, "effect_list")) {
        caps->can_effect = true;
    }
}

static void apply_effect_fallback(cJSON *attrs, ha_light_capabilities_t *caps)
{
    if (attrs_have_array(attrs, "effect_list")) {
        caps->can_effect = true;
    }

    cJSON *supported_features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
    if (cJSON_IsNumber(supported_features)) {
        uint32_t features = (uint32_t)supported_features->valuedouble;
        if ((features & HA_LIGHT_FEATURE_EFFECT) != 0U) {
            caps->can_effect = true;
        }
    }
}

static uint32_t clamp_u8_from_number(double value)
{
    if (value < 0.0) {
        return 0;
    }
    if (value > 255.0) {
        return 255;
    }
    return (uint32_t)(value + 0.5);
}

static void apply_rgb_color(cJSON *attrs, ha_light_capabilities_t *caps)
{
    cJSON *rgb = cJSON_GetObjectItemCaseSensitive(attrs, "rgb_color");
    if (!cJSON_IsArray(rgb) || cJSON_GetArraySize(rgb) < 3) {
        return;
    }

    cJSON *r_item = cJSON_GetArrayItem(rgb, 0);
    cJSON *g_item = cJSON_GetArrayItem(rgb, 1);
    cJSON *b_item = cJSON_GetArrayItem(rgb, 2);
    if (!cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
        return;
    }

    uint32_t r = clamp_u8_from_number(r_item->valuedouble);
    uint32_t g = clamp_u8_from_number(g_item->valuedouble);
    uint32_t b = clamp_u8_from_number(b_item->valuedouble);

    caps->has_rgb_color = true;
    caps->rgb_color = (r << 16) | (g << 8) | b;
}

static void apply_color_temp_kelvin(cJSON *attrs, ha_light_capabilities_t *caps)
{
    if (attrs == NULL || caps == NULL) {
        return;
    }

    caps->min_color_temp_kelvin = 2000;
    caps->max_color_temp_kelvin = 6500;

    cJSON *min_kelvin = cJSON_GetObjectItemCaseSensitive(attrs, "min_color_temp_kelvin");
    cJSON *max_kelvin = cJSON_GetObjectItemCaseSensitive(attrs, "max_color_temp_kelvin");
    if (cJSON_IsNumber(min_kelvin)) {
        caps->min_color_temp_kelvin = clamp_kelvin((int)(min_kelvin->valuedouble + 0.5));
    }
    if (cJSON_IsNumber(max_kelvin)) {
        caps->max_color_temp_kelvin = clamp_kelvin((int)(max_kelvin->valuedouble + 0.5));
    }

    if (!cJSON_IsNumber(min_kelvin) || !cJSON_IsNumber(max_kelvin)) {
        cJSON *min_mireds = cJSON_GetObjectItemCaseSensitive(attrs, "min_mireds");
        cJSON *max_mireds = cJSON_GetObjectItemCaseSensitive(attrs, "max_mireds");
        int kelvin_from_max_mireds = cJSON_IsNumber(max_mireds) ? kelvin_from_mired(max_mireds->valuedouble) : 0;
        int kelvin_from_min_mireds = cJSON_IsNumber(min_mireds) ? kelvin_from_mired(min_mireds->valuedouble) : 0;
        if (!cJSON_IsNumber(min_kelvin) && kelvin_from_max_mireds > 0) {
            caps->min_color_temp_kelvin = kelvin_from_max_mireds;
        }
        if (!cJSON_IsNumber(max_kelvin) && kelvin_from_min_mireds > 0) {
            caps->max_color_temp_kelvin = kelvin_from_min_mireds;
        }
    }

    if (caps->min_color_temp_kelvin > caps->max_color_temp_kelvin) {
        int tmp = caps->min_color_temp_kelvin;
        caps->min_color_temp_kelvin = caps->max_color_temp_kelvin;
        caps->max_color_temp_kelvin = tmp;
    }

    cJSON *current_kelvin = cJSON_GetObjectItemCaseSensitive(attrs, "color_temp_kelvin");
    if (cJSON_IsNumber(current_kelvin)) {
        caps->has_color_temp_kelvin = true;
        caps->color_temp_kelvin = clamp_kelvin((int)(current_kelvin->valuedouble + 0.5));
        return;
    }

    cJSON *current_mired = cJSON_GetObjectItemCaseSensitive(attrs, "color_temp");
    int from_mired = cJSON_IsNumber(current_mired) ? kelvin_from_mired(current_mired->valuedouble) : 0;
    if (from_mired > 0) {
        caps->has_color_temp_kelvin = true;
        caps->color_temp_kelvin = from_mired;
    }
}

void ha_light_capabilities_from_state(const ha_state_t *state, ha_light_capabilities_t *out_caps)
{
    if (out_caps == NULL) {
        return;
    }

    memset(out_caps, 0, sizeof(*out_caps));
    out_caps->brightness_percent = state_is_on(state != NULL ? state->state : NULL) ? 100 : 0;

    if (state == NULL || !light_entity_id_is_light(state->entity_id)) {
        return;
    }

    out_caps->is_light = true;
    out_caps->can_turn_on_off = true;

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs == NULL) {
        return;
    }

    bool has_supported_color_modes = apply_supported_color_modes(attrs, out_caps);
    if (!has_supported_color_modes) {
        apply_current_color_mode(attrs, out_caps);
        apply_legacy_supported_features(attrs, out_caps);
        apply_attribute_fallbacks(attrs, out_caps);
    } else {
        apply_effect_fallback(attrs, out_caps);
    }
    apply_rgb_color(attrs, out_caps);
    apply_color_temp_kelvin(attrs, out_caps);
    out_caps->brightness_percent = brightness_percent_from_attrs(attrs, state_is_on(state->state));

    cJSON_Delete(attrs);
}

bool ha_light_state_supports_dimming(const ha_state_t *state)
{
    ha_light_capabilities_t caps = {0};
    ha_light_capabilities_from_state(state, &caps);
    return caps.is_light && caps.can_dim;
}

bool ha_light_state_supports_color(const ha_state_t *state)
{
    ha_light_capabilities_t caps = {0};
    ha_light_capabilities_from_state(state, &caps);
    return caps.is_light && caps.can_color;
}

bool ha_light_state_supports_color_temp(const ha_state_t *state)
{
    ha_light_capabilities_t caps = {0};
    ha_light_capabilities_from_state(state, &caps);
    return caps.is_light && caps.can_color_temp;
}
