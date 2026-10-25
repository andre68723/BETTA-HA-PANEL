/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_bindings.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"

#define HEATING_ACTUAL_FONT ui_font_role(UI_TEXT_KEY)

#define HEATING_TARGET_FONT ui_font_role(UI_TEXT_META)

typedef struct {
    char climate_entity_id[APP_MAX_ENTITY_ID_LEN];
    char sensor_entity_id[APP_MAX_ENTITY_ID_LEN];
    bool is_on;
    bool unavailable;
    bool arc_semi;
    char arc_opening[APP_MAX_UI_OPTION_LEN];
    float target_temp;
    float current_temp;
    bool has_current_temp;
    char status_text[32];
    lv_obj_t *icon_label;
    lv_obj_t *title_label;
    lv_obj_t *arc;
    lv_obj_t *target_label;
    lv_obj_t *actual_label;
    lv_obj_t *status_label;
    lv_obj_t *minus_btn;
    lv_obj_t *plus_btn;
    lv_obj_t *min_label;
    lv_obj_t *max_label;
} w_heating_tile_ctx_t;

typedef struct {
    float target_temp;
    float current_temp;
    bool has_current_temp;
    char status_text[32];
} heating_values_t;

#define HEATING_ARC_SIZE_MIN 140
#define HEATING_ARC_SIZE_MAX 340


static bool heating_parse_float_relaxed(const char *text, float *out_value)
{
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return false;
    }

    char buf[32] = {0};
    size_t n = strnlen(text, sizeof(buf) - 1U);
    for (size_t i = 0; i < n; i++) {
        buf[i] = (text[i] == ',') ? '.' : text[i];
    }
    buf[n] = '\0';

    char *end = NULL;
    float parsed = strtof(buf, &end);
    if (end == buf) {
        return false;
    }
    *out_value = parsed;
    return true;
}

static float clamp_temp(float value)
{
    if (value < 5.0f) {
        return 5.0f;
    }
    if (value > 30.0f) {
        return 30.0f;
    }
    return value;
}

/* Snap to nearest 0.5 degree step. */
static float snap_half_deg(float value)
{
    return (float)((int)(value * 2.0f + (value >= 0 ? 0.5f : -0.5f))) * 0.5f;
}

static void heating_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%.*s", (int)(dst_size - 1U), src);
}

static bool heating_state_is_on(const char *state)
{
    if (state == NULL || state[0] == '\0') {
        return false;
    }
    if (strcmp(state, "off") == 0 || strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0) {
        return false;
    }
    return true;
}

static void heating_extract_climate_values(const ha_state_t *state, heating_values_t *out)
{
    if (state == NULL || out == NULL) {
        return;
    }

    out->target_temp = 20.0f;
    out->current_temp = 20.0f;
    out->has_current_temp = false;
    out->status_text[0] = '\0';
    bool has_target_temp = false;

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *temperature = cJSON_GetObjectItemCaseSensitive(attrs, "temperature");
        if (cJSON_IsNumber(temperature)) {
            out->target_temp = (float)temperature->valuedouble;
            has_target_temp = true;
        }

        cJSON *current_temperature = cJSON_GetObjectItemCaseSensitive(attrs, "current_temperature");
        if (cJSON_IsNumber(current_temperature)) {
            out->current_temp = (float)current_temperature->valuedouble;
            out->has_current_temp = true;
        }

        cJSON *hvac_action = cJSON_GetObjectItemCaseSensitive(attrs, "hvac_action");
        if (cJSON_IsString(hvac_action) && hvac_action->valuestring != NULL && hvac_action->valuestring[0] != '\0') {
            heating_copy_text(out->status_text, sizeof(out->status_text), hvac_action->valuestring);
        }
        cJSON_Delete(attrs);
    }

    if (!has_target_temp) {
        char *end = NULL;
        float parsed = strtof(state->state, &end);
        if (end != state->state) {
            out->target_temp = parsed;
        }
    }

    out->target_temp = clamp_temp(out->target_temp);
    out->current_temp = clamp_temp(out->current_temp);

    if (out->status_text[0] == '\0' && state->state[0] != '\0') {
        heating_copy_text(out->status_text, sizeof(out->status_text), state->state);
    }
}

