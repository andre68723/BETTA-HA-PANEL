/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Sensor tile value formatting: timestamps (ha_timestamp_format) and the device-class rules of ha_sensor_value. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_sensor_value.h"
#include "ha/ha_timestamp_format.h"

static int s_failures = 0;

#define SV_CHECK(cond) do { if (!(cond)) { s_failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

bool test_sensor_value(void)
{
    const int failures = s_failures;
    ha_sv_unit_t unit;
    SV_CHECK(ha_sv_unit_from_string(NULL, &unit) && unit == HA_SV_UNIT_DEFAULT);
    SV_CHECK(ha_sv_unit_from_string("auto", &unit) && unit == HA_SV_UNIT_AUTO);
    SV_CHECK(!ha_sv_unit_from_string("kW", &unit));
    SV_CHECK(ha_sv_format_valid("clock") && ha_sv_format_valid("relative") && !ha_sv_format_valid("") && !ha_sv_format_valid("iso"));
    SV_CHECK(ha_sv_duration_style(NULL) == HA_SV_DURATION_TEXT && ha_sv_duration_style("raw") == HA_SV_DURATION_RAW);
    SV_CHECK(ha_sv_language_decimal_comma("de") && ha_sv_language_decimal_comma("fr") && !ha_sv_language_decimal_comma("en"));
    SV_CHECK(!ha_sv_language_decimal_comma("dex") && !ha_sv_language_decimal_comma(NULL));

    char text[48];
    SV_CHECK(ha_sv_number_text("21.5", "21.46", "\u00B0C", "temperature", HA_SV_UNIT_DEFAULT, "de", text, sizeof(text)) &&
          strcmp(text, "21,5 \u00B0C") == 0);
    SV_CHECK(ha_sv_number_text("21.5", "21.46", "\u00B0C", "temperature", HA_SV_UNIT_DEFAULT, "en", text, sizeof(text)) &&
          strcmp(text, "21.5 \u00B0C") == 0);
    SV_CHECK(ha_sv_number_text("21.5", "21.5", "\u00B0C", "temperature", HA_SV_UNIT_DEGREE, "de", text, sizeof(text)) &&
          strcmp(text, "21,5\u00B0") == 0);
    SV_CHECK(ha_sv_number_text("48", "48", "%", "humidity", HA_SV_UNIT_HIDE, "de", text, sizeof(text)) && strcmp(text, "48") == 0);
    /* Degree only applies to a degree unit. */
    SV_CHECK(ha_sv_number_text("48", "48", "%", "humidity", HA_SV_UNIT_DEGREE, "de", text, sizeof(text)) && strcmp(text, "48 %") == 0);
    SV_CHECK(ha_sv_number_text("2350", "2350", "W", "power", HA_SV_UNIT_AUTO, "de", text, sizeof(text)) && strcmp(text, "2,35 kW") == 0);
    SV_CHECK(ha_sv_number_text("850", "850", "W", "power", HA_SV_UNIT_AUTO, "en", text, sizeof(text)) && strcmp(text, "850 W") == 0);
    SV_CHECK(ha_sv_number_text("12345", "12345", "W", "power", HA_SV_UNIT_AUTO, "en", text, sizeof(text)) && strcmp(text, "12.3 kW") == 0);
    SV_CHECK(ha_sv_number_text("-1500", "-1500", "W", "power", HA_SV_UNIT_AUTO, "en", text, sizeof(text)) && strcmp(text, "-1.50 kW") == 0);
    SV_CHECK(ha_sv_number_text("800", "800", "Wh", "energy", HA_SV_UNIT_AUTO, "en", text, sizeof(text)) && strcmp(text, "800 Wh") == 0);
    SV_CHECK(ha_sv_number_text("4321", "4321.2", "kWh", "energy", HA_SV_UNIT_AUTO, "en", text, sizeof(text)) && strcmp(text, "4.32 MWh") == 0);
    SV_CHECK(ha_sv_number_text("2350", "2350", "W", "power", HA_SV_UNIT_DEFAULT, "en", text, sizeof(text)) && strcmp(text, "2350 W") == 0);
    SV_CHECK(ha_sv_number_text("12.34", "12.34", "EUR", "monetary", HA_SV_UNIT_DEFAULT, "de", text, sizeof(text)) && strcmp(text, "12,34 \u20AC") == 0);
    SV_CHECK(ha_sv_number_text("12.34", "12.34", "EUR", "monetary", HA_SV_UNIT_CODE, "de", text, sizeof(text)) && strcmp(text, "12,34 EUR") == 0);
    SV_CHECK(ha_sv_currency_round("0.968200683593693", "EUR", text, sizeof(text)) && strcmp(text, "0.97") == 0);
    SV_CHECK(ha_sv_currency_round("1234.5", "JPY", text, sizeof(text)) && strcmp(text, "1234") == 0);
    SV_CHECK(!ha_sv_currency_round("31.234", "ct/kWh", text, sizeof(text)));
    SV_CHECK(!ha_sv_currency_round("n/a", "EUR", text, sizeof(text)));
    SV_CHECK(ha_sv_number_text("31.2", "31.2", "ct/kWh", "monetary", HA_SV_UNIT_DEFAULT, "de", text, sizeof(text)) && strcmp(text, "31,2 ct/kWh") == 0);
    /* Text states and versions stay as they are. */
    SV_CHECK(ha_sv_number_text("1.2.3", "1.2.3", "", "", HA_SV_UNIT_DEFAULT, "de", text, sizeof(text)) && strcmp(text, "1.2.3") == 0);
    SV_CHECK(ha_sv_number_text("charging", "charging", "", "", HA_SV_UNIT_DEFAULT, "de", text, sizeof(text)) && strcmp(text, "charging") == 0);

    int64_t seconds = 0;
    SV_CHECK(ha_sv_duration_seconds("3725", "s", &seconds) && seconds == 3725);
    SV_CHECK(ha_sv_duration_seconds("1.5", "h", &seconds) && seconds == 5400);
    SV_CHECK(ha_sv_duration_seconds("1500", "ms", &seconds) && seconds == 2);
    SV_CHECK(!ha_sv_duration_seconds("3725", "W", &seconds) && !ha_sv_duration_seconds("soon", "s", &seconds));
    const char *const labels[4] = {"d", "h", "min", "s"};
    SV_CHECK(ha_sv_duration_text(45, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "45 s") == 0);
    SV_CHECK(ha_sv_duration_text(125, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "2 min 5 s") == 0);
    SV_CHECK(ha_sv_duration_text(120, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "2 min") == 0);
    SV_CHECK(ha_sv_duration_text(3725, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "1 h 2 min") == 0);
    SV_CHECK(ha_sv_duration_text(2 * 86400 + 3 * 3600 + 59, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "2 d 3 h") == 0);
    SV_CHECK(ha_sv_duration_text(-90, HA_SV_DURATION_TEXT, labels, text, sizeof(text)) && strcmp(text, "-1 min 30 s") == 0);
    SV_CHECK(ha_sv_duration_text(3725, HA_SV_DURATION_CLOCK, labels, text, sizeof(text)) && strcmp(text, "1:02:05") == 0);
    SV_CHECK(ha_sv_duration_text(26 * 3600, HA_SV_DURATION_CLOCK, labels, text, sizeof(text)) && strcmp(text, "26:00:00") == 0);
    SV_CHECK(!ha_sv_duration_text(10, HA_SV_DURATION_RAW, labels, text, sizeof(text)));

    int y = 0, m = 0, d = 0;
    SV_CHECK(ha_sv_date_parse("2026-10-21", &y, &m, &d) && y == 2026 && m == 10 && d == 21);
    SV_CHECK(!ha_sv_date_parse("2026-10-21T05:00:00", &y, &m, &d) && !ha_sv_date_parse("2026-13-01", &y, &m, &d));
    SV_CHECK(!ha_sv_date_parse("2026-02-29", &y, &m, &d) && !ha_sv_date_parse("2026-04-31", &y, &m, &d));
    SV_CHECK(ha_sv_date_parse("2028-02-29", &y, &m, &d) && !ha_sv_date_parse("0000-01-01", &y, &m, &d));
    SV_CHECK(ha_sv_days_between(2026, 10, 21, 2026, 10, 22) == 1);
    SV_CHECK(ha_sv_days_between(2026, 12, 31, 2027, 1, 1) == 1);
    SV_CHECK(ha_sv_days_between(2028, 2, 28, 2028, 3, 1) == 2); /* leap year */
    SV_CHECK(ha_sv_days_between(2026, 10, 21, 2026, 10, 18) == -3);

    SV_CHECK(ha_sv_enum_text("no_risk", text, sizeof(text)) && strcmp(text, "No risk") == 0);
    SV_CHECK(ha_sv_enum_text("idle", text, sizeof(text)) && strcmp(text, "Idle") == 0);
    SV_CHECK(!ha_sv_enum_text("-keine-", text, sizeof(text)) && !ha_sv_enum_text("Charging", text, sizeof(text)));
    SV_CHECK(!ha_sv_enum_text("42", text, sizeof(text)));
    return s_failures == failures;
}

