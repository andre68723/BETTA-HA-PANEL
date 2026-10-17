/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_events.h"
#include "util/flash_gate.h"
#include "util/atomic_file.h"
#include "freertos/semphr.h"
static bool create_fail = true, queue_full = true;
static int mutex;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return create_fail ? NULL : &mutex; }
void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t t) { (void)t; if (*m) return pdFALSE; *m=1; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { *m=0; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned n, unsigned size) { (void)n; (void)size; return &mutex; }
BaseType_t xQueueSend(QueueHandle_t q, const void *ev, TickType_t t) { (void)q; (void)ev; (void)t; return queue_full ? pdFALSE : pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t q, void *ev, TickType_t t) { (void)q; (void)ev; (void)t; return pdFALSE; }
int main(void)
{
    assert(!flash_gate_enter(0));
    char path[] = "/tmp/betta-config-events-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0); FILE *f=fdopen(fd,"wb"); assert(f); assert(fwrite("old",1,3,f)==3); fclose(f);
    assert(atomic_file_write(path,"new",3)==ESP_ERR_TIMEOUT);
    create_fail=false; assert(flash_gate_enter(0)); assert(!flash_gate_enter(0));
    assert(atomic_file_write(path,"new",3)==ESP_ERR_TIMEOUT);
    char text[4]={0}; f=fopen(path,"rb"); assert(f); assert(fread(text,1,3,f)==3); fclose(f); assert(strcmp(text,"old")==0);
    flash_gate_leave(); assert(atomic_file_write(path,"new",3)==ESP_OK); remove(path);
    app_event_t layout={.type=EV_LAYOUT_UPDATED}, state={.type=EV_HA_CONNECTED};
    uint32_t initial=app_events_layout_revision();
    assert(app_events_publish(&layout,0)); assert(app_events_layout_revision()==initial+1);
    assert(!app_events_publish(&state,0));
    assert(app_events_init()==ESP_OK);
    for(unsigned n=0;n<1000;++n) assert(app_events_publish(&layout,0));
    assert(app_events_layout_revision()==initial+1001);
    assert(!app_events_publish(&state,0));
    queue_full=false; assert(app_events_publish(&state,0));
    assert(app_events_layout_revision()==initial+1001);
    puts("configuration events: queue saturation and pre-init revisions, flash allocation/lock failures and preserved files passed");
    return 0;
}
