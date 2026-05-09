/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Roborock widget. Shows status + battery for a vacuum.* entity and
 * exposes start/pause, return-to-base and room cleaning via roborock.get_maps.
 *
 * An optional secondary_entity_id may point at an image.* entity to surface a
 * live map preview directly inside the tile.
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
#include "ui/fonts/app_text_fonts.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_i18n.h"

#define W_RR_TAG "w_roborock"
#define W_RR_MAX_ROOMS 32
#define W_RR_ROOM_ID_LEN 24
#define W_RR_ROOM_NAME_LEN 72
#define W_RR_MAP_URL_LEN 512
#define W_RR_MAX_CAL_POINTS 4
#define W_RR_TICK_MS 1000
#define W_RR_ROOM_REFRESH_MS 120000
#define W_RR_ROOM_RETRY_MS 10000
#define W_RR_MAP_REFRESH_ACTIVE_MS 5000
#define W_RR_MAP_REFRESH_IDLE_MS 30000
#define W_RR_MAP_REFRESH_HIDDEN_MS 0
#define W_RR_MAP_RETRY_MS 12000
#define W_RR_POSITION_TRAIL_POINTS 8
#define W_RR_POSITION_REFRESH_ACTIVE_MS 4000
#define W_RR_POSITION_REFRESH_IDLE_MS 30000
#define W_RR_POSITION_RETRY_MS 12000

typedef struct {
    char segment_id[W_RR_ROOM_ID_LEN];
    char name[W_RR_ROOM_NAME_LEN];
    bool selected;
    bool has_bounds;
    double vacuum_x0;
    double vacuum_y0;
    double vacuum_x1;
    double vacuum_y1;
} w_rr_room_t;

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

    lv_obj_t *popup_status_label;
    lv_obj_t *popup_map_panel;
    lv_obj_t *popup_map_img;
    lv_obj_t *popup_map_overlay;
    lv_obj_t *popup_map_name_label;
    lv_obj_t *popup_map_placeholder;
    lv_obj_t *popup_selection_label;
    lv_obj_t *popup_repeat_btns[3];
    lv_obj_t *popup_repeat_labels[3];

    lv_timer_t *tick_timer;
    SemaphoreHandle_t staging_mutex;

    w_rr_room_t rooms[W_RR_MAX_ROOMS];
    size_t room_count;
    char active_map_name[APP_MAX_NAME_LEN];

    w_rr_room_t pending_rooms[W_RR_MAX_ROOMS];
    size_t pending_room_count;
    char pending_active_map_name[APP_MAX_NAME_LEN];
    bool pending_ready;
    bool pending_failure;

    bool unavailable;
    bool active_clean;
    bool paused;
    bool returning;
    bool charging;
    int battery_pct;
    int repeat_count;
    int64_t last_room_fetch_ms;
    bool rooms_fetching;
    bool room_load_failed;
    bool popup_room_request_pending;
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
    char detail_text[64];
    char fan_speed[32];
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

    bool visible;
    bool destroyed;
} w_rr_ctx_t;

static int64_t w_rr_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

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

static bool w_rr_is_numeric_text(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
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

static lv_color_t w_rr_pressed_fill(lv_color_t color)
{
    return lv_color_brightness(color) > 127 ? lv_color_darken(color, 26) : lv_color_lighten(color, 26);
}

static lv_color_t w_rr_contrast_text(lv_color_t bg)
{
    return lv_color_brightness(bg) > 150 ? lv_color_hex(0x101418) : lv_color_hex(0xFFFFFF);
}

static size_t w_rr_selected_room_count(const w_rr_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    size_t selected = 0;
    for (size_t i = 0; i < ctx->room_count; i++) {
        if (ctx->rooms[i].selected) {
            selected++;
        }
    }
    return selected;
}

static int w_rr_room_cmp(const void *lhs, const void *rhs)
{
    const w_rr_room_t *a = (const w_rr_room_t *)lhs;
    const w_rr_room_t *b = (const w_rr_room_t *)rhs;
    return strcmp(a->name, b->name);
}

static bool w_rr_room_name_is_placeholder(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return true;
    }
    return strcmp(name, "Unknown") == 0 || strcmp(name, "unknown") == 0;
}

