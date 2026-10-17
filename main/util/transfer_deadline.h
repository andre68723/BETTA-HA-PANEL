/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int64_t started_ms, progress_ms;
    uint32_t idle_ms, total_ms;
} transfer_deadline_t;
void transfer_deadline_init(transfer_deadline_t *d, int64_t now_ms, uint32_t idle_ms, uint32_t total_ms);
bool transfer_deadline_expired(const transfer_deadline_t *d, int64_t now_ms);
void transfer_deadline_progress(transfer_deadline_t *d, int64_t now_ms);
