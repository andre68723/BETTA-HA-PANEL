/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/auto_update.h"

#include <string.h>

auto_update_decision_t auto_update_decide(const auto_update_input_t *in)
{
    if (in == NULL || !in->enabled) {
        return AUTO_UPDATE_WAIT_DISABLED;
    }
    if (in->busy) {
        return AUTO_UPDATE_WAIT_BUSY;
    }
    if (in->safe_mode) {
        return AUTO_UPDATE_WAIT_SAFE_MODE;
    }
    if (!in->update_available) {
        return AUTO_UPDATE_WAIT_NO_UPDATE;
    }
    if (in->release_tag != NULL && in->blocked_tag != NULL && in->blocked_tag[0] != '\0' &&
        strcmp(in->release_tag, in->blocked_tag) == 0) {
        return AUTO_UPDATE_WAIT_BLOCKED_VERSION;
    }
    if (in->tried_today) {
        return AUTO_UPDATE_WAIT_TRIED_TODAY;
    }
    if (!in->sha256_known) {
        return AUTO_UPDATE_WAIT_NO_SHA256;
    }
    if (!in->bootloader_rollback) {
        return AUTO_UPDATE_WAIT_NO_ROLLBACK;
    }
    if (!in->time_valid) {
        return AUTO_UPDATE_WAIT_TIME_UNKNOWN;
    }
    if (in->local_minute_of_day < AUTO_UPDATE_WINDOW_START_MIN || in->local_minute_of_day >= AUTO_UPDATE_WINDOW_END_MIN) {
        return AUTO_UPDATE_WAIT_OUTSIDE_WINDOW;
    }
    if (in->local_minute_of_day < AUTO_UPDATE_WINDOW_START_MIN + in->start_offset_min) {
        return AUTO_UPDATE_WAIT_START_MINUTE;
    }
    if (in->idle_s < AUTO_UPDATE_IDLE_MIN_S) {
        return AUTO_UPDATE_WAIT_ACTIVITY;
    }
    if (!in->wifi_connected || in->rssi_dbm < AUTO_UPDATE_MIN_RSSI_DBM) {
        return AUTO_UPDATE_WAIT_WIFI;
    }
    if (in->free_internal < AUTO_UPDATE_MIN_FREE_INTERNAL || in->largest_internal < AUTO_UPDATE_MIN_LARGEST_INTERNAL ||
        in->free_psram < AUTO_UPDATE_MIN_FREE_PSRAM) {
        return AUTO_UPDATE_WAIT_MEMORY;
    }
    return AUTO_UPDATE_GO;
}

const char *auto_update_decision_name(auto_update_decision_t decision)
{
    switch (decision) {
    case AUTO_UPDATE_GO: return "go";
    case AUTO_UPDATE_WAIT_STORAGE: return "wait_storage";
    case AUTO_UPDATE_WAIT_DISABLED: return "wait_disabled";
    case AUTO_UPDATE_WAIT_BUSY: return "wait_busy";
    case AUTO_UPDATE_WAIT_SAFE_MODE: return "wait_safe_mode";
    case AUTO_UPDATE_WAIT_NO_UPDATE: return "wait_no_update";
    case AUTO_UPDATE_WAIT_BLOCKED_VERSION: return "wait_blocked_version";
    case AUTO_UPDATE_WAIT_TRIED_TODAY: return "wait_tried_today";
    case AUTO_UPDATE_WAIT_NO_SHA256: return "wait_no_sha256";
    case AUTO_UPDATE_WAIT_NO_ROLLBACK: return "wait_no_rollback";
    case AUTO_UPDATE_WAIT_TIME_UNKNOWN: return "wait_time_unknown";
    case AUTO_UPDATE_WAIT_OUTSIDE_WINDOW: return "wait_outside_window";
    case AUTO_UPDATE_WAIT_START_MINUTE: return "wait_start_minute";
    case AUTO_UPDATE_WAIT_ACTIVITY: return "wait_activity";
    case AUTO_UPDATE_WAIT_WIFI: return "wait_wifi";
    case AUTO_UPDATE_WAIT_MEMORY: return "wait_memory";
    }
    return "wait_unknown";
}

int auto_update_start_offset_min(uint32_t device_salt, int32_t day_number)
{
    /* A small integer hash (xorshift-multiply); only the spread matters, it is not a secret. */
    uint32_t h = device_salt ^ ((uint32_t)day_number * 2654435761U);
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return (int)(h % 60U);
}
