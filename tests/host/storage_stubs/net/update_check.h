/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
bool update_check_get_available(char *, size_t, char *, size_t, bool *);

#include <stdint.h>
bool update_check_sha256_matches(const char *, const uint8_t *);
