/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Vacuum widget. Shows status + battery for a vacuum.* entity with start/pause,
 * stop, return-to-base, locate and the cleaning mode (an optional select.* entity).
 *
 * An optional secondary_entity_id may point at an image.* entity to surface a
 * live map preview directly inside the tile; a Roborock map's calibration points
 * place the robot and its trail on it.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ha/ha_client.h"
#include "ha/ha_cover_fetcher.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_option_picker.h"
#include "ha/ha_control_options.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_i18n.h"

#define W_RR_TAG "w_roborock"
#define W_RR_MAP_URL_LEN 512
#define W_RR_MAX_CAL_POINTS 4
#define W_RR_TICK_MS 1000
#define W_RR_MAP_REFRESH_ACTIVE_MS 5000
#define W_RR_MAP_REFRESH_IDLE_MS 30000
#define W_RR_MAP_REFRESH_HIDDEN_MS 0
#define W_RR_MAP_RETRY_MS 12000
#define W_RR_POSITION_TRAIL_POINTS 8
#define W_RR_POSITION_REFRESH_ACTIVE_MS 4000
#define W_RR_POSITION_REFRESH_IDLE_MS 30000
#define W_RR_POSITION_RETRY_MS 12000

/* Home Assistant VacuumEntityFeature bits the tile cares about. */
#define W_RR_FEAT_STOP 8U
#define W_RR_FEAT_FAN_SPEED 32U
#define W_RR_FEAT_SEND_COMMAND 256U
#define W_RR_MAX_MODES 16

/* Glyph font for the symbol icons on the generic card's buttons (the Poppins text
 * fonts have no symbols). */
#define W_RR_ICON_FONT APP_FONT_SYMBOL_MEDIUM
#define W_RR_MODE_LEN 48

typedef struct {
    double vacuum_x;
    double vacuum_y;
    double map_x;
    double map_y;
} w_rr_cal_point_t;

typedef struct {
    double x;
    double y;
    int64_t updated_ms;
    bool valid;
} w_rr_position_t;

typedef struct w_rr_ctx {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char map_entity_id[APP_MAX_ENTITY_ID_LEN];
    /* Optional select.* entity holding the cleaning mode (tertiary_entity_id). */
    char mode_entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    char battery_entity_id[APP_MAX_ENTITY_ID_LEN]; /* optional sensor with the charge in %: the entity has no battery attribute since HA 2026.9 */
    int battery_sensor_pct;                        /* -1 until that sensor reports a number */
    bool history_popup;                            /* long press opens the recent-activity popup */

    lv_obj_t *card;
    lv_obj_t *controls_panel;
    lv_obj_t *title_label;
    lv_obj_t *state_label;
    lv_obj_t *detail_label;
    lv_obj_t *battery_chip;
    lv_obj_t *battery_label;
    lv_obj_t *start_btn;
    lv_obj_t *start_label;
    lv_obj_t *dock_btn;
    lv_obj_t *dock_label;
    lv_obj_t *rooms_btn;
    lv_obj_t *rooms_label;

    /* Generic vacuum card: line-art robot and the arrow of the mode chip. */
    lv_obj_t *art;
    lv_obj_t *art_disc;
    lv_obj_t *art_inner;
    lv_obj_t *art_lidar;
    lv_obj_t *art_slot;
    lv_obj_t *mode_arrow;

    lv_obj_t *popup_map_panel;
    lv_obj_t *popup_map_img;
    lv_obj_t *popup_map_overlay;
    lv_obj_t *popup_map_name_label;
    lv_obj_t *popup_map_placeholder;
    lv_obj_t *popup_map_spinner; /* shown above the placeholder text while a map/room list fetch is in flight */
    lv_obj_t *stop_btn;
    lv_obj_t *stop_label;

    lv_timer_t *tick_timer;
    SemaphoreHandle_t staging_mutex;



    bool unavailable;
    bool error;
    bool active_clean;
    bool paused;
    bool returning;
    bool charging;
    int battery_pct;
    bool position_fetching;
    bool position_failed;
    uint32_t position_request_seq;
    int64_t last_position_request_ms;
    int64_t last_position_success_ms;
    w_rr_position_t robot_position;
    w_rr_position_t pending_robot_position;
    bool pending_position_ready;
    bool pending_position_failure;
    w_rr_position_t robot_trail[W_RR_POSITION_TRAIL_POINTS];
    size_t robot_trail_count;

    char state_text[48];
    char restore_text[48];      /* state_text while a failed command's notice replaces it */
    int64_t notice_until_ms;    /* 0: no notice shown */
    char detail_text[160];
    char fan_speed[HA_CONTROL_OPTION_LEN];
    ha_control_options_t fan_options;
    lv_obj_t *fan_btn;
    lv_obj_t *fan_label;
    lv_obj_t *fan_popup;
    char map_state_value[APP_MAX_STATE_LEN];

    char map_url[W_RR_MAP_URL_LEN];
    char map_loaded_url[W_RR_MAP_URL_LEN];
    bool popup_map_request_pending;
    bool popup_map_request_inflight;
    bool popup_map_failed;
    int64_t last_map_request_ms;
    int64_t last_map_success_ms;
    int64_t last_map_failure_ms;
    uint32_t map_source_w;
    uint32_t map_source_h;
    bool map_content_bounds_valid;
    uint32_t map_content_x0;
    uint32_t map_content_y0;
    uint32_t map_content_x1;
    uint32_t map_content_y1;
    lv_image_dsc_t popup_map_dsc;
    w_rr_cal_point_t calibration[W_RR_MAX_CAL_POINTS];
    size_t calibration_count;

    /* supported_features of the vacuum; 0 until the first state arrived. */
    uint32_t features;
    bool features_known;
    char mode_current[W_RR_MODE_LEN];
    char mode_options[W_RR_MAX_MODES][W_RR_MODE_LEN];
    char default_mode[W_RR_MODE_LEN]; /* selected before Start when it differs from the current mode */
    size_t mode_count;
    lv_obj_t *mode_popup;

    bool visible;
    bool destroyed;
} w_rr_ctx_t;

static int64_t w_rr_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Every vacuum gets the same card: picture, state, cleaning mode chip and icon
 * buttons for what the entity supports (start/pause, stop, dock, locate). The
 * older Roborock-specific layout (room list, map popup, 1x/2x/3x repeat count)
 * has been removed. */
static void w_rr_format_coord(double value, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    double scaled_value = value * 10.0;
    int64_t scaled = (int64_t)(scaled_value >= 0.0 ? scaled_value + 0.5 : scaled_value - 0.5);
    const bool negative = scaled < 0;
    if (negative) {
        scaled = -scaled;
    }

    (void)snprintf(buf, buf_len, "%s%lld.%01lld",
                   negative ? "-" : "",
                   (long long)(scaled / 10),
                   (long long)(scaled % 10));
}

static bool w_rr_is_visible(const w_rr_ctx_t *ctx)
{
    return ctx != NULL && ctx->visible && ctx->card != NULL && lv_obj_is_visible(ctx->card);
}

static bool w_rr_has_map_entity(const w_rr_ctx_t *ctx)
{
    return ctx != NULL && ctx->map_entity_id[0] != '\0';
}

static void *w_rr_calloc(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = calloc(count, size);
    }
    return ptr;
}

static bool w_rr_state_is_unavailable(const char *state)
{
    if (state == NULL) {
        return false;
    }
    return strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

static bool w_rr_state_is_returning(const char *state)
{
    return state != NULL && strstr(state, "return") != NULL;
}

static bool w_rr_state_is_active_clean(const char *state)
{
    if (state == NULL) {
        return false;
    }
    return strcmp(state, "cleaning") == 0 ||
           strcmp(state, "segment_cleaning") == 0 ||
           strcmp(state, "zoned_cleaning") == 0 ||
           strcmp(state, "room_cleaning") == 0 ||
           strcmp(state, "spot_cleaning") == 0 ||
           strcmp(state, "target_found") == 0 ||
           strcmp(state, "going_to_target") == 0 ||
           w_rr_state_is_returning(state);
}

static bool w_rr_state_has_value(const char *state)
{
    return state != NULL && state[0] != '\0' &&
           strcmp(state, "unknown") != 0 &&
           strcmp(state, "unavailable") != 0;
}

static bool w_rr_url_encode_component(const char *src, char *dst, size_t dst_sz)
{
    if (src == NULL || dst == NULL || dst_sz == 0) {
        return false;
    }

    static const char hex[] = "0123456789ABCDEF";
    size_t off = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
        unsigned char ch = *p;
        bool keep =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (keep) {
            if (off + 1 >= dst_sz) {
                dst[0] = '\0';
                return false;
            }
            dst[off++] = (char)ch;
            continue;
        }

        if (off + 3 >= dst_sz) {
            dst[0] = '\0';
            return false;
        }
        dst[off++] = '%';
        dst[off++] = hex[(ch >> 4) & 0x0F];
        dst[off++] = hex[ch & 0x0F];
    }

    dst[off] = '\0';
    return true;
}

static bool w_rr_build_image_proxy_url(const char *entity_id, const char *access_token,
                                       const char *state_value, char *out, size_t out_sz)
{
    if (entity_id == NULL || entity_id[0] == '\0' || out == NULL || out_sz == 0) {
        return false;
    }

    if (w_rr_state_has_value(state_value)) {
        char encoded_state[128] = {0};
        if (!w_rr_url_encode_component(state_value, encoded_state, sizeof(encoded_state))) {
            return false;
        }

        int written = 0;
        if (access_token != NULL && access_token[0] != '\0') {
            written = snprintf(out, out_sz, "/api/image_proxy/%s?token=%s&state=%s",
                entity_id, access_token, encoded_state);
        } else {
            written = snprintf(out, out_sz, "/api/image_proxy/%s?state=%s",
                entity_id, encoded_state);
        }
        return written > 0 && (size_t)written < out_sz;
    }

    int written = 0;
    if (access_token != NULL && access_token[0] != '\0') {
        written = snprintf(out, out_sz, "/api/image_proxy/%s?token=%s",
            entity_id, access_token);
    } else {
        written = snprintf(out, out_sz, "/api/image_proxy/%s", entity_id);
    }
    return written > 0 && (size_t)written < out_sz;
}

static void w_rr_humanize_state(const char *state, char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (state == NULL || state[0] == '\0') {
        snprintf(out, out_sz, "%s", ui_i18n_get("common.unknown", "Unknown"));
        return;
    }

    /* Translated name for the common vacuum states ("roborock.state.<state>"). */
    char path[64] = "roborock.state.";
    size_t plen = strlen(path);
    for (size_t i = 0; state[i] != '\0' && plen + 1 < sizeof(path); i++) {
        path[plen++] = state[i];
    }
    path[plen] = '\0';
    const char *translated = ui_i18n_get(path, "");
    if (translated[0] != '\0') {
        snprintf(out, out_sz, "%s", translated);
        return;
    }

    size_t j = 0;
    bool upper = true;
    for (size_t i = 0; state[i] != '\0' && j + 1 < out_sz; i++) {
        char ch = state[i];
        if (ch == '_') {
            out[j++] = ' ';
            upper = true;
            continue;
        }
        if (upper && ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - ('a' - 'A'));
        }
        out[j++] = ch;
        upper = (ch == ' ');
    }
    out[j] = '\0';
}

static lv_color_t w_rr_surface_fill(uint8_t mix)
{
    return lv_color_mix(lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BG), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), mix);
}


static lv_color_t w_rr_contrast_text(lv_color_t bg)
{
    return lv_color_brightness(bg) > 150 ? lv_color_hex(0x101418) : lv_color_hex(0xFFFFFF);
}

/* Readable text for a cleaning mode value such as "vac_and_mop": a translation from the
 * "vacmode" table when there is one, else the value with underscores as spaces. */
