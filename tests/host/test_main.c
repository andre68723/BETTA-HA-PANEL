/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host tests: pure logic that runs on the development machine without a device (tools/test_host.sh).
 * A minimal runner, no framework: CHECK() counts failures, main() returns non-zero if there are any.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Only this executable's calendar is fixed; monotonic deadlines remain real. */
time_t host_test_time(time_t *out)
{
    const time_t now = 1792058400; /* Thursday 2026-10-15 */
    if (out != NULL) *out = now;
    return now;
}

#include "app_config.h"
#include "cJSON.h"
#include "ha/ha_attrs_compact.h"
#include "ha/ha_attr_serialize.h"
#include "ha/ha_layout_entities.h"
#include "ha/ha_display_precision.h"
#include "ha/ha_discovery_domains.h"
#include "ha/ha_datetime_value.h"
#include "ha/ha_history_reduce.h"
#include "util/distance_units.h"
#include "util/iso_time.h"
#include "util/room_extra.h"
#include "ha/energy_timeline_model.h"
#include <math.h>
#include "ha/ha_auth_state.h"
#include "ha/ha_select_options.h"
#include "ha/ha_lock_state.h"
#include "ha/ha_room_registry.h"
#include "settings/settings_validate.h"
#include "util/atomic_file.h"
#include "drivers/display_policy.h"
#include "util/cover_guard.h"
#include "util/json_util.h"
#include "util/cover_scale.h"
#include "util/url_decode.h"
#include "net/setup_passphrase.h"
#include "layout/layout_validate.h"
bool test_energy_timeline_colors(void);
bool test_sensor_value(void);
bool test_quote(void);
bool test_timestamp_format(void);
#include "layout/widget_types.h"
#include "settings/brightness_model.h"
#include "settings/settings_restart.h"
#include "ui/widgets/weather_value_parse.h"
#include "ui/widgets/weather_condition_icon.h"
#include "ui/theme/matte_title.h"
#include "ui/theme/ui_style_registry.h"
#include "net/update_check_parse.h"
#include "util/ota_health.h"
#include "util/auto_update.h"
#include "util/sha256_soft.h"
#include "util/auto_update_state.h"
#include "util/user_activity.h"
#include "util/chart_scale.h"
#include "util/reboot_log.h"
#include "util/net_watchdog.h"
#include "util/ui_watchdog.h"
#include "util/ota_chip_rev.h"
#include "util/history_feed_policy.h"
#include "ui/nav_layout.h"
#include "net/wifi_signal_level.h"
#include "util/boot_guard.h"
#include "util/entity_id.h"
#include "util/notification_parse.h"

static int s_checks = 0;
static int s_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        s_checks++;                                                        \
        if (!(cond)) {                                                     \
            s_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define CHECK_INT(actual, expected)                                                                    \
    do {                                                                                               \
        const int a_ = (actual);                                                                       \
        const int e_ = (expected);                                                                     \
        s_checks++;                                                                                    \
        if (a_ != e_) {                                                                                \
            s_failures++;                                                                              \
            printf("FAIL %s:%d: %s is %d, expected %d\n", __FILE__, __LINE__, #actual, a_, e_);        \
        }                                                                                              \
    } while (0)

/* ---- brightness model ------------------------------------------------------------------------ */

static void test_brightness(void)
{
    CHECK_INT(brightness_clamp_level(0), 1);
    CHECK_INT(brightness_clamp_level(-7), 1);
    CHECK_INT(brightness_clamp_level(55), 55);
    CHECK_INT(brightness_clamp_level(150), 100);
    CHECK_INT(brightness_clamp_night(-5), 0);
    CHECK_INT(brightness_clamp_night(0), 0);
    CHECK_INT(brightness_clamp_night(120), 100);

    /* The order night <= dimmed <= active is enforced, the lower levels follow. */
    brightness_levels_t l = {20, 27, 10};
    brightness_levels_normalize(&l);
    CHECK_INT(l.active, 20);
    CHECK_INT(l.dimmed, 20);
    CHECK_INT(l.night, 10);

    l = (brightness_levels_t){100, 50, 60};
    brightness_levels_normalize(&l);
    CHECK_INT(l.dimmed, 50);
    CHECK_INT(l.night, 50);

    l = (brightness_levels_t){0, 0, 0};
    brightness_levels_normalize(&l);
    CHECK_INT(l.active, 1);
    CHECK_INT(l.dimmed, 1);
    CHECK_INT(l.night, 0); /* night may be off */

    l = (brightness_levels_t){200, -3, 999};
    brightness_levels_normalize(&l);
    CHECK_INT(l.active, 100);
    CHECK_INT(l.dimmed, 1);
    CHECK_INT(l.night, 1);

    /* Room light scaling: proportional, never below 5 % (or the level itself when lower). */
    CHECK_INT(brightness_scale_for_room_light(80, 100), 80);
    CHECK_INT(brightness_scale_for_room_light(80, 10), 8);
    CHECK_INT(brightness_scale_for_room_light(75, 50), 38); /* 37.5 rounds up */
    CHECK_INT(brightness_scale_for_room_light(30, 10), 5);  /* 3 -> floor 5 */
    CHECK_INT(brightness_scale_for_room_light(3, 10), 3);   /* level lower than the floor */
    CHECK_INT(brightness_scale_for_room_light(1, 10), 1);
    CHECK_INT(brightness_scale_for_room_light(50, 250), 50); /* factor above 100 % is capped */
    CHECK_INT(brightness_scale_for_room_light(50, -20), 5);  /* negative factor counts as 0 */

    /* Modes: night is as configured (also 0), active and dimmed follow the room light. */
    const brightness_levels_t set = {80, 30, 10};
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_ACTIVE, 100), 80);
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_DIMMED, 100), 30);
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_NIGHT, 100), 10);
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_ACTIVE, 20), 16);
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_DIMMED, 20), 6);
    CHECK_INT(brightness_resolve(&set, BRIGHTNESS_MODE_NIGHT, 20), 10); /* night is not scaled */
    const brightness_levels_t off = {80, 30, 0};
    CHECK_INT(brightness_resolve(&off, BRIGHTNESS_MODE_NIGHT, 10), 0);
    CHECK_INT(brightness_resolve(NULL, BRIGHTNESS_MODE_ACTIVE, 100), 100);
}

/* ---- layout validation ----------------------------------------------------------------------- */

/* A one-widget layout; `body` is the widget JSON without braces. */
static int validate_widget(const char *body, layout_validation_result_t *result)
{
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"version\":1,\"pages\":[{\"id\":\"p1\",\"title\":\"T\",\"widgets\":[{\"id\":\"w1\",%s}]}]}", body);
    layout_validation_clear(result);
    return layout_validate_json(json, result) ? 1 : 0;
}

static void expect_valid(const char *label, const char *body)
{
    layout_validation_result_t r;
    const int ok = validate_widget(body, &r);
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("FAIL %s should be valid: %s\n", label, r.count > 0 ? r.messages[0] : "(no message)");
    }
}

static void expect_invalid(const char *label, const char *body, const char *message_part)
{
    layout_validation_result_t r;
    const int ok = validate_widget(body, &r);
    s_checks++;
    if (ok) {
        s_failures++;
        printf("FAIL %s should be rejected\n", label);
        return;
    }
    if (message_part != NULL) {
        bool found = false;
        for (uint16_t i = 0; i < r.count; i++) {
            if (strstr(r.messages[i], message_part) != NULL) {
                found = true;
            }
        }
        s_checks++;
        if (!found) {
            s_failures++;
            printf("FAIL %s: no message contains \"%s\" (first: %s)\n", label, message_part,
                r.count > 0 ? r.messages[0] : "(none)");
        }
    }
}

static void test_layout(void)
{
    layout_validation_result_t r;
    layout_validation_clear(&r);
    CHECK(!layout_validate_json("{\"version\":2,\"pages\":[]}", &r));
    layout_validation_clear(&r);
    CHECK(!layout_validate_json("not json", &r));

    /* the Energy page's detail presentation: popup (default, not stored) or side; anything else is refused */
    {
        const char *tpl = "{\"version\":1,\"pages\":[{\"id\":\"e1\",\"title\":\"E\",\"type\":\"energy_dashboard\","
                          "\"widgets\":[],\"energy\":{\"source\":\"ha_energy\",\"details\":%s}}]}";
        const char *values[] = {"\"side\"", "\"popup\"", "\"wide\"", "1", "null"};
        const bool accepted[] = {true, true, false, false, false};
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            char json[256];
            snprintf(json, sizeof(json), tpl, values[i]);
            layout_validation_clear(&r);
            const bool ok = layout_validate_json(json, &r);
            if (ok != accepted[i]) { s_failures++; printf("FAIL energy.details %s: validator said %d\n", values[i], (int)ok); }
        }
    }

    const char *rect = "\"rect\":{\"x\":0,\"y\":0,\"w\":290,\"h\":200}";
    const char *row = "\"rect\":{\"x\":0,\"y\":0,\"w\":260,\"h\":90}"; /* binary and contact tiles are at most 160 px high */
    char body[1024];

#define W(fmt, ...) (snprintf(body, sizeof(body), fmt, __VA_ARGS__), body)
    expect_valid("light tile", W("\"type\":\"light_tile\",\"title\":\"L\",\"entity_id\":\"light.a\",%s", rect));
    expect_valid("alarm tile actions", W("\"type\":\"alarm_tile\",\"title\":\"A\",\"entity_id\":\"alarm_control_panel.a\",\"alarm_actions\":[\"arm_home\"],%s", rect));
    expect_invalid("alarm tile bad action", W("\"type\":\"alarm_tile\",\"title\":\"A\",\"entity_id\":\"alarm_control_panel.a\",\"alarm_actions\":[\"trigger\"],%s", rect), "alarm_actions");
    expect_valid("sensor format", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_format\":\"relative\",%s", rect));
    expect_valid("sensor duration format", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_format\":\"clock\",%s", rect));
    expect_invalid("sensor bad format", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_format\":\"iso\",%s", rect), "sensor_format");
    expect_invalid("sensor empty format", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_format\":\"\",%s", rect), "sensor_format");
    expect_valid("sensor unit", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_unit\":\"degree\",%s", rect));
    expect_invalid("sensor bad unit", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_unit\":\"kW\",%s", rect), "sensor_unit");
    expect_invalid("sensor empty unit", W("\"type\":\"sensor\",\"title\":\"S\",\"entity_id\":\"sensor.a\",\"sensor_unit\":\"\",%s", rect), "sensor_unit");
    expect_invalid("unknown type", W("\"type\":\"nope\",\"title\":\"L\",\"entity_id\":\"light.a\",%s", rect), NULL);
    expect_invalid("light tile with a sensor", W("\"type\":\"light_tile\",\"title\":\"L\",\"entity_id\":\"sensor.a\",%s", rect), "must be light.*");
    expect_invalid("button with a light", W("\"type\":\"button\",\"title\":\"B\",\"entity_id\":\"light.a\",%s", rect), "must be switch.* or media_player.*");
    expect_invalid("too small",
        "\"type\":\"light_tile\",\"title\":\"L\",\"entity_id\":\"light.a\",\"rect\":{\"x\":0,\"y\":0,\"w\":10,\"h\":10}", NULL);

    const char *forecast_rect = "\"rect\":{\"x\":0,\"y\":0,\"w\":320,\"h\":300}";
    expect_valid("legacy forecast", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",%s", forecast_rect));
    expect_valid("auto forecast", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":0,%s", forecast_rect));
    expect_valid("six days", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":6,%s", forecast_rect));
    expect_valid("seven days", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":7,%s", forecast_rect));
    expect_valid("solar seven days with day 6 and 7", W("\"type\":\"solar_forecast\",\"entity_id\":\"sensor.a\",\"forecast_day_6_entity_id\":\"sensor.d6\",\"forecast_day_7_entity_id\":\"sensor.d7\",\"forecast_days\":7,%s", forecast_rect));
    expect_invalid("solar day 7 needs a sensor", W("\"type\":\"solar_forecast\",\"entity_id\":\"sensor.a\",\"forecast_day_7_entity_id\":\"light.d7\",%s", forecast_rect), "forecast_day_7_entity_id");
    expect_invalid("fractional days", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":2.5,%s", forecast_rect), "forecast_days");
    expect_invalid("too many days", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":8,%s", forecast_rect), "forecast_days");
    expect_invalid("string days", W("\"type\":\"weather_3day\",\"entity_id\":\"weather.a\",\"forecast_days\":\"auto\",%s", forecast_rect), "forecast_days");
    expect_invalid("bad alignment", W("\"type\":\"solar_forecast\",\"entity_id\":\"sensor.a\",\"align_forecast\":1,%s", forecast_rect), "align_forecast");
    /* Binary tile: door look, and the legacy contact tile. */
    expect_valid("binary door look",
        W("\"type\":\"binary_tile\",\"title\":\"D\",\"entity_id\":\"binary_sensor.d\",\"display_mode\":\"door\",%s", row));
    expect_valid("binary event", W("\"type\":\"binary_tile\",\"title\":\"B\",\"entity_id\":\"event.b\",%s", row));
    expect_invalid("binary bad display mode",
        W("\"type\":\"binary_tile\",\"title\":\"D\",\"entity_id\":\"binary_sensor.d\",\"display_mode\":\"bogus\",%s", row),
        "display_mode");
    expect_valid("legacy contact tile", W("\"type\":\"contact_tile\",\"title\":\"D\",\"entity_id\":\"binary_sensor.d\",%s", row));
    expect_valid("contact history", W("\"type\":\"contact_tile\",\"title\":\"D\",\"entity_id\":\"binary_sensor.d\",\"history_popup\":true,%s", row));
    expect_invalid("history must be boolean", W("\"type\":\"binary_tile\",\"title\":\"D\",\"entity_id\":\"binary_sensor.d\",\"history_popup\":\"yes\",%s", row), "history_popup");
    expect_invalid("history on unsupported tile", W("\"type\":\"scene_tile\",\"title\":\"D\",\"entity_id\":\"scene.d\",\"history_popup\":true,%s", row), "history_popup");

    /* Energy timeline: statistic ids of energy counters, grid import required, scales up to a whole page. */
    const char *timeline_rect = "\"rect\":{\"x\":0,\"y\":0,\"w\":320,\"h\":220}";
    expect_valid("energy timeline",
        W("\"type\":\"energy_timeline\",\"title\":\"T\",\"entity_id\":\"sensor.imp\",\"secondary_entity_id\":\"sensor.exp\","
          "\"tertiary_entity_id\":\"sensor.sun\",%s", timeline_rect));
    CHECK(test_energy_timeline_colors());
    expect_valid("energy timeline grid only", W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",%s", timeline_rect));
    expect_valid("energy timeline full page",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"rect\":{\"x\":0,\"y\":0,\"w\":%d,\"h\":%d}",
          APP_CONTENT_BOX_WIDTH, APP_CONTENT_BOX_HEIGHT));
    expect_invalid("energy timeline without grid import", W("\"type\":\"energy_timeline\",%s", timeline_rect), "grid import");
    expect_invalid("energy timeline export not a sensor",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"secondary_entity_id\":\"light.x\",%s", timeline_rect), "grid export");
    expect_valid("energy timeline with battery and forecast",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"battery_charge_entity_id\":\"sensor.c\","
          "\"battery_discharge_entity_id\":\"sensor.d\",\"battery_soc_entity_id\":\"sensor.soc\","
          "\"forecast_today_entity_id\":\"sensor.fc\",%s", timeline_rect));
    expect_valid("energy timeline nested rings",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"style_variant\":\"nested\",%s", timeline_rect));
    expect_invalid("energy timeline unknown look",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"style_variant\":\"donut\",%s", timeline_rect),
        "default|nested");
    expect_invalid("energy timeline half a battery",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"battery_charge_entity_id\":\"sensor.c\",%s", timeline_rect),
        "go together");
    expect_invalid("energy timeline soc not a sensor",
        W("\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"battery_soc_entity_id\":\"light.x\",%s", timeline_rect),
        "battery_soc_entity_id");
    expect_invalid("energy timeline too small",
        "\"type\":\"energy_timeline\",\"entity_id\":\"sensor.imp\",\"rect\":{\"x\":0,\"y\":0,\"w\":200,\"h\":150}", NULL);

    /* Stocks & ETFs: up to eight sensors, its options. */
    const char *stock_rect = "\"rect\":{\"x\":0,\"y\":0,\"w\":300,\"h\":200}";
    expect_valid("stock quotes list", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_entities\":"
        "[\"sensor.b\",\"sensor.c\"],\"stock_view\":\"grid\",\"stock_period\":\"ytd\",\"stock_delta\":\"both\","
        "\"stock_ticker\":true,%s", stock_rect));
    expect_invalid("stock quotes nine", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_entities\":"
        "[\"sensor.b\",\"sensor.c\",\"sensor.d\",\"sensor.e\",\"sensor.f\",\"sensor.g\",\"sensor.h\",\"sensor.i\"],%s", stock_rect),
        "at most 7");
    expect_invalid("stock quotes not a sensor", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_entities\":"
        "[\"light.b\"],%s", stock_rect), "stock_entities");
    expect_invalid("stock quotes period", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_period\":\"max\",%s",
        stock_rect), "stock_period");
    expect_invalid("stock quotes view", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_view\":\"tiles\",%s",
        stock_rect), "stock_view");
    expect_invalid("stock quotes ticker", W("\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"stock_ticker\":1,%s",
        stock_rect), "stock_ticker");
    expect_invalid("stock quotes without a quote", W("\"type\":\"stock_quotes\",%s", stock_rect), NULL);

    /* Room card extra field: old layouts, valid choices, and every way to get it wrong. */
    const char *room_rect = "\"rect\":{\"x\":0,\"y\":0,\"w\":240,\"h\":140}";
    expect_valid("room card without an extra field", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",%s", room_rect));
    expect_valid("room card default extra", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_extra_mode\":\"default\",%s", room_rect));
    expect_valid("room card humidity", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"sensor.h\"],"
                                       "\"room_extra_mode\":\"humidity\",\"room_extra_entity_id\":\"sensor.h\",%s", room_rect));
    expect_valid("room card cover on the primary entity", W("\"type\":\"room_tile\",\"entity_id\":\"cover.c\","
                                       "\"room_extra_mode\":\"cover\",\"room_extra_entity_id\":\"cover.c\",%s", room_rect));
    expect_valid("room card media player", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"light.l\",\"media_player.tv\"],"
                                       "\"room_extra_mode\":\"media_player\",\"room_extra_entity_id\":\"media_player.tv\",%s", room_rect));
    expect_invalid("room card unknown extra mode", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_extra_mode\":\"lights\",%s", room_rect),
                   "room_extra_mode must be");
    expect_invalid("room card extra without entity", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_extra_mode\":\"cover\",%s", room_rect),
                   "room_extra_entity_id is required");
    expect_invalid("room card wrong domain", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"light.l\"],"
                                       "\"room_extra_mode\":\"cover\",\"room_extra_entity_id\":\"light.l\",%s", room_rect), "does not fit");
    expect_invalid("room card extra not on the card", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"sensor.h\"],"
                                       "\"room_extra_mode\":\"humidity\",\"room_extra_entity_id\":\"sensor.other\",%s", room_rect),
                   "must be the room");
    expect_invalid("room card extra mode not a string", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_extra_mode\":3,%s", room_rect),
                   "room_extra_mode must be");
    expect_valid("room card controls on", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"light.l\"],\"room_controls\":true,%s", room_rect));
    expect_invalid("room card controls not a boolean", W("\"type\":\"room_tile\",\"entity_id\":\"sensor.t\",\"room_controls\":\"yes\",%s", room_rect),
                   "room_controls must be true or false");

    /* The same fields on the rooms of a house dashboard page. */
    {
        layout_validation_result_t hr;
        const char *good = "{\"version\":1,\"pages\":[{\"id\":\"h\",\"type\":\"house_dashboard\",\"title\":\"H\",\"house\":{\"rooms\":["
            "{\"name\":\"Kitchen\",\"entity_id\":\"sensor.t\",\"room_entities\":[\"cover.c\"],"
            "\"room_extra_mode\":\"cover\",\"room_extra_entity_id\":\"cover.c\"},"
            "{\"name\":\"Old room\",\"entity_id\":\"sensor.t2\"}]}}]}";
        layout_validation_clear(&hr);
        s_checks++;
        if (!layout_validate_json(good, &hr)) { s_failures++; printf("FAIL house rooms with an extra field: %s\n", hr.count ? hr.messages[0] : "?"); }
        const char *bad = "{\"version\":1,\"pages\":[{\"id\":\"h\",\"type\":\"house_dashboard\",\"title\":\"H\",\"house\":{\"rooms\":["
            "{\"name\":\"Kitchen\",\"entity_id\":\"sensor.t\",\"room_extra_mode\":\"cover\",\"room_extra_entity_id\":\"cover.absent\"}]}}]}";
        layout_validation_clear(&hr);
        s_checks++;
        if (layout_validate_json(bad, &hr) || hr.count == 0 || strstr(hr.messages[0], "must be the room") == NULL) {
            s_failures++;
            printf("FAIL a house room's extra entity outside the card is rejected (%d: %s)\n", (int)hr.count, hr.count ? hr.messages[0] : "-");
        }
    }

    /* Fan tile may be tall (large look). */
    const int tall_height = APP_CONTENT_BOX_HEIGHT < 420 ? APP_CONTENT_BOX_HEIGHT : 420;
    snprintf(body, sizeof(body),
        "\"type\":\"fan_tile\",\"title\":\"F\",\"entity_id\":\"fan.f\",\"rect\":{\"x\":0,\"y\":0,\"w\":340,\"h\":%d}",
        tall_height);
    expect_valid("large fan", body);

    /* Car tile extras: pre-conditioning, lock status, interior temperature. */
    char car[256];
    snprintf(car, sizeof(car),
        "\"type\":\"car_widget\",\"title\":\"C\",\"entity_id\":\"sensor.soc\",\"rect\":{\"x\":0,\"y\":0,\"w\":330,\"h\":%d}",
        tall_height);
    expect_valid("car without extras", W("%s", car));
    expect_valid("car with all extras",
        W("%s,\"precond_entity_id\":\"climate.c\",\"lock_entity_id\":\"binary_sensor.l\",\"interior_temp_entity_id\":\"sensor.t\"", car));
    expect_valid("car precond select", W("%s,\"precond_entity_id\":\"select.seat\"", car));
    expect_valid("car lock entity", W("%s,\"lock_entity_id\":\"lock.door\"", car));
    expect_invalid("car precond wrong domain", W("%s,\"precond_entity_id\":\"sensor.x\"", car), "precond_entity_id");
    expect_invalid("car lock wrong domain", W("%s,\"lock_entity_id\":\"light.x\"", car), "lock_entity_id");
    expect_invalid("car temp wrong domain", W("%s,\"interior_temp_entity_id\":\"switch.x\"", car), "interior_temp_entity_id");

    /* Vacuum tile: optional battery sensor and the long-press history popup. */
    char vac[256];
    snprintf(vac, sizeof(vac),
        "\"type\":\"vacuum_tile\",\"title\":\"V\",\"entity_id\":\"vacuum.v\",\"rect\":{\"x\":0,\"y\":0,\"w\":300,\"h\":%d}", tall_height);
    expect_valid("vacuum with a battery sensor", W("%s,\"battery_entity_id\":\"sensor.v_battery\"", vac));
    expect_valid("vacuum with an empty battery sensor", W("%s,\"battery_entity_id\":\"\"", vac));
    expect_valid("vacuum with history on long press", W("%s,\"history_popup\":true", vac));
    expect_invalid("vacuum battery must be a sensor", W("%s,\"battery_entity_id\":\"light.v\"", vac), "battery_entity_id");
    expect_invalid("vacuum battery must be a string", W("%s,\"battery_entity_id\":7", vac), "battery_entity_id");

    /* Bambu printer: ten explicit bindings, sensors for readings and buttons for the three actions. */
    char bambu[256];
    snprintf(bambu, sizeof(bambu),
        "\"type\":\"bambu_printer\",\"title\":\"P\",\"entity_id\":\"sensor.p_status\",\"rect\":{\"x\":0,\"y\":0,\"w\":360,\"h\":%d}",
        tall_height);
    expect_valid("bambu without bindings", W("%s", bambu));
    expect_valid("bambu with every binding",
        W("%s,\"nozzle_temp_entity_id\":\"sensor.n\",\"bed_temp_entity_id\":\"sensor.b\",\"progress_entity_id\":\"sensor.p\","
          "\"remaining_time_entity_id\":\"sensor.r\",\"pause_entity_id\":\"button.a\",\"resume_entity_id\":\"button.b\","
          "\"stop_entity_id\":\"button.c\",\"stage_entity_id\":\"sensor.s\",\"job_name_entity_id\":\"sensor.j\"", bambu));
    expect_valid("bambu empty optional binding", W("%s,\"stage_entity_id\":\"\"", bambu));
    expect_invalid("bambu pause must be a button", W("%s,\"pause_entity_id\":\"switch.a\"", bambu), "pause_entity_id");
    expect_invalid("bambu nozzle must be a sensor", W("%s,\"nozzle_temp_entity_id\":\"button.a\"", bambu), "nozzle_temp_entity_id");
    expect_invalid("bambu binding must be a string", W("%s,\"bed_temp_entity_id\":5", bambu), "bed_temp_entity_id");
    expect_invalid("bambu action buttons must differ",
        W("%s,\"pause_entity_id\":\"button.a\",\"stop_entity_id\":\"button.a\"", bambu), "three different buttons");
    expect_invalid("bambu rejects a status of the wrong domain",
        W("\"type\":\"bambu_printer\",\"entity_id\":\"light.p\",\"rect\":{\"x\":0,\"y\":0,\"w\":360,\"h\":%d}", tall_height), "entity");
    expect_invalid("bambu too small", "\"type\":\"bambu_printer\",\"entity_id\":\"sensor.p\",\"rect\":{\"x\":0,\"y\":0,\"w\":300,\"h\":300}", "size");

    expect_valid("graph from the Recorder", W("\"type\":\"graph\",\"entity_id\":\"sensor.a\",\"graph_ha_history\":true,%s", rect));
    expect_invalid("graph recorder flag must be boolean", W("\"type\":\"graph\",\"entity_id\":\"sensor.a\",\"graph_ha_history\":1,%s", rect), "graph_ha_history");
    expect_valid("name hidden", W("\"type\":\"light_tile\",\"entity_id\":\"light.a\",\"show_name\":false,%s", rect));
    expect_invalid("name visibility must be boolean", W("\"type\":\"light_tile\",\"entity_id\":\"light.a\",\"show_name\":0,%s", rect), "show_name");
    const char *name_cases[] = {"{}", "{\"type\":\"sensor\",\"sensor_show_label\":false}",
        "{\"type\":\"sensor\",\"sensor_show_label\":false,\"show_name\":true}",
        "{\"type\":\"gauge\",\"sensor_show_label\":false}", "{\"show_name\":false}",
        "{\"type\":\"weather_tile\"}", "{\"type\":\"weather_3day\"}",
        "{\"type\":\"weather_tile\",\"show_name\":true}", "{\"type\":\"weather_3day\",\"show_name\":false}"};
    const bool name_expected[] = {true, false, true, true, false, false, false, true, false};
    for (unsigned i = 0; i < 9; i++) {
        cJSON *name = cJSON_Parse(name_cases[i]);
        CHECK(widget_show_name(name) == name_expected[i]);
        cJSON_Delete(name);
    }
    CHECK(widget_type_supports_name("gauge"));
    CHECK(widget_type_supports_name("datetime_tile"));
    CHECK(!widget_type_supports_name("unknown"));

    /* Linear gauge LED blocks: gauge_segment_count within GAUGE_SEGMENT_COUNT_MIN..MAX. */
    const char *gauge = "\"type\":\"gauge\",\"title\":\"G\",\"entity_id\":\"sensor.g\",\"style_variant\":\"linear\","
                        "\"gauge_bar_style\":\"segmented\",\"rect\":{\"x\":0,\"y\":0,\"w\":160,\"h\":300}";
    expect_valid("gauge default segments", W("%s", gauge));
    expect_valid("gauge 30 segments", W("%s,\"gauge_segment_count\":30", gauge));
    expect_valid("gauge 4 segments", W("%s,\"gauge_segment_count\":4", gauge));
    expect_invalid("gauge 31 segments", W("%s,\"gauge_segment_count\":31", gauge), "gauge_segment_count");
    expect_invalid("gauge 3 segments", W("%s,\"gauge_segment_count\":3", gauge), "gauge_segment_count");
    expect_invalid("gauge fractional segments", W("%s,\"gauge_segment_count\":12.5", gauge), "gauge_segment_count");
    expect_invalid("gauge segments as text", W("%s,\"gauge_segment_count\":\"20\"", gauge), "gauge_segment_count");
    const char *gauge_h = "\"type\":\"gauge\",\"title\":\"G\",\"entity_id\":\"sensor.g\",\"style_variant\":\"horizontal\","
                          "\"rect\":{\"x\":0,\"y\":0,\"w\":300,\"h\":100}";
    expect_valid("horizontal gauge", W("%s", gauge_h));
    expect_valid("horizontal gauge with 20 blocks", W("%s,\"gauge_bar_style\":\"segmented\",\"gauge_segment_count\":20", gauge_h));
    expect_invalid("gauge unknown style",
        W("\"type\":\"gauge\",\"title\":\"G\",\"entity_id\":\"sensor.g\",\"style_variant\":\"diagonal\",%s", rect),
        "style_variant");
