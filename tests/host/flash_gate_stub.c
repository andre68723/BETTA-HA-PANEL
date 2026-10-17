/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/flash_gate.h"
/* Filesystem tests run synchronously; gate fault injection uses the storage test stubs. */
bool flash_gate_enter(uint32_t timeout_ms) { (void)timeout_ms; return true; }
void flash_gate_leave(void) {}