static void w_rr_mode_label(const char *raw, char *out, size_t out_len)
{
    char key[48];
    char pretty[W_RR_MODE_LEN];
    size_t n = 0;
    for (const char *p = raw; *p != '\0' && n + 1 < sizeof(pretty); p++) {
        pretty[n++] = *p == '_' ? ' ' : *p;
    }
    pretty[n] = '\0';
    if (n > 0 && pretty[0] >= 'a' && pretty[0] <= 'z') {
        pretty[0] = (char)(pretty[0] - 'a' + 'A');
    }
    snprintf(key, sizeof(key), "vacmode.%s", raw);
    for (char *p = key + 8; *p != '\0'; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        } else if (*p == ' ' || *p == '-') {
            *p = '_';
        }
    }
    snprintf(out, out_len, "%s", ui_i18n_get(key, pretty));
}

static bool w_rr_parse_calibration_point(cJSON *node, w_rr_cal_point_t *out)
{
    if (!cJSON_IsObject(node) || out == NULL) {
        return false;
    }

    cJSON *vacuum = cJSON_GetObjectItemCaseSensitive(node, "vacuum");
    cJSON *map = cJSON_GetObjectItemCaseSensitive(node, "map");
    if (!cJSON_IsObject(vacuum) || !cJSON_IsObject(map)) {
        return false;
    }

    cJSON *vacuum_x = cJSON_GetObjectItemCaseSensitive(vacuum, "x");
    cJSON *vacuum_y = cJSON_GetObjectItemCaseSensitive(vacuum, "y");
    cJSON *map_x = cJSON_GetObjectItemCaseSensitive(map, "x");
    cJSON *map_y = cJSON_GetObjectItemCaseSensitive(map, "y");
    if (!cJSON_IsNumber(vacuum_x) || !cJSON_IsNumber(vacuum_y) ||
        !cJSON_IsNumber(map_x) || !cJSON_IsNumber(map_y)) {
        return false;
    }

    out->vacuum_x = vacuum_x->valuedouble;
    out->vacuum_y = vacuum_y->valuedouble;
    out->map_x = map_x->valuedouble;
    out->map_y = map_y->valuedouble;
    return true;
}

static bool w_rr_compute_affine_from_calibration(const w_rr_cal_point_t *pts, size_t count,
                                                 double *a, double *b, double *c,
                                                 double *d, double *e, double *f)
{
    if (pts == NULL || count < 3 || a == NULL || b == NULL || c == NULL ||
        d == NULL || e == NULL || f == NULL) {
        return false;
    }

    double x1 = pts[0].vacuum_x, y1 = pts[0].vacuum_y;
    double x2 = pts[1].vacuum_x, y2 = pts[1].vacuum_y;
    double x3 = pts[2].vacuum_x, y3 = pts[2].vacuum_y;
    double u1 = pts[0].map_x, v1 = pts[0].map_y;
    double u2 = pts[1].map_x, v2 = pts[1].map_y;
    double u3 = pts[2].map_x, v3 = pts[2].map_y;

    double den = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2);
    if (fabs(den) < 0.000001) {
        return false;
    }

    *a = (u1 * (y2 - y3) + u2 * (y3 - y1) + u3 * (y1 - y2)) / den;
    *b = (u1 * (x3 - x2) + u2 * (x1 - x3) + u3 * (x2 - x1)) / den;
    *c = (u1 * (x2 * y3 - x3 * y2) + u2 * (x3 * y1 - x1 * y3) + u3 * (x1 * y2 - x2 * y1)) / den;
    *d = (v1 * (y2 - y3) + v2 * (y3 - y1) + v3 * (y1 - y2)) / den;
    *e = (v1 * (x3 - x2) + v2 * (x1 - x3) + v3 * (x2 - x1)) / den;
    *f = (v1 * (x2 * y3 - x3 * y2) + v2 * (x3 * y1 - x1 * y3) + v3 * (x1 * y2 - x2 * y1)) / den;
    return true;
}

static void w_rr_apply_affine(double a, double b, double c,
                              double d, double e, double f,
                              double x, double y, double *out_x, double *out_y)
{
    if (out_x == NULL || out_y == NULL) {
        return;
    }
    *out_x = a * x + b * y + c;
    *out_y = d * x + e * y + f;
}

static void w_rr_release_popup_map(w_rr_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, NULL);
    }
    if (ctx->popup_map_dsc.data != NULL) {
        heap_caps_free((void *)ctx->popup_map_dsc.data);
        ctx->popup_map_dsc.data = NULL;
        ctx->popup_map_dsc.data_size = 0;
    }
    if (ctx->popup_map_overlay != NULL) {
        lv_obj_clean(ctx->popup_map_overlay);
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
    }
    ctx->map_source_w = 0;
    ctx->map_source_h = 0;
    ctx->map_content_bounds_valid = false;
    ctx->map_content_x0 = 0;
    ctx->map_content_y0 = 0;
    ctx->map_content_x1 = 0;
    ctx->map_content_y1 = 0;
    ctx->map_loaded_url[0] = '\0';
}

/* busy: a fetch (map image or room list) is in flight and may still succeed, so a spinner sits above
 * the text; false is a settled state (nothing to wait for, or the fetch already failed), plain text only. */
static void w_rr_popup_show_map_placeholder(w_rr_ctx_t *ctx, const char *text, bool busy)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL || ctx->popup_map_placeholder == NULL) {
        return;
    }
    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, NULL);
    }
    if (ctx->popup_map_overlay != NULL) {
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
        lv_obj_clean(ctx->popup_map_overlay);
    }
    lv_obj_set_hidden(ctx->popup_map_placeholder, false);
    lv_label_set_text(ctx->popup_map_placeholder, text != NULL ? text : "");
    if (ctx->popup_map_spinner != NULL) {
        lv_obj_set_hidden(ctx->popup_map_spinner, !busy);
    }
    lv_obj_align(ctx->popup_map_placeholder, LV_ALIGN_CENTER, 0, busy ? 14 : 0);
    lv_obj_set_style_bg_color(ctx->popup_map_panel, w_rr_surface_fill(190), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_panel, LV_OPA_COVER, LV_PART_MAIN);
}

static void w_rr_update_popup_clean_button(w_rr_ctx_t *ctx);
static void w_rr_apply_visual(w_rr_ctx_t *ctx);
static void w_rr_layout_card(w_rr_ctx_t *ctx);

/* The state ("In der Station") shrinks to the board's smallest text before it ends in dots beside the charge chip. */
static void w_rr_fit_state(w_rr_ctx_t *ctx)
{
    const lv_coord_t width = lv_obj_get_width(ctx->state_label);
    if (width <= 0) return;
    ui_text_record(ctx->state_label, UI_TEXT_META, true);
    lv_obj_set_style_text_font(ctx->state_label, ui_text_fit(ctx->state_text, ui_font_role(UI_TEXT_META), width,
                                                             lv_font_get_line_height(ui_font_role(UI_TEXT_META))),
                               LV_PART_MAIN);
}
static void w_rr_render_map_overlay(w_rr_ctx_t *ctx);

static void w_rr_update_map_content_bounds(w_rr_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->map_content_bounds_valid = false;
    ctx->map_content_x0 = 0;
    ctx->map_content_y0 = 0;
    ctx->map_content_x1 = 0;
    ctx->map_content_y1 = 0;

    const lv_image_dsc_t *dsc = &ctx->popup_map_dsc;
    if (dsc->data == NULL || dsc->header.cf != LV_COLOR_FORMAT_RGB565A8 ||
        dsc->header.w == 0 || dsc->header.h == 0 || dsc->header.stride == 0) {
        return;
    }

    uint32_t w = dsc->header.w;
    uint32_t h = dsc->header.h;
    uint32_t color_stride = dsc->header.stride;
    uint32_t alpha_stride = color_stride / 2U;
    size_t alpha_offset = (size_t)color_stride * h;
    size_t min_size = alpha_offset + ((size_t)alpha_stride * h);
    if (alpha_stride < w || dsc->data_size < min_size) {
        return;
    }

    const uint8_t *alpha = ((const uint8_t *)dsc->data) + alpha_offset;
    uint32_t x0 = w;
    uint32_t y0 = h;
    uint32_t x1 = 0;
    uint32_t y1 = 0;
    uint32_t samples = 0;

    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = alpha + ((size_t)y * alpha_stride);
        for (uint32_t x = 0; x < w; x++) {
            if (row[x] < 24U) {
                continue;
            }
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
            samples++;
        }
    }

    if (samples == 0 || x0 > x1 || y0 > y1) {
        return;
    }

    ctx->map_content_bounds_valid = true;
    ctx->map_content_x0 = x0;
    ctx->map_content_y0 = y0;
    ctx->map_content_x1 = x1;
    ctx->map_content_y1 = y1;
}

static bool w_rr_project_vacuum_to_overlay(const w_rr_ctx_t *ctx,
                                           double vacuum_x, double vacuum_y,
                                           lv_coord_t map_w, lv_coord_t map_h,
                                           uint32_t source_w, uint32_t source_h,
                                           lv_coord_t *out_x, lv_coord_t *out_y)
{
    if (ctx == NULL || ctx->calibration_count < 3 || out_x == NULL || out_y == NULL ||
        map_w <= 0 || map_h <= 0 || source_w == 0 || source_h == 0) {
        return false;
    }

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0, f = 0.0;
    if (!w_rr_compute_affine_from_calibration(ctx->calibration, ctx->calibration_count, &a, &b, &c, &d, &e, &f)) {
        return false;
    }

    double map_px = 0.0;
    double map_py = 0.0;
    w_rr_apply_affine(a, b, c, d, e, f, vacuum_x, vacuum_y, &map_px, &map_py);

    lv_coord_t x = (lv_coord_t)((map_px / (double)source_w) * map_w);
    lv_coord_t y = (lv_coord_t)((map_py / (double)source_h) * map_h);
    if (x < -24 || y < -24 || x > map_w + 24 || y > map_h + 24) {
        return false;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > map_w) x = map_w;
    if (y > map_h) y = map_h;
    *out_x = x;
    *out_y = y;
    return true;
}

