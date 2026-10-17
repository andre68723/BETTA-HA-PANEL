/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api/ota_transfer.h"
#include "esp_http_server.h"
static int64_t now_ms;
static unsigned end_calls, abort_calls, selected, receive_calls;
static int mode;
static bool gate_available, held, checksum=true;
bool flash_gate_enter(uint32_t ms) { assert(ms==15000); held=gate_available; return held; }
void flash_gate_leave(void) { assert(held); held=false; }
esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; assert(held); ++end_calls; return ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; ++abort_calls; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { (void)p; ++selected; return ESP_OK; }
bool update_check_sha256_matches(const char *expected,const uint8_t *hash) { (void)expected; (void)hash; return checksum; }
int64_t esp_timer_get_time(void) { return now_ms*1000; }
int httpd_req_recv(httpd_req_t *req,char *out,size_t capacity)
{
    (void)req; ++receive_calls;
    if(mode==0) { now_ms+=10000; return HTTPD_SOCK_ERR_TIMEOUT; }
    if(mode==1) { now_ms+=10000; out[0]='x'; return 1; } /* active forever, bounded by total deadline */
    if(mode==2) { memcpy(out,"abc",capacity); return (int)capacity; }
    return 0;
}
int main(void)
{
    api_ota_stream_t s={.begun=true,.header_checked=true,.total=10,.written=10};
    assert(ota_stream_finish(&s)==ESP_ERR_TIMEOUT); assert(s.begun && end_calls==0 && selected==0);
    ota_stream_abort(&s); assert(!s.begun && abort_calls==1);
    gate_available=true; s.begun=true;
    assert(ota_stream_finish(&s)==ESP_OK); assert(!s.begun && end_calls==1 && selected==1 && !held);
    s.begun=true; s.written=9; assert(ota_stream_finish(&s)==ESP_ERR_INVALID_SIZE); assert(end_calls==1); ota_stream_abort(&s);
    s.begun=true; s.written=10; s.verify=true; sha256_soft_init(&s.sha); checksum=false;
    assert(ota_stream_finish(&s)==ESP_ERR_INVALID_CRC); assert(selected==1 && !s.begun && !s.verify);
    httpd_req_t req={.content_len=100};
    assert(ota_read_request_body(&req,99)==NULL && receive_calls==0);
    assert(ota_read_request_body(&req,100)==NULL && receive_calls==3 && now_ms==30000);
    mode=1; now_ms=0; receive_calls=0;
    assert(ota_read_request_body(&req,100)==NULL && receive_calls==18 && now_ms==180000);
    mode=2; now_ms=0; req.content_len=3;
    char *body=ota_read_request_body(&req,100); assert(body && strcmp(body,"abc")==0); free(body);
    mode=3; assert(ota_read_request_body(&req,100)==NULL);
    puts("OTA transfer: gate failure/abort, validation, checksum, silent and slow requests passed");
    return 0;
}
