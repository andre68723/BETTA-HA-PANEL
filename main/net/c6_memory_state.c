/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#include "net/c6_memory_state.h"
#include "util/reboot_log.h"
#include <stdio.h>
#include <string.h>

void c6_memory_state_init(c6_memory_state_t *s, bool supported)
{
    memset(s, 0, sizeof(*s));
    s->generation = 1;
    s->unsupported = !supported;
}

void c6_memory_state_reset(c6_memory_state_t *s, int64_t now)
{
    s->generation++;
    s->configured = false;
    s->attempts = 0;
    s->last_error = 0;
    s->retry_at_ms = now + 3000;
    /* Retain the old sample as evidence, but its generation no longer confirms it. */
}

bool c6_memory_state_due(const c6_memory_state_t *s, int64_t now)
{
    return s->allowed && s->link_up && !s->unsupported && !s->configured &&
        s->attempts < C6_MEMORY_MAX_ATTEMPTS && now >= s->retry_at_ms;
}

bool c6_memory_values_valid(const c6_memory_values_t *v)
{
    return v != NULL && v->internal_largest <= v->internal_free &&
        v->dma_largest <= v->dma_free && (!v->has_minimum || v->min_free_heap <= v->free_heap);
}

static void sample(c6_memory_state_t *s, int64_t now, const c6_memory_values_t *v)
{
    s->values = *v;
    s->sampled_ms = now;
    s->sample_generation = s->generation;
    s->available = true;
    s->last_error = 0;
}

void c6_memory_state_result(c6_memory_state_t *s, uint32_t generation, int64_t now,
    int32_t error, bool busy, bool unsupported, const c6_memory_values_t *v)
{
    if (generation != s->generation || !s->allowed || !s->link_up) return;
    s->retry_at_ms = now + (busy ? 1000 : 15000);
    if (busy) return;
    s->attempts++;
    s->last_error = error;
    if (unsupported) s->unsupported = true;
    if (error == 0 && c6_memory_values_valid(v)) {
        s->configured = true;
        sample(s, now, v);
    }
}

void c6_memory_state_report(c6_memory_state_t *s, int64_t now, const c6_memory_values_t *v)
{
    /* Do not let an event queued before reset confirm the new generation. */
    if (s->configured && s->allowed && s->link_up && c6_memory_values_valid(v)) sample(s, now, v);
}

const char *c6_memory_state_name(const c6_memory_state_t *s, int64_t now)
{
    if (s->unsupported) return "unsupported";
    if (!s->available) return s->last_error != 0 || !s->link_up ? "unavailable" : "collecting";
    if (!s->allowed || !s->link_up || !s->configured || s->sample_generation != s->generation ||
        now < s->sampled_ms || now - s->sampled_ms > C6_MEMORY_STALE_MS) return "stale";
    return "fresh";
}

bool c6_memory_incident_capture(const c6_memory_state_t *s, uint32_t boot,
    int64_t now, c6_memory_incident_t *out)
{
    if (!s->available || s->sample_generation != s->generation || now < s->sampled_ms) return false;
    memset(out, 0, sizeof(*out));
    out->magic = C6_MEMORY_RECORD_MAGIC;
    out->version = C6_MEMORY_RECORD_VERSION;
    out->size = sizeof(*out);
    out->boot_id = boot;
    out->generation = s->sample_generation;
    out->sampled_ms = s->sampled_ms;
    out->incident_ms = now;
    out->values = s->values;
    return true;
}

bool c6_memory_incident_valid(const c6_memory_incident_t *r, size_t size)
{
    return r != NULL && size == sizeof(*r) && r->magic == C6_MEMORY_RECORD_MAGIC &&
        r->version == C6_MEMORY_RECORD_VERSION && r->size == sizeof(*r) && r->restart_cause < REBOOT_CAUSE_COUNT && r->sampled_ms >= 0 &&
        r->incident_ms >= r->sampled_ms && c6_memory_values_valid(&r->values);
}

static void values_json(cJSON *obj, const c6_memory_values_t *v, bool valid)
{
    if (!valid) return;
    cJSON_AddNumberToObject(obj, "free_heap_bytes", v->free_heap);
    if (v->has_minimum) cJSON_AddNumberToObject(obj, "min_free_heap_bytes", v->min_free_heap);
    cJSON_AddNumberToObject(obj, "internal_free_bytes", v->internal_free);
    cJSON_AddNumberToObject(obj, "internal_largest_block_bytes", v->internal_largest);
    cJSON_AddNumberToObject(obj, "dma_free_bytes", v->dma_free);
    cJSON_AddNumberToObject(obj, "dma_largest_block_bytes", v->dma_largest);
}

static void boot_json(cJSON *obj, uint32_t id)
{
    char text[9];
    snprintf(text, sizeof(text), "%08lx", (unsigned long)id);
    cJSON_AddStringToObject(obj, "host_boot_id", text);
}

void c6_memory_state_json(cJSON *root, const c6_memory_state_t *s, int64_t now, uint32_t boot)
{
    cJSON *obj = cJSON_AddObjectToObject(root, "c6_memory");
    if (obj == NULL) return;
    cJSON_AddStringToObject(obj, "state", c6_memory_state_name(s, now));
    cJSON_AddBoolToObject(obj, "available", s->available);
    cJSON_AddNumberToObject(obj, "generation", s->generation);
    cJSON_AddNumberToObject(obj, "report_interval_s", C6_MEMORY_INTERVAL_SEC);
    cJSON_AddNumberToObject(obj, "last_error_code", s->last_error);
    boot_json(obj, boot);
    if (s->available) {
        cJSON_AddNumberToObject(obj, "sample_generation", s->sample_generation);
        cJSON_AddNumberToObject(obj, "sample_uptime_ms", (double)s->sampled_ms);
        if (now >= s->sampled_ms) cJSON_AddNumberToObject(obj, "sample_age_ms", (double)(now - s->sampled_ms));
    }
    values_json(obj, &s->values, s->available);
}

void c6_memory_incident_json(cJSON *parent, const c6_memory_incident_t *r, uint32_t boot)
{
    if (!c6_memory_incident_valid(r, sizeof(*r))) return;
    cJSON *obj = cJSON_AddObjectToObject(parent, "c6_last_incident");
    if (obj == NULL) return;
    boot_json(obj, r->boot_id);
    cJSON_AddBoolToObject(obj, "same_host_boot", boot == r->boot_id);
    cJSON_AddNumberToObject(obj, "generation", r->generation);
    cJSON_AddNumberToObject(obj, "incident_uptime_ms", (double)r->incident_ms);
    cJSON_AddNumberToObject(obj, "sample_uptime_ms", (double)r->sampled_ms);
    cJSON_AddNumberToObject(obj, "sample_age_ms", (double)(r->incident_ms - r->sampled_ms));
    if (r->restart_cause != REBOOT_CAUSE_NONE) cJSON_AddStringToObject(obj, "restart_cause", reboot_cause_name(r->restart_cause));
    values_json(obj, &r->values, true);
}
