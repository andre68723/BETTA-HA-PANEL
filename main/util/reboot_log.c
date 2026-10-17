/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/reboot_log.h"

#include <string.h>

/* esp_reset_reason_t values, copied so this file stays free of ESP-IDF (reboot_log_esp.c asserts them). */
enum {
    RST_UNKNOWN = 0, RST_POWERON = 1, RST_EXT = 2, RST_SW = 3, RST_PANIC = 4, RST_INT_WDT = 5, RST_TASK_WDT = 6,
    RST_WDT = 7, RST_DEEPSLEEP = 8, RST_BROWNOUT = 9, RST_SDIO = 10, RST_USB = 11, RST_JTAG = 12, RST_EFUSE = 13,
    RST_PWR_GLITCH = 14, RST_CPU_LOCKUP = 15,
};

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t crumb_crc(const reboot_crumb_t *crumb)
{
    return crc32_bytes((const uint8_t *)crumb, offsetof(reboot_crumb_t, crc));
}

void reboot_crumb_seal(reboot_crumb_t *crumb)
{
    crumb->magic = REBOOT_CRUMB_MAGIC;
    crumb->crc = crumb_crc(crumb);
}

bool reboot_crumb_valid(const reboot_crumb_t *crumb)
{
    return crumb != NULL && crumb->magic == REBOOT_CRUMB_MAGIC && crumb->crc == crumb_crc(crumb) &&
           crumb->cause < REBOOT_CAUSE_COUNT;
}

void reboot_ring_init_if_invalid(reboot_ring_t *ring)
{
    if (ring->magic == REBOOT_RING_MAGIC && ring->head < REBOOT_LOG_SLOTS && ring->count <= REBOOT_LOG_SLOTS) return;
    memset(ring, 0, sizeof(*ring));
    ring->magic = REBOOT_RING_MAGIC;
}

void reboot_entry_build(reboot_entry_t *out, uint32_t seq, uint8_t reset_reason, const reboot_crumb_t *crumb)
{
    memset(out, 0, sizeof(*out));
    out->seq = seq;
    out->reset_reason = reset_reason;
    /* after a power cycle or brownout the RTC memory is undefined, whatever its checksum says */
    const bool rtc_kept = reset_reason != RST_POWERON && reset_reason != RST_BROWNOUT;
    if (rtc_kept && reboot_crumb_valid(crumb)) {
        out->had_crumb = 1;
        out->prev_uptime_s = crumb->uptime_s;
        out->min_free_internal = crumb->min_free_internal;
        out->largest_internal = crumb->largest_internal;
        out->prev_wall_epoch = crumb->wall_epoch;
        /* a tag only explains a software restart; after a crash the tag of an earlier, unfinished restart must not count */
        if (reset_reason == RST_SW) out->cause = crumb->cause;
    }
}

void reboot_ring_push(reboot_ring_t *ring, reboot_entry_t *entry)
{
    reboot_ring_init_if_invalid(ring);
    entry->seq = ++ring->next_seq;
    ring->slot[ring->head] = *entry;
    ring->head = (uint8_t)((ring->head + 1) % REBOOT_LOG_SLOTS);
    if (ring->count < REBOOT_LOG_SLOTS) ring->count++;
}

bool reboot_ring_get(const reboot_ring_t *ring, int index, reboot_entry_t *out)
{
    if (ring == NULL || ring->magic != REBOOT_RING_MAGIC || index < 0 || index >= ring->count) return false;
    const int slot = ((int)ring->head - 1 - index + 2 * REBOOT_LOG_SLOTS) % REBOOT_LOG_SLOTS;
    *out = ring->slot[slot];
    return true;
}

const char *reboot_reason_name(uint8_t reason)
{
    switch (reason) {
    case RST_UNKNOWN: return "unknown";
    case RST_POWERON: return "power-on";
    case RST_EXT: return "external pin";
    case RST_SW: return "software";
    case RST_PANIC: return "panic";
    case RST_INT_WDT: return "interrupt watchdog";
    case RST_TASK_WDT: return "task watchdog";
    case RST_WDT: return "watchdog";
    case RST_DEEPSLEEP: return "deep sleep";
    case RST_BROWNOUT: return "brownout";
    case RST_SDIO: return "SDIO";
    case RST_USB: return "USB";
    case RST_JTAG: return "JTAG";
    case RST_EFUSE: return "eFuse";
    case RST_PWR_GLITCH: return "power glitch";
    case RST_CPU_LOCKUP: return "CPU lockup";
    default: return "other";
    }
}

const char *reboot_cause_name(uint8_t cause)
{
    switch (cause) {
    case REBOOT_CAUSE_NONE: return "none";
    case REBOOT_CAUSE_SETTINGS: return "settings";
    case REBOOT_CAUSE_OTA: return "ota";
    case REBOOT_CAUSE_FACTORY_RESET: return "factory_reset";
    case REBOOT_CAUSE_C6_UPDATE: return "c6_update";
    case REBOOT_CAUSE_NET_WATCHDOG: return "net_watchdog";
    case REBOOT_CAUSE_HTTP_WATCHDOG: return "http_watchdog";
    case REBOOT_CAUSE_UI_WATCHDOG: return "ui_watchdog";
    case REBOOT_CAUSE_MANUAL: return "manual";
    default: return "other";
    }
}

const char *reboot_entry_label(const reboot_entry_t *entry)
{
    if (entry->cause == REBOOT_CAUSE_UI_WATCHDOG) {
        return "UI watchdog"; /* a deliberate abort(), so the reset reason itself says "panic" */
    }
    if (entry->reset_reason == RST_SW) {
        switch (entry->cause) {
        case REBOOT_CAUSE_SETTINGS: return "settings restart";
        case REBOOT_CAUSE_OTA: return "firmware update";
        case REBOOT_CAUSE_FACTORY_RESET: return "factory reset";
        case REBOOT_CAUSE_C6_UPDATE: return "coprocessor update";
        case REBOOT_CAUSE_NET_WATCHDOG: return "network watchdog";
        case REBOOT_CAUSE_HTTP_WATCHDOG: return "HTTP watchdog";
        case REBOOT_CAUSE_MANUAL: return "manual restart";
        default: return entry->had_crumb ? "software restart (untagged)" : "software restart";
        }
    }
    return reboot_reason_name(entry->reset_reason);
}

bool reboot_entry_is_failure(const reboot_entry_t *entry)
{
    if (entry->cause == REBOOT_CAUSE_UI_WATCHDOG) {
        return true;
    }
    switch (entry->reset_reason) {
    case RST_PANIC:
    case RST_INT_WDT:
    case RST_TASK_WDT:
    case RST_WDT:
        return true;
    case RST_SW:
        return entry->cause == REBOOT_CAUSE_NET_WATCHDOG || entry->cause == REBOOT_CAUSE_HTTP_WATCHDOG;
    default:
        return false;
    }
}
