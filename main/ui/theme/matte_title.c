/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/theme/matte_title.h"

#include <stdbool.h>
#include <stdint.h>

void matte_title_uppercase(char *out, size_t capacity, const char *input)
{
    if (out == NULL || capacity == 0) return;
    out[0] = '\0';
    if (input == NULL) return;
    size_t used = 0;
    const uint8_t *p = (const uint8_t *)input;
    while (*p != 0) {
        uint8_t bytes[4] = {0};
        size_t count = 1;
        size_t consumed = 1;
        if (p[0] < 0x80) {
            bytes[0] = (p[0] >= 'a' && p[0] <= 'z') ? (uint8_t)(p[0] - 32) : p[0];
        } else if (p[0] == 0xC3 && p[1] != 0 && (p[1] & 0xC0) == 0x80) {
            consumed = 2;
            if (p[1] == 0x9F) { /* ß expands to SS. */
                bytes[0] = 'S'; bytes[1] = 'S'; count = 2;
            } else {
                bytes[0] = 0xC3;
                bytes[1] = ((p[1] >= 0xA0 && p[1] <= 0xB6 && p[1] != 0xB7) ||
                            (p[1] >= 0xB8 && p[1] <= 0xBE)) ? (uint8_t)(p[1] - 32) : p[1];
                count = 2;
            }
        } else {
            consumed = (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 :
                       (p[0] & 0xF8) == 0xF0 ? 4 : 1;
            bool valid = consumed > 1;
            for (size_t i = 1; i < consumed && valid; i++) {
                valid = p[i] != 0 && (p[i] & 0xC0) == 0x80;
            }
            if (valid) {
                count = consumed;
                for (size_t i = 0; i < count; i++) bytes[i] = p[i];
            } else {
                consumed = 1;
                bytes[0] = '?';
            }
        }
        if (used + count >= capacity) break;
        for (size_t i = 0; i < count; i++) out[used++] = bytes[i];
        p += consumed;
    }
    out[used] = '\0';
}
