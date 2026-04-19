/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ha/ha_model.h"

typedef struct {
    bool is_light;
    bool can_turn_on_off;
    bool can_dim;
    bool can_color;
    bool can_color_temp;
    bool can_effect;
    bool has_rgb_color;
    uint32_t rgb_color;
    bool has_color_temp_kelvin;
    int color_temp_kelvin;
    int min_color_temp_kelvin;
    int max_color_temp_kelvin;
    int brightness_percent;
} ha_light_capabilities_t;

void ha_light_capabilities_from_state(const ha_state_t *state, ha_light_capabilities_t *out_caps);
bool ha_light_state_supports_dimming(const ha_state_t *state);
bool ha_light_state_supports_color(const ha_state_t *state);
bool ha_light_state_supports_color_temp(const ha_state_t *state);
