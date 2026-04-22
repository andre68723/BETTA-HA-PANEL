/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/theme/theme_palette.h"
#include "ui/theme/theme_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* -------------------------------------------------------------------------
 * Built-in presets
 * ------------------------------------------------------------------------- */

static const theme_entry_t k_preset_dark_v2 = {
    .id = "dark_v2",
    .name = "Dark",
    .builtin = true,
    .palette = {
        .screen_bg = 0x070B12,
        .screen_bg_grad = 0x0F1A28,
        .content_bg = 0x18232F,
        .content_border = 0x2A3A4C,
        .topbar_text = 0xEAF2FA,
        .topbar_muted = 0xA1B1C1,
        .topbar_status_on = 0xC7D1DB,
        .topbar_status_off = 0x5F6B78,
        .topbar_bg = 0x0D1723,
        .topbar_border = 0x2A3D50,
        .topbar_chip_bg = 0x1B2A3A,
        .topbar_chip_border = 0x385064,
        .text_primary = 0xF5F9FD,
        .text_muted = 0x9FB2C4,
        .text_soft = 0xB7C3D0,
        .nav_bg = 0x152B38,
        .nav_border = 0x4D93B3,
        .nav_btn_bg_idle = 0x2A4454,
        .nav_btn_bg_active = 0x365F74,
        .nav_tab_idle = 0xA9C3D0,
        .nav_tab_active = 0x6FE8FF,
        .nav_home_idle = 0x9EB8C7,
        .nav_home_active = 0x53E5FF,
        .ok = 0x59D87C,
        .error = 0xE97474,
        .wifi_off = 0x6D7E90,
        .card_bg_off = 0x1D2935,
        .card_bg_on = 0x223344,
        .card_border = 0x314456,
        .card_icon_off = 0x8EA1B4,
        .card_icon_on = 0x53E5FF,
        .state_on = 0xD6EAF8,
        .state_off = 0xA2B2C3,
        .light_icon_on = 0xFFCF6B,
        .light_track_on = 0x5A4722,
        .light_track_off = 0x435262,
        .light_ind_on = 0xFFC34D,
        .light_ind_off = 0x7E93A5,
        .light_knob_on = 0xFFE2A3,
        .light_knob_off = 0xB0BFD0,
        .heat_icon_on = 0xFF9A72,
        .heat_track_on = 0x5C2C20,
        .heat_track_off = 0x414F5D,
        .heat_ind_on = 0xFF7340,
        .heat_ind_off = 0x9BB0C1,
        .heat_knob_on = 0xFF916A,
        .heat_knob_off = 0xC2CED9,
        .weather_icon = 0xEAF3FB,
    },
};

static const theme_entry_t k_preset_classic_v1 = {
    .id = "classic_v1",
    .name = "Classic",
    .builtin = true,
    /* Home-Assistant-inspired neutral dark palette. Anthracite backgrounds
     * with warm amber accents for "on" states and a green "ok" indicator,
     * matching the default HA dashboard look. Channels stay close to R=G=B
     * for the neutral greys so that RGB565 quantisation (5/6/5) does not
     * introduce a green/brown tint on the panel. */
    .palette = {
        .screen_bg = 0x121212,
        .screen_bg_grad = 0x161616,
        .content_bg = 0x1C1C1E,
        .content_border = 0x2A2A2C,
        .topbar_text = 0xEDEDED,
        .topbar_muted = 0x9B9B9B,
        .topbar_status_on = 0xCFCFCF,
        .topbar_status_off = 0x6C6C6C,
        .topbar_bg = 0x161616,
        .topbar_border = 0x2A2A2C,
        .topbar_chip_bg = 0x1F1F21,
        .topbar_chip_border = 0x343436,
        .text_primary = 0xEDEDED,
        .text_muted = 0x9B9B9B,
        .text_soft = 0xC4C4C4,
        .nav_bg = 0x161616,
        .nav_border = 0x2A2A2C,
        .nav_btn_bg_idle = 0x1F1F21,
        .nav_btn_bg_active = 0x343436,
        .nav_tab_idle = 0xB2B2B2,
        .nav_tab_active = 0xFDD835,
        .nav_home_idle = 0xA4A4A4,
        .nav_home_active = 0xFDD835,
        .ok = 0x4CAF50,
        .error = 0xDB4437,
        .wifi_off = 0x7A7A7A,
        .card_bg_off = 0x1F1F21,
        .card_bg_on = 0x26262A,
        .card_border = 0x2A2A2C,
        .card_icon_off = 0x9E9E9E,
        .card_icon_on = 0xFDD835,
        .state_on = 0xFDD835,
        .state_off = 0xB0B0B0,
        .light_icon_on = 0xFDD835,
        .light_track_on = 0x4A3A00,
        .light_track_off = 0x3A3A3A,
        .light_ind_on = 0xFDD835,
        .light_ind_off = 0x8A8A8A,
        .light_knob_on = 0xFFE066,
        .light_knob_off = 0xB0B0B0,
        .heat_icon_on = 0xFF8C42,
        .heat_track_on = 0x5A2A12,
        .heat_track_off = 0x3A3A3A,
        .heat_ind_on = 0xFF6D2A,
        .heat_ind_off = 0xB0B0B0,
        .heat_knob_on = 0xFF9A5C,
        .heat_knob_off = 0xB0B0B0,
        .weather_icon = 0xFDD835,
    },
};

