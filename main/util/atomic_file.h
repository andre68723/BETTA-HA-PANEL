/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Replace the file at `path` with `len` bytes of `data`: the bytes go to "<path>.tmp", are flushed, and the
 * temporary file is then renamed over `path`. A power loss or write error leaves the old file complete
 * instead of a truncated one. A stale "<path>.tmp" from an interrupted write is overwritten by the next call.
 * The ".tmp" suffix keeps the temporary file out of the stores' ".json" directory listings. */
esp_err_t atomic_file_write(const char *path, const void *data, size_t len);

/* Caller already holds flash_gate for a transaction containing additional NVS/file writes. */
esp_err_t atomic_file_write_locked(const char *path, const void *data, size_t len);