#undef W
}

/* ---- entity ids and notification actions ----------------------------------------------------- */

static void test_entity_ids(void)
{
    CHECK(entity_id_valid("light.kitchen_1"));
    CHECK(!entity_id_valid("light"));
    CHECK(!entity_id_valid("light."));
    CHECK(!entity_id_valid(".kitchen"));
    CHECK(!entity_id_valid("light.kitchen.extra"));
    CHECK(!entity_id_valid("Light.kitchen"));
    CHECK(!entity_id_valid("light.kit chen"));
    CHECK(!entity_id_valid(NULL));

    /* The id goes into a JSON service call: quotes, commas and braces must never pass. */
    CHECK(!entity_id_valid("light.x\",\"brightness\":255,\"a\":\"b"));
    CHECK(!entity_id_valid("light.a,switch.b"));
    CHECK(!entity_id_valid("light.a}"));

    CHECK(entity_id_action_allowed("button.doorbell"));
    CHECK(entity_id_action_allowed("lock.front_door"));
    CHECK(entity_id_action_allowed("script.lights_off"));
    CHECK(!entity_id_action_allowed("automation.night"));
    CHECK(!entity_id_action_allowed("climate.heater"));
    CHECK(!entity_id_action_allowed("homeassistant.turn_on"));
    CHECK(!entity_id_action_allowed("lock.front_door\"}"));

    app_notification_t n = {0};
    strcpy(n.action_entity_id, "lock.front_door");
    strcpy(n.action_label, "Open");
    strcpy(n.camera_entity_id, "camera.door");
    notification_sanitize_entities(&n);
    CHECK(strcmp(n.action_entity_id, "lock.front_door") == 0 && strcmp(n.camera_entity_id, "camera.door") == 0);
    strcpy(n.action_entity_id, "automation.x");
    strcpy(n.camera_entity_id, "sensor.x");
    notification_sanitize_entities(&n);
    CHECK(n.action_entity_id[0] == '\0' && n.action_label[0] == '\0' && n.camera_entity_id[0] == '\0');

    /* action_service: "open" unlatches a lock; only unlock/open, and only for a lock. */
    app_notification_t *parsed = NULL;
    const char *err = NULL;
    const char *open_json = "{\"message\":\"Ding\",\"action_entity_id\":\"lock.front_door\",\"action_service\":\"open\"}";
    CHECK(notification_parse_json(open_json, strlen(open_json), &parsed, &err));
    CHECK(parsed != NULL && parsed->action_open);
    free(parsed);
    parsed = NULL;
    const char *plain_json = "{\"message\":\"Ding\",\"action_entity_id\":\"lock.front_door\"}";
    CHECK(notification_parse_json(plain_json, strlen(plain_json), &parsed, &err));
    CHECK(parsed != NULL && !parsed->action_open);
    free(parsed);
    parsed = NULL;
    const char *bad_service = "{\"message\":\"Ding\",\"action_entity_id\":\"lock.front_door\",\"action_service\":\"lock\"}";
    CHECK(!notification_parse_json(bad_service, strlen(bad_service), &parsed, &err) && err != NULL);
    const char *not_lock = "{\"message\":\"Ding\",\"action_entity_id\":\"button.gate\",\"action_service\":\"open\"}";
    CHECK(!notification_parse_json(not_lock, strlen(not_lock), &parsed, &err) && parsed == NULL);

    /* Fields are cut at a character boundary: with 61 ASCII bytes a 3-byte heart does not fit the 63-byte title. */
    char long_json[1200];
    for (int pad = 59; pad <= 64; pad++) {
        char ascii[80];
        memset(ascii, 'a', (size_t)pad);
        ascii[pad] = '\0';
        snprintf(long_json, sizeof(long_json), "{\"message\":\"m\",\"title\":\"%s\xE2\x9D\xA4\"}", ascii);
        CHECK(notification_parse_json(long_json, strlen(long_json), &parsed, &err) && parsed != NULL);
        if (parsed != NULL) {
            const size_t room = sizeof(parsed->title) - 1U;
            const size_t keep = (size_t)pad + 3U <= room ? (size_t)pad + 3U : ((size_t)pad < room ? (size_t)pad : room);
            CHECK(strlen(parsed->title) == keep);
            /* never a lone lead or continuation byte at the end */
            const unsigned char last = (unsigned char)parsed->title[keep - 1U];
            CHECK(last == 'a' || last == 0xA4);
        }
        free(parsed);
        parsed = NULL;
    }
    /* 4-byte emoji in the message field: 767 ASCII bytes plus U+1F600 keep only the ASCII. */
    {
        static char big[1000];
        static char json[1100];
        memset(big, 'b', 767);
        strcpy(big + 767, "\xF0\x9F\x98\x80");
        snprintf(json, sizeof(json), "{\"message\":\"%s\"}", big);
        CHECK(notification_parse_json(json, strlen(json), &parsed, &err) && parsed != NULL);
        CHECK(parsed != NULL && strlen(parsed->message) == 767);
        free(parsed);
        parsed = NULL;
    }
    /* A sequence that does not fit is left out as a whole (heart + FE0F, flag, skin tone), in every display field. */
    {
        static const char *const seqs[] = {
            "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA", "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"};
        static char pad[800];
        static char json[2000];
        for (size_t q = 0; q < sizeof(seqs) / sizeof(seqs[0]); q++) {
            const size_t len = strlen(seqs[q]);
            for (size_t cut = 1; cut < len; cut++) { /* `cut` bytes of the sequence would still fit */
                const struct { const char *key; size_t room; } fields[] = {{"title", 63}, {"message", 767}, {"action_label", 23}};
                for (size_t f = 0; f < 3; f++) {
                    const size_t n = fields[f].room - cut;
                    memset(pad, 'a', n);
                    pad[n] = '\0';
                    if (strcmp(fields[f].key, "message") == 0) {
                        snprintf(json, sizeof(json), "{\"message\":\"%s%s\"}", pad, seqs[q]);
                    } else {
                        snprintf(json, sizeof(json), "{\"message\":\"m\",\"%s\":\"%s%s\"%s}", fields[f].key, pad, seqs[q],
                                 strcmp(fields[f].key, "action_label") == 0 ? ",\"action_entity_id\":\"lock.front_door\"" : "");
                    }
                    CHECK(notification_parse_json(json, strlen(json), &parsed, &err) && parsed != NULL);
                    if (parsed != NULL) {
                        const char *got = strcmp(fields[f].key, "message") == 0 ? parsed->message
                                          : strcmp(fields[f].key, "title") == 0 ? parsed->title
                                                                                : parsed->action_label;
                        CHECK(strlen(got) == n);
                        CHECK(strspn(got, "a") == n);
                    }
                    free(parsed);
                    parsed = NULL;
                }
            }
        }
    }
}

static int validate_layout_alerts(const char *alerts_json, layout_validation_result_t *result)
{
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"version\":1,\"alerts\":%s,\"pages\":[{\"id\":\"p1\",\"title\":\"T\",\"widgets\":[]}]}", alerts_json);
    layout_validation_clear(result);
    return layout_validate_json(json, result) ? 1 : 0;
}

static void test_alerts(void)
{
    layout_validation_result_t r;
    CHECK(validate_layout_alerts("[{\"entity_id\":\"event.bell\",\"action_entity_id\":\"button.open\",\"camera_entity_id\":\"camera.door\"}]", &r));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"event.bell\",\"action_entity_id\":\"automation.x\"}]", &r));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"event.bell\",\"action_entity_id\":\"light.x\\\",\\\"a\\\":1\"}]", &r));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"bad id\"}]", &r));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"event.a\",\"camera_entity_id\":\"light.a\"}]", &r));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"event.a\"},{\"entity_id\":\"event.b\"},{\"entity_id\":\"event.c\"},{\"entity_id\":\"event.d\"},{\"entity_id\":\"event.e\"}]", &r));
    CHECK(!validate_layout_alerts("{}", &r));
}

/* ---- generic attributes_json overflow fallback ------------------------------------------------ */

/* A ~13 KB Solcast-forecast-like attributes object: real fields plus a large filler value standing
 * in for hourly forecast data, similar in shape to the sensor that originally found this bug (see
 * the "solar-forecast-attrs-truncation-plan" development note). */
static cJSON *build_oversized_attrs(void)
{
    cJSON *attrs = cJSON_CreateObject();
    cJSON_AddStringToObject(attrs, "device_class", "energy");
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "kWh");
    cJSON_AddStringToObject(attrs, "friendly_name", "Solcast PV Forecast Today");

    char *filler = (char *)malloc(13000);
    if (filler != NULL) {
        memset(filler, 'a', 12999);
        filler[12999] = '\0';
        cJSON_AddStringToObject(attrs, "detailedHourly", filler);
        free(filler);
    }
    return attrs;
}

static void test_iso_time_and_forecast_series(void)
{
    int64_t at = 0;
    int64_t utc = 0, local = 0, frac = 0;
    CHECK(iso_time_parse("2026-09-28T22:00:00Z", &utc));
    CHECK(iso_time_parse("2026-09-29T00:00:00+02:00", &local) && local == utc);
    CHECK(iso_time_parse("2026-09-28T22:00:00.250Z", &frac) && frac == utc);
    CHECK(iso_time_parse("2026-09-28T17:30:00-04:30", &at) && at == utc);
    CHECK(!iso_time_parse("2026-13-01T00:00:00Z", &at) && !iso_time_parse("yesterday", &at) && !iso_time_parse(NULL, &at));
    CHECK(!iso_time_parse("2026-09-28T22:00:00+25:00", &at));

    /* Solcast-style attributes far bigger than attributes_json: only the compact series and the audited keys stay. */
    cJSON *attrs = cJSON_CreateObject();
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "kWh");
    cJSON_AddStringToObject(attrs, "friendly_name", "Forecast today");
    cJSON *series = cJSON_AddArrayToObject(attrs, "detailedForecast");
    for (int i = 0; i < 48; i++) {
        cJSON *item = cJSON_CreateObject();
        char start[40];
        snprintf(start, sizeof(start), "2026-09-29T%02d:%02d:00+02:00", i / 2, (i % 2) * 30);
        cJSON_AddStringToObject(item, "period_start", start);
        cJSON_AddNumberToObject(item, "pv_estimate", i == 26 ? 7.3699 : 0.0);
        cJSON_AddNumberToObject(item, "pv_estimate10", 1.0);
        cJSON_AddItemToArray(series, item);
    }
    cJSON *padding = cJSON_AddArrayToObject(attrs, "detailedHourly");
    for (int i = 0; i < 300; i++) cJSON_AddItemToArray(padding, cJSON_CreateString("0123456789012345678901234567890123456789"));
    char out[APP_HA_ATTRS_MAX_LEN];
    CHECK(ha_attrs_build_generic_overflow_fallback(attrs, out, sizeof(out)));
    cJSON *kept = cJSON_Parse(out);
    CHECK(kept != NULL);
    if (kept != NULL) {
        int64_t first = 0;
        CHECK(iso_time_parse("2026-09-29T00:00:00+02:00", &first));
        CHECK((int64_t)cJSON_GetObjectItemCaseSensitive(kept, "fc_t0")->valuedouble == first);
        CHECK((int)cJSON_GetObjectItemCaseSensitive(kept, "fc_step")->valuedouble == 1800);
        cJSON *kw = cJSON_GetObjectItemCaseSensitive(kept, "fc_kw");
        CHECK(cJSON_GetArraySize(kw) == 48 && fabs(cJSON_GetArrayItem(kw, 26)->valuedouble - 7.37) < 1e-9);
        CHECK(cJSON_GetObjectItemCaseSensitive(kept, "detailedForecast") == NULL);
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(kept, "unit_of_measurement")->valuestring, "kWh") == 0);
        cJSON_Delete(kept);
    }
    /* An uneven series is not compacted (better nothing than shifted values). */
    cJSON_DeleteItemFromArray(series, 10);
    CHECK(ha_attrs_build_generic_overflow_fallback(attrs, out, sizeof(out)));
    kept = cJSON_Parse(out);
    CHECK(kept != NULL && cJSON_GetObjectItemCaseSensitive(kept, "fc_kw") == NULL);
    cJSON_Delete(kept);
    cJSON_Delete(attrs);
}

static void test_ha_attrs_fallback(void)
{
    /* Oversized attrs (~13 KB): the object itself does not fit APP_HA_ATTRS_MAX_LEN (1280), so this is
     * exactly the case ha_client.c calls the fallback for once its own verbatim copy overflows. */
    cJSON *big = build_oversized_attrs();
    char out[APP_HA_ATTRS_MAX_LEN];
    memset(out, 0xAA, sizeof(out)); /* poison: a bug that leaves bytes untouched must not look valid */
    bool kept_any = ha_attrs_build_generic_overflow_fallback(big, out, sizeof(out));
    CHECK(kept_any);
    CHECK(strlen(out) < sizeof(out)); /* still null-terminated well inside the buffer */

    cJSON *parsed = cJSON_Parse(out); /* valid JSON: the whole point of this fallback */
    CHECK(parsed != NULL);
    if (parsed != NULL) {
        cJSON *dc = cJSON_GetObjectItemCaseSensitive(parsed, "device_class");
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(parsed, "unit_of_measurement");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(parsed, "friendly_name");
        CHECK(cJSON_IsString(dc) && strcmp(dc->valuestring, "energy") == 0);
        CHECK(cJSON_IsString(unit) && strcmp(unit->valuestring, "kWh") == 0);
        CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "Solcast PV Forecast Today") == 0);
        /* The oversized field that caused the overflow is deliberately not in the audited field set. */
        CHECK(cJSON_GetObjectItemCaseSensitive(parsed, "detailedHourly") == NULL);
        cJSON_Delete(parsed);
    }
    cJSON_Delete(big);

    /* A normal, small attributes object (e.g. a plain temperature sensor): still produces valid JSON
     * with the fields present, so a caller does not need special-case logic for "was this the
     * overflow path or not" -- it never actually reaches this function in that case (ha_client.c only
     * calls it once its own copy overflowed), but the function's own behavior is unconditional. */
    cJSON *small = cJSON_CreateObject();
    cJSON_AddStringToObject(small, "unit_of_measurement", "\xc2\xb0" "C");
    cJSON_AddStringToObject(small, "friendly_name", "Living Room Temperature");
    char out2[APP_HA_ATTRS_MAX_LEN];
    CHECK(ha_attrs_build_generic_overflow_fallback(small, out2, sizeof(out2)));
    cJSON *parsed2 = cJSON_Parse(out2);
    CHECK(parsed2 != NULL);
    if (parsed2 != NULL) {
        cJSON *unit = cJSON_GetObjectItemCaseSensitive(parsed2, "unit_of_measurement");
        CHECK(cJSON_IsString(unit) && strcmp(unit->valuestring, "\xc2\xb0" "C") == 0);
        cJSON_Delete(parsed2);
    }
    cJSON_Delete(small);

    /* None of the four audited fields present: falls back to "{}", not an empty/garbage buffer. */
    cJSON *none = cJSON_CreateObject();
    cJSON_AddStringToObject(none, "some_other_field", "value");
    char out3[APP_HA_ATTRS_MAX_LEN];
    memset(out3, 0xAA, sizeof(out3));
    CHECK(!ha_attrs_build_generic_overflow_fallback(none, out3, sizeof(out3)));
    CHECK(strcmp(out3, "{}") == 0);
    cJSON_Delete(none);

    /* Defensive callers: NULL/non-object input and a too-small buffer never crash or leave garbage. */
    char out4[APP_HA_ATTRS_MAX_LEN];
    memset(out4, 0xAA, sizeof(out4));
    CHECK(!ha_attrs_build_generic_overflow_fallback(NULL, out4, sizeof(out4)));
    CHECK(strcmp(out4, "{}") == 0);

    cJSON *has_fields = cJSON_CreateObject();
    cJSON_AddStringToObject(has_fields, "device_class", "energy");
    char tiny[4]; /* too small even for {"device_class":"energy"} */
    memset(tiny, 0xAA, sizeof(tiny));
    CHECK(!ha_attrs_build_generic_overflow_fallback(has_fields, tiny, sizeof(tiny)));
    CHECK(strcmp(tiny, "{}") == 0);
    cJSON_Delete(has_fields);
}

/* ---- Home Assistant attribute serializers --------------------------------------------------- */

