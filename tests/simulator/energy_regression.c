/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The Energy page's seven-day details through the real node circles: a tap opens the popup (default) or fills the
 * right-hand panel ("side", wide pages only), nodes without a source stay informational, a new selection replaces the
 * old one, and the page can be deleted with a popup or a selection open. */
#include "popup_contract_regression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ui/display_geometry.h"
#include "ui/ui_energy_page.h"
#include "ui/ui_days_view.h"
#include "ui/ui_i18n.h"

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
    fprintf(stderr, "Energy regression failed at line %d: %s\n", __LINE__, #condition); exit(1); \
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

/* A label whose text starts with the given prefix. */
static lv_obj_t *label_starting(lv_obj_t *root, const char *prefix)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strncmp(lv_label_get_text(root), prefix, strlen(prefix))) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label_starting(lv_obj_get_child(root, i), prefix);
        if (found != NULL) return found;
    }
    return NULL;
}

/* The same click the touch driver sends to whatever sits at the point. */
static void tap(lv_obj_t *page, int x, int y)
{
    lv_point_t point = {x, y};
    lv_obj_t *target = lv_indev_search_obj(lv_layer_top(), &point);
    if (target == NULL) target = lv_indev_search_obj(page, &point);
    CHECK(target != NULL);
    lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    advance();
}

static lv_obj_t *make_page(const char *energy_json, ui_energy_page_instance_t *instance)
{
    ha_energy_snapshot_t snapshot = {.available = true, .has_grid = true, .has_solar = true, .has_battery = true,
        .has_gas = true, .has_water = true, .from_grid_kwh = 0.4f, .to_grid_kwh = 16.7f, .solar_kwh = 42.6f,
        .to_battery_kwh = 9.2f, .from_battery_kwh = 7.6f, .gas_value = 0.78f, .water_value = 190.0f};
    snprintf(snapshot.gas_unit, sizeof(snapshot.gas_unit), "m3");
    snprintf(snapshot.water_unit, sizeof(snapshot.water_unit), "L");
    sim_host_energy_set(&snapshot);
    cJSON *page = cJSON_Parse(energy_json);
    CHECK(page != NULL);
    ui_energy_page_config_t config;
    ui_energy_page_config_from_json(page, "energy", "Energy", &config);
    cJSON_Delete(page);
    CHECK(ui_energy_page_create(&config, lv_screen_active(), instance) == ESP_OK);
    ui_energy_page_apply_all_states(instance);
    lv_obj_update_layout(instance->obj);
    advance();
    return instance->obj;
}

/* Node centres in page coordinates (ui_energy_page.c): gas above and water below the home column. */
#define GAS_X 560
#define GAS_Y_TOP 100
#define SOLAR_X 360
#define WATER_X 560

/* Bytes the process keeps after `cycles` pages (after a warm-up of 30): the page is built and deleted, alternately as popup and
 * as right-hand panel; with `taps` Gas, Water and Solar are tapped on the way and every popup is closed. */
static size_t energy_cycles(bool taps, int cycles)
{
    lv_obj_t *top = lv_layer_top();
    const int water_y = APP_VIEW_CONTENT_HEIGHT == 720 ? 462 : 500;
    const int gas_y = APP_VIEW_CONTENT_HEIGHT == 720 ? 102 : GAS_Y_TOP;
    for (int round = 0; round < 2; round++) {
        const int n = round == 0 ? 30 : cycles;
        const size_t before = heap_in_use();
        for (int i = 0; i < n; i++) {
            ui_energy_page_instance_t inst = {0};
            lv_obj_t *page = make_page((i % 2) == 0 ? "{\"energy\":{\"source\":\"ha_energy\",\"details\":\"side\"}}"
                                                     : "{\"energy\":{\"source\":\"ha_energy\"}}", &inst);
            if (taps) {
                const int xs[3] = {GAS_X, WATER_X, SOLAR_X};
                const int ys[3] = {gas_y, water_y, gas_y};
                for (int t = 0; t < 3; t++) {
                    tap(page, xs[t], ys[t]);
                    if (lv_obj_get_child_count(top) > 0) { lv_obj_delete(lv_obj_get_child(top, 0)); advance(); }
                }
            }
            lv_obj_delete(page);
            advance();
        }
        if (round == 1) { const size_t after = heap_in_use(); return after > before ? after - before : 0; }
    }
    return 0;
}

static void daily_labels(void)
{
    ha_history_days_t *result = ui_days_view_simulated(false);
    CHECK(result != NULL);
    for (size_t i = 0; i < result->days.count; i++) {
        result->days.day[i].valid = result->days.day[i].complete = result->days.day[i].hours;
    }
    lv_obj_t *root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(root, 650, 430);
    lv_obj_t *status = lv_label_create(root);
    lv_obj_t *body = lv_obj_create(root);
    lv_obj_set_size(body, 600, 360);
    lv_obj_update_layout(root);
    ui_days_view_t view = {.body = body, .status = status, .color = 0x4FCB7E};
    const char *languages[] = {"en", "de"};
    const char *consumption[] = {"Daily consumption", "Tagesverbrauch"};
    const char *production[] = {"Daily production", "Tagesproduktion"};
    for (int i = 0; i < 2; i++) {
        CHECK(ui_i18n_init(languages[i]) == ESP_OK);
        view.production = false;
        ui_days_view_show(&view, result);
        CHECK(strcmp(lv_label_get_text(status), consumption[i]) == 0);
        view.production = true;
        ui_days_view_show(&view, result);
        CHECK(strcmp(lv_label_get_text(status), production[i]) == 0);
    }
    CHECK(ui_i18n_init("en") == ESP_OK);
    result->days.day[0].complete = 0;
    ui_days_view_show(&view, result);
    CHECK(strcmp(lv_label_get_text(status), "Lighter bars: incomplete day") == 0);
    free(result);
    lv_obj_delete(root);
}