static int w_rr_find_room_index(const w_rr_ctx_t *ctx, const char *segment_id)
{
    if (ctx == NULL || segment_id == NULL || segment_id[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < ctx->room_count; i++) {
        if (strcmp(ctx->rooms[i].segment_id, segment_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void w_rr_copy_room_runtime(w_rr_room_t *dst, const w_rr_room_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->selected = src->selected;
    dst->has_bounds = src->has_bounds;
    dst->vacuum_x0 = src->vacuum_x0;
    dst->vacuum_y0 = src->vacuum_y0;
    dst->vacuum_x1 = src->vacuum_x1;
    dst->vacuum_y1 = src->vacuum_y1;
    if (dst->name[0] == '\0' && src->name[0] != '\0') {
        snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    }
}

static void w_rr_copy_room_bounds_only(w_rr_room_t *dst, const w_rr_room_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    dst->has_bounds = src->has_bounds;
    dst->vacuum_x0 = src->vacuum_x0;
    dst->vacuum_y0 = src->vacuum_y0;
    dst->vacuum_x1 = src->vacuum_x1;
    dst->vacuum_y1 = src->vacuum_y1;
    if (w_rr_room_name_is_placeholder(dst->name) && !w_rr_room_name_is_placeholder(src->name)) {
        snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    }
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

static bool w_rr_parse_room_bounds(cJSON *room_obj, w_rr_room_t *out)
{
    if (!cJSON_IsObject(room_obj) || out == NULL) {
        return false;
    }

    cJSON *x0 = cJSON_GetObjectItemCaseSensitive(room_obj, "x0");
    cJSON *y0 = cJSON_GetObjectItemCaseSensitive(room_obj, "y0");
    cJSON *x1 = cJSON_GetObjectItemCaseSensitive(room_obj, "x1");
    cJSON *y1 = cJSON_GetObjectItemCaseSensitive(room_obj, "y1");
    if (!cJSON_IsNumber(x0) || !cJSON_IsNumber(y0) || !cJSON_IsNumber(x1) || !cJSON_IsNumber(y1)) {
        return false;
    }

    out->vacuum_x0 = x0->valuedouble;
    out->vacuum_y0 = y0->valuedouble;
    out->vacuum_x1 = x1->valuedouble;
    out->vacuum_y1 = y1->valuedouble;
    if (out->vacuum_x0 > out->vacuum_x1) {
        double tmp = out->vacuum_x0;
        out->vacuum_x0 = out->vacuum_x1;
        out->vacuum_x1 = tmp;
    }
    if (out->vacuum_y0 > out->vacuum_y1) {
        double tmp = out->vacuum_y0;
        out->vacuum_y0 = out->vacuum_y1;
        out->vacuum_y1 = tmp;
    }
    out->has_bounds = true;

    const char *best_name = NULL;
    cJSON *name = cJSON_GetObjectItemCaseSensitive(room_obj, "name");
    if (cJSON_IsString(name) && name->valuestring != NULL && name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(name->valuestring)) {
        best_name = name->valuestring;
    }

    cJSON *iot_name = cJSON_GetObjectItemCaseSensitive(room_obj, "iot_name");
    if (best_name == NULL &&
        cJSON_IsString(iot_name) && iot_name->valuestring != NULL && iot_name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(iot_name->valuestring)) {
        best_name = iot_name->valuestring;
    }

    cJSON *room_name = cJSON_GetObjectItemCaseSensitive(room_obj, "room_name");
    if (best_name == NULL &&
        cJSON_IsString(room_name) && room_name->valuestring != NULL && room_name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(room_name->valuestring)) {
        best_name = room_name->valuestring;
    }

    if (best_name == NULL &&
        cJSON_IsString(name) && name->valuestring != NULL && name->valuestring[0] != '\0') {
        best_name = name->valuestring;
    }
    if (best_name == NULL &&
        cJSON_IsString(iot_name) && iot_name->valuestring != NULL && iot_name->valuestring[0] != '\0') {
        best_name = iot_name->valuestring;
    }
    if (best_name == NULL &&
        cJSON_IsString(room_name) && room_name->valuestring != NULL && room_name->valuestring[0] != '\0') {
        best_name = room_name->valuestring;
    }

    if (best_name != NULL) {
        snprintf(out->name, sizeof(out->name), "%s", best_name);
    }
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

static bool w_rr_room_flag_selected(cJSON *flag)
{
    if (cJSON_IsTrue(flag)) {
        return true;
    }
    if (cJSON_IsNumber(flag)) {
        return flag->valuedouble != 0.0;
    }
    if (cJSON_IsString(flag) && flag->valuestring != NULL) {
        return strcmp(flag->valuestring, "0") != 0 && strcmp(flag->valuestring, "false") != 0;
    }
    return false;
}

static const char *w_rr_room_name_from_json(cJSON *room)
{
    if (cJSON_IsString(room) && room->valuestring != NULL && room->valuestring[0] != '\0') {
        return room->valuestring;
    }
    if (!cJSON_IsObject(room)) {
        return NULL;
    }

    cJSON *name = cJSON_GetObjectItemCaseSensitive(room, "name");
    if (cJSON_IsString(name) && name->valuestring != NULL && name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(name->valuestring)) {
        return name->valuestring;
    }

    cJSON *iot_name = cJSON_GetObjectItemCaseSensitive(room, "iot_name");
    if (cJSON_IsString(iot_name) && iot_name->valuestring != NULL && iot_name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(iot_name->valuestring)) {
        return iot_name->valuestring;
    }

    cJSON *room_name = cJSON_GetObjectItemCaseSensitive(room, "room_name");
    if (cJSON_IsString(room_name) && room_name->valuestring != NULL && room_name->valuestring[0] != '\0' &&
        !w_rr_room_name_is_placeholder(room_name->valuestring)) {
        return room_name->valuestring;
    }

    if (cJSON_IsString(name) && name->valuestring != NULL && name->valuestring[0] != '\0') {
        return name->valuestring;
    }
    if (cJSON_IsString(iot_name) && iot_name->valuestring != NULL && iot_name->valuestring[0] != '\0') {
        return iot_name->valuestring;
    }
    if (cJSON_IsString(room_name) && room_name->valuestring != NULL && room_name->valuestring[0] != '\0') {
        return room_name->valuestring;
    }
    return NULL;
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
        lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
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

static void w_rr_popup_show_map_placeholder(w_rr_ctx_t *ctx, const char *text)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL || ctx->popup_map_placeholder == NULL) {
        return;
    }
    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, NULL);
    }
    if (ctx->popup_map_overlay != NULL) {
        lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(ctx->popup_map_overlay);
    }
    lv_obj_clear_flag(ctx->popup_map_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ctx->popup_map_placeholder, text != NULL ? text : "");
    lv_obj_set_style_bg_color(ctx->popup_map_panel, w_rr_surface_fill(190), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_panel, LV_OPA_COVER, LV_PART_MAIN);
}

static void w_rr_update_popup_clean_button(w_rr_ctx_t *ctx);
static void w_rr_render_popup_rooms(w_rr_ctx_t *ctx);
static void w_rr_refresh_popup_header(w_rr_ctx_t *ctx);
static void w_rr_apply_visual(w_rr_ctx_t *ctx);
static void w_rr_layout_card(w_rr_ctx_t *ctx);
static void w_rr_render_map_hotspots(w_rr_ctx_t *ctx);

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
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(inner, 18, 18);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(inner, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(inner);

    lv_obj_t *core_dot = lv_obj_create(marker);
    lv_obj_remove_style_all(core_dot);
    lv_obj_clear_flag(core_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(core_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(core_dot, 7, 7);
    lv_obj_set_style_radius(core_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(core_dot, core, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(core_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(core_dot);
    return true;
}

static bool w_rr_map_can_refresh_now(w_rr_ctx_t *ctx, int64_t now_ms)
{
    if (ctx == NULL || ctx->card == NULL || ctx->popup_map_panel == NULL) {
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

static void w_rr_room_overlay_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *room_obj = lv_event_get_target(event);
    if (ctx == NULL || room_obj == NULL) {
        return;
    }
    uintptr_t idx1 = (uintptr_t)lv_obj_get_user_data(room_obj);
    if (idx1 == 0 || idx1 > ctx->room_count) {
        return;
    }
    size_t idx = (size_t)(idx1 - 1U);
    ctx->rooms[idx].selected = !ctx->rooms[idx].selected;
    w_rr_render_popup_rooms(ctx);
    w_rr_refresh_popup_header(ctx);
    w_rr_update_popup_clean_button(ctx);
}

static bool w_rr_use_room_list_fallback(const w_rr_ctx_t *ctx)
{
    return ctx != NULL && ctx->map_entity_id[0] == '\0';
}

static void w_rr_render_room_list_fallback(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL || ctx->popup_map_overlay == NULL) {
        return;
    }

    lv_obj_clean(ctx->popup_map_overlay);
    if (ctx->popup_map_img != NULL) {
        lv_image_set_src(ctx->popup_map_img, NULL);
        lv_obj_set_pos(ctx->popup_map_img, 0, 0);
        lv_obj_set_size(ctx->popup_map_img, 1, 1);
    }
    if (ctx->popup_map_name_label != NULL) {
        lv_obj_add_flag(ctx->popup_map_name_label, LV_OBJ_FLAG_HIDDEN);
    }

    const char *empty_text = NULL;
    if ((ctx->rooms_fetching || ctx->popup_room_request_pending) && ctx->room_count == 0) {
        empty_text = ui_i18n_get("roborock.loading_rooms", "Loading rooms");
    } else if (ctx->room_load_failed && ctx->room_count == 0) {
        empty_text = ui_i18n_get("roborock.rooms_failed_hint", "The room list could not be refreshed.");
    } else if (ctx->room_count == 0) {
        empty_text = ui_i18n_get("roborock.no_rooms_hint", "No room metadata on the map yet.");
    }
    if (empty_text != NULL) {
        w_rr_popup_show_map_placeholder(ctx, empty_text);
        return;
    }

    if (ctx->popup_map_placeholder != NULL) {
        lv_obj_add_flag(ctx->popup_map_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(ctx->popup_map_panel, w_rr_surface_fill(190), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_panel, LV_OPA_COVER, LV_PART_MAIN);

    lv_coord_t panel_w = lv_obj_get_width(ctx->popup_map_panel);
    lv_coord_t panel_h = lv_obj_get_height(ctx->popup_map_panel);
    if (panel_w <= 0) panel_w = 320;
    if (panel_h <= 0) panel_h = 160;

    lv_obj_set_pos(ctx->popup_map_overlay, 0, 0);
    lv_obj_set_size(ctx->popup_map_overlay, panel_w, panel_h);
    lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ctx->popup_map_overlay, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->popup_map_overlay, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->popup_map_overlay);

    lv_coord_t pad = 8;
    lv_coord_t gap = 6;
    lv_coord_t cols = panel_w >= 260 ? 2 : 1;
    lv_coord_t btn_w = (panel_w - (pad * 2) - (gap * (cols - 1))) / cols;
    if (btn_w < 92) {
        cols = 1;
        btn_w = panel_w - (pad * 2);
    }
    if (btn_w < 72) btn_w = 72;
    lv_coord_t btn_h = panel_h >= 180 ? 36 : 32;

    lv_color_t selected_bg =
        lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 176);
    lv_color_t selected_border = lv_color_mix(selected_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), 150);
    lv_color_t idle_bg = w_rr_surface_fill(218);
    lv_color_t idle_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);

    for (size_t i = 0; i < ctx->room_count; i++) {
        w_rr_room_t *room = &ctx->rooms[i];
        lv_coord_t col = (lv_coord_t)(i % (size_t)cols);
        lv_coord_t row = (lv_coord_t)(i / (size_t)cols);
        lv_coord_t x = pad + col * (btn_w + gap);
        lv_coord_t y = pad + row * (btn_h + gap);

        lv_obj_t *btn = lv_btn_create(ctx->popup_map_overlay);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)(i + 1U));
        lv_obj_add_event_cb(btn, w_rr_room_overlay_event_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, room->selected ? 2 : 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, room->selected ? selected_border : idle_border, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, room->selected ? selected_bg : idle_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, w_rr_pressed_fill(room->selected ? selected_bg : idle_bg), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, room->name[0] != '\0' ? room->name : room->segment_id);
        lv_obj_set_width(label, btn_w - 12);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, APP_FONT_TEXT_12, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(label,
            room->selected ? w_rr_contrast_text(selected_bg) : lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_center(label);
    }
}

static void w_rr_render_map_hotspots(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_overlay == NULL) {
        return;
    }

    lv_obj_clean(ctx->popup_map_overlay);
    if (w_rr_use_room_list_fallback(ctx)) {
        w_rr_render_room_list_fallback(ctx);
        return;
    }
    lv_obj_clear_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ctx->popup_map_overlay, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(ctx->popup_map_overlay, LV_SCROLLBAR_MODE_OFF);
    if (ctx->popup_map_dsc.data == NULL || ctx->calibration_count < 3) {
        lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint32_t source_w = ctx->map_source_w > 0 ? ctx->map_source_w : ctx->popup_map_dsc.header.w;
    uint32_t source_h = ctx->map_source_h > 0 ? ctx->map_source_h : ctx->popup_map_dsc.header.h;
    if (source_w == 0 || source_h == 0) {
        lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_coord_t map_x = 0;
    lv_coord_t map_y = 0;
    lv_coord_t map_w = 0;
    lv_coord_t map_h = 0;
    if (!w_rr_get_displayed_map_rect(ctx, &map_x, &map_y, &map_w, &map_h, NULL) || map_w <= 0 || map_h <= 0) {
        lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_pos(ctx->popup_map_overlay, map_x, map_y);
    lv_obj_set_size(ctx->popup_map_overlay, map_w, map_h);
    lv_obj_clear_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->popup_map_overlay);
    if (ctx->popup_map_name_label != NULL) {
        lv_obj_move_foreground(ctx->popup_map_name_label);
    }

    bool rendered_robot = w_rr_render_robot_overlay(ctx, map_w, map_h, source_w, source_h);
    if (ctx->room_count == 0) {
        if (!rendered_robot) {
            lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0, f = 0.0;
    if (!w_rr_compute_affine_from_calibration(ctx->calibration, ctx->calibration_count, &a, &b, &c, &d, &e, &f)) {
        if (!rendered_robot) {
            lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_color_t selected_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 168);
    lv_color_t selected_border = lv_color_mix(selected_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), 150);
    lv_color_t idle_bg = lv_color_hex(0xFFFFFF);
    lv_color_t idle_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);

    for (size_t i = 0; i < ctx->room_count; i++) {
        w_rr_room_t *room = &ctx->rooms[i];
        if (!room->has_bounds) {
            continue;
        }

        double mx[4] = {0};
        double my[4] = {0};
        w_rr_apply_affine(a, b, c, d, e, f, room->vacuum_x0, room->vacuum_y0, &mx[0], &my[0]);
        w_rr_apply_affine(a, b, c, d, e, f, room->vacuum_x1, room->vacuum_y0, &mx[1], &my[1]);
        w_rr_apply_affine(a, b, c, d, e, f, room->vacuum_x1, room->vacuum_y1, &mx[2], &my[2]);
        w_rr_apply_affine(a, b, c, d, e, f, room->vacuum_x0, room->vacuum_y1, &mx[3], &my[3]);

        double min_mx = mx[0], max_mx = mx[0], min_my = my[0], max_my = my[0];
        for (int k = 1; k < 4; k++) {
            if (mx[k] < min_mx) min_mx = mx[k];
            if (mx[k] > max_mx) max_mx = mx[k];
            if (my[k] < min_my) min_my = my[k];
            if (my[k] > max_my) max_my = my[k];
        }

        lv_coord_t x = (lv_coord_t)((min_mx / (double)source_w) * map_w);
        lv_coord_t y = (lv_coord_t)((min_my / (double)source_h) * map_h);
        lv_coord_t w = (lv_coord_t)(((max_mx - min_mx) / (double)source_w) * map_w);
        lv_coord_t h = (lv_coord_t)(((max_my - min_my) / (double)source_h) * map_h);
        if (w < 26) w = 26;
        if (h < 24) h = 24;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > map_w) {
            w = map_w - x;
        }
        if (y + h > map_h) {
            h = map_h - y;
        }
        if (w < 12 || h < 12) {
            continue;
        }

        lv_obj_t *hotspot = lv_btn_create(ctx->popup_map_overlay);
        lv_obj_set_pos(hotspot, x, y);
        lv_obj_set_size(hotspot, w, h);
        lv_obj_set_user_data(hotspot, (void *)(uintptr_t)(i + 1U));
        lv_obj_add_event_cb(hotspot, w_rr_room_overlay_event_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_set_style_radius(hotspot, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(hotspot, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(hotspot, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(hotspot, room->selected ? 2 : 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(hotspot, room->selected ? selected_border : idle_border, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(hotspot, room->selected ? selected_bg : idle_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(hotspot, w_rr_pressed_fill(room->selected ? selected_bg : idle_bg), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(hotspot, room->selected ? LV_OPA_50 : LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(hotspot, room->selected ? LV_OPA_70 : LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(hotspot, 0, LV_PART_MAIN);

        if (w >= 52 && h >= 22) {
            lv_obj_t *label = lv_label_create(hotspot);
            lv_label_set_text(label, room->name[0] != '\0' ? room->name : room->segment_id);
            lv_obj_set_width(label, w - 10);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(label, APP_FONT_TEXT_12, LV_PART_MAIN);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(label,
                room->selected ? w_rr_contrast_text(selected_bg) : lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
            lv_obj_center(label);
        }
    }
}

static void w_rr_layout_map_visual(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_map_panel == NULL) {
        return;
    }

    if (ctx->popup_map_placeholder != NULL) {
        lv_obj_center(ctx->popup_map_placeholder);
    }

    if (w_rr_use_room_list_fallback(ctx)) {
        w_rr_render_room_list_fallback(ctx);
        return;
    }

    if (ctx->popup_map_img == NULL || ctx->popup_map_dsc.data == NULL ||
        ctx->popup_map_dsc.header.w == 0 || ctx->popup_map_dsc.header.h == 0) {
        if (ctx->popup_map_img != NULL) {
            lv_obj_set_pos(ctx->popup_map_img, 0, 0);
            lv_obj_set_size(ctx->popup_map_img, 1, 1);
        }
        if (ctx->popup_map_overlay != NULL) {
            lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);
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
    w_rr_render_map_hotspots(ctx);
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
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_failed", "Map preview unavailable"));
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
        lv_obj_add_flag(ctx->popup_map_placeholder, LV_OBJ_FLAG_HIDDEN);
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
    if (ctx->popup_map_request_inflight) {
        return;
    }
    if (ctx->map_url[0] == '\0') {
        ctx->popup_map_request_pending = false;
        ctx->popup_map_failed = false;
        if (ctx->map_entity_id[0] != '\0') {
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_waiting", "Waiting for map"));
        } else {
            w_rr_render_room_list_fallback(ctx);
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
        w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_loading", "Loading map"));
    }
    if (ha_cover_fetcher_request(ctx->map_url, tgt_w, tgt_h, w_rr_popup_map_cb, ctx) != ESP_OK) {
        ctx->popup_map_request_inflight = false;
        ctx->popup_map_request_pending = true;
        ctx->popup_map_failed = false;
        if (ctx->popup_map_dsc.data == NULL) {
            w_rr_popup_show_map_placeholder(ctx, ui_i18n_get("roborock.map_waiting", "Waiting for map"));
        }
    }
}

static cJSON *w_rr_result_entity_object(cJSON *result, const char *entity_id)
{
    if (!cJSON_IsObject(result)) {
        return NULL;
    }

    cJSON *root = result;
    cJSON *response = cJSON_GetObjectItemCaseSensitive(result, "response");
    if (cJSON_IsObject(response)) {
        root = response;
    }

    cJSON *entity_obj = NULL;
    if (entity_id != NULL && entity_id[0] != '\0') {
        entity_obj = cJSON_GetObjectItemCaseSensitive(root, entity_id);
    }
    if (!cJSON_IsObject(entity_obj)) {
        entity_obj = root->child;
    }
    return cJSON_IsObject(entity_obj) ? entity_obj : NULL;
}

static void w_rr_rooms_response_cb(bool success, cJSON *result, void *user)
{
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)user;
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }

    w_rr_room_t *parsed = w_rr_calloc(W_RR_MAX_ROOMS, sizeof(*parsed));
    if (parsed == NULL) {
        ESP_LOGW(W_RR_TAG, "get_maps: out of memory");
        return;
    }

    char active_map_name[APP_MAX_NAME_LEN] = {0};
    size_t parsed_count = 0;
    bool parse_ok = false;

    if (success && result != NULL) {
        cJSON *entity_obj = w_rr_result_entity_object(result, ctx->entity_id);
        cJSON *maps = entity_obj != NULL ? cJSON_GetObjectItemCaseSensitive(entity_obj, "maps") : NULL;
        if (!cJSON_IsArray(maps)) {
            maps = cJSON_GetObjectItemCaseSensitive(result, "maps");
        }

        if (cJSON_IsArray(maps)) {
            parse_ok = true;
            cJSON *best_map = NULL;
            cJSON *map = NULL;
            cJSON_ArrayForEach(map, maps)
            {
                if (!cJSON_IsObject(map)) {
                    continue;
                }
                cJSON *rooms = cJSON_GetObjectItemCaseSensitive(map, "rooms");
                if (!cJSON_IsObject(rooms) || rooms->child == NULL) {
                    continue;
                }
                if (best_map == NULL) {
                    best_map = map;
                }
                cJSON *flag = cJSON_GetObjectItemCaseSensitive(map, "flag");
                if (w_rr_room_flag_selected(flag)) {
                    best_map = map;
                    break;
                }
            }

            if (best_map != NULL) {
                cJSON *name = cJSON_GetObjectItemCaseSensitive(best_map, "name");
                if (cJSON_IsString(name) && name->valuestring != NULL) {
                    snprintf(active_map_name, sizeof(active_map_name), "%s", name->valuestring);
                }
                cJSON *rooms = cJSON_GetObjectItemCaseSensitive(best_map, "rooms");
                if (cJSON_IsObject(rooms)) {
                    for (cJSON *room = rooms->child; room != NULL && parsed_count < W_RR_MAX_ROOMS; room = room->next) {
                        if (room->string == NULL || room->string[0] == '\0') {
                            continue;
                        }
                        const char *room_name = w_rr_room_name_from_json(room);
                        if (room_name == NULL || room_name[0] == '\0') {
                            continue;
                        }

                        w_rr_room_t *dst = &parsed[parsed_count++];
                        snprintf(dst->segment_id, sizeof(dst->segment_id), "%s", room->string);
                        snprintf(dst->name, sizeof(dst->name), "%s", room_name);
                    }
                }
            }
        }
    }

    if (parsed_count > 1) {
        qsort(parsed, parsed_count, sizeof(parsed[0]), w_rr_room_cmp);
    }

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    if (!ctx->destroyed) {
        if (parsed_count > 0) {
            memcpy(ctx->pending_rooms, parsed, parsed_count * sizeof(parsed[0]));
        }
        ctx->pending_room_count = parsed_count;
        snprintf(ctx->pending_active_map_name, sizeof(ctx->pending_active_map_name), "%s", active_map_name);
        ctx->pending_failure = !success || !parse_ok;
        ctx->pending_ready = true;
    }
    xSemaphoreGive(ctx->staging_mutex);

    free(parsed);
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
        w_rr_render_map_hotspots(ctx);
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

static void w_rr_request_rooms(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0') {
        return;
    }
    if (!w_rr_is_visible(ctx)) {
        ctx->popup_room_request_pending = true;
        return;
    }
    if (ctx->rooms_fetching) {
        return;
    }
    int64_t now_ms = w_rr_now_ms();
    int64_t retry_ms = ctx->room_load_failed ? W_RR_ROOM_RETRY_MS : 3000;
    if (ctx->last_room_fetch_ms > 0 && (now_ms - ctx->last_room_fetch_ms) < retry_ms) {
        ctx->popup_room_request_pending = true;
        return;
    }
    if (!ha_client_is_connected() || !ha_client_is_initial_sync_done()) {
        ctx->popup_room_request_pending = true;
        return;
    }

    ctx->rooms_fetching = true;
    ctx->popup_room_request_pending = false;
    ctx->room_load_failed = false;
    ctx->last_room_fetch_ms = now_ms;

    ESP_LOGI(W_RR_TAG, "Requesting roborock.get_maps for %s", ctx->entity_id);
    esp_err_t err = ha_client_call_service_with_response(
        "roborock", "get_maps", ctx->entity_id, NULL, w_rr_rooms_response_cb, ctx);
    if (err != ESP_OK) {
        ctx->rooms_fetching = false;
        ctx->popup_room_request_pending = true;
        if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(W_RR_TAG, "get_maps request failed (%s): %s", ctx->entity_id, esp_err_to_name(err));
            ctx->room_load_failed = true;
        }
    }
}

static void w_rr_merge_map_attr_metadata(w_rr_ctx_t *ctx, cJSON *attrs)
{
    if (ctx == NULL || !cJSON_IsObject(attrs)) {
        return;
    }

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

    for (size_t i = 0; i < ctx->room_count; i++) {
        ctx->rooms[i].has_bounds = false;
    }

    cJSON *rooms = cJSON_GetObjectItemCaseSensitive(attrs, "rooms");
    if (!cJSON_IsObject(rooms)) {
        if (w_rr_is_visible(ctx)) {
            w_rr_render_popup_rooms(ctx);
            w_rr_refresh_popup_header(ctx);
            w_rr_update_popup_clean_button(ctx);
        }
        return;
    }

    w_rr_room_t unmatched[W_RR_MAX_ROOMS] = {0};
    size_t unmatched_count = 0;

    for (cJSON *room = rooms->child; room != NULL; room = room->next) {
        if (room->string == NULL || room->string[0] == '\0' || !cJSON_IsObject(room)) {
            continue;
        }

        int idx = w_rr_find_room_index(ctx, room->string);
        if (idx < 0) {
            if (unmatched_count >= W_RR_MAX_ROOMS) {
                continue;
            }
            w_rr_room_t *dst = &unmatched[unmatched_count];
            memset(dst, 0, sizeof(*dst));
            snprintf(dst->segment_id, sizeof(dst->segment_id), "%s", room->string);
            if (w_rr_parse_room_bounds(room, dst)) {
                unmatched_count++;
            }
            continue;
        }

        w_rr_parse_room_bounds(room, &ctx->rooms[idx]);
    }

    size_t missing_indices[W_RR_MAX_ROOMS] = {0};
    size_t missing_count = 0;
    for (size_t i = 0; i < ctx->room_count; i++) {
        if (!ctx->rooms[i].has_bounds) {
            missing_indices[missing_count++] = i;
        }
    }

    size_t missing_cursor = 0;
    for (size_t i = 0; i < unmatched_count && missing_cursor < missing_count; i++) {
        if (!w_rr_room_name_is_placeholder(unmatched[i].name)) {
            continue;
        }
        w_rr_copy_room_bounds_only(&ctx->rooms[missing_indices[missing_cursor++]], &unmatched[i]);
        unmatched[i].segment_id[0] = '\0';
    }

    size_t remaining_unmatched = 0;
    for (size_t i = 0; i < unmatched_count; i++) {
        if (unmatched[i].segment_id[0] != '\0') {
            remaining_unmatched++;
        }
    }
    if (missing_cursor < missing_count && remaining_unmatched == (missing_count - missing_cursor)) {
        for (size_t i = 0; i < unmatched_count && missing_cursor < missing_count; i++) {
            if (unmatched[i].segment_id[0] == '\0') {
                continue;
            }
            w_rr_copy_room_bounds_only(&ctx->rooms[missing_indices[missing_cursor++]], &unmatched[i]);
            unmatched[i].segment_id[0] = '\0';
        }
    }

    for (size_t i = 0; i < unmatched_count && ctx->room_count < W_RR_MAX_ROOMS; i++) {
        if (unmatched[i].segment_id[0] == '\0') {
            continue;
        }
        ctx->rooms[ctx->room_count++] = unmatched[i];
    }

    if (ctx->room_count > 1) {
        qsort(ctx->rooms, ctx->room_count, sizeof(ctx->rooms[0]), w_rr_room_cmp);
    }
    if (w_rr_is_visible(ctx)) {
        w_rr_render_popup_rooms(ctx);
        w_rr_refresh_popup_header(ctx);
        w_rr_update_popup_clean_button(ctx);
    }
}

static void w_rr_drain_pending_rooms(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->staging_mutex == NULL) {
        return;
    }

    bool ready = false;
    bool failed = false;
    size_t count = 0;
    char active_map_name[APP_MAX_NAME_LEN] = {0};
    w_rr_room_t staged[W_RR_MAX_ROOMS] = {0};

    xSemaphoreTake(ctx->staging_mutex, portMAX_DELAY);
    ready = ctx->pending_ready;
    if (ready) {
        failed = ctx->pending_failure;
        count = ctx->pending_room_count;
        if (count > W_RR_MAX_ROOMS) {
            count = W_RR_MAX_ROOMS;
        }
        if (count > 0) {
            memcpy(staged, ctx->pending_rooms, count * sizeof(staged[0]));
        }
        snprintf(active_map_name, sizeof(active_map_name), "%s", ctx->pending_active_map_name);
        ctx->pending_ready = false;
        ctx->pending_failure = false;
        ctx->pending_room_count = 0;
        ctx->pending_active_map_name[0] = '\0';
    }
    xSemaphoreGive(ctx->staging_mutex);

    if (!ready) {
        return;
    }

    ctx->rooms_fetching = false;
    ctx->last_room_fetch_ms = w_rr_now_ms();
    ctx->room_load_failed = failed;
    if (!failed) {
        if (count > 0) {
            for (size_t i = 0; i < count; i++) {
                int existing_idx = w_rr_find_room_index(ctx, staged[i].segment_id);
                if (existing_idx >= 0) {
                    w_rr_copy_room_runtime(&staged[i], &ctx->rooms[existing_idx]);
                } else {
                    staged[i].selected = false;
                }
            }

            for (size_t i = 0; i < ctx->room_count && count < W_RR_MAX_ROOMS; i++) {
                const w_rr_room_t *existing = &ctx->rooms[i];
                bool already_staged = false;
                for (size_t j = 0; j < count; j++) {
                    if (strcmp(staged[j].segment_id, existing->segment_id) == 0) {
                        already_staged = true;
                        break;
                    }
                }
                if (already_staged) {
                    continue;
                }
                if (!existing->has_bounds && !existing->selected) {
                    continue;
                }
                staged[count++] = *existing;
            }

            if (count > 1) {
                qsort(staged, count, sizeof(staged[0]), w_rr_room_cmp);
            }
            memcpy(ctx->rooms, staged, count * sizeof(ctx->rooms[0]));
            ctx->room_count = count;
        }
        if (active_map_name[0] != '\0') {
            snprintf(ctx->active_map_name, sizeof(ctx->active_map_name), "%s", active_map_name);
        }
    }

    w_rr_render_popup_rooms(ctx);
    w_rr_refresh_popup_header(ctx);
    w_rr_update_popup_clean_button(ctx);
}

static void w_rr_style_action_button(lv_obj_t *btn, lv_obj_t *label, lv_color_t bg, lv_color_t border, lv_color_t text)
{
    if (btn == NULL) {
        return;
    }
    lv_color_t pressed = w_rr_pressed_fill(bg);
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, border, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, border, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

    if (label != NULL) {
        lv_obj_set_style_text_color(label, text, LV_PART_MAIN);
    }
}

static void w_rr_refresh_popup_repeat_visual(w_rr_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    lv_color_t active_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 180);
    lv_color_t active_border = lv_color_mix(active_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), 160);
    lv_color_t idle_bg = w_rr_surface_fill(220);
    lv_color_t idle_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_color_t idle_text = lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY);
    lv_color_t active_text = w_rr_contrast_text(active_bg);

    for (int i = 0; i < 3; i++) {
        bool active = (ctx->repeat_count == (i + 1));
        w_rr_style_action_button(
            ctx->popup_repeat_btns[i],
            ctx->popup_repeat_labels[i],
            active ? active_bg : idle_bg,
            active ? active_border : idle_border,
            active ? active_text : idle_text);
    }
}

static void w_rr_refresh_popup_header(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_status_label == NULL) {
        return;
    }

    char text[64] = {0};
    if (ctx->rooms_fetching && ctx->room_count == 0) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("roborock.loading_rooms", "Loading rooms"));
    } else if (ctx->room_load_failed && ctx->room_count == 0) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("roborock.rooms_failed", "Could not load"));
    } else {
        size_t selected = w_rr_selected_room_count(ctx);
        if (selected > 0) {
            snprintf(text, sizeof(text), "%u %s",
                (unsigned)selected,
                ui_i18n_get(selected == 1 ? "roborock.selected_singular" : "roborock.selected_plural",
                    selected == 1 ? "selected" : "selected"));
        } else if (ctx->room_count > 0) {
            snprintf(text, sizeof(text), "%u %s",
                (unsigned)ctx->room_count,
                ui_i18n_get(ctx->room_count == 1 ? "roborock.room_singular" : "roborock.room_plural",
                    ctx->room_count == 1 ? "room" : "rooms"));
        } else {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("roborock.no_rooms", "No rooms"));
        }
    }
    lv_label_set_text(ctx->popup_status_label, text);

    if (ctx->popup_map_name_label != NULL) {
        if (ctx->active_map_name[0] != '\0') {
            lv_label_set_text(ctx->popup_map_name_label, ctx->active_map_name);
            lv_obj_clear_flag(ctx->popup_map_name_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->popup_map_name_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
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

    lv_coord_t battery_w = 0;
    lv_coord_t top_row_h = 28;
    if (ctx->battery_chip != NULL) {
        lv_obj_update_layout(ctx->battery_chip);
        battery_w = lv_obj_get_width(ctx->battery_chip);
        top_row_h = LV_MAX(top_row_h, lv_obj_get_height(ctx->battery_chip));
        lv_obj_set_pos(ctx->battery_chip, LV_MAX(0, inner_w - battery_w), 0);
    }

    if (ctx->title_label != NULL) {
        lv_coord_t title_w = inner_w - battery_w - 12;
        if (title_w < 72) {
            title_w = 72;
        }
        lv_obj_set_width(ctx->title_label, title_w);
        lv_obj_set_pos(ctx->title_label, 0, 2);
        top_row_h = LV_MAX(top_row_h, lv_obj_get_height(ctx->title_label) + 2);
    }

    const lv_coord_t gap = 8;
    lv_coord_t body_y = top_row_h + 8;
    if (inner_h - body_y < 120) {
        body_y = LV_MAX(28, inner_h / 6);
    }

    lv_coord_t body_h = inner_h - body_y;
    if (body_h < 64) {
        body_h = 64;
    }

    bool wide = inner_w > inner_h;
    lv_coord_t map_x = 0;
    lv_coord_t map_y = body_y;
    lv_coord_t map_w = inner_w;
    lv_coord_t map_h = body_h;
    lv_coord_t controls_x = 0;
    lv_coord_t controls_y = body_y;
    lv_coord_t controls_w = inner_w;
    lv_coord_t controls_h = body_h;

    if (wide) {
        lv_coord_t min_controls_w = 184;
        map_w = (inner_w * 62) / 100;
        if (map_w < 138) {
            map_w = 138;
        }
        if ((inner_w - map_w - gap) < min_controls_w) {
            map_w = inner_w - min_controls_w - gap;
        }
        if (map_w < 120) {
            map_w = inner_w / 2;
        }
        controls_x = map_w + gap;
        controls_w = inner_w - controls_x;
    } else {
        lv_coord_t min_controls_h = 184;
        map_h = (body_h * 64) / 100;
        if (map_h < 112) {
            map_h = 112;
        }
        if ((body_h - map_h - gap) < min_controls_h) {
            map_h = body_h - min_controls_h - gap;
        }
        if (map_h < 96) {
            map_h = body_h / 2;
        }
        controls_y = body_y + map_h + gap;
        controls_h = inner_h - controls_y;
    }

    if (ctx->popup_map_panel != NULL) {
        lv_obj_set_pos(ctx->popup_map_panel, map_x, map_y);
        lv_obj_set_size(ctx->popup_map_panel, map_w, map_h);
    }
    if (ctx->controls_panel != NULL) {
        lv_obj_set_pos(ctx->controls_panel, controls_x, controls_y);
        lv_obj_set_size(ctx->controls_panel, controls_w, controls_h);
    }

    lv_coord_t controls_meta_h = 0;
    if (ctx->state_label != NULL) {
        lv_obj_set_width(ctx->state_label, controls_w);
        lv_obj_set_pos(ctx->state_label, controls_x, controls_y);
        controls_meta_h = 20;
    }
    if (ctx->detail_label != NULL) {
        lv_obj_set_width(ctx->detail_label, controls_w);
        lv_obj_set_pos(ctx->detail_label, controls_x, controls_y + controls_meta_h);
        controls_meta_h += 16;
    }

    if (ctx->popup_status_label != NULL) {
        lv_obj_set_pos(ctx->popup_status_label, 0, controls_meta_h + 2);
        lv_obj_set_width(ctx->popup_status_label, controls_w);
    }

    lv_coord_t repeat_btn_h = 34;
    lv_coord_t repeat_gap = 8;
    lv_coord_t repeat_btn_w = (controls_w - (repeat_gap * 2)) / 3;
    if (repeat_btn_w > 68) {
        repeat_btn_w = 68;
    }
    if (repeat_btn_w < 44) {
        repeat_btn_w = 44;
    }
    lv_coord_t repeat_row_w = repeat_btn_w * 3 + repeat_gap * 2;
    lv_coord_t repeat_x = LV_MAX(0, (controls_w - repeat_row_w) / 2);
    lv_coord_t repeat_y = controls_meta_h + 22;
    for (int i = 0; i < 3; i++) {
        if (ctx->popup_repeat_btns[i] != NULL) {
            lv_obj_set_pos(ctx->popup_repeat_btns[i], repeat_x + i * (repeat_btn_w + repeat_gap), repeat_y);
            lv_obj_set_size(ctx->popup_repeat_btns[i], repeat_btn_w, repeat_btn_h);
        }
    }

    lv_coord_t action_btn_h = 42;
    lv_coord_t action_gap = 8;
    lv_coord_t action_btn_w = wide ? controls_w : (controls_w - (action_gap * 2)) / 3;
    if (!wide && action_btn_w < 54 && controls_w >= 178) {
        action_btn_w = 54;
    }
    lv_coord_t selection_y = repeat_y + repeat_btn_h + 8;
    lv_coord_t selection_h = 30;
    lv_coord_t action_row_y = 0;
    if (wide) {
        lv_coord_t action_stack_h = action_btn_h * 3 + action_gap * 2;
        action_row_y = controls_h - action_stack_h;
        if (action_row_y < selection_y + 32) {
            action_row_y = selection_y + 32;
        }
        selection_h = action_row_y - selection_y - 8;
        if (selection_h < 24) {
            selection_h = 24;
        }
    } else {
        action_row_y = selection_y + selection_h + 10;
        lv_coord_t max_action_y = controls_h - action_btn_h;
        if (action_row_y > max_action_y) {
            action_row_y = max_action_y;
            selection_h = action_row_y - selection_y - 10;
        }
        if (selection_h < 24) {
            selection_h = 24;
            action_row_y = selection_y + selection_h + 8;
            if (action_row_y > max_action_y) {
                action_row_y = max_action_y;
            }
        }
    }

    if (ctx->popup_selection_label != NULL) {
        lv_obj_set_pos(ctx->popup_selection_label, 0, selection_y);
        lv_obj_set_size(ctx->popup_selection_label, controls_w, selection_h);
    }

    if (ctx->start_btn != NULL) {
        lv_obj_set_pos(ctx->start_btn, 0, action_row_y);
        lv_obj_set_size(ctx->start_btn, action_btn_w, action_btn_h);
    }
    if (ctx->dock_btn != NULL) {
        lv_obj_set_pos(ctx->dock_btn, wide ? 0 : (action_btn_w + action_gap), wide ? (action_row_y + action_btn_h + action_gap) : action_row_y);
        lv_obj_set_size(ctx->dock_btn, action_btn_w, action_btn_h);
    }
    if (ctx->rooms_btn != NULL) {
        lv_obj_set_pos(ctx->rooms_btn, wide ? 0 : ((action_btn_w + action_gap) * 2),
            wide ? (action_row_y + (action_btn_h + action_gap) * 2) : action_row_y);
        lv_obj_set_size(ctx->rooms_btn, action_btn_w, action_btn_h);
    }

    w_rr_layout_map_visual(ctx);
    w_rr_update_popup_clean_button(ctx);
}

static void w_rr_render_popup_rooms(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->popup_selection_label == NULL) {
        return;
    }

    if ((ctx->rooms_fetching || ctx->popup_room_request_pending) && ctx->room_count == 0) {
        lv_label_set_text(ctx->popup_selection_label, ui_i18n_get("roborock.loading_rooms", "Loading rooms"));
        lv_obj_set_style_text_color(ctx->popup_selection_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        w_rr_render_map_hotspots(ctx);
        return;
    }
    if (ctx->room_load_failed && ctx->room_count == 0) {
        lv_label_set_text(ctx->popup_selection_label, ui_i18n_get("roborock.rooms_failed_hint", "The room list could not be refreshed."));
        lv_obj_set_style_text_color(ctx->popup_selection_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        w_rr_render_map_hotspots(ctx);
        return;
    }
    if (ctx->room_count == 0) {
        lv_label_set_text(ctx->popup_selection_label, ui_i18n_get("roborock.no_rooms_hint", "No room metadata on the map yet."));
        lv_obj_set_style_text_color(ctx->popup_selection_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        w_rr_render_map_hotspots(ctx);
        return;
    }

    char summary[160] = {0};
    size_t selected = w_rr_selected_room_count(ctx);
    if (selected == 0) {
        snprintf(summary, sizeof(summary), "%s",
            w_rr_use_room_list_fallback(ctx)
                ? ui_i18n_get("roborock.tap_room_list_hint", "Tap rooms to select them.")
                : ui_i18n_get("roborock.tap_rooms_hint", "Tap rooms directly on the map to select them."));
    } else {
        size_t off = (size_t)snprintf(summary, sizeof(summary), "%u: ", (unsigned)selected);
        for (size_t i = 0; i < ctx->room_count && off + 2 < sizeof(summary); i++) {
            if (!ctx->rooms[i].selected) {
                continue;
            }
            if (off > 3 && off + 2 < sizeof(summary)) {
                summary[off++] = ',';
                summary[off++] = ' ';
            }
            off += (size_t)snprintf(summary + off, sizeof(summary) - off, "%s",
                ctx->rooms[i].name[0] != '\0' ? ctx->rooms[i].name : ctx->rooms[i].segment_id);
            if (off >= sizeof(summary)) {
                break;
            }
        }
    }
    lv_label_set_text(ctx->popup_selection_label, summary);
    lv_obj_set_style_text_color(ctx->popup_selection_label,
        selected > 0 ? lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY) : lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    w_rr_render_map_hotspots(ctx);
}

static void w_rr_repeat_btn_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *btn = lv_event_get_target(event);
    if (ctx == NULL || btn == NULL) {
        return;
    }
    uintptr_t repeat1 = (uintptr_t)lv_obj_get_user_data(btn);
    if (repeat1 < 1 || repeat1 > 3) {
        return;
    }
    ctx->repeat_count = (int)repeat1;
    w_rr_refresh_popup_repeat_visual(ctx);
}

static void w_rr_update_popup_clean_button(w_rr_ctx_t *ctx)
{
    if (ctx == NULL || ctx->rooms_btn == NULL || ctx->rooms_label == NULL) {
        return;
    }

    size_t selected = w_rr_selected_room_count(ctx);
    lv_color_t enabled_bg = lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 176);
    lv_color_t enabled_border = lv_color_mix(enabled_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), 150);
    lv_color_t disabled_bg = w_rr_surface_fill(214);
    lv_color_t disabled_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    bool enabled = !ctx->unavailable && selected > 0;

    w_rr_style_action_button(
        ctx->rooms_btn,
        ctx->rooms_label,
        enabled ? enabled_bg : disabled_bg,
        enabled ? enabled_border : disabled_border,
        enabled ? w_rr_contrast_text(enabled_bg) : lv_color_hex(APP_UI_COLOR_TEXT_MUTED));

    char text[32] = {0};
    if (selected > 0) {
        snprintf(text, sizeof(text), "%s %u",
            ui_i18n_get("roborock.clean_selected_short", "Clean"),
            (unsigned)selected);
    } else {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("roborock.clean_selected_short", "Clean"));
    }
    lv_label_set_text(ctx->rooms_label, text);
    lv_obj_set_width(ctx->rooms_label, lv_obj_get_width(ctx->rooms_btn) - 12);
    lv_label_set_long_mode(ctx->rooms_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ctx->rooms_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(ctx->rooms_label);
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

static void w_rr_popup_clean_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->unavailable) {
        return;
    }

    size_t selected = w_rr_selected_room_count(ctx);
    if (selected == 0) {
        return;
    }

    char segments[256] = {0};
    size_t off = 0;
    for (size_t i = 0; i < ctx->room_count; i++) {
        if (!ctx->rooms[i].selected) {
            continue;
        }
        off += snprintf(segments + off, sizeof(segments) - off, "%s",
            off > 0 ? "," : "");
        if (off >= sizeof(segments)) {
            break;
        }
        if (w_rr_is_numeric_text(ctx->rooms[i].segment_id)) {
            off += snprintf(segments + off, sizeof(segments) - off, "%s", ctx->rooms[i].segment_id);
        } else {
            off += snprintf(segments + off, sizeof(segments) - off, "\"%s\"", ctx->rooms[i].segment_id);
        }
        if (off >= sizeof(segments)) {
            break;
        }
    }
    if (segments[0] == '\0') {
        return;
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"command\":\"app_segment_clean\",\"params\":[{\"segments\":[%s],\"repeat\":%d}]}",
        ctx->entity_id, segments, ctx->repeat_count);

    esp_err_t err = ha_client_call_service("vacuum", "send_command", payload);
    if (err != ESP_OK) {
        ESP_LOGW(W_RR_TAG, "segment clean failed (%s): %s", ctx->entity_id, esp_err_to_name(err));
        if (ctx->popup_status_label != NULL) {
            lv_label_set_text(ctx->popup_status_label, ui_i18n_get("roborock.command_failed", "Command failed"));
        }
        return;
    }

    ctx->active_clean = true;
    ctx->paused = false;
    ctx->returning = false;
    ctx->charging = false;
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("roborock.cleaning", "Cleaning"));
    w_rr_apply_visual(ctx);
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
    esp_err_t err = w_rr_call_vacuum_simple(should_pause ? "pause" : "start", ctx->entity_id);
    if (err != ESP_OK) {
        return;
    }

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
    if (w_rr_call_vacuum_simple("return_to_base", ctx->entity_id) != ESP_OK) {
        return;
    }
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
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, APP_FONT_TEXT_16, LV_PART_MAIN);
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

    lv_color_t primary_btn_bg = ctx->unavailable
        ? w_rr_surface_fill(214)
        : lv_color_mix(lv_color_hex(APP_UI_COLOR_STATE_ON), lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_ACTIVE), 176);
    lv_color_t primary_btn_border = lv_color_mix(primary_btn_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), 150);
    lv_color_t primary_btn_text = w_rr_contrast_text(primary_btn_bg);
    lv_color_t idle_btn_bg = w_rr_surface_fill(220);
    lv_color_t idle_btn_border = lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER);
    lv_color_t idle_btn_text = lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED : APP_UI_COLOR_TEXT_PRIMARY);

    if (ctx->start_label != NULL) {
        lv_label_set_text(ctx->start_label,
            (ctx->active_clean || ctx->returning) ? ui_i18n_get("common.pause", "Pause")
                                                  : ui_i18n_get("common.start", "Start"));
    }
    w_rr_style_action_button(ctx->start_btn, ctx->start_label, primary_btn_bg, primary_btn_border, primary_btn_text);
    w_rr_style_action_button(ctx->dock_btn, ctx->dock_label, idle_btn_bg, idle_btn_border, idle_btn_text);

    lv_opa_t button_opa = ctx->unavailable ? LV_OPA_50 : LV_OPA_COVER;
    if (ctx->start_btn != NULL) lv_obj_set_style_opa(ctx->start_btn, button_opa, LV_PART_MAIN);
    if (ctx->dock_btn != NULL) lv_obj_set_style_opa(ctx->dock_btn, button_opa, LV_PART_MAIN);
    w_rr_layout_card(ctx);

    if (ctx->popup_map_panel != NULL || ctx->popup_selection_label != NULL) {
        w_rr_refresh_popup_header(ctx);
        w_rr_refresh_popup_repeat_visual(ctx);
        if (ctx->popup_map_request_pending && w_rr_is_visible(ctx)) {
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
    ctx->returning = !ctx->unavailable && w_rr_state_is_returning(state->state);
    ctx->charging = !ctx->unavailable && (strcmp(state->state, "charging") == 0 || strcmp(state->state, "docked") == 0);
    ctx->battery_pct = -1;
    ctx->fan_speed[0] = '\0';
    ctx->detail_text[0] = '\0';

    if (ctx->charging && (ctx->robot_position.valid || ctx->robot_trail_count > 0 || ctx->position_failed)) {
        ctx->robot_position = (w_rr_position_t){0};
        ctx->robot_trail_count = 0;
        ctx->position_failed = false;
        w_rr_render_map_hotspots(ctx);
    }

    if (ctx->unavailable) {
        snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("common.unavailable", "Unavailable"));
        w_rr_apply_visual(ctx);
        return;
    }

    w_rr_humanize_state(state->state, ctx->state_text, sizeof(ctx->state_text));

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (cJSON_IsObject(attrs)) {
        cJSON *battery_level = cJSON_GetObjectItemCaseSensitive(attrs, "battery_level");
        if (!cJSON_IsNumber(battery_level)) {
            battery_level = cJSON_GetObjectItemCaseSensitive(attrs, "battery");
        }
        if (cJSON_IsNumber(battery_level)) {
            ctx->battery_pct = (int)battery_level->valuedouble;
        }

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

    w_rr_drain_pending_rooms(ctx);
    w_rr_drain_pending_position(ctx);

    int64_t now = w_rr_now_ms();
    int64_t room_refresh_ms = ctx->room_load_failed ? W_RR_ROOM_RETRY_MS : W_RR_ROOM_REFRESH_MS;
    if (!ctx->rooms_fetching && (ctx->last_room_fetch_ms == 0 || (now - ctx->last_room_fetch_ms) >= room_refresh_ms)) {
        ctx->popup_room_request_pending = true;
    }

    if (ctx->popup_room_request_pending && !ctx->rooms_fetching) {
        w_rr_request_rooms(ctx);
    }
    if (ctx->popup_map_panel != NULL && ctx->popup_map_request_pending &&
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

esp_err_t w_roborock_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    theme_default_style_card(card);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    w_rr_ctx_t *ctx = w_rr_calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    snprintf(ctx->map_entity_id, sizeof(ctx->map_entity_id), "%s", def->secondary_entity_id);
    ctx->battery_pct = -1;
    ctx->repeat_count = 1;
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("roborock.ready", "Ready"));

    ctx->staging_mutex = xSemaphoreCreateMutex();
    if (ctx->staging_mutex == NULL) {
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    ctx->card = card;

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, def->title[0] != '\0' ? def->title : def->entity_id);
    lv_obj_set_style_text_font(title, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, def->w - 120);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    ctx->title_label = title;

    lv_obj_t *battery_chip = lv_obj_create(card);
    lv_obj_set_size(battery_chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(battery_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(battery_chip, LV_ALIGN_TOP_RIGHT, 0, 0);
    ctx->battery_chip = battery_chip;

    lv_obj_t *battery_label = lv_label_create(battery_chip);
    lv_label_set_text(battery_label, "--");
    lv_obj_set_style_text_font(battery_label, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_center(battery_label);
    ctx->battery_label = battery_label;

    lv_obj_t *state_label = lv_label_create(card);
    lv_label_set_text(state_label, "");
    lv_obj_set_style_text_font(state_label, APP_FONT_TEXT_22, LV_PART_MAIN);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state_label, def->w - 28);
    lv_obj_align(state_label, LV_ALIGN_TOP_LEFT, 0, 40);
    ctx->state_label = state_label;

    lv_obj_t *detail_label = lv_label_create(card);
    lv_label_set_text(detail_label, "");
    lv_obj_set_style_text_font(detail_label, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(detail_label, def->w - 28);
    lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 0, 82);
    ctx->detail_label = detail_label;

    ctx->popup_map_panel = lv_obj_create(card);
    lv_obj_clear_flag(ctx->popup_map_panel, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_clear_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ctx->popup_map_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->popup_map_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->popup_map_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(ctx->popup_map_overlay, LV_OBJ_FLAG_HIDDEN);

    ctx->popup_map_placeholder = lv_label_create(ctx->popup_map_panel);
    lv_obj_set_width(ctx->popup_map_placeholder, LV_PCT(100));
    lv_obj_set_style_text_font(ctx->popup_map_placeholder, APP_FONT_TEXT_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->popup_map_placeholder, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_map_placeholder, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_center(ctx->popup_map_placeholder);

    ctx->popup_map_name_label = lv_label_create(ctx->popup_map_panel);
    lv_obj_set_style_text_font(ctx->popup_map_name_label, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_map_name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->popup_map_name_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->popup_map_name_label, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->popup_map_name_label, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ctx->popup_map_name_label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ctx->popup_map_name_label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ctx->popup_map_name_label, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ctx->popup_map_name_label, 4, LV_PART_MAIN);
    lv_obj_align(ctx->popup_map_name_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_flag(ctx->popup_map_name_label, LV_OBJ_FLAG_HIDDEN);

    ctx->controls_panel = lv_obj_create(card);
    lv_obj_clear_flag(ctx->controls_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ctx->controls_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->controls_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->controls_panel, 0, LV_PART_MAIN);

    ctx->popup_status_label = lv_label_create(ctx->controls_panel);
    lv_label_set_text(ctx->popup_status_label, "");
    lv_obj_set_style_text_font(ctx->popup_status_label, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_status_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_width(ctx->popup_status_label, LV_PCT(100));

    for (int i = 0; i < 3; i++) {
        ctx->popup_repeat_btns[i] = lv_btn_create(ctx->controls_panel);
        lv_obj_set_user_data(ctx->popup_repeat_btns[i], (void *)(uintptr_t)(i + 1));
        lv_obj_add_event_cb(ctx->popup_repeat_btns[i], w_rr_repeat_btn_event_cb, LV_EVENT_CLICKED, ctx);
        ctx->popup_repeat_labels[i] = lv_label_create(ctx->popup_repeat_btns[i]);
        char repeat_text[8];
        snprintf(repeat_text, sizeof(repeat_text), "%dx", i + 1);
        lv_label_set_text(ctx->popup_repeat_labels[i], repeat_text);
        lv_obj_set_style_text_font(ctx->popup_repeat_labels[i], APP_FONT_TEXT_16, LV_PART_MAIN);
        lv_obj_center(ctx->popup_repeat_labels[i]);
    }

    ctx->popup_selection_label = lv_label_create(ctx->controls_panel);
    lv_label_set_text(ctx->popup_selection_label, "");
    lv_obj_set_style_text_font(ctx->popup_selection_label, APP_FONT_TEXT_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->popup_selection_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->popup_selection_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(ctx->popup_selection_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->popup_selection_label, LV_PCT(100));

    ctx->start_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("common.start", "Start"), w_rr_start_event_cb, ctx, &ctx->start_label);
    ctx->dock_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("roborock.dock", "Dock"), w_rr_dock_event_cb, ctx, &ctx->dock_label);
    ctx->rooms_btn = w_rr_make_small_button(ctx->controls_panel, ui_i18n_get("roborock.clean_selected_short", "Clean"), w_rr_popup_clean_event_cb, ctx, &ctx->rooms_label);

    lv_obj_add_event_cb(card, w_rr_card_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, w_rr_card_event_cb, LV_EVENT_SIZE_CHANGED, ctx);

    ctx->tick_timer = lv_timer_create(w_rr_tick_cb, W_RR_TICK_MS, ctx);
    if (ctx->tick_timer == NULL) {
        vSemaphoreDelete(ctx->staging_mutex);
        free(ctx);
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    lv_timer_pause(ctx->tick_timer);

    ctx->popup_room_request_pending = true;
    ctx->popup_map_request_pending = true;
    w_rr_render_popup_rooms(ctx);
    w_rr_refresh_popup_repeat_visual(ctx);
    w_rr_refresh_popup_header(ctx);
    w_rr_layout_card(ctx);
    w_rr_apply_visual(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_roborock_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
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
    }
}

void w_roborock_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_rr_ctx_t *ctx = (w_rr_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    ctx->active_clean = false;
    ctx->paused = false;
    ctx->returning = false;
    ctx->charging = false;
    ctx->battery_pct = -1;
    ctx->detail_text[0] = '\0';
    snprintf(ctx->state_text, sizeof(ctx->state_text), "%s", ui_i18n_get("common.unavailable", "Unavailable"));
    w_rr_apply_visual(ctx);
}

void w_roborock_set_visible(ui_widget_instance_t *instance, bool visible)
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
        if (ctx->popup_map_request_inflight) {
            ha_cover_fetcher_cancel(ctx);
            ctx->popup_map_request_inflight = false;
            ctx->popup_map_request_pending = true;
        }
        if (ctx->tick_timer != NULL) {
            lv_timer_pause(ctx->tick_timer);
        }
        return;
    }

    if (ctx->tick_timer != NULL) {
        lv_timer_resume(ctx->tick_timer);
    }

    w_rr_drain_pending_rooms(ctx);
    w_rr_drain_pending_position(ctx);
    if (ctx->popup_room_request_pending && !ctx->rooms_fetching) {
        w_rr_request_rooms(ctx);
    }
    if (ctx->popup_map_panel != NULL && ctx->popup_map_request_pending &&
        !ctx->popup_map_request_inflight) {
        w_rr_request_popup_map(ctx);
    }
    w_rr_apply_visual(ctx);
}
