/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/text_font.h"

#include <stdlib.h>
#include <string.h>

#include "ui/fonts/ttf_data.h"

/* lv_tiny_ttf puts the base line one pixel lower than the converter did: re-applied after every create (see text_scale.h) */
#define TTF_BASELINE_ADJUST (-1)

/* LVGL's own default font (the built-in unscii 8, see the root CMakeLists.txt): always there, the last resort when a font cannot be made. */
#define FALLBACK_FONT ((const lv_font_t *)LV_FONT_DEFAULT)

typedef enum { FAMILY_TEXT, FAMILY_BOLD, FAMILY_ICON, FAMILY_EMOJI } font_family_t;

typedef struct {
    font_family_t family;
    int key;             /* text/emoji: pixel size; icon: ui_icon_font_t */
    lv_font_t *font;
} font_instance_t;

static font_instance_t s_instances[UI_FONT_MAX_INSTANCES];
static int s_count;
static int s_user_step;
static bool s_board_set;
static text_board_t s_board;
static ui_fonts_stats_t s_stats;
static lv_font_t *s_probe;
static int s_probe_px;

void ui_fonts_set_user_step(int step) { s_user_step = text_scale_clamp_user_step(step); }
int ui_fonts_user_step(void) { return s_user_step; }

void ui_fonts_set_board(text_board_t board)
{
    s_board = board;
    s_board_set = true;
}

text_board_t ui_fonts_board(void) { return s_board_set ? s_board : text_scale_build_board(); }

static int s_tile_depth;
static int s_tile_cap = TEXT_TILE_CAP_NONE;
static int s_cap_override = -1;

void ui_fonts_tile_begin(const char *widget_type, int w, int h)
{
    s_tile_depth++;
    s_tile_cap = s_cap_override >= 0 ? s_cap_override : text_scale_tile_cap(ui_fonts_board(), widget_type, w, h);
}

void ui_fonts_tile_end(void)
{
    if (s_tile_depth > 0) s_tile_depth--;
    if (s_tile_depth == 0) s_tile_cap = TEXT_TILE_CAP_NONE;
}

void ui_fonts_set_cap_override(int cap) { s_cap_override = cap < 0 ? -1 : (cap > TEXT_TILE_CAP_NONE ? TEXT_TILE_CAP_NONE : cap); }

int ui_fonts_role_px(ui_text_role_t role)
{
    return s_tile_depth > 0 ? text_scale_px_capped(ui_fonts_board(), role, s_user_step, s_tile_cap)
                            : text_scale_px(ui_fonts_board(), role, s_user_step);
}

ui_fonts_stats_t ui_fonts_stats(void)
{
    ui_fonts_stats_t stats = s_stats;
    stats.instances = s_count;
    return stats;
}

static font_instance_t *find_instance(font_family_t family, int key)
{
    for (int i = 0; i < s_count; i++) {
        if (s_instances[i].family == family && s_instances[i].key == key) return &s_instances[i];
    }
    return NULL;
}

/* The metrics of the converter fonts, with the tiny_ttf base-line correction. */
static void apply_metrics(lv_font_t *font, int line_height, int base_line)
{
    font->line_height = (int32_t)line_height;
    font->base_line = (int32_t)(base_line + TTF_BASELINE_ADJUST > 0 ? base_line + TTF_BASELINE_ADJUST : 0);
}

#ifdef UI_FONTS_TEST_HOOKS
static int s_fail_creates;
void ui_fonts_test_fail_creates(int count) { s_fail_creates = count; }
#endif

static lv_font_t *make_font(const unsigned char *data, unsigned int size, int px, lv_font_kerning_t kerning, size_t cache_entries)
{
#ifdef UI_FONTS_TEST_HOOKS
    if (s_fail_creates > 0) {
        s_fail_creates--;
        s_stats.create_failures++;
        return NULL;
    }
#endif
    lv_font_t *font = lv_tiny_ttf_create_data_ex(data, size, px, kerning, cache_entries);
    if (font == NULL) s_stats.create_failures++;
    return font;
}

static font_instance_t *add_instance(font_family_t family, int key, lv_font_t *font)
{
    if (font == NULL || s_count >= UI_FONT_MAX_INSTANCES) return NULL;
    s_instances[s_count] = (font_instance_t){family, key, font};
    return &s_instances[s_count++];
}

/* The nearest existing text instance at or below px, else the smallest above it (used when the instance budget is reached or a font cannot
 * be created). At or below first: a smaller text always fits where the requested size was meant to. NULL only when no text font exists yet. */
static const lv_font_t *nearest_text(int px)
{
    const lv_font_t *below = NULL, *above = NULL;
    int below_px = -1, above_px = 1 << 20;
    for (int i = 0; i < s_count; i++) {
        if (s_instances[i].family != FAMILY_TEXT) continue;
        const int k = s_instances[i].key;
        if (k <= px && k > below_px) { below_px = k; below = s_instances[i].font; }
        if (k > px && k < above_px) { above_px = k; above = s_instances[i].font; }
    }
    return below != NULL ? below : above;
}

/* Never NULL: a caller sets the result as a style font, and LVGL has no answer for a NULL one. */
static const lv_font_t *text_or_default(int px)
{
    const lv_font_t *near = nearest_text(px);
    return near != NULL ? near : FALLBACK_FONT;
}

const lv_font_t *ui_font_emoji_for_text(int text_px)
{
    const icon_metrics_t m = text_scale_emoji_metrics(text_px);
    font_instance_t *inst = find_instance(FAMILY_EMOJI, m.px);
    if (inst != NULL) return inst->font;
    lv_font_t *font = make_font(ttf_emoji_data, ttf_emoji_size, m.px, LV_FONT_KERNING_NONE, 32);
    if (font == NULL) return NULL;
    apply_metrics(font, m.line_height, m.base_line);
    inst = add_instance(FAMILY_EMOJI, m.px, font);
    if (inst == NULL) {
        s_stats.budget_hits++;
        lv_tiny_ttf_destroy(font);
        return NULL; /* the text font simply has no emoji fallback */
    }
    return font;
}