static bool heating_extract_sensor_temp(const ha_state_t *state, float *out_temp)
{
    if (state == NULL || out_temp == NULL) {
        return false;
    }

    float parsed = 0.0f;
    if (heating_parse_float_relaxed(state->state, &parsed)) {
        *out_temp = clamp_temp(parsed);
        return true;
    }

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(attrs, "temperature");
        if (cJSON_IsNumber(value)) {
            *out_temp = clamp_temp((float)value->valuedouble);
            cJSON_Delete(attrs);
            return true;
        }
        cJSON_Delete(attrs);
    }

    return false;
}

static void heating_set_target_label(lv_obj_t *label, float value)
{
    if (label == NULL) {
        return;
    }
    char text[20] = {0};
    snprintf(text, sizeof(text), ui_i18n_get("heating.target_format", "Target %.1f°C"), (double)clamp_temp(value));
    lv_label_set_text(label, text);
}

static void heating_set_actual_label(lv_obj_t *label, bool has_current_temp, float current_temp, const char *status_text)
{
    if (label == NULL) {
        return;
    }

    if (has_current_temp) {
        char text[20] = {0};
        ui_format_temp_c(text, sizeof(text), true, clamp_temp(current_temp));
        lv_label_set_text(label, text);
        return;
    }
    /* No reading: the placeholder, not the raw Home Assistant state ("unavailable", untranslated
     * and far too wide for the large font); the status line below already shows it translated. */
    (void)status_text;
    lv_label_set_text(label, "--.-°C"); /* matches ui_format_temp_c()'s own unavailable text */
}

static void heating_set_status_label(lv_obj_t *label, bool is_on, const char *status_text)
{
    if (label == NULL) {
        return;
    }

    if (status_text != NULL && status_text[0] != '\0') {
        char text[40] = {0};
        size_t out = 0;
        for (size_t i = 0; status_text[i] != '\0' && out + 1 < sizeof(text); i++) {
            char c = status_text[i];
            if (c == '_') {
                c = ' ';
            }
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c + ('a' - 'A'));
            }
            text[out++] = c;
        }
        text[out] = '\0';
        if (strcmp(text, "on") == 0) {
            lv_label_set_text(label, ui_i18n_get("common.on", "ON"));
        } else if (strcmp(text, "off") == 0) {
            lv_label_set_text(label, ui_i18n_get("common.off", "OFF"));
        } else if (strcmp(text, "unavailable") == 0) {
            lv_label_set_text(label, ui_i18n_get("common.unavailable", "unavailable"));
        } else if (strcmp(text, "heating") == 0) {
            lv_label_set_text(label, ui_i18n_get("heating.active", "heating active"));
        } else {
            /* Any other Home Assistant hvac_action ("cooling", "drying", "idle", "fan", ...) or, when
             * hvac_action is absent, the hvac_mode used as a fallback status ("cool", "heat", "auto",
             * "dry", "fan_only") -- shares the climate tile's own "climate.mode.<value>" keys
             * (w_climate.c's climate_mode_text()) instead of a second word list, and falls back the
             * same way it does there (capitalized raw name) when even that has no translation. Note
             * "text" here already lowercased/space-converted for the switch above; the key itself
             * needs the original underscore form ("fan_only", not "fan only"). */
            char key_value[40] = {0};
            size_t kn = 0;
            for (size_t i = 0; status_text[i] != '\0' && kn + 1 < sizeof(key_value); i++) {
                char c = status_text[i];
                if (c >= 'A' && c <= 'Z') {
                    c = (char)(c + ('a' - 'A'));
                }
                key_value[kn++] = c;
            }
            key_value[kn] = '\0';
            char key[64]; /* "climate.mode." (13) + key_value's full capacity (39) + NUL: comfortably under 64 */
            snprintf(key, sizeof(key), "climate.mode.%s", key_value);
            const char *translated = ui_i18n_get(key, "");
            lv_label_set_text(label, translated[0] != '\0' ? translated : text);
        }
        return;
    }

    lv_label_set_text(
        label, is_on ? ui_i18n_get("heating.active", "heating active") : ui_i18n_get("common.off", "OFF"));
}

/* Places a label with its center at (cx, top + its own height / 2), in the card's content coordinates. */
static void heating_place_centered(lv_obj_t *label, int cx, int top)
{
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, cx - lv_obj_get_width(label) / 2, top);
}

