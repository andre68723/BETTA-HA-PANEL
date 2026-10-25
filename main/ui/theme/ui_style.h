/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Shared look for interactive controls, so every card reads the same:
 *
 *  - Glow buttons are raised, Glass buttons are translucent, and Flat/Matte
 *    buttons use opaque fills with no depth effects.
 *  - Tracks follow each look's radius and surface; the fill carries the accent.
 *
 * Surface and semantic-role implementation lives in ui_style.c.
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "lvgl.h"
#include "ui/theme/theme_palette.h"
#include "ui/theme/ui_style_registry.h"

/* The four looks: flat (the classic one), glow (gradients, drop shadows),
 * glass (translucent surfaces with light edges), and matte (opaque soft surfaces). The choice
 * is made once at boot from Settings > Appearance; saving a change reboots
 * the panel. See docs/appearance.md for the full look-and-feel description
 * of each. The persisted IDs, the default and the fallback for unknown IDs
 * are defined in ui_style_registry.h. */
void ui_style_set(ui_style_kind_t kind);
ui_style_kind_t ui_style_get(void);
/* True only for depth effects in Glow and Glass. Matte shares Flat's lack of
 * depth, while keeping its own shapes and opaque surface treatment. */
bool ui_style_has_depth(void);
bool ui_style_is_flat(void);
bool ui_style_is_glow(void);
bool ui_style_is_glass(void);
bool ui_style_is_matte(void);
bool ui_style_matte_dark_surfaces(void);

/* Layout traits of the active style, for widgets whose anatomy differs between styles. A widget
 * asks for the trait instead of the style, so a new style that wants, say, the split layout
 * sets one flag here instead of adding a branch to every widget. */
typedef struct {
    bool split_wide_tiles; /* wide Light/Slider/Climate tiles: information left, controls right */
    bool touch_tracks;     /* thick slider and progress tracks, sized for a finger */
    bool value_beside_slider; /* a slider's value sits in its own fixed column beside the track */
    bool caps_titles;      /* widget titles in capitals (ui_style_title()) */
    bool round_switches;   /* switches are always pills, whatever the ends of the style's other tracks */
    bool frameless_page;   /* no frame around the page's content area */
    bool plain_tracks;     /* slider tracks: the style's track radius, no outline, no shadow */
    bool no_slider_knob;   /* a slider's filled part is its handle; no separate knob is drawn */
    bool adaptive_control_icons; /* control icons take their colour from the control's own fill */
} ui_style_traits_t;
const ui_style_traits_t *ui_style_traits(void);

/* Colour roles. A widget asks for the role, passing the colour it uses when the style has none of its
 * own, so no widget names a style and a new style sets its values in one place (ui_style.c). */
lv_color_t ui_style_page_bg(lv_color_t fallback);        /* the page's content area */
lv_color_t ui_style_control_bg(lv_color_t fallback);     /* resting button, chip, switch track, slider track */
lv_color_t ui_style_control_on(lv_color_t fallback);     /* active button, cover position fill */
lv_color_t ui_style_accent(lv_color_t fallback);         /* filled part of a light or switch */
lv_color_t ui_style_switch_knob(lv_color_t fallback);
lv_color_t ui_style_value_on_fill(lv_color_t fallback);  /* value text drawn over a slider's fill */
lv_color_t ui_style_slider_fill(bool active, lv_color_t fallback, lv_color_t track);

/* The widget title as the active style shows it: `text` itself, or its capitals written to `buf`. */
const char *ui_style_title(char *buf, size_t buf_len, const char *text);
/* Sets a widget title on `label`: in capitals where the style asks for them, in the title's own case as soon as the
 * capitals no longer fit the label's width (capitals run wider and would end in "..."). The label keeps its own copy
 * and re-checks whenever its size changes; it is freed with the label. */
void ui_style_title_fit(lv_obj_t *label, const char *plain);

/* Widget cards. A widget marks whether its card shows an active entity (a light on, a cover open,
 * media playing, ...); the style decides what that looks like, today only Matte tints it. Call it
 * whenever the widget restyles its card, after setting its own background, so a Matte card keeps
 * its tint between state updates. Cards that are never marked count as inactive. */
