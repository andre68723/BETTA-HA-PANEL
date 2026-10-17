/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/theme/ui_style.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/theme_palette.h"
#include "ui/theme/matte_title.h"
#include <string.h>


static ui_style_kind_t s_kind = UI_STYLE_DEFAULT;

void ui_style_set(ui_style_kind_t kind)
{
    s_kind = kind;
}

ui_style_kind_t ui_style_get(void)
{
    return s_kind;
}

bool ui_style_has_depth(void)
{
    return s_kind == UI_STYLE_GLOW || s_kind == UI_STYLE_GLASS;
}

bool ui_style_is_flat(void)
{
    return s_kind == UI_STYLE_FLAT;
}

bool ui_style_is_glow(void)
{
    return s_kind == UI_STYLE_GLOW;
}

bool ui_style_is_glass(void)
{
    return s_kind == UI_STYLE_GLASS;
}

bool ui_style_is_matte(void)
{
    return s_kind == UI_STYLE_MATTE;
}

const ui_style_traits_t *ui_style_traits(void)
{
    static const ui_style_traits_t s_plain = {0};
    static const ui_style_traits_t s_matte = {
        .split_wide_tiles = true,
        .touch_tracks = true,
        .value_beside_slider = true,
        .caps_titles = true,
        .single_line_titles = true,
        .round_switches = true,
        .frameless_page = true,
        .plain_tracks = true,
        .no_slider_knob = true,
        .adaptive_control_icons = true,
    };
    switch (s_kind) {
    case UI_STYLE_MATTE: return &s_matte;
    case UI_STYLE_FLAT:
    case UI_STYLE_GLOW:
    case UI_STYLE_GLASS: break;
    }
    return &s_plain;
}

/* Only Matte defines its own colours today; every other style returns the widget's fallback. */
static const theme_palette_t *own_palette(void)
{
    return ui_style_is_matte() ? theme_palette_active() : NULL;
}

lv_color_t ui_style_page_bg(lv_color_t fallback)
{
    const theme_palette_t *p = own_palette();
    return p != NULL ? lv_color_hex(p->matte_page_bg) : fallback;
}

lv_color_t ui_style_control_bg(lv_color_t fallback)
{
    const theme_palette_t *p = own_palette();
    return p != NULL ? lv_color_hex(p->matte_track) : fallback;
}

lv_color_t ui_style_control_on(lv_color_t fallback)
{
    const theme_palette_t *p = own_palette();
    return p != NULL ? lv_color_hex(p->matte_card_on) : fallback;
}

lv_color_t ui_style_accent(lv_color_t fallback)
{
    const theme_palette_t *p = own_palette();
    return p != NULL ? lv_color_hex(p->matte_accent) : fallback;
}

lv_color_t ui_style_switch_knob(lv_color_t fallback)
{
    return own_palette() != NULL ? lv_color_hex(0xFFFFFF) : fallback;
}

lv_color_t ui_style_value_on_fill(lv_color_t fallback)
{
    return own_palette() != NULL ? lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY) : fallback;
}

lv_color_t ui_style_slider_fill(bool active, lv_color_t fallback, lv_color_t track)
{
    const theme_palette_t *p = own_palette();
    if (p == NULL) {
        return fallback;
    }
    /* Keep the fill apart from the style's card and track, even for an inactive slider. */
    const lv_color_t accent = lv_color_hex(p->matte_accent);
    return active ? accent : lv_color_mix(accent, track, LV_OPA_50);
}

typedef struct {
    char plain[192];
} title_fit_t;

static void title_fit_apply(lv_obj_t *label, const title_fit_t *fit)
{
    const char *use = fit->plain;
    char caps[sizeof(fit->plain) * 2];
    if (ui_style_traits()->caps_titles) {
        matte_title_uppercase(caps, sizeof(caps), fit->plain);
        use = caps;
        const int32_t avail = lv_obj_get_content_width(label);
        if (avail > 0) {
            lv_point_t size;
            lv_text_get_size(&size, caps, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                             lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                             lv_obj_get_style_text_line_space(label, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > avail) use = fit->plain;
        }
    }
    if (strcmp(lv_label_get_text(label), use) != 0) {
        lv_label_set_text(label, use);
    }
}

static void title_fit_event_cb(lv_event_t *event)
{
    lv_obj_t *label = lv_event_get_target(event);
    title_fit_t *fit = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        lv_free(fit);
    } else {
        title_fit_apply(label, fit);
    }
}

