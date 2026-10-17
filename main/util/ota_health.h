/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * When a freshly installed firmware image counts as healthy enough to cancel the bootloader's rollback
 * (util/ota_validate.c). Pure decision, so the host tests cover it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The image must have run this long without a reset, so a crash loop or a watchdog reset a little after boot
 * still falls back to the previous image. */
#define OTA_HEALTH_MIN_UPTIME_S 60

typedef struct {
    int64_t uptime_s;
    bool boot_completed; /* app_main got through display, storage, HTTP server and UI start */
    bool http_answers;   /* the device's own HTTP server answered a request */
    bool network_ok;     /* Wi-Fi connected, or the setup access point is up */
} ota_health_t;

bool ota_health_ok(const ota_health_t *health);

/* The bootloader's own version (CONFIG_BOOTLOADER_PROJECT_VER) that first carries the rollback support; bootloaders from before it
 * report the default 1. Bump the config value together with this when the bootloader's behaviour changes again. */
#define OTA_BOOTLOADER_ROLLBACK_VERSION 2

bool ota_bootloader_has_rollback(uint32_t bootloader_version);
