/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Gate for background flash writers. Code and read-only data run from PSRAM through the flash MMU
 * (CONFIG_SPIRAM_XIP_FROM_PSRAM); esp_ota_end() verifies the new image by remapping flash pages, and a flash write in
 * that window can reset the chip (kruzio's storage_guard saw rst:0x7). A writer that can run at any time (the graph
 * history saver) holds the gate for the length of its write; the OTA code holds it around esp_ota_end(). Both wait
 * a bounded time: a writer that cannot get the gate skips this save, the OTA aborts after the timeout.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Take the gate, waiting at most timeout_ms. Returns false on timeout; then the caller must not call flash_gate_leave(). */
bool flash_gate_enter(uint32_t timeout_ms);
void flash_gate_leave(void);
