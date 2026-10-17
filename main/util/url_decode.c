/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/url_decode.h"

#include <stddef.h>

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void url_decode_in_place(char *s)
{
    if (s == NULL) {
        return;
    }
    char *out = s;
    for (const char *in = s; *in != '\0'; in++) {
        if (*in == '+') {
            *out++ = ' ';
            continue;
        }
        if (*in == '%') {
            int hi = hex_value(in[1]);
            int lo = hi >= 0 ? hex_value(in[2]) : -1;
            if (lo >= 0 && (hi | lo) != 0) {
                *out++ = (char)((hi << 4) | lo);
                in += 2;
                continue;
            }
        }
        *out++ = *in;
    }
    *out = '\0';
}
