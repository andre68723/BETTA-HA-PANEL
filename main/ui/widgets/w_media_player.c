/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Media player widget. Aggregates title/artist, progress bar,
 * prev / play-pause / next controls and a volume slider for a single
 * HA media_player.* entity.
 */
#include "ui/display_geometry.h"
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_bindings.h"
#include "ha/ha_cover_fetcher.h"

#define W_MP_TAG "w_media_player"

#define MP_FEATURE_SHUFFLE_SET 32768 /* MediaPlayerEntityFeature.SHUFFLE_SET */
#define MP_FEATURE_REPEAT_SET 262144 /* MediaPlayerEntityFeature.REPEAT_SET */
enum { MP_REPEAT_OFF = 0, MP_REPEAT_ALL, MP_REPEAT_ONE };

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *title_label;    /* widget title (from def->title) */
    lv_obj_t *now_title;      /* media_title */
    lv_obj_t *now_artist;     /* media_artist / state */
    lv_obj_t *progress_bar;
    lv_obj_t *pos_label;
    lv_obj_t *dur_label;
    lv_obj_t *btn_prev;
    lv_obj_t *btn_prev_label;
    lv_obj_t *btn_play;
    lv_obj_t *btn_play_label;
    lv_obj_t *btn_next;
    lv_obj_t *btn_next_label;
    lv_obj_t *btn_shuffle;     /* shown only when the player supports it and the card is wide enough */
    lv_obj_t *btn_shuffle_label;
    lv_obj_t *btn_repeat;
    lv_obj_t *btn_repeat_label;
    /* A stream (radio) has no duration: no progress row, no shuffle/repeat, and the cover takes the room. */
    bool no_timeline;
    bool portrait_layout;
    lv_coord_t lay_content_w;
    lv_coord_t lay_cover_size;
    lv_coord_t lay_cover_y;
    lv_coord_t lay_text_y;
    lv_coord_t lay_title_h;
    lv_coord_t lay_prog_h;
    bool extras_fit;           /* the card is wide enough for the shuffle and repeat buttons */
    bool shuffle_supported;
    bool repeat_supported;
    bool shuffle_on;
    int repeat_mode;           /* MP_REPEAT_* */
    lv_obj_t *volume_slider;
    lv_obj_t *volume_icon;
    lv_obj_t *btn_vol_down;
    lv_obj_t *btn_vol_down_label;
    lv_obj_t *btn_vol_up;
    lv_obj_t *btn_vol_up_label;
    lv_color_t accent_color;
    bool is_playing;
    bool unavailable;
    int volume_value;          /* 0..100 */
    int volume_last_sent;
    bool volume_dragging;
    bool volume_suppress;
    int volume_press_x;        /* finger x at press, for relative dragging */
    int volume_press_value;    /* volume (0..100) when the finger went down */
    bool volume_press_valid;
    int duration_s;            /* media_duration in seconds, 0 if unknown */
    int position_s;            /* media_position (seconds) reported by HA */
    time_t position_anchor_s;  /* wall-clock unix timestamp at which position_s was reported */
    int resume_guard_pos_s;    /* local resume anchor while HA catches up */
    int64_t resume_guard_until_ms;
    lv_timer_t *tick_timer;
    bool visible;
    /* Cover art */
    lv_obj_t *cover_img;
    lv_image_dsc_t cover_dsc;        /* data pointer owned by us, may be NULL */
    char cover_url[512];             /* last entity_picture we requested */
    char pending_cover_url[512];
    bool cover_request_deferred;
    bool cover_request_inflight;
    uint8_t cover_fail_count;
    int64_t cover_retry_after_ms;
    uint32_t cover_dominant_rgb;     /* 0 when no cover tint is applied */
} w_mp_ctx_t;

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int64_t mp_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void *mp_calloc(size_t count, size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
}

static bool mp_is_visible(const w_mp_ctx_t *ctx)
{
    return ctx != NULL && ctx->visible && ctx->card != NULL && lv_obj_is_visible(ctx->card);
}

static void mp_defer_cover_request(w_mp_ctx_t *ctx, const char *url)
{
    if (ctx == NULL || url == NULL || url[0] == '\0') {
        return;
    }
    snprintf(ctx->pending_cover_url, sizeof(ctx->pending_cover_url), "%s", url);
    ctx->cover_request_deferred = true;
}

static bool mp_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int mp_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool mp_parse_hex_color(const char *text, lv_color_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }
    const char *p = text;
    if (p[0] == '#') {
        p++;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (strlen(p) != 6) return false;
    for (size_t i = 0; i < 6; i++) {
        if (!mp_is_hex_digit(p[i])) return false;
    }
    int r_hi = mp_hex_nibble(p[0]);
    int r_lo = mp_hex_nibble(p[1]);
    int g_hi = mp_hex_nibble(p[2]);
    int g_lo = mp_hex_nibble(p[3]);
    int b_hi = mp_hex_nibble(p[4]);
    int b_lo = mp_hex_nibble(p[5]);
    uint32_t rgb = (uint32_t)(((r_hi << 4) | r_lo) << 16) | (uint32_t)(((g_hi << 4) | g_lo) << 8) |
                   (uint32_t)((b_hi << 4) | b_lo);
    *out = lv_color_hex(rgb);
    return true;
}

static void format_time(int seconds, char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) return;
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        snprintf(out, out_sz, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(out, out_sz, "%d:%02d", m, s);
    }
}

static lv_color_t mp_theme_surface_fill(uint8_t mix)
{
    return lv_color_mix(lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), mix);
}

static lv_color_t mp_pressed_variant(lv_color_t color)
{
    return lv_color_brightness(color) > 127 ? lv_color_darken(color, 28) : lv_color_lighten(color, 28);
}

static lv_color_t mp_tint_card_bg_v2(uint32_t dominant_rgb)
{
    return lv_color_mix(lv_color_hex(dominant_rgb & 0xFFFFFFU), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 64);
}

/* Mix a dominant RGB color with the stock card background to obtain a muted
 * tint: ~25% cover color + ~75% card bg.  The result stays dark enough for
 * the text overlay to keep contrast while still giving each title its own
 * mood. */
static lv_color_t __attribute__((unused)) mp_tint_card_bg(uint32_t dominant_rgb)
{
    uint32_t card = APP_UI_COLOR_CARD_BG_OFF;
    uint8_t dr = (dominant_rgb >> 16) & 0xFF;
    uint8_t dg = (dominant_rgb >> 8) & 0xFF;
    uint8_t db = dominant_rgb & 0xFF;
    uint8_t cr = (card >> 16) & 0xFF;
    uint8_t cg = (card >> 8) & 0xFF;
    uint8_t cb = card & 0xFF;
    const uint32_t w = 56; /* /256 ≈ 22% cover */
    uint8_t r = (uint8_t)((dr * w + cr * (256 - w)) / 256);
    uint8_t g = (uint8_t)((dg * w + cg * (256 - w)) / 256);
    uint8_t b = (uint8_t)((db * w + cb * (256 - w)) / 256);
    return lv_color_make(r, g, b);
}

/* Parse ISO-8601 timestamp like "2026-04-22T12:34:56.789+00:00" or "...Z".
 * Returns true on success, storing unix-seconds (UTC) in *out. */
static bool parse_iso8601_utc(const char *s, time_t *out)
{
    if (s == NULL || out == NULL) return false;
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    int consumed = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d%n", &year, &mon, &day, &hour, &min, &sec, &consumed) < 6) {
        return false;
    }
    const char *p = s + consumed;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    int tz_offset_s = 0;
    if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        p++;
        int tz_h = 0, tz_m = 0;
        if (sscanf(p, "%2d:%2d", &tz_h, &tz_m) == 2 || sscanf(p, "%2d%2d", &tz_h, &tz_m) == 2) {
            tz_offset_s = sign * (tz_h * 3600 + tz_m * 60);
        }
    }

    /* Compute days since epoch (Gregorian, Howard Hinnant's algorithm). */
    int y = year;
    int m = mon;
    if (m <= 2) { y -= 1; m += 12; }
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153U * (unsigned)(m - 3) + 2U) / 5U + (unsigned)day - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    long days = era * 146097L + (long)doe - 719468L;
    time_t t = (time_t)(days * 86400L + hour * 3600L + min * 60L + sec - tz_offset_s);
    *out = t;
    return true;
}

