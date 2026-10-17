/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/iso_time.h"

#include <stdio.h>
#include <string.h>

static int64_t civil_days(int year, int month, int day)
{
    year -= month <= 2;
    const int era = year / 400;
    const int yoe = year - era * 400;
    const int shifted = month + (month > 2 ? -3 : 9);
    return (int64_t)era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 +
           (153 * shifted + 2) / 5 + day - 1 - 719468;
}

bool iso_time_parse(const char *text, int64_t *epoch)
{
    if (text == NULL || epoch == NULL || strlen(text) < 19) return false;
    int y, mo, d, h, mi, s;
    if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6 ||
        y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) return false;
    const char *tz = text + 19;
    if (*tz == '.') {
        tz++;
        while (*tz >= '0' && *tz <= '9') tz++;
    }
    int offset = 0;
    if (*tz == '+' || *tz == '-') {
        int oh, om;
        if (sscanf(tz + 1, "%2d:%2d", &oh, &om) != 2 || oh > 14 || om > 59) return false;
        offset = (oh * 60 + om) * (*tz == '+' ? 1 : -1);
    } else if (*tz != 'Z' && *tz != '\0') return false;
    *epoch = civil_days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - offset * 60;
    return true;
}
