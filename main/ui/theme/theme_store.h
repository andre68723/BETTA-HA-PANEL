/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "app_config.h"
#include "ui/theme/theme_palette.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_MAX_CUSTOM_THEMES
#define APP_MAX_CUSTOM_THEMES 16
#endif

#ifndef APP_THEME_MAX_JSON_LEN
#define APP_THEME_MAX_JSON_LEN 4096
#endif

/* Ensures the theme directory exists. The UI applies the persisted palette under its display lock. */
esp_err_t theme_store_init(void);

/* Persists the active theme id so it is re-applied on next boot. */
esp_err_t theme_store_set_active_id(const char *id);
/* Snapshot of the persisted choice; does not access or change LVGL state. */
esp_err_t theme_store_load_active(theme_entry_t *out);

/* Loads (copies) a custom theme from littlefs. Returns ESP_ERR_NOT_FOUND if
 * the id does not exist. Built-in ids always return ESP_ERR_NOT_FOUND. */
esp_err_t theme_store_load_custom(const char *id, theme_entry_t *out_entry);

/* Saves (or overwrites) a custom theme. Must have a non-empty id that does
 * not collide with a built-in preset. */
esp_err_t theme_store_save_custom(const theme_entry_t *entry);

/* Deletes a custom theme. Built-in themes cannot be deleted. */
esp_err_t theme_store_delete_custom(const char *id);

/* Lists all custom theme ids. Writes up to max_ids entries. */
esp_err_t theme_store_list_custom(
    char (*out_ids)[APP_MAX_THEME_ID_LEN], size_t max_ids, size_t *out_count);

#ifdef __cplusplus
}
#endif
