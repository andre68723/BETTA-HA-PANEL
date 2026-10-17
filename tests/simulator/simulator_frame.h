/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "cJSON.h"

/* Feeds the firmware's real top bar (ui_pages.c) from a fixture's optional "frame": "date" (dd.mm.yyyy) and "time"
 * (HH:MM) default to now; "weather_temperature" ("18°C") shows the weather chip; "stocks" is a list of
 * {"symbol", "value"} for the stock quote. Wi-Fi and Home Assistant show as connected; "mqtt": true adds the MQTT chip, connected, with a demo broker. */
void simulator_frame_apply(const cJSON *frame);
