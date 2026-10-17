/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "util/chime_synth.h"
void chime_configure(bool enabled, int volume, bool night_mute);
/* Test explicitly bypasses enable/night mute and uses the supplied volume. */
bool chime_request(chime_kind_t kind, bool test, int test_volume);