static const theme_entry_t k_preset_light = {
    .id = "light",
    .name = "Light",
    .builtin = true,
    .palette = {
        .screen_bg = 0xF4F6FA,
        .screen_bg_grad = 0xDCE3ED,
        .content_bg = 0xFFFFFF,
        .content_border = 0xC9D3DE,
        .topbar_text = 0x1A232D,
        .topbar_muted = 0x5C6B7A,
        .topbar_status_on = 0x2E3944,
        .topbar_status_off = 0xA3ADB8,
        .topbar_bg = 0xFFFFFF,
        .topbar_border = 0xD4DCE5,
        .topbar_chip_bg = 0xEEF2F7,
        .topbar_chip_border = 0xC3CDD7,
        .text_primary = 0x1A2330,
        .text_muted = 0x5A6A7A,
        .text_soft = 0x394454,
        .nav_bg = 0xEEF2F7,
        .nav_border = 0x2388CF,
        .nav_btn_bg_idle = 0xDCE3EC,
        .nav_btn_bg_active = 0xB4CFE8,
        .nav_tab_idle = 0x4A5866,
        .nav_tab_active = 0x1175C7,
        .nav_home_idle = 0x4A5866,
        .nav_home_active = 0x0F6CB8,
        .ok = 0x2BAE4C,
        .error = 0xC93D3D,
        .wifi_off = 0x97A3B0,
        .card_bg_off = 0xFFFFFF,
        .card_bg_on = 0xEAF4FE,
        .card_border = 0xCBD5E0,
        .card_icon_off = 0x6A7685,
        .card_icon_on = 0x1680D0,
        .state_on = 0x1A2330,
        .state_off = 0x6A7685,
        .light_icon_on = 0xE6A600,
        .light_track_on = 0xFCE6A3,
        .light_track_off = 0xCBD5E0,
        .light_ind_on = 0xE6A600,
        .light_ind_off = 0x8B97A4,
        .light_knob_on = 0xE6A600,
        .light_knob_off = 0x6A7685,
        .heat_icon_on = 0xD94E1F,
        .heat_track_on = 0xFCD7C5,
        .heat_track_off = 0xCBD5E0,
        .heat_ind_on = 0xD94E1F,
        .heat_ind_off = 0x8B97A4,
        .heat_knob_on = 0xD94E1F,
        .heat_knob_off = 0x6A7685,
        .weather_icon = 0x2D3846,
    },
};

