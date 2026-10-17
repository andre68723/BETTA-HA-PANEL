/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings/runtime_settings.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "layout/layout_validate.h"

static char public_file[8192], layout_file[8192];
static unsigned writes, nvs_call, fail_nvs, fail_alloc;
static int file_error;
static bool fail_public_write, namespace_missing;
static unsigned json_alloc, fail_json;
static void *json_malloc(size_t n) { if (++json_alloc == fail_json) return NULL; return malloc(n); }
static bool gate_available = true, validation_ok = true;
static unsigned char blobs[2][2048];
static size_t blob_lengths[2];
static char legacy_values[3][512];
static bool legacy_present[3];
static FILE *storage_fopen(const char *, const char *);
static void *storage_calloc(size_t, size_t);
#define fopen storage_fopen
#define calloc storage_calloc
#include "../../main/settings/runtime_settings.c"
#include "../../main/settings/runtime_secrets.c"
#include "../../main/layout/layout_store.c"
#undef fopen
#undef calloc

static FILE *storage_fopen(const char *path, const char *mode)
{
    assert(strcmp(mode, "rb") == 0);
    if (file_error) { errno = file_error; file_error = 0; return NULL; }
    const char *text = strcmp(path, APP_SETTINGS_PATH) == 0 ? public_file : layout_file;
    if (text[0] == '\0') { errno = ENOENT; return NULL; }
    FILE *f = tmpfile(); assert(f); fwrite(text, 1, strlen(text), f); rewind(f); return f;
}
static void *storage_calloc(size_t n, size_t size)
{
    if (fail_alloc && --fail_alloc == 0) return NULL;
    return calloc(n, size);
}
bool flash_gate_enter(uint32_t ms) { (void)ms; return gate_available; }
void flash_gate_leave(void) {}
esp_err_t atomic_file_write_locked(const char *path, const void *data, size_t len)
{
    ++writes;
    if (fail_public_write) return ESP_FAIL;
    if (file_error) { file_error = 0; return ESP_FAIL; }
    char *dst = strcmp(path, APP_SETTINGS_PATH) == 0 ? public_file : layout_file;
    assert(len < 8192); memcpy(dst, data, len); dst[len] = '\0'; return ESP_OK;
}
esp_err_t atomic_file_write(const char *path, const void *data, size_t len) { return atomic_file_write_locked(path, data, len); }
bool layout_validate_json(const char *json, layout_validation_result_t *out) { (void)json; (void)out; return validation_ok; }
bool i18n_store_normalize_language_code(const char *in, char *out, size_t n) { strlcpy(out, in, n); return true; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(int)); }
void vSemaphoreDelete(SemaphoreHandle_t m) { free(m); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t t) { (void)t; if (*m) return pdFALSE; *m = 1; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { *m = 0; return pdTRUE; }
static bool nvs_fault(void) { return ++nvs_call == fail_nvs; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) { (void)name; if (nvs_fault()) return ESP_FAIL; if (namespace_missing && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND; *out = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
static int slot(const char *key) { return strcmp(key, "secrets_1") == 0; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *n)
{
    (void)h; if (nvs_fault()) return ESP_FAIL; int i = slot(key);
    if (!blob_lengths[i]) return ESP_ERR_NVS_NOT_FOUND;
    if (*n < blob_lengths[i]) return ESP_ERR_NVS_INVALID_LENGTH;
    *n = blob_lengths[i]; memcpy(out, blobs[i], *n); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t n)
{
    (void)h; if (nvs_fault()) return ESP_FAIL; int i = slot(key); assert(n <= sizeof(blobs[i]));
    /* Worst case: set persisted even if the following commit fails. */
    memcpy(blobs[i], data, n); blob_lengths[i] = n; return ESP_OK;
}
static int legacy_slot(const char *key) { return strcmp(key,"wifi_pwd") == 0 ? 0 : strcmp(key,"ha_token") == 0 ? 1 : 2; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *n)
{
    (void)h; if (nvs_fault()) return ESP_FAIL;
    if (!legacy_present[legacy_slot(key)]) return ESP_ERR_NVS_NOT_FOUND;
    const char *s = legacy_values[legacy_slot(key)];
    if (*n < strlen(s) + 1) return ESP_ERR_NVS_INVALID_LENGTH;
    strlcpy(out, s, *n); *n = strlen(s) + 1; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *s) { (void)h; if(nvs_fault()) return ESP_FAIL; strlcpy(legacy_values[legacy_slot(key)], s, 512); legacy_present[legacy_slot(key)] = true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return nvs_fault() ? ESP_FAIL : ESP_OK; }
static void baseline(void)
{
    strcpy(public_file, "{\"version\":1,\"ha\":{\"ws_url\":\"ws://old/api\"},\"wifi\":{\"ssid\":\"old\"}}");
    strcpy(layout_file, "{\"version\":1,\"pages\":[]}");
    memset(blobs, 0, sizeof(blobs)); memset(blob_lengths, 0, sizeof(blob_lengths));
    strcpy(legacy_values[0], "old-wifi"); strcpy(legacy_values[1], "old-token"); strcpy(legacy_values[2], "old-mqtt");
    for (int i = 0; i < 3; ++i) legacy_present[i] = true;
    writes = nvs_call = fail_nvs = fail_alloc = 0; file_error = 0; fail_public_write = namespace_missing = false; gate_available = validation_ok = true;
}
static runtime_settings_t changed(void)
{
    runtime_settings_t s; runtime_settings_set_defaults(&s); strcpy(s.ha_ws_url,"ws://new/api");
    strcpy(s.wifi_password,"new-wifi"); strcpy(s.ha_access_token,"new-token"); strcpy(s.mqtt_password,"new-mqtt"); return s;
}
static void expect_old(void)
{
    runtime_settings_t s; fail_nvs = 0;
    assert(runtime_settings_load(&s) == ESP_OK);
    assert(strcmp(s.ha_ws_url,"ws://old/api") == 0); assert(strcmp(s.ha_access_token,"old-token") == 0);
    assert(strcmp(s.wifi_password,"old-wifi") == 0); assert(strcmp(s.mqtt_password,"old-mqtt") == 0);
}
int main(void)
{
    runtime_settings_t next = changed(), loaded;
    unsigned cases = 0;
    for (unsigned fault = 1; fault <= 17; ++fault) {
        baseline(); fail_nvs = fault;
        esp_err_t err = runtime_settings_save(&next);
        if (err != ESP_OK) { assert(writes == 0); expect_old(); }
        else { fail_nvs = 0; assert(runtime_settings_load(&loaded) == ESP_OK); assert(strcmp(loaded.ha_access_token,"new-token") == 0); }
        ++cases;
    }
    baseline(); assert(runtime_settings_save(&next) == ESP_OK); assert(!strstr(public_file,"new-token"));
    assert(legacy_values[0][0] == 0 && legacy_values[1][0] == 0 && legacy_values[2][0] == 0);
    assert(runtime_settings_load(&loaded) == ESP_OK); assert(strcmp(loaded.ha_ws_url,next.ha_ws_url) == 0);
    char saved[8192]; strcpy(saved,public_file);
    for (unsigned fault = 1; fault <= 8; ++fault) {
        nvs_call = 0; fail_nvs = fault; writes = 0; next.ha_access_token[0] = 'X';
        assert(runtime_settings_save(&next) != ESP_OK); assert(strcmp(saved, public_file) == 0);
        fail_nvs = 0; assert(runtime_settings_load(&loaded) == ESP_OK); assert(strcmp(loaded.ha_access_token,"new-token") == 0); ++cases;
    }
    /* Readers never pair a generation mismatch with an older legacy token. */
    blob_lengths[1] = 0; strcpy(legacy_values[1],"unsafe-old-token");
    assert(runtime_settings_load(&loaded) != ESP_OK); assert(loaded.ha_access_token[0] == 0);
    baseline(); fail_nvs = 1; assert(runtime_settings_init() != ESP_OK); assert(writes == 0); expect_old();
    for (unsigned fault = 1; fault <= 3; ++fault) {
        baseline(); fail_alloc = fault; assert(runtime_settings_init() != ESP_OK); assert(writes == 0); expect_old(); ++cases;
    }
    baseline(); file_error = EIO; assert(runtime_settings_init() != ESP_OK); assert(writes == 0); expect_old();
    baseline(); gate_available = false; assert(runtime_settings_save(&next) == ESP_ERR_TIMEOUT); assert(writes == 0); gate_available = true; expect_old();
    baseline(); *s_settings_lock = 1; assert(runtime_settings_save(&next) == ESP_ERR_TIMEOUT); assert(runtime_settings_load(&loaded) == ESP_ERR_TIMEOUT); *s_settings_lock = 0; expect_old();
    baseline(); fail_alloc = 1; assert(layout_store_init() == ESP_ERR_NO_MEM); assert(writes == 0);
    baseline(); file_error = EIO; assert(layout_store_init() == ESP_FAIL); assert(writes == 0);
    baseline(); validation_ok = false; assert(layout_store_init() == ESP_OK); assert(writes == 0);
    baseline(); fail_alloc = 2; assert(layout_store_init() == ESP_OK); assert(writes == 0);
    baseline(); layout_file[0] = 0; assert(layout_store_init() == ESP_OK); assert(writes == 1);
    baseline(); namespace_missing = true; loaded = changed();
    assert(runtime_secrets_load(0, &loaded) == ESP_OK);
    assert(!loaded.wifi_password[0] && !loaded.ha_access_token[0] && !loaded.mqtt_password[0]);
    baseline(); fail_public_write = true; assert(runtime_settings_save(&next) != ESP_OK); expect_old();
    baseline(); json_alloc = 0; fail_json = 0;
    cJSON_Hooks count_hooks = {json_malloc, free}; cJSON_InitHooks(&count_hooks);
    assert(runtime_settings_save(&next) == ESP_OK);
    unsigned allocation_count = json_alloc; cJSON_InitHooks(NULL);
    for (unsigned fault = 1; fault <= allocation_count; ++fault) {
        baseline(); json_alloc = 0; fail_json = fault;
        cJSON_Hooks hooks = {json_malloc, free}; cJSON_InitHooks(&hooks);
        esp_err_t saved = runtime_settings_save(&next);
        cJSON_InitHooks(NULL);
        if (saved != ESP_OK) { assert(writes == 0); expect_old(); }
        ++cases;
    }
    /* Supported legacy writers after rollback must not reactivate the retired snapshot. */
    baseline(); assert(runtime_settings_save(&next) == ESP_OK);
    strcpy(public_file, "{\"ha\":{\"ws_url\":\"ws://after-rollback/api\"},\"wifi\":{\"ssid\":\"after-rollback\"}}");
    strcpy(legacy_values[0], "rollback-wifi"); strcpy(legacy_values[1], "rollback-token"); strcpy(legacy_values[2], "rollback-mqtt");
    assert(runtime_settings_load(&loaded) == ESP_OK);
    assert(strcmp(loaded.ha_access_token,"rollback-token") == 0 && strcmp(loaded.ha_ws_url,"ws://after-rollback/api") == 0);
    baseline(); assert(runtime_settings_load(&loaded) == ESP_OK);
    assert(runtime_settings_save(&loaded) == ESP_OK);
    assert(strcmp(legacy_values[1], "old-token") == 0); /* unchanged credentials remain usable by older firmware */
    strcpy(public_file, "{\"ha\":{\"ws_url\":\"ws://old/api\"},\"wifi\":{\"ssid\":\"old\"}}");
    strcpy(legacy_values[1], "replacement-token");
    assert(runtime_settings_load(&loaded) == ESP_OK && strcmp(loaded.ha_access_token,"replacement-token") == 0);
    assert(runtime_settings_save(&loaded) == ESP_OK); /* slot zero reused by generation two */
    strcpy(public_file, "{\"ha\":{\"ws_url\":\"ws://old/api\"},\"wifi\":{\"ssid\":\"old\"}}");
    assert(runtime_settings_load(&loaded) == ESP_OK && strcmp(loaded.ha_access_token,"replacement-token") == 0);
    legacy_present[1] = false; /* older writer explicitly cleared the token */
    assert(runtime_settings_load(&loaded) == ESP_OK && loaded.ha_access_token[0] == 0);
    baseline(); assert(runtime_settings_save(&next) == ESP_OK);
    strcpy(public_file, "{\"secret_generation\":1,\"ha\":{\"ws_url\":\"ws://unrelated/api\"}}");
    assert(runtime_settings_load(&loaded) == ESP_ERR_INVALID_RESPONSE && loaded.ha_access_token[0] == 0);
    baseline(); assert(runtime_secrets_stage(UINT32_MAX, &next) == ESP_OK);
    strcpy(public_file, "{\"secret_generation\":4294967295,\"ha\":{\"ws_url\":\"ws://new/api\"}}");
    assert(runtime_settings_save(&next) == ESP_OK);
    assert(strstr(public_file,"\"secret_generation\":2") != NULL);
    assert(runtime_settings_load(&loaded) == ESP_OK);
    printf("runtime storage: %u injected NVS/allocation faults plus initialization, mismatch and lock checks passed\n", cases);
    return 0;
}
