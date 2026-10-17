/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stddef.h>

/* Uppercase Latin-1 titles without splitting UTF-8 at the output limit. */
void matte_title_uppercase(char *out, size_t capacity, const char *input);
