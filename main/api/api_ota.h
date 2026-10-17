/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Starting an OTA install from inside the firmware (the automatic update, net/auto_update_task.c); the editor uses the HTTP handlers.
 */
#pragma once

#include "esp_err.h"

typedef enum {
    API_OTA_PHASE_IDLE = 0,
    API_OTA_PHASE_RUNNING,   /* downloading or writing */
    API_OTA_PHASE_REBOOTING, /* written and verified, the panel restarts in a moment */
    API_OTA_PHASE_FAILED     /* the last install ended with an error (api_ota status "error") */
} api_ota_phase_t;

/* Installs the image at `url` like POST /api/ota/url (same URL rules, project check and SHA-256 check against the update check's
 * release). ESP_OK: started in its own task. ESP_ERR_INVALID_ARG: not an accepted URL; ESP_ERR_NOT_FOUND: no OTA partition;
 * ESP_ERR_INVALID_STATE: another install is running or a restart is pending; otherwise a start failure. */
esp_err_t api_ota_start_url(const char *url);
api_ota_phase_t api_ota_phase(void);