static const theme_entry_t k_preset_ocean = {
    .id = "ocean",
    .name = "Ocean",
    .builtin = true,
    .palette = {
        .screen_bg = 0x031827,
        .screen_bg_grad = 0x0A3C56,
        .content_bg = 0x0B2E44,
        .content_border = 0x1A4D6A,
        .topbar_text = 0xE4F6FE,
        .topbar_muted = 0x8FB8CC,
        .topbar_status_on = 0xBFE2F1,
        .topbar_status_off = 0x486B7B,
        .topbar_bg = 0x062233,
        .topbar_border = 0x1B4A64,
        .topbar_chip_bg = 0x0F3A55,
        .topbar_chip_border = 0x2C6A8C,
        .text_primary = 0xEAF6FC,
        .text_muted = 0x90B4C6,
        .text_soft = 0xB8D2DF,
        .nav_bg = 0x093049,
        .nav_border = 0x3AA6D1,
        .nav_btn_bg_idle = 0x13496A,
        .nav_btn_bg_active = 0x1F6A95,
        .nav_tab_idle = 0x9BC3D5,
        .nav_tab_active = 0x5AE4FF,
        .nav_home_idle = 0x88B5C8,
        .nav_home_active = 0x4AD9FF,
        .ok = 0x36D4A6,
        .error = 0xE8657A,
        .wifi_off = 0x678699,
        .card_bg_off = 0x0D3048,
        .card_bg_on = 0x124160,
        .card_border = 0x1E537A,
        .card_icon_off = 0x7FA8BD,
        .card_icon_on = 0x5AE4FF,
        .state_on = 0xCCEEFB,
        .state_off = 0x92B5C7,
        .light_icon_on = 0xFFD87A,
        .light_track_on = 0x544220,
        .light_track_off = 0x1E4966,
        .light_ind_on = 0xFFC85A,
        .light_ind_off = 0x6E95A8,
        .light_knob_on = 0xFFE4A2,
        .light_knob_off = 0xA2BECD,
        .heat_icon_on = 0xFF9E6C,
        .heat_track_on = 0x5A2A1D,
        .heat_track_off = 0x1E4966,
        .heat_ind_on = 0xFF7340,
        .heat_ind_off = 0x8FB1C4,
        .heat_knob_on = 0xFF9170,
        .heat_knob_off = 0xB8D2DF,
        .weather_icon = 0xE1F4FC,
    },
};

static const theme_entry_t k_preset_contrast = {
    .id = "contrast",
    .name = "High Contrast",
    .builtin = true,
    .palette = {
        .screen_bg = 0x000000,
        .screen_bg_grad = 0x000000,
        .content_bg = 0x000000,
        .content_border = 0xFFFFFF,
        .topbar_text = 0xFFFFFF,
        .topbar_muted = 0xC8C8C8,
        .topbar_status_on = 0xFFFFFF,
        .topbar_status_off = 0x707070,
        .topbar_bg = 0x000000,
        .topbar_border = 0xFFFFFF,
        .topbar_chip_bg = 0x1A1A1A,
        .topbar_chip_border = 0xFFFFFF,
        .text_primary = 0xFFFFFF,
        .text_muted = 0xC8C8C8,
        .text_soft = 0xE0E0E0,
        .nav_bg = 0x000000,
        .nav_border = 0xFFFF00,
        .nav_btn_bg_idle = 0x1A1A1A,
        .nav_btn_bg_active = 0x333333,
        .nav_tab_idle = 0xE0E0E0,
        .nav_tab_active = 0xFFFF00,
        .nav_home_idle = 0xE0E0E0,
        .nav_home_active = 0xFFFF00,
        .ok = 0x00FF66,
        .error = 0xFF4444,
        .wifi_off = 0x909090,
        .card_bg_off = 0x000000,
        .card_bg_on = 0x1A1A1A,
        .card_border = 0xFFFFFF,
        .card_icon_off = 0xC8C8C8,
        .card_icon_on = 0xFFFF00,
        .state_on = 0xFFFFFF,
        .state_off = 0xC8C8C8,
        .light_icon_on = 0xFFFF00,
        .light_track_on = 0x666600,
        .light_track_off = 0x333333,
        .light_ind_on = 0xFFFF00,
        .light_ind_off = 0xC8C8C8,
        .light_knob_on = 0xFFFF66,
        .light_knob_off = 0xFFFFFF,
        .heat_icon_on = 0xFF6633,
        .heat_track_on = 0x661A00,
        .heat_track_off = 0x333333,
        .heat_ind_on = 0xFF6633,
        .heat_ind_off = 0xC8C8C8,
        .heat_knob_on = 0xFF9966,
        .heat_knob_off = 0xFFFFFF,
        .weather_icon = 0xFFFFFF,
    },
};