static bool w_rr_render_robot_overlay(w_rr_ctx_t *ctx, lv_coord_t map_w, lv_coord_t map_h,
                                      uint32_t source_w, uint32_t source_h)
{
    if (ctx == NULL || ctx->popup_map_overlay == NULL || !ctx->robot_position.valid) {
        return false;
    }

    bool rendered = false;
    lv_color_t accent = lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON),
                                     lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 172);
    lv_color_t halo = lv_color_mix(accent, lv_color_white(), 72);
    lv_color_t core = lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY);
    lv_color_t ring = lv_color_white();

    for (size_t i = 0; i < ctx->robot_trail_count && i < W_RR_POSITION_TRAIL_POINTS; i++) {
        const w_rr_position_t *pos = &ctx->robot_trail[i];
        if (!pos->valid) {
            continue;
        }

        lv_coord_t x = 0;
        lv_coord_t y = 0;
        if (!w_rr_project_vacuum_to_overlay(ctx, pos->x, pos->y, map_w, map_h, source_w, source_h, &x, &y)) {
            continue;
        }

        uint8_t age = (uint8_t)(ctx->robot_trail_count - i);
        lv_coord_t size = 7 + (lv_coord_t)(age / 2U);
        lv_obj_t *dot = lv_obj_create(ctx->popup_map_overlay);
        lv_obj_remove_style_all(dot);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_size(dot, size, size);
        lv_obj_set_pos(dot, x - size / 2, y - size / 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, age > 5 ? LV_OPA_50 : LV_OPA_30, LV_PART_MAIN);
        rendered = true;
    }

    lv_coord_t x = 0;
    lv_coord_t y = 0;
    if (!w_rr_project_vacuum_to_overlay(ctx, ctx->robot_position.x, ctx->robot_position.y,
                                        map_w, map_h, source_w, source_h, &x, &y)) {
        return rendered;
    }

    lv_obj_t *marker = lv_obj_create(ctx->popup_map_overlay);
    lv_obj_remove_style_all(marker);
    lv_obj_set_clickable(marker, false);
    lv_obj_set_scrollable(marker, false);
    lv_obj_set_size(marker, 30, 30);
    lv_obj_set_pos(marker, x - 15, y - 15);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(marker, halo, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(marker, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(marker, ring, LV_PART_MAIN);
    lv_obj_set_style_border_opa(marker, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *inner = lv_obj_create(marker);
    lv_obj_remove_style_all(inner);
    lv_obj_set_clickable(inner, false);
    lv_obj_set_scrollable(inner, false);
    lv_obj_set_size(inner, 18, 18);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(inner, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(inner);

    lv_obj_t *core_dot = lv_obj_create(marker);
    lv_obj_remove_style_all(core_dot);
    lv_obj_set_clickable(core_dot, false);
    lv_obj_set_scrollable(core_dot, false);
    lv_obj_set_size(core_dot, 7, 7);
    lv_obj_set_style_radius(core_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(core_dot, core, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(core_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(core_dot);
    return true;
}

static bool w_rr_map_can_refresh_now(w_rr_ctx_t *ctx, int64_t now_ms)
{
    if (ctx == NULL || ctx->card == NULL || ctx->popup_map_panel == NULL || !w_rr_has_map_entity(ctx)) {
        return false;
    }
    if (!w_rr_is_visible(ctx) || !lv_obj_is_visible(ctx->popup_map_panel)) {
        return false;
    }

    int64_t min_interval = ctx->active_clean ? W_RR_MAP_REFRESH_ACTIVE_MS : W_RR_MAP_REFRESH_IDLE_MS;
    if (ctx->popup_map_failed) {
        min_interval = W_RR_MAP_RETRY_MS;
    }
    if (ctx->last_map_request_ms > 0 && (now_ms - ctx->last_map_request_ms) < min_interval) {
        return false;
    }
    return true;
}

static bool w_rr_get_displayed_map_rect(const w_rr_ctx_t *ctx,
                                        lv_coord_t *out_x, lv_coord_t *out_y,
                                        lv_coord_t *out_w, lv_coord_t *out_h,
                                        uint16_t *out_zoom)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL ||
        ctx->popup_map_dsc.data == NULL ||
        ctx->popup_map_dsc.header.w == 0 || ctx->popup_map_dsc.header.h == 0) {
        return false;
    }

    lv_coord_t tgt_w = lv_obj_get_width(ctx->popup_map_panel);
    lv_coord_t tgt_h = lv_obj_get_height(ctx->popup_map_panel);
    if (tgt_w <= 0 || tgt_h <= 0) {
        return false;
    }

    int32_t zoom_w = (int32_t)((int64_t)tgt_w * 256 / ctx->popup_map_dsc.header.w);
    int32_t zoom_h = (int32_t)((int64_t)tgt_h * 256 / ctx->popup_map_dsc.header.h);
    int32_t zoom = zoom_w < zoom_h ? zoom_w : zoom_h;
    if (zoom < 16) {
        zoom = 16;
    }

    int32_t disp_w = (int32_t)(((int64_t)ctx->popup_map_dsc.header.w * zoom) / 256);
    int32_t disp_h = (int32_t)(((int64_t)ctx->popup_map_dsc.header.h * zoom) / 256);
    if (disp_w < 1 || disp_h < 1) {
        return false;
    }

    int32_t rect_x = (tgt_w - disp_w) / 2;
    int32_t rect_y = (tgt_h - disp_h) / 2;

    if (ctx->map_content_bounds_valid) {
        int32_t content_cx = (int32_t)(((int64_t)(ctx->map_content_x0 + ctx->map_content_x1 + 1U) * zoom) / 512);
        int32_t content_cy = (int32_t)(((int64_t)(ctx->map_content_y0 + ctx->map_content_y1 + 1U) * zoom) / 512);
        rect_x += (tgt_w / 2) - (rect_x + content_cx);
        rect_y += (tgt_h / 2) - (rect_y + content_cy);
    }

    if (out_x != NULL) {
        *out_x = (lv_coord_t)rect_x;
    }
    if (out_y != NULL) {
        *out_y = (lv_coord_t)rect_y;
    }
    if (out_w != NULL) {
        *out_w = (lv_coord_t)disp_w;
    }
    if (out_h != NULL) {
        *out_h = (lv_coord_t)disp_h;
    }
    if (out_zoom != NULL) {
        *out_zoom = (uint16_t)zoom;
    }
    return true;
}

static bool w_rr_has_no_map_view(const w_rr_ctx_t *ctx)
{
    return ctx != NULL && !w_rr_has_map_entity(ctx);
}

static void w_rr_clear_map_view(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL || ctx->popup_map_overlay == NULL) {
        return;
    }
    lv_obj_clean(ctx->popup_map_overlay);
    lv_obj_set_hidden(ctx->popup_map_overlay, true);
    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, NULL);
    }
    if (ctx->popup_map_placeholder != NULL) {
        lv_obj_set_hidden(ctx->popup_map_placeholder, true);
    }
    if (ctx->popup_map_spinner != NULL) {
        lv_obj_set_hidden(ctx->popup_map_spinner, true);
    }
    if (ctx->popup_map_name_label != NULL) {
        lv_obj_set_hidden(ctx->popup_map_name_label, true);
    }
}

static void w_rr_render_map_overlay(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_overlay == NULL) {
        return;
    }

    lv_obj_clean(ctx->popup_map_overlay);
    if (w_rr_has_no_map_view(ctx)) {
        w_rr_clear_map_view(ctx);
        return;
    }
    lv_obj_set_scrollable(ctx->popup_map_overlay, false);
    lv_obj_set_scroll_dir(ctx->popup_map_overlay, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(ctx->popup_map_overlay, LV_SCROLLBAR_MODE_OFF);
    if (ctx->popup_map_dsc.data == NULL || ctx->calibration_count < 3) {
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
        return;
    }

    uint32_t source_w = ctx->map_source_w > 0 ? ctx->map_source_w : ctx->popup_map_dsc.header.w;
    uint32_t source_h = ctx->map_source_h > 0 ? ctx->map_source_h : ctx->popup_map_dsc.header.h;
    if (source_w == 0 || source_h == 0) {
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
        return;
    }

    lv_coord_t map_x = 0;
    lv_coord_t map_y = 0;
    lv_coord_t map_w = 0;
    lv_coord_t map_h = 0;
    if (!w_rr_get_displayed_map_rect(ctx, &map_x, &map_y, &map_w, &map_h, NULL) || map_w <= 0 || map_h <= 0) {
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
        return;
    }

    lv_obj_set_pos(ctx->popup_map_overlay, map_x, map_y);
    lv_obj_set_size(ctx->popup_map_overlay, map_w, map_h);
    lv_obj_set_hidden(ctx->popup_map_overlay, false);
    lv_obj_move_foreground(ctx->popup_map_overlay);
    if (ctx->popup_map_name_label != NULL) {
        lv_obj_move_foreground(ctx->popup_map_name_label);
    }

    if (!w_rr_render_robot_overlay(ctx, map_w, map_h, source_w, source_h)) {
        lv_obj_set_hidden(ctx->popup_map_overlay, true);
    }
}

static void w_rr_layout_map_visual(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL) {
        return;
    }

    if (!w_rr_has_map_entity(ctx)) {
        w_rr_release_popup_map(ctx);
        lv_obj_set_hidden(ctx->popup_map_panel, true);
        ctx->popup_map_request_pending = false;
        ctx->popup_map_failed = false;
        return;
    }
    lv_obj_set_hidden(ctx->popup_map_panel, false);

    if (ctx->popup_map_placeholder != NULL) {
        lv_obj_center(ctx->popup_map_placeholder);
    }

    if (w_rr_has_no_map_view(ctx)) {
        w_rr_clear_map_view(ctx);
        return;
    }

    if (ctx->popup_map_img == NULL || ctx->popup_map_dsc.data == NULL ||
        ctx->popup_map_dsc.header.w == 0 || ctx->popup_map_dsc.header.h == 0) {
        if (ctx->popup_map_img != NULL) {
            lv_obj_set_pos(ctx->popup_map_img, 0, 0);
            lv_obj_set_size(ctx->popup_map_img, 1, 1);
        }
        if (ctx->popup_map_overlay != NULL) {
            lv_obj_set_hidden(ctx->popup_map_overlay, true);
        }
        return;
    }

    lv_coord_t map_x = 0;
    lv_coord_t map_y = 0;
    uint16_t zoom = 0;
    if (!w_rr_get_displayed_map_rect(ctx, &map_x, &map_y, NULL, NULL, &zoom)) {
        return;
    }
    lv_image_set_inner_align(ctx->popup_map_img, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_pivot(ctx->popup_map_img, 0, 0);
    lv_obj_set_size(ctx->popup_map_img, ctx->popup_map_dsc.header.w, ctx->popup_map_dsc.header.h);
    lv_image_set_scale(ctx->popup_map_img, zoom);
    lv_obj_set_pos(ctx->popup_map_img, map_x, map_y);
    w_rr_render_map_overlay(ctx);
}

static void w_rr_popup_map_cb(void *user, const ha_cover_result_t *result)
{
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)user;
    if (ctx == NULL) {
        return;
    }
    int64_t now_ms = w_rr_now_ms();
    ctx->popup_map_request_inflight = false;
    bool had_visible_map = (ctx->popup_map_dsc.data != NULL);
    if (ctx->popup_map_panel == NULL) {
        if (result != NULL && result->valid && result->image.data != NULL) {
            heap_caps_free((void *)result->image.data);
        }
        return;
    }
    if (result == NULL || !result->valid) {
        ctx->popup_map_failed = true;
        ctx->last_map_failure_ms = now_ms;
        ctx->popup_map_request_pending = true;
        ESP_LOGW(W_RR_TAG, "Map fetch failed for %s", ctx->map_url);
        if (!had_visible_map) {
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_failed", "Map preview unavailable"), false);
        }
        return;
    }

    w_rr_release_popup_map(ctx);
    ctx->popup_map_dsc = result->image;
    ctx->map_source_w = result->source_w;
    ctx->map_source_h = result->source_h;
    w_rr_update_map_content_bounds(ctx);
    ctx->popup_map_failed = false;
    ctx->last_map_success_ms = now_ms;
    snprintf(ctx->map_loaded_url, sizeof(ctx->map_loaded_url), "%s", ctx->map_url);

    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, &ctx->popup_map_dsc);
        lv_obj_set_hidden(ctx->popup_map_placeholder, true);
        if (ctx->popup_map_spinner != NULL) {
            lv_obj_set_hidden(ctx->popup_map_spinner, true);
        }
        w_rr_layout_map_visual(ctx);
        lv_obj_set_style_bg_opa(ctx->popup_map_panel, LV_OPA_TRANSP, LV_PART_MAIN);
        ESP_LOGI(W_RR_TAG, "Map fetched: %ux%u for %s",
            (unsigned)ctx->popup_map_dsc.header.w,
            (unsigned)ctx->popup_map_dsc.header.h,
            ctx->map_entity_id);
    }
}

