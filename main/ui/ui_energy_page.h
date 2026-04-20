/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#include "app_config.h"
#include "ha/ha_model.h"

typedef struct {
    char page_id[APP_MAX_PAGE_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    char source[APP_MAX_UI_OPTION_LEN];
    char home_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char solar_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char grid_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char grid_import_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char grid_export_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char battery_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char battery_charge_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char battery_discharge_power_entity_id[APP_MAX_ENTITY_ID_LEN];
    char battery_soc_entity_id[APP_MAX_ENTITY_ID_LEN];
} ui_energy_page_config_t;

typedef struct {
    char page_id[APP_MAX_PAGE_ID_LEN];
    void *ctx;
    lv_obj_t *obj;
} ui_energy_page_instance_t;

esp_err_t ui_energy_page_create(
    const ui_energy_page_config_t *config, lv_obj_t *parent, ui_energy_page_instance_t *out_instance);
bool ui_energy_page_apply_state(ui_energy_page_instance_t *instance, const ha_state_t *state);
void ui_energy_page_apply_all_states(ui_energy_page_instance_t *instance);
