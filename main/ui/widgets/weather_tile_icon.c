/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

const lv_font_t *weather_find_icon_font_for_cp(uint32_t codepoint)
{
    (void)codepoint;
    const lv_font_t *font = mdi_font_weather();
    if (font != NULL) {
        return font;
    }
    return mdi_font_large();
}

bool weather_font_has_codepoint(const lv_font_t *font, uint32_t codepoint)
{
    if (font == NULL || codepoint == 0U) {
        return false;
    }
    lv_font_glyph_dsc_t dsc = {0};
    return lv_font_get_glyph_dsc(font, &dsc, codepoint, 0);
}

static bool weather_font_has_render_headroom(const lv_font_t *font, uint32_t codepoint)
{
#if LV_USE_STDLIB_MALLOC != LV_STDLIB_BUILTIN
    (void)font;
    (void)codepoint;
    /* With CLIB/custom allocators we don't have meaningful LVGL pool headroom
     * telemetry here; rely on runtime allocator and glyph probing only. */
    return true;
#else
    if (font == NULL || codepoint == 0U) {
        return false;
    }

    lv_font_glyph_dsc_t dsc = {0};
    if (!lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) {
        return false;
    }

    /* Static A8 glyphs don't need a transient draw buffer allocation. */
    if (lv_font_has_static_bitmap(font) && dsc.format == LV_FONT_GLYPH_FORMAT_A8) {
        return true;
    }

    if (dsc.box_w == 0 || dsc.box_h == 0) {
        return false;
    }

    uint32_t rounded_h = LV_ROUND_UP((uint32_t)dsc.box_h, 32U);
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)dsc.box_w, LV_COLOR_FORMAT_A8);
    size_t needed = (size_t)stride * (size_t)rounded_h;

    /* Keep margin for concurrent LVGL allocations in the same frame. */
    const size_t margin = 1024U;
    lv_mem_monitor_t mon = {0};
    lv_mem_monitor(&mon);
    return mon.free_biggest_size > (needed + margin);
#endif
}

#if APP_UI_WEATHER_ICON_DEBUG
static size_t weather_font_render_bytes_required(const lv_font_t *font, uint32_t codepoint)
{
    if (font == NULL || codepoint == 0U) {
        return 0U;
    }

    lv_font_glyph_dsc_t dsc = {0};
    if (!lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) {
        return 0U;
    }

    if (lv_font_has_static_bitmap(font) && dsc.format == LV_FONT_GLYPH_FORMAT_A8) {
        return 0U;
    }

    if (dsc.box_w == 0 || dsc.box_h == 0) {
        return 0U;
    }

    uint32_t rounded_h = LV_ROUND_UP((uint32_t)dsc.box_h, 32U);
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)dsc.box_w, LV_COLOR_FORMAT_A8);
    return (size_t)stride * (size_t)rounded_h;
}
#endif

static void weather_append_unique_font_candidate(
    const lv_font_t **candidates, size_t capacity, size_t *count, const lv_font_t *font)
{
    if (candidates == NULL || count == NULL || capacity == 0 || font == NULL) {
        return;
    }

    for (size_t i = 0; i < *count; i++) {
        if (candidates[i] == font) {
            return;
        }
    }

    if (*count < capacity) {
        candidates[*count] = font;
        (*count)++;
    }
}

const lv_font_t *weather_pick_render_icon_font(
    lv_obj_t *card, const w_weather_tile_ctx_t *ctx, uint32_t codepoint, const lv_font_t *preferred)
{
    lv_coord_t min_dim = weather_effective_min_dim(card, ctx);
    const lv_font_t *font_56 = mdi_font_large();
#if APP_UI_WEATHER_ICON_ALLOW_72
    const lv_font_t *font_72 = mdi_font_weather();
#else
    const lv_font_t *font_72 = NULL;
#endif

    /* If size is unresolved during early render, stay conservative and avoid
     * selecting a very large icon font too early. */
    if (min_dim <= 0) {
        min_dim = 240;
    }

    const lv_font_t *candidates[4] = {0};
    size_t count = 0;

    const lv_coord_t tier_72_min_dim = 261;

    if (min_dim < tier_72_min_dim) {
        weather_append_unique_font_candidate(candidates, sizeof(candidates) / sizeof(candidates[0]), &count, font_56);
        weather_append_unique_font_candidate(candidates, sizeof(candidates) / sizeof(candidates[0]), &count, font_72);
    } else {
        weather_append_unique_font_candidate(candidates, sizeof(candidates) / sizeof(candidates[0]), &count, font_72);
        weather_append_unique_font_candidate(candidates, sizeof(candidates) / sizeof(candidates[0]), &count, font_56);
    }

    if (preferred != NULL) {
        weather_append_unique_font_candidate(candidates, sizeof(candidates) / sizeof(candidates[0]), &count, preferred);
    } else {
        weather_append_unique_font_candidate(
            candidates, sizeof(candidates) / sizeof(candidates[0]), &count, weather_find_icon_font_for_cp(codepoint));
    }

#if APP_UI_WEATHER_ICON_DEBUG
    lv_coord_t card_w = (card != NULL) ? lv_obj_get_width(card) : 0;
    lv_coord_t card_h = (card != NULL) ? lv_obj_get_height(card) : 0;
    bool use_builtin_malloc = (LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN);
    lv_mem_monitor_t mon = {0};
    if (use_builtin_malloc) {
        lv_mem_monitor(&mon);
    }
    size_t need56 = weather_font_render_bytes_required(font_56, codepoint);
    size_t need72 = weather_font_render_bytes_required(font_72, codepoint);
    ESP_LOGI("w_weather_tile",
        "card=%dx%d min=%d cp=0x%lX has56=%d has72=%d fit56=%d fit72=%d need56=%u need72=%u free_big=%u lv_malloc_builtin=%d LV_FONT_FMT_TXT_LARGE=%d",
        (int)card_w, (int)card_h, (int)min_dim,
        (unsigned long)codepoint,
        weather_font_has_codepoint(font_56, codepoint),
        weather_font_has_codepoint(font_72, codepoint),
        weather_font_has_render_headroom(font_56, codepoint),
        weather_font_has_render_headroom(font_72, codepoint),
        (unsigned int)need56,
        (unsigned int)need72,
        use_builtin_malloc ? (unsigned int)mon.free_biggest_size : 0U,
        use_builtin_malloc ? 1 : 0,
        (int)LV_FONT_FMT_TXT_LARGE);
#endif

    for (size_t i = 0; i < count; i++) {
        if (weather_font_has_codepoint(candidates[i], codepoint) &&
            weather_font_has_render_headroom(candidates[i], codepoint)) {
            return candidates[i];
        }
    }

    /* Fallback if glyph probing fails unexpectedly on this platform/build. */
    for (size_t i = 0; i < count; i++) {
        if (candidates[i] != NULL) {
            return candidates[i];
        }
    }

    if (preferred != NULL) {
        return preferred;
    }
    return weather_find_icon_font_for_cp(codepoint);
}
