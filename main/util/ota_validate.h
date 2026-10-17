/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Rollback protection for OTA updates. With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE the bootloader starts a new
 * image once "pending verify"; if the panel resets before the image is marked valid, the next boot returns to the
 * previous image. This marks the running image valid after util/ota_health.h says it is healthy.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Call at the end of app_main. Starts a small task that checks every few seconds and ends once the image is valid;
 * without a pending image (normal boots, or a bootloader without rollback support) it ends at once. */
void ota_validate_start(void);

/* The bootloader in flash (not the running app): its version number (CONFIG_BOOTLOADER_PROJECT_VER) and build date/time. False when
 * it cannot be read, for example a bootloader that predates the description. /api/version reports it as "bootloader". */
bool ota_validate_bootloader_info(uint32_t *version, char *built, size_t built_len);

/* "valid", "pending_verify", "new", "invalid", "aborted" or "undefined" for the running image (/api/version). */
const char *ota_validate_image_state(void);
