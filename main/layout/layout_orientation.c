/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/layout_orientation.h"
#include "layout/widget_types.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

static int number(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}
static int clamp(int n, int lo, int hi) { return n < lo ? lo : (n > hi ? hi : n); }
static const char *text(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
widget_size_limits_t layout_orientation_widget_limits(const char *type, const char *style, bool portrait)
{
    widget_size_limits_t l = widget_size_limits_for(type, style);
    if (portrait) {
        if (type && !strcmp(type, "solar_forecast")) l.max_w = LAYOUT_PORTRAIT_WIDTH;
        if (l.max_w > LAYOUT_PORTRAIT_WIDTH) l.max_w = LAYOUT_PORTRAIT_WIDTH;
        if (l.max_h == APP_CONTENT_BOX_HEIGHT) l.max_h = LAYOUT_PORTRAIT_HEIGHT;
    }
    return l;
}
static widget_size_limits_t limits(const cJSON *widget, bool portrait)
{
    return layout_orientation_widget_limits(text(widget, "type"), text(widget, "style_variant"), portrait);
}
bool layout_orientation_rect_valid(const cJSON *widget, const cJSON *rect, bool portrait)
{
    const char *keys[] = {"x", "y", "w", "h"};
    for (int i = 0; i < 4; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(rect, keys[i]);
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || floor(v->valuedouble) != v->valuedouble ||
            v->valuedouble < 0 || v->valuedouble > LAYOUT_PORTRAIT_MAX_Y) return false;
    }
    const int x = number(rect, "x"), y = number(rect, "y");
    const int w = number(rect, "w"), h = number(rect, "h");
    const widget_size_limits_t l = limits(widget, portrait);
    return w >= l.min_w && h >= l.min_h && w <= l.max_w && h <= l.max_h &&
           x + w <= (portrait ? LAYOUT_PORTRAIT_WIDTH : APP_CONTENT_BOX_WIDTH) &&
           y + h <= (portrait ? LAYOUT_PORTRAIT_MAX_Y : APP_CONTENT_BOX_HEIGHT);
}
static bool set_copy(cJSON *obj, const char *key, const cJSON *value)
{
    cJSON *copy = cJSON_Duplicate(value, true);
    if (!copy) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(obj, key);
    if (!cJSON_AddItemToObject(obj, key, copy)) { cJSON_Delete(copy); return false; }
    return true;
}
static cJSON *rect(int x, int y, int w, int h)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    if (!cJSON_AddNumberToObject(r, "x", x) || !cJSON_AddNumberToObject(r, "y", y) ||
        !cJSON_AddNumberToObject(r, "w", w) || !cJSON_AddNumberToObject(r, "h", h)) {
        cJSON_Delete(r); return NULL;
    }
    return r;
}

/* Missing portrait positions are generated deterministically in landscape
 * reading order. Wide cards keep their width; compact cards share a row.
 * Long pages scroll rather than dropping entities or shrinking text. */
static bool portrait_positions(cJSON *page)
{
    cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
    cJSON *order[APP_MAX_WIDGETS_PER_PAGE];
    int n = 0;
    cJSON *widget;
    cJSON_ArrayForEach(widget, widgets) {
        if (n == APP_MAX_WIDGETS_PER_PAGE) return false;
        order[n++] = widget;
    }
    for (int i = 1; i < n; i++) {
        cJSON *item = order[i];
        const cJSON *r = cJSON_GetObjectItemCaseSensitive(item, "rect");
        int j = i;
        while (j > 0) {
            const cJSON *prev = cJSON_GetObjectItemCaseSensitive(order[j-1], "rect");
            if (number(prev, "y") < number(r, "y") ||
                (number(prev, "y") == number(r, "y") && number(prev, "x") <= number(r, "x"))) break;
            order[j] = order[j-1]; j--;
        }
        order[j] = item;
    }
    int x = 0, y = 0, row_h = 0;
    for (int i = 0; i < n; i++) {
        widget = order[i];
        cJSON *saved = cJSON_GetObjectItemCaseSensitive(widget, "portrait_rect");
        if (saved && !layout_orientation_rect_valid(widget, saved, true)) return false;
    }
    for (int i = 0; i < n; i++) {
        widget = order[i];
        if (cJSON_GetObjectItemCaseSensitive(widget, "portrait_rect")) continue;
        const cJSON *base = cJSON_GetObjectItemCaseSensitive(widget, "rect");
        const widget_size_limits_t l = limits(widget, true);
        const int w = clamp(number(base, "w"), l.min_w, l.max_w);
        const int h = clamp(number(base, "h"), l.min_h, l.max_h);
        const bool background = !strcmp(text(widget, "type"), "empty_tile");
        if (!background) {
            if (x && x + w > LAYOUT_PORTRAIT_WIDTH) { x = 0; y += row_h + 10; row_h = 0; }
            /* Reserve all explicit positions, including cards later in reading order. */
            bool overlap;
            do {
                overlap = false;
                for (int j = 0; j < n; j++) {
                    const cJSON *r = cJSON_GetObjectItemCaseSensitive(order[j], "portrait_rect");
                    if (!r || !strcmp(text(order[j], "type"), "empty_tile")) continue;
                    int rx = number(r, "x"), ry = number(r, "y");
                    if (x < rx + number(r, "w") + 10 && x + w + 10 > rx &&
                        y < ry + number(r, "h") + 10 && y + h + 10 > ry) {
                        y = ry + number(r, "h") + 10;
                        x = 0; row_h = 0; overlap = true; break;
                    }
                }
            } while (overlap);
            if (y + h > LAYOUT_PORTRAIT_MAX_Y) return false;
        }
        cJSON *r = background ? rect(0, 0, 720, 1160) : rect(x, y, w, h);
        if (!r || !cJSON_AddItemToObject(widget, "portrait_rect", r)) { cJSON_Delete(r); return false; }
        if (!background) { x += w + 10; if (h > row_h) row_h = h; }
    }
    return true;
}