static int mp_current_position_s(const w_mp_ctx_t *ctx)
{
    if (ctx == NULL) return 0;
    int pos = ctx->position_s;
    if (ctx->is_playing && !ctx->unavailable && ctx->position_anchor_s > 0) {
        time_t now_s = time(NULL);
        if (now_s > ctx->position_anchor_s) {
            long elapsed = (long)(now_s - ctx->position_anchor_s);
            if (elapsed > 0 && elapsed < 24 * 3600) {
                pos += (int)elapsed;
            }
        }
    }
    if (ctx->duration_s > 0 && pos > ctx->duration_s) {
        pos = ctx->duration_s;
    }
    if (pos < 0) pos = 0;
    return pos;
}

static bool mp_media_identity_matches(const w_mp_ctx_t *ctx, const char *title_text, int duration_s, const char *pic_url)
{
    if (ctx == NULL) return false;

    bool matched = false;
    if (title_text != NULL && title_text[0] != '\0' && ctx->now_title != NULL) {
        const char *current_title = lv_label_get_text(ctx->now_title);
        if (current_title != NULL && current_title[0] != '\0') {
            if (strcmp(current_title, title_text) != 0) {
                return false;
            }
            matched = true;
        }
    }

    if (duration_s > 0 && ctx->duration_s > 0) {
        if (duration_s != ctx->duration_s) {
            return false;
        }
        matched = true;
    }

    if (pic_url != NULL && pic_url[0] != '\0' && ctx->cover_url[0] != '\0') {
        if (strcmp(ctx->cover_url, pic_url) != 0) {
            return false;
        }
        matched = true;
    }

    return matched;
}

static void mp_update_progress_visual(w_mp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->progress_bar == NULL) return;
    int pos = mp_current_position_s(ctx);
    int dur = ctx->duration_s;
    if (dur > 0) {
        int pct = (int)((int64_t)pos * 100 / dur);
        pct = clampi(pct, 0, 100);
        lv_bar_set_value(ctx->progress_bar, pct, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(ctx->progress_bar, 0, LV_ANIM_OFF);
    }
    if (ctx->pos_label != NULL) {
        if (dur > 0) {
            char buf[16];
            format_time(pos, buf, sizeof(buf));
            lv_label_set_text(ctx->pos_label, buf);
        } else {
            lv_label_set_text(ctx->pos_label, "");
        }
    }
    if (ctx->dur_label != NULL) {
        if (dur > 0) {
            char buf[16];
            format_time(dur, buf, sizeof(buf));
            lv_label_set_text(ctx->dur_label, buf);
        } else {
            lv_label_set_text(ctx->dur_label, "");
        }
    }
}

static void mp_release_cover(w_mp_ctx_t *ctx);

static void mp_style_control_button(lv_obj_t *btn, lv_obj_t *label, lv_color_t bg, lv_color_t border, lv_color_t text)
{
    if (btn == NULL) {
        return;
    }

    ui_style_button(btn, bg, border, mp_pressed_variant(bg));

    if (label != NULL) {
        lv_obj_set_style_text_color(label, text, LV_PART_MAIN);
    }
}

