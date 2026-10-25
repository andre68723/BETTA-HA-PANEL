/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Portfolio widget: what the Parqet Companion sensors become on the tile (value, gain, XIRR, dividends, interval, holdings), the
 * percentages-only option, the states without data and the tile sizes. All figures are invented. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ha/parqet_performance.h"
#include "ui/ui_widget_factory.h"

esp_err_t w_parqet_portfolio_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_parqet_portfolio_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_parqet_portfolio_mark_unavailable(ui_widget_instance_t *);
void sim_parqet_set(const char *portfolio_id, const char *interval, const parqet_perf_t *perf);
void sim_parqet_clear(void);

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "parqet line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void advance(void) { lv_timer_handler(); lv_timer_handler(); }

/* The first visible label under root whose text starts with prefix. */
static lv_obj_t *find_prefix(lv_obj_t *root, const char *prefix)
{
    if (lv_obj_check_type(root, &lv_label_class) && !lv_obj_is_hidden(root)) {
        const char *text = lv_label_get_text(root);
        if (text != NULL && strncmp(text, prefix, strlen(prefix)) == 0) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_prefix(lv_obj_get_child(root, i), prefix);
        if (found) return found;
    }
    return NULL;
}

static void apply(ui_widget_instance_t *widget, const char *entity, const char *state, const char *attrs)
{
    ha_state_t s = {0};
    snprintf(s.entity_id, sizeof(s.entity_id), "%s", entity);
    snprintf(s.state, sizeof(s.state), "%s", state);
    snprintf(s.attributes_json, sizeof(s.attributes_json), "%s", attrs);
    sim_host_state_add(&s);
    w_parqet_portfolio_apply_state(widget, &s);
    advance();
}

static const char *ATTRS_MAX =
    "{\"interval\":\"max\",\"unit_of_measurement\":\"EUR\",\"entry_id\":\"e1\",\"portfolio_id\":\"p1\",\"holdings_count\":24,\"top_holdings\":["
    "{\"name\":\"Alpha Utilities\",\"value\":3170.43,\"weight\":6.0},{\"name\":\"Beta Global Dividend ETF\",\"value\":2900.1,\"weight\":5.5},"
    "{\"name\":\"Gamma Telecom\",\"value\":2536.35,\"weight\":4.8}]}";

/* Each case gets a screen of its own: tiles created one after another on one screen were laid out with no size here. */
static const char *s_interval = "";
static const char *s_view = "";
static bool s_selector = false;

static ui_widget_instance_t make(int h, bool hide, bool with_sensors)
{
    lv_screen_load(lv_obj_create(NULL));
    advance();
    ui_widget_def_t def = {.w = 360, .h = h, .parqet_hide_amounts = hide, .parqet_selector = s_selector};
    strlcpy(def.parqet_interval, s_interval, sizeof(def.parqet_interval));
    strlcpy(def.parqet_view, s_view, sizeof(def.parqet_view));
    strcpy(def.type, "parqet_portfolio");
    strcpy(def.entity_id, "sensor.pq_value");
    strcpy(def.title, "Portfolio");
    if (with_sensors) {
        strcpy(def.secondary_entity_id, "sensor.pq_gain");
        strcpy(def.tertiary_entity_id, "sensor.pq_xirr");
        strcpy(def.extra_entity_id[UI_PARQET_EXTRA_DIVIDENDS], "sensor.pq_div");
        strcpy(def.extra_entity_id[UI_PARQET_EXTRA_RETURN], "sensor.pq_ret");
    }
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    lv_obj_update_layout(widget.obj);
    advance();
    return widget;
}