void ui_style_title_fit(lv_obj_t *label, const char *plain)
{
    if (label == NULL) {
        return;
    }
    title_fit_t *fit = lv_malloc(sizeof(*fit));
    if (fit == NULL) {
        lv_label_set_text(label, plain != NULL ? plain : "");
        return;
    }
    snprintf(fit->plain, sizeof(fit->plain), "%s", plain != NULL ? plain : "");
    lv_obj_add_event_cb(label, title_fit_event_cb, LV_EVENT_SIZE_CHANGED, fit);
    lv_obj_add_event_cb(label, title_fit_event_cb, LV_EVENT_DELETE, fit);
    title_fit_apply(label, fit);
}

const char *ui_style_title(char *buf, size_t buf_len, const char *text)
{
    if (text == NULL || buf == NULL || buf_len == 0 || !ui_style_traits()->caps_titles) {
        return text;
    }
    matte_title_uppercase(buf, buf_len, text);
    return buf;
}

bool ui_style_matte_dark_surfaces(void)
{
    return ui_style_is_matte() &&
           lv_color_brightness(lv_color_hex(theme_palette_active()->content_bg)) < 128;
}

/* The semantic state is kept on the card as LVGL's first user state (LV_STATE_USER_1), instead of
 * being guessed from the card's current colour. No style selector uses that state. */

static lv_color_t matte_card_color(const lv_obj_t *card)
{
    const theme_palette_t *palette = theme_palette_active();
    return lv_color_hex(ui_style_card_is_active(card) ? palette->matte_card_on : palette->matte_card_off);
}

void ui_style_card_set_active(lv_obj_t *card, bool active)
{
    if (card == NULL) {
        return;
    }
    lv_obj_set_state_user_1(card, active);
    if (ui_style_is_matte()) {
        lv_obj_set_style_bg_color(card, matte_card_color(card), LV_PART_MAIN);
    }
}

bool ui_style_card_is_active(const lv_obj_t *card)
{
    return card != NULL && lv_obj_is_state_user_1(card);
}

/* Flat cards: an explicit, crisper border than the shared base style's default (theme_default.c's
 * s_style_card, the same border every card starts from regardless of style) and a hard, defensive
 * shadow_width 0, so flat reads as its own deliberate "precise 2D" look instead of looking like
 * glow or glass with their effects simply switched off -- a real design review's complaint,
 * confirmed by reading theme_default.c: the base border is identical for all three styles and
 * only glow/glass add anything on top of it, so flat and glow shared the exact same card outline.
 */
static void flat_card(lv_obj_t *card)
{
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(card, UI_STYLE_FLAT_CARD_RADIUS, LV_PART_MAIN);
}

/* Glass cards: the tile background turns translucent with a light edge, over
 * the tinted page background. Called after a widget is created and after each
 * state update (widgets restyle their background themselves); no-op for the
 * other styles.
 *
 * Frosting: a light tint mixed into the card's own background reads as "frosted glass"; a lower
 * opacity of the plain dark color alone (the previous version of this function) just reads as
 * "dim", especially over the uniform dark background the widget gallery screenshots it against
 * -- a real design review's complaint, confirmed side by side with flat there.
 * Light edge: only the top and left side lit, as if light falls from one direction, instead of a
 * uniform all-around outline, which reads as a plain border rather than a glass edge.
 *
 * Idempotent: the frosting mixes white into the card's *current* colour, and this runs after every
 * state update. Widgets that set their background on each update start from their base colour
 * again, but the room card and the car tile set theirs only once at creation, so each update
 * lightened them further until they were near grey (seen in the gallery). The frosted colour is
 * remembered in the card's outline colour (cards never draw an outline, width 0) and the card is
 * frosted again only when the widget has since changed its background. */
static void glass_card(lv_obj_t *card)
{
    lv_color_t base = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
    /* Unchanged since the last call: the colour is already frosted, do not mix again. */
    if (!lv_color_eq(base, lv_obj_get_style_outline_color(card, LV_PART_MAIN))) {
        lv_color_t frosted = lv_color_mix(lv_color_white(), base, 32);
        lv_obj_set_style_outline_color(card, frosted, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, frosted, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_opa(card, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_40, LV_PART_MAIN);
}

/* Matte cards: opaque, no outline or shadow, a muted tint for an active entity. Widgets set their
 * own background on every update, so the Matte colour is set again each time from the card's
 * explicit state (ui_style_card_set_active()). */
static void matte_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, matte_card_color(card), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, UI_STYLE_MATTE_CARD_RADIUS, LV_PART_MAIN);
}

void ui_style_apply_card(lv_obj_t *card)
{
    if (card == NULL) {
        return;
    }
    switch (ui_style_get()) {
    case UI_STYLE_FLAT: flat_card(card); return;
    case UI_STYLE_GLASS: glass_card(card); return;
    case UI_STYLE_MATTE: matte_card(card); return;
    case UI_STYLE_GLOW: return;
    }
}

