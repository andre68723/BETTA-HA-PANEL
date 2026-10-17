/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "lvgl.h"
#include "ha/ha_evcc_sessions.h"
void ui_evcc_sessions_view(lv_obj_t *body, lv_obj_t *status, const evcc_sessions_t *sessions);
const char *ui_evcc_sessions_status(evcc_sessions_status_t status, bool empty);
void ui_evcc_session_date(int64_t timestamp, char *out, size_t capacity);
