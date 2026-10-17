/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdint.h>

/* MDI fallback for boards without the colored PNG icons. */
uint32_t weather_icon_codepoint_for_key(const char *key);