static void test_ha_attr_serializers(void)
{
    typedef bool (*serializer_t)(cJSON *, char *, size_t);
    const struct {
        serializer_t serialize;
        const char *input;
        const char *kept;
        const char *dropped;
    } cases[] = {
        {ha_client_serialize_weather_attrs_compact,
         "{\"temperature\":21,\"humidity\":45,\"unused\":123}", "temperature", "unused"},
        {ha_client_serialize_climate_attrs_compact,
         "{\"target_temperature\":22,\"hvac_action\":\"heating\",\"unused\":123}", "temperature", "unused"},
        {ha_client_serialize_media_player_attrs_compact,
         "{\"volume_level\":0.4,\"media_title\":\"Track\",\"unused\":123}", "media_title", "unused"},
        {ha_client_serialize_sonos_favorites_attrs_compact,
         "{\"items\":{\"one\":\"Station\"},\"unused\":123}", "items", "unused"},
        {ha_client_serialize_light_attrs_compact,
         "{\"brightness\":128,\"effect_list\":[\"A\",\"B\"],\"unused\":123}", "brightness", "unused"},
        {ha_client_serialize_cover_attrs_compact,
         "{\"current_position\":50,\"unused\":123}", "current_position", "unused"},
        {ha_client_serialize_cover_attrs_compact,
         "{\"device_class\":\"shutter\",\"unused\":123}", "device_class", "unused"},
        {ha_client_serialize_media_player_attrs_compact,
         "{\"device_class\":\"speaker\",\"unused\":123}", "device_class", "unused"},
        {ha_client_serialize_fan_attrs_compact,
         "{\"percentage\":80,\"unused\":123}", "percentage", "unused"},
        {ha_client_serialize_image_attrs_compact,
         "{\"entity_picture\":\"/image\",\"unused\":123}", "entity_picture", "unused"},
        {ha_client_serialize_alarm_attrs_compact,
         "{\"supported_features\":7,\"code_format\":\"number\",\"changed_by\":\"x\"}", "code_format", "changed_by"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cJSON *src = cJSON_Parse(cases[i].input);
        char out[APP_HA_ATTRS_MAX_LEN] = {0};
        CHECK(src != NULL);
        if (src == NULL) {
            continue;
        }
        CHECK(cases[i].serialize(src, out, sizeof(out)));
        cJSON *parsed = cJSON_Parse(out);
        CHECK(cJSON_IsObject(parsed));
        if (cJSON_IsObject(parsed)) {
            CHECK(cJSON_GetObjectItemCaseSensitive(parsed, cases[i].kept) != NULL);
            CHECK(cJSON_GetObjectItemCaseSensitive(parsed, cases[i].dropped) == NULL);
        }
        cJSON_Delete(parsed);
        cJSON_Delete(src);
    }

    /* The media-player widget's shuffle and repeat buttons read these two attributes. */
    cJSON *player = cJSON_Parse("{\"shuffle\":true,\"repeat\":\"one\",\"supported_features\":294912,\"unused\":1}");
    char out[APP_HA_ATTRS_MAX_LEN] = {0};
    CHECK(ha_client_serialize_media_player_attrs_compact(player, out, sizeof(out)));
    cJSON *compact_player = cJSON_Parse(out);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(compact_player, "shuffle")));
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(compact_player, "repeat")), "one") == 0);
    CHECK_INT((int)cJSON_GetObjectItemCaseSensitive(compact_player, "supported_features")->valuedouble, 294912);
    cJSON_Delete(compact_player);
    cJSON_Delete(player);

    cJSON *light = cJSON_Parse("{\"brightness\":128,\"effect_list\":[\"A\"]}");
    CHECK(ha_client_serialize_light_attrs_compact(light, out, sizeof(out)));
    cJSON *compact_light = cJSON_Parse(out);
    CHECK(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(compact_light, "effect_list")));
    CHECK_INT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(compact_light, "effect_list")), 1);
    cJSON_Delete(compact_light);
    cJSON_Delete(light);

    cJSON *weather = cJSON_Parse(
        "{\"temperature\":21,\"forecast_daily\":["
        "{\"date\":\"2026-09-25\",\"native_temperature\":20,\"unused\":1},"
        "{\"date\":\"2026-09-26\",\"temperature\":19}]}");
    CHECK(ha_client_serialize_weather_attrs_compact(weather, out, sizeof(out)));
    cJSON *compact_weather = cJSON_Parse(out);
    cJSON *forecast = cJSON_GetObjectItemCaseSensitive(compact_weather, "forecast");
    CHECK_INT(cJSON_GetArraySize(forecast), 2);
    cJSON *first = cJSON_GetArrayItem(forecast, 0);
    CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(first, "datetime")));
    CHECK(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(first, "temperature")));
    CHECK(cJSON_GetObjectItemCaseSensitive(first, "unused") == NULL);
    cJSON_Delete(compact_weather);
    cJSON_Delete(weather);

    cJSON *response = cJSON_Parse(
        "{\"response\":{\"service_response\":{\"weather.home\":{\"forecast\":["
        "{\"datetime\":\"2026-09-25\",\"temperature\":20,\"unused\":1}]}}}}");
    cJSON *found = ha_client_find_compact_weather_forecast(response, "weather.home");
    CHECK_INT(cJSON_GetArraySize(found), 1);
    CHECK(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(found, 0), "unused") == NULL);
    cJSON_Delete(found);
    cJSON_Delete(response);

    strcpy(out, "{\"humidity\":50}");
    CHECK(!ha_client_weather_attrs_has_forecast_json(out));
    cJSON *new_forecast = cJSON_Parse("[{\"datetime\":\"2026-09-25\"}]");
    CHECK(ha_client_append_compact_forecast_to_attrs_json(out, sizeof(out), new_forecast));
    CHECK(ha_client_weather_attrs_has_forecast_json(out));
    found = ha_client_extract_compact_forecast_from_attrs_json(out);
    CHECK_INT(cJSON_GetArraySize(found), 1);
    cJSON_Delete(found);
    char tiny[17] = "{\"humidity\":50}";
    new_forecast = cJSON_Parse("[{\"datetime\":\"2026-09-25\"}]");
    CHECK(!ha_client_append_compact_forecast_to_attrs_json(tiny, sizeof(tiny), new_forecast));
    CHECK(strcmp(tiny, "{\"humidity\":50}") == 0);

    cJSON *seven = cJSON_CreateObject();
    cJSON *seven_items = cJSON_AddArrayToObject(seven, "forecast");
    for (int i = 0; i < 8; i++) {
        cJSON *day = cJSON_CreateObject();
        cJSON_AddNumberToObject(day, "temperature", i);
        cJSON_AddItemToArray(seven_items, day);
    }
    found = ha_client_find_compact_weather_forecast(seven, "weather.home");
    CHECK_INT(cJSON_GetArraySize(found), 7);
    cJSON_Delete(found);
    cJSON_Delete(seven);

    char report[1024] = "Wetterbericht **12:00 Uhr**\n\nHeute ist es heiter.\n\n";
    size_t report_len = strlen(report);
    memset(report + report_len, 'x', 450);
    report[report_len + 450] = '\0';
    strcat(report, "\n\nDetaillierter Wetterablauf\nHeute scheint die Sonne.");
    cJSON *report_attrs = cJSON_CreateObject();
    cJSON_AddStringToObject(report_attrs, "data", report);
    CHECK(ha_client_attrs_is_weather_report(report_attrs));
    CHECK(ha_client_serialize_weather_report_compact(report_attrs, out, sizeof(out)));
    cJSON *compact_report = cJSON_Parse(out);
    CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(compact_report, "s")));
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(compact_report, "d")));
    CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(compact_report, "d"), "4")->valuestring,
                 "Heute scheint die Sonne.") == 0);
    cJSON_Delete(compact_report);
    cJSON_Delete(report_attrs);
}

/* ---- layout-driven Home Assistant subscriptions ---------------------------------------------- */

static int s_layout_overflows = 0;

static void test_layout_overflow(size_t capacity, const char *entity_id)
{
    CHECK_INT((int)capacity, 2);
    CHECK(entity_id != NULL && entity_id[0] != '\0');
    s_layout_overflows++;
}

static void test_ha_layout_entities(void)
{
    const char *layout_json =
        "{\"alerts\":[{\"entity_id\":\"binary_sensor.door\",\"camera_entity_id\":\"camera.entry\"}],"
        "\"pages\":["
        "{\"type\":\"house_dashboard\",\"house\":{\"rooms\":[{\"entity_id\":\"sensor.room\","
        "\"room_entities\":[\"light.room\",\"sensor.room\"]}]}},"
        "{\"type\":\"energy_dashboard\",\"energy\":{\"source\":\"manual_live\","
        "\"home_power_entity_id\":\"sensor.home_power\",\"solar_power_entity_id\":\"sensor.solar_power\","
        "\"battery_soc_entity_id\":\"sensor.battery_soc\"}},"
        "{\"type\":\"energy_dashboard\",\"energy\":{\"source\":\"home_assistant\","
        "\"home_power_entity_id\":\"sensor.ignored\",\"solar_power_entity_id\":\"sensor.ha_solar\","
        "\"battery_soc_entity_id\":\"sensor.ha_battery\"}},"
        "{\"widgets\":[{\"type\":\"weather_3day\",\"entity_id\":\"weather.home\","
        "\"secondary_entity_id\":\"sensor.outdoor\",\"forecast_today_entity_id\":\"sensor.forecast\","
        "\"room_entities\":[\"sensor.extra\",\"weather.home\"]},"
        "{\"type\":\"car\",\"entity_id\":\"sensor.car\",\"precond_entity_id\":\"switch.precond\"}]}]}";
    cJSON *layout = cJSON_Parse(layout_json);
    CHECK(layout != NULL);
    if (layout == NULL) {
        return;
    }
    char ids[20][APP_MAX_ENTITY_ID_LEN] = {{0}};
    ha_layout_entity_set_t set = {.ids = &ids[0][0], .capacity = 20};
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 15);
    CHECK(set.need_weather_forecast);
    CHECK(ha_layout_needs_ha_energy(layout));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "binary_sensor.door"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "camera.entry"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "light.room"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.home_power"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.ha_solar"));
    CHECK(!ha_layout_entities_contains(set.ids, set.count, "sensor.ignored"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.forecast"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.extra"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "switch.precond"));
    ha_layout_entities_add(&set, "weather.topbar");
    ha_layout_entities_add(&set, "sensor.stock_1");
    ha_layout_entities_add(&set, "weather.topbar");
    CHECK_INT((int)set.count, 17);
    cJSON_Delete(layout);

    layout = cJSON_Parse("{\"pages\":[{\"widgets\":[{\"type\":\"weather_tile\",\"entity_id\":\"weather.home\"}]}]}");
    ha_layout_entities_collect(layout, &set);
    CHECK(set.need_weather_forecast);
    CHECK_INT((int)set.count, 1);
    cJSON_Delete(layout);
    layout = cJSON_Parse("{\"pages\":[{\"type\":\"energy_dashboard\","
                         "\"energy\":{\"home_power_entity_id\":\"sensor.manual\"}}]}");
    CHECK(!ha_layout_needs_ha_energy(layout)); /* Missing source + manual IDs = manual_live. */
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 1);
    CHECK(!set.need_weather_forecast);
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.manual"));
    cJSON_Delete(layout);

    /* Stocks & ETFs: the first quote and the list after it. */
    layout = cJSON_Parse("{\"pages\":[{\"widgets\":[{\"type\":\"stock_quotes\",\"entity_id\":\"sensor.quote_a\","
                         "\"stock_entities\":[\"sensor.quote_b\",\"sensor.quote_c\"]}]}]}");
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 3);
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.quote_c"));
    cJSON_Delete(layout);

    /* Every widget ID slot must reach the HA subscription set. */
    const char *widget_keys[] = {
        "entity_id", "secondary_entity_id", "tertiary_entity_id",
        "forecast_today_entity_id", "forecast_tomorrow_entity_id", "forecast_day_3_entity_id",
        "forecast_day_4_entity_id", "forecast_day_5_entity_id", "forecast_day_6_entity_id", "forecast_day_7_entity_id", "precond_entity_id",
        "lock_entity_id", "interior_temp_entity_id",
    };
    layout = cJSON_CreateObject();
    cJSON *pages = cJSON_AddArrayToObject(layout, "pages");
    cJSON *page = cJSON_CreateObject();
    cJSON_AddItemToArray(pages, page);
    cJSON *widgets = cJSON_AddArrayToObject(page, "widgets");
    cJSON *widget = cJSON_CreateObject();
    cJSON_AddItemToArray(widgets, widget);
    for (size_t i = 0; i < sizeof(widget_keys) / sizeof(widget_keys[0]); i++) {
        char entity_id[32];
        snprintf(entity_id, sizeof(entity_id), "sensor.slot_%u", (unsigned)i);
        cJSON_AddStringToObject(widget, widget_keys[i], entity_id);
    }
    cJSON *room_entities = cJSON_AddArrayToObject(widget, "room_entities");
    cJSON_AddItemToArray(room_entities, cJSON_CreateString("sensor.room_extra"));
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 14);
    for (size_t i = 0; i < sizeof(widget_keys) / sizeof(widget_keys[0]); i++) {
        char entity_id[32];
        snprintf(entity_id, sizeof(entity_id), "sensor.slot_%u", (unsigned)i);
        CHECK(ha_layout_entities_contains(set.ids, set.count, entity_id));
    }
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.room_extra"));
    cJSON_Delete(layout);

    /* Bambu printer: every explicit binding reaches the subscription set, reads shared with the primary once. */
    layout = cJSON_Parse("{\"pages\":[{\"widgets\":[{\"type\":\"bambu_printer\",\"entity_id\":\"sensor.b_status\","
                         "\"nozzle_temp_entity_id\":\"sensor.b_nozzle\",\"bed_temp_entity_id\":\"sensor.b_bed\","
                         "\"progress_entity_id\":\"sensor.b_progress\",\"remaining_time_entity_id\":\"sensor.b_remaining\","
                         "\"pause_entity_id\":\"button.b_pause\",\"resume_entity_id\":\"button.b_resume\","
                         "\"stop_entity_id\":\"button.b_stop\",\"stage_entity_id\":\"sensor.b_stage\","
                         "\"job_name_entity_id\":\"sensor.b_status\"}]}]}");
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 9);
    CHECK(ha_layout_entities_contains(set.ids, set.count, "button.b_resume"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.b_remaining"));
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.b_stage"));
    cJSON_Delete(layout);

    layout = cJSON_Parse("{\"pages\":[{\"widgets\":[{\"type\":\"vacuum_tile\",\"entity_id\":\"vacuum.v\","
                         "\"battery_entity_id\":\"sensor.v_battery\"}]}]}");
    ha_layout_entities_collect(layout, &set);
    CHECK_INT((int)set.count, 2);
    CHECK(ha_layout_entities_contains(set.ids, set.count, "sensor.v_battery"));
    cJSON_Delete(layout);

    char limited[2][APP_MAX_ENTITY_ID_LEN] = {{0}};
    ha_layout_entity_set_t cap = {.ids = &limited[0][0], .capacity = 2, .on_overflow = test_layout_overflow};
    s_layout_overflows = 0;
    layout = cJSON_Parse("{\"alerts\":[{\"entity_id\":\"sensor.one\","
                         "\"camera_entity_id\":\"camera.two\"}],"
                         "\"pages\":[{\"widgets\":[{\"entity_id\":\"sensor.three\"}]}]}");
    ha_layout_entities_collect(layout, &cap);
    CHECK_INT((int)cap.count, 2);
    CHECK(!ha_layout_entities_contains(cap.ids, cap.count, "sensor.three"));
    CHECK_INT(s_layout_overflows, 1); /* the collector reports omitted entities rather than stopping silently */
    ha_layout_entities_add(&cap, "sensor.four");
    CHECK_INT(s_layout_overflows, 2);
    ha_layout_entities_add(&cap, "sensor.one"); CHECK_INT(s_layout_overflows, 2); /* a stored duplicate is not an overflow */
    cJSON_Delete(layout);

    char ordered_a[2][APP_MAX_ENTITY_ID_LEN] = {{0}};
    char ordered_b[2][APP_MAX_ENTITY_ID_LEN] = {{0}};
    ha_layout_entity_set_t a = {.ids = &ordered_a[0][0], .capacity = 2};
    ha_layout_entity_set_t b = {.ids = &ordered_b[0][0], .capacity = 2};
    ha_layout_entities_add(&a, "sensor.alpha");
    ha_layout_entities_add(&a, "sensor.beta");
    ha_layout_entities_add(&b, "sensor.beta");
    ha_layout_entities_add(&b, "sensor.alpha");
    CHECK(ha_layout_entities_signature(a.ids, a.count) == ha_layout_entities_signature(b.ids, b.count));
    CHECK_INT((int)ha_layout_entities_signature(NULL, 0), 0);
}

static void test_chime(void)
{
    chime_kind_t kind = CHIME_NONE;
    CHECK(chime_kind_parse("chime", &kind) && kind == CHIME_DOORBELL);
    CHECK(chime_kind_parse("notify", &kind) && kind == CHIME_NOTIFY);
    CHECK(chime_kind_parse("none", &kind) && kind == CHIME_NONE);
    CHECK(!chime_kind_parse("other", &kind));
    CHECK(!chime_kind_parse(NULL, &kind));
    CHECK(!chime_should_play(false, false, false, 100));
    CHECK(!chime_should_play(true, true, true, 50));
    CHECK(chime_should_play(true, false, true, 50));
    CHECK(chime_should_play(true, true, false, 50));
    CHECK(!chime_should_play(true, false, false, 0));
    CHECK_INT(chime_synth_samples(CHIME_DOORBELL), 19200);
    CHECK_INT(chime_synth_samples(CHIME_NOTIFY), 4800);
    CHECK_INT(chime_synth_samples(CHIME_NONE), 0);
    int16_t pcm[19200];
    chime_synth_render(CHIME_DOORBELL, 0, pcm, 19200);
    int peak = 0; long energy = 0;
    for (size_t i = 0; i < 19200; i++) { int a = abs(pcm[i]); if (a > peak) peak = a; energy += a; }
    CHECK(peak > 18000 && peak <= 26000);
    CHECK(energy > 3000000);
    CHECK_INT(pcm[0], 0); CHECK_INT(pcm[7999], 0); CHECK_INT(pcm[8000], 0); CHECK_INT(pcm[19199], 0);
    int16_t chunk[257]; chime_synth_render(CHIME_DOORBELL, 7980, chunk, 257);
    CHECK(memcmp(chunk, pcm + 7980, sizeof(chunk)) == 0);
    chime_synth_render(CHIME_NOTIFY, 0, pcm, 4802);
    CHECK_INT(pcm[0], 0); CHECK_INT(pcm[4799], 0); CHECK_INT(pcm[4800], 0); CHECK_INT(pcm[4801], 0);
    app_notification_t *n = NULL; const char *err = NULL;
    const char *json = "{\"message\":\"Bell\",\"sound\":\"chime\"}";
    CHECK(notification_parse_json(json, strlen(json), &n, &err));
    CHECK(n && n->sound == CHIME_DOORBELL && n->remember); free(n);
    json = "{\"message\":\"Silent\"}";
    CHECK(notification_parse_json(json, strlen(json), &n, &err));
    CHECK(n && n->sound == CHIME_NONE); free(n);
    json = "{\"message\":\"Bad\",\"sound\":true}";
    CHECK(!notification_parse_json(json, strlen(json), &n, &err));
    json = "{\"message\":\"Bad\",\"sound\":\"loud\"}";
    CHECK(!notification_parse_json(json, strlen(json), &n, &err));
    layout_validation_result_t result;
    CHECK(validate_layout_alerts("[{\"entity_id\":\"event.bell\",\"sound\":\"chime\"}]", &result));
    CHECK(!validate_layout_alerts("[{\"entity_id\":\"event.bell\",\"sound\":\"wrong\"}]", &result));
}

/* ---- weather widget value parser ------------------------------------------------------------- */

static void test_weather_value_parser(void)
{
    CHECK_INT(weather_icon_codepoint_for_key(NULL), 0);
    CHECK_INT(weather_icon_codepoint_for_key("unknown"), 0);
    CHECK_INT(weather_icon_codepoint_for_key("sunny"), 0xF0599);
    CHECK_INT(weather_icon_codepoint_for_key("rainy"), 0xF0597);
    CHECK_INT(weather_icon_codepoint_for_key("clear-night"), 0xF0594);
    CHECK_INT(weather_icon_codepoint_for_key("lightning-rainy"), 0xF067E);
    float number = 0;
    int integer = 0;
    char text[16] = {0};

    CHECK(weather_parse_float_token(" 21.5 C", &number));
    CHECK(number == 21.5f);
    CHECK(!weather_parse_float_token("  ", &number));
    CHECK(!weather_parse_float_token(NULL, &number));
    CHECK(weather_parse_int_token(" 45%", &integer));
    CHECK_INT(integer, 45);
    CHECK(!weather_parse_int_token("n/a", &integer));

    cJSON *attrs = cJSON_Parse("{\"temperature\":21.5,\"humidity\":\"45\",\"unit\":\"\\u00b0C\"}");
    CHECK(weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "temperature"), &number));
    CHECK(number == 21.5f);
    CHECK(weather_json_item_to_int(cJSON_GetObjectItemCaseSensitive(attrs, "humidity"), &integer));
    CHECK_INT(integer, 45);
    CHECK(!weather_json_item_to_float(cJSON_GetObjectItemCaseSensitive(attrs, "missing"), &number));
    cJSON_Delete(attrs);

    const char *raw = "{\"temperature\":21.5,\"humidity\":\"45\",\"unit\":\"C\"}";
    CHECK(weather_extract_raw_number_attr(raw, "temperature", &number));
    CHECK(number == 21.5f);
    CHECK(weather_extract_raw_int_attr(raw, "humidity", &integer));
    CHECK_INT(integer, 45);
    CHECK(weather_extract_raw_string_attr(raw, "unit", text, sizeof(text)));
    CHECK(strcmp(text, "C") == 0);
    CHECK(!weather_extract_raw_string_attr(raw, "missing", text, sizeof(text)));
}

/* ---- crash-loop guard ------------------------------------------------------------------------ */

static void test_boot_guard(void)
{
    boot_guard_state_t st = {0}; /* power-up: the RTC memory holds nothing valid */
    boot_guard_next(&st, true, false);
    CHECK(!boot_guard_safe(&st));

    /* Three crashes in a row without a stable boot in between: the fourth boot is the safe one. */
    boot_guard_next(&st, false, true);
    CHECK_INT((int)st.crashes, 1);
    boot_guard_next(&st, false, true);
    CHECK_INT((int)st.crashes, 2);
    CHECK(!boot_guard_safe(&st));
    boot_guard_next(&st, false, true);
    CHECK(boot_guard_safe(&st));

    /* A boot that ran long enough clears the count: a later crash is not a loop. */
    st.stable = 1;
    boot_guard_next(&st, false, true);
    CHECK_INT((int)st.crashes, 0);
    CHECK(!boot_guard_safe(&st));

    /* A planned restart (not a crash) ends the streak, a power cycle starts from scratch. */
    boot_guard_next(&st, false, true);
    boot_guard_next(&st, false, true);
    boot_guard_next(&st, false, false);
    CHECK_INT((int)st.crashes, 0);
    boot_guard_next(&st, false, true);
    boot_guard_next(&st, false, true);
    boot_guard_next(&st, true, true);
    CHECK_INT((int)st.crashes, 0);

    /* Garbage in RTC memory (wrong magic) is not trusted. */
    boot_guard_state_t junk = {0x12345678u, 99, 0};
    boot_guard_next(&junk, false, true);
    CHECK(!boot_guard_safe(&junk));
    CHECK(!boot_guard_safe(NULL));
}

static void test_ha_room_registry(void)
{
    /* Nothing read yet: an areas-only poll asks for the registry once and waits for it. */
    CHECK(!ha_room_registry_has_areas());
    CHECK(ha_room_registry_poll(true, false, 1000));
    CHECK(ha_room_registry_is_due());
    CHECK(ha_room_registry_begin(11, 12, 1000));
    CHECK(!ha_room_registry_is_due());
    CHECK(ha_room_registry_poll(true, false, 2000));
    CHECK(!ha_room_registry_handle_result(99, true, NULL));

    cJSON *areas = cJSON_Parse("[{\"name\":\"Kueche\",\"floor_id\":\"eg\",\"temperature_entity_id\":\"sensor.t\"},"
        "{\"name\":\"Garten\"},7]");
    cJSON *floors = cJSON_Parse("[{\"floor_id\":\"eg\",\"name\":\"Erdgeschoss\",\"level\":0}]");
    CHECK(ha_room_registry_handle_result(11, true, areas));
    CHECK(ha_room_registry_poll(true, false, 2000)); /* floors still due */
    CHECK(ha_room_registry_handle_result(12, true, floors));
    CHECK(!ha_room_registry_poll(true, false, 2000));
    CHECK(ha_room_registry_has_areas());
    CHECK(!ha_room_registry_handle_result(11, true, areas)); /* answered already */
    cJSON_Delete(areas);
    cJSON_Delete(floors);

    cJSON *root = cJSON_CreateObject();
    ha_room_registry_add_areas_json(root);
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "areas");
    CHECK(cJSON_GetArraySize(list) == 2);
    cJSON *kitchen = cJSON_GetArrayItem(list, 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(kitchen, "floor")), "Erdgeschoss") == 0);
    CHECK(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(kitchen, "floor_level")));
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(kitchen, "temperature_entity_id")), "sensor.t") == 0);
    cJSON *garden = cJSON_GetArrayItem(list, 1);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(garden, "floor")), "") == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(garden, "floor_level") == NULL);
    cJSON_Delete(root);

    /* A refresh asks again; an answer lost with the connection stops blocking after the timeout. */
    CHECK(ha_room_registry_poll(true, true, 3000));
    CHECK(ha_room_registry_begin(21, 22, 3000));
    CHECK(ha_room_registry_poll(true, false, 3000 + HA_ROOM_REGISTRY_TIMEOUT_MS - 1));
    CHECK(!ha_room_registry_poll(true, false, 3000 + HA_ROOM_REGISTRY_TIMEOUT_MS));
    /* A failed send forgets the requests; a failed answer keeps the areas read before. */
    CHECK(ha_room_registry_begin(31, 32, 40000));
    ha_room_registry_cancel();
    CHECK(!ha_room_registry_handle_result(31, true, NULL));
    CHECK(!ha_room_registry_poll(false, true, 40000)); /* disconnected: no new request */
    CHECK(ha_room_registry_begin(41, 42, 50000));
    CHECK(ha_room_registry_handle_result(41, false, NULL));
    CHECK(ha_room_registry_has_areas());
}