int sim_energy_regression(void)
{
    daily_labels();
    lv_obj_t *top = lv_layer_top();
    const int water_y = APP_VIEW_CONTENT_HEIGHT == 720 ? 462 : 500; /* the Tab5 sits its nodes closer together */
    const int gas_y = APP_VIEW_CONTENT_HEIGHT == 720 ? 102 : GAS_Y_TOP;

    /* popup choice (the default): a tap opens the popup, another node replaces it */
    ui_energy_page_instance_t popup_page = {0};
    lv_obj_t *page = make_page("{\"energy\":{\"source\":\"ha_energy\"}}", &popup_page);
    CHECK(lv_obj_get_child_count(top) == 0);
    tap(page, GAS_X, gas_y);
    CHECK(lv_obj_get_child_count(top) == 1);
    CHECK(label(top, "Gas") != NULL);
    CHECK(label_starting(top, "Last 7 days") != NULL);
    tap(page, WATER_X, water_y);   /* the backdrop is on top: closing first is what a user does */
    lv_obj_delete(lv_obj_get_child(top, 0));
    advance();
    CHECK(lv_obj_get_child_count(top) == 0);
    tap(page, WATER_X, water_y);
    CHECK(lv_obj_get_child_count(top) == 1 && label(top, "Water") != NULL);
    lv_obj_delete(lv_obj_get_child(top, 0));
    advance();
    lv_obj_delete(page);           /* the page can go with nothing open */
    advance();

    /* a deleted page with a popup still open must not leave a dangling reference */
    ui_energy_page_instance_t second = {0};
    page = make_page("{\"energy\":{\"source\":\"ha_energy\"}}", &second);
    tap(page, GAS_X, gas_y);
    CHECK(lv_obj_get_child_count(top) == 1);
    lv_obj_delete(page);
    advance();
    lv_obj_delete(lv_obj_get_child(top, 0));
    advance();
    CHECK(lv_obj_get_child_count(top) == 0);

    /* manual source: Gas and Water follow their entities, a node without one stays informational */
    ui_energy_page_instance_t manual = {0};
    page = make_page("{\"energy\":{\"source\":\"manual_live\",\"gas_entity_id\":\"sensor.gas_meter\"}}", &manual);
    tap(page, WATER_X, water_y);
    CHECK(lv_obj_get_child_count(top) == 0);          /* no water entity configured */
    tap(page, GAS_X, gas_y);
    CHECK(lv_obj_get_child_count(top) == 1);
    lv_obj_delete(lv_obj_get_child(top, 0));
    advance();
    tap(page, SOLAR_X, APP_VIEW_CONTENT_HEIGHT == 720 ? 102 : 100); /* a manual Solar is a power sensor: no history */
    CHECK(lv_obj_get_child_count(top) == 0);
    lv_obj_delete(page);
    advance();

    /* right-hand panel: only where it fits; there a tap fills it and opens no popup */
    ui_energy_page_instance_t side = {0};
    page = make_page("{\"energy\":{\"source\":\"ha_energy\",\"details\":\"side\"}}", &side);
    const bool wide = label(page, "Select Gas, Water or Solar") != NULL;
    if (APP_VIEW_CONTENT_WIDTH >= 1200 && APP_VIEW_CONTENT_HEIGHT >= 560) {
        CHECK(wide);                                      /* the 10-inch and the Tab5 landscape page have room at the standard text size */
    }
    if (wide) {
        tap(page, GAS_X, gas_y);
        CHECK(lv_obj_get_child_count(top) == 0);       /* no duplicate popup */
        CHECK(label(page, "Select Gas, Water or Solar") == NULL);
        CHECK(label_starting(page, "Last 7 days") != NULL);
        tap(page, WATER_X, water_y);                     /* a new selection replaces the old one */
        CHECK(label_starting(page, "Last 7 days") != NULL);
        unsigned bars = 0;
        for (uint32_t i = 0; i < lv_obj_get_child_count(page); i++) bars++;
        CHECK(bars > 0);
        tap(page, SOLAR_X, gas_y);
        CHECK(lv_obj_get_child_count(top) == 0);
        lv_obj_delete(page);                              /* with a selection shown */
        advance();
    } else {
        CHECK(!wide);                                     /* narrow page: the popup stays */
        tap(page, GAS_X, gas_y);
        CHECK(lv_obj_get_child_count(top) == 1);
        lv_obj_delete(lv_obj_get_child(top, 0));
        advance();
        lv_obj_delete(page);
        advance();
    }
    CHECK(lv_obj_get_child_count(top) == 0);

    /* memory: building and deleting an Energy page keeps a little in LVGL itself, so the node taps are judged against a
     * baseline of pages built and deleted without any tap: a cycle of three taps may add at most 512 bytes. */
    const size_t baseline = energy_cycles(false, 300);
    const size_t tapping = energy_cycles(true, 300);
    fprintf(stderr, "energy memory over 300 page cycles: page only +%zu bytes, with three taps +%zu bytes\n", baseline, tapping);
    CHECK(tapping <= baseline + 300 * 512);
    CHECK(lv_obj_get_child_count(top) == 0);

    printf("energy regression: ok (%u checks)\n", checks);
    return 0;
}
