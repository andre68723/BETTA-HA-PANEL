/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * RAM ring buffer with the most recent log lines, readable through GET /api/log.
 */
#pragma once

#include <stddef.h>

/* Install the log hook (keeps writing to the serial console as before). Call as early as possible. */
void log_buffer_init(void);

/* Copy up to len bytes starting at offset (0 = oldest kept byte). Returns the bytes copied. */
size_t log_buffer_read(size_t offset, char *out, size_t len);

/* Number of bytes currently held. */
size_t log_buffer_size(void);
