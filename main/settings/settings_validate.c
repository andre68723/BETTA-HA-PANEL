/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/settings_validate.h"

#include <string.h>

bool settings_host_valid(const char *host)
{
    if (host == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)host; *p != '\0'; p++) {
        const bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                        *p == '.' || *p == '-' || *p == '_' || *p == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool settings_timezone_valid(const char *tz)
{
    if (tz == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)tz; *p != '\0'; p++) {
        if (*p < 0x20 || *p > 0x7e) {
            return false;
        }
    }
    return true;
}

bool settings_topic_prefix_valid(const char *prefix)
{
    if (prefix == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)prefix; *p != '\0'; p++) {
        if (*p <= 0x20 || *p > 0x7e || *p == '+' || *p == '#') {
            return false;
        }
    }
    const size_t len = strlen(prefix);
    if (len > 0U && (prefix[0] == '/' || prefix[len - 1U] == '/')) {
        return false;
    }
    return strstr(prefix, "//") == NULL;
}

bool settings_text_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            return false;
        }
    }
    return true;
}