const lv_font_t *ui_font_text_px(int px)
{
    if (px < 8) px = 8;
    if (px > text_scale_max_px()) px = text_scale_max_px();
    font_instance_t *inst = find_instance(FAMILY_TEXT, px);
    if (inst != NULL) return inst->font;
    if (s_count >= UI_FONT_MAX_INSTANCES - 1) {
        s_stats.budget_hits++;
        const lv_font_t *near = nearest_text(px);
        if (near != NULL) return near;
    }
    lv_font_t *font = make_font(ttf_poppins_data, ttf_poppins_size, px, LV_FONT_KERNING_NORMAL, 128);
    if (font == NULL) return text_or_default(px);
    int line_height = 0, base_line = 0;
    text_scale_text_metrics(px, &line_height, &base_line);
    apply_metrics(font, line_height, base_line);
    font->fallback = ui_font_emoji_for_text(px);
    if (add_instance(FAMILY_TEXT, px, font) == NULL) {
        s_stats.budget_hits++;
        lv_tiny_ttf_destroy(font);
        return text_or_default(px);
    }
    return font;
}

const lv_font_t *ui_font_emphasis(const lv_font_t *base)
{
    if (!base) base = ui_font_role(UI_TEXT_META);
    const int px = ui_font_text_px_of(base);
    if (!px) return base;
    font_instance_t *inst = find_instance(FAMILY_BOLD, px);
    if (inst) return inst->font;
    if (s_count >= UI_FONT_MAX_INSTANCES) { s_stats.budget_hits++; return base; }
    lv_font_t *font = make_font(ttf_poppins_bold_data, ttf_poppins_bold_size, px, LV_FONT_KERNING_NORMAL, 64);
    if (!font) return base;
    font->line_height = base->line_height;
    font->base_line = base->base_line;
    font->fallback = base->fallback;
    if (!add_instance(FAMILY_BOLD, px, font)) {
        s_stats.budget_hits++;
        lv_tiny_ttf_destroy(font);
        return base;
    }
    return font;
}

int ui_font_text_px_of(const lv_font_t *font)
{
    for (int i = 0; i < s_count; i++) {
        if ((s_instances[i].family == FAMILY_TEXT || s_instances[i].family == FAMILY_BOLD) && s_instances[i].font == font) return s_instances[i].key;
    }
    return 0;
}

bool ui_font_at_floor(const lv_font_t *font)
{
    const int px = ui_font_text_px_of(font);
    return px > 0 && px <= text_scale_floor_px(ui_fonts_board());
}

const lv_font_t *ui_font_role(ui_text_role_t role)
{
    const int px = ui_fonts_role_px(role);
    return ui_font_text_px(px > 0 ? px : ui_fonts_role_px(UI_TEXT_LABEL));
}

/* The text size a symbol or icon font falls back to for text mixed with the glyph (the former Poppins fallbacks). */
static int icon_fallback_text_px(ui_icon_font_t kind)
{
    switch (kind) {
    case ICON_SYMBOL_SMALL: return text_scale_floor_px(ui_fonts_board());
    case ICON_SYMBOL_MEDIUM: return 24;
    case ICON_SYMBOL_LARGE: return 48;
    default: return 0; /* status, energy, arrow, weather and tile fonts never mixed text in */
    }
}

const lv_font_t *ui_font_icon(ui_icon_font_t kind)
{
    if ((int)kind < 0 || kind >= ICON_FONT_COUNT) kind = ICON_SYMBOL_SMALL;
    font_instance_t *inst = find_instance(FAMILY_ICON, (int)kind);
    if (inst != NULL) return inst->font;
    const icon_metrics_t m = text_scale_icon_metrics(kind);
    lv_font_t *font = make_font(ttf_mdi_data, ttf_mdi_size, m.px, LV_FONT_KERNING_NONE, m.px >= 42 ? 32 : 64);
    if (font == NULL) return FALLBACK_FONT; /* the glyphs show as the placeholder box, the screen stays usable */
    apply_metrics(font, m.line_height, m.base_line);
    const int fallback_px = icon_fallback_text_px(kind);
    if (fallback_px > 0) font->fallback = ui_font_text_px(fallback_px);
    if (add_instance(FAMILY_ICON, (int)kind, font) == NULL) {
        s_stats.budget_hits++;
        lv_tiny_ttf_destroy(font);
        return FALLBACK_FONT;
    }
    return font;
}

const lv_font_t *ui_font_probe(int px)
{
    if (px < 8) px = 8;
    if (px > text_scale_max_px()) px = text_scale_max_px();
    if (s_probe == NULL) {
        s_probe = make_font(ttf_poppins_data, ttf_poppins_size, px, LV_FONT_KERNING_NORMAL, 0); /* 0 entries: no caches */
        if (s_probe == NULL) return ui_font_text_px(px);
        s_probe_px = px;
    } else if (s_probe_px != px) {
        lv_tiny_ttf_set_size(s_probe, px);
        s_probe_px = px;
    }
    /* create and set_size both reset the metrics: apply them every time; the emoji fallback keeps mixed text measured right */
    int line_height = 0, base_line = 0;
    text_scale_text_metrics(px, &line_height, &base_line);
    apply_metrics(s_probe, line_height, base_line);
    s_probe->fallback = ui_font_emoji_for_text(px);
    return s_probe;
}
