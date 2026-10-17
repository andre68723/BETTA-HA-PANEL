/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/transfer_deadline.h"
void transfer_deadline_init(transfer_deadline_t *d, int64_t now_ms, uint32_t idle_ms, uint32_t total_ms)
{
    *d = (transfer_deadline_t){now_ms, now_ms, idle_ms, total_ms};
}
bool transfer_deadline_expired(const transfer_deadline_t *d, int64_t now_ms)
{
    return now_ms - d->progress_ms >= d->idle_ms || now_ms - d->started_ms >= d->total_ms;
}
void transfer_deadline_progress(transfer_deadline_t *d, int64_t now_ms) { d->progress_ms = now_ms; }