bool layout_orientation_project(cJSON *root, bool portrait)
{
    int canvas_h = portrait ? LAYOUT_PORTRAIT_HEIGHT : APP_CONTENT_BOX_HEIGHT;
    cJSON *page;
    cJSON_ArrayForEach(page, cJSON_GetObjectItemCaseSensitive(root, "pages")) {
        if (portrait && !portrait_positions(page)) return false;
        cJSON *widget;
        cJSON_ArrayForEach(widget, cJSON_GetObjectItemCaseSensitive(page, "widgets")) {
            if (!portrait) continue;
            const cJSON *base = cJSON_GetObjectItemCaseSensitive(widget, "rect");
            if (!set_copy(widget, "landscape_rect", base)) return false;
            const cJSON *position = cJSON_GetObjectItemCaseSensitive(widget, "portrait_rect");
            const int bottom = number(position, "y") + number(position, "h");
            if (bottom > canvas_h) canvas_h = bottom;
            if (!set_copy(widget, "rect", position)) return false;
        }
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "orientation");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "canvas_w");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "canvas_h");
    return cJSON_AddStringToObject(root, "orientation", portrait ? "portrait" : "landscape") &&
           cJSON_AddNumberToObject(root, "canvas_w", portrait ? LAYOUT_PORTRAIT_WIDTH : APP_CONTENT_BOX_WIDTH) &&
           cJSON_AddNumberToObject(root, "canvas_h", canvas_h);
}

bool layout_orientation_canonicalize(cJSON *root)
{
    const bool portrait = !strcmp(text(root, "orientation"), "portrait");
    cJSON *page;
    cJSON_ArrayForEach(page, cJSON_GetObjectItemCaseSensitive(root, "pages")) {
        cJSON *widget;
        cJSON_ArrayForEach(widget, cJSON_GetObjectItemCaseSensitive(page, "widgets")) {
            if (portrait) {
                const cJSON *edited = cJSON_GetObjectItemCaseSensitive(widget, "rect");
                if (!layout_orientation_rect_valid(widget, edited, true) || !set_copy(widget, "portrait_rect", edited)) return false;
                const cJSON *base = cJSON_GetObjectItemCaseSensitive(widget, "landscape_rect");
                if (base) {
                    if (!set_copy(widget, "rect", base)) return false;
                } else {
                    /* A new portrait widget has no existing landscape position. */
                    widget_size_limits_t l = limits(widget, false);
                    int w = clamp(number(edited, "w"), l.min_w, l.max_w);
                    int h = clamp(number(edited, "h"), l.min_h, l.max_h);
                    cJSON *r = rect(clamp(number(edited, "x"), 0, APP_CONTENT_BOX_WIDTH-w),
                                    clamp(number(edited, "y"), 0, APP_CONTENT_BOX_HEIGHT-h), w, h);
                    if (!r) return false;
                    cJSON_DeleteItemFromObjectCaseSensitive(widget, "rect");
                    if (!cJSON_AddItemToObject(widget, "rect", r)) { cJSON_Delete(r); return false; }
                }
            }
            cJSON_DeleteItemFromObjectCaseSensitive(widget, "landscape_rect");
        }
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "orientation");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "canvas_w");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "canvas_h");
    return true;
}