void ui_style_card_set_active(lv_obj_t *card, bool active);
bool ui_style_card_is_active(const lv_obj_t *card);
/* The active style's card surface: Flat's crisp outline, Glass's frosting, Matte's opaque on/off
 * fill; nothing extra for Glow. ui_widget_factory_create()/_apply_state()/_mark_unavailable() call
 * it after the widget, because widgets restyle their own background on every update; code that
 * creates widgets without the factory (the desktop simulator) calls it too. */
void ui_style_apply_card(lv_obj_t *card);

/* Button surface in the active style: Glow raised with a gradient and shadow, Glass translucent
 * with a light edge, Flat and Matte opaque without depth. `bg` is the base colour, `border` the
 * outline (Flat and Glow). Sets the default and pressed states. */
void ui_style_button(lv_obj_t *btn, lv_color_t bg, lv_color_t border, lv_color_t pressed_bg);

/* Semantic controls (docs/appearance-implementation.md): an explicit primary role
 * keeps the accent independent of state, as on the media player's Play/Pause.
 * Openers, one-shot actions and value +/- stay neutral; abort uses danger. */
typedef enum {
    UI_CONTROL_ACTION,
    UI_CONTROL_ACTIVE,
    UI_CONTROL_SELECTED,
    UI_CONTROL_DANGER,
    UI_CONTROL_PRIMARY,
} ui_control_role_t;
lv_color_t ui_style_control_fill(ui_control_role_t role, bool enabled, lv_color_t accent);
lv_color_t ui_style_control_color(ui_control_role_t role, bool enabled);
void ui_style_action_button(lv_obj_t *btn, lv_obj_t *label, ui_control_role_t role, bool enabled);

/* Same, with the pressed color derived from the base. */
static inline void ui_style_button_auto(lv_obj_t *btn, lv_color_t bg, lv_color_t border)
{
    ui_style_button(btn, bg, border, lv_color_mix(lv_color_white(), bg, 24));
}

/* The icon/label color to put on a button of the given background: light text on a dark
 * button, dark text on a light one. ui_style_button()/_auto() only style the button
 * surface itself (background, border, shadow) and deliberately take no opinion on what sits
 * on top of it, since a button can hold an icon, a label or both; every caller chose that
 * color on its own before this helper existed, which is how the same shared button surface
 * ended up looking inconsistent from one widget to the next (a light `bg` with a caller's
 * hardcoded light icon reads as a washed-out button, a dark `bg` with a hardcoded dark icon
 * reads as no icon at all). Call this with the exact same `bg` passed to the button helper. */
static inline lv_color_t ui_style_button_icon_color(lv_color_t bg)
{
    return lv_color_brightness(bg) > 150 ? lv_color_hex(0x101418) : lv_color_hex(0xFFFFFF);
}

/* Formats a Celsius reading consistently across every widget: one decimal place, the degree sign
 * directly against the number with no space -- "21.0°C" -- matching the majority of existing
 * widgets (climate, heating, the car tile). The sensor and weather tiles show a space ("21.8 °C")
 * only because they run any Home Assistant value through a generic "%s %s" value+unit join meant
 * for arbitrary units like "kWh" or "hPa", not a deliberate choice for degrees specifically; that
 * generic join is unaffected by this helper. `has_value` false (an unavailable/placeholder
 * reading) gets "--.-°C" instead of formatting garbage.
 *
 * Added after a design review found three different spellings of the same kind of value across
 * the app: "21.0°C" (climate), "21.0 °C" (heating's target format, via i18n_store.c) and
 * "37,0°C" (a comma, not even the same decimal separator, from the room card's own hand-rolled
 * period-to-comma swap) -- each widget had formatted this by hand instead of sharing one place.
 * New code formatting a Celsius value should use this instead of its own snprintf; a fully
 * generic value+unit formatter (percentages, power, illuminance, ...) is a larger follow-up, not
 * attempted here since only temperature had actually drifted out of sync so far. */