static void mp_apply_visual(w_mp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) return;

    lv_color_t card_bg;
    if (ctx->is_playing && !ctx->unavailable && ctx->cover_dominant_rgb != 0) {
        card_bg = mp_tint_card_bg_v2(ctx->cover_dominant_rgb);
    } else {
        card_bg = lv_color_hex(ctx->is_playing && !ctx->unavailable ? APP_UI_COLOR_CARD_BG_ON
                                                                    : APP_UI_COLOR_CARD_BG_OFF);
    }
    lv_obj_set_style_bg_color(ctx->card, card_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, LV_PART_MAIN);
    ui_style_card_set_active(ctx->card, ctx->is_playing && !ctx->unavailable);

    lv_color_t text_primary = lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED : APP_UI_COLOR_TEXT_PRIMARY);
    lv_color_t text_muted = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);

    if (ctx->title_label) {
        lv_obj_set_style_text_color(ctx->title_label, text_primary, LV_PART_MAIN);
    }
    if (ctx->now_title) {
        lv_obj_set_style_text_color(ctx->now_title, text_primary, LV_PART_MAIN);
    }
    if (ctx->now_artist) {
        lv_obj_set_style_text_color(ctx->now_artist, text_muted, LV_PART_MAIN);
    }
    if (ctx->pos_label) {
        lv_obj_set_style_text_color(ctx->pos_label, text_muted, LV_PART_MAIN);
    }
    if (ctx->dur_label) {
        lv_obj_set_style_text_color(ctx->dur_label, text_muted, LV_PART_MAIN);
    }
    if (ctx->volume_icon) {
        lv_obj_set_style_text_color(ctx->volume_icon, text_muted, LV_PART_MAIN);
    }

    lv_color_t accent = ctx->unavailable ? lv_color_hex(APP_UI_COLOR_CARD_BORDER) : ctx->accent_color;
    lv_color_t control_surface = ui_style_control_color(UI_CONTROL_ACTION, !ctx->unavailable);
    lv_color_t track_surface = mp_theme_surface_fill(196);
    lv_color_t control_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    /* Auto-contrast from the actual button surface (ui_style_button_icon_color()), not a fixed
     * guess: prev/next and volume +/- used to hardcode CARD_ICON_OFF regardless of how light or
     * dark control_surface's theme mix came out, unlike the play button below (already correct,
     * via this same formula under its own local name). */
    lv_color_t control_text = ctx->unavailable ? lv_color_hex(APP_UI_COLOR_TEXT_MUTED) : ui_style_button_icon_color(control_surface);
    lv_color_t play_bg = ui_style_control_fill(UI_CONTROL_PRIMARY, !ctx->unavailable, accent);
    lv_color_t play_border = control_border;
    lv_color_t play_text = ctx->unavailable ? lv_color_hex(APP_UI_COLOR_TEXT_MUTED) : ui_style_button_icon_color(play_bg);
    lv_color_t slider_knob = ctx->unavailable
                                 ? lv_color_hex(APP_UI_COLOR_TEXT_MUTED)
                                 : lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), accent, 72);

    if (ctx->progress_bar) {
        lv_obj_set_style_radius(ctx->progress_bar, ui_style_track_radius(), LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->progress_bar, ui_style_track_radius(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ctx->progress_bar, track_surface, LV_PART_MAIN);
        /* Glass is translucent everywhere else; this track kept a leftover full-opacity fill. */
        lv_obj_set_style_bg_opa(ctx->progress_bar, ui_style_is_glass() ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->progress_bar, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ctx->progress_bar, 0, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(ctx->progress_bar, true, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->progress_bar, accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(ctx->progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(ctx->progress_bar, 0, LV_PART_INDICATOR);
    }
    if (ctx->volume_slider) {
        lv_obj_set_style_radius(ctx->volume_slider, ui_style_track_radius(), LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->volume_slider, ui_style_track_radius(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(ctx->volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_bg_color(ctx->volume_slider, track_surface, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ctx->volume_slider, track_surface, LV_PART_MAIN | LV_STATE_PRESSED);
        /* Glass is translucent everywhere else; this track kept a leftover full-opacity fill. */
        const lv_opa_t volume_track_opa = ui_style_is_glass() ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_bg_opa(ctx->volume_slider, volume_track_opa, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ctx->volume_slider, volume_track_opa, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(ctx->volume_slider, 0, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(ctx->volume_slider, true, LV_PART_MAIN);

        lv_obj_set_style_bg_color(ctx->volume_slider, accent, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ctx->volume_slider, accent, LV_PART_INDICATOR | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_INDICATOR | LV_STATE_PRESSED);

        lv_obj_set_style_bg_color(ctx->volume_slider, slider_knob, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ctx->volume_slider, slider_knob, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_90, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(ctx->volume_slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_set_style_transform_width(ctx->volume_slider, -4, LV_PART_KNOB);
        lv_obj_set_style_transform_height(ctx->volume_slider, -4, LV_PART_KNOB);
    }

    if (ctx->btn_play_label) {
        lv_label_set_text(ctx->btn_play_label, ctx->is_playing ? APP_SYMBOL_PAUSE : APP_SYMBOL_PLAY);
    }

    mp_style_control_button(ctx->btn_prev, ctx->btn_prev_label, control_surface, control_border, control_text);
    mp_style_control_button(ctx->btn_play, ctx->btn_play_label, play_bg, play_border, play_text);
    mp_style_control_button(ctx->btn_next, ctx->btn_next_label, control_surface, control_border, control_text);
    /* Shuffle and repeat: the glyph is accent-coloured while the mode is on. */
    const lv_color_t extra_off_text = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    mp_style_control_button(ctx->btn_shuffle, ctx->btn_shuffle_label, control_surface, control_border,
                            ctx->shuffle_on ? accent : extra_off_text);
    mp_style_control_button(ctx->btn_repeat, ctx->btn_repeat_label, control_surface, control_border,
                            ctx->repeat_mode != MP_REPEAT_OFF ? accent : extra_off_text);
    if (ctx->btn_repeat_label) {
        lv_label_set_text(ctx->btn_repeat_label, ctx->repeat_mode == MP_REPEAT_ONE ? APP_SYMBOL_REPEAT_ONCE : APP_SYMBOL_REPEAT);
    }
    if (ctx->btn_shuffle) lv_obj_set_hidden(ctx->btn_shuffle, !(ctx->extras_fit && ctx->shuffle_supported && !ctx->no_timeline));
    if (ctx->btn_repeat) lv_obj_set_hidden(ctx->btn_repeat, !(ctx->extras_fit && ctx->repeat_supported && !ctx->no_timeline));
    mp_style_control_button(ctx->btn_vol_down, ctx->btn_vol_down_label, control_surface, control_border, control_text);
    mp_style_control_button(ctx->btn_vol_up, ctx->btn_vol_up_label, control_surface, control_border, control_text);

    /* disable controls visually when unavailable */
    const lv_opa_t control_opa = ctx->unavailable ? LV_OPA_30 : LV_OPA_COVER;
    if (ctx->btn_prev) lv_obj_set_style_opa(ctx->btn_prev, control_opa, LV_PART_MAIN);
    if (ctx->btn_play) lv_obj_set_style_opa(ctx->btn_play, control_opa, LV_PART_MAIN);
    if (ctx->btn_next) lv_obj_set_style_opa(ctx->btn_next, control_opa, LV_PART_MAIN);
    if (ctx->btn_shuffle) lv_obj_set_style_opa(ctx->btn_shuffle, control_opa, LV_PART_MAIN);
    if (ctx->btn_repeat) lv_obj_set_style_opa(ctx->btn_repeat, control_opa, LV_PART_MAIN);
    if (ctx->btn_vol_down) lv_obj_set_style_opa(ctx->btn_vol_down, control_opa, LV_PART_MAIN);
    if (ctx->btn_vol_up) lv_obj_set_style_opa(ctx->btn_vol_up, control_opa, LV_PART_MAIN);
    if (ctx->volume_slider) {
        lv_obj_set_style_opa(ctx->volume_slider, control_opa, LV_PART_MAIN);
        lv_obj_set_style_opa(ctx->volume_slider, control_opa, LV_PART_INDICATOR);
        lv_obj_set_style_opa(ctx->volume_slider, control_opa, LV_PART_KNOB);
    }
    if (ctx->progress_bar) {
        lv_obj_set_style_opa(ctx->progress_bar, control_opa, LV_PART_MAIN);
        lv_obj_set_style_opa(ctx->progress_bar, control_opa, LV_PART_INDICATOR);
    }

    mp_update_progress_visual(ctx);
}

static void mp_tick_cb(lv_timer_t *timer)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL) return;
    if (!ctx->is_playing || ctx->unavailable || ctx->duration_s <= 0) return;
    mp_update_progress_visual(ctx);
}

static void mp_play_clicked(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    int current_pos = mp_current_position_s(ctx);
    bool next_playing = !ctx->is_playing;
    if (ui_bindings_media_player_action(ctx->entity_id, UI_BINDINGS_MEDIA_ACTION_PLAY_PAUSE) == ESP_OK) {
        ctx->position_s = current_pos;
        ctx->is_playing = next_playing;
        ctx->position_anchor_s = next_playing ? time(NULL) : 0;
        ctx->resume_guard_pos_s = current_pos;
        ctx->resume_guard_until_ms = mp_now_ms() + 15000;
        mp_apply_visual(ctx);
    }
}

static void mp_prev_clicked(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    (void)ui_bindings_media_player_action(ctx->entity_id, UI_BINDINGS_MEDIA_ACTION_PREVIOUS);
}

static const char *mp_repeat_name(int mode)
{
    return mode == MP_REPEAT_ALL ? "all" : (mode == MP_REPEAT_ONE ? "one" : "off");
}

static void mp_shuffle_clicked(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    const bool next = !ctx->shuffle_on;
    if (ui_bindings_media_player_set_shuffle(ctx->entity_id, next) == ESP_OK) {
        ctx->shuffle_on = next; /* until Home Assistant reports the real state */
        mp_apply_visual(ctx);
    }
}

/* off -> all -> one -> off */
static void mp_repeat_clicked(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    const int next = (ctx->repeat_mode + 1) % 3;
    if (ui_bindings_media_player_set_repeat(ctx->entity_id, mp_repeat_name(next)) == ESP_OK) {
        ctx->repeat_mode = next;
        mp_apply_visual(ctx);
    }
}

static void mp_next_clicked(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    (void)ui_bindings_media_player_action(ctx->entity_id, UI_BINDINGS_MEDIA_ACTION_NEXT);
}

/* LVGL declares this in a private header only; it is a public function. */
void lv_obj_set_ext_click_area(lv_obj_t *obj, int32_t size);

/* Volume after a relative drag: the level at press time plus the finger's
 * horizontal travel as a share of the slider width. Returns -1 if the pointer
 * is not available. LVGL's own slider handling jumps the value to the finger
 * position on press/release, so a slightly misplaced touch (or a finger that
 * slips right) used to set an arbitrary, often maximum, volume. */
static int mp_volume_from_drag(const w_mp_ctx_t *ctx, lv_obj_t *slider)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL || !ctx->volume_press_valid) {
        return -1;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t w = lv_obj_get_width(slider);
    if (w <= 0) {
        w = 1;
    }
    const int delta = (int)(((int64_t)(p.x - ctx->volume_press_x) * 100) / w);
    return clampi(ctx->volume_press_value + delta, 0, 100);
}

static void mp_volume_event_cb(lv_event_t *event)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        if (ctx->tick_timer) {
            lv_timer_del(ctx->tick_timer);
            ctx->tick_timer = NULL;
        }
        ha_cover_fetcher_cancel(ctx);
        mp_release_cover(ctx);
        heap_caps_free(ctx);
        return;
    }
    if (ctx->volume_suppress || ctx->unavailable) return;
    lv_obj_t *slider = lv_event_get_target(event);
    if (slider == NULL) return;

    if (code == LV_EVENT_PRESSED) {
        ctx->volume_dragging = true;
        lv_indev_t *indev = lv_indev_active();
        if (indev != NULL) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            ctx->volume_press_x = p.x;
            ctx->volume_press_value = ctx->volume_value;
            ctx->volume_press_valid = true;
        }
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        ctx->volume_dragging = true;
        /* Replace LVGL's absolute "value = finger position" with the relative
         * value, so pressing, tapping or sliding off the end never jumps. */
        int relative = mp_volume_from_drag(ctx, slider);
        if (relative >= 0 && relative != lv_slider_get_value(slider)) {
            lv_slider_set_value(slider, relative, LV_ANIM_OFF);
        }
        ctx->volume_value = clampi(lv_slider_get_value(slider), 0, 100);
    } else if (code == LV_EVENT_RELEASED) {
        int next_value = clampi(lv_slider_get_value(slider), 0, 100);
        ctx->volume_dragging = false;
        ctx->volume_press_valid = false;
        if (next_value != ctx->volume_last_sent) {
            if (ui_bindings_set_slider_value(ctx->entity_id, next_value) == ESP_OK) {
                ctx->volume_last_sent = next_value;
                ctx->volume_value = next_value;
            } else {
                ctx->volume_suppress = true;
                lv_slider_set_value(slider, ctx->volume_value, LV_ANIM_OFF);
                ctx->volume_suppress = false;
            }
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        ctx->volume_dragging = false;
        ctx->volume_press_valid = false;
    }
}

#define MP_VOLUME_STEP 5

static void mp_volume_step(w_mp_ctx_t *ctx, int delta)
{
    if (ctx == NULL || ctx->unavailable) return;
    int next_value = clampi(ctx->volume_value + delta, 0, 100);
    if (next_value == ctx->volume_last_sent && next_value == ctx->volume_value) return;
    if (ui_bindings_set_slider_value(ctx->entity_id, next_value) == ESP_OK) {
        ctx->volume_last_sent = next_value;
        ctx->volume_value = next_value;
        if (ctx->volume_slider) {
            ctx->volume_suppress = true;
            lv_slider_set_value(ctx->volume_slider, next_value, LV_ANIM_OFF);
            ctx->volume_suppress = false;
        }
    }
}

static void mp_vol_down_clicked(lv_event_t *event)
{
    mp_volume_step((w_mp_ctx_t *)lv_event_get_user_data(event), -MP_VOLUME_STEP);
}

static void mp_vol_up_clicked(lv_event_t *event)
{
    mp_volume_step((w_mp_ctx_t *)lv_event_get_user_data(event), MP_VOLUME_STEP);
}

static lv_obj_t *mp_create_icon_button(lv_obj_t *parent, const char *symbol, w_mp_ctx_t *ctx, lv_event_cb_t cb,
                                        lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 54, 54);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(label);
    if (out_label) *out_label = label;
    return btn;
}

static bool mp_state_is_unavailable(const char *state)
{
    if (state == NULL) return false;
    return strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

static void mp_release_cover(w_mp_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->cover_dsc.data != NULL) {
        heap_caps_free((void *)ctx->cover_dsc.data);
        ctx->cover_dsc.data = NULL;
        ctx->cover_dsc.data_size = 0;
    }
}

static void mp_cover_show_placeholder(w_mp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->cover_img == NULL) return;
    lv_image_set_src(ctx->cover_img, NULL);
    lv_obj_set_style_bg_color(ctx->cover_img, mp_theme_surface_fill(184), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->cover_img, LV_OPA_COVER, LV_PART_MAIN);
}

static void mp_note_cover_failure(w_mp_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->cover_fail_count < 6) {
        ctx->cover_fail_count++;
    }
    int64_t backoff_ms = 5000LL * (int64_t)ctx->cover_fail_count;
    if (backoff_ms > 60000LL) {
        backoff_ms = 60000LL;
    }
    ctx->cover_retry_after_ms = mp_now_ms() + backoff_ms;
}

static void mp_cover_cb(void *user, const ha_cover_result_t *result)
{
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)user;
    if (ctx == NULL) return;
    ctx->cover_request_inflight = false;
    if (result == NULL || !result->valid) {
        ESP_LOGW(W_MP_TAG, "cover result invalid for %s", ctx->entity_id);
        mp_note_cover_failure(ctx);
        mp_cover_show_placeholder(ctx);
        ctx->cover_dominant_rgb = 0;
        mp_apply_visual(ctx);
        return;
    }
    /* Take ownership of the decoded buffer. */
    mp_release_cover(ctx);
    ctx->cover_dsc = result->image;
    ctx->cover_dominant_rgb = result->dominant_rgb;
    ctx->cover_fail_count = 0;
    ctx->cover_retry_after_ms = 0;
    ESP_LOGD(W_MP_TAG, "cover result applied for %s: cf=%u out=%ux%u stride=%u size=%u dominant=%06x",
             ctx->entity_id,
             (unsigned)ctx->cover_dsc.header.cf,
             (unsigned)ctx->cover_dsc.header.w,
             (unsigned)ctx->cover_dsc.header.h,
             (unsigned)ctx->cover_dsc.header.stride,
             (unsigned)ctx->cover_dsc.data_size,
             (unsigned)(ctx->cover_dominant_rgb & 0xFFFFFFU));

    if (ctx->cover_img != NULL) {
        lv_image_set_src(ctx->cover_img, &ctx->cover_dsc);
        /* Scale decoded image to the widget's cover square. */
        lv_coord_t tgt_w = lv_obj_get_width(ctx->cover_img);
        lv_coord_t tgt_h = lv_obj_get_height(ctx->cover_img);
        if (tgt_w > 0 && ctx->cover_dsc.header.w > 0) {
            int32_t zoom_w = (int32_t)((int64_t)tgt_w * 256 / ctx->cover_dsc.header.w);
            int32_t zoom_h = (int32_t)((int64_t)tgt_h * 256 / ctx->cover_dsc.header.h);
            int32_t zoom = zoom_w < zoom_h ? zoom_w : zoom_h;
            if (zoom < 16) zoom = 16;
            lv_image_set_scale(ctx->cover_img, (uint16_t)zoom);
        }
        lv_obj_set_style_bg_opa(ctx->cover_img, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    mp_apply_visual(ctx);
}

/* Switch between a track (progress row, shuffle/repeat) and a stream (none of them; in the portrait layout the cover
 * grows into the freed row and the text follows it). The cover is fetched again at its new size. */
static void mp_set_timeline(w_mp_ctx_t *ctx, bool has_timeline)
{
    if (ctx == NULL || ctx->no_timeline == !has_timeline) return;
    ctx->no_timeline = !has_timeline;
    if (ctx->progress_bar) lv_obj_set_hidden(ctx->progress_bar, ctx->no_timeline);
    if (ctx->pos_label) lv_obj_set_hidden(ctx->pos_label, ctx->no_timeline);
    if (ctx->dur_label) lv_obj_set_hidden(ctx->dur_label, ctx->no_timeline);
    if (!ctx->portrait_layout || ctx->lay_cover_size <= 0 || ctx->cover_img == NULL) return;

    const lv_coord_t grow = ctx->no_timeline ? ctx->lay_prog_h : 0;
    lv_coord_t size = ctx->lay_cover_size + grow;
    if (size > ctx->lay_content_w) size = ctx->lay_content_w;
    const lv_coord_t delta = size - ctx->lay_cover_size;
    const lv_coord_t leftover = grow - delta; /* a cover as wide as the card cannot grow: the rest becomes air */
    const lv_coord_t old_size = lv_obj_get_width(ctx->cover_img);
    lv_obj_set_size(ctx->cover_img, size, size);
    lv_obj_set_pos(ctx->cover_img, (ctx->lay_content_w - size) / 2, ctx->lay_cover_y + leftover / 3);
    const lv_coord_t text_y = ctx->lay_text_y + delta + (leftover * 2) / 3;
    if (ctx->now_title) lv_obj_align(ctx->now_title, LV_ALIGN_TOP_LEFT, 0, text_y);
    if (ctx->now_artist) lv_obj_align(ctx->now_artist, LV_ALIGN_TOP_LEFT, 0, text_y + ctx->lay_title_h);
    if (size != old_size && ctx->cover_url[0] != '\0') {
        ctx->cover_url[0] = '\0'; /* refetch at the new size with the next state update */
        mp_release_cover(ctx);
        mp_cover_show_placeholder(ctx);
    }
}

static void mp_request_cover(w_mp_ctx_t *ctx, const char *url)
{
    if (ctx == NULL || url == NULL || url[0] == '\0') return;
    if (!mp_is_visible(ctx)) {
        mp_defer_cover_request(ctx, url);
        return;
    }

    bool same_url = (strncmp(ctx->cover_url, url, sizeof(ctx->cover_url)) == 0);
    int64_t now_ms = mp_now_ms();
    if (same_url && (ctx->cover_request_inflight || ctx->cover_dsc.data != NULL ||
            now_ms < ctx->cover_retry_after_ms)) {
        /* Same URL already fetched/in progress, or a failed fetch is still
         * backing off. The next state tick may retry after the backoff. */
        return;
    }
    if (!same_url) {
        ctx->cover_fail_count = 0;
        ctx->cover_retry_after_ms = 0;
    }
    ctx->cover_request_deferred = false;
    ctx->pending_cover_url[0] = '\0';
    snprintf(ctx->cover_url, sizeof(ctx->cover_url), "%s", url);
    lv_coord_t tgt = ctx->cover_img ? lv_obj_get_width(ctx->cover_img) : 96;
    if (tgt < 48) tgt = 48;
    ctx->cover_request_inflight = true;
    if (ha_cover_fetcher_request(url, tgt, tgt, mp_cover_cb, ctx) != ESP_OK) {
        ctx->cover_request_inflight = false;
        mp_note_cover_failure(ctx);
    }
}

esp_err_t w_media_player_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    w_mp_ctx_t *ctx = (w_mp_ctx_t *)mp_calloc(1, sizeof(w_mp_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->card = card;
    ctx->accent_color = lv_color_hex(APP_UI_COLOR_STATE_ON);
    lv_color_t parsed_color;
    if (mp_parse_hex_color(def->button_accent_color, &parsed_color) ||
        mp_parse_hex_color(def->slider_accent_color, &parsed_color)) {
        ctx->accent_color = parsed_color;
    }
    ctx->volume_last_sent = -1;

    const lv_coord_t content_w = def->w - 28;
    const lv_coord_t content_h = def->h - 28;

    /* Layout mode selection:
     *  - landscape: widget is clearly wider than tall (e.g. 640×240 strip).
     *    Cover sits on the left with full card height; text + controls fill
     *    the right column.
     *  - portrait/square (default): cover centred at the top taking all
     *    remaining vertical space; text centred below; progress + controls +
     *    volume stacked at the bottom. */
    bool landscape = (content_w >= content_h * 3 / 2 && content_w >= 260 && content_h >= 130);

    bool has_widget_title = !def->hide_name && (def->title[0] != '\0');
    const lv_coord_t widget_title_h = has_widget_title ? lv_font_get_line_height(ui_font_role(UI_TEXT_META)) : 0;
    /* Line heights of the real fonts (20 px text is the floor on the 4-inch class and has taller lines than 16 px). */
    const lv_coord_t now_title_line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t now_artist_line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t now_title_h = now_title_line > 26 ? now_title_line : 26;
    const lv_coord_t now_artist_h = now_artist_line + 2 > 22 ? now_artist_line + 2 : 22;
    /* 10 bar + labels + spacing: the row is as tall as the position/duration labels (larger text grows it) */
    const lv_coord_t progress_row_h = now_title_line + 4 > 34 ? now_title_line + 4 : 34;
    const lv_coord_t controls_row_h = 68; /* 64 play button + 4 gap       */
    /* Finger-sized volume controls. The 10-inch panels are about 150 ppi (46 px buttons are 7.8 mm), the 4-inch panel
     * about 254 ppi (52 px are 5.2 mm); the old 34 px buttons and 14 px bar were 5.8 mm and 3.4 mm / 1.4 mm. */
    const bool large_panel = APP_VIEW_WIDTH >= 1000;
    const lv_coord_t vbtn = large_panel ? 46 : 52;           /* minus / plus buttons */
    const lv_coord_t vslider_h = large_panel ? 20 : 22;      /* slider bar */
    const lv_coord_t volume_row_h = large_panel ? 50 : 56;   /* slider + minus/plus buttons  */

    lv_coord_t cover_x = 0;
    lv_coord_t cover_y = 0;
    lv_coord_t cover_size = 0;

    /* Right (or full) column geometry */
    lv_coord_t col_x = 0;
    lv_coord_t col_w = content_w;
    lv_coord_t col_y_top = 0;     /* first usable y within the column   */
    bool show_artist = true;

    if (landscape) {
        cover_size = content_h;
        const lv_coord_t min_col_w = 170;
        if (content_w - cover_size - 14 < min_col_w) {
            lv_coord_t reduced_cover = content_w - min_col_w - 14;
            if (reduced_cover >= 72 && reduced_cover < cover_size) {
                cover_size = reduced_cover;
            }
        }
        cover_x = 0;
        cover_y = 0;
        col_x = cover_size + 14;
        col_w = content_w - col_x;
        col_y_top = 0;
        if (col_w < 160) {
            landscape = false;
        }
    }

    if (!landscape) {
        /* Height available for the cover in portrait mode after reserving
         * the text + controls stack below it. */
        const lv_coord_t stack_h = now_title_h + now_artist_h + progress_row_h +
                                   controls_row_h + volume_row_h + 20 /* gaps */;
        lv_coord_t avail_h = content_h - widget_title_h - stack_h;
        if (avail_h < 72) avail_h = 72;
        cover_size = avail_h < content_w ? avail_h : content_w;
        if (cover_size < 64) cover_size = 64;
        cover_x = (content_w - cover_size) / 2;
        cover_y = widget_title_h + (has_widget_title ? 6 : 0);
        /* A card taller than the cover can grow (the cover is as wide as the card at most) keeps the rest as air
         * around the cover and between text and progress bar, not as one gap below the artist line. */
        const lv_coord_t spare = avail_h > cover_size ? (avail_h - cover_size) : 0;
        cover_y += spare / 3;
        col_y_top = cover_y + cover_size + 8 + spare / 3;

        /* The 72/64 px floors above exist so the cover never disappears, but at a tile near
         * the widget's minimum size they can ask for more room than stack_h actually reserved,
         * which used to push the artist line (now_artist, "media_artist" or the raw state as a
         * fallback, e.g. "idle") down underneath the progress bar below it -- found via the
         * widget gallery's side-by-side comparison (a 40 px "last-resort" floor on the shrink
         * below used to leave a residual overlap of its own at common sizes like 300x280,
         * confirmed on a real panel via the gallery screenshot: the artist text and the
         * progress track visibly overlapped instead of just abutting). progress/controls/volume
         * are positioned independently, measured up from the card bottom (prog_y below), so
         * nothing here shifts them; shrink the cover instead, as far as it takes (down to 0,
         * i.e. no cover shown at all, rather than stop short and let the overlap happen), so
         * the title+artist stack never reaches that fixed floor. */
        const lv_coord_t prog_y = content_h - volume_row_h - controls_row_h - progress_row_h;
        const lv_coord_t stack_bottom = col_y_top + now_title_h + now_artist_h;
        if (stack_bottom > prog_y) {
            lv_coord_t shrink = stack_bottom - prog_y;
            if (shrink > cover_size) shrink = cover_size; /* never shrink past 0 */
            cover_size -= shrink;
            cover_x = (content_w - cover_size) / 2;
            col_y_top = cover_y + cover_size + (cover_size > 0 ? 8 : 0);
        }
        /* Even without a cover the two text lines may not fit above the progress row (the
         * 260x220 minimum): the artist line gives way to the track title. */
        if (col_y_top + now_title_h + now_artist_h > prog_y) {
            show_artist = false;
        }
        /* Still no room for the track title: the widget's own title gives way to it last. */
        if (col_y_top + now_title_h > prog_y && has_widget_title && cover_size == 0) {
            has_widget_title = false;
            cover_y = 0;
            col_y_top = 0;
        }
    }

    /* Cover image (placeholder until first fetch). */
    lv_obj_t *cover = lv_image_create(card);
    lv_obj_set_size(cover, cover_size, cover_size);
    lv_obj_set_pos(cover, cover_x, cover_y);
    lv_obj_set_style_radius(cover, ui_style_radius(10), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(cover, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cover, mp_theme_surface_fill(184), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    ctx->cover_img = cover;

    /* ---- Widget title (always pinned top-left of full card) ---- */
    if (has_widget_title) {
        lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
        lv_label_set_text(title, def->title);
        lv_obj_set_width(title, content_w);
        lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        ui_widget_name_one_line(title);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
        ctx->title_label = title;
    }

    /* ---- Text + progress + controls + volume stack ----
     * In landscape: stacked inside the right column (col_x, col_w).
     * In portrait: stacked in the full card width below the cover.        */
    const lv_text_align_t text_align = landscape ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
    lv_coord_t y = col_y_top;
    if (landscape && has_widget_title) y += widget_title_h + 4;

    lv_obj_t *now_title = lv_label_create(card);
    lv_label_set_text(now_title, "");
    lv_obj_set_style_text_font(now_title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(now_title, text_align, LV_PART_MAIN);
    lv_label_set_long_mode(now_title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(now_title, now_title_h);
    ctx->now_title = now_title;

    lv_obj_t *now_artist = lv_label_create(card);
    lv_label_set_text(now_artist, "");
    lv_obj_set_style_text_font(now_artist, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(now_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_height(now_artist, now_artist_h);
    lv_obj_set_hidden(now_artist, !show_artist);
    ctx->now_artist = now_artist;

    if (landscape) {
        lv_obj_set_width(now_title, col_w);
        lv_obj_align(now_title, LV_ALIGN_TOP_LEFT, col_x, y);
        y += now_title_h;
        lv_obj_set_width(now_artist, col_w);
        lv_obj_set_style_text_align(now_artist, text_align, LV_PART_MAIN);
        lv_obj_align(now_artist, LV_ALIGN_TOP_LEFT, col_x, y);
    } else {
        lv_obj_set_width(now_title, col_w);
        lv_obj_align(now_title, LV_ALIGN_TOP_LEFT, col_x, y);
        y += now_title_h;
        lv_obj_set_width(now_artist, col_w);
        lv_obj_set_style_text_align(now_artist, text_align, LV_PART_MAIN);
        lv_obj_align(now_artist, LV_ALIGN_TOP_LEFT, col_x, y);
    }

    ctx->portrait_layout = !landscape;
    ctx->lay_content_w = content_w;
    ctx->lay_cover_size = cover_size;
    ctx->lay_cover_y = cover_y;
    ctx->lay_text_y = col_y_top;
    ctx->lay_title_h = now_title_h;
    ctx->lay_prog_h = progress_row_h;

    /* Progress bar (flanked by current position / duration labels). */
    lv_obj_t *progress = lv_bar_create(card);
    lv_bar_set_range(progress, 0, 100);
    lv_bar_set_value(progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(progress, ui_style_track_radius(), LV_PART_MAIN);
    lv_obj_set_style_radius(progress, ui_style_track_radius(), LV_PART_INDICATOR);
    ctx->progress_bar = progress;

    lv_obj_t *pos_label = lv_label_create(card);
    lv_label_set_text(pos_label, "");
    lv_obj_set_style_text_font(pos_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(pos_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    ctx->pos_label = pos_label;

    lv_obj_t *dur_label = lv_label_create(card);
    lv_label_set_text(dur_label, "");
    lv_obj_set_style_text_font(dur_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(dur_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    ctx->dur_label = dur_label;

    /* Controls row */
    ctx->btn_prev = mp_create_icon_button(card, APP_SYMBOL_PREV, ctx, mp_prev_clicked, &ctx->btn_prev_label);
    ctx->btn_play = mp_create_icon_button(card, APP_SYMBOL_PLAY, ctx, mp_play_clicked, &ctx->btn_play_label);
    ctx->btn_next = mp_create_icon_button(card, APP_SYMBOL_NEXT, ctx, mp_next_clicked, &ctx->btn_next_label);
    /* MDI's transport glyphs are small in their em square; one symbol size up. */
    lv_obj_set_style_text_font(ctx->btn_prev_label, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->btn_play_label, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->btn_next_label, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
    ctx->btn_shuffle = mp_create_icon_button(card, APP_SYMBOL_SHUFFLE, ctx, mp_shuffle_clicked, &ctx->btn_shuffle_label);
    ctx->btn_repeat = mp_create_icon_button(card, APP_SYMBOL_REPEAT, ctx, mp_repeat_clicked, &ctx->btn_repeat_label);
    lv_obj_set_style_text_font(ctx->btn_shuffle_label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->btn_repeat_label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_set_hidden(ctx->btn_shuffle, true);
    lv_obj_set_hidden(ctx->btn_repeat, true);
    const lv_coord_t play_size = (landscape && col_w < 190) ? 56 : 64;
    const lv_coord_t side_size = (landscape && col_w < 190) ? 48 : 54;
    lv_obj_set_size(ctx->btn_prev, side_size, side_size);
    lv_obj_set_size(ctx->btn_play, play_size, play_size);
    lv_obj_set_size(ctx->btn_next, side_size, side_size);

    /* Volume slider + icon */
    lv_obj_t *vol_icon = lv_label_create(card);
    lv_label_set_text(vol_icon, APP_SYMBOL_VOLUME);
    lv_obj_set_style_text_font(vol_icon, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    ctx->volume_icon = vol_icon;

    ctx->btn_vol_down = mp_create_icon_button(card, APP_SYMBOL_MINUS, ctx, mp_vol_down_clicked, &ctx->btn_vol_down_label);
    ctx->btn_vol_up = mp_create_icon_button(card, APP_SYMBOL_PLUS, ctx, mp_vol_up_clicked, &ctx->btn_vol_up_label);
    lv_obj_set_size(ctx->btn_vol_down, vbtn, vbtn);
    lv_obj_set_size(ctx->btn_vol_up, vbtn, vbtn);

    lv_obj_t *vol_slider = lv_slider_create(card);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, 0, LV_ANIM_OFF);
    lv_obj_set_event_bubble(vol_slider, false);
    lv_obj_add_event_cb(vol_slider, mp_volume_event_cb, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(vol_slider, mp_volume_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(vol_slider, mp_volume_event_cb, LV_EVENT_RELEASED, ctx);
    lv_obj_add_event_cb(vol_slider, mp_volume_event_cb, LV_EVENT_PRESS_LOST, ctx);
    lv_obj_add_event_cb(vol_slider, mp_volume_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_style_radius(vol_slider, ui_style_track_radius(), LV_PART_MAIN);
    lv_obj_set_style_radius(vol_slider, ui_style_track_radius(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(vol_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    /* Bigger knob and touch target: the bar is only 14 px high, which made the
     * slider hard to grab. The knob grows by its padding, the click area by
     * the extended area (limited by the row above and the card edge). */
    lv_obj_set_style_pad_all(vol_slider, large_panel ? 9 : 11, LV_PART_KNOB);
    lv_obj_set_ext_click_area(vol_slider, large_panel ? 22 : 24);
    ctx->volume_slider = vol_slider;
    /* The slider's click area reaches into the neighbouring minus/plus buttons: on top of it they keep their own taps. */
    lv_obj_move_foreground(ctx->btn_vol_down);
    lv_obj_move_foreground(ctx->btn_vol_up);

    /* Stack the bottom rows (volume, controls, progress) within the active
     * column, measured upwards from the card bottom. */
    const lv_coord_t vol_y = content_h - volume_row_h;
    const lv_coord_t ctrl_y = vol_y - controls_row_h;
    const lv_coord_t prog_y = ctrl_y - progress_row_h;

    /* Progress row: [ m:ss ][=====bar=====][ m:ss ]. Times flank the bar. */
    /* the time labels are as wide as the text needs (larger text grows them), at least the former fixed width */
    lv_point_t time_size;
    lv_text_get_size(&time_size, "8:88", ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_coord_t time_label_w = col_w < 190 ? 38 : 50;
    if (time_label_w < time_size.x + 4) time_label_w = time_size.x + 4;
    const lv_coord_t time_gap = col_w < 190 ? 4 : 6;
    const lv_coord_t bar_w = col_w - 2 * (time_label_w + time_gap);
    const lv_coord_t bar_x = col_x + time_label_w + time_gap;
    lv_obj_set_width(pos_label, time_label_w);
    lv_obj_set_width(dur_label, time_label_w);
    lv_obj_set_size(progress, bar_w > 20 ? bar_w : 20, 10);
    lv_obj_align(progress, LV_ALIGN_TOP_LEFT, bar_x, prog_y + 5 + (now_title_line - 25) / 2); /* the bar stays centred on the labels */
    lv_obj_align(pos_label, LV_ALIGN_TOP_LEFT, col_x, prog_y + 1);
    lv_obj_align(dur_label, LV_ALIGN_TOP_LEFT, col_x + col_w - time_label_w, prog_y + 1);

    /* Controls: centered within the active column. */
    const lv_coord_t col_mid_dx = col_x + col_w / 2 - content_w / 2;
    lv_coord_t control_offset = 80;
    lv_coord_t max_control_offset = (col_w - side_size) / 2;
    if (control_offset > max_control_offset) {
        control_offset = max_control_offset;
    }
    if (control_offset < 52) {
        control_offset = 52;
    }
    lv_obj_align(ctx->btn_prev, LV_ALIGN_TOP_MID, col_mid_dx - control_offset, ctrl_y + 4);
    lv_obj_align(ctx->btn_play, LV_ALIGN_TOP_MID, col_mid_dx,       ctrl_y);
    lv_obj_align(ctx->btn_next, LV_ALIGN_TOP_MID, col_mid_dx + control_offset, ctrl_y + 4);

    /* Shuffle left and repeat right of the transport row, when the column is wide enough for both beside the
     * 54 px skip buttons (the prev/next offset then shrinks towards its 56 px minimum to make room). */
    const lv_coord_t extra_size = 46;
    const lv_coord_t extra_gap = 6;
    lv_coord_t extra_prev_offset = (col_w / 2) - (side_size / 2) - extra_gap - extra_size;
    if (extra_prev_offset > control_offset) extra_prev_offset = control_offset;
    ctx->extras_fit = extra_prev_offset >= 56;
    if (ctx->extras_fit) {
        control_offset = extra_prev_offset;
        lv_obj_align(ctx->btn_prev, LV_ALIGN_TOP_MID, col_mid_dx - control_offset, ctrl_y + 4);
        lv_obj_align(ctx->btn_next, LV_ALIGN_TOP_MID, col_mid_dx + control_offset, ctrl_y + 4);
        const lv_coord_t extra_offset = control_offset + side_size / 2 + extra_gap + extra_size / 2;
        lv_obj_set_size(ctx->btn_shuffle, extra_size, extra_size);
        lv_obj_set_size(ctx->btn_repeat, extra_size, extra_size);
        lv_obj_align(ctx->btn_shuffle, LV_ALIGN_TOP_MID, col_mid_dx - extra_offset, ctrl_y + 9);
        lv_obj_align(ctx->btn_repeat, LV_ALIGN_TOP_MID, col_mid_dx + extra_offset, ctrl_y + 9);
    }

    /* [ - ][=====slider=====][ + ]. The speaker glyph gives way to the
     * buttons; it stays as an (invisible) mute-state holder. */
    const bool glow = ui_style_has_depth();
    if (!glow) {
        /* Classic: speaker glyph and a plain slider, no minus/plus buttons. */
        lv_obj_set_hidden(ctx->btn_vol_down, true);
        lv_obj_set_hidden(ctx->btn_vol_up, true);
        lv_obj_align(vol_icon, LV_ALIGN_TOP_LEFT, col_x, vol_y + 6);
        lv_obj_set_pos(vol_slider, col_x + 28, vol_y + (volume_row_h - vslider_h) / 2);
        lv_obj_set_size(vol_slider, col_w > 44 ? col_w - 28 : 16, vslider_h);
    } else {
    lv_obj_set_hidden(vol_icon, true);
    const lv_coord_t vgap = 8;
    lv_obj_set_pos(ctx->btn_vol_down, col_x, vol_y);
    lv_obj_set_pos(ctx->btn_vol_up, col_x + col_w - vbtn, vol_y);
    lv_obj_set_pos(vol_slider, col_x + vbtn + vgap, vol_y + (vbtn - vslider_h) / 2);
    lv_obj_set_size(vol_slider, col_w > 2 * (vbtn + vgap) + 16 ? col_w - 2 * (vbtn + vgap) : 16, vslider_h);
    }

    ctx->tick_timer = lv_timer_create(mp_tick_cb, 1000, ctx);
    if (ctx->tick_timer) {
        lv_timer_pause(ctx->tick_timer);
    }

    mp_apply_visual(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_media_player_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) return;
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)instance->ctx;
    if (ctx == NULL) return;
    int local_pos_before = mp_current_position_s(ctx);
    int64_t now_ms = mp_now_ms();

    if (mp_state_is_unavailable(state->state)) {
        ctx->unavailable = true;
        ctx->is_playing = false;
        ctx->resume_guard_until_ms = 0;
        if (ctx->tick_timer) lv_timer_pause(ctx->tick_timer);
        if (ctx->now_title) lv_label_set_text(ctx->now_title, ui_i18n_get("common.unavailable", "unavailable"));
        if (ctx->now_artist) lv_label_set_text(ctx->now_artist, "");
        mp_apply_visual(ctx);
        return;
    }

    ctx->unavailable = false;
    bool next_is_playing = (strcmp(state->state, "playing") == 0);
    bool playback_state_changed = (ctx->is_playing != next_is_playing);

    /* Defaults when attrs are missing */
    const char *title_text = "";
    const char *artist_text = state->state;
    const char *pic_url = NULL;
    int duration_s = 0;
    int position_s = 0;
    bool have_position = false;
    time_t position_anchor_s = 0;
    double volume_level = -1.0;
    bool same_media_hint = false;

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *mtitle = cJSON_GetObjectItemCaseSensitive(attrs, "media_title");
        cJSON *martist = cJSON_GetObjectItemCaseSensitive(attrs, "media_artist");
        cJSON *mdur = cJSON_GetObjectItemCaseSensitive(attrs, "media_duration");
        cJSON *mpos = cJSON_GetObjectItemCaseSensitive(attrs, "media_position");
        cJSON *mpos_upd = cJSON_GetObjectItemCaseSensitive(attrs, "media_position_updated_at");
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(attrs, "volume_level");
        cJSON *vmuted = cJSON_GetObjectItemCaseSensitive(attrs, "is_volume_muted");
        cJSON *pic = cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture");

        const cJSON *features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
        const long feature_bits = cJSON_IsNumber(features) ? (long)features->valuedouble : 0;
        ctx->shuffle_supported = (feature_bits & MP_FEATURE_SHUFFLE_SET) != 0;
        ctx->repeat_supported = (feature_bits & MP_FEATURE_REPEAT_SET) != 0;
        const cJSON *shuffle = cJSON_GetObjectItemCaseSensitive(attrs, "shuffle");
        ctx->shuffle_on = cJSON_IsTrue(shuffle);
        const cJSON *repeat = cJSON_GetObjectItemCaseSensitive(attrs, "repeat");
        ctx->repeat_mode = (cJSON_IsString(repeat) && repeat->valuestring != NULL)
                               ? (strcmp(repeat->valuestring, "all") == 0 ? MP_REPEAT_ALL
                                  : strcmp(repeat->valuestring, "one") == 0 ? MP_REPEAT_ONE : MP_REPEAT_OFF)
                               : MP_REPEAT_OFF;

        if (cJSON_IsString(mtitle) && mtitle->valuestring) {
            title_text = mtitle->valuestring;
        }
        if (cJSON_IsString(martist) && martist->valuestring) {
            artist_text = martist->valuestring;
        }
        if (cJSON_IsNumber(mdur)) {
            duration_s = (int)(mdur->valuedouble + 0.5);
            if (duration_s < 0) duration_s = 0;
        }
        if (cJSON_IsNumber(mpos)) {
            position_s = (int)(mpos->valuedouble + 0.5);
            if (position_s < 0) position_s = 0;
            have_position = true;
        }
        if (cJSON_IsString(mpos_upd) && mpos_upd->valuestring != NULL) {
            time_t anchor = 0;
            if (parse_iso8601_utc(mpos_upd->valuestring, &anchor)) {
                position_anchor_s = anchor;
            }
        }
        if (cJSON_IsNumber(vol)) {
            volume_level = vol->valuedouble;
        }
        if (cJSON_IsBool(vmuted) && cJSON_IsTrue(vmuted)) {
            if (ctx->volume_icon) lv_label_set_text(ctx->volume_icon, APP_SYMBOL_MUTE);
        } else if (ctx->volume_icon) {
            lv_label_set_text(ctx->volume_icon, APP_SYMBOL_VOLUME);
        }

        if (cJSON_IsString(pic) && pic->valuestring != NULL && pic->valuestring[0] != '\0') {
            pic_url = pic->valuestring;
        }

        same_media_hint = mp_media_identity_matches(ctx, title_text, duration_s, pic_url);

        if (ctx->now_title) lv_label_set_text(ctx->now_title, title_text);
        if (ctx->now_artist) lv_label_set_text(ctx->now_artist, artist_text);
        mp_set_timeline(ctx, duration_s > 0);

        if (pic_url != NULL) {
            mp_request_cover(ctx, pic_url);
        } else if (ctx->cover_url[0] != '\0' || ctx->cover_request_deferred) {
            /* Source stopped providing a cover – clear cached state. */
            ctx->cover_url[0] = '\0';
            ctx->pending_cover_url[0] = '\0';
            ctx->cover_request_deferred = false;
            ctx->cover_dominant_rgb = 0;
            mp_release_cover(ctx);
            mp_cover_show_placeholder(ctx);
        }

        cJSON_Delete(attrs);
    } else {
        if (ctx->now_title) lv_label_set_text(ctx->now_title, "");
        if (ctx->now_artist) lv_label_set_text(ctx->now_artist, artist_text);
    }

    if (playback_state_changed && local_pos_before > 0 && same_media_hint) {
        ctx->resume_guard_pos_s = local_pos_before;
        if (ctx->resume_guard_until_ms < now_ms + 15000) {
            ctx->resume_guard_until_ms = now_ms + 15000;
        }
    }
    ctx->is_playing = next_is_playing;

    ctx->duration_s = duration_s;
    bool resume_guard_active = ctx->resume_guard_until_ms > now_ms && ctx->resume_guard_pos_s > 0;
    bool incoming_position_regressive = have_position && local_pos_before > 0 && position_s + 2 < local_pos_before;
    if (have_position) {
        if (resume_guard_active && incoming_position_regressive) {
            /* Some integrations briefly report a stale low/zero media_position
             * while transitioning back to play. Keep the locally visible
             * position until HA catches up with a non-regressive update. */
            ctx->position_s = local_pos_before;
            ctx->position_anchor_s = ctx->is_playing ? time(NULL) : 0;
        } else {
            ctx->position_s = position_s;
            ctx->position_anchor_s = ctx->is_playing ? ((position_anchor_s > 0) ? position_anchor_s : time(NULL)) : 0;
            if (resume_guard_active && position_s + 2 >= ctx->resume_guard_pos_s) {
                ctx->resume_guard_until_ms = 0;
                ctx->resume_guard_pos_s = 0;
            }
        }
    } else if (!ctx->is_playing) {
        ctx->position_s = local_pos_before;
        ctx->position_anchor_s = 0;
    } else if (resume_guard_active && local_pos_before > 0) {
        ctx->position_s = local_pos_before;
        ctx->position_anchor_s = time(NULL);
    }

    if (ctx->resume_guard_until_ms > 0 && now_ms >= ctx->resume_guard_until_ms) {
        ctx->resume_guard_until_ms = 0;
        ctx->resume_guard_pos_s = 0;
    }

    if (volume_level >= 0.0 && !ctx->volume_dragging) {
        if (volume_level < 0.0) volume_level = 0.0;
        if (volume_level > 1.0) volume_level = 1.0;
        int v = clampi((int)(volume_level * 100.0 + 0.5), 0, 100);
        ctx->volume_value = v;
        if (ctx->volume_slider) {
            ctx->volume_suppress = true;
            lv_slider_set_value(ctx->volume_slider, v, LV_ANIM_OFF);
            ctx->volume_suppress = false;
        }
    }

    if (ctx->tick_timer) {
        if (mp_is_visible(ctx) && ctx->is_playing && ctx->duration_s > 0) {
            lv_timer_resume(ctx->tick_timer);
        } else {
            lv_timer_pause(ctx->tick_timer);
        }
    }

    mp_apply_visual(ctx);
}

void w_media_player_set_visible(ui_widget_instance_t *instance, bool visible)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)instance->ctx;
    ctx->visible = visible;

    if (!visible) {
        if (ctx->cover_request_inflight) {
            ha_cover_fetcher_cancel(ctx);
            ctx->cover_request_inflight = false;
        }
        if (ctx->tick_timer) {
            lv_timer_pause(ctx->tick_timer);
        }
        return;
    }

    if (ctx->tick_timer) {
        if (mp_is_visible(ctx) && ctx->is_playing && ctx->duration_s > 0) {
            lv_timer_resume(ctx->tick_timer);
        } else {
            lv_timer_pause(ctx->tick_timer);
        }
    }

    if (ctx->cover_request_deferred && ctx->pending_cover_url[0] != '\0') {
        char url[sizeof(ctx->pending_cover_url)] = {0};
        snprintf(url, sizeof(url), "%s", ctx->pending_cover_url);
        ctx->cover_request_deferred = false;
        ctx->pending_cover_url[0] = '\0';
        mp_request_cover(ctx, url);
    }
}

void w_media_player_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) return;
    w_mp_ctx_t *ctx = (w_mp_ctx_t *)instance->ctx;
    if (ctx == NULL) return;
    ctx->unavailable = true;
    ctx->is_playing = false;
    if (ctx->tick_timer) lv_timer_pause(ctx->tick_timer);
    if (ctx->now_title) lv_label_set_text(ctx->now_title, ui_i18n_get("common.unavailable", "unavailable"));
    if (ctx->now_artist) lv_label_set_text(ctx->now_artist, "");
    mp_apply_visual(ctx);
}