static void w_rr_request_popup_map(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL) {
        return;
    }
    if (!w_rr_has_map_entity(ctx)) {
        ctx->popup_map_request_pending = false;
        ctx->popup_map_failed = false;
        w_rr_layout_map_visual(ctx);
        return;
    }
    if (ctx->popup_map_request_inflight) {
        return;
    }
    if (ctx->map_url[0] == '\0') {
        ctx->popup_map_request_pending = false;
        ctx->popup_map_failed = false;
        if (ctx->map_entity_id[0] != '\0') {
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_waiting", "Waiting for map"), true);
        } else {
            w_rr_clear_map_view(ctx);
        }
        return;
    }
    if (strncmp(ctx->map_loaded_url, ctx->map_url, sizeof(ctx->map_loaded_url)) == 0 && ctx->popup_map_dsc.data != NULL) {
        ctx->popup_map_request_pending = false;
        return;
    }

    int64_t now_ms = w_rr_now_ms();
    if (!w_rr_map_can_refresh_now(ctx, now_ms)) {
        ctx->popup_map_request_pending = true;
        return;
    }

    lv_coord_t tgt_w = lv_obj_get_width(ctx->popup_map_panel);
    lv_coord_t tgt_h = lv_obj_get_height(ctx->popup_map_panel);
    if (tgt_w <= 0) {
        tgt_w = 320;
    }
    if (tgt_h <= 0) {
        tgt_h = 160;
    }

    ctx->popup_map_request_inflight = true;
    ctx->popup_map_request_pending = false;
    ctx->last_map_request_ms = now_ms;
    if (ctx->popup_map_dsc.data == NULL) {
        w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_loading", "Loading map"), true);
    }
    if (ha_cover_fetcher_request(ctx->map_url, tgt_w, tgt_h, w_rr_popup_map_cb, ctx) != ESP_OK) {
        ctx->popup_map_request_inflight = false;
        ctx->popup_map_request_pending = true;
        ctx->popup_map_failed = false;
        if (ctx->popup_map_dsc.data == NULL) {
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_waiting", "Waiting for map"), true);
        }
    }
}

static bool w_rr_parse_position_object(cJSON *obj, w_rr_position_t *out)
{
    if (!cJSON_IsObject(obj) || out == NULL) {
        return false;
    }

    cJSON *x = cJSON_GetObjectItemCaseSensitive(obj, "x");
    cJSON *y = cJSON_GetObjectItemCaseSensitive(obj, "y");
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
        return false;
    }

    out->x = x->valuedouble;
    out->y = y->valuedouble;
    out->updated_ms = w_rr_now_ms();
    out->valid = true;
    return true;
}

static bool w_rr_parse_position_any(cJSON *node, w_rr_position_t *out, uint8_t depth)
{
    if (node == NULL || out == NULL || depth > 4) {
        return false;
    }
    if (w_rr_parse_position_object(node, out)) {
        return true;
    }

    if (cJSON_IsArray(node) && cJSON_GetArraySize(node) >= 2) {
        cJSON *x = cJSON_GetArrayItem(node, 0);
        cJSON *y = cJSON_GetArrayItem(node, 1);
        if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
            out->x = x->valuedouble;
            out->y = y->valuedouble;
            out->updated_ms = w_rr_now_ms();
            out->valid = true;
            return true;
        }
    }

    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, node)
        {
            if (w_rr_parse_position_any(child, out, depth + 1U)) {
                return true;
            }
        }
    }
    return false;
}

static bool w_rr_parse_position_result(cJSON *result, w_rr_position_t *out)
{
    if (result == NULL || out == NULL) {
        return false;
    }
    return w_rr_parse_position_any(result, out, 0);
}

static void w_rr_position_response_cb(bool success, cJSON *result, void *user)
{
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)user;
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }

    w_rr_position_t parsed = {0};
    bool parse_ok = success && w_rr_parse_position_result(result, &parsed);
    const int64_t now = w_rr_now_ms();
    uint32_t request_seq = 0;
    int64_t request_ms = 0;
    bool destroyed = false;

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    destroyed = ctx->destroyed;
    request_seq = ctx->position_request_seq;
    request_ms = ctx->last_position_request_ms;
    if (!ctx->destroyed) {
        ctx->pending_robot_position = parsed;
        ctx->pending_position_failure = !parse_ok;
        ctx->pending_position_ready = true;
    }
    xSemaphoreGive(ctx->staging_mutex);

    if (destroyed) {
        return;
    }

    const long long latency_ms = request_ms > 0 ? (long long)(now - request_ms) : -1LL;
    if (parse_ok) {
        char x_text[24];
        char y_text[24];
        w_rr_format_coord(parsed.x, x_text, sizeof(x_text));
        w_rr_format_coord(parsed.y, y_text, sizeof(y_text));
        ESP_LOGD(W_RR_TAG,
                 "get_vacuum_current_position[%u] ok: x=%s y=%s latency=%lld ms",
                 (unsigned)request_seq, x_text, y_text, latency_ms);
    } else {
        ESP_LOGW(W_RR_TAG,
                 "get_vacuum_current_position[%u] failed: success=%d has_result=%d latency=%lld ms",
                 (unsigned)request_seq, success ? 1 : 0, result != NULL ? 1 : 0, latency_ms);
    }
}

static void w_rr_push_robot_position(w_rr_ctx_t *ctx, const w_rr_position_t *pos)
{
    if (ctx == NULL || pos == NULL || !pos->valid) {
        return;
    }

    if (ctx->robot_position.valid) {
        double dx = ctx->robot_position.x - pos->x;
        double dy = ctx->robot_position.y - pos->y;
        double dist2 = dx * dx + dy * dy;
        if (dist2 > 6400.0) {
            if (ctx->robot_trail_count >= W_RR_POSITION_TRAIL_POINTS) {
                memmove(&ctx->robot_trail[0], &ctx->robot_trail[1],
                        sizeof(ctx->robot_trail[0]) * (W_RR_POSITION_TRAIL_POINTS - 1U));
                ctx->robot_trail_count = W_RR_POSITION_TRAIL_POINTS - 1U;
            }
            ctx->robot_trail[ctx->robot_trail_count++] = ctx->robot_position;
        }
    }

    ctx->robot_position = *pos;
}

static void w_rr_drain_pending_position(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }

    bool ready = false;
    bool failed = false;
    w_rr_position_t staged = {0};

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    ready = ctx->pending_position_ready;
    if (ready) {
        failed = ctx->pending_position_failure;
        staged = ctx->pending_robot_position;
        ctx->pending_robot_position = (w_rr_position_t){0};
        ctx->pending_position_ready = false;
        ctx->pending_position_failure = false;
    }
    xSemaphoreGive(ctx->staging_mutex);

    if (!ready) {
        return;
    }

    ctx->position_fetching = false;
    ctx->position_failed = failed;
    if (!failed && staged.valid) {
        ctx->last_position_success_ms = staged.updated_ms;
        w_rr_push_robot_position(ctx, &staged);
        w_rr_render_map_overlay(ctx);
    }
}

static void w_rr_request_position(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->position_fetching) {
        return;
    }
    if (!ha_client_is_connected() || !ha_client_is_initial_sync_done()) {
        return;
    }

    ctx->position_fetching = true;
    int64_t previous_request_ms = ctx->last_position_request_ms;
    int64_t now = w_rr_now_ms();
    uint32_t request_seq = ++ctx->position_request_seq;
    ctx->last_position_request_ms = now;
    ESP_LOGD(W_RR_TAG,
             "Requesting roborock.get_vacuum_current_position[%u] for %s interval=%lld ms",
             (unsigned)request_seq,
             ctx->entity_id,
             previous_request_ms > 0 ? (long long)(now - previous_request_ms) : -1LL);
    esp_err_t err = ha_client_call_service_with_response(
        "roborock", "get_vacuum_current_position", ctx->entity_id, NULL, w_rr_position_response_cb, ctx);
    if (err != ESP_OK) {
        ctx->position_fetching = false;
        if (err != ESP_ERR_INVALID_STATE) {
            ctx->position_failed = true;
            ESP_LOGW(W_RR_TAG,
                     "get_vacuum_current_position[%u] request failed (%s): %s",
                     (unsigned)request_seq,
                     ctx->entity_id,
                     esp_err_to_name(err));
        }
    }
}



static void w_rr_merge_map_attr_metadata(w_rr_ctx_t *ctx, cJSON *attrs)
{
    if (ctx == NULL || !cJSON_IsObject(attrs)) {
        return;
    }

    /* The map image's calibration points tie vacuum coordinates to map pixels (the robot marker and its trail). */
    cJSON *calibration_points = cJSON_GetObjectItemCaseSensitive(attrs, "calibration_points");
    size_t calibration_count = 0;
    w_rr_cal_point_t calibration[W_RR_MAX_CAL_POINTS] = {0};
    if (cJSON_IsArray(calibration_points)) {
        cJSON *pt = NULL;
        cJSON_ArrayForEach(pt, calibration_points)
        {
            if (calibration_count >= W_RR_MAX_CAL_POINTS) {
                break;
            }
            if (w_rr_parse_calibration_point(pt, &calibration[calibration_count])) {
                calibration_count++;
            }
        }
    }

    if (calibration_count >= 3) {
        memcpy(ctx->calibration, calibration, calibration_count * sizeof(calibration[0]));
        ctx->calibration_count = calibration_count;
    } else {
        ctx->calibration_count = 0;
    }

    if (w_rr_is_visible(ctx)) {
        w_rr_update_popup_clean_button(ctx);
    }
}

static void w_rr_style_control(lv_obj_t *btn, lv_obj_t *label, ui_control_role_t role, bool enabled)
{
    if (btn == NULL) return;
    lv_obj_set_style_radius(btn, ui_style_radius(18), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    ui_style_action_button(btn, label, role, enabled);
}

static esp_err_t w_rr_call_vacuum_simple(const char *service, const char *entity_id);
static void w_rr_command_failed(w_rr_ctx_t *ctx, const char *what, esp_err_t err);

static void w_rr_style_stop_button(w_rr_ctx_t *ctx)
{
    if (ctx != NULL) w_rr_style_control(ctx->stop_btn, ctx->stop_label, UI_CONTROL_ACTION, !ctx->unavailable);
}

/* Colors of the line-art robot: gray while idle, accent while working, red on
 * an error. */
static void w_rr_update_art_colors(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->art == NULL) {
        return;
    }
    lv_color_t line = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    if (ctx->unavailable) {
        line = lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_MUTED), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 120);
    } else if (ctx->error) {
        line = lv_color_hex(APP_UI_COLOR_ERROR);
    } else if (ctx->active_clean || ctx->returning) {
        line = lv_color_hex(APP_UI_COLOR_STATE_ON);
    } else if (ctx->paused) {
        line = lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE);
    }
    lv_color_t fill = w_rr_surface_fill(206);
    lv_obj_set_style_border_color(ctx->art_disc, line, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->art_disc, fill, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->art_inner, lv_color_mix(line, fill, 150), LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->art_lidar, line, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->art_slot, lv_color_mix(line, fill, 190), LV_PART_MAIN);
}

/* Sizes and places the four circles of the robot inside a square of `side`. */
static void w_rr_layout_art(w_rr_ctx_t *ctx, lv_coord_t x, lv_coord_t y, lv_coord_t side)
{
    if (ctx == NULL || ctx->art == NULL || side < 40) {
        return;
    }
    lv_obj_set_pos(ctx->art, x, y);
    lv_obj_set_size(ctx->art, side, side);
    const lv_coord_t stroke = LV_MAX(2, side / 48);
    lv_obj_set_size(ctx->art_disc, side, side);
    lv_obj_set_pos(ctx->art_disc, 0, 0);
    lv_obj_set_style_border_width(ctx->art_disc, stroke + 1, LV_PART_MAIN);
    const lv_coord_t inner = (side * 80) / 100;
    lv_obj_set_size(ctx->art_inner, inner, inner);
    lv_obj_set_pos(ctx->art_inner, (side - inner) / 2, (side - inner) / 2);
    lv_obj_set_style_border_width(ctx->art_inner, stroke, LV_PART_MAIN);
    const lv_coord_t lidar = (side * 27) / 100;
    lv_obj_set_size(ctx->art_lidar, lidar, lidar);
    lv_obj_set_pos(ctx->art_lidar, (side - lidar) / 2, (side * 20) / 100);
    lv_obj_set_style_border_width(ctx->art_lidar, stroke + 1, LV_PART_MAIN);
    const lv_coord_t slot_w = LV_MAX(6, (side * 8) / 100);
    const lv_coord_t slot_h = LV_MAX(10, (side * 17) / 100);
    lv_obj_set_size(ctx->art_slot, slot_w, slot_h);
    lv_obj_set_pos(ctx->art_slot, (side - slot_w) / 2, (side * 68) / 100);
    lv_obj_set_style_border_width(ctx->art_slot, stroke, LV_PART_MAIN);
    w_rr_update_art_colors(ctx);
}