/* what the integration answers for an interval (invented figures) */
static parqet_perf_t sample(double end, double gain, double ret)
{
    parqet_perf_t p;
    memset(&p, 0, sizeof(p));
    p.value_start = end - gain;
    p.value_end = end;
    p.unrealized_gain = gain;
    p.unrealized_return = ret;
    p.realized_gain = -12.4;
    p.dividends = 211.2;
    p.xirr = 0.0;
    p.ttwror = ret;
    parqet_perf_holding_t alpha = {.value = 3170.0, .weight = 6.0, .ret = 2.1, .has_return = true};
    parqet_perf_holding_t beta = {.value = 2900.0, .weight = 5.5, .ret = -0.4, .has_return = true};
    parqet_perf_holding_t gamma = {.value = 2536.0, .weight = 4.8, .ret = 0.7, .has_return = true};
    parqet_perf_holding_t delta = {.value = 2325.0, .weight = 4.4, .ret = -1.5, .has_return = true};
    snprintf(alpha.name, sizeof(alpha.name), "Alpha Utilities");
    snprintf(beta.name, sizeof(beta.name), "Beta Global Dividend ETF");
    snprintf(gamma.name, sizeof(gamma.name), "Gamma Telecom");
    snprintf(delta.name, sizeof(delta.name), "Delta Consumer Holdings");
    p.largest_count = 2; p.largest[0] = alpha; p.largest[1] = beta;
    p.top_count = 2; p.top[0] = alpha; p.top[1] = gamma;
    p.flop_count = 2; p.flop[0] = delta; p.flop[1] = beta;
    p.point_count = 5;
    for (int i = 0; i < 5; i++) p.point[i] = (float)(end - gain + gain * i / 4.0);
    return p;
}

static void click(lv_obj_t *obj)
{
    CHECK(obj != NULL);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
    advance();
}

