/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stddef.h>

/* Build time of this firmware as the compiler writes it: "Sep 20 2026 10:42:11". */
extern const char app_build_stamp[];

/* The build time as "2026-09-20 10:42", or the raw stamp if it cannot be parsed. */
void app_build_stamp_format(char *out, size_t out_len);