bool test_timestamp_format(void)
{
    const int failures = s_failures;
    ha_ts_style_t style;
    SV_CHECK(ha_ts_style_from_string(NULL, &style) && style == HA_TS_DATETIME);
    SV_CHECK(ha_ts_style_from_string("", &style) && style == HA_TS_DATETIME);
    SV_CHECK(ha_ts_style_from_string("date", &style) && style == HA_TS_DATE);
    SV_CHECK(ha_ts_style_from_string("time", &style) && style == HA_TS_TIME);
    SV_CHECK(ha_ts_style_from_string("relative", &style) && style == HA_TS_RELATIVE);
    SV_CHECK(!ha_ts_style_from_string("iso", &style));

    int64_t epoch = 0, other = 0;
    SV_CHECK(ha_timestamp_parse("2026-10-21T05:56:10+00:00", &epoch) && epoch == 1792562170LL);
    SV_CHECK(ha_timestamp_parse("2026-10-21 05:56:10+00:00", &other) && other == epoch);
    SV_CHECK(ha_timestamp_parse("2026-10-21T07:56:10+02:00", &other) && other == epoch);
    SV_CHECK(ha_timestamp_parse("2026-10-21T05:56:10.123456Z", &other) && other == epoch);
    SV_CHECK(!ha_timestamp_parse("2026-10-21T05:56:10", &other));
    SV_CHECK(!ha_timestamp_parse("2026-10-21", &other));
    SV_CHECK(!ha_timestamp_parse("unknown", &other));
    SV_CHECK(!ha_timestamp_parse("12.5", &other));
    SV_CHECK(!ha_timestamp_parse("2026-02-30T05:56:10Z", &other));
    SV_CHECK(!ha_timestamp_parse("2026-10-21T05:56:10Zjunk", &other));
    SV_CHECK(!ha_timestamp_parse("2026-10-21T05:56:10+00:00junk", &other));
    SV_CHECK(!ha_timestamp_parse("2026-10-21T05:56:10.Z", &other));
    SV_CHECK(!ha_timestamp_parse("2026-10-21T-1:56:10Z", &other));

    const char *previous_tz = getenv("TZ");
    char *saved_tz = previous_tz ? strdup(previous_tz) : NULL;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    char text[40];
    SV_CHECK(ha_timestamp_format_absolute(epoch, HA_TS_DATETIME, "de", text, sizeof(text)) && strcmp(text, "21.10.2026 07:56") == 0);
    SV_CHECK(ha_timestamp_format_absolute(epoch, HA_TS_DATETIME, "en", text, sizeof(text)) && strcmp(text, "10/21/2026 07:56") == 0);
    SV_CHECK(ha_timestamp_format_absolute(epoch, HA_TS_DATE, "de", text, sizeof(text)) && strcmp(text, "21.10.2026") == 0);
    SV_CHECK(ha_timestamp_format_absolute(epoch, HA_TS_TIME, "en", text, sizeof(text)) && strcmp(text, "07:56") == 0);
    SV_CHECK(ha_timestamp_format_absolute(epoch, HA_TS_RELATIVE, "de", text, sizeof(text)) && strcmp(text, "21.10.2026 07:56") == 0);
    /* 23:30 UTC is already the next day in Berlin. */
    SV_CHECK(ha_timestamp_parse("2026-10-20T23:30:00+00:00", &other));
    SV_CHECK(ha_timestamp_format_absolute(other, HA_TS_DATE, "de", text, sizeof(text)) && strcmp(text, "21.10.2026") == 0);
    if (saved_tz) { setenv("TZ", saved_tz, 1); free(saved_tz); }
    else unsetenv("TZ");
    tzset();

    ha_ts_rel_unit_t unit;
    int amount = -1;
    bool future = false;
    ha_timestamp_relative(epoch, epoch - 30, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_NOW && amount == 0 && future);
    ha_timestamp_relative(epoch, epoch + 30, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_NOW && !future);
    ha_timestamp_relative(epoch, epoch - 5 * 60 - 59, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_MINUTES && amount == 5 && future);
    ha_timestamp_relative(epoch, epoch - 10 * 3600 - 180, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_HOURS && amount == 10 && future);
    ha_timestamp_relative(epoch, epoch + 47 * 3600, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_HOURS && amount == 47 && !future);
    ha_timestamp_relative(epoch, epoch + 2 * 86400 + 5, &unit, &amount, &future);
    SV_CHECK(unit == HA_TS_REL_DAYS && amount == 2 && !future);
    return s_failures == failures;
}
