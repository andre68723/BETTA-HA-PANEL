/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/wifi_diag_store.h"
#include "nvs.h"

static unsigned char blobs[2][256];
static size_t lengths[2];
static bool gate = true, held;
static unsigned calls, fail_at, closes;
static bool fault(void) { return ++calls == fail_at; }
static int slot(const char *key) {
    assert(!strcmp(key, "last") || !strcmp(key, "c6_last"));
    return !strcmp(key, "c6_last");
}
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) {
    (void)mode; assert(!strcmp(name, "wifi_diag"));
    if (fault()) return ESP_FAIL;
    *out = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); closes++; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) {
    assert(h == 1); if (fault()) return ESP_FAIL;
    const int i = slot(key);
    if (!lengths[i]) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < lengths[i]) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, blobs[i], lengths[i]); *len = lengths[i]; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len) {
    assert(h == 1 && held); if (fault()) return ESP_FAIL;
    const int i = slot(key); assert(len <= sizeof(blobs[i]));
    memcpy(blobs[i], data, len); lengths[i] = len; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1 && held); return fault() ? ESP_FAIL : ESP_OK; }
bool flash_gate_enter(uint32_t ms) { assert(ms == 2000 && !held); held = gate; return held; }
void flash_gate_leave(void) { assert(held); held = false; }
int64_t esp_timer_get_time(void) { return 10000000; }

int main(void) {
    c6_memory_incident_t record, out;
    c6_memory_state_t state;
    c6_memory_state_init(&state, true);
    state.allowed = state.link_up = true;
    const c6_memory_values_t values = { .free_heap = 50000 };
    c6_memory_state_result(&state, 1, 1000, 0, false, false, &values);
    assert(c6_memory_incident_capture(&state, 42, 2000, &record));
    assert(!wifi_diag_store_load_c6(&out)); /* Existing installs have no new key. */
    wifi_mgr_diag_t legacy = { .disconnect_count = 77 }, legacy_out;
    wifi_diag_store_save(&legacy, true);
    assert(wifi_diag_store_save_c6(&record));
    assert(wifi_diag_store_load_c6(&out) && !memcmp(&record, &out, sizeof(record)));
    assert(wifi_diag_store_load(&legacy_out) && !memcmp(&legacy, &legacy_out, sizeof(legacy)));
    assert(lengths[0] == sizeof(legacy)); /* The existing raw blob is unchanged. */
    for (unsigned n = 1; n <= 3; n++) {
        calls = closes = 0; fail_at = n;
        assert(!wifi_diag_store_save_c6(&record) && !held);
        assert(closes == (n == 1 ? 0 : 1)); /* Failed open/set/commit release resources. */
    }
    calls = fail_at = 0; gate = false;
    assert(!wifi_diag_store_save_c6(&record) && calls == 0 && !held);
    gate = true; record.version++;
    memcpy(blobs[1], &record, sizeof(record));
    assert(!wifi_diag_store_load_c6(&out) && out.magic == 0);
    record.version--; memcpy(blobs[1], &record, sizeof(record)); lengths[1]--;
    assert(!wifi_diag_store_load_c6(&out) && out.magic == 0);
    lengths[1] = sizeof(record) + 1;
    assert(!wifi_diag_store_load_c6(&out) && out.magic == 0);
    assert(!wifi_diag_store_load_c6(NULL));
    assert(wifi_diag_store_load(&legacy_out) && legacy_out.disconnect_count == 77);
    puts("C6 incident store: legacy compatibility, missing/corrupt records, open/set/commit/gate failures passed");
    return 0;
}
