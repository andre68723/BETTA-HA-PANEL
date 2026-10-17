/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Build time of this firmware ("Sep 20 2026 10:42:11"). The CMake target app_touch_build_stamp
 * touches this file before every build so that it is recompiled each time; the date in
 * ESP-IDF's application descriptor only changes when the project configuration does.
 */
#include "build_stamp.h"

#include <stdio.h>
#include <string.h>

const char app_build_stamp[] = __DATE__ " " __TIME__;

void app_build_stamp_format(char *out, size_t out_len)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    const char *found = NULL;
    if (sscanf(app_build_stamp, "%3s %d %d %d:%d", month, &day, &year, &hour, &minute) == 5 && strlen(month) == 3) {
        found = strstr(months, month);
    }
    if (found == NULL) {
        snprintf(out, out_len, "%s", app_build_stamp);
        return;
    }
    snprintf(out, out_len, "%04d-%02d-%02d %02d:%02d", year, (int)((found - months) / 3) + 1, day, hour, minute);
}
