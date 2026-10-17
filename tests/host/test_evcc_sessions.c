/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_evcc_sessions.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static unsigned checks, token=1000;
#define CHECK(x) do { ++checks; assert(x); } while(0)
#ifdef HA_EVCC_SESSIONS_TEST_HOOKS
static unsigned lock_depth, clock_reads;
static time_t clock_now=1791840000;
void ha_evcc_sessions_test_lock(void) { CHECK(lock_depth==0); lock_depth++; }
void ha_evcc_sessions_test_unlock(void) { CHECK(lock_depth==1); lock_depth--; }
time_t ha_evcc_sessions_test_time(time_t *out) {
    CHECK(lock_depth==0); clock_reads++;
    if(out) *out=clock_now;
    return clock_now;
}
#endif
static const char *str(const cJSON *o,const char *key) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,key)); }
static cJSON *request(const char *type) { cJSON *r=ha_evcc_sessions_next_request(++token); CHECK(r); CHECK(!strcmp(str(r,"type"),type)); return r; }
static void reply(cJSON *r,const char *text) { cJSON *j=cJSON_Parse(text); CHECK(j); ha_evcc_sessions_result(token,true,j); cJSON_Delete(j); cJSON_Delete(r); }
static void resolve(const char *entity) {
    ha_evcc_sessions_request(entity,true);
    cJSON *r=request("config/entity_registry/get"); CHECK(!strcmp(str(r,"entity_id"),entity));
    reply(r,"{\"config_entry_id\":\"instance_a\",\"unique_id\":\"evcc_intg.evcc_demo_driveway_mode\",\"disabled_by\":null}");
    r=request("evcc_intg/capabilities"); CHECK(!strcmp(str(r,"entry_id"),"instance_a"));
    reply(r,"{\"commands\":[\"sessions\"],\"loadpoints\":[{\"id\":\"driveway\",\"name\":\"Driveway\"},{\"id\":\"way\",\"name\":\"Other\"}]}");
}
void test_evcc_sessions(void)
{
    evcc_sessions_t data={0};
    const char *records="[{\"created\":\"2026-10-10T11:54:00+02:00\",\"finished\":\"2026-10-10T15:00:00+02:00\",\"loadpoint\":\"Driveway\",\"vehicle\":\"id4\",\"chargedEnergy\":20.2},{\"created\":\"2026-10-07T14:48:00Z\",\"finished\":\"2026-10-07T16:00:00Z\",\"loadpoint\":\"Driveway\",\"vehicle\":\"e-Golf\",\"chargedEnergy\":7.1}]";
    cJSON *rows=cJSON_Parse(records); ha_evcc_sessions_reduce(&data,rows,"Driveway"); CHECK(data.count==2); CHECK(data.rows[0].energy==20.2); CHECK(!strcmp(data.rows[0].vehicle,"id4")); CHECK(data.rows[0].start>data.rows[1].start);
    ha_evcc_sessions_reduce(&data,rows,"Driveway"); CHECK(data.count==2); cJSON_Delete(rows);
    rows=cJSON_Parse("[{\"created\":\"2026-10-11T11:00:00Z\",\"finished\":null,\"loadpoint\":\"Driveway\",\"vehicle\":\"Active\",\"chargedEnergy\":3},{\"created\":\"2026-10-11T11:00:00Z\",\"finished\":\"2026-10-11T12:00:00Z\",\"loadpoint\":\"Other\",\"vehicle\":\"Wrong charger\",\"chargedEnergy\":3},{\"created\":\"2026-10-11T11:00:00Z\",\"finished\":\"2026-10-10T12:00:00Z\",\"loadpoint\":\"Driveway\",\"vehicle\":\"Invalid\",\"chargedEnergy\":-3}]");
    ha_evcc_sessions_reduce(&data,rows,"Driveway"); CHECK(data.count==2); cJSON_Delete(rows);
    resolve("select.renamed");
    cJSON *r=request("evcc_intg/sessions"); CHECK(!strcmp(str(r,"entry_id"),"instance_a"));
    int previous=cJSON_GetObjectItemCaseSensitive(r,"year")->valueint*12+cJSON_GetObjectItemCaseSensitive(r,"month")->valueint;
    cJSON *result=cJSON_CreateObject(); rows=cJSON_Parse(records); cJSON_AddItemToObject(result,"sessions",rows);
    ha_evcc_sessions_result(token,true,result); cJSON_Delete(result); cJSON_Delete(r);
    for(int i=1;i<HA_EVCC_SESSION_MONTHS;i++) {
        r=request("evcc_intg/sessions");
        int current=cJSON_GetObjectItemCaseSensitive(r,"year")->valueint*12+cJSON_GetObjectItemCaseSensitive(r,"month")->valueint;
        CHECK(current==previous-1); previous=current; reply(r,"{\"sessions\":[]}");
    }
    CHECK(!ha_evcc_sessions_is_due()); ha_evcc_sessions_snapshot("select.renamed",&data); CHECK(data.status==EVCC_SESSIONS_READY && data.count==2);
    ha_evcc_sessions_request("select.renamed",false); CHECK(!ha_evcc_sessions_is_due());
#ifdef HA_EVCC_SESSIONS_TEST_HOOKS
    clock_now+=299; ha_evcc_sessions_request("select.renamed",false); CHECK(!ha_evcc_sessions_is_due());
    clock_now++; ha_evcc_sessions_request("select.renamed",false); CHECK(ha_evcc_sessions_is_due());
    ha_evcc_sessions_cancel();
#endif
    ha_evcc_sessions_request("select.renamed",true); r=request("config/entity_registry/get"); unsigned stale=token;
    ha_evcc_sessions_cancel(); ha_evcc_sessions_result(stale,true,NULL); cJSON_Delete(r);
    ha_evcc_sessions_snapshot("select.renamed",&data); CHECK(data.status==EVCC_SESSIONS_ERROR);
    ha_evcc_sessions_request("select.wrong",true); r=request("config/entity_registry/get");
    reply(r,"{\"config_entry_id\":\"instance_b\",\"unique_id\":\"other.demo_driveway_mode\"}");
    ha_evcc_sessions_snapshot("select.wrong",&data); CHECK(data.status==EVCC_SESSIONS_UNSUPPORTED);
    ha_evcc_sessions_request("select.duplicate",true); r=request("config/entity_registry/get");
    reply(r,"{\"config_entry_id\":\"a\",\"unique_id\":\"evcc_intg.evcc_demo_driveway_mode\"}");
    r=request("evcc_intg/capabilities"); reply(r,"{\"commands\":[\"sessions\"],\"loadpoints\":[{\"id\":\"driveway\",\"name\":\"Same\"},{\"id\":\"other\",\"name\":\"Same\"}]}");
    ha_evcc_sessions_snapshot("select.duplicate",&data); CHECK(data.status==EVCC_SESSIONS_UNSUPPORTED);
    resolve("select.retry"); r=request("evcc_intg/sessions"); unsigned old=token; cJSON_Delete(r);
    ha_evcc_sessions_send_failed(true); r=request("evcc_intg/sessions"); CHECK(token!=old);
    ha_evcc_sessions_result(old,true,NULL); CHECK(!ha_evcc_sessions_is_due());
    ha_evcc_sessions_result(token,false,NULL); cJSON_Delete(r); ha_evcc_sessions_snapshot("select.retry",&data); CHECK(data.status==EVCC_SESSIONS_ERROR);
    resolve("select.failed"); r=request("evcc_intg/sessions"); cJSON_Delete(r);
    ha_evcc_sessions_send_failed(false); ha_evcc_sessions_snapshot("select.failed",&data); CHECK(data.status==EVCC_SESSIONS_ERROR);
    ha_evcc_sessions_request("select.queued",true); ha_evcc_sessions_cancel();
    ha_evcc_sessions_snapshot("select.queued",&data); CHECK(data.status==EVCC_SESSIONS_ERROR && !ha_evcc_sessions_is_due());
    data=(evcc_sessions_t){0}; rows=cJSON_CreateArray();
    for(int i=1;i<=25;i++) { cJSON *row=cJSON_CreateObject(); char date[40]; snprintf(date,sizeof(date),"2026-09-%02dT11:00:00Z",i); cJSON_AddStringToObject(row,"created",date); snprintf(date,sizeof(date),"2026-09-%02dT12:00:00Z",i); cJSON_AddStringToObject(row,"finished",date); cJSON_AddStringToObject(row,"loadpoint","Driveway"); cJSON_AddStringToObject(row,"vehicle","car"); cJSON_AddNumberToObject(row,"chargedEnergy",i); cJSON_AddItemToArray(rows,row); }
    ha_evcc_sessions_reduce(&data,rows,"Driveway"); CHECK(data.count==20 && data.rows[0].energy==25 && data.rows[19].energy==6); cJSON_Delete(rows);
#ifdef HA_EVCC_SESSIONS_TEST_HOOKS
    CHECK(lock_depth==0 && clock_reads>0);
#endif
    printf("evcc sessions: %u checks passed\n",checks);
}
