/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Central device identity: the room the panel stands in (settings "device.room")
 * gives the device id ("betta_ha_panel_arbeitszimmer") that MQTT, Home Assistant
 * discovery and the editor header use. Without a room the id stays the
 * MAC-based "panel-XXXX".
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define DEVICE_ROOM_MAX_LEN 32

void device_identity_set_room(const char *room);

/* "betta_ha_panel_<slug>"; false while no room is set. */
bool device_identity_room_id(char *out, size_t out_len);

/* "BETTA <Room>", or "BETTA HA Panel" without a room. */
void device_identity_display_name(char *out, size_t out_len);

/* The id in use: the room id, else the MAC-based one. */
void device_identity_effective_id(char *out, size_t out_len);