static void w_rr_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_set_hidden(obj, true);
    } else {
        lv_obj_set_hidden(obj, false);
    }
}

static void w_rr_set_label_if_changed(lv_obj_t *label, const lv_font_t *font, const char *text)
{
    if (label == NULL) {
        return;
    }
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    }
    if (text != NULL && strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

/* Card layout for a vacuum without Roborock services, after Home Assistant's
 * own vacuum card: robot picture, name, state, a dropdown-style mode chip and
 * a row of icon buttons (start/pause, stop, dock, locate). Wide tiles put the
 * picture on the left, tall tiles on top. */
static bool w_rr_can_set_fan(const w_rr_ctx_t *ctx)
{
    return ctx->fan_options.count > 0 && (!ctx->features_known || (ctx->features & W_RR_FEAT_FAN_SPEED) != 0);
}

static void w_rr_fan_picker_closed(void *user)
{
    ((w_rr_ctx_t *)user)->fan_popup = NULL;
}

static void w_rr_fan_picker_label(void *user, size_t index, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", ((w_rr_ctx_t *)user)->fan_options.values[index]);
}

static void w_rr_fan_picker_chosen(void *user, size_t index)
{
    w_rr_ctx_t *ctx = user;
    if (ctx->unavailable || !ha_client_is_connected() || !w_rr_can_set_fan(ctx) || index >= ctx->fan_options.count) return;
    char payload[768];
    if (!ha_control_option_payload(ctx->entity_id, "fan_speed", ctx->fan_options.values[index], payload, sizeof(payload))) return;
    if (ha_client_call_service("vacuum", "set_fan_speed", payload) == ESP_OK) {
        snprintf(ctx->fan_speed, sizeof(ctx->fan_speed), "%s", ctx->fan_options.values[index]);
        w_rr_layout_card(ctx);
    }
}

static void w_rr_fan_event_cb(lv_event_t *event)
{
    w_rr_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx->unavailable || !w_rr_can_set_fan(ctx) || ctx->fan_popup != NULL) return;
    int current = ha_control_options_find(&ctx->fan_options, ctx->fan_speed);
    ui_option_picker_cfg_t cfg = {
        .title = ui_i18n_get("roborock.fan_title", "Suction level"), .count = ctx->fan_options.count,
        .current = current >= 0 ? (size_t)current : UI_OPTION_PICKER_NONE,
        .label = w_rr_fan_picker_label, .chosen = w_rr_fan_picker_chosen,
        .closed = w_rr_fan_picker_closed, .user = ctx,
    };
    ctx->fan_popup = ui_option_picker_open(&cfg);
}

static void w_rr_layout_generic(w_rr_ctx_t *ctx, lv_coord_t inner_w, lv_coord_t inner_h)
{
    w_rr_set_hidden(ctx->popup_map_name_label, true);
    w_rr_set_hidden(ctx->detail_label, true);
    w_rr_set_hidden(ctx->art, false);

    const bool wide = inner_w * 10 >= inner_h * 12;
    /* Height the name, the state, the chip and the button row need under the picture or the map: it grows with
     * the text size (a larger step has taller lines), so the picture gets only what is left. */
    const lv_coord_t meta_line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t below_art = meta_line + 11 + ui_widget_name_gap(ctx->title_label, meta_line + 7) + 40 + 12 + 46;
    /* An image entity replaces the picture with the live map: left of the controls on a wide tile, above the
     * name on a tall one, as long as the tile leaves the controls their ~200 px (a smaller tile keeps the picture). */
    const bool map_entity = w_rr_has_map_entity(ctx) && ctx->popup_map_panel != NULL;
    lv_coord_t map_w = 0;
    lv_coord_t map_h = 0;
    if (map_entity) {
        if (wide) {
            map_w = (inner_w * 56) / 100;
            map_h = inner_h;
        } else {
            map_w = inner_w;
            map_h = inner_h - 10 - below_art;
        }
        if (map_w < 96 || map_h < 96) {
            map_w = 0;
            map_h = 0;
        }
    }
    const bool show_map = map_w > 0 && map_h > 0;
    w_rr_set_hidden(ctx->popup_map_panel, !show_map);
    const bool show_battery = ctx->battery_pct >= 0;
    lv_coord_t battery_w = 0;
    if (ctx->battery_chip != NULL) {
        w_rr_set_hidden(ctx->battery_chip, !show_battery);
        if (show_battery) {
            lv_obj_update_layout(ctx->battery_chip);
            battery_w = lv_obj_get_width(ctx->battery_chip);
            lv_obj_align(ctx->battery_chip, LV_ALIGN_TOP_RIGHT, 0, 0); /* created TOP_RIGHT: set_pos would offset from that */
        }
    }

    lv_coord_t art_side;
    lv_coord_t col_x;
    lv_coord_t col_w;
    lv_coord_t name_y;
    if (show_map) {
        w_rr_set_hidden(ctx->art, true);
        lv_obj_set_pos(ctx->popup_map_panel, 0, 0);
        lv_obj_set_size(ctx->popup_map_panel, map_w, map_h);
        if (wide) {
            col_x = map_w + 16;
            col_w = inner_w - col_x;
            name_y = 0;
        } else {
            col_x = 0;
            col_w = inner_w;
            name_y = map_h + 10;
        }
        w_rr_layout_map_visual(ctx);
        if (ctx->battery_chip != NULL) {
            lv_obj_move_foreground(ctx->battery_chip); /* the charge chip stays readable on top of the map */
        }
    } else if (wide) {
        art_side = LV_MIN(inner_h - 4, (inner_w * 42) / 100);
        w_rr_layout_art(ctx, 0, (inner_h - art_side) / 2, art_side);
        col_x = art_side + 16;
        col_w = inner_w - col_x;
        name_y = 0;
    } else {
        /* The picture gets what is left after name, state, chip and buttons
         * (about 170 px); on short tiles it is dropped so the buttons stay. */
        art_side = LV_MIN((inner_w * 70) / 100, LV_MIN((inner_h * 45) / 100, inner_h - 12 - below_art));
        col_x = 0;
        col_w = inner_w;
        if (art_side >= 56) {
            w_rr_set_hidden(ctx->art, false);
            w_rr_layout_art(ctx, (inner_w - art_side) / 2, 4, art_side);
            name_y = art_side + 12;
        } else {
            w_rr_set_hidden(ctx->art, true);
            name_y = 0;
        }
    }

    /* Name and state. */
    const bool beside_chip = wide || name_y == 0; /* no picture: left-aligned beside the chip, not centred under it */
    lv_coord_t text_w = col_w - (beside_chip ? battery_w + 8 : 0);
    if (text_w < 60) text_w = 60;
    /* the name-to-state gap and the state row follow the line height of the text (25 px at the reference size: the former 32 and 36) */
    const lv_coord_t name_line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t name_gap = name_line + 7;
    if (ctx->title_label != NULL) {
        lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(ctx->title_label, beside_chip ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_size(ctx->title_label, text_w, lv_font_get_line_height(ui_font_role(UI_TEXT_META))); /* one line in dot mode: a wrapped name would run into the state line */
        lv_obj_set_pos(ctx->title_label, col_x, name_y + 2);
    }
    if (ctx->state_label != NULL) {
        lv_obj_set_style_text_align(ctx->state_label, beside_chip ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_size(ctx->state_label, text_w, name_line);
        lv_obj_set_pos(ctx->state_label, col_x, name_y + ui_widget_name_gap(ctx->title_label, name_gap));
        w_rr_fit_state(ctx);
    }

    /* Controls: chip on top, icon buttons at the bottom. */
    const lv_coord_t controls_y = name_y + name_line + 11 + ui_widget_name_gap(ctx->title_label, name_gap);
    lv_coord_t controls_h = inner_h - controls_y;
    if (controls_h < 96) {
        controls_h = 96;
    }
    if (ctx->controls_panel != NULL) {
        lv_obj_set_pos(ctx->controls_panel, col_x, controls_y);
        lv_obj_set_size(ctx->controls_panel, col_w, controls_h);
    }
    const lv_coord_t chip_h = 40;
    const lv_coord_t btn_h = 46;
    if (ctx->rooms_btn != NULL) {
        lv_obj_set_pos(ctx->rooms_btn, 0, 0);
        lv_obj_set_size(ctx->rooms_btn, col_w, chip_h);
    }

    const bool fan = w_rr_can_set_fan(ctx);
    const bool mode = ctx->mode_count > 0;
    if (ctx->rooms_btn != NULL) {
        w_rr_set_hidden(ctx->rooms_btn, fan && !mode);
        lv_obj_set_size(ctx->rooms_btn, fan && mode ? (col_w - 8) / 2 : col_w, chip_h);
    }
    if (ctx->fan_btn != NULL) {
        w_rr_set_hidden(ctx->fan_btn, !fan);
        lv_coord_t fan_x = mode ? (col_w + 8) / 2 : 0;
        lv_coord_t fan_w = col_w - fan_x;
        lv_obj_set_pos(ctx->fan_btn, fan_x, 0);
        lv_obj_set_size(ctx->fan_btn, fan_w, chip_h);
        lv_obj_set_state(ctx->fan_btn, LV_STATE_DISABLED, ctx->unavailable);
        w_rr_style_control(ctx->fan_btn, ctx->fan_label, UI_CONTROL_ACTION, !ctx->unavailable);
        lv_label_set_text(ctx->fan_label, ctx->fan_speed[0] != '\0' ? ctx->fan_speed : ui_i18n_get("roborock.fan_title", "Suction level"));
        lv_label_set_long_mode(ctx->fan_label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(ctx->fan_label, fan_w - 16, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
        lv_obj_center(ctx->fan_label);
    }

    /* Icon buttons: start/pause, stop (only if supported) and dock. */
    lv_obj_t *icon_btns[3] = {ctx->start_btn, ctx->stop_btn, ctx->dock_btn};
    bool show_icon[3] = {true, (ctx->features & W_RR_FEAT_STOP) != 0U, true};
    int shown = 0;
    for (int i = 0; i < 3; i++) {
        if (show_icon[i]) {
            shown++;
        }
    }
    const lv_coord_t gap = 8;
    lv_coord_t btn_w = shown > 0 ? (col_w - gap * (shown - 1)) / shown : 0;
    if (btn_w > 72) {
        btn_w = 72;
    }
    if (btn_w < 40) {
        btn_w = 40;
    }
    lv_coord_t row_w = btn_w * shown + gap * (shown > 0 ? shown - 1 : 0);
    lv_coord_t row_x = wide ? 0 : LV_MAX(0, (col_w - row_w) / 2);
    /* Wide: buttons at the bottom of the column. Tall: right under the chip,
     * so free space collects at the bottom like on Home Assistant's card. */
    lv_coord_t row_y = wide ? controls_h - btn_h : chip_h + 12;
    if (row_y < chip_h + 8) {
        row_y = chip_h + 8;
    }
    int slot = 0;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = icon_btns[i];
        if (btn == NULL) {
            continue;
        }
        if (!show_icon[i]) {
            w_rr_set_hidden(btn, true);
            continue;
        }
        w_rr_set_hidden(btn, false);
        lv_obj_set_pos(btn, row_x + slot * (btn_w + gap), row_y);
        lv_obj_set_size(btn, btn_w, btn_h);
        slot++;
    }
    const char *start_symbol = (ctx->active_clean || ctx->returning) ? APP_SYMBOL_PAUSE : APP_SYMBOL_PLAY;
    w_rr_set_label_if_changed(ctx->start_label, W_RR_ICON_FONT, start_symbol);
    w_rr_set_label_if_changed(ctx->stop_label, W_RR_ICON_FONT, APP_SYMBOL_STOP);
    w_rr_set_label_if_changed(ctx->dock_label, W_RR_ICON_FONT, APP_SYMBOL_HOME);
    w_rr_set_label_if_changed(ctx->rooms_label, ui_font_role(UI_TEXT_META), NULL);

    w_rr_update_popup_clean_button(ctx);
}

static void w_rr_layout_card(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return;
    }

    lv_obj_update_layout(ctx->card);
    lv_coord_t inner_w = lv_obj_get_content_width(ctx->card);
    lv_coord_t inner_h = lv_obj_get_content_height(ctx->card);
    if (inner_w <= 0 || inner_h <= 0) {
        return;
    }
    w_rr_layout_generic(ctx, inner_w, inner_h);
}

/* Long press on the card (not on a button): the last 24 hours of the vacuum's state, as a timeline and a list of changes. */
static void w_rr_history_long_press_cb(lv_event_t *event)
{
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || !ctx->history_popup || lv_event_get_target(event) != lv_event_get_current_target(event)) {
        return;
    }
    ui_history_popup_open(ctx->entity_id, ctx->title, ctx->state_text, false, "vacuum");
}

static void w_rr_stop_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) {
        return;
    }
    esp_err_t err = w_rr_call_vacuum_simple("stop", ctx->entity_id);
    if (err != ESP_OK) {
        w_rr_command_failed(ctx, "stop", err);
    }
}

