/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/device_identity.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_wifi.h"

#define DEVICE_SLUG_MAX 16

static char s_room[DEVICE_ROOM_MAX_LEN] = {0};

void device_identity_set_room(const char *room)
{
    strlcpy(s_room, room != NULL ? room : "", sizeof(s_room));
}

/* Lowercase ASCII slug: umlauts become ae/oe/ue/ss, everything else that is not
 * a letter or digit becomes one underscore. */
static void device_slug(char *out, size_t out_len)
{
    size_t n = 0;
    bool pending_sep = false;
    for (const unsigned char *p = (const unsigned char *)s_room; *p != '\0' && n + 3 < out_len && n < DEVICE_SLUG_MAX; p++) {
        const char *rep = NULL;
        if (p[0] == 0xC3 && p[1] != '\0') {
            switch (p[1]) {
            case 0xA4: case 0x84: rep = "ae"; break;
            case 0xB6: case 0x96: rep = "oe"; break;
            case 0xBC: case 0x9C: rep = "ue"; break;
            case 0x9F: rep = "ss"; break;
            default: break;
            }
            p++;
        }
        char one[2] = {0, 0};
        if (rep == NULL) {
            if (isalnum(*p)) {
                one[0] = (char)tolower(*p);
                rep = one;
            } else {
                pending_sep = n > 0;
                continue;
            }
        }
        if (pending_sep && n + 1 < out_len) {
            out[n++] = '_';
        }
        pending_sep = false;
        for (const char *r = rep; *r != '\0' && n + 1 < out_len && n < DEVICE_SLUG_MAX; r++) {
            out[n++] = *r;
        }
    }
    out[n] = '\0';
}

bool device_identity_room_id(char *out, size_t out_len)
{
    char slug[DEVICE_SLUG_MAX + 4];
    device_slug(slug, sizeof(slug));
    if (slug[0] == '\0') {
        return false;
    }
    snprintf(out, out_len, "betta_ha_panel_%s", slug);
    return true;
}

void device_identity_display_name(char *out, size_t out_len)
{
    if (s_room[0] != '\0') {
        snprintf(out, out_len, "BETTA %s", s_room);
    } else {
        snprintf(out, out_len, "BETTA HA Panel");
    }
}

void device_identity_effective_id(char *out, size_t out_len)
{
    if (device_identity_room_id(out, out_len)) {
        return;
    }
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK && esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(out, out_len, "panel-0000");
        return;
    }
    snprintf(out, out_len, "panel-%02X%02X", mac[4], mac[5]);
}
