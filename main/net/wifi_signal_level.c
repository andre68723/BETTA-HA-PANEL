/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/wifi_signal_level.h"

#define WIFI_SIGNAL_STRONG_DBM (-60)
#define WIFI_SIGNAL_MEDIUM_DBM (-70)
#define WIFI_SIGNAL_HYSTERESIS_DB 3

static int wifi_signal_raw_level(int rssi_dbm)
{
    if (rssi_dbm >= WIFI_SIGNAL_STRONG_DBM) {
        return WIFI_SIGNAL_STRONG;
    }
    if (rssi_dbm >= WIFI_SIGNAL_MEDIUM_DBM) {
        return WIFI_SIGNAL_MEDIUM;
    }
    return WIFI_SIGNAL_WEAK;
}

int wifi_signal_level(int rssi_dbm, int previous_level)
{
    const int raw = wifi_signal_raw_level(rssi_dbm);
    if (previous_level < WIFI_SIGNAL_WEAK || previous_level > WIFI_SIGNAL_STRONG || raw == previous_level) {
        return raw;
    }
    /* Leave the shown level only 3 dB beyond the threshold between it and the next one. */
    if (raw > previous_level) {
        const int up_threshold = previous_level == WIFI_SIGNAL_WEAK ? WIFI_SIGNAL_MEDIUM_DBM : WIFI_SIGNAL_STRONG_DBM;
        return rssi_dbm >= up_threshold + WIFI_SIGNAL_HYSTERESIS_DB ? raw : previous_level;
    }
    const int down_threshold = previous_level == WIFI_SIGNAL_STRONG ? WIFI_SIGNAL_STRONG_DBM : WIFI_SIGNAL_MEDIUM_DBM;
    return rssi_dbm < down_threshold - WIFI_SIGNAL_HYSTERESIS_DB ? raw : previous_level;
}