static int s_precision_changes = 0;
static void test_precision_changed(const char *entity_id)
{
    (void)entity_id;
    s_precision_changes++;
}

static void test_ha_display_precision(void)
{
    char out[32];
    /* Nothing known yet: the state is shown as it is. */
    CHECK(!ha_display_precision_format("sensor.power", "5.9518", out, sizeof(out)));
    CHECK(!ha_display_precision_is_due(0));

    /* Only numeric domains are asked for, in batches. */
    static char ids[20][APP_MAX_ENTITY_ID_LEN];
    memset(ids, 0, sizeof(ids));
    snprintf(ids[0], sizeof(ids[0]), "%s", "sensor.power");
    snprintf(ids[1], sizeof(ids[1]), "%s", "weather.home");
    snprintf(ids[2], sizeof(ids[2]), "%s", "sensor.solar");
    snprintf(ids[3], sizeof(ids[3]), "%s", "number.limit");
    snprintf(ids[4], sizeof(ids[4]), "%s", "sensor.plain");
    for (int i = 5; i < 20; i++) {
        snprintf(ids[i], sizeof(ids[i]), "sensor.extra_%d", i);
    }
    ha_display_precision_set_wanted(&ids[0][0], 20);
    CHECK(ha_display_precision_is_due(1000));
    cJSON *req = ha_display_precision_next_request(7, 1000);
    CHECK(req != NULL);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(req, "type")),
        "config/entity_registry/get_entries") == 0);
    CHECK_INT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(req, "entity_ids")), HA_DISPLAY_PRECISION_BATCH);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(req, "entity_ids"), 1)),
        "sensor.solar") == 0);
    cJSON_Delete(req);
    CHECK(!ha_display_precision_is_due(1000)); /* waiting for the answer */

    /* A failed send asks for the same batch again. */
    ha_display_precision_cancel();
    req = ha_display_precision_next_request(8, 1000);
    CHECK_INT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(req, "entity_ids")), HA_DISPLAY_PRECISION_BATCH);
    cJSON_Delete(req);
    CHECK(!ha_display_precision_handle_result(7, true, NULL, test_precision_changed));

    /* The user's display_precision wins over the integration's suggestion; no entry, no precision. */
    cJSON *result = cJSON_Parse("{\"sensor.power\":{\"options\":{\"sensor\":{\"suggested_display_precision\":2}}},"
        "\"sensor.solar\":{\"options\":{\"sensor\":{\"suggested_display_precision\":2,\"display_precision\":0}}},"
        "\"number.limit\":{\"options\":{}},\"sensor.plain\":null}");
    CHECK(ha_display_precision_handle_result(8, true, result, test_precision_changed));
    cJSON_Delete(result);
    /* every entity of the batch learned its integration ("other" without an entry), two also a precision */
    CHECK_INT(s_precision_changes, HA_DISPLAY_PRECISION_BATCH);
    CHECK_INT(ha_display_precision_get("sensor.power"), 2);
    CHECK_INT(ha_display_precision_get("sensor.solar"), 0);
    CHECK_INT(ha_display_precision_get("number.limit"), HA_DISPLAY_PRECISION_UNKNOWN);

    CHECK(ha_display_precision_format("sensor.power", "5.9518", out, sizeof(out)));
    CHECK(strcmp(out, "5.95") == 0);
    CHECK(ha_display_precision_format("sensor.solar", "8567.4", out, sizeof(out)));
    CHECK(strcmp(out, "8567") == 0);
    CHECK(ha_display_precision_format("sensor.power", "-0.001", out, sizeof(out)));
    CHECK(strcmp(out, "0.00") == 0);
    CHECK(!ha_display_precision_format("sensor.power", "unavailable", out, sizeof(out)));
    CHECK(!ha_display_precision_format("sensor.power", "5.9 kW", out, sizeof(out)));
    CHECK(!ha_display_precision_format("sensor.power", "", out, sizeof(out)));
    CHECK(!ha_display_precision_format("sensor.plain", "1.2345", out, sizeof(out)));

    /* The rest in a second batch; an unanswered one is skipped after the timeout. */
    CHECK(ha_display_precision_is_due(2000));
    req = ha_display_precision_next_request(9, 2000);
    CHECK_INT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(req, "entity_ids")), 20 - 1 - HA_DISPLAY_PRECISION_BATCH);
    cJSON_Delete(req);
    CHECK(!ha_display_precision_is_due(2000 + HA_DISPLAY_PRECISION_TIMEOUT_MS - 1));
    CHECK(!ha_display_precision_is_due(2000 + HA_DISPLAY_PRECISION_TIMEOUT_MS));
    CHECK(!ha_display_precision_handle_result(9, true, NULL, test_precision_changed));

    /* A new layout asks again; an unchanged precision is not reported twice. A Home Assistant
     * without get_entries (error answer) stops the asking. */
    ha_display_precision_set_wanted(&ids[0][0], 1);
    req = ha_display_precision_next_request(10, 3000);
    cJSON_Delete(req);
    result = cJSON_Parse("{\"sensor.power\":{\"options\":{\"sensor\":{\"suggested_display_precision\":2}}}}");
    CHECK(ha_display_precision_handle_result(10, true, result, test_precision_changed));
    cJSON_Delete(result);
    CHECK_INT(s_precision_changes, HA_DISPLAY_PRECISION_BATCH);
    ha_display_precision_set_wanted(&ids[0][0], 20);
    req = ha_display_precision_next_request(11, 4000);
    cJSON_Delete(req);
    CHECK(ha_display_precision_handle_result(11, false, NULL, test_precision_changed));
    CHECK(!ha_display_precision_is_due(4000));
}

static void test_setup_passphrase(void)
{
    char out[SETUP_PASSPHRASE_MAX_LEN];

    /* Shape: word-word-NN, every character lower-case ASCII, a hyphen or a digit. */
    size_t n = setup_passphrase_format(out, sizeof(out), 0U, 1U, 7U);
    CHECK(n == strlen(out));
    CHECK(n >= 8U && n <= 63U);
    CHECK(strcmp(out, "acorn-adobe-07") == 0);
    CHECK(setup_passphrase_format(out, sizeof(out), 1024U, 2048U + 1U, 100U + 42U) > 0U);
    CHECK(strcmp(out, "acorn-adobe-42") == 0);

    /* The list: 1024 distinct words of 4 to 6 lower-case letters, so every output is 12..16 long. */
    for (size_t i = 0; i < SETUP_PASSPHRASE_WORD_COUNT; i++) {
        const char *w = setup_passphrase_word(i);
        size_t len = strlen(w);
        CHECK(len >= 4U && len <= 6U);
        for (size_t k = 0; k < len; k++) {
            CHECK(w[k] >= 'a' && w[k] <= 'z');
        }
        for (size_t j = i + 1U; j < SETUP_PASSPHRASE_WORD_COUNT; j++) {
            CHECK(strcmp(w, setup_passphrase_word(j)) != 0);
        }
    }
    CHECK(setup_passphrase_word(SETUP_PASSPHRASE_WORD_COUNT)[0] == '\0');

    /* The longest combination still fits and a short buffer yields an empty string. */
    size_t longest = 0U;
    for (size_t i = 0; i < SETUP_PASSPHRASE_WORD_COUNT; i++) {
        size_t l = strlen(setup_passphrase_word(i));
        longest = l > longest ? l : longest;
    }
    CHECK(2U * longest + 1U + 1U + 2U < SETUP_PASSPHRASE_MAX_LEN);
    char tiny[8];
    CHECK(setup_passphrase_format(tiny, sizeof(tiny), 0U, 1U, 2U) == 0U);
    CHECK(tiny[0] == '\0');
    CHECK(setup_passphrase_format(NULL, 10U, 0U, 1U, 2U) == 0U);
    CHECK(setup_passphrase_format(out, 0U, 0U, 1U, 2U) == 0U);
}

static void test_url_decode(void)
{
    char a[] = "K%C3%BCche";
    url_decode_in_place(a);
    CHECK(strcmp(a, "K\xc3\xbc" "che") == 0);
    char b[] = "Flur+OG%20oben";
    url_decode_in_place(b);
    CHECK(strcmp(b, "Flur OG oben") == 0);
    char c[] = "100%-%zz%4";
    url_decode_in_place(c);
    CHECK(strcmp(c, "100%-%zz%4") == 0);
    char d[] = "a%00b%2fc";
    url_decode_in_place(d);
    CHECK(strcmp(d, "a%00b/c") == 0);
    url_decode_in_place(NULL);
}

static void test_wifi_signal_level(void)
{
    CHECK_INT(wifi_signal_level(-45, WIFI_SIGNAL_UNKNOWN), WIFI_SIGNAL_STRONG);
    CHECK_INT(wifi_signal_level(-65, WIFI_SIGNAL_UNKNOWN), WIFI_SIGNAL_MEDIUM);
    CHECK_INT(wifi_signal_level(-80, WIFI_SIGNAL_UNKNOWN), WIFI_SIGNAL_WEAK);
    /* Hysteresis: 3 dB past a threshold before the shown level changes. */
    CHECK_INT(wifi_signal_level(-61, WIFI_SIGNAL_STRONG), WIFI_SIGNAL_STRONG);
    CHECK_INT(wifi_signal_level(-64, WIFI_SIGNAL_STRONG), WIFI_SIGNAL_MEDIUM);
    CHECK_INT(wifi_signal_level(-59, WIFI_SIGNAL_MEDIUM), WIFI_SIGNAL_MEDIUM);
    CHECK_INT(wifi_signal_level(-57, WIFI_SIGNAL_MEDIUM), WIFI_SIGNAL_STRONG);
    CHECK_INT(wifi_signal_level(-72, WIFI_SIGNAL_MEDIUM), WIFI_SIGNAL_MEDIUM);
    CHECK_INT(wifi_signal_level(-74, WIFI_SIGNAL_MEDIUM), WIFI_SIGNAL_WEAK);
    CHECK_INT(wifi_signal_level(-69, WIFI_SIGNAL_WEAK), WIFI_SIGNAL_WEAK);
    CHECK_INT(wifi_signal_level(-67, WIFI_SIGNAL_WEAK), WIFI_SIGNAL_MEDIUM);
    /* A jump over two levels still lands on the new one. */
    CHECK_INT(wifi_signal_level(-40, WIFI_SIGNAL_WEAK), WIFI_SIGNAL_STRONG);
    CHECK_INT(wifi_signal_level(-85, WIFI_SIGNAL_STRONG), WIFI_SIGNAL_WEAK);
}

static void test_update_check(void)
{
    CHECK(strcmp(update_check_variant_for_project("betta-ha-panel"), "panel4") == 0);
    CHECK(strcmp(update_check_variant_for_project("betta-ha-panel-jc"), "paneljc") == 0);
    CHECK(strcmp(update_check_variant_for_project("betta-ha-panel-jc-v2"), "paneljc-v2") == 0);
    CHECK(update_check_variant_for_project("other") == NULL);

    CHECK(update_check_compare_versions("v0.9.0-rc2", "v0.9.0", false) < 0);
    CHECK(update_check_compare_versions("v0.9.0-rc10", "v0.9.0-rc2", false) > 0);
    CHECK(update_check_compare_versions("v0.9.0-beta", "v0.9.0-rc1", false) < 0);
    CHECK(update_check_compare_versions("v0.10.0", "v0.9.9", false) > 0);
    CHECK(update_check_compare_versions("0.9.0", "v0.9.0", false) == 0);
    CHECK(update_check_compare_versions("v0.9.0-rc2", "v0.9.0", true) == 0);

    int64_t t = 0;
    CHECK(update_check_parse_iso8601_utc("2026-09-26T16:46:25Z", &t));
    CHECK(t == 1790441185LL);
    CHECK(!update_check_parse_iso8601_utc("2026-09-26 16:46:25", &t));

    static const char releases[] =
        "[{\"tag_name\":\"v0.9.1-rc1\",\"draft\":true,\"prerelease\":true,\"assets\":["
        "{\"name\":\"betta86-ha-panel-v0.9.1-rc1-paneljc.ota.bin\",\"updated_at\":\"2026-10-02T10:00:00Z\","
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v0.9.1-rc1/draft.ota.bin\"}]},"
        "{\"tag_name\":\"v0.9.0-rc2\",\"prerelease\":true,\"assets\":["
        "{\"name\":\"betta86-ha-panel-v0.9.0-rc2-paneljc-v2.ota.bin\",\"updated_at\":\"2026-09-26T16:46:26Z\","
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v0.9.0-rc2/v2.ota.bin\"},"
        "{\"name\":\"betta86-ha-panel-v0.9.0-rc2-paneljc.ota.bin\",\"updated_at\":\"2026-09-26T16:46:26Z\","
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v0.9.0-rc2/jc.ota.bin\"}]},"
        "{\"tag_name\":\"v0.8.2\",\"prerelease\":false,\"assets\":["
        "{\"name\":\"betta86-ha-panel-v0.8.2-paneljc.ota.bin\",\"updated_at\":\"2026-08-01T10:00:00Z\","
        "\"browser_download_url\":\"https://github.com/o/r/releases/download/v0.8.2/jc.ota.bin\"}]}]";
    update_check_release_t r;
    int64_t built = 0;
    CHECK(update_check_parse_iso8601_utc("2026-09-25T20:00:00Z", &built));
    /* Pre-releases included: the draft is skipped, rc2 wins over v0.8.2 and was uploaded a day after
     * the running v0.9.0 build (every build reports the plain version core). */
    CHECK(update_check_parse_releases(releases, strlen(releases), "paneljc", true, "v0.9.0", built, &r));
    CHECK(r.found && r.newer && r.prerelease);
    CHECK(strcmp(r.tag, "v0.9.0-rc2") == 0);
    CHECK(strcmp(r.asset_url, "https://github.com/o/r/releases/download/v0.9.0-rc2/jc.ota.bin") == 0);
    /* The same release, built minutes before its upload: not newer. */
    CHECK(update_check_parse_iso8601_utc("2026-09-26T16:30:00Z", &built));
    CHECK(update_check_parse_releases(releases, strlen(releases), "paneljc", true, "v0.9.0", built, &r));
    CHECK(r.found && !r.newer);
    /* Unknown build time: a same-version release is never offered. */
    CHECK(update_check_parse_releases(releases, strlen(releases), "paneljc", true, "v0.9.0", 0, &r));
    CHECK(r.found && !r.newer);
    /* Stable only: v0.8.2 is found but older. */
    CHECK(update_check_parse_releases(releases, strlen(releases), "paneljc", false, "v0.9.0", built, &r));
    CHECK(r.found && !r.newer && strcmp(r.tag, "v0.8.2") == 0);
    CHECK(update_check_parse_releases(releases, strlen(releases), "paneljc-v2", true, "v0.8.2", built, &r));
    CHECK(r.found && r.newer && strcmp(r.asset_url, "https://github.com/o/r/releases/download/v0.9.0-rc2/v2.ota.bin") == 0);
    CHECK(update_check_parse_releases(releases, strlen(releases), "panel4", true, "v0.9.0", built, &r));
    CHECK(!r.found && !r.newer);
    /* A running release candidate is compared as a whole: rc3 is newer than rc2 at once (no upload-time margin), the
     * final release is newer than any rc, an older rc is not, and the same rc needs the margin (a re-upload). */
    static const char rc_list[] =
        "[{\"tag_name\":\"v0.9.0-rc3\",\"prerelease\":true,\"assets\":[{\"name\":\"betta86-ha-panel-v0.9.0-rc3-panel4.ota.bin\","
        "\"updated_at\":\"2026-10-02T10:00:00Z\",\"browser_download_url\":\"https://github.com/o/r/rc3.ota.bin\"}]}]";
    int64_t built_rc = 0;
    CHECK(update_check_parse_iso8601_utc("2026-10-02T09:59:00Z", &built_rc)); /* built a minute before the upload */
    CHECK(update_check_parse_releases(rc_list, strlen(rc_list), "panel4", true, "v0.9.0-rc2", built_rc, &r));
    CHECK(r.found && r.newer);
    CHECK(update_check_parse_releases(rc_list, strlen(rc_list), "panel4", true, "v0.9.0-rc3", built_rc, &r));
    CHECK(r.found && !r.newer);
    CHECK(update_check_parse_releases(rc_list, strlen(rc_list), "panel4", true, "v0.9.0-rc4", built_rc, &r));
    CHECK(r.found && !r.newer);
    CHECK(update_check_parse_releases(rc_list, strlen(rc_list), "panel4", true, "v0.9.0", built_rc, &r));
    CHECK(r.found && !r.newer); /* a plain v0.9.0 only has the time margin */
    static const char final_list[] =
        "[{\"tag_name\":\"v0.9.0\",\"prerelease\":false,\"assets\":[{\"name\":\"betta86-ha-panel-v0.9.0-panel4.ota.bin\","
        "\"updated_at\":\"2026-10-02T10:00:00Z\",\"browser_download_url\":\"https://github.com/o/r/final.ota.bin\"}]}]";
    CHECK(update_check_parse_releases(final_list, strlen(final_list), "panel4", false, "v0.9.0-rc3", built_rc, &r));
    CHECK(r.found && r.newer);
    /* /releases/latest answers with one object. */
    static const char latest[] =
        "{\"tag_name\":\"v0.9.1\",\"prerelease\":false,\"assets\":[{\"name\":\"betta86-ha-panel-v0.9.1-panel4.ota.bin\","
        "\"updated_at\":\"2026-10-01T10:00:00Z\",\"browser_download_url\":\"https://github.com/o/r/a.ota.bin\"}]}";
    CHECK(update_check_parse_releases(latest, strlen(latest), "panel4", false, "v0.9.0", built, &r));
    CHECK(r.found && r.newer && !r.prerelease);
    CHECK(!update_check_parse_releases("{\"message\":", 11, "panel4", false, "v0.9.0", built, &r));

    /* GitHub's asset digest: "sha256:<64 hex>" is kept lowercase, anything else means no digest. */
    static const char with_digest[] =
        "{\"tag_name\":\"v0.9.1\",\"prerelease\":false,\"assets\":[{\"name\":\"betta86-ha-panel-v0.9.1-panel4.ota.bin\","
        "\"updated_at\":\"2026-10-01T10:00:00Z\",\"browser_download_url\":\"https://github.com/o/r/a.ota.bin\","
        "\"digest\":\"sha256:00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff\"}]}";
    CHECK(update_check_parse_releases(with_digest, strlen(with_digest), "panel4", false, "v0.9.0", built, &r));
    CHECK(strcmp(r.asset_sha256, "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff") == 0);
    CHECK(r.asset_sha256[0] == '\0' || strlen(r.asset_sha256) == 64);
    CHECK(update_check_parse_releases(latest, strlen(latest), "panel4", false, "v0.9.0", built, &r));
    CHECK(r.asset_sha256[0] == '\0');
    static const char bad_digest[] =
        "{\"tag_name\":\"v0.9.1\",\"prerelease\":false,\"assets\":[{\"name\":\"betta86-ha-panel-v0.9.1-panel4.ota.bin\","
        "\"browser_download_url\":\"https://github.com/o/r/a.ota.bin\",\"digest\":\"sha256:zz112233\"}]}";
    CHECK(update_check_parse_releases(bad_digest, strlen(bad_digest), "panel4", false, "v0.9.0", built, &r));
    CHECK(r.asset_sha256[0] == '\0');
    uint8_t bytes[32];
    for (int i = 0; i < 32; i++) bytes[i] = (uint8_t)((i % 16) * 17);
    CHECK(update_check_sha256_matches("00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff", bytes));
    bytes[31] ^= 1;
    CHECK(!update_check_sha256_matches("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", bytes));
    CHECK(!update_check_sha256_matches("0011", bytes));
    CHECK(!update_check_sha256_matches(NULL, bytes));
}

static void test_chart_scale(void)
{
    chart_scale_t s;
    chart_scale_nice(266.0f, 9618.0f, 4, &s); /* a household's load over a day */
    CHECK(s.min == 0.0f && s.max == 10000.0f && s.step == 2500.0f && s.intervals == 4 && s.decimals == 0);
    chart_scale_nice(24.5f, 27.3f, 4, &s);
    CHECK(s.min == 24.0f && s.max == 28.0f && s.step == 1.0f && s.intervals == 4 && s.decimals == 0);
    chart_scale_nice(0.0f, 100.0f, 4, &s);
    CHECK(s.min == 0.0f && s.max == 100.0f && s.step == 25.0f && s.decimals == 0);
    chart_scale_nice(0.11f, 0.19f, 4, &s);
    CHECK(s.min <= 0.11f && s.max >= 0.19f && s.decimals >= 2 && s.decimals <= 3);
    chart_scale_nice(-201.6f, 50.0f, 4, &s); /* a range across zero keeps a tick at zero */
    CHECK(s.min <= -201.6f && s.max >= 50.0f && fabsf(s.min / s.step - roundf(s.min / s.step)) < 1e-3f);
    chart_scale_nice(33.4f, 33.4f, 4, &s); /* a flat series still gets a range around it */
    CHECK(s.min < 33.4f && s.max > 33.4f && s.intervals >= 1);
    chart_scale_nice(0.0f, 0.0f, 4, &s);
    CHECK(s.max > s.min);
    chart_scale_nice(5.0f, 1.0f, 99, &s); /* reversed input and an oversized wish are tamed */
    CHECK(s.intervals >= 1 && s.intervals <= CHART_SCALE_MAX_INTERVALS);

    char text[24];
    CHECK(strcmp(chart_scale_format(text, sizeof(text), 2500.0f, 0, "W"), "2500 W") == 0);
    CHECK(strcmp(chart_scale_format(text, sizeof(text), -0.04f, 1, ""), "0.0") == 0);
    CHECK(strcmp(chart_scale_format(text, sizeof(text), -201.6f, 1, "W"), "-201.6 W") == 0);
    char unit[12];
    chart_scale_unit_of("506 W", unit, sizeof(unit)); CHECK(strcmp(unit, "W") == 0);
    chart_scale_unit_of("33.4 %", unit, sizeof(unit)); CHECK(strcmp(unit, "%") == 0);
    chart_scale_unit_of("25.7 \xC2\xB0" "C", unit, sizeof(unit)); CHECK(strcmp(unit, "\xC2\xB0" "C") == 0);
    chart_scale_unit_of("12", unit, sizeof(unit)); CHECK(unit[0] == '\0');
    chart_scale_unit_of("12 kWh 4", unit, sizeof(unit)); CHECK(unit[0] == '\0');
    chart_scale_unit_of(NULL, unit, sizeof(unit)); CHECK(unit[0] == '\0');
}

