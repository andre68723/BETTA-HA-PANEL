/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "util/coredump_store.h"

/* Early boot, after reboot history and LittleFS, before network/UI. A failed import leaves
 * the raw partition untouched and accessible. Subsequent access belongs to the HTTP task. */
void coredump_archive_init(void);
/* Atomic count for the UI task; the filesystem/cache remains owned by HTTP after boot. */
unsigned coredump_archive_count(void);
/* Add coredump.items (newest first), plus legacy newest-dump summary fields. */
void coredump_archive_add_json(cJSON *root);
/* NULL/empty id selects newest for compatibility with /api/coredump. Explicit IDs never shift. */
bool coredump_archive_get(const char *id, coredump_record_t *out);
bool coredump_archive_read(const char *id, size_t offset, void *buffer, size_t size);
esp_err_t coredump_archive_delete(const char *id);
