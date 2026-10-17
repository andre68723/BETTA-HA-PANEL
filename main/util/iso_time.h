/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Home Assistant timestamps ("2026-09-29T00:00:00+02:00", "...Z", with optional fractional seconds) as UTC epoch
 * seconds. No libc time zone state is involved.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool iso_time_parse(const char *text, int64_t *epoch);
