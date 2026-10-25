/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "popup_contract_regression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "app_config.h"
#include "ui/notification_message.h"
#include "ui/notification_emoji.h"
#include "ui/text_font.h"
#include "ui/ui_notification_popup.h"

#if defined(__APPLE__)
#include <malloc/malloc.h>
static size_t heap_in_use(void) { malloc_statistics_t s; malloc_zone_statistics(NULL, &s); return s.size_in_use; }
#else
#include <malloc.h>
static size_t heap_in_use(void) { return mallinfo2().uordblks; }
#endif

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Notification regression line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static lv_obj_t *find_span(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_spangroup_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_span(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}

static void show(const char *message)
{
    app_notification_t *n = calloc(1, sizeof(*n)); CHECK(n);
    n->remember = true;
    snprintf(n->title, sizeof(n->title), "Weather");
    snprintf(n->message, sizeof(n->message), "%s", message);
    ui_notification_popup_show(n);
    lv_obj_update_layout(lv_layer_top());
    for (int i = 0; i < 4; i++) { lv_tick_inc(20); lv_timer_handler(); }
    sim_popup_contract_check();
}

int sim_notification_regression(void)
{
    const lv_font_t *base = ui_font_role(UI_TEXT_META);
    const lv_font_t *bold = ui_font_emphasis(base);
    CHECK(bold != base && bold == ui_font_emphasis(base));
    CHECK(bold->line_height == base->line_height && bold->base_line == base->base_line);
    CHECK(ui_font_text_px_of(bold) == ui_font_text_px_of(base));
    for (uint32_t cp = 0x2080; cp <= 0x2089; cp++) {
        lv_font_glyph_dsc_t glyph;CHECK(bold->get_glyph_dsc(bold, &glyph, cp, 0));
    }
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    lv_obj_set_width(parent, 300);lv_obj_set_height(parent, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *label = lv_label_create(parent), *spans = NULL;
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, notification_emoji_font(base, ui_font_text_px_of(base)), LV_PART_MAIN);
    notification_message_set(label, &spans, "Humidity: **100%**\n\n__CO₂ / m³ ⚠️__");
    lv_obj_update_layout(parent);
    CHECK(spans && lv_obj_is_hidden(label) && !lv_obj_is_hidden(spans));
    CHECK(lv_spangroup_get_span_count(spans) == 4);
    CHECK(!strcmp(lv_label_get_text(label), "Humidity: 100%\n\nCO₂ / m³ ⚠️"));
    CHECK(!strcmp(lv_span_get_text(lv_spangroup_get_child(spans, 1)), "100%"));
    lv_style_value_t font;
    CHECK(lv_style_get_prop(lv_span_get_style(lv_spangroup_get_child(spans, 1)), LV_STYLE_TEXT_FONT, &font) == LV_STYLE_RES_FOUND);
    CHECK(((const lv_font_t *)font.ptr)->fallback == bold);
    CHECK(lv_obj_get_width(spans) <= lv_obj_get_content_width(parent));
    CHECK(lv_obj_get_style_text_align(spans, LV_PART_MAIN) == LV_TEXT_ALIGN_LEFT);
    const int multiline_height = lv_obj_get_height(spans);
    CHECK(multiline_height >= base->line_height * 3);
    notification_message_set(label, &spans, "A **short** message");lv_obj_update_layout(parent);
    CHECK(lv_obj_get_style_text_align(spans, LV_PART_MAIN) == LV_TEXT_ALIGN_CENTER);
    CHECK(lv_obj_get_height(spans) < multiline_height);
    notification_message_set(label, &spans, "Unmatched **marker");
    CHECK(!lv_obj_is_hidden(label) && lv_obj_is_hidden(spans));
    CHECK(lv_spangroup_get_span_count(spans) == 0 && !strcmp(lv_label_get_text(label), "Unmatched **marker"));
    lv_obj_delete(parent);

    /* Real popup replacement, plain/bold toggling and recall. Objects and fonts
     * must remain bounded; a text-area tap still dismisses the card. */
    const char *message = "**Dense Fog Detected!**\n\nTime: 09:00\nHumidity: **100%**\nTemperature: 11.8 °C\n\n__Effects on Solar Production:__\nFog blocks less light than real clouds.\n\nCO₂ / m³ ⚠️";
    show(message);show("Plain message");show(message);
    const size_t heap_before = heap_in_use();
    const int font_count = ui_fonts_stats().instances;
    const uint32_t overlay_count = lv_obj_get_child_count(lv_layer_top());
    for (int i = 0; i < 80; i++) {
        show(i % 2 ? "Plain message" : message);
        CHECK(lv_obj_get_child_count(lv_layer_top()) == overlay_count);
        CHECK(ui_fonts_stats().instances == font_count);
        lv_obj_t *body = find_span(lv_layer_top());CHECK(body);
        CHECK(lv_spangroup_get_span_count(body) == (i % 2 ? 0U : 6U));
    }
    show(message);
    CHECK(heap_in_use() <= heap_before + 4096);
    lv_obj_t *body = find_span(lv_layer_top());CHECK(body && !lv_obj_is_hidden(body));
    lv_area_t area;lv_obj_get_coords(body, &area);
    lv_point_t point = {area.x1 + 3, area.y1 + 3};
    lv_obj_t *hit = lv_indev_search_obj(lv_layer_top(), &point);CHECK(hit);
    lv_obj_send_event(hit, LV_EVENT_CLICKED, NULL);
    CHECK(!ui_notification_popup_is_active());
    CHECK(ui_notification_popup_show_last());
    body = find_span(lv_layer_top());CHECK(body && !lv_obj_is_hidden(body));
    ui_notification_popup_dismiss();
    CHECK(!ui_notification_popup_is_active());
    printf("Notification regression: %u checks passed\n", checks);
    return 0;
}
