/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The history popup's 24 h / 7 days selector driven through the real buttons: it exists only for a numeric sensor,
 * every switch replaces the chart (never two at once), choosing the active range does nothing, and a closed popup
 * leaves nothing behind and reopens at 24 h. */
#include "popup_contract_regression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ui/ui_history_popup.h"

/* Bytes the process has allocated right now: a leak per cycle shows up as growth over many cycles. */
#if defined(__APPLE__)
#include <malloc/malloc.h>
static size_t heap_in_use(void)
{
    malloc_statistics_t stats;
    malloc_zone_statistics(NULL, &stats);
    return stats.size_in_use;
}
#else
#include <malloc.h>
static size_t heap_in_use(void)
{
    struct mallinfo2 info = mallinfo2();
    return info.uordblks;
}
#endif

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "History regression failed at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void advance(void)
{
    for (int i = 0; i < 12; i++) lv_timer_handler();
    sim_popup_contract_check();
}

static lv_obj_t *label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}

static unsigned charts(lv_obj_t *root)
{
    unsigned count = lv_obj_check_type(root, &lv_chart_class) ? 1 : 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) count += charts(lv_obj_get_child(root, i));
    return count;
}

static void click(const char *text)
{
    lv_obj_t *found = label(lv_layer_top(), text);
    CHECK(found != NULL);
    lv_obj_t *button = lv_obj_get_parent(found);
    CHECK(lv_obj_check_type(button, &lv_button_class));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    advance();
}

static void close_popup(void)
{
    lv_obj_t *overlay = lv_obj_get_child(lv_layer_top(), 0);
    CHECK(overlay != NULL);
    lv_obj_delete(overlay);
    advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}

/* Bytes the process keeps after `cycles` popups, measured after a warm-up of 40 (glyph caches, fonts). With `switch_range` a
 * numeric sensor popup is switched between 24 h and 7 days seven times per cycle; without it a lock popup is opened and closed. */
static size_t history_cycles(bool switch_range, int cycles)
{
    for (int round = 0; round < 2; round++) {
        const int n = round == 0 ? 40 : cycles;
        const size_t before = heap_in_use();
        for (int i = 0; i < n; i++) {
            if (switch_range) {
                ui_history_popup_open("sensor.test_temperature", "Test", "21.8 °C", true, "");
                advance();
                for (int k = 0; k < 3; k++) { click("7 days"); click("24 h"); }
                click("7 days");
            } else {
                ui_history_popup_open("lock.test_lock", "Lock", "Locked", false, "lock");
                advance();
            }
            close_popup();
        }
        if (round == 1) { const size_t after = heap_in_use(); return after > before ? after - before : 0; }
    }
    return 0;
}

int sim_history_regression(void)
{
    lv_obj_t *top = lv_layer_top();

    /* a numeric sensor: selector, 24 h chart first */
    ui_history_popup_open("sensor.test_temperature", "Test", "21.8 °C", true, "");
    advance();
    CHECK(lv_obj_get_child_count(top) == 1);
    CHECK(label(top, "24 h") != NULL && label(top, "7 days") != NULL);
    CHECK(charts(top) == 1);
    CHECK(label(top, "Hourly means") == NULL);

    click("7 days");
    CHECK(label(top, "Hourly means") != NULL);
    CHECK(charts(top) == 1);
    click("7 days"); /* the active range: nothing changes */
    CHECK(label(top, "Hourly means") != NULL && charts(top) == 1);

    click("24 h");
    CHECK(label(top, "Hourly means") == NULL);
    CHECK(charts(top) == 1);

    for (int i = 0; i < 6; i++) { /* quick switching never stacks charts or leaves the other view's status */
        click(i % 2 == 0 ? "7 days" : "24 h");
        CHECK(charts(top) == 1);
        CHECK((label(top, "Hourly means") != NULL) == (i % 2 == 0));
    }
    close_popup();

    /* reopen: always 24 h, even after closing on the week view */
    ui_history_popup_open("sensor.test_temperature", "Test", "21.8 °C", true, "");
    advance();
    click("7 days");
    close_popup();
    ui_history_popup_open("sensor.test_temperature", "Test", "21.8 °C", true, "");
    advance();
    CHECK(label(top, "Hourly means") == NULL && charts(top) == 1);
    /* a second popup replaces the first instead of stacking */
    ui_history_popup_open("sensor.other_temperature", "Other", "5 °C", true, "");
    advance();
    CHECK(lv_obj_get_child_count(top) == 1 && charts(top) == 1);
    close_popup();

    /* no selector for a discrete history, a lock or a sensor without numbers */
    ui_history_popup_open("lock.test_lock", "Lock", "Locked", false, "lock");
    advance();
    CHECK(label(top, "7 days") == NULL && label(top, "24 h") == NULL);
    close_popup();
    ui_history_popup_open("sensor.test_text", "Text", "idle", false, "");
    advance();
    CHECK(label(top, "7 days") == NULL);
    close_popup();
    ui_history_popup_open("binary_sensor.test_door", "Door", "Open", false, "door");
    advance();
    CHECK(label(top, "7 days") == NULL);
    close_popup();

    /* memory: a popup cycle keeps a little in LVGL itself (the untouched lock popup does too), so the switching of the
     * range is judged against that baseline: seven chart builds per cycle may add at most 512 bytes. */
    const size_t baseline = history_cycles(false, 300);
    const size_t switching = history_cycles(true, 300);
    fprintf(stderr, "history memory over 300 popup cycles: lock popup +%zu bytes, week/day switching +%zu bytes\n", baseline, switching);
    CHECK(switching <= baseline + 300 * 512);

    printf("history regression: ok (%u checks)\n", checks);
    return 0;
}