static void w_rr_update_popup_clean_button(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->rooms_btn == NULL || ctx->rooms_label == NULL) {
        return;
    }

    const bool enabled = !ctx->unavailable && ctx->mode_count > 0;
    w_rr_style_control(ctx->rooms_btn, ctx->rooms_label, UI_CONTROL_ACTION, enabled);

    char text[64] = {0};
    char mode_text[W_RR_MODE_LEN];
    w_rr_mode_label(ctx->mode_current, mode_text, sizeof(mode_text));
    snprintf(text, sizeof(text), "%s", ctx->mode_current[0] != '\0' ? mode_text : ui_i18n_get("roborock.mode", "Mode"));
    lv_label_set_text(ctx->rooms_label, text);
    lv_label_set_long_mode(ctx->rooms_label, LV_LABEL_LONG_DOT);
    /* Dropdown look: left-aligned text and an arrow on the right. */
    const bool compact_pair = w_rr_can_set_fan(ctx) && ctx->mode_count > 0;
    lv_obj_set_width(ctx->rooms_label, lv_obj_get_width(ctx->rooms_btn) - (compact_pair ? 16 : 40));
    lv_obj_set_height(ctx->rooms_label, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    lv_obj_set_style_text_align(ctx->rooms_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_align(ctx->rooms_label, LV_ALIGN_LEFT_MID, 8, 0);
    if (ctx->mode_arrow != NULL) {
        lv_obj_align(ctx->mode_arrow, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_hidden(ctx->mode_arrow, compact_pair);
        lv_obj_set_style_text_color(ctx->mode_arrow, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    }
    lv_obj_set_style_opa(ctx->rooms_btn, enabled ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
}

static esp_err_t w_rr_call_vacuum_simple(const char *service, const char *entity_id)
{
    if (service == NULL || entity_id == NULL || entity_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char payload[160];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", entity_id);
    return ha_client_call_service("vacuum", service, payload);
}

#define W_RR_NOTICE_MS 3000

/* A command that could not be sent says so in the state line for a few seconds, then the real state returns. */
static void w_rr_command_failed(w_rr_ctx_t *ctx, const char *what, esp_err_t err)
{
    ESP_LOGW(W_RR_TAG, "%s failed (%s): %s", what, ctx->entity_id, esp_err_to_name(err));
    if (ctx->notice_until_ms == 0) strlcpy(ctx->restore_text, ctx->state_text, sizeof(ctx->restore_text));
    const char *text = err == ESP_ERR_INVALID_STATE ? ui_i18n_get("roborock.not_connected", "Home Assistant not connected")
                                                    : ui_i18n_get("roborock.send_failed", "Command not sent");
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", text);
    ctx->notice_until_ms = w_rr_now_ms() + W_RR_NOTICE_MS;
    w_rr_apply_visual(ctx);
}

/* The cleaning-mode popup is the shared option picker (ui_option_picker.c). */
static void w_rr_mode_popup_close(w_rr_ctx_t *ctx)
{
    if (ctx != NULL && ctx->mode_popup != NULL) {
        ui_option_picker_close(ctx->mode_popup); /* its closed callback clears mode_popup */
    }
}

static void w_rr_mode_picker_closed(void *user)
{
    ((w_rr_ctx_t *)user)->mode_popup = NULL;
}

static void w_rr_mode_picker_label(void *user, size_t index, char *out, size_t out_len)
{
    w_rr_mode_label(((w_rr_ctx_t *)user)->mode_options[index], out, out_len);
}

/* Ask Home Assistant to switch the mode select to option (built with cJSON so option names with
 * quotes or backslashes stay valid). */
static esp_err_t w_rr_select_mode(const w_rr_ctx_t *ctx, const char *option)
{
    cJSON *payload = cJSON_CreateObject();
    char *text = NULL;
    if (payload != NULL) {
        cJSON_AddStringToObject(payload, "entity_id", ctx->mode_entity_id);
        cJSON_AddStringToObject(payload, "option", option);
        text = cJSON_PrintUnformatted(payload);
    }
    esp_err_t err = (text != NULL) ? ha_client_call_service("select", "select_option", text) : ESP_ERR_NO_MEM;
    cJSON_free(text);
    cJSON_Delete(payload);
    return err;
}

static void w_rr_mode_picker_chosen(void *user, size_t idx)
{
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)user;
    if (ctx == NULL || ctx->unavailable || !ha_client_is_connected() || ctx->mode_entity_id[0] == '\0' || idx >= ctx->mode_count) {
        return;
    }

    esp_err_t err = w_rr_select_mode(ctx, ctx->mode_options[idx]);
    if (err != ESP_OK) {
        ESP_LOGW(W_RR_TAG, "select_option failed (%s): %s", ctx->mode_entity_id, esp_err_to_name(err));
        return;
    }

    strlcpy(ctx->mode_current, ctx->mode_options[idx], sizeof(ctx->mode_current));
    if (!ctx->unavailable) {
        char mode_text[W_RR_MODE_LEN];
        w_rr_mode_label(ctx->mode_current, mode_text, sizeof(mode_text));
        snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s: %s", ui_i18n_get("roborock.mode", "Mode"), mode_text);
    }
    w_rr_apply_visual(ctx);
}

/* The cleaning modes of the configured select entity; a tap on one calls select.select_option. */
static void w_rr_show_mode_popup(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->mode_popup != NULL || ctx->mode_count == 0) {
        return;
    }
    size_t current = UI_OPTION_PICKER_NONE;
    for (size_t i = 0; i < ctx->mode_count; i++) {
        if (strcmp(ctx->mode_options[i], ctx->mode_current) == 0) {
            current = i;
        }
    }
    const ui_option_picker_cfg_t cfg = {
        .title = ui_i18n_get("roborock.mode_title", "Cleaning mode"),
        .count = ctx->mode_count,
        .current = current,
        .label = w_rr_mode_picker_label,
        .chosen = w_rr_mode_picker_chosen,
        .closed = w_rr_mode_picker_closed,
        .user = ctx,
    };
    ctx->mode_popup = ui_option_picker_open(&cfg);
}

static void w_rr_popup_clean_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) {
        return;
    }
    w_rr_show_mode_popup(ctx);
}

static void w_rr_start_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) {
        return;
    }

    bool should_pause = ctx->active_clean || ctx->returning;
    if (!should_pause && !ctx->paused && ctx->default_mode[0] != '\0' && ctx->mode_entity_id[0] != '\0' &&
        strcmp(ctx->mode_current, ctx->default_mode) != 0) {
        /* The configured default cleaning program applies to a fresh start, not to resuming a pause. */
        if (w_rr_select_mode(ctx, ctx->default_mode) == ESP_OK) {
            strlcpy(ctx->mode_current, ctx->default_mode, sizeof(ctx->mode_current));
        }
    }
    esp_err_t err = w_rr_call_vacuum_simple(should_pause ? "pause" : "start", ctx->entity_id);
    if (err != ESP_OK) {
        w_rr_command_failed(ctx, should_pause ? "pause" : "start", err);
        return;
    }
    ctx->notice_until_ms = 0;

    if (should_pause) {
        ctx->active_clean = false;
        ctx->paused = true;
        ctx->returning = false;
        snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("common.paused", "Paused"));
    } else {
        ctx->active_clean = true;
        ctx->paused = false;
        ctx->returning = false;
        ctx->charging = false;
        snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("roborock.cleaning", "Cleaning"));
    }
    w_rr_apply_visual(ctx);
}

static void w_rr_dock_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) {
        return;
    }
    const esp_err_t err = w_rr_call_vacuum_simple("return_to_base", ctx->entity_id);
    if (err != ESP_OK) {
        w_rr_command_failed(ctx, "return_to_base", err);
        return;
    }
    ctx->notice_until_ms = 0;
    ctx->active_clean = false;
    ctx->paused = false;
    ctx->returning = true;
    ctx->charging = false;
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("roborock.returning", "Returning"));
    w_rr_apply_visual(ctx);
}

static lv_obj_t *w_rr_make_small_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, w_rr_ctx_t *ctx, lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_center(label);
    if (out_label != NULL) {
        *out_label = label;
    }
    return btn;
}

