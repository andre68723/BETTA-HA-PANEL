/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

void ui_ota_progress_begin(const char *status_text, size_t total_bytes);
void ui_ota_progress_set_status(const char *status_text);
void ui_ota_progress_update(size_t written_bytes, size_t total_bytes);
void ui_ota_progress_success(const char *status_text);
void ui_ota_progress_error(const char *error_text);
bool ui_ota_progress_is_active(void);
