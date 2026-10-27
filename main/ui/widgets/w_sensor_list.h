/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "ui/ui_widget_factory.h"

#define UI_SENSOR_LIST_MAX 8
esp_err_t w_sensor_list_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out);
void w_sensor_list_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_sensor_list_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_sensor_row_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out, lv_obj_t *font_root);