static inline void ui_format_temp_c(char *out, size_t out_len, bool has_value, float value)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    if (has_value) {
        snprintf(out, out_len, "%.1f°C", (double)value);
    } else {
        snprintf(out, out_len, "--.-°C");
    }
}

/* Flat's nearly square ends, Matte's soft ends; Glow and Glass draw pills. */
#define UI_STYLE_FLAT_TRACK_RADIUS 4
#define UI_STYLE_MATTE_TRACK_RADIUS 8

/* Corner radius of every track and its fill: bars, sliders, progress and volume bars, colour
 * bands. A pill in glow and glass (LVGL caps LV_RADIUS_CIRCLE at half the thickness), flat's
 * nearly square ends in flat. Use it wherever a track's radius is set, including layout code that
 * sets it again after a resize; see docs/appearance.md. Knobs are round controls and stay round. */
static inline lv_coord_t ui_style_track_radius(void)
{
    if (ui_style_is_flat()) return UI_STYLE_FLAT_TRACK_RADIUS;
    if (ui_style_is_matte()) return UI_STYLE_MATTE_TRACK_RADIUS;
    return LV_RADIUS_CIRCLE;
}

/* Dark inset track with a gradient-filled indicator, for lv_bar. The caller
 * sets the gradient direction and colors of the indicator. */
