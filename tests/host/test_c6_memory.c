/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "util/reboot_log.h"
#include "net/c6_memory_state.h"

static c6_memory_values_t values(bool minimum)
{
    return (c6_memory_values_t){ .free_heap = 50000, .min_free_heap = 30000,
        .internal_free = 48000, .internal_largest = 24000,
        .dma_free = 40000, .dma_largest = 20000, .has_minimum = minimum };
}

int main(void)
{
    c6_memory_state_t s;
    c6_memory_state_init(&s, true);
    assert(!c6_memory_state_due(&s, 0));
    s.allowed = true;
    s.link_up = true;
    assert(strcmp(c6_memory_state_name(&s, 0), "collecting") == 0);
    assert(c6_memory_state_due(&s, 0));
    c6_memory_values_t v = values(false);
    c6_memory_state_result(&s, s.generation, 100, 1, true, false, NULL);
    assert(s.attempts == 0 && !c6_memory_state_due(&s, 100));
    c6_memory_state_result(&s, s.generation, 1100, 0, false, false, &v);
    assert(s.configured && s.available && !s.values.has_minimum);
    assert(!c6_memory_state_due(&s, 100000));
    assert(strcmp(c6_memory_state_name(&s, 181100), "fresh") == 0);
    assert(strcmp(c6_memory_state_name(&s, 181101), "stale") == 0);
    assert(strcmp(c6_memory_state_name(&s, 0), "stale") == 0);

    cJSON *root = cJSON_CreateObject();
    c6_memory_state_json(root, &s, 1200, 42);
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, "c6_memory");
    assert(cJSON_GetObjectItemCaseSensitive(obj, "free_heap_bytes")->valuedouble == 50000);
    assert(cJSON_GetObjectItemCaseSensitive(obj, "min_free_heap_bytes") == NULL);
    assert(cJSON_GetObjectItemCaseSensitive(obj, "total_heap_bytes") == NULL);
    cJSON_Delete(root);

    v = values(true);
    c6_memory_state_report(&s, 2000, &v);
    assert(s.values.has_minimum && s.values.min_free_heap == 30000);
    c6_memory_incident_t r;
    assert(c6_memory_incident_capture(&s, 42, 2500, &r));
    assert(c6_memory_incident_valid(&r, sizeof(r)));
    assert(!c6_memory_incident_valid(&r, sizeof(r) - 1));
    r.version++;
    assert(!c6_memory_incident_valid(&r, sizeof(r)));
    r.version--;
    r.incident_ms = 0;
    assert(!c6_memory_incident_valid(&r, sizeof(r)));
    assert(c6_memory_incident_capture(&s, 42, 2500, &r));
    root = cJSON_CreateObject();
    c6_memory_incident_json(root, &r, 99);
    obj = cJSON_GetObjectItemCaseSensitive(root, "c6_last_incident");
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(obj, "same_host_boot")));
    assert(cJSON_GetObjectItemCaseSensitive(obj, "sample_age_ms")->valuedouble == 500);
    assert(cJSON_GetObjectItemCaseSensitive(obj, "restart_cause") == NULL); /* a connection incident */
    cJSON_Delete(root);
    /* A watchdog restart names its reboot cause; a version 1 record (no cause) or an unknown cause is dropped. */
    r.restart_cause = REBOOT_CAUSE_NET_WATCHDOG;
    root = cJSON_CreateObject();
    c6_memory_incident_json(root, &r, 42);
    obj = cJSON_GetObjectItemCaseSensitive(root, "c6_last_incident");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(obj, "restart_cause")->valuestring, "net_watchdog") == 0);
    cJSON_Delete(root);
    r.version = 1;
    assert(!c6_memory_incident_valid(&r, sizeof(r)));
    r.version = C6_MEMORY_RECORD_VERSION;
    r.restart_cause = REBOOT_CAUSE_COUNT;
    assert(!c6_memory_incident_valid(&r, sizeof(r)));
    r.restart_cause = 0;

    uint32_t old = s.generation;
    c6_memory_state_reset(&s, 3000);
    assert(strcmp(c6_memory_state_name(&s, 3000), "stale") == 0);
    assert(!c6_memory_incident_capture(&s, 42, 3100, &r));
    c6_memory_state_result(&s, old, 4000, 0, false, false, &v);
    c6_memory_state_report(&s, 4000, &v);
    assert(s.sample_generation == old && !s.configured);
    assert(!c6_memory_state_due(&s, 5999));
    assert(c6_memory_state_due(&s, 6000));
    c6_memory_state_result(&s, s.generation, 6000, 0, false, false, &v);
    assert(s.configured && s.sample_generation != old);
    s.link_up = false;
    assert(strcmp(c6_memory_state_name(&s, 6000), "stale") == 0);

    c6_memory_state_init(&s, true);
    s.allowed = s.link_up = true;
    for (int i = 0; i < C6_MEMORY_MAX_ATTEMPTS; ++i) {
        assert(c6_memory_state_due(&s, i * 15000));
        c6_memory_state_result(&s, s.generation, i * 15000, 9, false, false, NULL);
    }
    assert(!c6_memory_state_due(&s, 999999));
    assert(strcmp(c6_memory_state_name(&s, 999999), "unavailable") == 0);
    c6_memory_state_reset(&s, 1000000);
    assert(c6_memory_state_due(&s, 1003000));
    c6_memory_state_result(&s, s.generation, 1003000, 7, false, true, NULL);
    assert(strcmp(c6_memory_state_name(&s, 1003000), "unsupported") == 0);
    assert(!c6_memory_state_due(&s, 2000000));

    /* Real zero is valid. Capability categories overlap: do not add them together. */
    v = (c6_memory_values_t){.has_minimum = true};
    assert(c6_memory_values_valid(&v));
    v.internal_largest = 1;
    assert(!c6_memory_values_valid(&v));
    v = values(true);
    v.min_free_heap = 60000;
    assert(!c6_memory_values_valid(&v));
    c6_memory_state_init(&s, false);
    assert(strcmp(c6_memory_state_name(&s, 0), "unsupported") == 0);
    puts("C6 memory: lifecycle, bounded retries, minima, stale data, incident format and JSON passed");
    return 0;
}
