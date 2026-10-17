/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Daily firmware update check: a few minutes after boot and then once a day, the panel asks the GitHub
 * release API of the fork whether a newer build for its variant exists. It only reports; installing
 * is a user action in the editor (OTA by URL) or, when ota.auto_install is on, auto_update_task.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

/* Starts the check task. Called once after the network is up; the check itself waits for Wi-Fi and a
 * synchronized clock (needed for the TLS certificate check). */
esp_err_t update_check_start(bool enabled, bool include_prereleases);
/* Settings changed without a restart: disabling clears the result, a changed channel checks again. */
void update_check_configure(bool enabled, bool include_prereleases);
/* "Check now" from the editor. ESP_ERR_INVALID_STATE while a check runs or within a minute of the
 * last one (the GitHub API allows 60 unauthenticated requests per hour and address). */
esp_err_t update_check_request_now(void);
/* Adds the "update" object to /api/ota/status. */
void update_check_add_status_json(cJSON *root);
/* The SHA-256 (64 lowercase hex digits) GitHub reports for the release asset at `url`, when that is the asset the
 * last check found. False for any other URL or when GitHub reported no digest. */
bool update_check_expected_sha256(const char *url, char out[65]);
/* The newer release the last check found (what the automatic update installs): its tag, asset URL and whether GitHub published a SHA-256
 * for it. False when there is none or the check is disabled. */
bool update_check_get_available(char *tag, size_t tag_len, char *url, size_t url_len, bool *has_sha256);
/* The newer release's tag when one was found, for the status popup on the panel. */
bool update_check_available_version(char *out, size_t out_len);