static void interval_cases(void)
{
    sim_parqet_clear();
    parqet_perf_t month = sample(52840.55, 2419.8, 4.8);
    parqet_perf_t week = sample(52000.0, 890.5, 1.7);
    sim_parqet_set("p1", "1m", &month);
    sim_parqet_set("p1", "1w", &week);

    /* an interval of the integration: its own figures, the interval named, the curve and four figures */
    s_selector = true;
    s_interval = "1m";
    ui_widget_instance_t w = make(400, false, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_gain", "8425.1", "{}");
    CHECK(find_prefix(w.obj, "52,841") != NULL);
    CHECK(find_prefix(w.obj, "+2,420") != NULL && find_prefix(w.obj, "+4.8 %") != NULL);   /* the interval's gain, not the sensors' +8,425 */
    CHECK(find_prefix(w.obj, "+8,425") == NULL);
    CHECK(find_prefix(w.obj, "TTWROR") != NULL && find_prefix(w.obj, "Dividends") != NULL && find_prefix(w.obj, "Realised") != NULL);
    CHECK(find_prefix(w.obj, "Performance") != NULL && find_prefix(w.obj, "Holdings") != NULL);
    CHECK(find_prefix(w.obj, "1D") != NULL && find_prefix(w.obj, "YTD") != NULL && find_prefix(w.obj, "MAX") != NULL);

    /* another chip asks for another interval; the answer of a week replaces the month's */
    click(find_prefix(w.obj, "1W"));
    CHECK(find_prefix(w.obj, "52,000") != NULL && find_prefix(w.obj, "+890") != NULL && find_prefix(w.obj, "+4.8 %") == NULL);

    /* Holdings: the interval's return of every holding, no amounts */
    click(find_prefix(w.obj, "Holdings"));
    CHECK(find_prefix(w.obj, "Alpha Utilities") != NULL && find_prefix(w.obj, "+2.1 %") != NULL && find_prefix(w.obj, "\xE2\x88\x92" "0.4 %") != NULL);
    CHECK(find_prefix(w.obj, "52,000") == NULL);
    /* the choice of holdings: the largest first, then the best and the worst by return */
    CHECK(find_prefix(w.obj, "Largest") != NULL && find_prefix(w.obj, "Top 5") != NULL && find_prefix(w.obj, "Flop 5") != NULL);
    CHECK(find_prefix(w.obj, "Gamma Telecom") == NULL && find_prefix(w.obj, "Delta Consumer") == NULL);
    click(find_prefix(w.obj, "Top 5"));
    CHECK(find_prefix(w.obj, "Gamma Telecom") != NULL && find_prefix(w.obj, "+0.7 %") != NULL && find_prefix(w.obj, "Beta Global") == NULL);
    click(find_prefix(w.obj, "Flop 5"));
    CHECK(find_prefix(w.obj, "Delta Consumer") != NULL && find_prefix(w.obj, "\xE2\x88\x92" "1.5 %") != NULL && find_prefix(w.obj, "Alpha Utilities") == NULL);
    click(find_prefix(w.obj, "Largest"));
    CHECK(find_prefix(w.obj, "Alpha Utilities") != NULL && find_prefix(w.obj, "Delta Consumer") == NULL);
    click(find_prefix(w.obj, "Performance"));
    CHECK(find_prefix(w.obj, "52,000") != NULL && find_prefix(w.obj, "Largest") == NULL);

    /* MAX is the sensors' figures again */
    click(find_prefix(w.obj, "MAX"));
    CHECK(find_prefix(w.obj, "52,841") != NULL && find_prefix(w.obj, "+8,425") != NULL);

    /* best and worst need an interval: the sensors only know the weights */
    click(find_prefix(w.obj, "Holdings"));
    click(find_prefix(w.obj, "Top 5"));
    CHECK(find_prefix(w.obj, "Pick an interval") != NULL);
    click(find_prefix(w.obj, "Largest"));
    CHECK(find_prefix(w.obj, "Pick an interval") == NULL);
    click(find_prefix(w.obj, "Performance"));

    /* an interval nothing answers for says so, and the tile is not left with the figures of another interval */
    click(find_prefix(w.obj, "1D"));
    CHECK(find_prefix(w.obj, "No answer") != NULL && find_prefix(w.obj, "52,841") == NULL);
    click(find_prefix(w.obj, "1M"));
    CHECK(find_prefix(w.obj, "+2,420") != NULL && find_prefix(w.obj, "No answer") == NULL);

    /* the options start the tile in a view and interval; percentages only */
    s_selector = false;
    s_interval = "1w";
    s_view = "holdings";
    w = make(330, false, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    CHECK(find_prefix(w.obj, "Alpha Utilities") != NULL && find_prefix(w.obj, "+2.1 %") != NULL && find_prefix(w.obj, "Performance") == NULL);
    s_view = "";
    w = make(330, true, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    CHECK(find_prefix(w.obj, "+1.7 %") != NULL && find_prefix(w.obj, "Dividends") == NULL && find_prefix(w.obj, "52,000") == NULL);
    s_interval = "";
    sim_parqet_clear();
}

int sim_parqet_regression(void)
{
    sim_host_set_online(true);

    /* nothing received yet */
    ui_widget_instance_t w = make(330, false, true);
    CHECK(find_prefix(w.obj, "No data") != NULL);

    /* value, gain, XIRR and dividends, the interval and the largest holdings */
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_gain", "8425.1", "{}");
    apply(&w, "sensor.pq_xirr", "9.8", "{}");
    apply(&w, "sensor.pq_div", "1820.4", "{}");
    apply(&w, "sensor.pq_ret", "27.72", "{}");
    CHECK(find_prefix(w.obj, "No data") == NULL);
    CHECK(find_prefix(w.obj, "52,841") != NULL);                 /* the value, rounded to whole euros */
    lv_obj_t *gain = find_prefix(w.obj, "+8,425");
    CHECK(gain != NULL && strstr(lv_label_get_text(gain), "+27.7 %") != NULL); /* the return sensor, not a computed share */
    CHECK(find_prefix(w.obj, "Max") != NULL);
    CHECK(find_prefix(w.obj, "XIRR") != NULL && find_prefix(w.obj, "+9.8 %") != NULL);
    CHECK(find_prefix(w.obj, "Dividends") != NULL && find_prefix(w.obj, "1,820") != NULL);
    CHECK(find_prefix(w.obj, "Alpha Utilities") != NULL && find_prefix(w.obj, "6.0 %") != NULL);

    /* an unavailable sensor takes its figure away, not the rest */
    apply(&w, "sensor.pq_div", "unavailable", "{}");
    CHECK(find_prefix(w.obj, "Dividends") == NULL && find_prefix(w.obj, "XIRR") != NULL);
    apply(&w, "sensor.pq_div", "1820.4", "{}");
    CHECK(find_prefix(w.obj, "Dividends") != NULL);

    /* the tile names another interval; the figures stay those of the sensors */
    apply(&w, "sensor.pq_value", "52840.55", "{\"interval\":\"1y\",\"unit_of_measurement\":\"EUR\"}");
    CHECK(find_prefix(w.obj, "1Y") != NULL && find_prefix(w.obj, "Max") == NULL);
    CHECK(find_prefix(w.obj, "+8,425") != NULL);
    CHECK(find_prefix(w.obj, "Alpha Utilities") == NULL);        /* the attribute list went away with the new attributes */

    /* the whole state of the integration becomes unavailable */
    w_parqet_portfolio_mark_unavailable(&w);
    advance();
    CHECK(find_prefix(w.obj, "No data") != NULL && find_prefix(w.obj, "52,841") == NULL);

    /* a loss, with a real minus sign */
    w = make(330, false, true);
    apply(&w, "sensor.pq_value", "18400.2", "{\"interval\":\"max\",\"unit_of_measurement\":\"EUR\"}");
    apply(&w, "sensor.pq_gain", "-1210.75", "{}");
    apply(&w, "sensor.pq_xirr", "-4.2", "{}");
    apply(&w, "sensor.pq_ret", "-6.17", "{}");
    gain = find_prefix(w.obj, "\xE2\x88\x92" "1,211");
    CHECK(gain != NULL && strstr(lv_label_get_text(gain), "\xE2\x88\x92" "6.2 %") != NULL);
    CHECK(find_prefix(w.obj, "\xE2\x88\x92" "4.2 %") != NULL);

    /* percentages only: the return replaces the value, no amount and no dividends show */
    w = make(330, true, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_gain", "8425.1", "{}");
    apply(&w, "sensor.pq_xirr", "9.8", "{}");
    apply(&w, "sensor.pq_div", "1820.4", "{}");
    apply(&w, "sensor.pq_ret", "27.72", "{}");
    CHECK(find_prefix(w.obj, "+27.7 %") != NULL);
    CHECK(find_prefix(w.obj, "52,841") == NULL && find_prefix(w.obj, "+8,425") == NULL && find_prefix(w.obj, "Dividends") == NULL);
    CHECK(find_prefix(w.obj, "XIRR") != NULL);

    /* without a return sensor the percentages-only tile shows the XIRR, and the value tile no percent */
    w = make(330, true, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_xirr", "9.8", "{}");
    CHECK(find_prefix(w.obj, "+9.8 %") != NULL && find_prefix(w.obj, "52,841") == NULL);

    /* a small tile shows the value and the gain, not the holdings; without sensors there is only the value */
    w = make(170, false, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_gain", "8425.1", "{}");
    CHECK(find_prefix(w.obj, "52,841") != NULL && find_prefix(w.obj, "Alpha Utilities") == NULL);
    w = make(330, false, false);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    CHECK(find_prefix(w.obj, "52,841") != NULL && find_prefix(w.obj, "XIRR") == NULL && find_prefix(w.obj, "Alpha Utilities") != NULL);

    /* an amount sensor in another currency, or a percentage sensor in another unit, is left out and named; the others stay */
    w = make(330, false, true);
    apply(&w, "sensor.pq_value", "52840.55", ATTRS_MAX);
    apply(&w, "sensor.pq_gain", "8425.1", "{\"unit_of_measurement\":\"USD\"}");
    apply(&w, "sensor.pq_div", "1820.4", "{\"unit_of_measurement\":\"EUR\"}");
    apply(&w, "sensor.pq_xirr", "9.8", "{\"unit_of_measurement\":\"EUR\"}");
    CHECK(find_prefix(w.obj, "+8,425") == NULL && find_prefix(w.obj, "Unit differs") != NULL);
    CHECK(find_prefix(w.obj, "Dividends") != NULL && find_prefix(w.obj, "1,820") != NULL);
    CHECK(find_prefix(w.obj, "XIRR") == NULL && find_prefix(w.obj, "+9.8 %") == NULL);
    apply(&w, "sensor.pq_gain", "8425.1", "{\"unit_of_measurement\":\"EUR\"}");
    apply(&w, "sensor.pq_xirr", "9.8", "{\"unit_of_measurement\":\"%\"}");
    CHECK(find_prefix(w.obj, "+8,425") != NULL && find_prefix(w.obj, "Unit differs") == NULL);
    CHECK(find_prefix(w.obj, "XIRR") != NULL && find_prefix(w.obj, "+9.8 %") != NULL);

    interval_cases();

    printf("parqet portfolio: %u checks passed\n", checks);
    return 0;
}