void ui_style_button(lv_obj_t *btn, lv_color_t bg, lv_color_t border, lv_color_t pressed_bg)
{
    if (btn == NULL) {
        return;
    }
    const lv_style_selector_t def = LV_PART_MAIN | LV_STATE_DEFAULT;
    const lv_style_selector_t prs = LV_PART_MAIN | LV_STATE_PRESSED;

    /* Switch, not if/else-by-elimination: with -Wswitch (part of -Wall, and warnings are errors in
     * this project) the compiler refuses to build once a style is added here without adding its
     * case, instead of that new style silently falling through as "glow". */
    switch (ui_style_get()) {
    case UI_STYLE_FLAT:
        /* Classic: flat fill, thin border, no shadow. */
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, def);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, prs);
        lv_obj_set_style_bg_color(btn, bg, def);
        lv_obj_set_style_bg_color(btn, pressed_bg, prs);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, def);
        lv_obj_set_style_border_width(btn, 1, prs);
        lv_obj_set_style_border_color(btn, border, def);
        lv_obj_set_style_border_color(btn, border, prs);
        lv_obj_set_style_border_opa(btn, LV_OPA_80, def);
        lv_obj_set_style_border_opa(btn, LV_OPA_80, prs);
        lv_obj_set_style_shadow_width(btn, 0, def);
        lv_obj_set_style_shadow_width(btn, 0, prs);
        lv_obj_set_style_outline_width(btn, 0, def);
        lv_obj_set_style_outline_width(btn, 0, prs);
        return;
    case UI_STYLE_MATTE:
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, def);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, prs);
        lv_obj_set_style_bg_color(btn, bg, def);
        lv_obj_set_style_bg_color(btn, pressed_bg, prs);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, def);
        lv_obj_set_style_border_width(btn, 0, prs);
        lv_obj_set_style_shadow_width(btn, 0, def);
        lv_obj_set_style_shadow_width(btn, 0, prs);
        lv_obj_set_style_outline_width(btn, 0, def);
        lv_obj_set_style_outline_width(btn, 0, prs);
        return;
    case UI_STYLE_GLASS:
        /* Glass: translucent, lit from above, with a light edge and a soft shadow. */
        lv_obj_set_style_bg_opa(btn, LV_OPA_70, def);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80, prs);
        lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), bg, 70), def);
        lv_obj_set_style_bg_grad_color(btn, lv_color_mix(lv_color_white(), bg, 24), def);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), pressed_bg, 50), prs);
        lv_obj_set_style_bg_grad_color(btn, lv_color_mix(lv_color_white(), pressed_bg, 20), prs);
        lv_obj_set_style_border_width(btn, 1, def);
        lv_obj_set_style_border_width(btn, 1, prs);
        lv_obj_set_style_border_color(btn, lv_color_white(), def);
        lv_obj_set_style_border_color(btn, lv_color_white(), prs);
        lv_obj_set_style_border_opa(btn, LV_OPA_40, def);
        lv_obj_set_style_border_opa(btn, LV_OPA_60, prs);
        lv_obj_set_style_shadow_width(btn, 16, def);
        lv_obj_set_style_shadow_offset_y(btn, 5, def);
        lv_obj_set_style_shadow_color(btn, lv_color_black(), def);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_30, def);
        lv_obj_set_style_shadow_width(btn, 4, prs);
        lv_obj_set_style_shadow_offset_y(btn, 1, prs);
        lv_obj_set_style_shadow_color(btn, lv_color_black(), prs);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_20, prs);
        lv_obj_set_style_outline_width(btn, 0, def);
        lv_obj_set_style_outline_width(btn, 0, prs);
        return;
    case UI_STYLE_GLOW:
        break;
    }

    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, def);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, prs);
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), bg, 46), def);
    lv_obj_set_style_bg_grad_color(btn, lv_color_mix(lv_color_black(), bg, 56), def);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, pressed_bg, prs);
    lv_obj_set_style_bg_grad_color(btn, lv_color_mix(lv_color_white(), pressed_bg, 30), prs);

    lv_obj_set_style_border_width(btn, 1, def);
    lv_obj_set_style_border_width(btn, 1, prs);
    lv_obj_set_style_border_color(btn, border, def);
    lv_obj_set_style_border_color(btn, border, prs);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, def);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, prs);

    lv_obj_set_style_shadow_width(btn, 10, def);
    lv_obj_set_style_shadow_offset_y(btn, 3, def);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), def);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_40, def);
    lv_obj_set_style_shadow_width(btn, 2, prs);
    lv_obj_set_style_shadow_offset_y(btn, 1, prs);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), prs);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, prs);

    lv_obj_set_style_outline_width(btn, 0, def);
    lv_obj_set_style_outline_width(btn, 0, prs);
}