static void test_nav_layout(void)
{
    nav_layout_t l;
    int title[NAV_MAX_TABS];
    for (int i = 0; i < NAV_MAX_TABS; i++) title[i] = 80; /* "Old School" at 16 px is about 90 */

    /* a single page is centered */
    nav_layout_compute(&l, 720, 1, 0, title);
    CHECK(!l.compact && l.count == 1 && l.tab[0].x + l.tab[0].w / 2 == 360);

    /* five pages on the 4-inch panel: full tabs left to right, in order, inside the margins, never overlapping */
    nav_layout_compute(&l, 720, 5, 0, title);
    CHECK(!l.compact && l.count == 5);
    for (int i = 0; i < 5; i++) {
        CHECK(l.tab[i].show_title && l.tab[i].w >= (i == 0 ? NAV_HOME_MIN_W : NAV_FULL_TAB_MIN_W));
        CHECK(l.tab[i].x >= NAV_MARGIN && l.tab[i].x + l.tab[i].w <= 720 - NAV_MARGIN);
        if (i > 0) CHECK(l.tab[i].x >= l.tab[i - 1].x + l.tab[i - 1].w + NAV_GAP);
    }
    /* equal tabs when the first title fits an equal share, never narrower than title + overhead (no dots in the first tab) */
    int arbeits[NAV_MAX_TABS];
    for (int i = 0; i < NAV_MAX_TABS; i++) arbeits[i] = 112; /* "Arbeitszimmer" at 16 px */
    nav_layout_compute(&l, 1280, 5, 0, arbeits);
    CHECK(!l.compact);
    for (int i = 1; i < 5; i++) CHECK_INT(l.tab[i].w, l.tab[0].w);
    CHECK(l.tab[0].w >= NAV_TAB_OVERHEAD + 112);
    CHECK(l.tab[0].x >= NAV_MARGIN && l.tab[4].x + l.tab[4].w <= 1280 - NAV_MARGIN);
    for (int i = 1; i < 5; i++) CHECK(l.tab[i].x == l.tab[i - 1].x + l.tab[i - 1].w + NAV_GAP);
    /* a long first title that does not fit an equal share: the first tab grows to fit it, the rest stay equal */
    arbeits[0] = 150;
    nav_layout_compute(&l, 720, 4, 0, arbeits);
    CHECK(!l.compact && l.tab[0].w > l.tab[1].w);
    CHECK(l.tab[0].w >= NAV_TAB_OVERHEAD + 150 && l.tab[0].w <= 720 / 3);
    for (int i = 2; i < 4; i++) CHECK_INT(l.tab[i].w, l.tab[1].w);
    /* any first title up to the cap keeps title + overhead */
    for (int tw = 20; tw <= 200; tw += 20) {
        int one[NAV_MAX_TABS];
        for (int i = 0; i < NAV_MAX_TABS; i++) one[i] = 60;
        one[0] = tw;
        nav_layout_compute(&l, 1280, 4, 0, one);
        CHECK(!l.compact && l.tab[0].w >= (NAV_TAB_OVERHEAD + tw < NAV_HOME_MIN_W ? NAV_HOME_MIN_W : NAV_TAB_OVERHEAD + tw));
    }
    /* one page more than fits with titles: compact, the shown page keeps its title */
    nav_layout_compute(&l, 720, 6, 2, title);
    CHECK(l.compact && l.count == 6);
    for (int i = 0; i < 6; i++) {
        CHECK(l.tab[i].show_title == (i == 2));
        CHECK(l.tab[i].w >= NAV_COMPACT_MIN_W && l.tab[i].x >= NAV_MARGIN && l.tab[i].x + l.tab[i].w <= 720 - NAV_MARGIN);
        if (i > 0) CHECK(l.tab[i].x >= l.tab[i - 1].x + l.tab[i - 1].w + NAV_GAP);
    }
    CHECK(l.tab[2].w > l.tab[0].w);
    /* the page limit of both panels fits at the worst case: a title that needs the widest tab, any page shown */
    int widest[NAV_MAX_TABS];
    for (int i = 0; i < NAV_MAX_TABS; i++) widest[i] = 400;
    const int max4 = nav_layout_max_pages(720), max10 = nav_layout_max_pages(1280);
    CHECK_INT(max4, 8);
    CHECK(max10 >= 12);
    CHECK(APP_MAX_PAGES <= nav_layout_max_pages(APP_SCREEN_WIDTH)); /* the page limit of this build fits its own bar */
    for (int shown = 0; shown < max4; shown++) {
        nav_layout_compute(&l, 720, max4, shown, widest);
        for (int i = 0; i < max4; i++) {
            CHECK(l.tab[i].x >= NAV_MARGIN && l.tab[i].x + l.tab[i].w <= 720 - NAV_MARGIN);
            if (i > 0) CHECK(l.tab[i].x >= l.tab[i - 1].x + l.tab[i - 1].w + NAV_GAP);
        }
    }
    for (int shown = 0; shown < max10; shown++) {
        nav_layout_compute(&l, 1280, max10, shown, widest);
        CHECK(l.tab[max10 - 1].x + l.tab[max10 - 1].w <= 1280 - NAV_MARGIN);
    }
    /* no page shown (a page of a window): all icons */
    nav_layout_compute(&l, 720, 8, -1, title);
    CHECK(l.compact);
    for (int i = 0; i < 8; i++) CHECK(!l.tab[i].show_title);
    /* where compact begins */
    CHECK(nav_layout_full_pages(720, 80) >= 5 && nav_layout_full_pages(720, 80) < max4);
    CHECK(nav_layout_full_pages(1280, 80) >= 7 && nav_layout_full_pages(1280, 80) < max10);
    nav_layout_compute(&l, 720, 0, 0, NULL);
    CHECK(l.count == 0);
    nav_layout_compute(&l, 720, 99, 0, title); /* clamped */
    CHECK(l.count == NAV_MAX_TABS);
}

static void test_net_watchdog(void)
{
    net_wd_state_t st;
    net_wd_init(&st);
    net_wd_input_t up = {.wifi_expected = true, .link_up = true, .ha_connected = true, .ping = 1, .ping_ever_ok = true, .http = 1};
    CHECK_INT(net_wd_step(&st, &up, 100), NET_WD_OK);
    CHECK(!net_wd_may_clear_restarts(&st, 100 + NET_WD_CLEAR_AFTER_S - 1));
    CHECK(net_wd_may_clear_restarts(&st, 100 + NET_WD_CLEAR_AFTER_S));

    /* the link drops: nothing before the window, a restart at its end */
    net_wd_input_t down = up;
    down.link_up = false;
    down.ping = -1;
    CHECK_INT(net_wd_step(&st, &down, 1000), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &down, 1000 + NET_WD_LINK_DOWN_S - 1), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &down, 1000 + NET_WD_LINK_DOWN_S), NET_WD_RESTART_LINK);
    down.restarts_in_a_row = NET_WD_MAX_RESTARTS; /* bounded: a router outage cannot reboot the panel forever */
    CHECK_INT(net_wd_step(&st, &down, 1000 + NET_WD_LINK_DOWN_S + 30), NET_WD_SUPPRESSED);
    CHECK_INT(net_wd_step(&st, &up, 2000), NET_WD_OK); /* the link returns: the timers start over */
    CHECK(st.link_down_since < 0);
    CHECK_INT(net_wd_step(&st, &down, 2010), NET_WD_OK);
    CHECK(st.link_down_since == 2010);

    /* never inside the boot grace period, even when the timers ran out */
    net_wd_init(&st);
    down.restarts_in_a_row = 0;
    CHECK_INT(net_wd_step(&st, &down, 0), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &down, NET_WD_BOOT_GRACE_S - 1), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &down, NET_WD_LINK_DOWN_S), NET_WD_RESTART_LINK);

    /* a link that looks up but a gateway that stopped answering, with no Home Assistant traffic */
    net_wd_init(&st);
    net_wd_input_t silent = up;
    silent.ping = 0;
    silent.ha_connected = false;
    CHECK_INT(net_wd_step(&st, &silent, 1000), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &silent, 1000 + NET_WD_PING_DEAD_S - 1), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &silent, 1000 + NET_WD_PING_DEAD_S), NET_WD_RESTART_TRANSPORT);
    CHECK(st.alive_since < 0);
    silent.ha_connected = true; /* Home Assistant traffic proves the transport works */
    CHECK_INT(net_wd_step(&st, &silent, 1000 + NET_WD_PING_DEAD_S + 30), NET_WD_OK);
    silent.ha_connected = false;
    silent.ping_ever_ok = false; /* a router that never answers pings must not cause restarts */
    net_wd_init(&st);
    CHECK_INT(net_wd_step(&st, &silent, 1000), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &silent, 1000 + 5 * NET_WD_PING_DEAD_S), NET_WD_OK);
    silent.ping_ever_ok = true;
    silent.ping = 1; /* one answer clears the gateway timer */
    CHECK_INT(net_wd_step(&st, &silent, 9000), NET_WD_OK);
    CHECK(st.ping_fail_since < 0);

    /* the panel's own web server stops answering while everything else looks fine */
    net_wd_init(&st);
    net_wd_input_t hung = up;
    hung.http = 0;
    CHECK_INT(net_wd_step(&st, &hung, 1000), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &hung, 1000 + NET_WD_HTTP_DEAD_S - 1), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &hung, 1000 + NET_WD_HTTP_DEAD_S), NET_WD_RESTART_HTTP);
    hung.http = 1; /* an answer clears it */
    CHECK_INT(net_wd_step(&st, &hung, 1000 + NET_WD_HTTP_DEAD_S + 5), NET_WD_OK);
    CHECK(st.http_fail_since < 0);
    hung.http = -1; /* no probe yet never counts */
    CHECK_INT(net_wd_step(&st, &hung, 5000), NET_WD_OK);
    CHECK(st.http_fail_since < 0);
    net_wd_input_t hung_ap = hung; /* also in setup AP mode, where there is no network to expect */
    hung_ap.wifi_expected = false;
    hung_ap.http = 0;
    net_wd_init(&st);
    CHECK_INT(net_wd_step(&st, &hung_ap, 1000), NET_WD_OK);
    CHECK_INT(net_wd_step(&st, &hung_ap, 1000 + NET_WD_HTTP_DEAD_S), NET_WD_RESTART_HTTP);
    hung_ap.restarts_in_a_row = NET_WD_MAX_RESTARTS;
    CHECK_INT(net_wd_step(&st, &hung_ap, 1000 + NET_WD_HTTP_DEAD_S + 30), NET_WD_SUPPRESSED);

    /* no network configured, or setup AP mode: nothing to watch */
    net_wd_init(&st);
    net_wd_input_t none = down;
    none.wifi_expected = false;
    CHECK_INT(net_wd_step(&st, &none, 100000), NET_WD_OK);
}

static void test_reboot_log(void)
{
    reboot_crumb_t crumb = {.uptime_s = 1234, .min_free_internal = 70000, .largest_internal = 34816, .wall_epoch = 1790800000,
                            .cause = REBOOT_CAUSE_SETTINGS};
    CHECK(!reboot_crumb_valid(&crumb)); /* not sealed yet */
    reboot_crumb_seal(&crumb);
    CHECK(reboot_crumb_valid(&crumb));
    crumb.uptime_s++;
    CHECK(!reboot_crumb_valid(&crumb)); /* any change breaks the checksum until it is sealed again */
    reboot_crumb_seal(&crumb);
    CHECK(reboot_crumb_valid(&crumb));
    CHECK(!reboot_crumb_valid(NULL));

    reboot_entry_t e;
    reboot_entry_build(&e, 0, 3 /* software */, &crumb);
    CHECK(e.had_crumb == 1 && e.cause == REBOOT_CAUSE_SETTINGS && e.prev_uptime_s == 1235 && e.largest_internal == 34816);
    CHECK(strcmp(reboot_entry_label(&e), "settings restart") == 0);
    reboot_entry_build(&e, 0, 4 /* panic */, &crumb); /* a stale tag does not explain a crash */
    CHECK(e.had_crumb == 1 && e.cause == REBOOT_CAUSE_NONE && strcmp(reboot_entry_label(&e), "panic") == 0);
    reboot_entry_build(&e, 0, 6, &crumb);
    CHECK(strcmp(reboot_entry_label(&e), "task watchdog") == 0 && e.prev_uptime_s == 1235);
    reboot_entry_build(&e, 0, 1 /* power on */, &crumb); /* RTC memory is garbage after a power cycle */
    CHECK(e.had_crumb == 0 && e.prev_uptime_s == 0 && strcmp(reboot_entry_label(&e), "power-on") == 0);
    reboot_entry_build(&e, 0, 9 /* brownout */, &crumb);
    CHECK(e.had_crumb == 0 && strcmp(reboot_entry_label(&e), "brownout") == 0);
    reboot_entry_build(&e, 0, 3, NULL);
    CHECK(e.had_crumb == 0 && strcmp(reboot_entry_label(&e), "software restart") == 0);
    crumb.cause = REBOOT_CAUSE_NONE; reboot_crumb_seal(&crumb);
    reboot_entry_build(&e, 0, 3, &crumb);
    CHECK(strcmp(reboot_entry_label(&e), "software restart (untagged)") == 0);
    crumb.cause = 200; reboot_crumb_seal(&crumb); /* an unknown cause is treated as a damaged breadcrumb */
    reboot_entry_build(&e, 0, 3, &crumb);
    CHECK(e.had_crumb == 0);

    reboot_ring_t ring;
    memset(&ring, 0xAB, sizeof(ring)); /* garbage starts over */
    reboot_ring_init_if_invalid(&ring);
    CHECK(ring.count == 0 && ring.next_seq == 0);
    CHECK(!reboot_ring_get(&ring, 0, &e));
    for (int i = 1; i <= 11; i++) {
        reboot_entry_t item = {.reset_reason = (uint8_t)(i % 10), .prev_uptime_s = (uint32_t)i};
        reboot_ring_push(&ring, &item);
        CHECK(item.seq == (uint32_t)i);
    }
    CHECK(ring.count == REBOOT_LOG_SLOTS);
    for (int i = 0; i < REBOOT_LOG_SLOTS; i++) { /* newest first, the three oldest were overwritten */
        CHECK(reboot_ring_get(&ring, i, &e) && e.seq == (uint32_t)(11 - i) && e.prev_uptime_s == (uint32_t)(11 - i));
    }
    CHECK(!reboot_ring_get(&ring, REBOOT_LOG_SLOTS, &e) && !reboot_ring_get(&ring, -1, &e));
    ring.head = 200; /* a damaged ring starts over rather than indexing out of range */
    reboot_ring_init_if_invalid(&ring);
    CHECK(ring.count == 0 && ring.head == 0);
    CHECK(strcmp(reboot_cause_name(REBOOT_CAUSE_OTA), "ota") == 0 && strcmp(reboot_reason_name(200), "other") == 0);
}

static auto_update_input_t auto_update_ready(void)
{
    /* Everything fine, 03:30 with a start offset of 10, a release v0.9.0-rc4 that was not tried before. */
    auto_update_input_t in = {
        .enabled = true, .update_available = true, .sha256_known = true, .bootloader_rollback = true,
        .time_valid = true, .local_minute_of_day = 3 * 60 + 30, .start_offset_min = 10, .idle_s = 3600,
        .wifi_connected = true, .rssi_dbm = -60, .free_internal = 300 * 1024, .largest_internal = 150 * 1024,
        .free_psram = 12U * 1024 * 1024, .release_tag = "v0.9.0-rc4", .blocked_tag = "",
    };
    return in;
}

static void test_auto_update(void)
{
    auto_update_input_t in = auto_update_ready();
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    CHECK(strcmp(auto_update_decision_name(AUTO_UPDATE_GO), "go") == 0);
    CHECK_INT(auto_update_decide(NULL), AUTO_UPDATE_WAIT_DISABLED);

    in = auto_update_ready(); in.enabled = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_DISABLED);
    in = auto_update_ready(); in.busy = true;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_BUSY);
    in = auto_update_ready(); in.safe_mode = true;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_SAFE_MODE);
    in = auto_update_ready(); in.update_available = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_NO_UPDATE);
    /* A version that rolled back is not tried again, but a newer one is. */
    in = auto_update_ready(); in.blocked_tag = "v0.9.0-rc4";
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_BLOCKED_VERSION);
    in = auto_update_ready(); in.blocked_tag = "v0.9.0-rc3";
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    in = auto_update_ready(); in.tried_today = true;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_TRIED_TODAY);
    in = auto_update_ready(); in.sha256_known = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_NO_SHA256);
    /* Without a rollback-capable bootloader a bad image would leave the panel stuck. */
    in = auto_update_ready(); in.bootloader_rollback = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_NO_ROLLBACK);
    in = auto_update_ready(); in.time_valid = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_TIME_UNKNOWN);

    /* The window is 03:00 to 04:00, and the panel starts at its own minute. */
    in = auto_update_ready(); in.local_minute_of_day = 2 * 60 + 59;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_OUTSIDE_WINDOW);
    in = auto_update_ready(); in.local_minute_of_day = 3 * 60 + 9;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_START_MINUTE);
    in = auto_update_ready(); in.local_minute_of_day = 3 * 60 + 10;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    in = auto_update_ready(); in.local_minute_of_day = 3 * 60 + 59;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    in = auto_update_ready(); in.local_minute_of_day = 4 * 60;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_OUTSIDE_WINDOW);
    in = auto_update_ready(); in.local_minute_of_day = 15 * 60;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_OUTSIDE_WINDOW);

    in = auto_update_ready(); in.idle_s = AUTO_UPDATE_IDLE_MIN_S - 1;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_ACTIVITY);
    in = auto_update_ready(); in.idle_s = AUTO_UPDATE_IDLE_MIN_S;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    in = auto_update_ready(); in.wifi_connected = false;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_WIFI);
    in = auto_update_ready(); in.rssi_dbm = AUTO_UPDATE_MIN_RSSI_DBM - 1;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_WIFI);
    in = auto_update_ready(); in.rssi_dbm = AUTO_UPDATE_MIN_RSSI_DBM;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_GO);
    in = auto_update_ready(); in.free_internal = AUTO_UPDATE_MIN_FREE_INTERNAL - 1;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_MEMORY);
    in = auto_update_ready(); in.largest_internal = AUTO_UPDATE_MIN_LARGEST_INTERNAL - 1;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_MEMORY);
    in = auto_update_ready(); in.free_psram = AUTO_UPDATE_MIN_FREE_PSRAM - 1;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_MEMORY);

    /* Several reasons at once: the first in the list names it (disabled before everything, memory last). */
    in = auto_update_ready(); in.enabled = false; in.bootloader_rollback = false; in.free_psram = 0;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_DISABLED);
    in = auto_update_ready(); in.bootloader_rollback = false; in.idle_s = 0; in.free_psram = 0;
    CHECK_INT(auto_update_decide(&in), AUTO_UPDATE_WAIT_NO_ROLLBACK);

    /* The start minute is within the window's first hour, differs by day and by panel, and is stable for one input. */
    int seen[60] = {0};
    for (int day = 20000; day < 20400; day++) {
        const int m = auto_update_start_offset_min(0xA1B2C3D4U, day);
        CHECK(m >= 0 && m < 60);
        CHECK_INT(m, auto_update_start_offset_min(0xA1B2C3D4U, day));
        seen[m]++;
    }
    int distinct = 0;
    for (int m = 0; m < 60; m++) distinct += seen[m] > 0;
    CHECK(distinct >= 50);
    int differing = 0;
    for (int day = 20000; day < 20100; day++) {
        differing += auto_update_start_offset_min(0x11111111U, day) != auto_update_start_offset_min(0x22222222U, day);
    }
    CHECK(differing >= 80);
    for (int d = AUTO_UPDATE_GO; d <= AUTO_UPDATE_WAIT_MEMORY; d++) {
        CHECK(strncmp(auto_update_decision_name((auto_update_decision_t)d), "wait_", 5) == 0 || d == AUTO_UPDATE_GO);
    }

    /* Activity: without any since boot the idle time is the time since boot; after one it counts from there. */
    CHECK(user_activity_idle_s(100) == 100);
    user_activity_note(150);
    CHECK(user_activity_idle_s(150) == 0);
    CHECK(user_activity_idle_s(750) == 600);
    user_activity_note(800);
    CHECK(user_activity_idle_s(1000) == 200);
}

