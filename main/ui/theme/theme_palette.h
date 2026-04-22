/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_MAX_THEME_ID_LEN
#define APP_MAX_THEME_ID_LEN 32
#endif
#ifndef APP_MAX_THEME_NAME_LEN
#define APP_MAX_THEME_NAME_LEN 48
#endif

/* Flat palette struct: every APP_UI_COLOR_* has a matching field here.
 * Values are 0xRRGGBB (24-bit). */
typedef struct {
    uint32_t screen_bg;
    uint32_t screen_bg_grad;
    uint32_t content_bg;
    uint32_t content_border;

    uint32_t topbar_text;
    uint32_t topbar_muted;
    uint32_t topbar_status_on;
    uint32_t topbar_status_off;
    uint32_t topbar_bg;
    uint32_t topbar_border;
    uint32_t topbar_chip_bg;
    uint32_t topbar_chip_border;

    uint32_t text_primary;
    uint32_t text_muted;
    uint32_t text_soft;

    uint32_t nav_bg;
    uint32_t nav_border;
    uint32_t nav_btn_bg_idle;
    uint32_t nav_btn_bg_active;
    uint32_t nav_tab_idle;
    uint32_t nav_tab_active;
    uint32_t nav_home_idle;
    uint32_t nav_home_active;

    uint32_t ok;
    uint32_t error;
    uint32_t wifi_off;

    uint32_t card_bg_off;
    uint32_t card_bg_on;
    uint32_t card_border;
    uint32_t card_icon_off;
    uint32_t card_icon_on;
    uint32_t state_on;
    uint32_t state_off;

    uint32_t light_icon_on;
    uint32_t light_track_on;
    uint32_t light_track_off;
    uint32_t light_ind_on;
    uint32_t light_ind_off;
    uint32_t light_knob_on;
    uint32_t light_knob_off;

    uint32_t heat_icon_on;
    uint32_t heat_track_on;
    uint32_t heat_track_off;
    uint32_t heat_ind_on;
    uint32_t heat_ind_off;
    uint32_t heat_knob_on;
    uint32_t heat_knob_off;

    uint32_t weather_icon;
} theme_palette_t;

typedef struct {
    char id[APP_MAX_THEME_ID_LEN];
    char name[APP_MAX_THEME_NAME_LEN];
    bool builtin;
    theme_palette_t palette;
} theme_entry_t;

/* Active palette accessor used by APP_UI_COLOR_* macros. Always returns a
 * pointer to a valid palette (falls back to dark_v2 if not initialised). */
const theme_palette_t *theme_palette_active(void);

/* Returns the active theme id (never NULL). */
const char *theme_palette_active_id(void);

/* Built-in preset ids: "dark_v2" (default), "classic_v1", "light",
 * "ocean", "contrast". Unknown id returns NULL. */
const theme_entry_t *theme_palette_find_builtin(const char *id);

/* Enumerate built-in presets. Returns count; fills list up to max_count. */
size_t theme_palette_builtin_list(const theme_entry_t **out_list);

/* Set the active palette (copies). Does not persist. */
void theme_palette_set_active(const theme_palette_t *palette, const char *id, const char *name);

/* Lookup + apply. If id matches a built-in preset, activates that preset.
 * If id names a custom theme (stored via theme_store), that theme is
 * activated. Returns ESP_ERR_NOT_FOUND otherwise. Does not persist. */
esp_err_t theme_palette_activate_by_id(const char *id);

/* JSON (de)serialisation. Caller owns returned string (cJSON_free). */
char *theme_palette_to_json(const theme_entry_t *entry);
bool theme_palette_from_json(const char *json, theme_entry_t *out_entry);

/* Returns the active theme's display name. */
const char *theme_palette_active_name(void);

#ifdef __cplusplus
}
#endif