static const theme_entry_t *const k_builtin_list[] = {
    &k_preset_dark_v2,
    &k_preset_classic_v1,
    &k_preset_light,
    &k_preset_ocean,
    &k_preset_contrast,
};
#define BUILTIN_COUNT (sizeof(k_builtin_list) / sizeof(k_builtin_list[0]))

/* -------------------------------------------------------------------------
 * Active palette state
 * ------------------------------------------------------------------------- */

static theme_palette_t s_active_palette;
static char s_active_id[APP_MAX_THEME_ID_LEN];
static char s_active_name[APP_MAX_THEME_NAME_LEN];
static bool s_active_initialised = false;

static void theme_palette_ensure_initialised(void)
{
    if (s_active_initialised) {
        return;
    }
    s_active_palette = k_preset_dark_v2.palette;
    snprintf(s_active_id, sizeof(s_active_id), "%s", k_preset_dark_v2.id);
    snprintf(s_active_name, sizeof(s_active_name), "%s", k_preset_dark_v2.name);
    s_active_initialised = true;
}

const theme_palette_t *theme_palette_active(void)
{
    theme_palette_ensure_initialised();
    return &s_active_palette;
}

const char *theme_palette_active_id(void)
{
    theme_palette_ensure_initialised();
    return s_active_id;
}

const char *theme_palette_active_name(void)
{
    theme_palette_ensure_initialised();
    return s_active_name;
}

void theme_palette_set_active(const theme_palette_t *palette, const char *id, const char *name)
{
    theme_palette_ensure_initialised();
    if (palette != NULL) {
        s_active_palette = *palette;
    }
    if (id != NULL && id[0] != '\0') {
        snprintf(s_active_id, sizeof(s_active_id), "%s", id);
    }
    if (name != NULL && name[0] != '\0') {
        snprintf(s_active_name, sizeof(s_active_name), "%s", name);
    }
}

const theme_entry_t *theme_palette_find_builtin(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (strncmp(k_builtin_list[i]->id, id, APP_MAX_THEME_ID_LEN) == 0) {
            return k_builtin_list[i];
        }
    }
    return NULL;
}

size_t theme_palette_builtin_list(const theme_entry_t **out_list)
{
    if (out_list != NULL) {
        for (size_t i = 0; i < BUILTIN_COUNT; i++) {
            out_list[i] = k_builtin_list[i];
        }
    }
    return BUILTIN_COUNT;
}