/* Target, actual and (when given) status stacked and centered on (cx, cy). */
static void heating_place_stack(w_heating_tile_ctx_t *ctx, int cx, int cy, bool with_status)
{
    const int gap = 2;
    const int h_target = lv_obj_get_height(ctx->target_label);
    const int h_actual = lv_obj_get_height(ctx->actual_label);
    const int h_status = with_status ? lv_obj_get_height(ctx->status_label) : 0;
    const int total = h_target + gap + h_actual + (with_status ? gap + h_status : 0);
    int y = cy - total / 2;
    heating_place_centered(ctx->target_label, cx, y);
    y += h_target + gap;
    heating_place_centered(ctx->actual_label, cx, y);
    if (with_status) {
        y += h_actual + gap;
        heating_place_centered(ctx->status_label, cx, y);
    }
}

/* Next to one end of the semi arc, pushed out towards the opening so it never sits on the ring. */
static void heating_place_end_label(lv_obj_t *label, int cx, int cy, int radius, int angle_deg, int dir_x, int dir_y,
                                    int clearance)
{
    const float rad = (float)angle_deg * 3.14159265f / 180.0f;
    const int ex = cx + (int)((float)radius * cosf(rad));
    const int ey = cy + (int)((float)radius * sinf(rad));
    const int w = lv_obj_get_width(label);
    const int h = lv_obj_get_height(label);
    const int lx = ex + dir_x * (clearance + w / 2);
    const int ly = ey + dir_y * (clearance + h / 2);
    /* Stay inside the tile's content area: the label then rests against the edge instead of reaching out of the card. */
    lv_obj_t *parent = lv_obj_get_parent(label);
    int x = lx - w / 2;
    int y = ly - h / 2;
    if (parent != NULL) {
        const int max_x = lv_obj_get_content_width(parent) - w;
        const int max_y = lv_obj_get_content_height(parent) - h;
        x = x > max_x ? max_x : x;
        y = y > max_y ? max_y : y;
    }
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x < 0 ? 0 : x, y < 0 ? 0 : y);
}