static void sha256_hex(const char *msg, size_t repeat, size_t chunk, char out[65])
{
    sha256_soft_t ctx;
    sha256_soft_init(&ctx);
    const size_t len = strlen(msg);
    uint8_t buf[1000];
    size_t filled = 0;
    for (size_t r = 0; r < repeat; r++) {
        for (size_t i = 0; i < len; i++) {
            buf[filled++] = (uint8_t)msg[i];
            if (filled == chunk) { sha256_soft_update(&ctx, buf, filled); filled = 0; }
        }
    }
    if (filled) sha256_soft_update(&ctx, buf, filled);
    uint8_t d[32];
    sha256_soft_final(&ctx, d);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

static void test_sha256_soft(void)
{
    char h[65];
    /* FIPS 180-4 / NIST vectors, also split into odd chunk sizes so the block buffering is exercised. */
    sha256_hex("", 1, 7, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    sha256_hex("abc", 1, 1, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, 13, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    sha256_hex("a", 1000000, 997, h);
    CHECK(strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    sha256_hex("a", 1000000, 64, h);
    CHECK(strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
}

static void test_auto_update_state(void)
{
    auto_update_state_t st;
    auto_update_state_init(&st);
    CHECK_INT(st.version, AUTO_UPDATE_STATE_VERSION);
    /* No attempt: a restart changes nothing. */
    CHECK(!auto_update_state_resolve_boot(&st, "v0.9.0-rc3", false, 100));
    CHECK_INT(st.result, AUTO_UPDATE_RESULT_NONE);

    /* The panel came up on the version it installed. */
    auto_update_state_begin(&st, "v0.9.0-rc4", 1000, 20500);
    CHECK(strcmp(st.attempt_tag, "v0.9.0-rc4") == 0);
    CHECK_INT(st.last_attempt_day, 20500);
    CHECK(auto_update_state_resolve_boot(&st, "v0.9.0-rc4", false, 1100));
    CHECK_INT(st.result, AUTO_UPDATE_RESULT_INSTALLED);
    CHECK(strcmp(st.result_tag, "v0.9.0-rc4") == 0 && st.attempt_tag[0] == '\0' && st.blocked_tag[0] == '\0' && !st.notice_pending);
    CHECK(!auto_update_state_resolve_boot(&st, "v0.9.0-rc4", false, 1200)); /* only once */

    /* A crashing image: the bootloader returned to the old firmware after a failure. The version is blocked and the panel says so. */
    auto_update_state_begin(&st, "v0.9.0-rc5", 2000, 20501);
    CHECK(auto_update_state_resolve_boot(&st, "v0.9.0-rc4", true, 2100));
    CHECK_INT(st.result, AUTO_UPDATE_RESULT_ROLLED_BACK);
    CHECK(strcmp(st.blocked_tag, "v0.9.0-rc5") == 0 && st.notice_pending == 1);
    auto_update_state_ack_notice(&st);
    CHECK(st.notice_pending == 0 && strcmp(st.blocked_tag, "v0.9.0-rc5") == 0);

    /* A different version without a failure (a power cut during the install): failed, not blocked. */
    auto_update_state_begin(&st, "v0.9.0-rc6", 3000, 20502);
    CHECK(auto_update_state_resolve_boot(&st, "v0.9.0-rc4", false, 3100));
    CHECK_INT(st.result, AUTO_UPDATE_RESULT_FAILED);
    CHECK(strcmp(st.blocked_tag, "v0.9.0-rc5") == 0 && !st.notice_pending); /* the earlier block stays, rc6 is not added */

    /* The install failed before the restart. */
    auto_update_state_begin(&st, "v0.9.0-rc6", 4000, 20503);
    auto_update_state_failed(&st, 4050);
    CHECK_INT(st.result, AUTO_UPDATE_RESULT_FAILED);
    CHECK(st.attempt_tag[0] == '\0' && strcmp(st.result_tag, "v0.9.0-rc6") == 0 && st.result_epoch == 4050);
    CHECK(strcmp(auto_update_result_name(AUTO_UPDATE_RESULT_ROLLED_BACK), "rolled_back") == 0);
    CHECK(strcmp(auto_update_result_name(AUTO_UPDATE_RESULT_NONE), "") == 0);

    /* What counts as a failed run. */
    reboot_entry_t e;
    memset(&e, 0, sizeof(e));
    e.reset_reason = 4; CHECK(reboot_entry_is_failure(&e));       /* panic */
    e.reset_reason = 6; CHECK(reboot_entry_is_failure(&e));       /* task watchdog */
    e.reset_reason = 1; CHECK(!reboot_entry_is_failure(&e));      /* power on */
    e.reset_reason = 2; CHECK(!reboot_entry_is_failure(&e));      /* reset button */
    e.reset_reason = 3; e.cause = REBOOT_CAUSE_OTA; CHECK(!reboot_entry_is_failure(&e));
    e.cause = REBOOT_CAUSE_SETTINGS; CHECK(!reboot_entry_is_failure(&e));
    e.cause = REBOOT_CAUSE_MANUAL; CHECK(!reboot_entry_is_failure(&e) && strcmp(reboot_entry_label(&e), "manual restart") == 0);
    e.cause = REBOOT_CAUSE_HTTP_WATCHDOG; CHECK(reboot_entry_is_failure(&e));
    e.cause = REBOOT_CAUSE_NET_WATCHDOG; CHECK(reboot_entry_is_failure(&e));
    e.reset_reason = 4; e.cause = REBOOT_CAUSE_UI_WATCHDOG; CHECK(reboot_entry_is_failure(&e)); /* abort() after the mark */
    CHECK(strcmp(reboot_entry_label(&e), "UI watchdog") == 0);
    CHECK(strcmp(reboot_cause_name(REBOOT_CAUSE_UI_WATCHDOG), "ui_watchdog") == 0);
    CHECK(strcmp(reboot_cause_name(REBOOT_CAUSE_MANUAL), "manual") == 0);
}

static void test_ui_watchdog(void)
{
    /* The beat stamp is never 0 ("no beat yet"), also where the millisecond count wraps after 49.7 days. */
    CHECK_INT((int)ui_wd_stamp_ms(0), 1);
    CHECK_INT((int)ui_wd_stamp_ms(1000 * 1000LL), 1001);
    CHECK(ui_wd_stamp_ms(0xFFFFFFFELL * 1000) == 0xFFFFFFFFU);
    CHECK(ui_wd_stamp_ms(0xFFFFFFFFLL * 1000) != 0U); /* the millisecond that used to give 0 */
    CHECK(ui_wd_stamp_ms(0x100000000LL * 1000) != 0U);
    CHECK((uint32_t)(ui_wd_stamp_ms(0x100000005LL * 1000) - ui_wd_stamp_ms(0xFFFFFFFELL * 1000)) == 7U); /* ages stay right across the wrap */

    ui_wd_state_t st;
    ui_wd_init(&st);
    ui_wd_input_t in = {.lvgl_age_s = 1, .runtime_age_s = 1, .restarts_in_a_row = 0};

    /* Nothing happens during the boot grace, even with silent heartbeats. */
    in.lvgl_age_s = 500;
    CHECK(ui_wd_step(&st, &in, UI_WD_BOOT_GRACE_S - 1) == UI_WD_OK);
    /* A healthy UI is alive and may clear the restart counter after UI_WD_CLEAR_AFTER_S. */
    in.lvgl_age_s = 1;
    CHECK(ui_wd_step(&st, &in, 200) == UI_WD_OK);
    CHECK(!ui_wd_may_clear_restarts(&st, 200 + UI_WD_CLEAR_AFTER_S - 1));
    CHECK(ui_wd_may_clear_restarts(&st, 200 + UI_WD_CLEAR_AFTER_S));
    /* No beat yet (-1) is not a stall. */
    in.lvgl_age_s = -1; in.runtime_age_s = -1;
    CHECK(ui_wd_step(&st, &in, 400) == UI_WD_OK);
    /* Either heartbeat silent for UI_WD_STALL_S asks for a restart and says which. */
    in.lvgl_age_s = 2; in.runtime_age_s = UI_WD_STALL_S - 1;
    CHECK(ui_wd_step(&st, &in, 500) == UI_WD_OK);
    in.runtime_age_s = UI_WD_STALL_S;
    CHECK(ui_wd_step(&st, &in, 510) == UI_WD_RESTART_RUNTIME);
    in.lvgl_age_s = UI_WD_STALL_S; in.runtime_age_s = 3;
    CHECK(ui_wd_step(&st, &in, 520) == UI_WD_RESTART_LVGL);
    CHECK(!ui_wd_may_clear_restarts(&st, 100000)); /* a stall resets the "alive" clock */
    /* The restarts in a row are bounded. */
    in.restarts_in_a_row = UI_WD_MAX_RESTARTS - 1U;
    CHECK(ui_wd_step(&st, &in, 530) == UI_WD_RESTART_LVGL);
    in.restarts_in_a_row = UI_WD_MAX_RESTARTS;
    CHECK(ui_wd_step(&st, &in, 540) == UI_WD_SUPPRESSED);
}

static void test_ota_chip_rev(void)
{
    /* esp_image_header_t of an ESP32-P4 image: magic, 3 bytes, entry, wp_pin, 3 drive bytes, chip id 0x12, min rev 0,
     * min_full 100 (v1.0), max_full 199 (v1.99). */
    uint8_t head[24] = {0xE9, 4, 2, 0x2F, 0, 0, 0, 0x4F, 0xEE, 0, 0, 0, 0x12, 0, 0, 100, 0, 199, 0};
    ota_image_revs_t revs;
    CHECK(ota_image_revs_parse(head, sizeof(head), &revs));
    CHECK_INT(revs.min_rev, 100);
    CHECK_INT(revs.max_rev, 199);
    CHECK(ota_image_revs_check(&revs, 103) == OTA_REV_OK);     /* v1.3, the standard image's chip */
    CHECK(ota_image_revs_check(&revs, 100) == OTA_REV_OK);
    CHECK(ota_image_revs_check(&revs, 199) == OTA_REV_OK);
    CHECK(ota_image_revs_check(&revs, 301) == OTA_REV_ABOVE_MAX); /* v3.1 chip, v1.x image */
    CHECK(ota_image_revs_check(&revs, 99) == OTA_REV_BELOW_MIN);

    /* A v3 image: min 300, max 399 (the bytes are little endian: 300 = 0x012C, 399 = 0x018F). */
    head[15] = 0x2C; head[16] = 0x01; head[17] = 0x8F; head[18] = 0x01;
    CHECK(ota_image_revs_parse(head, sizeof(head), &revs));
    CHECK_INT(revs.min_rev, 300);
    CHECK_INT(revs.max_rev, 399);
    CHECK(ota_image_revs_check(&revs, 103) == OTA_REV_BELOW_MIN);
    CHECK(ota_image_revs_check(&revs, 301) == OTA_REV_OK);

    /* No upper limit. */
    head[17] = 0xFF; head[18] = 0xFF;
    CHECK(ota_image_revs_parse(head, sizeof(head), &revs));
    CHECK(ota_image_revs_check(&revs, 399) == OTA_REV_OK);

    /* Not an image, or too short. */
    head[0] = 0x00;
    CHECK(!ota_image_revs_parse(head, sizeof(head), &revs));
    head[0] = 0xE9;
    CHECK(!ota_image_revs_parse(head, 18, &revs));
    CHECK(!ota_image_revs_parse(NULL, 24, &revs));

    char text[128];
    CHECK_INT(ota_rev_format(103, text, sizeof(text)), 4); CHECK(strcmp(text, "v1.3") == 0);
    ota_rev_format(301, text, sizeof(text)); CHECK(strcmp(text, "v3.1") == 0);
    ota_image_revs_t v1 = {100, 199};
    ota_rev_mismatch_text(&v1, 301, text, sizeof(text));
    CHECK(strstr(text, "v1.0 to v1.99") != NULL);
    CHECK(strstr(text, "this chip is v3.1") != NULL);
    CHECK(strstr(text, "a v3 image is needed") != NULL);
    char tiny[8];
    ota_rev_mismatch_text(&v1, 301, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
}

static void test_history_feed_policy(void)
{
    history_feed_slot_t slots[HISTORY_FEED_MAX] = {0};
    history_feed_state_t st;
    history_feed_state_init(&st);
    const int64_t t0 = 1000000;

    history_feed_decision_t d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0);
    CHECK_INT(d.next, -1);                      /* nothing registered */
    slots[0] = (history_feed_slot_t){.used = true, .visible = false};
    slots[1] = (history_feed_slot_t){.used = true, .visible = true};
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0);
    CHECK_INT(d.next, 1);                       /* only a widget on screen is fetched, a never fetched one at once */
    slots[0].visible = true;
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0);
    CHECK_INT(d.next, 0);                       /* equal: the first one */

    /* one request at a time */
    st.inflight = 0; st.inflight_since_ms = t0;
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + 1000);
    CHECK_INT(d.next, -1); CHECK_INT(d.timed_out, -1);
    /* an unanswered request is forgotten after the timeout; the other widget goes next, not the failed one */
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + HISTORY_FEED_TIMEOUT_MS);
    CHECK_INT(d.timed_out, 0); CHECK_INT(d.next, 1);

    /* after an answer: fresh until the refresh interval, then due again */
    st.inflight = -1;
    slots[0].last_ok_ms = t0; slots[0].last_try_ms = t0;
    slots[1].last_ok_ms = t0 + 10; slots[1].last_try_ms = t0 + 10;
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + HISTORY_FEED_REFRESH_MS - 1);
    CHECK_INT(d.next, -1);
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + HISTORY_FEED_REFRESH_MS + 20);
    CHECK_INT(d.next, 0);                       /* both due: the older one first */

    /* a failed try pauses that widget */
    slots[0].last_try_ms = t0 + HISTORY_FEED_REFRESH_MS + 20;
    slots[1].last_try_ms = t0 + HISTORY_FEED_REFRESH_MS + 20;
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + HISTORY_FEED_REFRESH_MS + 20 + HISTORY_FEED_RETRY_MS - 1);
    CHECK_INT(d.next, -1);
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + HISTORY_FEED_REFRESH_MS + 20 + HISTORY_FEED_RETRY_MS);
    CHECK_INT(d.next, 0);
    /* a widget on a page that is not shown is left alone, whatever its age */
    slots[0].visible = false; slots[1].visible = false;
    d = history_feed_decide(slots, HISTORY_FEED_MAX, &st, t0 + 10 * HISTORY_FEED_REFRESH_MS);
    CHECK_INT(d.next, -1);
}

static void test_ota_health(void)
{
    ota_health_t h = {.uptime_s = OTA_HEALTH_MIN_UPTIME_S, .boot_completed = true, .http_answers = true, .network_ok = true};
    CHECK(ota_health_ok(&h));
    CHECK(!ota_bootloader_has_rollback(1)); /* every bootloader from before the version bump */
    CHECK(ota_bootloader_has_rollback(OTA_BOOTLOADER_ROLLBACK_VERSION) && ota_bootloader_has_rollback(7));
    h.uptime_s = OTA_HEALTH_MIN_UPTIME_S - 1;
    CHECK(!ota_health_ok(&h)); /* a reset shortly after boot must still roll back */
    h.uptime_s = 600;
    h.http_answers = false;
    CHECK(!ota_health_ok(&h));
    h.http_answers = true;
    h.network_ok = false;
    CHECK(!ota_health_ok(&h)); /* configured Wi-Fi that never connects is not healthy */
    h.network_ok = true;
    h.boot_completed = false;
    CHECK(!ota_health_ok(&h));
    CHECK(!ota_health_ok(NULL));
}

static void test_matte_titles(void)
{
    char out[80];
    matte_title_uppercase(out, sizeof(out), "Küche Straße 21.5°C");
    CHECK(strcmp(out, "KÜCHE STRASSE 21.5°C") == 0);
    matte_title_uppercase(out, sizeof(out), "één ijssel 75 %");
    CHECK(strcmp(out, "ÉÉN IJSSEL 75 %") == 0);
    matte_title_uppercase(out, 5, "äpfel");
    CHECK(strcmp(out, "ÄPF") == 0);
    matte_title_uppercase(out, 2, "°");
    CHECK(strcmp(out, "") == 0);
}

/* The style ID contract of ui_style_registry.h: supported IDs round-trip, anything else (also a
 * removed style) falls back to Flat, and a new device starts with Glow. */
static void test_ui_style_registry(void)
{
    ui_style_kind_t kind;
    CHECK(strcmp(ui_style_name(UI_STYLE_DEFAULT), "glow") == 0);
    CHECK(strcmp(ui_style_name(UI_STYLE_FALLBACK), "flat") == 0);
    CHECK_INT((int)ui_style_count(), 4);
    for (size_t i = 0; ui_style_at(i, &kind); i++) {
        ui_style_kind_t again = UI_STYLE_FALLBACK;
        CHECK(ui_style_lookup(ui_style_name(kind), &again));
        CHECK_INT(again, kind);
    }
    CHECK(!ui_style_at(ui_style_count(), &kind));

    CHECK(!ui_style_lookup("removed_style", &kind));
    CHECK_INT(kind, UI_STYLE_FALLBACK);
    CHECK(strcmp(ui_style_name(ui_style_from_name("removed_style")), "flat") == 0);
    CHECK(!ui_style_lookup("", &kind));
    CHECK(!ui_style_lookup("Glow", &kind));
    CHECK(!ui_style_lookup(NULL, &kind));
    CHECK_INT(kind, UI_STYLE_FALLBACK);
    CHECK(ui_style_lookup("matte", NULL));
}

/* ---- entity discovery domains ---------------------------------------------------------------- */

static void test_discovery_domains(void)
{
    CHECK(ha_discovery_domain_supported("light"));
    CHECK(ha_discovery_domain_supported("select"));
    CHECK(ha_discovery_domain_supported("any"));
    CHECK(ha_discovery_domain_supported("room"));
    CHECK(ha_discovery_domain_supported("input_select"));
    CHECK(ha_discovery_domain_supported("input_datetime"));
    CHECK(ha_discovery_domain_supported("time"));
    CHECK(ha_discovery_domain_supported("date"));
    CHECK(ha_discovery_domain_supported("datetime"));
    CHECK(!ha_discovery_domain_supported(NULL));
    CHECK(strcmp(ha_discovery_domain_or_default("nope"), "light") == 0);
    CHECK(strcmp(ha_discovery_domain_or_default("cover"), "cover") == 0);

    CHECK(ha_discovery_entity_matches("cover.garage", "cover"));
    CHECK(!ha_discovery_entity_matches("cover_x.garage", "cover"));
    CHECK(ha_discovery_entity_matches("light.a", "nope")); /* unknown domain falls back to light */
    CHECK(ha_discovery_entity_matches("input_boolean.guest", "any"));
    CHECK(ha_discovery_entity_matches("image.map", "any"));
    CHECK(!ha_discovery_entity_matches("input_boolean.guest", "room"));
    CHECK(ha_discovery_entity_matches("vacuum.robot", "room"));
    CHECK(!ha_discovery_entity_matches("automation.x", "any"));
    CHECK(!ha_discovery_entity_matches("", "any"));
    CHECK(!ha_discovery_entity_matches(NULL, "any"));

    char jinja[384]; /* as ha_client.c */
    CHECK(ha_discovery_domains_jinja("any", jinja, sizeof(jinja)));
    CHECK(strcmp(jinja, "['sensor','binary_sensor','light','cover','fan','climate','media_player','lock','alarm_control_panel','switch',"
                        "'input_boolean','weather','vacuum','camera','image','event','scene','script','button','input_button',"
                        "'todo','select','input_select','input_datetime','time','date','datetime']") == 0);
    CHECK(ha_discovery_domains_jinja("room", jinja, sizeof(jinja)));
    CHECK(strcmp(jinja, "['sensor','binary_sensor','light','cover','fan','climate','media_player','lock','switch',"
                        "'vacuum']") == 0);
    CHECK(!ha_discovery_domains_jinja("light", jinja, sizeof(jinja)));
    CHECK(!ha_discovery_domains_jinja("room", jinja, 20)); /* too small, no partial list */
}

/* ---- Home Assistant auth guard ---------------------------------------------------------------- */

static void test_auth_state(void)
{
    ha_auth_state_reset();
    CHECK(!ha_auth_state_blocked());
    CHECK(strcmp(ha_auth_state_name(), "ok") == 0);
    CHECK(!ha_auth_state_report_http(200, false, true));
    CHECK(!ha_auth_state_report_http(500, false, true)); /* server errors are no failed logins */
    CHECK(!ha_auth_state_report_http(0, false, false));  /* no HTTP status (connect error) */
    CHECK(!ha_auth_state_blocked());

    /* Not admin: the template discovery stops, everything else keeps running. */
    CHECK(ha_auth_state_report_http(401, true, true));
    CHECK(!ha_auth_state_report_http(401, true, true)); /* logged once */
    CHECK(!ha_auth_state_blocked());
    CHECK(ha_auth_state_template_disabled());
    CHECK(strcmp(ha_auth_state_name(), "not_admin") == 0);

    /* A 401 on the template before the WebSocket authenticated is a rejected token. */
    ha_auth_state_reset();
    CHECK(ha_auth_state_report_http(401, true, false));
    CHECK(ha_auth_state_blocked());
    CHECK_INT(ha_auth_state_reason(), HA_AUTH_TOKEN_REJECTED);

    /* The first reason stays; later rejections change nothing. */
    CHECK(!ha_auth_state_report_http(403, false, true));
    CHECK(!ha_auth_state_report_ws_auth_invalid());
    CHECK_INT(ha_auth_state_reason(), HA_AUTH_TOKEN_REJECTED);

    ha_auth_state_reset();
    CHECK(ha_auth_state_report_http(403, false, false)); /* e.g. a WebSocket handshake after an IP ban */
    CHECK(strcmp(ha_auth_state_name(), "forbidden") == 0);
    CHECK(ha_auth_state_template_disabled());

    ha_auth_state_reset();
    CHECK(ha_auth_state_report_ws_auth_invalid());
    CHECK(strcmp(ha_auth_state_name(), "token_rejected") == 0);
    ha_auth_state_reset();

    /* An image URL with its own token (camera_proxy) or on a foreign host says nothing about the panel's token. */
    CHECK(!ha_auth_state_report_image_fetch(401, false));
    CHECK(!ha_auth_state_report_image_fetch(403, false));
    CHECK(!ha_auth_state_blocked());
    CHECK(!ha_auth_state_report_image_fetch(200, true));
    CHECK(!ha_auth_state_blocked());
    CHECK(ha_auth_state_report_image_fetch(401, true)); /* the panel's own token was sent and refused */
    CHECK_INT(ha_auth_state_reason(), HA_AUTH_TOKEN_REJECTED);
    ha_auth_state_reset();
    CHECK(ha_auth_state_report_image_fetch(403, true));
    CHECK_INT(ha_auth_state_reason(), HA_AUTH_FORBIDDEN);
    ha_auth_state_reset();
}

/* ---- settings that need a restart ------------------------------------------------------------- */

static void test_settings_restart(void)
{
    static runtime_settings_t before, after; /* large: keep them off the stack */
    memset(&before, 0, sizeof(before));
    snprintf(before.wifi_ssid, sizeof(before.wifi_ssid), "home");
    snprintf(before.ha_ws_url, sizeof(before.ha_ws_url), "ws://ha:8123/api/websocket");
    snprintf(before.ui_style, sizeof(before.ui_style), "flat");
    after = before;
    CHECK(!settings_need_restart(&before, &after));

    /* Applied at once by the settings API. */
    snprintf(after.ui_style, sizeof(after.ui_style), "glow");
    snprintf(after.ui_language, sizeof(after.ui_language), "de");
    snprintf(after.time_tz, sizeof(after.time_tz), "CET-1CEST");
    after.hardware_active_brightness_percent = 42;
    after.hardware_night_enabled = true;
    snprintf(after.topbar_left_slots[0], sizeof(after.topbar_left_slots[0]), "weather");
    snprintf(after.topbar_left_slots[APP_TOPBAR_LEFT_SLOTS - 1], sizeof(after.topbar_left_slots[0]), "stock"); /* the third item */
    after.ota_update_check = true;
    CHECK(!settings_need_restart(&before, &after));
    CHECK(APP_TOPBAR_LEFT_SLOTS == 3);

    /* Read at boot. */
    runtime_settings_t *changed = &after;
    const runtime_settings_t live = after;
    snprintf(changed->wifi_password, sizeof(changed->wifi_password), "secret");
    CHECK(settings_need_restart(&before, changed));
    *changed = live;
    snprintf(changed->ha_access_token, sizeof(changed->ha_access_token), "new");
    CHECK(settings_need_restart(&before, changed));
    *changed = live;
    changed->ha_rest_enabled = true;
    CHECK(settings_need_restart(&before, changed));
    *changed = live;
    changed->mqtt_broker_port = 8883;
    CHECK(settings_need_restart(&before, changed));
    *changed = live;
    snprintf(changed->device_room, sizeof(changed->device_room), "kitchen");
    CHECK(settings_need_restart(&before, changed));
    *changed = live;
    snprintf(changed->ntp_server, sizeof(changed->ntp_server), "ptbtime1.ptb.de");
    CHECK(settings_need_restart(&before, changed));
    CHECK(settings_need_restart(NULL, changed));
}

/* ---- select options ---------------------------------------------------------------------------- */

static void test_select_options(void)
{
    static ha_select_options_t opts;
    ha_select_options_parse("{\"options\":[\"off\",\"now\",\"minpv\",\"pv\"],\"friendly_name\":\"Mode\"}", &opts);
    CHECK(!opts.missing);
    CHECK_INT((int)opts.count, 4);
    CHECK_INT(ha_select_options_find(&opts, "minpv"), 2);
    CHECK_INT(ha_select_options_find(&opts, "fast"), -1);

    ha_select_options_parse("{\"friendly_name\":\"Mode\"}", &opts); /* dropped with oversized attributes */
    CHECK(opts.missing);
    CHECK_INT((int)opts.count, 0);
    ha_select_options_parse("not json", &opts);
    CHECK(opts.missing);
    ha_select_options_parse(NULL, &opts);
    CHECK(opts.missing);

    /* Too long options are left out rather than shortened; non-strings are skipped; at most 24. */
    char json[2048];
    int n = snprintf(json, sizeof(json), "{\"options\":[\"%0*d\",1,\"ok\"", HA_SELECT_OPTION_LEN, 0);
    for (int i = 0; i < 30; i++) {
        n += snprintf(json + n, sizeof(json) - (size_t)n, ",\"o%d\"", i);
    }
    snprintf(json + n, sizeof(json) - (size_t)n, "]}");
    ha_select_options_parse(json, &opts);
    CHECK_INT((int)opts.count, HA_SELECT_MAX_OPTIONS);
    CHECK(strcmp(opts.option[0], "ok") == 0);

    CHECK(strcmp(ha_select_service_domain("input_select.guest_mode"), "input_select") == 0);
    CHECK(strcmp(ha_select_service_domain("select.evcc_mode"), "select") == 0);
    char payload[128];
    CHECK(ha_select_option_payload("select.evcc_mode", "a \"b\"", payload, sizeof(payload)));
    CHECK(strcmp(payload, "{\"entity_id\":\"select.evcc_mode\",\"option\":\"a \\\"b\\\"\"}") == 0);
    CHECK(!ha_select_option_payload("select.evcc_mode", "pv", payload, 10));
}