static inline void ui_style_inset_track(lv_obj_t *bar, lv_color_t track)
{
    if (bar == NULL) {
        return;
    }
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    switch (ui_style_get()) {
    case UI_STYLE_FLAT:
        /* Nearly square ends set flat apart from the pill-shaped glow and glass bars;
         * the caller keeps its own flat track colors. */
        lv_obj_set_style_radius(bar, UI_STYLE_FLAT_TRACK_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, UI_STYLE_FLAT_TRACK_RADIUS, LV_PART_INDICATOR);
        return;
    case UI_STYLE_MATTE:
        lv_obj_set_style_radius(bar, UI_STYLE_MATTE_TRACK_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, UI_STYLE_MATTE_TRACK_RADIUS, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, lv_color_hex(theme_palette_active()->matte_track), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(bar, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        return;
    case UI_STYLE_GLASS:
        lv_obj_set_style_bg_color(bar, lv_color_mix(lv_color_white(), track, 60), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bar, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_opa(bar, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_90, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(bar, 10, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(bar, LV_OPA_30, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_offset_y(bar, 0, LV_PART_INDICATOR);
        return;
    case UI_STYLE_GLOW:
        break;
    }
    lv_obj_set_style_bg_color(bar, track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_mix(lv_color_black(), track, 90), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_30, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_offset_y(bar, 2, LV_PART_INDICATOR);
}

/* Gauge arc (car widget and similar): glow = rounded caps and a dark inset
 * track; classic = the flat translucent track. Width is the glow width. */
static inline void ui_style_gauge_arc(lv_obj_t *arc, int32_t width, int32_t classic_width, lv_color_t track,
                                      lv_color_t classic_track, lv_color_t indicator)
{
    if (arc == NULL) {
        return;
    }
    const bool glow = ui_style_has_depth();
    const int32_t w = glow ? width : classic_width;
    lv_obj_set_style_arc_width(arc, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, glow, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, glow, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, glow ? track : classic_track, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, glow ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, indicator, LV_PART_INDICATOR);
}

/* Gold (low fill) to green (high fill), the same two stops as the solar forecast bars' gradient
 * (SOLAR_BAR_GOLD/SOLAR_BAR_GREEN) and the gauge widget's linear bar. An lv_arc has no gradient
 * along the ring itself (LV_PART_INDICATOR only takes a flat arc_color, unlike a bar's
 * bg_grad_color), so a radial gauge or the car tile's rings recolour the whole ring by the
 * current fill level instead of shading along it. `value` is clamped to [min, max] internally. */
static inline lv_color_t ui_style_fill_level_color(int32_t value, int32_t min, int32_t max)
{
    const lv_color_t gold = lv_color_hex(ui_style_has_depth() ? 0xF2C14E : 0xFFD500);
    const lv_color_t green = lv_color_hex(ui_style_has_depth() ? 0x4FCB7E : 0x22D164);
    if (max <= min) {
        return gold;
    }
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    /* lv_color_mix's second weight (0..255) is the share of the FIRST color kept. */
    uint8_t green_share = (uint8_t)(((int64_t)(value - min) * 255) / (max - min));
    return lv_color_mix(green, gold, green_share);
}

/* Extra depth for tracks that keep their own colors (sliders, switches):
 * an inset outline on the track and a soft shadow under the fill. No-op in the
 * classic style. */
static inline void ui_style_inset_effects(lv_obj_t *obj)
{
    if (obj == NULL || !ui_style_has_depth()) {
        return;
    }
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, ui_style_is_glass() ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, ui_style_is_glass() ? LV_OPA_30 : LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 6, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_offset_y(obj, 2, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, LV_PART_INDICATOR);
}

/* Soft drop shadow for a knob or a small round/rounded surface that keeps its
 * own fill. `part` is LV_PART_KNOB for slider/switch knobs, LV_PART_MAIN for
 * plain objects. No-op in the classic style. */
static inline void ui_style_drop_shadow(lv_obj_t *obj, lv_style_selector_t part)
{
    if (obj == NULL || !ui_style_has_depth()) {
        return;
    }
    lv_obj_set_style_shadow_width(obj, ui_style_is_glass() ? 14 : 8, part);
    lv_obj_set_style_shadow_offset_y(obj, ui_style_is_glass() ? 4 : 2, part);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), part);
    lv_obj_set_style_shadow_opa(obj, ui_style_is_glass() ? LV_OPA_30 : LV_OPA_40, part);
}

/* Flat keeps its corners crisp: cards get UI_STYLE_FLAT_CARD_RADIUS (ui_style_apply_card()), and a
 * surface inside a card (a forecast day, a list row, a button) at most UI_STYLE_FLAT_INNER_RADIUS,
 * so it does not look rounder than the card around it. Glow and glass keep the radius they are given.
 * Not for round controls whose radius makes them a circle. */
#define UI_STYLE_FLAT_CARD_RADIUS 8
#define UI_STYLE_FLAT_INNER_RADIUS 6
#define UI_STYLE_MATTE_CARD_RADIUS 12
#define UI_STYLE_MATTE_INNER_RADIUS 8

static inline lv_coord_t ui_style_radius(lv_coord_t radius)
{
    switch (ui_style_get()) {
    case UI_STYLE_FLAT:
        return radius > UI_STYLE_FLAT_INNER_RADIUS ? UI_STYLE_FLAT_INNER_RADIUS : radius;
    case UI_STYLE_MATTE:
        return radius > UI_STYLE_MATTE_INNER_RADIUS ? UI_STYLE_MATTE_INNER_RADIUS : radius;
    case UI_STYLE_GLOW:
    case UI_STYLE_GLASS:
        break;
    }
    return radius;
}

/* Glass page background: the content area gets a soft diagonal-free vertical
 * gradient from the theme's accent tint to the plain content color, so the
 * translucent tiles have something to show.
 *
 * The panel runs LV_COLOR_DEPTH 16 (RGB565, 5-6 bits per channel) and this LVGL version has no
 * dithering for gradients at all (checked: no Kconfig option, no style/API for it) -- a genuine
 * hardware/config ceiling, not something fixable in this function alone. A visibly banded
 * gradient across the whole content box was reported; a real fix (higher color depth, or a
 * dithered noise overlay) is a bigger, costlier change (framebuffer size, or a flash-budget
 * image asset) than this warrants. Narrowing the color distance between the two stops instead:
 * fewer distinct shades have to fit the same span, so each visible step covers less of a jump,
 * at the cost of a slightly less pronounced tint. */
static inline void ui_style_glass_background(lv_obj_t *area, lv_color_t base, lv_color_t accent)
{
    if (area == NULL || !ui_style_is_glass()) {
        return;
    }
    lv_obj_set_style_bg_color(area, lv_color_mix(accent, base, 35), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(area, lv_color_mix(lv_color_black(), base, 55), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(area, LV_GRAD_DIR_VER, LV_PART_MAIN);
}
