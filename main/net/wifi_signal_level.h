/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

/* Signal strength shown by the top bar's Wi-Fi symbol: 1 weak, 2 medium, 3 strong; 0 not known yet. */
#define WIFI_SIGNAL_UNKNOWN 0
#define WIFI_SIGNAL_WEAK 1
#define WIFI_SIGNAL_MEDIUM 2
#define WIFI_SIGNAL_STRONG 3

/* The level for an RSSI in dBm, given the level shown so far. Thresholds match the Wi-Fi info popup
 * (-60 dBm and better strong, -70 and better medium, weaker weak); a shown level changes only once the
 * RSSI is 3 dB past a threshold, so a value close to one does not make the symbol flicker. */
int wifi_signal_level(int rssi_dbm, int previous_level);
