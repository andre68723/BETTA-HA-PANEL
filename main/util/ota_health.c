/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/ota_health.h"

bool ota_bootloader_has_rollback(uint32_t bootloader_version)
{
    return bootloader_version >= OTA_BOOTLOADER_ROLLBACK_VERSION;
}

bool ota_health_ok(const ota_health_t *health)
{
    return health != NULL && health->uptime_s >= OTA_HEALTH_MIN_UPTIME_S && health->boot_completed &&
           health->http_answers && health->network_ok;
}
