/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_evcc_sessions.h"
#include "layout/evcc_slots.h"
#include "util/iso_time.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define LOCK() taskENTER_CRITICAL(&s_lock)
#define UNLOCK() taskEXIT_CRITICAL(&s_lock)
#define ALLOC(n, size) heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#ifdef HA_EVCC_SESSIONS_TEST_HOOKS
void ha_evcc_sessions_test_lock(void);
void ha_evcc_sessions_test_unlock(void);
#define LOCK() ha_evcc_sessions_test_lock()
#define UNLOCK() ha_evcc_sessions_test_unlock()
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
#define ALLOC(n, size) calloc(n, size)
#endif
#define CACHE_COUNT 8
/* Only small state copies are protected. JSON traversal and allocation stay outside
 * the critical section; time() takes a libc mutex and must also run outside it.
 * No UI lock or callback object is retained by the worker. */
typedef struct {
    char entity[APP_MAX_ENTITY_ID_LEN];
    evcc_sessions_t data;
    bool requested;
    time_t updated;
} cache_t;
static cache_t *s_cache;
static struct { int slot, phase, year, month, scanned; uint32_t token; char entry[40], stem[96], loadpoint[APP_MAX_NAME_LEN]; } s_work;
static bool s_busy;
static const char *string(const cJSON *o, const char *key) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key)); }
static bool copy(char *out, size_t capacity, const char *value) {
    if (!value || !*value || strlen(value) >= capacity) return false;
    strcpy(out,value); return true;
}
static bool ensure_cache(void)
{
    LOCK(); bool present = s_cache != NULL; UNLOCK();
    if (present) return true;
    cache_t *fresh = ALLOC(CACHE_COUNT, sizeof(*fresh)); if (!fresh) return false;
    LOCK(); if (!s_cache) { s_cache = fresh; fresh = NULL; } UNLOCK(); free(fresh); return true;
}
static int find(const char *id, bool create)
{
    int empty = -1;
    for (int i=0;i<CACHE_COUNT;i++) {
        if (!strcmp(s_cache[i].entity,id)) return i;
        if (!s_cache[i].entity[0] && empty < 0) empty = i;
    }
    if (create && empty >= 0) strcpy(s_cache[empty].entity,id);
    return create ? empty : -1;
}
void ha_evcc_sessions_request(const char *id, bool force)
{
    if (!id || strncmp(id,"select.",7) || strlen(id)>=APP_MAX_ENTITY_ID_LEN || !ensure_cache()) return;
    const time_t now=time(NULL);
    LOCK(); int slot=find(id,true);
    if (slot>=0 && s_cache[slot].data.status != EVCC_SESSIONS_LOADING &&
        (force || !s_cache[slot].updated || now-s_cache[slot].updated>=300)) {
        s_cache[slot].requested=true;
        s_cache[slot].data.status=EVCC_SESSIONS_LOADING; s_cache[slot].data.version++;
    } UNLOCK();
}
void ha_evcc_sessions_snapshot(const char *id, evcc_sessions_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out)); out->status=EVCC_SESSIONS_IDLE;
    if (!id) return;
    LOCK(); int slot=s_cache?find(id,false):-1; if(slot>=0) *out=s_cache[slot].data; UNLOCK();
}
void ha_evcc_sessions_latest(const char *id, evcc_session_t *out, evcc_sessions_status_t *status, bool *exists)
{
    *status=EVCC_SESSIONS_IDLE; *exists=false; memset(out,0,sizeof(*out));
    LOCK(); int slot=s_cache && id?find(id,false):-1;
    if(slot>=0) { *status=s_cache[slot].data.status; *exists=*status==EVCC_SESSIONS_READY && s_cache[slot].data.count>0; if(*exists) *out=s_cache[slot].data.rows[0]; } UNLOCK();
}
void ha_evcc_sessions_reduce(evcc_sessions_t *out, const cJSON *rows, const char *loadpoint)
{
    const cJSON *row;
    cJSON_ArrayForEach(row,rows) {
        const char *lp=string(row,"loadpoint"), *vehicle=string(row,"vehicle");
        int64_t start,end;
        const cJSON *energy=cJSON_GetObjectItemCaseSensitive(row,"chargedEnergy");
        if (!lp || strcmp(lp,loadpoint) || !vehicle || strlen(vehicle)>=APP_MAX_NAME_LEN ||
            !iso_time_parse(string(row,"created"),&start) || !iso_time_parse(string(row,"finished"),&end) ||
            start<=0 || end<start || !cJSON_IsNumber(energy) || !isfinite(energy->valuedouble) || energy->valuedouble<0) continue;
        unsigned at=0; bool duplicate=false;
        for(unsigned i=0;i<out->count;i++) if(out->rows[i].start==start && out->rows[i].energy==energy->valuedouble && !strcmp(out->rows[i].vehicle,vehicle)) duplicate=true;
        if(duplicate) continue;
        while(at<out->count && out->rows[at].start>=start) at++;
        if(at>=HA_EVCC_SESSION_LIMIT) continue;
        unsigned count=out->count<HA_EVCC_SESSION_LIMIT?out->count+1:out->count;
        memmove(out->rows+at+1,out->rows+at,(count-at-1)*sizeof(out->rows[0]));
        out->rows[at]=(evcc_session_t){.start=start,.energy=energy->valuedouble};
        strcpy(out->rows[at].vehicle,vehicle); out->count=count;
    }
}
static void finish(evcc_sessions_status_t status, time_t now)
{
    s_cache[s_work.slot].data.status=status; s_cache[s_work.slot].data.version++;
    s_cache[s_work.slot].updated=now; s_busy=false; s_work.token=0;
}
bool ha_evcc_sessions_is_due(void)
{
    LOCK(); bool due=s_busy && !s_work.token;
    if(!s_busy && s_cache) for(int i=0;i<CACHE_COUNT;i++) if(s_cache[i].requested) { due=true; break; }
    UNLOCK(); return due;
}
cJSON *ha_evcc_sessions_next_request(uint32_t token)
{
    if(!token || !ha_evcc_sessions_is_due()) return NULL;
    time_t now=time(NULL); struct tm date; gmtime_r(&now,&date);
    LOCK();
    if(!s_busy) for(int i=0;i<CACHE_COUNT;i++) if(s_cache[i].requested) {
        memset(&s_work,0,sizeof(s_work)); s_work.slot=i; s_busy=true; s_cache[i].requested=false;
        s_cache[i].data.count=0; s_work.phase=0;
        s_work.year=date.tm_year+1900; s_work.month=date.tm_mon+1;
        break;
    }
    if(!s_busy || s_work.token) { UNLOCK(); return NULL; }
    s_work.token=token;
    const int phase=s_work.phase, year=s_work.year, month=s_work.month;
    char entity[APP_MAX_ENTITY_ID_LEN],entry[40]; strcpy(entity,s_cache[s_work.slot].entity); strcpy(entry,s_work.entry);
    UNLOCK();
    cJSON *cmd=cJSON_CreateObject();
    if(!cmd) { ha_evcc_sessions_send_failed(false); return NULL; }
    cJSON_AddStringToObject(cmd,"type",phase==0?"config/entity_registry/get":phase==1?"evcc_intg/capabilities":"evcc_intg/sessions");
    if(phase==0) cJSON_AddStringToObject(cmd,"entity_id",entity);
    else cJSON_AddStringToObject(cmd,"entry_id",entry);
    if(phase==2) { cJSON_AddNumberToObject(cmd,"year",year); cJSON_AddNumberToObject(cmd,"month",month); }
    return cmd;
}
void ha_evcc_sessions_result(uint32_t token, bool success, const cJSON *result)
{
    LOCK(); bool valid=s_busy && token && token==s_work.token; int phase=s_work.phase; UNLOCK();
    if(!valid) return;
    const time_t now=time(NULL);
    if(!success || !cJSON_IsObject(result)) { LOCK(); if(token==s_work.token) finish(phase==1?EVCC_SESSIONS_UNSUPPORTED:EVCC_SESSIONS_ERROR,now); UNLOCK(); return; }
    char entry[40]="",stem[96]="",loadpoint[APP_MAX_NAME_LEN]="";
    evcc_sessions_t *reduced=NULL; bool good=false;
    if(phase==0) {
        size_t role;
        good=copy(entry,sizeof(entry),string(result,"config_entry_id")) &&
            evcc_registry_role(s_cache[s_work.slot].entity,string(result,"unique_id"),stem,sizeof(stem),&role) && role==EVCC_MODE &&
            !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(result,"disabled_by"));
    } else if(phase==1) {
        const cJSON *v; bool supported=false; size_t longest=0; unsigned matches=0;
        cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(result,"commands")) if(cJSON_IsString(v) && !strcmp(v->valuestring,"sessions")) supported=true;
        cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(result,"loadpoints")) {
            const char *id=string(v,"id"),*name=string(v,"name"); size_t n=id?strlen(id):0, len=strlen(s_work.stem);
            if(n && len>n && s_work.stem[len-n-1]=='_' && !strcmp(s_work.stem+len-n,id) && n>=longest) {
                if(n>longest) matches=0;
                longest=n; matches++; if(!copy(loadpoint,sizeof(loadpoint),name)) matches+=2;
            }
        }
        unsigned names=0;
        cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(result,"loadpoints")) { const char *name=string(v,"name"); if(name && !strcmp(name,loadpoint)) names++; }
        good=supported && matches==1 && names==1;
    } else {
        const cJSON *rows=cJSON_GetObjectItemCaseSensitive(result,"sessions");
        reduced=ALLOC(1,sizeof(*reduced)); good=cJSON_IsArray(rows) && reduced;
        if(good) { LOCK(); *reduced=s_cache[s_work.slot].data; UNLOCK(); ha_evcc_sessions_reduce(reduced,rows,s_work.loadpoint); }
    }
    LOCK();
    if(s_busy && token==s_work.token) {
        if(!good) finish(phase<2?EVCC_SESSIONS_UNSUPPORTED:EVCC_SESSIONS_ERROR,now);
        else if(phase==0) { strcpy(s_work.entry,entry); strcpy(s_work.stem,stem); s_work.phase=1; s_work.token=0; }
        else if(phase==1) { strcpy(s_work.loadpoint,loadpoint); s_work.phase=2; s_work.token=0; }
        else {
            s_cache[s_work.slot].data=*reduced; s_cache[s_work.slot].data.version++;
            if(reduced->count>=HA_EVCC_SESSION_LIMIT || ++s_work.scanned>=HA_EVCC_SESSION_MONTHS) finish(EVCC_SESSIONS_READY,now);
            else { if(--s_work.month<1) { s_work.month=12; s_work.year--; } s_work.token=0; }
        }
    } UNLOCK(); free(reduced);
}
void ha_evcc_sessions_send_failed(bool retry)
{
    const time_t now=time(NULL);
    LOCK(); if(s_busy) { if(retry) s_work.token=0; else finish(EVCC_SESSIONS_ERROR,now); } UNLOCK();
}
void ha_evcc_sessions_cancel(void)
{
    const time_t now=time(NULL);
    LOCK(); if(s_busy) finish(EVCC_SESSIONS_ERROR,now);
    if(s_cache) for(int i=0;i<CACHE_COUNT;i++) if(s_cache[i].requested) { s_cache[i].requested=false; s_cache[i].data.status=EVCC_SESSIONS_ERROR; s_cache[i].data.version++; s_cache[i].updated=now; }
    UNLOCK();
}
#ifdef BETTA_SIMULATOR
void ha_evcc_sessions_seed(const char *id, const char *loadpoint, const cJSON *rows)
{
    if(!id || strlen(id)>=APP_MAX_ENTITY_ID_LEN || !ensure_cache()) return;
    evcc_sessions_t *data=ALLOC(1,sizeof(*data)); if(!data) return;
    ha_evcc_sessions_reduce(data,rows,loadpoint); data->status=EVCC_SESSIONS_READY;
    const time_t now=time(NULL);
    LOCK(); int slot=find(id,true); if(slot>=0) { data->version=s_cache[slot].data.version+1; s_cache[slot].data=*data; s_cache[slot].updated=now; s_cache[slot].requested=false; } UNLOCK(); free(data);
}
#endif
