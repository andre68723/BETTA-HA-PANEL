/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define C6_MEMORY_INTERVAL_SEC 60
#define C6_MEMORY_STALE_MS 180000
#define C6_MEMORY_MAX_ATTEMPTS 3

typedef struct {
    uint32_t free_heap, min_free_heap;
    uint32_t internal_free, internal_largest;
    uint32_t dma_free, dma_largest;
    bool has_minimum;
} c6_memory_values_t;

typedef struct {
    c6_memory_values_t values;
    int64_t sampled_ms, retry_at_ms;
    uint32_t generation, sample_generation;
    int32_t last_error;
    uint8_t attempts;
    bool available, allowed, link_up, configured, unsupported;
} c6_memory_state_t;

/* A separate NVS key/version preserves the legacy Wi-Fi diagnostic blob. */
#define C6_MEMORY_RECORD_MAGIC 0x43364d31U
#define C6_MEMORY_RECORD_VERSION 2 /* 2: restart_cause; a version 1 record is dropped */
typedef struct {
    uint32_t magic, version, size, boot_id, generation;
    int64_t sampled_ms, incident_ms;
    c6_memory_values_t values;
    uint8_t restart_cause; /* reboot_cause_t of a watchdog restart that followed, 0 for a connection incident */
    uint8_t reserved[7];
} c6_memory_incident_t;

void c6_memory_state_init(c6_memory_state_t *state, bool supported);
void c6_memory_state_reset(c6_memory_state_t *state, int64_t now_ms);
bool c6_memory_state_due(const c6_memory_state_t *state, int64_t now_ms);
bool c6_memory_values_valid(const c6_memory_values_t *values);
void c6_memory_state_result(c6_memory_state_t *state, uint32_t generation, int64_t now_ms,
    int32_t error, bool busy, bool unsupported, const c6_memory_values_t *values);
void c6_memory_state_report(c6_memory_state_t *state, int64_t now_ms, const c6_memory_values_t *values);
const char *c6_memory_state_name(const c6_memory_state_t *state, int64_t now_ms);
bool c6_memory_incident_capture(const c6_memory_state_t *state, uint32_t boot_id,
    int64_t now_ms, c6_memory_incident_t *out);
bool c6_memory_incident_valid(const c6_memory_incident_t *record, size_t size);
void c6_memory_state_json(cJSON *root, const c6_memory_state_t *state, int64_t now_ms, uint32_t boot_id);
void c6_memory_incident_json(cJSON *parent, const c6_memory_incident_t *record, uint32_t boot_id);