esp_err_t theme_palette_activate_by_id(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const theme_entry_t *builtin = theme_palette_find_builtin(id);
    if (builtin != NULL) {
        theme_palette_set_active(&builtin->palette, builtin->id, builtin->name);
        return ESP_OK;
    }
    theme_entry_t custom = {0};
    if (theme_store_load_custom(id, &custom) == ESP_OK) {
        theme_palette_set_active(&custom.palette, custom.id, custom.name);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

/* -------------------------------------------------------------------------
 * JSON (de)serialisation
 * ------------------------------------------------------------------------- */

typedef struct {
    const char *key;
    size_t offset;
} theme_field_map_t;

#define THEME_FIELD(name) { #name, offsetof(theme_palette_t, name) }

static const theme_field_map_t k_field_map[] = {
    THEME_FIELD(screen_bg), THEME_FIELD(screen_bg_grad), THEME_FIELD(content_bg), THEME_FIELD(content_border),
    THEME_FIELD(topbar_text), THEME_FIELD(topbar_muted), THEME_FIELD(topbar_status_on), THEME_FIELD(topbar_status_off),
    THEME_FIELD(topbar_bg), THEME_FIELD(topbar_border), THEME_FIELD(topbar_chip_bg), THEME_FIELD(topbar_chip_border),
    THEME_FIELD(text_primary), THEME_FIELD(text_muted), THEME_FIELD(text_soft),
    THEME_FIELD(nav_bg), THEME_FIELD(nav_border), THEME_FIELD(nav_btn_bg_idle), THEME_FIELD(nav_btn_bg_active),
    THEME_FIELD(nav_tab_idle), THEME_FIELD(nav_tab_active), THEME_FIELD(nav_home_idle), THEME_FIELD(nav_home_active),
    THEME_FIELD(ok), THEME_FIELD(error), THEME_FIELD(wifi_off),
    THEME_FIELD(card_bg_off), THEME_FIELD(card_bg_on), THEME_FIELD(card_border),
    THEME_FIELD(card_icon_off), THEME_FIELD(card_icon_on), THEME_FIELD(state_on), THEME_FIELD(state_off),
    THEME_FIELD(light_icon_on), THEME_FIELD(light_track_on), THEME_FIELD(light_track_off),
    THEME_FIELD(light_ind_on), THEME_FIELD(light_ind_off), THEME_FIELD(light_knob_on), THEME_FIELD(light_knob_off),
    THEME_FIELD(heat_icon_on), THEME_FIELD(heat_track_on), THEME_FIELD(heat_track_off),
    THEME_FIELD(heat_ind_on), THEME_FIELD(heat_ind_off), THEME_FIELD(heat_knob_on), THEME_FIELD(heat_knob_off),
    THEME_FIELD(weather_icon),
};
#define FIELD_COUNT (sizeof(k_field_map) / sizeof(k_field_map[0]))

static bool parse_hex_color(const char *s, uint32_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    const char *p = s;
    if (*p == '#') {
        p++;
    }
    if (strlen(p) != 6) {
        return false;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < 6; i++) {
        char c = p[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(10 + c - 'A');
        } else {
            return false;
        }
        v = (v << 4) | digit;
    }
    *out = v & 0xFFFFFFU;
    return true;
}

char *theme_palette_to_json(const theme_entry_t *entry)
{
    if (entry == NULL) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "id", entry->id);
    cJSON_AddStringToObject(root, "name", entry->name);
    cJSON_AddBoolToObject(root, "builtin", entry->builtin);
    cJSON *colors = cJSON_AddObjectToObject(root, "colors");
    if (colors == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    const uint8_t *base = (const uint8_t *)&entry->palette;
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        uint32_t v = *(const uint32_t *)(base + k_field_map[i].offset);
        char hex[10];
        snprintf(hex, sizeof(hex), "#%06X", (unsigned)(v & 0xFFFFFFU));
        cJSON_AddStringToObject(colors, k_field_map[i].key, hex);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

bool theme_palette_from_json(const char *json, theme_entry_t *out_entry)
{
    if (json == NULL || out_entry == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    memset(out_entry, 0, sizeof(*out_entry));

    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *colors = cJSON_GetObjectItemCaseSensitive(root, "colors");
    if (!cJSON_IsString(id) || !cJSON_IsObject(colors)) {
        cJSON_Delete(root);
        return false;
    }
    snprintf(out_entry->id, sizeof(out_entry->id), "%s", id->valuestring);
    if (cJSON_IsString(name) && name->valuestring != NULL) {
        snprintf(out_entry->name, sizeof(out_entry->name), "%s", name->valuestring);
    } else {
        snprintf(out_entry->name, sizeof(out_entry->name), "%s", out_entry->id);
    }
    out_entry->builtin = false;

    /* Start from dark_v2 so any missing fields remain sensible. */
    out_entry->palette = k_preset_dark_v2.palette;
    uint8_t *base = (uint8_t *)&out_entry->palette;
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(colors, k_field_map[i].key);
        if (!cJSON_IsString(item) || item->valuestring == NULL) {
            continue;
        }
        uint32_t parsed = 0;
        if (parse_hex_color(item->valuestring, &parsed)) {
            /* Snap to RGB565 so what the user picks matches what the panel
             * shows (avoids ghost tints from 8->5/6/5 truncation). */
            uint32_t r8 = (parsed >> 16) & 0xFFU;
            uint32_t g8 = (parsed >> 8) & 0xFFU;
            uint32_t b8 = parsed & 0xFFU;
            uint32_t r5 = (r8 * 31U + 127U) / 255U;
            uint32_t g6 = (g8 * 63U + 127U) / 255U;
            uint32_t b5 = (b8 * 31U + 127U) / 255U;
            r8 = (r5 << 3) | (r5 >> 2);
            g8 = (g6 << 2) | (g6 >> 4);
            b8 = (b5 << 3) | (b5 >> 2);
            *(uint32_t *)(base + k_field_map[i].offset) = (r8 << 16) | (g8 << 8) | b8;
        }
    }
    cJSON_Delete(root);
    return true;
}