static void w_rr_apply_visual(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return;
    }

    lv_color_t active_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_CARD_BG_ON), 100);
    lv_color_t paused_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), lv_color_hex(APP_UI_COLOR_CARD_BG_ON), 90);
    lv_color_t returning_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 118);
    lv_color_t card_bg = lv_color_hex(APP_UI_COLOR_CARD_BG_OFF);
    lv_color_t border = lv_color_hex(APP_UI_COLOR_CARD_BORDER);

    if (ctx->unavailable) {
        card_bg = lv_color_hex(APP_UI_COLOR_CARD_BG_OFF);
    } else if (ctx->returning) {
        card_bg = returning_bg;
        border = lv_color_mix(returning_bg, border, 156);
    } else if (ctx->active_clean) {
        card_bg = active_bg;
        border = lv_color_mix(active_bg, border, 150);
    } else if (ctx->paused) {
        card_bg = paused_bg;
        border = lv_color_mix(paused_bg, border, 130);
    }

    lv_obj_set_style_bg_color(ctx->card, card_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->card, border, LV_PART_MAIN);
    ui_style_card_set_active(ctx->card, !ctx->unavailable && (ctx->returning || ctx->active_clean || ctx->paused));

    lv_color_t text_primary = lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED : APP_UI_COLOR_TEXT_PRIMARY);
    lv_color_t text_muted = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    if (ctx->title_label != NULL) {
        lv_obj_set_style_text_color(ctx->title_label, text_muted, LV_PART_MAIN);
    }
    if (ctx->state_label != NULL) {
        lv_obj_set_style_text_color(ctx->state_label, text_primary, LV_PART_MAIN);
    }
    if (ctx->detail_label != NULL) {
        lv_obj_set_style_text_color(ctx->detail_label, text_muted, LV_PART_MAIN);
    }

    if (ctx->state_label != NULL) {
        lv_label_set_text(ctx->state_label, ctx->state_text);
        w_rr_fit_state(ctx);
    }
    if (ctx->detail_label != NULL) {
        lv_label_set_text(ctx->detail_label, ctx->detail_text);
    }

    if (ctx->battery_chip != NULL && ctx->battery_label != NULL) {
        lv_color_t chip_bg = w_rr_surface_fill(216);
        if (!ctx->unavailable && ctx->battery_pct >= 0 && ctx->battery_pct < 20) {
            chip_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_ERROR), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 84);
        } else if (!ctx->unavailable && (ctx->active_clean || ctx->charging)) {
            chip_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_OK), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 84);
        }
        lv_color_t chip_text = w_rr_contrast_text(chip_bg);
        lv_obj_set_style_bg_color(ctx->battery_chip, chip_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->battery_chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ctx->battery_chip, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(ctx->battery_chip, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->battery_chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_left(ctx->battery_chip, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(ctx->battery_chip, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_top(ctx->battery_chip, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ctx->battery_chip, 6, LV_PART_MAIN);
        lv_obj_set_style_text_color(ctx->battery_label, chip_text, LV_PART_MAIN);

        char battery_text[16];
        if (ctx->battery_pct >= 0) {
            snprintf(battery_text, sizeof(battery_text), "%d%%", ctx->battery_pct);
        } else {
            snprintf(battery_text, sizeof(battery_text), "--");
        }
        lv_label_set_text(ctx->battery_label, battery_text);
    }

    /* The primary control pauses both cleaning and return movement. */
    const bool running = ctx->active_clean || ctx->returning;
    w_rr_style_control(ctx->start_btn, ctx->start_label, running ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, !ctx->unavailable);
    w_rr_style_control(ctx->dock_btn, ctx->dock_label, UI_CONTROL_ACTION, !ctx->unavailable);
    w_rr_layout_card(ctx);

    if (ctx->popup_map_panel != NULL) {
        w_rr_style_stop_button(ctx);
        if (w_rr_has_map_entity(ctx) && ctx->popup_map_request_pending && w_rr_is_visible(ctx)) {
            w_rr_request_popup_map(ctx);
        }
    }
}

static void w_rr_apply_primary_state(w_rr_ctx_t *ctx, const ha_state_t *state)
{
    if (ctx == NULL || state == NULL) {
        return;
    }

    ctx->unavailable = w_rr_state_is_unavailable(state->state);
    ctx->active_clean = !ctx->unavailable && w_rr_state_is_active_clean(state->state) && strcmp(state->state, "paused") != 0;
    ctx->paused = !ctx->unavailable && strcmp(state->state, "paused") == 0;
    ctx->error = !ctx->unavailable && strcmp(state->state, "error") == 0;
    ctx->returning = !ctx->unavailable && w_rr_state_is_returning(state->state);
    ctx->charging = !ctx->unavailable && (strcmp(state->state, "charging") == 0 || strcmp(state->state, "docked") == 0);
    ctx->battery_pct = -1;
    ctx->fan_speed[0] = '\0';
    if (ctx->fan_popup != NULL) ui_option_picker_close(ctx->fan_popup);
    ha_control_options_parse(state->attributes_json, "fan_speed_list", "fan_speed", &ctx->fan_options);
    ctx->detail_text[0] = '\0';
    ctx->notice_until_ms = 0; /* a new state replaces a failed command's notice */

    if (ctx->charging && (ctx->robot_position.valid || ctx->robot_trail_count > 0 || ctx->position_failed)) {
        ctx->robot_position = (w_rr_position_t){0};
        ctx->robot_trail_count = 0;
        ctx->position_failed = false;
        w_rr_render_map_overlay(ctx);
    }

    if (ctx->unavailable) {
        w_rr_mode_popup_close(ctx);
        snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("common.unavailable", "Unavailable"));
        w_rr_apply_visual(ctx);
        return;
    }

    w_rr_humanize_state(state->state, ctx->state_text, sizeof(ctx->state_text));

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (cJSON_IsObject(attrs)) {
        cJSON *features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
        if (cJSON_IsNumber(features) && features->valuedouble >= 0) {
            ctx->features = (uint32_t)features->valuedouble;
            ctx->features_known = true;
        }

        /* Home Assistant 2026.9 dropped battery_level from the vacuum entity: only the sensor reports the charge. */
        ctx->battery_pct = ctx->battery_sensor_pct;

        cJSON *fan_speed = cJSON_GetObjectItemCaseSensitive(attrs, "fan_speed");
        if (cJSON_IsString(fan_speed) && fan_speed->valuestring != NULL && fan_speed->valuestring[0] != '\0') {
            snprintf(ctx->fan_speed, sizeof(ctx->fan_speed), "%s", fan_speed->valuestring);
        } else if (cJSON_IsNumber(fan_speed)) {
            snprintf(ctx->fan_speed, sizeof(ctx->fan_speed), "%d", fan_speed->valueint);
        }
    }
    cJSON_Delete(attrs);

    if (ctx->fan_speed[0] != '\0') {
        snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s: %s",
            ui_i18n_get("roborock.fan", "Fan"), ctx->fan_speed);
    } else if (ctx->map_entity_id[0] != '\0' && ctx->map_url[0] != '\0') {
        snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s", ui_i18n_get("roborock.map_ready", "Map connected"));
    } else if (ctx->charging) {
        snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s", ui_i18n_get("roborock.ready_to_charge", "On dock"));
    }
    if (ctx->mode_current[0] != '\0') {
        {
            char mode_text[W_RR_MODE_LEN];
            w_rr_mode_label(ctx->mode_current, mode_text, sizeof(mode_text));
            snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s: %s", ui_i18n_get("roborock.mode", "Mode"), mode_text);
        }
    }

    w_rr_apply_visual(ctx);
}

/* State of the optional cleaning mode select: current option plus its list. */
static void w_rr_apply_mode_state(w_rr_ctx_t *ctx, const ha_state_t *state)
{
    if (ctx == NULL || state == NULL) {
        return;
    }
    /* strlcpy: the state can be longer than an option name we keep (truncated). */
    strlcpy(ctx->mode_current, w_rr_state_has_value(state->state) ? state->state : "", sizeof(ctx->mode_current));

    w_rr_mode_popup_close(ctx);
    ctx->mode_count = 0;
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (cJSON_IsObject(attrs)) {
        cJSON *options = cJSON_GetObjectItemCaseSensitive(attrs, "options");
        cJSON *option = NULL;
        if (cJSON_IsArray(options)) {
            cJSON_ArrayForEach(option, options) {
                if (ctx->mode_count >= W_RR_MAX_MODES) {
                    break;
                }
                if (cJSON_IsString(option) && option->valuestring != NULL && option->valuestring[0] != '\0') {
                    snprintf(ctx->mode_options[ctx->mode_count], W_RR_MODE_LEN, "%s", option->valuestring);
                    ctx->mode_count++;
                }
            }
        }
    }
    cJSON_Delete(attrs);

    if (!ctx->unavailable && ctx->mode_current[0] != '\0') {
        {
            char mode_text[W_RR_MODE_LEN];
            w_rr_mode_label(ctx->mode_current, mode_text, sizeof(mode_text));
            snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s: %s", ui_i18n_get("roborock.mode", "Mode"), mode_text);
        }
    }
    w_rr_apply_visual(ctx);
}

static void w_rr_apply_battery_state(w_rr_ctx_t *ctx, const ha_state_t *state)
{
    char *end = NULL;
    const double value = strtod(state->state, &end);
    if (end == state->state || *end != '\0' || value < 0.0 || value > 100.0) {
        ctx->battery_sensor_pct = -1; /* unknown/unavailable: no charge chip */
    } else {
        ctx->battery_sensor_pct = (int)(value + 0.5);
        ctx->battery_pct = ctx->battery_sensor_pct;
    }
    w_rr_apply_visual(ctx);
}

static void w_rr_apply_secondary_state(w_rr_ctx_t *ctx, const ha_state_t *state)
{
    if (ctx == NULL || state == NULL) {
        return;
    }

    if (w_rr_state_has_value(state->state)) {
        snprintf(ctx->map_state_value, sizeof(ctx->map_state_value), "%s", state->state);
    }

    char next_url[W_RR_MAP_URL_LEN] = {0};
    char access_token[APP_HA_ACCESS_TOKEN_MAX_LEN] = {0};
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (cJSON_IsObject(attrs)) {
        cJSON *entity_picture = cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture");
        if (cJSON_IsString(entity_picture) && entity_picture->valuestring != NULL && entity_picture->valuestring[0] != '\0') {
            snprintf(next_url, sizeof(next_url), "%s", entity_picture->valuestring);
        }

        if (next_url[0] == '\0') {
            cJSON *entity_picture_local = cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture_local");
            if (cJSON_IsString(entity_picture_local) && entity_picture_local->valuestring != NULL &&
                entity_picture_local->valuestring[0] != '\0') {
                snprintf(next_url, sizeof(next_url), "%s", entity_picture_local->valuestring);
            }
        }

        cJSON *token = cJSON_GetObjectItemCaseSensitive(attrs, "access_token");
        if (cJSON_IsString(token) && token->valuestring != NULL && token->valuestring[0] != '\0') {
            snprintf(access_token, sizeof(access_token), "%s", token->valuestring);
        }

        w_rr_merge_map_attr_metadata(ctx, attrs);
    }
    cJSON_Delete(attrs);

    if (next_url[0] == '\0') {
        const char *map_state_for_url =
            w_rr_state_has_value(ctx->map_state_value) ? ctx->map_state_value : state->state;
        if (w_rr_build_image_proxy_url(ctx->map_entity_id, access_token, map_state_for_url, next_url, sizeof(next_url))) {
            if (strncmp(ctx->map_url, next_url, sizeof(ctx->map_url)) != 0) {
                ESP_LOGI(W_RR_TAG, "Built fallback image_proxy URL for %s (token=%s, state=%s)",
                    ctx->map_entity_id,
                    access_token[0] != '\0' ? "yes" : "no",
                    w_rr_state_has_value(map_state_for_url) ? "yes" : "no");
            }
        } else if (ctx->map_entity_id[0] != '\0') {
            ESP_LOGW(W_RR_TAG, "No usable map URL yet for %s (state=%s, token=%s)",
                ctx->map_entity_id,
                w_rr_state_has_value(map_state_for_url) ? map_state_for_url : state->state,
                access_token[0] != '\0' ? "yes" : "no");
        }
    }

    if (strncmp(ctx->map_url, next_url, sizeof(ctx->map_url)) != 0) {
        snprintf(ctx->map_url, sizeof(ctx->map_url), "%s", next_url);
        ctx->popup_map_request_pending = true;
        ctx->popup_map_failed = false;
        if (ctx->popup_map_panel != NULL && w_rr_is_visible(ctx)) {
            w_rr_request_popup_map(ctx);
        }
    }

    if (ctx->detail_text[0] == '\0' && ctx->map_url[0] != '\0' && !ctx->unavailable) {
        snprintf(ctx->detail_text, sizeof(ctx->detail_text), "%s", ui_i18n_get("roborock.map_ready", "Map connected"));
        w_rr_apply_visual(ctx);
    }
}

static void w_rr_tick_cb(lv_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx->destroyed) {
        return;
    }

    if (!w_rr_is_visible(ctx)) {
        return;
    }

    w_rr_drain_pending_position(ctx);

    int64_t now = w_rr_now_ms();
    if (ctx->notice_until_ms != 0 && now >= ctx->notice_until_ms) {
        ctx->notice_until_ms = 0;
        strlcpy(ctx->state_text, ctx->restore_text, sizeof(ctx->state_text));
        w_rr_apply_visual(ctx);
    }
    if (w_rr_has_map_entity(ctx) && ctx->popup_map_panel != NULL && ctx->popup_map_request_pending &&
        !ctx->popup_map_request_inflight) {
        w_rr_request_popup_map(ctx);
    }

    bool live_position_needed = ctx->active_clean || ctx->returning || ctx->paused;
    bool map_ready_for_position = live_position_needed &&
        ctx->popup_map_dsc.data != NULL && ctx->calibration_count >= 3;
    int64_t position_refresh_ms = ctx->position_failed
        ? W_RR_POSITION_RETRY_MS
        : ((ctx->active_clean || ctx->returning || ctx->paused)
            ? W_RR_POSITION_REFRESH_ACTIVE_MS
            : W_RR_POSITION_REFRESH_IDLE_MS);
    if (map_ready_for_position && !ctx->position_fetching &&
        (ctx->last_position_request_ms == 0 ||
         (now - ctx->last_position_request_ms) >= position_refresh_ms)) {
        w_rr_request_position(ctx);
    }
}