/* ---- lock state ---------------------------------------------------------------------------------- */

static void test_lock_state(void)
{
    CHECK_INT(ha_lock_state_parse("locked"), HA_LOCK_LOCKED);
    CHECK_INT(ha_lock_state_parse("jammed"), HA_LOCK_JAMMED);
    CHECK_INT(ha_lock_state_parse("unavailable"), HA_LOCK_UNKNOWN);
    CHECK_INT(ha_lock_state_parse(NULL), HA_LOCK_UNKNOWN);
    CHECK(strcmp(ha_lock_tap_service(HA_LOCK_LOCKED), "unlock") == 0);
    CHECK(strcmp(ha_lock_tap_service(HA_LOCK_UNLOCKED), "lock") == 0);
    CHECK(strcmp(ha_lock_tap_service(HA_LOCK_OPEN), "lock") == 0);
    CHECK(strcmp(ha_lock_tap_service(HA_LOCK_JAMMED), "lock") == 0); /* retry securing it */
    CHECK(ha_lock_tap_service(HA_LOCK_UNLOCKING) == NULL);          /* moving: no second command */
    CHECK(ha_lock_tap_service(HA_LOCK_UNKNOWN) == NULL);
    CHECK(ha_lock_state_unsecured(HA_LOCK_OPEN));
    CHECK(ha_lock_state_unsecured(HA_LOCK_JAMMED));
    CHECK(!ha_lock_state_unsecured(HA_LOCK_LOCKED));
    CHECK(!ha_lock_state_unsecured(HA_LOCK_LOCKING));
    CHECK(ha_lock_supports_open("{\"supported_features\":1,\"friendly_name\":\"Door\"}"));
    CHECK(ha_lock_supports_open("{\"supported_features\":3}"));
    CHECK(!ha_lock_supports_open("{\"supported_features\":0}"));
    CHECK(!ha_lock_supports_open("{\"friendly_name\":\"Door\"}"));
    CHECK(!ha_lock_supports_open(NULL));
    CHECK(strcmp(ha_lock_state_key(HA_LOCK_OPENING), "lock.state.opening") == 0);
    CHECK(strcmp(ha_lock_state_key(HA_LOCK_UNKNOWN), "common.unknown") == 0);
}

static void test_history_reduce(void)
{
    ha_history_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    CHECK(snapshot != NULL);
    if (snapshot == NULL) return;
    ha_history_reducer_t reducer;
    int64_t start = 1780000000, end = start + 86400;
    ha_history_reduce_init(&reducer, snapshot, false, start, end);
    CHECK(ha_history_reduce_feed(&reducer, "[[]]", 4));
    CHECK(snapshot->valid_count == 0);
    /* Offsets and fractional seconds are accepted across arbitrary read boundaries. */
    const char *history = "[[{\"state\":\"off\",\"last_changed\":\"2026-05-27T00:00:00+00:00\"},"
                          "{\"state\":\"on\",\"last_changed\":\"2026-05-27T12:00:00.123Z\"}]]";
    ha_history_reduce_init(&reducer, snapshot, false, 1779840000, 1780100000);
    for (size_t i = 0; i < strlen(history); i += 7) {
        size_t n = strlen(history) - i;
        if (n > 7) n = 7;
        CHECK(ha_history_reduce_feed(&reducer, history + i, n));
    }
    CHECK(snapshot->event_count == 2);
    CHECK(strcmp(ha_history_event_at(snapshot, 0)->state, "off") == 0);
    CHECK(strcmp(ha_history_event_at(snapshot, 1)->state, "on") == 0);
    CHECK(ha_history_event_at(snapshot, 2) == NULL);
    const char *numbers = "[[{\"state\":\"1.5\",\"last_changed\":\"2026-05-27T00:00:00Z\"},"
                          "{\"state\":\"2.5\",\"last_changed\":\"2026-05-27T00:01:00Z\"},"
                          "{\"state\":\"unavailable\",\"last_changed\":\"2026-05-27T00:02:00Z\"}]]";
    ha_history_reduce_init(&reducer, snapshot, true, 1779840000, 1780100000);
    CHECK(ha_history_reduce_feed(&reducer, numbers, strlen(numbers)));
    CHECK(snapshot->valid_count == 2);
    size_t populated = 0;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) populated += snapshot->buckets[i].count;
    CHECK(populated == 2);

    /* A slow sensor: 10 at the start, 20 in bucket 9, unavailable in bucket 19, 30 in bucket 29. Empty
     * buckets keep the last value; the unavailable stretch stays a gap until the next number. */
    const char *slow = "[[{\"state\":\"10\",\"last_changed\":\"2026-05-28T20:26:40Z\"},"
                       "{\"state\":\"20\",\"last_changed\":\"2026-05-28T21:16:40Z\"},"
                       "{\"state\":\"unavailable\",\"last_changed\":\"2026-05-28T22:06:40Z\"},"
                       "{\"state\":\"30\",\"last_changed\":\"2026-05-28T22:56:40Z\"}]]";
    ha_history_reduce_init(&reducer, snapshot, true, start, end);
    CHECK(ha_history_reduce_feed(&reducer, slow, strlen(slow)));
    ha_history_reduce_finish(snapshot);
    CHECK(snapshot->valid_count == 3);
    CHECK(snapshot->buckets[0].count == 1 && snapshot->buckets[0].sum == 10.0f);
    CHECK(snapshot->buckets[5].count == 1 && snapshot->buckets[5].sum == 10.0f); /* carried */
    CHECK(snapshot->buckets[9].count == 1 && snapshot->buckets[9].sum == 20.0f && snapshot->buckets[10].sum == 20.0f);
    CHECK(snapshot->buckets[15].count == 1 && snapshot->buckets[15].sum == 20.0f);
    CHECK(snapshot->buckets[19].count == 0 && snapshot->buckets[20].count == 0 && snapshot->buckets[25].count == 0); /* a gap */
    CHECK(snapshot->buckets[29].count == 1 && snapshot->buckets[29].sum == 30.0f);
    CHECK(snapshot->buckets[100].count == 1 && snapshot->buckets[100].min == 30.0f);
    CHECK(snapshot->buckets[HA_HISTORY_BUCKETS - 1].sum == 30.0f);                /* to the end of the window */

    /* Nothing before the first value, nothing at all for a state history. */
    const char *late = "[[{\"state\":\"5\",\"last_changed\":\"2026-05-28T21:16:40Z\"}]]";
    ha_history_reduce_init(&reducer, snapshot, true, start, end);
    CHECK(ha_history_reduce_feed(&reducer, late, strlen(late)));
    ha_history_reduce_finish(snapshot);
    CHECK(snapshot->buckets[8].count == 0 && snapshot->buckets[9].count == 1 && snapshot->buckets[10].count == 1);
    ha_history_reduce_init(&reducer, snapshot, false, start, end);
    ha_history_reduce_finish(snapshot);
    CHECK(snapshot->buckets[10].count == 0);
    free(snapshot);
}

static void test_distance_units(void)
{
    double value = 66000.0;
    CHECK(distance_to_km("m", &value) && fabs(value - 66.0) < 1e-9);
    value = 100.0;
    CHECK(distance_to_km("mi", &value) && fabs(value - 160.9344) < 1e-6);
    value = 195.0;
    CHECK(distance_to_km("km", &value) && value == 195.0);
    value = 195.0;
    CHECK(distance_to_km("", &value) && distance_to_km(NULL, &value) && value == 195.0);
    value = 5.0;
    CHECK(!distance_to_km("kWh", &value) && value == 5.0 && !distance_to_km("m", NULL));

    char out[32];
    CHECK(distance_readout_km("distance", "m", "66000.0", out, sizeof(out)) && strcmp(out, "66 km") == 0);
    CHECK(distance_readout_km("distance", "m", "66450", out, sizeof(out)) && strcmp(out, "66.5 km") == 0);
    CHECK(distance_readout_km("distance", "m", "1000", out, sizeof(out)) && strcmp(out, "1 km") == 0);
    CHECK(distance_readout_km("distance", "m", "-2500", out, sizeof(out)) && strcmp(out, "-2.5 km") == 0);
    CHECK(!distance_readout_km("distance", "m", "999.9", out, sizeof(out)));       /* a real short distance stays */
    CHECK(!distance_readout_km("distance", "km", "66", out, sizeof(out)));
    CHECK(!distance_readout_km("temperature", "m", "66000", out, sizeof(out)));   /* only the distance class */
    CHECK(!distance_readout_km(NULL, "m", "66000", out, sizeof(out)));
    CHECK(!distance_readout_km("distance", "m", "unavailable", out, sizeof(out)));
    CHECK(!distance_readout_km("distance", "m", "66000", out, 0));
}

static void et_fill_slot(et_series_t *series, size_t slot, double import, double exported, double solar,
                         double charge, double discharge)
{
    for (int part = 0; part < ET_PARTS_PER_SLOT; part++) {
        const int64_t at = series->window_start + (int64_t)slot * ET_SLOT_SECONDS + part * ET_PART_SECONDS;
        et_add_part(series, ET_CH_GRID_IMPORT, at, import / 3.0);
        et_add_part(series, ET_CH_GRID_EXPORT, at, exported / 3.0);
        et_add_part(series, ET_CH_SOLAR, at, solar / 3.0);
        et_add_part(series, ET_CH_BATTERY_CHARGE, at, charge / 3.0);
        et_add_part(series, ET_CH_BATTERY_DISCHARGE, at, discharge / 3.0);
    }
}

static bool et_near(float a, float b) { return fabsf(a - b) < 1e-5f; }

static void test_energy_timeline_model(void)
{
    static et_series_t series;
    const bool solar_grid[ET_CH_COUNT] = {true, true, true, false, false};
    const bool with_battery[ET_CH_COUNT] = {true, true, true, true, true};
    const int64_t now = 1780000123;
    et_series_init(&series, now, solar_grid);
    const int64_t current_slot = now - now % ET_SLOT_SECONDS;
    CHECK(series.window_start == current_slot - 95 * ET_SLOT_SECONDS);
    CHECK(series.window_start % ET_SLOT_SECONDS == 0);

    /* Window edges, alignment, resets and repeats. */
    CHECK(et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start, 0.10));
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start, 0.10));            /* repeat */
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start - 300, 0.10));      /* before the window */
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, current_slot + ET_SLOT_SECONDS, 0.1));  /* after it */
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start + 301, 0.10));      /* off the grid */
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start + 300, -0.5));      /* a meter reset */
    CHECK(!et_add_part(&series, ET_CH_GRID_IMPORT, series.window_start + 300, NAN));
    CHECK(!et_add_part(&series, ET_CH_COUNT, series.window_start + 300, 0.1));

    /* A Home Assistant reply: milliseconds, a null change is a hole. */
    et_series_init(&series, now, solar_grid);
    char reply[512];
    snprintf(reply, sizeof(reply),
             "[{\"start\":%lld,\"change\":0.2},{\"start\":%lld,\"change\":null},{\"start\":%lld,\"change\":0.3},"
             "{\"start\":\"x\",\"change\":1}]",
             (long long)(current_slot * 1000), (long long)((current_slot + 300) * 1000),
             (long long)((current_slot + 600) * 1000));
    cJSON *points = cJSON_Parse(reply);
    CHECK(points != NULL && et_add_stats_json(&series, ET_CH_GRID_IMPORT, points) == 2);
    cJSON_Delete(points);
    et_slot_t slot;
    et_slot(&series, ET_SLOTS - 1, &slot);
    CHECK(slot.state == ET_SLOT_PARTIAL && et_near(slot.home_from_grid, 0.5f));
    et_slot(&series, 0, &slot);
    CHECK(slot.state == ET_SLOT_EMPTY && slot.home_from_grid == 0.0f);                   /* a gap, not a zero bar */
    CHECK(et_add_stats_json(&series, ET_CH_SOLAR, NULL) == 0);

    /* Solar 1.0, export 0.4, import 0.2: home 0.8, grid serves 0.2, solar 0.6, feed-in 0.4. */
    et_series_init(&series, now, solar_grid);
    et_fill_slot(&series, 10, 0.2, 0.4, 1.0, 0.0, 0.0);
    et_slot(&series, 10, &slot);
    CHECK(slot.state == ET_SLOT_COMPLETE && !slot.battery);
    CHECK(et_near(slot.home_kwh, 0.8f) && et_near(slot.home_from_solar, 0.6f) && et_near(slot.home_from_grid, 0.2f));
    CHECK(et_near(slot.export_from_solar, 0.4f) && et_near(slot.export_kwh, 0.4f) && et_near(slot.solar_kwh, 1.0f));
    et_totals_t totals;
    et_totals(&series, &totals);
    CHECK(totals.slots_with_data == 1 && totals.slots_partial == 0 && et_near(totals.home_kwh, 0.8f));
    CHECK(et_near(et_share(totals.home_from_solar, totals.home_kwh), 0.75f) && totals.estimated && !totals.battery);

    /* With a battery: solar 2.0, discharge 0.5, charge 0.8, export 0.6, no import.
     * home = 2.0 + 0.5 - 0.6 - 0.8 = 1.1, all from solar; the battery charges from solar; the rest of the
     * solar feeds in, and the discharge makes up the export. */
    et_series_init(&series, now, with_battery);
    et_fill_slot(&series, 20, 0.0, 0.6, 2.0, 0.8, 0.5);
    et_slot(&series, 20, &slot);
    CHECK(slot.battery && et_near(slot.home_kwh, 1.1f) && et_near(slot.home_from_solar, 1.1f));
    CHECK(et_near(slot.home_from_battery, 0.0f) && et_near(slot.home_from_grid, 0.0f));
    CHECK(et_near(slot.battery_from_solar, 0.8f) && et_near(slot.battery_from_grid, 0.0f));
    CHECK(et_near(slot.export_from_solar, 0.1f) && et_near(slot.export_from_battery, 0.5f));

    /* Evening: no sun, battery serves the home, import tops it up; grid also charges. */
    et_series_init(&series, now, with_battery);
    et_fill_slot(&series, 30, 0.5, 0.0, 0.0, 0.1, 0.6);
    et_slot(&series, 30, &slot);
    CHECK(et_near(slot.home_kwh, 1.0f) && et_near(slot.home_from_grid, 0.5f) && et_near(slot.home_from_battery, 0.5f));
    CHECK(et_near(slot.battery_from_grid, 0.0f) && et_near(slot.battery_from_solar, 0.0f)); /* the mismatch stays unassigned */
    et_series_init(&series, now, with_battery);
    et_fill_slot(&series, 30, 0.9, 0.0, 0.0, 0.5, 0.0);
    et_slot(&series, 30, &slot);
    CHECK(et_near(slot.home_kwh, 0.4f) && et_near(slot.home_from_grid, 0.4f) && et_near(slot.battery_from_grid, 0.5f));
    et_totals(&series, &totals);
    CHECK(totals.battery && totals.estimated && et_near(totals.charge_kwh, 0.5f) && et_near(totals.import_kwh, 0.9f));

    /* Only one battery meter: the battery is left out of the balance. */
    const bool half_battery[ET_CH_COUNT] = {true, false, false, true, false};
    et_series_init(&series, now, half_battery);
    et_fill_slot(&series, 5, 0.3, 0.0, 0.0, 9.0, 9.0);
    et_slot(&series, 5, &slot);
    CHECK(!slot.battery && et_near(slot.home_kwh, 0.3f) && slot.charge_kwh == 0.0f);

    /* Export larger than production (unsynchronised meters) never gives negative solar use. */
    et_series_init(&series, now, solar_grid);
    et_fill_slot(&series, 0, 0.0, 0.5, 0.3, 0.0, 0.0);
    et_slot(&series, 0, &slot);
    CHECK(slot.home_kwh == 0.0f && slot.home_from_solar == 0.0f && slot.export_from_solar <= 0.3f + 1e-6f);

    /* Grid only: everything is grid, and a solar meter that is not configured is ignored. */
    const bool grid_only[ET_CH_COUNT] = {true, false, false, false, false};
    et_series_init(&series, now, grid_only);
    et_fill_slot(&series, 0, 0.4, 0.0, 9.0, 0.0, 0.0);
    et_slot(&series, 0, &slot);
    CHECK(et_near(slot.home_from_grid, 0.4f) && slot.home_from_solar == 0.0f && slot.solar_kwh == 0.0f);
    et_totals(&series, &totals);
    CHECK(et_share(totals.home_from_solar, totals.home_kwh) == 0.0f && !totals.estimated);

    /* Grid import not configured: no slot has data. */
    const bool none[ET_CH_COUNT] = {false, true, true, false, false};
    et_series_init(&series, now, none);
    et_fill_slot(&series, 0, 0.4, 0.0, 0.0, 0.0, 0.0);
    et_slot(&series, 0, &slot);
    CHECK(slot.state == ET_SLOT_EMPTY);
    CHECK(et_share(1.0f, 0.0f) == 0.0f && et_share(2.0f, 1.0f) == 1.0f && et_share(-1.0f, 2.0f) == 0.0f);

    /* Forecast: 30-minute periods of average kW become 0.25 h slots. */
    et_series_init(&series, now, solar_grid);
    et_forecast_t forecast;
    char attrs[256];
    snprintf(attrs, sizeof(attrs), "{\"fc_t0\":%lld,\"fc_step\":1800,\"fc_kw\":[0,4.0,8.0,null,2.0]}",
             (long long)(series.window_start + 60 * ET_SLOT_SECONDS));
    CHECK(et_forecast_from_attrs(attrs, series.window_start, &forecast) && forecast.valid);
    CHECK(!forecast.has[59] && forecast.has[60] && et_near(forecast.kwh[60], 0.0f));
    CHECK(forecast.has[62] && et_near(forecast.kwh[62], 1.0f) && et_near(forecast.kwh[63], 1.0f)); /* 4 kW for 15 min */
    CHECK(et_near(forecast.kwh[64], 2.0f) && !forecast.has[66] && !forecast.has[67]);            /* the null period */
    CHECK(forecast.has[68] && !forecast.has[70]);                                                 /* past the series */
    CHECK(!et_forecast_from_attrs("{\"fc_kw\":[1]}", series.window_start, &forecast) && !forecast.valid);
    CHECK(!et_forecast_from_attrs("not json", series.window_start, &forecast) && !et_forecast_from_attrs(NULL, 0, &forecast));
}

static void test_room_extra(void)
{
    room_extra_mode_t mode;
    CHECK(room_extra_mode_parse(NULL, &mode) && mode == ROOM_EXTRA_DEFAULT);
    CHECK(room_extra_mode_parse("", &mode) && mode == ROOM_EXTRA_DEFAULT);
    CHECK(room_extra_mode_parse("default", &mode) && mode == ROOM_EXTRA_DEFAULT);
    CHECK(room_extra_mode_parse("humidity", &mode) && mode == ROOM_EXTRA_HUMIDITY);
    CHECK(room_extra_mode_parse("cover", &mode) && mode == ROOM_EXTRA_COVER);
    CHECK(room_extra_mode_parse("media_player", &mode) && mode == ROOM_EXTRA_MEDIA_PLAYER);
    CHECK(!room_extra_mode_parse("lights", &mode) && mode == ROOM_EXTRA_DEFAULT && !room_extra_mode_parse("Cover", NULL));
    CHECK(strcmp(room_extra_mode_name(ROOM_EXTRA_MEDIA_PLAYER), "media_player") == 0 && strcmp(room_extra_mode_name(ROOM_EXTRA_DEFAULT), "default") == 0);
    CHECK(room_extra_entity_fits(ROOM_EXTRA_HUMIDITY, "sensor.bad_humidity") && !room_extra_entity_fits(ROOM_EXTRA_HUMIDITY, "climate.x"));
    CHECK(room_extra_entity_fits(ROOM_EXTRA_COVER, "cover.blind") && !room_extra_entity_fits(ROOM_EXTRA_COVER, "covers.blind"));
    CHECK(room_extra_entity_fits(ROOM_EXTRA_MEDIA_PLAYER, "media_player.tv") && !room_extra_entity_fits(ROOM_EXTRA_MEDIA_PLAYER, "media_player."));
    CHECK(room_extra_entity_fits(ROOM_EXTRA_DEFAULT, "anything") && room_extra_entity_fits(ROOM_EXTRA_DEFAULT, NULL));
    CHECK(!room_extra_entity_fits(ROOM_EXTRA_COVER, NULL));

    /* Cover: the next action follows the state, Stop only where supported, unknown states do nothing. */
    CHECK(room_extra_cover_action("closed", 0, true) == ROOM_ACTION_COVER_OPEN);
    CHECK(room_extra_cover_action("open", 0, true) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_extra_cover_action("opening", ROOM_COVER_FEAT_STOP, true) == ROOM_ACTION_COVER_STOP);
    CHECK(room_extra_cover_action("closing", 3, true) == ROOM_ACTION_NONE);            /* no STOP bit: disabled */
    CHECK(room_extra_cover_action("closing", 0, false) == ROOM_ACTION_COVER_STOP);     /* features unknown */
    CHECK(room_extra_cover_action("unavailable", 15, true) == ROOM_ACTION_NONE);
    /* two directions */
    CHECK(room_extra_cover_up("open", 70, 15, true) == ROOM_ACTION_COVER_OPEN && room_extra_cover_down("open", 70, 15, true) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_extra_cover_up("open", 100, 15, true) == ROOM_ACTION_NONE && room_extra_cover_down("open", 100, 15, true) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_extra_cover_up("open", -1, 15, true) == ROOM_ACTION_NONE); /* open without a position: already fully open */
    CHECK(room_extra_cover_up("closed", 0, 15, true) == ROOM_ACTION_COVER_OPEN && room_extra_cover_down("closed", 0, 15, true) == ROOM_ACTION_NONE);
    CHECK(room_extra_cover_up("opening", 30, 15, true) == ROOM_ACTION_COVER_STOP && room_extra_cover_down("opening", 30, 15, true) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_extra_cover_down("closing", 30, 15, true) == ROOM_ACTION_COVER_STOP && room_extra_cover_up("closing", 30, 15, true) == ROOM_ACTION_COVER_OPEN);
    CHECK(room_extra_cover_down("closing", 30, 3, true) == ROOM_ACTION_NONE);
    CHECK(room_extra_cover_up("unavailable", 50, 15, true) == ROOM_ACTION_NONE && room_extra_cover_down(NULL, 50, 15, true) == ROOM_ACTION_NONE);
    /* control row groups */
    CHECK(!room_group_is_on(0, 0) && !room_group_is_on(3, 0) && room_group_is_on(3, 1));
    CHECK(room_group_toggle_service(3, 1) != NULL && strcmp(room_group_toggle_service(3, 1), "turn_off") == 0);
    CHECK(strcmp(room_group_toggle_service(2, 0), "turn_on") == 0 && room_group_toggle_service(0, 0) == NULL);
    CHECK(room_cover_group_merge(ROOM_ACTION_NONE, ROOM_ACTION_COVER_OPEN) == ROOM_ACTION_COVER_OPEN);
    CHECK(room_cover_group_merge(ROOM_ACTION_COVER_STOP, ROOM_ACTION_COVER_CLOSE) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_cover_group_merge(ROOM_ACTION_COVER_CLOSE, ROOM_ACTION_COVER_STOP) == ROOM_ACTION_COVER_CLOSE);
    CHECK(room_cover_group_merge(ROOM_ACTION_COVER_STOP, ROOM_ACTION_NONE) == ROOM_ACTION_COVER_STOP);
    CHECK(room_cover_group_merge(ROOM_ACTION_NONE, ROOM_ACTION_NONE) == ROOM_ACTION_NONE);
    CHECK(room_extra_cover_action("unknown", 15, true) == ROOM_ACTION_NONE && room_extra_cover_action(NULL, 15, true) == ROOM_ACTION_NONE);

    /* Media player. */
    CHECK(room_extra_media_action("playing", ROOM_MEDIA_FEAT_PAUSE, true) == ROOM_ACTION_MEDIA_PAUSE);
    CHECK(room_extra_media_action("playing", ROOM_MEDIA_FEAT_PLAY, true) == ROOM_ACTION_NONE);
    CHECK(room_extra_media_action("paused", ROOM_MEDIA_FEAT_PLAY, true) == ROOM_ACTION_MEDIA_PLAY);
    CHECK(room_extra_media_action("idle", ROOM_MEDIA_FEAT_PLAY | ROOM_MEDIA_FEAT_PAUSE, true) == ROOM_ACTION_MEDIA_PLAY);
    CHECK(room_extra_media_action("idle", ROOM_MEDIA_FEAT_PAUSE, true) == ROOM_ACTION_NONE);
    CHECK(room_extra_media_action("off", 0xFFFF, true) == ROOM_ACTION_NONE && room_extra_media_action("unavailable", 0xFFFF, true) == ROOM_ACTION_NONE);
    CHECK(room_extra_media_action("paused", 0, false) == ROOM_ACTION_MEDIA_PLAY && room_extra_media_action("playing", 0, false) == ROOM_ACTION_MEDIA_PAUSE);

    CHECK(strcmp(room_action_domain(ROOM_ACTION_COVER_STOP), "cover") == 0 && strcmp(room_action_service(ROOM_ACTION_COVER_STOP), "stop_cover") == 0);
    CHECK(strcmp(room_action_service(ROOM_ACTION_COVER_OPEN), "open_cover") == 0 && strcmp(room_action_service(ROOM_ACTION_COVER_CLOSE), "close_cover") == 0);
    CHECK(strcmp(room_action_domain(ROOM_ACTION_MEDIA_PLAY), "media_player") == 0 && strcmp(room_action_service(ROOM_ACTION_MEDIA_PAUSE), "media_pause") == 0);
    CHECK(room_action_domain(ROOM_ACTION_NONE) == NULL && room_action_service(ROOM_ACTION_NONE) == NULL);
}

