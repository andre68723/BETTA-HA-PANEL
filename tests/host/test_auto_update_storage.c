/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CONFIG_APP_AUTO_UPDATE_TEST_HOOK 1
#include "../../main/net/auto_update_task.c"

static auto_update_state_t durable;
static bool has_state, gate = true, sent = true, display_available = true, display_held;
static unsigned calls, fail_at, installs;
static const esp_app_desc_t app = {.version="v0.9.0"};
static bool fault(void) { return ++calls == fail_at; }
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *h) { (void)name; (void)mode; if(fault())return ESP_FAIL; *h=1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *len)
{
    (void)h; (void)key; if(fault())return ESP_FAIL; if(!has_state)return ESP_ERR_NVS_NOT_FOUND;
    assert(*len>=sizeof(durable)); memcpy(out,&durable,sizeof(durable)); *len=sizeof(durable); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len)
{
    (void)h; (void)key; if(fault())return ESP_FAIL; assert(len==sizeof(durable));
    memcpy(&durable,data,len); has_state=true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return fault()?ESP_FAIL:ESP_OK; }
bool flash_gate_enter(uint32_t ms) { (void)ms; return gate; }
void flash_gate_leave(void) {}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(int)); }
void vSemaphoreDelete(SemaphoreHandle_t m) { free(m); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m,TickType_t t) { (void)t; assert(!*m); *m=1; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { assert(*m); *m=0; return pdTRUE; }
const esp_app_desc_t *esp_app_get_description(void) { return &app; }
const char *ota_validate_image_state(void) { return "valid"; }
bool ota_validate_bootloader_info(uint32_t *v,char *b,size_t n) { (void)b; (void)n; *v=9000; return true; }
bool reboot_log_last_run_failed(void) { return false; }
bool boot_guard_safe_mode(void) { return false; }
bool update_check_get_available(char *tag,size_t tn,char *url,size_t un,bool *sha) { snprintf(tag,tn,"v1.0.0"); snprintf(url,un,"https://example.invalid/test.bin"); *sha=true; return true; }
bool wifi_mgr_is_connected(void) { return true; }
esp_err_t wifi_mgr_try_get_sta_rssi(int8_t *r) { *r=-40; return ESP_OK; }
size_t heap_caps_get_free_size(unsigned cap) { (void)cap; return 8*1024*1024; }
size_t heap_caps_get_largest_free_block(unsigned cap) { (void)cap; return 1024*1024; }
esp_err_t esp_efuse_mac_get_default(uint8_t *mac) { memset(mac,0,6); return ESP_OK; }
int64_t esp_timer_get_time(void) { return 1000000000; }
int64_t user_activity_idle_s(uint32_t now) { (void)now; return 1000; }
api_ota_phase_t api_ota_phase(void) { return installs ? API_OTA_PHASE_REBOOTING : API_OTA_PHASE_IDLE; }
esp_err_t api_ota_start_url(const char *url) { (void)url; assert(has_state && durable.attempt_tag[0]); assert(s_storage_ok); ++installs; return ESP_OK; }
void vTaskDelay(TickType_t t) { (void)t; }
BaseType_t xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *p,unsigned pr,TaskHandle_t *out) { (void)f; (void)n; (void)st; (void)p; (void)pr; *out=(void *)1; return pdPASS; }
unsigned ulTaskNotifyTake(BaseType_t b,TickType_t t) { (void)b; (void)t; return 0; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
bool display_lock(uint32_t ms) { (void)ms; display_held=display_available; return display_held; }
void display_unlock(void) { assert(display_held); display_held=false; }
const char *ui_i18n_get(const char *k,const char *fallback) { (void)k; assert(display_held); return fallback; }
bool app_events_publish(const app_event_t *ev,TickType_t t) { (void)t; if(sent) free(ev->data.notification.notification); return sent; }
static void reset(void)
{
    auto_update_state_init(&s_state); auto_update_state_init(&durable);
    has_state=false; calls=fail_at=installs=0; gate=sent=true;
    s_state_loaded=s_storage_ok=s_enabled=true; s_test_force=true;
}
int main(void)
{
    s_lock=xSemaphoreCreateMutex();
    for(unsigned n=1;n<=3;++n) { reset(); fail_at=n; evaluate(); assert(installs==0); assert(!s_storage_ok); assert(s_decision==AUTO_UPDATE_WAIT_STORAGE); }
    reset(); gate=false; evaluate(); assert(installs==0 && !s_storage_ok);
    reset(); evaluate(); assert(installs==1 && has_state && strcmp(durable.attempt_tag,"v1.0.0")==0);
    /* An unreadable state must preserve the blocked release instead of using defaults. */
    reset(); strcpy(durable.blocked_tag,"v1.0.0"); has_state=true; s_state_loaded=s_storage_ok=false;
    fail_at=1; assert(!resolve_attempt()); assert(installs==0);
    fail_at=0; assert(resolve_attempt()); evaluate(); assert(installs==0 && strcmp(s_state.blocked_tag,"v1.0.0")==0);
    reset(); durable.version=999; has_state=true; s_state_loaded=false;
    assert(state_load()!=ESP_OK && !s_storage_ok && !s_state_loaded);
    reset(); strcpy(s_state.blocked_tag,"v1.0.0"); s_state.notice_pending=1; s_storage_ok=false;
    fail_at=3; assert(!resolve_attempt()); assert(strcmp(s_state.blocked_tag,"v1.0.0")==0);
    fail_at=0; assert(resolve_attempt()); assert(strcmp(durable.blocked_tag,"v1.0.0")==0 && durable.notice_pending);
    sent=false; assert(!show_rollback_notice("v1.0.0")); assert(s_state.notice_pending);
    display_available=false; assert(!show_rollback_notice("v1.0.0") && s_state.notice_pending);
    display_available=true; sent=true; assert(show_rollback_notice("v1.0.0"));
    vSemaphoreDelete(s_lock);
    puts("automatic update worker: failed open/set/commit/gate, unreadable/corrupt state, blocked release and notice retry passed");
    return 0;
}