static void heating_apply_layout(lv_obj_t *card, w_heating_tile_ctx_t *ctx)
{
    if (card == NULL || ctx == NULL || ctx->arc == NULL || ctx->target_label == NULL ||
        ctx->actual_label == NULL || ctx->status_label == NULL) {
        return;
    }

    /* The card's and the labels' sizes are only known once LVGL has laid them out: right after
     * creation (and before the first refresh) they read as 0, which left the arc at its minimum
     * size and pushed the reading and status above the tile. All positions below are in the
     * card's content coordinates, derived from those sizes instead of fixed offsets. */
    lv_obj_update_layout(card);
    const int cw = lv_obj_get_content_width(card);
    const int ch = lv_obj_get_content_height(card);
    const int title_reserve = ui_widget_name_enabled(ctx->title_label) ? lv_obj_get_y(ctx->title_label) + lv_obj_get_height(ctx->title_label) + 6 : 0;
    const int arc_w = lv_obj_get_style_arc_width(ctx->arc, LV_PART_MAIN);

    if (ctx->arc_semi) {
        /* Semi arc: +/- buttons on the open side, the ring on the other, and the target/actual/
         * status block in between -- never on top of the buttons or the ring. */
        const char *op = ctx->arc_opening;
        const bool open_left = strcmp(op, "left") == 0;
        const bool open_right = strcmp(op, "right") == 0;
        const bool open_top = strcmp(op, "top") == 0;
        const bool sideways = open_left || open_right;
        const int btn = ctx->minus_btn != NULL ? lv_obj_get_width(ctx->minus_btn) : 0;
        const int gap = 8;
        const int avail_h = ch - title_reserve;
        int arc_size = cw < avail_h ? cw : avail_h;
        if (arc_size > HEATING_ARC_SIZE_MAX) {
            arc_size = HEATING_ARC_SIZE_MAX;
        }
        /* The ring sits against the edge opposite its opening, which leaves the text between it and
         * the buttons as much room as the tile has. */
        const bool open_bottom = !sideways && !open_top;
        const int cx = open_left ? cw - arc_size / 2 : (open_right ? arc_size / 2 : cw / 2);
        const int cy = open_bottom ? title_reserve + arc_size / 2
                                   : (open_top ? ch - arc_size / 2 : title_reserve + avail_h / 2);
        lv_obj_set_size(ctx->arc, arc_size, arc_size);
        lv_obj_align(ctx->arc, LV_ALIGN_TOP_LEFT, cx - arc_size / 2, cy - arc_size / 2);
        /* Keep text clear of the ring and of the knob riding on it (about the ring's width). */
        const int r_inner = arc_size / 2 - arc_w - arc_w / 2;

        int text_cx = cx;
        int text_cy = cy;
        int text_w = 2 * r_inner;
        int text_h = 2 * r_inner;
        if (sideways) {
            const int bx = open_left ? 0 : cw - btn;
            if (ctx->minus_btn != NULL && ctx->plus_btn != NULL) {
                lv_obj_align(ctx->plus_btn, LV_ALIGN_TOP_LEFT, bx, cy - gap / 2 - btn);
                lv_obj_align(ctx->minus_btn, LV_ALIGN_TOP_LEFT, bx, cy + gap / 2);
            }
            /* Between the button column and the ring's inner edge on the far side. */
            const int lo = open_left ? btn + gap : cx - r_inner + gap;
            const int hi = open_left ? cx + r_inner - gap : cw - btn - gap;
            text_cx = (lo + hi) / 2;
            text_w = hi - lo;
            text_h = 2 * r_inner;
        } else {
            const int by = open_top ? title_reserve : ch - btn;
            if (ctx->minus_btn != NULL && ctx->plus_btn != NULL) {
                lv_obj_align(ctx->minus_btn, LV_ALIGN_TOP_LEFT, cx - gap / 2 - btn, by);
                lv_obj_align(ctx->plus_btn, LV_ALIGN_TOP_LEFT, cx + gap / 2, by);
            }
            const int lo = open_top ? by + btn + gap : cy - r_inner + gap;
            const int hi = open_top ? cy + r_inner - gap : by - gap;
            text_cy = (lo + hi) / 2;
            text_w = (r_inner * 3) / 2; /* the ring's chord a little off its center */
            text_h = hi - lo;
        }
        /* The large reading drops to the next size, and the target and status lines to a smaller
         * one, when they do not fit between the buttons and the ring. */
        lv_obj_set_style_text_font(ctx->actual_label, HEATING_ACTUAL_FONT, LV_PART_MAIN);
        lv_obj_update_layout(ctx->actual_label);
        if (lv_obj_get_width(ctx->actual_label) > text_w) {
            lv_obj_set_style_text_font(ctx->actual_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
            lv_obj_update_layout(ctx->actual_label);
        }
        const lv_font_t *small = HEATING_TARGET_FONT;
        lv_obj_set_style_text_font(ctx->target_label, small, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->status_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        heating_set_target_label(ctx->target_label, ctx->target_temp);
        lv_obj_set_hidden(ctx->status_label, false);
        lv_obj_update_layout(ctx->target_label);
        lv_obj_update_layout(ctx->status_label);
        /* 20 px is the smallest text on the 4-inch class. A status line wider than the room between the buttons and
         * the ring is dropped first; a block that still does not fit loses the word "Target" too, keeping the two
         * temperatures. */
        bool with_status = lv_obj_get_width(ctx->status_label) <= text_w;
        const int stack_h = lv_obj_get_height(ctx->target_label) + 2 + lv_obj_get_height(ctx->actual_label) +
                            (with_status ? 2 + lv_obj_get_height(ctx->status_label) : 0);
        if (lv_obj_get_width(ctx->target_label) > text_w || stack_h > text_h) {
            char compact[16];
            snprintf(compact, sizeof(compact), "%.1f°C", (double)clamp_temp(ctx->target_temp));
            lv_label_set_text(ctx->target_label, compact);
            lv_obj_update_layout(ctx->target_label);
            with_status = false;
        }
        lv_obj_set_hidden(ctx->status_label, !with_status);
        heating_place_stack(ctx, text_cx, text_cy, with_status);

        /* Min / max next to the ring's two ends, moved out towards the opening. */
        if (ctx->min_label != NULL && ctx->max_label != NULL) {
            const int start = (int)lv_arc_get_bg_angle_start(ctx->arc);
            const int stop = (int)lv_arc_get_bg_angle_end(ctx->arc);
            const int dir_x = open_left ? -1 : (open_right ? 1 : 0);
            const int dir_y = open_top ? -1 : (sideways ? 0 : 1);
            const int r_mid = arc_size / 2 - arc_w / 2;
            /* With the opening on top or bottom the +/- buttons span the middle of the opening and a label
             * centred on the ring's end can reach under them (20 px digits): move each to its own side. */
            const int min_dx = sideways ? dir_x : (cosf((float)start * 3.14159265f / 180.0f) >= 0.0f ? 1 : -1);
            const int max_dx = sideways ? dir_x : (cosf((float)stop * 3.14159265f / 180.0f) >= 0.0f ? 1 : -1);
            heating_place_end_label(ctx->min_label, cx, cy, r_mid, start, min_dx, dir_y, arc_w / 2 + 4);
            heating_place_end_label(ctx->max_label, cx, cy, r_mid, stop, max_dx, dir_y, arc_w / 2 + 4);
        }
        return;
    }

    /* Default: the ring is open at the bottom (it ends at 20 and 160 degrees, sin 20 = 0.342 of the
     * radius below its center), so only its top and its two ends have to stay clear of the title
     * and the status line; its unused bottom may reach down beside the status. The target and the
     * large actual reading sit centered in it, slightly low, the reading partly in the opening. */
    const int status_h = lv_obj_get_height(ctx->status_label);
    const int area_top = title_reserve;
    const int area_bottom = ch - status_h - 4;
    int radius = (int)((float)(area_bottom - area_top - arc_w / 2) / 1.342f);
    if (radius * 2 > cw) {
        radius = cw / 2;
    }
    if (radius * 2 > HEATING_ARC_SIZE_MAX) {
        radius = HEATING_ARC_SIZE_MAX / 2;
    }
    /* The arc object's square must still end inside the card (its padding may take the unused
     * bottom): LVGL clips children that leave a rounded card through a costly mask. */
    const int card_bottom = ch + lv_obj_get_style_pad_bottom(card, LV_PART_MAIN) - 1;
    int used = 0;
    int cy = 0;
    for (;;) {
        used = radius + (int)((float)radius * 0.342f) + arc_w / 2;
        cy = area_top + (area_bottom - area_top - used) / 2 + radius;
        if (cy + radius <= card_bottom || radius <= HEATING_ARC_SIZE_MIN / 4) {
            break;
        }
        radius--;
    }
    const int cx = cw / 2;
    lv_obj_set_size(ctx->arc, radius * 2, radius * 2);
    lv_obj_align(ctx->arc, LV_ALIGN_TOP_LEFT, cx - radius, cy - radius);
    /* Slightly low in the ring, but never down onto the status line; a smaller reading when the
     * ring's inside is too short for the full size. */
    const int status_top = ch - status_h;
    lv_obj_set_style_text_font(ctx->actual_label, HEATING_ACTUAL_FONT, LV_PART_MAIN);
    lv_obj_update_layout(ctx->actual_label);
    int stack_h = lv_obj_get_height(ctx->target_label) + 2 + lv_obj_get_height(ctx->actual_label);
    if (cy - radius + arc_w + stack_h > status_top - 2) {
        lv_obj_set_style_text_font(ctx->actual_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
        lv_obj_update_layout(ctx->actual_label);
        stack_h = lv_obj_get_height(ctx->target_label) + 2 + lv_obj_get_height(ctx->actual_label);
    }
    int stack_cy = cy + radius / 6;
    if (stack_cy + stack_h / 2 > status_top - 2) {
        stack_cy = status_top - 2 - stack_h / 2;
    }
    heating_place_stack(ctx, cx, stack_cy, false);
    heating_place_centered(ctx->status_label, cx, status_top);
}

static void heating_apply_visual(lv_obj_t *card, w_heating_tile_ctx_t *ctx, bool allow_status_fallback)
{
    if (card == NULL || ctx == NULL) {
        return;
    }
    lv_obj_t *icon = ctx->icon_label;
    lv_obj_t *title = ctx->title_label;
    lv_obj_t *arc = ctx->arc;
    lv_obj_t *target_label = ctx->target_label;
    lv_obj_t *actual_label = ctx->actual_label;
    lv_obj_t *status_label = ctx->status_label;
    if (icon == NULL || title == NULL || arc == NULL || target_label == NULL || actual_label == NULL || status_label == NULL) {
        return;
    }

    bool is_on = ctx->is_on;
    float target_temp = ctx->target_temp;
    bool has_current_temp = ctx->has_current_temp;
    float current_temp = ctx->current_temp;
    const char *status_text = ctx->status_text;

    lv_obj_set_style_bg_color(
        card, is_on ? lv_color_hex(APP_UI_COLOR_CARD_BG_ON) : lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    ui_style_card_set_active(card, is_on);
    lv_obj_set_style_text_color(
        icon, is_on ? lv_color_hex(APP_UI_COLOR_HEAT_ICON_ON) : lv_color_hex(APP_UI_COLOR_CARD_ICON_OFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(target_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
    lv_obj_set_style_text_color(actual_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

    lv_obj_set_style_arc_color(
        arc, is_on ? lv_color_hex(APP_UI_COLOR_HEAT_TRACK_ON) : lv_color_hex(APP_UI_COLOR_HEAT_TRACK_OFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, (lv_obj_get_width(card) >= 300) ? 16 : 15, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_color(
        arc, is_on ? lv_color_hex(APP_UI_COLOR_HEAT_IND_ON) : lv_color_hex(APP_UI_COLOR_HEAT_IND_OFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, (lv_obj_get_width(card) >= 300) ? 16 : 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(
        arc, is_on ? lv_color_hex(APP_UI_COLOR_HEAT_KNOB_ON) : lv_color_hex(APP_UI_COLOR_HEAT_KNOB_OFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_KNOB);

    lv_obj_set_state(ctx->arc, LV_STATE_DISABLED, ctx->unavailable);
    if (ctx->arc_semi) {
        ui_style_action_button(ctx->minus_btn, lv_obj_get_child(ctx->minus_btn, 0), UI_CONTROL_ACTION, !ctx->unavailable);
        ui_style_action_button(ctx->plus_btn, lv_obj_get_child(ctx->plus_btn, 0), UI_CONTROL_ACTION, !ctx->unavailable);
        if (ctx->min_label != NULL) lv_obj_set_style_text_color(ctx->min_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        if (ctx->max_label != NULL) lv_obj_set_style_text_color(ctx->max_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    }

    int arc_value = (int)(clamp_temp(target_temp) * 2.0f + 0.5f);
    lv_arc_set_value(arc, arc_value);
    heating_set_target_label(target_label, target_temp);
    heating_set_actual_label(actual_label, has_current_temp, current_temp, allow_status_fallback ? status_text : "");
    heating_set_status_label(status_label, is_on, status_text);
    heating_apply_layout(card, ctx);
}

static void heating_apply_from_ctx(lv_obj_t *card, const w_heating_tile_ctx_t *ctx)
{
    if (ctx == NULL || card == NULL) {
        return;
    }
    bool allow_status_fallback = (ctx->sensor_entity_id[0] == '\0');
    heating_apply_visual(card, (w_heating_tile_ctx_t *)ctx, allow_status_fallback);
}

static void w_heating_tile_card_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    w_heating_tile_ctx_t *ctx = (w_heating_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        if (ctx->unavailable) return;
        lv_obj_t *card = lv_event_get_target(event);
        bool prev_is_on = ctx->is_on;
        char prev_status[sizeof(ctx->status_text)] = {0};
        heating_copy_text(prev_status, sizeof(prev_status), ctx->status_text);
        bool next_is_on = !ctx->is_on;

        esp_err_t err = ui_bindings_toggle_entity(ctx->climate_entity_id);
        if (err == ESP_OK) {
            ctx->is_on = next_is_on;
            heating_copy_text(ctx->status_text, sizeof(ctx->status_text), ctx->is_on ? "on" : "off");
            heating_apply_from_ctx(card, ctx);
        } else {
            ctx->is_on = prev_is_on;
            heating_copy_text(ctx->status_text, sizeof(ctx->status_text), prev_status);
            heating_apply_from_ctx(card, ctx);
        }
    } else if (code == LV_EVENT_DELETE) {
        free(ctx);
    }
}

static void w_heating_tile_arc_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }

    lv_obj_t *arc = lv_event_get_target(event);
    w_heating_tile_ctx_t *ctx = (w_heating_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) return;
    int value = (arc != NULL) ? lv_arc_get_value(arc) : 40;
    float target_c = (float)value * 0.5f;

    heating_set_target_label((ctx != NULL) ? ctx->target_label : NULL, target_c);

    if (code == LV_EVENT_RELEASED) {
        if (ctx != NULL) {
            ctx->target_temp = target_c;
            ui_bindings_set_climate_target_c(ctx->climate_entity_id, target_c);
        }
    }
}

static void w_heating_tile_step_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    w_heating_tile_ctx_t *ctx = (w_heating_tile_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    if (ctx->unavailable) return;
    lv_obj_t *btn = lv_event_get_target(event);
    int delta = (btn == ctx->plus_btn) ? 1 : -1;
    float next = snap_half_deg(clamp_temp(ctx->target_temp) + (float)delta * 0.5f);
    next = clamp_temp(next);
    ctx->target_temp = next;
    if (ctx->arc != NULL) {
        lv_arc_set_value(ctx->arc, (int)(next * 2.0f + 0.5f));
    }
    heating_set_target_label(ctx->target_label, next);
    ui_bindings_set_climate_target_c(ctx->climate_entity_id, next);
}

esp_err_t w_heating_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
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
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, APP_SYMBOL_POWER);
    lv_obj_set_style_text_font(icon, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 2);

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    /* Centered, one line, and clear of the power icon on the left (mirrored on the right). */
    lv_obj_set_width(title, def->w - 32 - 2 * 26);
    lv_obj_set_height(title, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, HEATING_ARC_SIZE_MIN, HEATING_ARC_SIZE_MIN);
    lv_arc_set_range(arc, 10, 60);
    lv_arc_set_value(arc, 40);
    lv_arc_set_bg_angles(arc, 160, 20);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_event_bubble(arc, false);

    lv_obj_t *target_label = lv_label_create(card);
    heating_set_target_label(target_label, 20.0f);
    lv_obj_set_style_text_font(target_label, HEATING_TARGET_FONT, LV_PART_MAIN);
    lv_obj_align(target_label, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *actual_label = lv_label_create(card);
    lv_label_set_text(actual_label, "--.- C");
    lv_obj_set_style_text_font(actual_label, HEATING_ACTUAL_FONT, LV_PART_MAIN);
    lv_obj_align(actual_label, LV_ALIGN_CENTER, 0, 50);

    lv_obj_t *status_label = lv_label_create(card);
    lv_label_set_text(status_label, ui_i18n_get("common.off", "OFF"));
    lv_obj_set_style_text_font(status_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    w_heating_tile_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_heating_tile_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->climate_entity_id, sizeof(ctx->climate_entity_id), "%s", def->entity_id);
    snprintf(ctx->sensor_entity_id, sizeof(ctx->sensor_entity_id), "%s", def->secondary_entity_id);
    ctx->is_on = false;
    ctx->target_temp = 20.0f;
    ctx->current_temp = 20.0f;
    ctx->has_current_temp = false;
    snprintf(ctx->status_text, sizeof(ctx->status_text), "OFF");
    ctx->icon_label = icon;
    ctx->title_label = title;
    ctx->arc = arc;
    ctx->target_label = target_label;
    ctx->actual_label = actual_label;
    ctx->status_label = status_label;

    ctx->arc_semi = (def->style_variant[0] != '\0' && strcmp(def->style_variant, "arc_semi") == 0);
    const char *opening = (def->arc_opening[0] != '\0') ? def->arc_opening : "left";
    snprintf(ctx->arc_opening, sizeof(ctx->arc_opening), "%s", opening);

    if (ctx->arc_semi) {
        uint16_t bg_start = 270, bg_end = 90;
        if (strcmp(ctx->arc_opening, "bottom") == 0) {
            bg_start = 180;
            bg_end = 360;
        } else if (strcmp(ctx->arc_opening, "top") == 0) {
            bg_start = 0;
            bg_end = 180;
        } else if (strcmp(ctx->arc_opening, "right") == 0) {
            bg_start = 90;
            bg_end = 270;
        } else { /* left (default) */
            bg_start = 270;
            bg_end = 90;
        }
        lv_arc_set_bg_angles(arc, bg_start, bg_end);
        lv_arc_set_rotation(arc, 0);

        /* Min/Max labels at arc ends. */
        ctx->min_label = lv_label_create(card);
        lv_label_set_text(ctx->min_label, "5");
        lv_obj_set_style_text_font(ctx->min_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->min_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

        ctx->max_label = lv_label_create(card);
        lv_label_set_text(ctx->max_label, "30");
        lv_obj_set_style_text_font(ctx->max_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->max_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

        /* +/- buttons. */
        ctx->minus_btn = lv_btn_create(card);
        lv_obj_set_size(ctx->minus_btn, 48, 48);
        lv_obj_set_style_radius(ctx->minus_btn, 24, LV_PART_MAIN);
        lv_obj_t *minus_lbl = lv_label_create(ctx->minus_btn);
        lv_label_set_text(minus_lbl, "-");
        lv_obj_set_style_text_font(minus_lbl, HEATING_TARGET_FONT, LV_PART_MAIN);
        lv_obj_center(minus_lbl);
        lv_obj_add_event_cb(ctx->minus_btn, w_heating_tile_step_event_cb, LV_EVENT_CLICKED, ctx);

        ctx->plus_btn = lv_btn_create(card);
        lv_obj_set_size(ctx->plus_btn, 48, 48);
        lv_obj_set_style_radius(ctx->plus_btn, 24, LV_PART_MAIN);
        lv_obj_t *plus_lbl = lv_label_create(ctx->plus_btn);
        lv_label_set_text(plus_lbl, "+");
        lv_obj_set_style_text_font(plus_lbl, HEATING_TARGET_FONT, LV_PART_MAIN);
        lv_obj_center(plus_lbl);
        lv_obj_add_event_cb(ctx->plus_btn, w_heating_tile_step_event_cb, LV_EVENT_CLICKED, ctx);
    }

    lv_obj_add_event_cb(card, w_heating_tile_card_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(card, w_heating_tile_card_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(arc, w_heating_tile_arc_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(arc, w_heating_tile_arc_event_cb, LV_EVENT_RELEASED, ctx);

    ctx->unavailable = true;
    out_instance->ctx = ctx;
    out_instance->obj = card;
    heating_apply_from_ctx(card, ctx);
    return ESP_OK;
}

void w_heating_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }

    w_heating_tile_ctx_t *ctx = (w_heating_tile_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    if (strncmp(state->entity_id, ctx->climate_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        heating_values_t values = {0};
        heating_extract_climate_values(state, &values);
        ctx->unavailable = state->state[0] == '\0' || strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
        ctx->is_on = heating_state_is_on(state->state);
        ctx->target_temp = values.target_temp;
        heating_copy_text(ctx->status_text, sizeof(ctx->status_text), values.status_text);
        if (ctx->sensor_entity_id[0] == '\0') {
            ctx->has_current_temp = values.has_current_temp;
            ctx->current_temp = values.current_temp;
        }
    } else if (ctx->sensor_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->sensor_entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        float sensor_temp = 0.0f;
        bool ok = heating_extract_sensor_temp(state, &sensor_temp);
        ctx->has_current_temp = ok;
        if (ok) {
            ctx->current_temp = sensor_temp;
        }
    } else {
        return;
    }

    heating_apply_from_ctx(instance->obj, ctx);
}

void w_heating_tile_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }

    w_heating_tile_ctx_t *ctx = (w_heating_tile_ctx_t *)instance->ctx;
    if (ctx != NULL) {
        ctx->is_on = false;
        ctx->unavailable = true;
        ctx->has_current_temp = false;
        heating_copy_text(ctx->status_text, sizeof(ctx->status_text), "unavailable");
        heating_apply_from_ctx(instance->obj, ctx);
        return;
    }

    w_heating_tile_ctx_t fallback = {0};
    fallback.is_on = false;
    fallback.unavailable = true;
    fallback.target_temp = 20.0f;
    fallback.current_temp = 20.0f;
    fallback.has_current_temp = false;
    snprintf(fallback.status_text, sizeof(fallback.status_text), "unavailable");
    fallback.icon_label = lv_obj_get_child(instance->obj, 0);
    fallback.title_label = lv_obj_get_child(instance->obj, 1);
    fallback.arc = lv_obj_get_child(instance->obj, 2);
    fallback.target_label = lv_obj_get_child(instance->obj, 3);
    fallback.actual_label = lv_obj_get_child(instance->obj, 4);
    fallback.status_label = lv_obj_get_child(instance->obj, 5);
    heating_apply_visual(instance->obj, &fallback, true);
}
