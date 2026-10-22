/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#pragma once
#include "net/c6_memory_state.h"
#include "esp_err.h"

/* Called only by the diagnostics worker. busy means no RPC was started. */
typedef esp_err_t (*c6_memory_configure_fn)(c6_memory_values_t *out, bool *busy);
void c6_memory_init(c6_memory_configure_fn configure);
void c6_memory_allow(bool allow);
bool c6_memory_can_configure(void);
void c6_memory_transport(bool up);
void c6_memory_cp_init(void);
void c6_memory_report(const c6_memory_values_t *values);
void c6_memory_note_incident(void);
/* Before a deliberate network-watchdog restart: capture the latest sample with the reboot cause and wait up to
 * timeout_ms for the worker (internal stack) to write it; callers may run on a PSRAM stack. False when nothing was
 * retained in time. */
bool c6_memory_retain_for_restart(uint8_t cause, uint32_t timeout_ms);
/* Cached copies only, including incident data loaded once by the worker. */
void c6_memory_add_json(cJSON *root, cJSON *wifi_diag);