static void test_datetime_value(void)
{
    ha_datetime_value_t value;
    char formatted[48], payload[160];
    const char *domain = NULL, *service = NULL;
    CHECK(ha_datetime_parse("input_datetime.morning", "06:00:00", "{\"has_date\":false,\"has_time\":true}", &value));
    CHECK(!value.has_date && value.has_time);
    CHECK(ha_datetime_format(&value, "de", formatted, sizeof(formatted)) && strcmp(formatted, "06:00") == 0);
    CHECK(ha_datetime_service_payload("input_datetime.morning", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strcmp(domain, "input_datetime") == 0 && strcmp(service, "set_datetime") == 0);
    CHECK(strcmp(payload, "{\"entity_id\":\"input_datetime.morning\",\"time\":\"06:00:00\"}") == 0);
    CHECK(ha_datetime_parse("time.robot", "22:15:07", NULL, &value));
    CHECK(ha_datetime_format(&value, "en", formatted, sizeof(formatted)) && strcmp(formatted, "22:15:07") == 0);
    CHECK(ha_datetime_service_payload("time.robot", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strcmp(domain, "time") == 0 && strcmp(service, "set_value") == 0);
    CHECK(ha_datetime_parse("date.reminder", "2026-09-27", NULL, &value));
    CHECK(ha_datetime_format(&value, "de", formatted, sizeof(formatted)) && strcmp(formatted, "27.09.2026") == 0);
    CHECK(ha_datetime_format(&value, "en", formatted, sizeof(formatted)) && strcmp(formatted, "09/27/2026") == 0);
    CHECK(ha_datetime_service_payload("date.reminder", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strcmp(domain, "date") == 0 && strstr(payload, "\"date\":\"2026-09-27\"") != NULL);
    CHECK(ha_datetime_parse("input_datetime.meeting", "2026-09-27 07:30:00", "{\"has_date\":true,\"has_time\":true}", &value));
    CHECK(ha_datetime_format(&value, "de", formatted, sizeof(formatted)) && strcmp(formatted, "27.09.2026 07:30") == 0);
    CHECK(ha_datetime_service_payload("input_datetime.meeting", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strstr(payload, "\"datetime\":\"2026-09-27 07:30:00\"") != NULL);
    CHECK(!ha_datetime_parse("input_datetime.bad", "2026-02-29", NULL, &value));
    CHECK(!ha_datetime_parse("input_datetime.bad", "06:00:00", "{\"has_date\":true}", &value));
    CHECK(!ha_datetime_parse("time.bad", "24:00:00", NULL, &value));
    CHECK(!ha_datetime_parse("datetime.bad", "2026-09-27T07:30:00", NULL, &value));

    const char *previous_tz = getenv("TZ");
    char *saved_tz = previous_tz ? strdup(previous_tz) : NULL;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    CHECK(ha_datetime_parse("datetime.meeting", "2026-09-27T05:00:00+00:00", NULL, &value));
    CHECK(ha_datetime_format(&value, "de", formatted, sizeof(formatted)) && strcmp(formatted, "27.09.2026 07:00") == 0);
    CHECK(ha_datetime_service_payload("datetime.meeting", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strstr(payload, "\"datetime\":\"2026-09-27T05:00:00+00:00\"") != NULL);
    value.year = 2026; value.month = 3; value.day = 29; value.hour = 2; value.minute = 30;
    CHECK(!ha_datetime_service_payload("datetime.meeting", &value, &domain, &service, payload, sizeof(payload)));
    value.month = 10; value.day = 25; value.hour = 2;
    CHECK(ha_datetime_service_payload("datetime.meeting", &value, &domain, &service, payload, sizeof(payload)));
    CHECK(strstr(payload, "\"datetime\":\"2026-10-25T00:30:00+00:00\"") != NULL);
    if (saved_tz) { setenv("TZ", saved_tz, 1); free(saved_tz); }
    else unsetenv("TZ");
    tzset();
}

static bool read_whole_file(const char *path, char *out, size_t out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    size_t n = fread(out, 1U, out_len - 1U, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void test_atomic_file(void)
{
    char dir[] = "/tmp/betta_atomic_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[128];
    char tmp[136];
    char text[64];
    snprintf(path, sizeof(path), "%s/settings.json", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    CHECK_INT(atomic_file_write(path, "{\"a\":1}", 7), ESP_OK);
    CHECK(read_whole_file(path, text, sizeof(text)) && strcmp(text, "{\"a\":1}") == 0);
    CHECK(access(tmp, F_OK) != 0); /* the temporary file is gone after the rename */

    /* replacing a longer file by a shorter one leaves no tail of the old content */
    CHECK_INT(atomic_file_write(path, "{\"a\":1,\"b\":2}", 13), ESP_OK);
    CHECK_INT(atomic_file_write(path, "{}", 2), ESP_OK);
    CHECK(read_whole_file(path, text, sizeof(text)) && strcmp(text, "{}") == 0);

    /* an empty payload gives an empty file */
    CHECK_INT(atomic_file_write(path, NULL, 0), ESP_OK);
    CHECK(read_whole_file(path, text, sizeof(text)) && text[0] == '\0');

    /* a stale temporary file from an interrupted write is overwritten */
    FILE *stale = fopen(tmp, "wb");
    CHECK(stale != NULL);
    if (stale) { fputs("garbage that is longer than the next payload", stale); fclose(stale); }
    CHECK_INT(atomic_file_write(path, "ok", 2), ESP_OK);
    CHECK(read_whole_file(path, text, sizeof(text)) && strcmp(text, "ok") == 0);
    CHECK(access(tmp, F_OK) != 0);

    /* a failed write keeps the old file: the directory of the target does not exist */
    char bad[160];
    snprintf(bad, sizeof(bad), "%s/missing/x.json", dir);
    CHECK_INT(atomic_file_write(bad, "x", 1), ESP_FAIL);
    CHECK(read_whole_file(path, text, sizeof(text)) && strcmp(text, "ok") == 0);

    CHECK_INT(atomic_file_write(NULL, "x", 1), ESP_ERR_INVALID_ARG);
    CHECK_INT(atomic_file_write("", "x", 1), ESP_ERR_INVALID_ARG);
    CHECK_INT(atomic_file_write(path, NULL, 3), ESP_ERR_INVALID_ARG);

    remove(path);
    rmdir(dir);
}

static void test_settings_validate(void)
{
    CHECK(settings_host_valid("") && settings_host_valid("pool.ntp.org") && settings_host_valid("192.168.1.10") &&
          settings_host_valid("fe80::1") && settings_host_valid("mqtt_host-1.local"));
    CHECK(!settings_host_valid("a b") && !settings_host_valid("host/path") && !settings_host_valid("h\n") &&
          !settings_host_valid("[::1]") && !settings_host_valid("h\xc3\xa4") && !settings_host_valid(NULL));

    CHECK(settings_timezone_valid("") && settings_timezone_valid("CET-1CEST,M3.5.0,M10.5.0/3") &&
          settings_timezone_valid("<+03>-3"));
    CHECK(!settings_timezone_valid("CET\n") && !settings_timezone_valid("Z\xc3\xbcrich") && !settings_timezone_valid(NULL));

    CHECK(settings_topic_prefix_valid("") && settings_topic_prefix_valid("betta_ha_panel") &&
          settings_topic_prefix_valid("home/panels/kitchen"));
    CHECK(!settings_topic_prefix_valid("a+b") && !settings_topic_prefix_valid("a/#") && !settings_topic_prefix_valid("/a") &&
          !settings_topic_prefix_valid("a/") && !settings_topic_prefix_valid("a//b") && !settings_topic_prefix_valid("a b") &&
          !settings_topic_prefix_valid(NULL));

    CHECK(settings_text_valid("") && settings_text_valid("Kitchen Panel") && settings_text_valid("K\xc3\xbc" "che"));
    CHECK(!settings_text_valid("a\tb") && !settings_text_valid("a\x7f") && !settings_text_valid(NULL));
}

static uint16_t rgb565_of(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static void test_cover_scale(void)
{
    uint32_t sum[16 * 4];
    uint16_t cnt[16];
    cover_box_t box;
    uint16_t dst[16];
    uint8_t alpha[16];

    /* shrinking only, sane sizes */
    CHECK(!cover_box_init(&box, 4, 4, 8, 8, sum, cnt));
    CHECK(!cover_box_init(&box, 0, 4, 2, 2, sum, cnt));
    CHECK(!cover_box_init(&box, 4, 4, 0, 2, sum, cnt));
    CHECK(!cover_box_init(&box, 4000, 4000, 1, 1, sum, cnt)); /* 16 million samples per pixel */

    /* 4x4 -> 2x2: each output pixel is the mean of a 2x2 block */
    CHECK(cover_box_init(&box, 4, 4, 2, 2, sum, cnt));
    uint8_t row_a[4 * 3] = {0, 0, 0, 200, 200, 200, 40, 40, 40, 240, 240, 240};
    uint8_t row_b[4 * 3] = {0, 0, 0, 200, 200, 200, 40, 40, 40, 240, 240, 240};
    CHECK(!cover_box_add_rgb24_row(&box, row_a));
    CHECK(cover_box_add_rgb24_row(&box, row_b));
    CHECK_INT((int)cover_box_ready_row(&box), 0);
    cover_box_take_rgb565_row(&box, dst, alpha);
    CHECK_INT(dst[0], rgb565_of(100, 100, 100));
    CHECK_INT(dst[1], rgb565_of(140, 140, 140));
    CHECK_INT(alpha[0], 255);
    CHECK(!cover_box_add_rgb24_row(&box, row_a));
    CHECK(cover_box_add_rgb24_row(&box, row_b));
    CHECK_INT((int)cover_box_ready_row(&box), 1);
    cover_box_take_rgb565_row(&box, dst, NULL);
    CHECK(!cover_box_add_rgb24_row(&box, row_a)); /* rows past the end are ignored */

    /* a one-pixel line survives averaging with its weight (nearest would drop or double it) */
    CHECK(cover_box_init(&box, 8, 1, 2, 1, sum, cnt));
    uint8_t line[8 * 3] = {0};
    line[2 * 3] = line[2 * 3 + 1] = line[2 * 3 + 2] = 255; /* one white pixel in the first half */
    CHECK(cover_box_add_rgb24_row(&box, line));
    cover_box_take_rgb565_row(&box, dst, NULL);
    CHECK_INT(dst[0], rgb565_of(64, 64, 64)); /* 255 / 4 */
    CHECK_INT(dst[1], 0);

    /* a ratio that is not an integer: 5 -> 2 columns splits 3 + 2, every source pixel is used once */
    CHECK(cover_box_init(&box, 5, 5, 2, 2, sum, cnt));
    CHECK_INT(cnt[0] + cnt[1], 5);
    CHECK(cnt[0] >= 2 && cnt[1] >= 2);
    uint8_t flat[5 * 3];
    memset(flat, 90, sizeof(flat));
    int completed = 0;
    for (int y = 0; y < 5; y++) {
        if (cover_box_add_rgb24_row(&box, flat)) {
            uint16_t px[2];
            cover_box_take_rgb565_row(&box, px, NULL);
            CHECK_INT(px[0], rgb565_of(90, 90, 90));
            CHECK_INT(px[1], rgb565_of(90, 90, 90));
            CHECK_INT((int)cover_box_ready_row(&box), completed);
            completed++;
        }
    }
    CHECK_INT(completed, 2);

    /* alpha: transparent pixels do not tint the colour, partial coverage lowers alpha */
    CHECK(cover_box_init(&box, 2, 1, 1, 1, sum, cnt));
    uint8_t rgba[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 0}; /* opaque red, transparent green */
    CHECK(cover_box_add_rgba_row(&box, rgba));
    cover_box_take_rgb565_row(&box, dst, alpha);
    CHECK_INT(dst[0], rgb565_of(255, 0, 0));
    CHECK_INT(alpha[0], 128);

    /* RGB565 input: expansion and repacking are lossless for a uniform colour */
    CHECK(cover_box_init(&box, 4, 2, 2, 1, sum, cnt));
    uint16_t px565[4];
    for (int i = 0; i < 4; i++) px565[i] = rgb565_of(200, 100, 50);
    CHECK(!cover_box_add_rgb565_row(&box, px565));
    CHECK(cover_box_add_rgb565_row(&box, px565));
    cover_box_take_rgb565_row(&box, dst, alpha);
    CHECK_INT(dst[0], px565[0]);
    CHECK_INT(dst[1], px565[0]);

    /* 1:1 is the identity */
    CHECK(cover_box_init(&box, 3, 1, 3, 1, sum, cnt));
    uint8_t ident[3 * 3] = {10, 20, 30, 100, 110, 120, 200, 210, 220};
    CHECK(cover_box_add_rgb24_row(&box, ident));
    cover_box_take_rgb565_row(&box, dst, NULL);
    CHECK_INT(dst[1], rgb565_of(100, 110, 120));
    CHECK_INT(dst[2], rgb565_of(200, 210, 220));
}

static void test_cover_guard(void)
{
    const char *base = "http://192.0.2.19:8123";
    CHECK(cover_url_on_origin("http://192.0.2.19:8123", base));
    CHECK(cover_url_on_origin("http://192.0.2.19:8123/api/camera_proxy/camera.x", base));
    CHECK(cover_url_on_origin("http://192.0.2.19:8123?a=1", base));
    CHECK(!cover_url_on_origin("http://192.0.2.19:81234/x", base));          /* other port */
    CHECK(!cover_url_on_origin("http://192.0.2.19:8123.evil.example/x", base)); /* other host */
    CHECK(!cover_url_on_origin("http://192.0.2.19:8123@evil.example/x", base)); /* userinfo trick */
    CHECK(!cover_url_on_origin("https://192.0.2.19:8123/x", base));          /* other scheme */
    CHECK(!cover_url_on_origin("https://i.scdn.co/image/abc", base));
    CHECK(!cover_url_on_origin("http://192.0.2.19:8123/x", ""));
    CHECK(!cover_url_on_origin(NULL, base) && !cover_url_on_origin("x", NULL));

    /* PNG chunk bounds: 4 length + 4 type + data + 4 CRC, no wrap-around with a 32-bit size_t */
    CHECK(cover_png_chunk_fits(8, 33, 13));          /* IHDR: 8 + 12 + 13 = 33 */
    CHECK(!cover_png_chunk_fits(8, 32, 13));
    CHECK(cover_png_chunk_fits(8, 20, 0));
    CHECK(!cover_png_chunk_fits(8, 19, 0));
    CHECK(!cover_png_chunk_fits(30, 20, 0));         /* pos beyond len */
    CHECK(!cover_png_chunk_fits(8, 1000, 0xFFFFFFF5u)); /* 0xFFFFFFF5 + 12 wraps to 1 in 32 bits */
    CHECK(!cover_png_chunk_fits(8, 1000, 0xFFFFFFFFu));
    CHECK(cover_png_chunk_fits(0, 100, 88));

    CHECK(cover_image_size_ok(640, 640, 3000U * 3000U));
    CHECK(!cover_image_size_ok(0, 640, 100) && !cover_image_size_ok(640, 0, 100));
    CHECK(!cover_image_size_ok(65536, 65536, 3000U * 3000U)); /* 2^32 pixels must not wrap to 0 */
    CHECK(cover_image_size_ok(3000, 3000, 3000U * 3000U));
    CHECK(!cover_image_size_ok(3001, 3000, 3000U * 3000U));
}

static void test_json_number_u32(void)
{
    cJSON *obj = cJSON_Parse("{\"a\":7,\"b\":4294967295,\"c\":1e20,\"d\":-5,\"e\":0,\"f\":2.9,\"g\":\"9\",\"h\":4294967294}");
    CHECK(obj != NULL);
    if (obj != NULL) {
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "a")), 7);
        CHECK(json_util_number_u32(cJSON_GetObjectItem(obj, "b")) == UINT32_MAX);
        CHECK(json_util_number_u32(cJSON_GetObjectItem(obj, "c")) == UINT32_MAX);
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "d")), 0);
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "e")), 0);
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "f")), 2);
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "g")), 0); /* a string is not a number */
        CHECK(json_util_number_u32(cJSON_GetObjectItem(obj, "h")) == 4294967294U);
        CHECK_INT((int)json_util_number_u32(cJSON_GetObjectItem(obj, "missing")), 0);
        cJSON_Delete(obj);
    }
    cJSON *nan = cJSON_CreateNumber(0.0 / 0.0);
    CHECK_INT((int)json_util_number_u32(nan), 0);
    cJSON_Delete(nan);
    CHECK_INT((int)json_util_number_u32(NULL), 0);
}

static void test_display_policy(void)
{
    display_policy_t p;
    display_policy_init(&p, 80, 30, 300000);
    const display_policy_env_t idle = {false, false, 0};
    const display_policy_env_t night = {false, true, 5};
    const display_policy_env_t saver = {true, false, 0};

    /* no room-light limit: the levels as configured */
    CHECK_INT(display_policy_wake_level(&p), 80);
    CHECK_INT(display_policy_dim_level(&p, &idle), 30);
    CHECK_INT(display_policy_dim_level(&p, &night), 5); /* night mode replaces the dim level, even 0 = off */
    const display_policy_env_t night_off = {false, true, 0};
    CHECK_INT(display_policy_dim_level(&p, &night_off), 0);

    /* room light: active and dimmed scale, night does not; the floor of 5 % holds */
    display_policy_set_ambient(&p, 50);
    CHECK_INT(display_policy_wake_level(&p), 40);
    CHECK_INT(display_policy_dim_level(&p, &idle), 15);
    CHECK_INT(display_policy_dim_level(&p, &night), 5);
    display_policy_set_ambient(&p, 10);
    CHECK_INT(display_policy_wake_level(&p), 8);
    CHECK_INT(display_policy_dim_level(&p, &idle), 5);   /* 3 would fall under the floor */
    display_policy_set_ambient(&p, 250);                 /* clamped to 100 */
    CHECK_INT(p.ambient_limit_percent, 100);
    display_policy_set_ambient(&p, -3);
    CHECK_INT(p.ambient_limit_percent, 0);
    display_policy_set_ambient(&p, 100);

    /* a change of the room light: awake or dimmed level, hands off at night and under the screen protection */
    CHECK_INT(display_policy_ambient_level(&p, &idle), 80);
    p.dimmed = true;
    CHECK_INT(display_policy_ambient_level(&p, &idle), 30);
    CHECK_INT(display_policy_ambient_level(&p, &night), -1);
    CHECK_INT(display_policy_ambient_level(&p, &saver), -1);
    p.dimmed = false;

    /* waiting to be woken: dimmed, protected, or sitting at the night level */
    CHECK(!display_policy_is_dimmed(&p, &idle, 80));
    CHECK(display_policy_is_dimmed(&p, &saver, 80));
    CHECK(display_policy_is_dimmed(&p, &night, 5));
    CHECK(display_policy_is_dimmed(&p, &night, 0));
    CHECK(!display_policy_is_dimmed(&p, &night, 40)); /* night mode on, but touched awake */
    p.dimmed = true;
    CHECK(display_policy_is_dimmed(&p, &idle, 30));

    /* configuration from the settings: clamped levels and timeout */
    display_policy_configure(&p, 150, 0, 1);
    CHECK_INT(p.active_percent, 100);
    CHECK_INT(p.dim_percent, 1);
    CHECK_INT((int)p.dim_timeout_ms, 5000);
    display_policy_configure(&p, 60, 20, 99999);
    CHECK_INT(p.active_percent, 60);
    CHECK_INT(p.dim_percent, 20);
    CHECK_INT((int)p.dim_timeout_ms, 3600000);
    display_policy_configure(&p, 60, 20, 120);
    CHECK_INT((int)p.dim_timeout_ms, 120000);
    CHECK_INT(display_policy_clamp_percent(-1), 0);
    CHECK_INT(display_policy_clamp_percent(101), 100);
    CHECK_INT(display_policy_clamp_percent(42), 42);
}

int main(void)
{
    test_display_policy();
    test_json_number_u32();
    test_cover_guard();
    test_cover_scale();
    test_settings_validate();
    test_atomic_file();
    test_history_reduce();
    test_distance_units();
    test_energy_timeline_model();
    test_room_extra();
    test_datetime_value();
    CHECK(test_timestamp_format());
    CHECK(test_sensor_value());
    CHECK(test_quote());
    test_lock_state();
    test_select_options();
    test_settings_restart();
    test_auth_state();
    test_discovery_domains();
    test_ui_style_registry();
    test_matte_titles();
    test_brightness();
    test_layout();
    test_entity_ids();
    test_alerts();
    test_ha_attrs_fallback();
    test_iso_time_and_forecast_series();
    test_ha_attr_serializers();
    test_ha_layout_entities();
    test_ha_room_registry();
    test_ha_display_precision();
    test_setup_passphrase();
    test_url_decode();
    test_weather_value_parser();
    test_chime();
    test_wifi_signal_level();
    test_update_check();
    test_ui_watchdog();
    test_ota_chip_rev();
    test_history_feed_policy();
    test_ota_health();
    test_auto_update();
    test_auto_update_state();
    test_sha256_soft();
    test_chart_scale();
    test_reboot_log();
    test_net_watchdog();
    test_nav_layout();
    test_boot_guard();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