static void w_rr_card_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SIZE_CHANGED) {
        w_rr_layout_card(ctx);
        return;
    }

    if (code != LV_EVENT_DELETE) {
        return;
    }

    ha_client_cancel_pending_responses_for_user(ctx);
    ha_cover_fetcher_cancel(ctx);
    w_rr_mode_popup_close(ctx);
    if (ctx->fan_popup != NULL) ui_option_picker_close(ctx->fan_popup);
    if (ctx->tick_timer != NULL) {
        lv_timer_del(ctx->tick_timer);
        ctx->tick_timer = NULL;
    }
    if (ctx->staging_mutex != NULL) {
        xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
        ctx->destroyed = true;
        xSemaphoreGive(ctx->staging_mutex);
        vSemaphoreDelete(ctx->staging_mutex);
        ctx->staging_mutex = NULL;
    }
    w_rr_release_popup_map(ctx);
    free(ctx);
}

esp_err_t w_vacuum_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    theme_default_style_card(card);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    w_rr_ctx_t *ctx = w_rr_calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    snprintf(ctx->map_entity_id, sizeof(ctx->map_entity_id), "%s", def->secondary_entity_id);
    snprintf(ctx->mode_entity_id, sizeof(ctx->mode_entity_id), "%s", def->tertiary_entity_id);
    snprintf(ctx->default_mode, sizeof(ctx->default_mode), "%s", def->default_mode);
    snprintf(ctx->battery_entity_id, sizeof(ctx->battery_entity_id), "%s", def->extra_entity_id[UI_VACUUM_EXTRA_BATTERY]);
    ctx->battery_sensor_pct = -1;
    ctx->history_popup = def->history_popup;
    strlcpy(ctx->title, def->title[0] != '\0' ? def->title : def->entity_id, sizeof(ctx->title));
    ctx->battery_pct = -1;
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("roborock.ready", "Ready"));

    ctx->staging_mutex = xSemaphoreCreateMutex();
    if (ctx->staging_mutex == NULL) {
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    ctx->card = card;

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(title, def->title[0] != '\0' ? def->title : def->entity_id);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, def->w - 120);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    ctx->title_label = title;

    lv_obj_t *battery_chip = lv_obj_create(card);
    lv_obj_set_size(battery_chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(battery_chip, false);
    lv_obj_align(battery_chip, LV_ALIGN_TOP_RIGHT, 0, 0);
    ctx->battery_chip = battery_chip;

    lv_obj_t *battery_label = lv_label_create(battery_chip);
    lv_label_set_text(battery_label, "--");
    lv_obj_set_style_text_font(battery_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_center(battery_label);
    ctx->battery_label = battery_label;

    lv_obj_t *state_label = lv_label_create(card);
    lv_label_set_text(state_label, "");
    lv_obj_set_style_text_font(state_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state_label, def->w - 28);
    lv_obj_align(state_label, LV_ALIGN_TOP_LEFT, 0, 40);
    ctx->state_label = state_label;

    lv_obj_t *detail_label = lv_label_create(card);
    lv_label_set_text(detail_label, "");
    lv_obj_set_style_text_font(detail_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(detail_label, def->w - 28);
    lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 0, 82);
    ctx->detail_label = detail_label;

    ctx->popup_map_panel = lv_obj_create(card);
    lv_obj_set_scrollable(ctx->popup_map_panel, false);
    lv_obj_set_style_radius(ctx->popup_map_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(ctx->popup_map_panel, true, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->popup_map_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->popup_map_panel, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->popup_map_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->popup_map_panel, w_rr_surface_fill(190), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_panel, LV_OPA_COVER, LV_PART_MAIN);

    ctx->popup_map_img = lv_image_create(ctx->popup_map_panel);
    lv_image_set_inner_align(ctx->popup_map_img, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_pivot(ctx->popup_map_img, 0, 0);
    lv_obj_set_pos(ctx->popup_map_img, 0, 0);

    ctx->popup_map_overlay = lv_obj_create(ctx->popup_map_panel);
    lv_obj_set_scrollable(ctx->popup_map_overlay, false);
    lv_obj_set_style_bg_opa(ctx->popup_map_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->popup_map_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->popup_map_overlay, 0, LV_PART_MAIN);
    lv_obj_set_hidden(ctx->popup_map_overlay, true);

    ctx->popup_map_placeholder = lv_label_create(ctx->popup_map_panel);
    lv_obj_set_width(ctx->popup_map_placeholder, LV_PCT(100));
    lv_obj_set_style_text_font(ctx->popup_map_placeholder, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->popup_map_placeholder, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_map_placeholder, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_center(ctx->popup_map_placeholder);

    /* Above the text while a fetch is in flight (w_rr_popup_show_map_placeholder); hidden the rest of the time. */
    ctx->popup_map_spinner = lv_spinner_create(ctx->popup_map_panel);
    lv_obj_set_size(ctx->popup_map_spinner, 28, 28);
    lv_obj_set_style_arc_width(ctx->popup_map_spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ctx->popup_map_spinner, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ctx->popup_map_spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ctx->popup_map_spinner, lv_color_hex(APP_UI_COLOR_STATE_ON), LV_PART_INDICATOR);
    lv_obj_align(ctx->popup_map_spinner, LV_ALIGN_CENTER, 0, -26);
    lv_obj_set_hidden(ctx->popup_map_spinner, true);

    ctx->popup_map_name_label = lv_label_create(ctx->popup_map_panel);
    lv_obj_set_style_text_font(ctx->popup_map_name_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_map_name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->popup_map_name_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_name_label, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->popup_map_name_label, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ctx->popup_map_name_label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ctx->popup_map_name_label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ctx->popup_map_name_label, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ctx->popup_map_name_label, 4, LV_PART_MAIN);
    lv_obj_align(ctx->popup_map_name_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_hidden(ctx->popup_map_name_label, true);

    ctx->controls_panel = lv_obj_create(card);
    lv_obj_set_scrollable(ctx->controls_panel, false);
    lv_obj_set_style_bg_opa(ctx->controls_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->controls_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->controls_panel, 0, LV_PART_MAIN);

    /* The Stop icon button; there is no Locate button. */
    ctx->stop_btn = w_rr_make_small_button(ctx->controls_panel, APP_SYMBOL_STOP, w_rr_stop_event_cb, ctx, &ctx->stop_label);

    ctx->start_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("common.start", "Start"), w_rr_start_event_cb, ctx, &ctx->start_label);
    ctx->dock_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("roborock.dock", "Dock"), w_rr_dock_event_cb, ctx, &ctx->dock_label);
    ctx->fan_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("roborock.fan_title", "Suction level"), w_rr_fan_event_cb, ctx, &ctx->fan_label);
    lv_obj_set_hidden(ctx->fan_btn, true);
    ctx->rooms_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("roborock.clean_selected_short", "Clean"), w_rr_popup_clean_event_cb, ctx, &ctx->rooms_label);

    /* Dropdown arrow of the mode chip (generic vacuum card only). */
    ctx->mode_arrow = lv_label_create(ctx->rooms_btn);
    lv_label_set_text(ctx->mode_arrow, APP_SYMBOL_CHEVRON_DOWN);
    lv_obj_set_style_text_font(ctx->mode_arrow, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_align(ctx->mode_arrow, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_hidden(ctx->mode_arrow, true);

    /* Line-art robot of the generic vacuum card: a disc, an inner ring, the
     * lidar turret and a dust-bin slot. Hidden until the vacuum is known to be
     * a generic one. */
    ctx->art = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->art);
    lv_obj_set_clickable(ctx->art, false);
    lv_obj_set_scrollable(ctx->art, false);
    lv_obj_t **art_parts[4] = {&ctx->art_disc, &ctx->art_inner, &ctx->art_lidar, &ctx->art_slot};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *part = lv_obj_create(ctx->art);
        lv_obj_remove_style_all(part);
        lv_obj_set_clickable(part, false);
        lv_obj_set_scrollable(part, false);
        lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_opa(part, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(part, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(part, 2, LV_PART_MAIN);
        *art_parts[i] = part;
    }
    lv_obj_set_hidden(ctx->art, true);

    lv_obj_add_event_cb(card, w_rr_card_event_cb, LV_EVENT_DELETE, ctx);
    if (def->history_popup) {
        lv_obj_add_event_cb(card, w_rr_history_long_press_cb, LV_EVENT_LONG_PRESSED, ctx);
    }
    lv_obj_add_event_cb(card, w_rr_card_event_cb, LV_EVENT_SIZE_CHANGED, ctx);

    ctx->tick_timer = lv_timer_create(w_rr_tick_cb, W_RR_TICK_MS, ctx);
    if (ctx->tick_timer == NULL) {
        vSemaphoreDelete(ctx->staging_mutex);
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    lv_timer_pause(ctx->tick_timer);

    if (!w_rr_has_map_entity(ctx) && ctx->popup_map_panel != NULL) {
        lv_obj_set_hidden(ctx->popup_map_panel, true);
    }
    ctx->popup_map_request_pending = w_rr_has_map_entity(ctx);
    w_rr_style_stop_button(ctx);
    w_rr_layout_card(ctx);
    w_rr_apply_visual(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_vacuum_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || state == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }
    if (strncmp(state->entity_id, ctx->entity_id, sizeof(ctx->entity_id)) == 0) {
        w_rr_apply_primary_state(ctx, state);
    } else if (ctx->map_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->map_entity_id, sizeof(ctx->map_entity_id)) == 0) {
        w_rr_apply_secondary_state(ctx, state);
    } else if (ctx->mode_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->mode_entity_id, sizeof(ctx->mode_entity_id)) == 0) {
        w_rr_apply_mode_state(ctx, state);
    } else if (ctx->battery_entity_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->battery_entity_id, sizeof(ctx->battery_entity_id)) == 0) {
        w_rr_apply_battery_state(ctx, state);
    }
}

void w_vacuum_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    w_rr_mode_popup_close(ctx);
    if (ctx->fan_popup != NULL) ui_option_picker_close(ctx->fan_popup);
    ctx->active_clean = false;
    ctx->paused = false;
    ctx->returning = false;
    ctx->charging = false;
    ctx->battery_pct = -1;
    ctx->detail_text[0] = '\0';
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("common.unavailable", "Unavailable"));
    w_rr_apply_visual(ctx);
}

void w_vacuum_set_visible(ui_widget_instance_t *instance, bool visible)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)instance->ctx;
    if (ctx->destroyed) {
        return;
    }

    ctx->visible = visible;
    if (!visible) {
        w_rr_mode_popup_close(ctx);
        if (ctx->fan_popup != NULL) ui_option_picker_close(ctx->fan_popup);
        if (ctx->popup_map_request_inflight) {
            ha_cover_fetcher_cancel(ctx);
            ctx->popup_map_request_inflight = false;
            ctx->popup_map_request_pending = w_rr_has_map_entity(ctx);
        }
        if (ctx->tick_timer != NULL) {
            lv_timer_pause(ctx->tick_timer);
        }
        return;
    }

    if (ctx->tick_timer != NULL) {
        lv_timer_resume(ctx->tick_timer);
    }

    w_rr_drain_pending_position(ctx);
    if (w_rr_has_map_entity(ctx) && ctx->popup_map_panel != NULL && ctx->popup_map_request_pending &&
        !ctx->popup_map_request_inflight) {
        w_rr_request_popup_map(ctx);
    }
    w_rr_apply_visual(ctx);
}
