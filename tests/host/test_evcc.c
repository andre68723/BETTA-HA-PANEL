/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ha/ha_evcc_lookup.h"
#include "ha/ha_evcc_value.h"
#include "ha/ha_evcc_vehicle_names.h"
#include "layout/evcc_slots.h"
#include "layout/layout_validate.h"
#include "ha/ha_layout_entities.h"
#define CHECK(x) do { ++checks; assert(x); } while (0)
static unsigned checks, id = 100;
static cJSON *request(void) { cJSON *r = ha_evcc_lookup_next_request(++id, 100); CHECK(r); return r; }
static void answer(cJSON *r, const char *text)
{
    cJSON *json = cJSON_Parse(text); CHECK(json);
    CHECK(ha_evcc_lookup_handle_result(id, true, json)); cJSON_Delete(json); cJSON_Delete(r);
}
static const char *str(cJSON *o, const char *key) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key)); }
static void lookup(bool number_first)
{
    ha_evcc_lookup_request(); cJSON *r = request(); CHECK(!strcmp(str(r, "type"), "config_entries/get"));
    answer(r, "[{\"entry_id\":\"demo\",\"title\":\"evcc demo\",\"domain\":\"evcc_intg\"}]");
    r = request(); CHECK(!strcmp(str(r, "type"), "search/related"));
    cJSON *related = cJSON_CreateObject(), *ids = cJSON_AddArrayToObject(related, "entity");
    const char *names[] = {"select.renamed_mode", "sensor.renamed_power", "number.target", "select.plan_target", "sensor.effective", "binary_sensor.disabled"};
    for (int i = 0; i < 6; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(names[i]));
    for (int i = 0; i < 25; i++) { char name[50]; snprintf(name, sizeof(name), "sensor.other_%d", i); cJSON_AddItemToArray(ids, cJSON_CreateString(name)); }
    /* two site power sensors of the instance (evcc_intg.<instance>_<role>, no loadpoint in between) */
    cJSON_AddItemToArray(ids, cJSON_CreateString("sensor.site_pv")); cJSON_AddItemToArray(ids, cJSON_CreateString("sensor.site_grid"));
    if (!number_first) { cJSON *n = cJSON_DetachItemFromArray(ids, 2); cJSON_InsertItemInArray(ids, 3, n); }
    CHECK(ha_evcc_lookup_handle_result(id, true, related)); cJSON_Delete(r); cJSON_Delete(related);
    int batches = 0;
    while (ha_evcc_lookup_is_due(100)) {
        r = request(); batches++; CHECK(!strcmp(str(r, "type"), "config/entity_registry/get_entries"));
        cJSON *entries = cJSON_CreateObject(), *list = cJSON_GetObjectItemCaseSensitive(r, "entity_ids"), *item;
        CHECK(cJSON_GetArraySize(list) <= 24);
        cJSON_ArrayForEach(item, list) {
            const char *key = !strcmp(item->valuestring, names[0]) ? "mode" : !strcmp(item->valuestring, names[1]) ? "charge_power" :
                (!strcmp(item->valuestring, names[2]) || !strcmp(item->valuestring, names[3])) ? "limit_soc" :
                !strcmp(item->valuestring, names[4]) ? "effective_limit_soc" : !strcmp(item->valuestring, names[5]) ? "disabled_in_config" : "not_used";
            cJSON *e = cJSON_AddObjectToObject(entries, item->valuestring);
            cJSON_AddStringToObject(e, "config_entry_id", "demo"); cJSON_AddStringToObject(e, "platform", "evcc_intg");
            char uid[100]; snprintf(uid, sizeof(uid), "evcc_intg.evcc_demo_driveway_%s", key);
            if (!strcmp(item->valuestring, "sensor.site_pv")) strcpy(uid, "evcc_intg.evcc_pv_power");
            if (!strcmp(item->valuestring, "sensor.site_grid")) strcpy(uid, "evcc_intg.evcc_grid_power");
            cJSON_AddStringToObject(e, "unique_id", uid);
            if (!strcmp(key, "disabled_in_config")) cJSON_AddStringToObject(e, "disabled_by", "user");
            else cJSON_AddNullToObject(e, "disabled_by");
        }
        CHECK(ha_evcc_lookup_handle_result(id, true, entries)); cJSON_Delete(entries); cJSON_Delete(r);
    }
    CHECK(batches == 2);
    char *text = ha_evcc_lookup_json(); cJSON *out = cJSON_Parse(text); free(text);
    CHECK(!strcmp(str(out, "status"), "ready")); cJSON *points = cJSON_GetObjectItemCaseSensitive(out, "loadpoints"); CHECK(cJSON_GetArraySize(points) == 1);
    cJSON *bindings = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(points, 0), "entities");
    CHECK(!strcmp(str(bindings, "entity_id"), names[0])); CHECK(!strcmp(str(bindings, "evcc_target_soc_entity_id"), "number.target"));
    CHECK(!strcmp(str(bindings, "evcc_effective_soc_entity_id"), "sensor.effective")); CHECK(!str(bindings, "evcc_disabled_entity_id"));
    CHECK(!strcmp(str(bindings, "evcc_pv_power_entity_id"), "sensor.site_pv")); CHECK(!strcmp(str(bindings, "evcc_grid_power_entity_id"), "sensor.site_grid"));
    CHECK(!str(bindings, "evcc_home_power_entity_id")); cJSON_Delete(out);
}
void test_evcc_sessions(void);
int main(void)
{
    /* Names are display-only and HA's raw IDs remain the service options. */
    ha_evcc_vehicle_names_request(); CHECK(ha_evcc_vehicle_names_is_due(0));
    cJSON *names_cmd=ha_evcc_vehicle_names_next_request(41,0); CHECK(names_cmd);
    CHECK(!strcmp(str(names_cmd,"type"),"frontend/get_translations")); cJSON_Delete(names_cmd);
    cJSON *names=cJSON_Parse("{\"resources\":{\"component.evcc_intg.entity.select.vehiclename.state.null\":\"-none-\",\"component.evcc_intg.entity.select.vehiclename.state.ev1\":\"Demo #1\",\"component.evcc_intg.entity.select.vehiclename.state.ev4\":\"Demo ID.4\"}}");
    char name[APP_MAX_NAME_LEN];
    ha_evcc_vehicle_names_result(40,true,names); CHECK(!ha_evcc_vehicle_name("ev1",name,sizeof(name)));
    ha_evcc_vehicle_names_result(41,true,names); CHECK(ha_evcc_vehicle_name("ev1",name,sizeof(name))); CHECK(!strcmp(name,"Demo #1"));
    CHECK(ha_evcc_vehicle_name("ev4",name,sizeof(name))); CHECK(!strcmp(name,"Demo ID.4")); CHECK(!ha_evcc_vehicle_name("null",name,sizeof(name)));
    CHECK(!ha_evcc_vehicle_names_is_due(299999)); CHECK(ha_evcc_vehicle_names_is_due(300000));
    names_cmd=ha_evcc_vehicle_names_next_request(42,300000); CHECK(names_cmd); cJSON_Delete(names_cmd);
    ha_evcc_vehicle_names_result(42,false,NULL); CHECK(ha_evcc_vehicle_name("ev4",name,sizeof(name)));
    ha_evcc_vehicle_names_cancel(); CHECK(ha_evcc_vehicle_names_is_due(300001));
    names_cmd=ha_evcc_vehicle_names_next_request(43,300001); CHECK(names_cmd); cJSON_Delete(names_cmd);
    ha_evcc_vehicle_names_cancel(); ha_evcc_vehicle_names_result(43,true,names); cJSON_Delete(names);
    float value, lo, hi, step; char stem[96], out[1280]; size_t role;
    CHECK(evcc_registry_site_role("sensor.evcc_home_power", "evcc_intg.evcc_home_power", &role) && role == EVCC_SITE_HOME);
    CHECK(!evcc_registry_site_role("sensor.lp_power", "evcc_intg.evcc_stellplatz_pv_power", &role)); /* a loadpoint stem */
    CHECK(!evcc_registry_site_role("switch.evcc_pv_power", "evcc_intg.evcc_pv_power", &role));
    const char *bad[] = {"", "unknown", "unavailable", "nan", "inf", "1junk", "-1", "100.1"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) CHECK(!evcc_soc(bad[i], &value));
    CHECK(evcc_soc("0", &value) && value == 0); CHECK(evcc_soc("62.187", &value)); CHECK(evcc_soc("100", &value));
    CHECK(evcc_registry_role("sensor.renamed", "evcc_intg.evcc_demo_driveway_effective_limit_soc", stem, sizeof(stem), &role) && role == EVCC_EFFECTIVE);
    CHECK(!evcc_registry_role("number.renamed", "evcc_intg.evcc_demo_driveway_effective_limit_soc", stem, sizeof(stem), &role));
    CHECK(!evcc_registry_role("sensor.power", "other.evcc_demo_charge_power", stem, sizeof(stem), &role));
    CHECK(evcc_number_bounds("{\"min\":20,\"max\":100,\"step\":5,\"unit_of_measurement\":\"%\"}", &lo, &hi, &step));
    CHECK(evcc_target_valid(85,lo,hi,step)); CHECK(!evcc_target_valid(82,lo,hi,step)); CHECK(!evcc_target_valid(15,lo,hi,step));
    CHECK(!evcc_number_bounds("{\"min\":20,\"max\":100,\"step\":0,\"unit_of_measurement\":\"%\"}", &lo,&hi,&step));
    CHECK(!evcc_number_bounds("{\"min\":20,\"max\":100,\"step\":5,\"unit_of_measurement\":\"C\"}", &lo,&hi,&step));
    CHECK(evcc_number_payload("number.target",85,out,sizeof(out))); cJSON *json = cJSON_Parse(out); CHECK(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(json,"value"))); cJSON_Delete(json);
    ha_select_options_t opts; CHECK(evcc_select_parse("{\"options\":[\"off\",\"smart\",\"now\"]}",&opts) && opts.count == 3);
    json = cJSON_CreateObject(); cJSON *list = cJSON_AddArrayToObject(json,"options");
    for (int i = 0; i < 25; i++) cJSON_AddItemToArray(list,cJSON_CreateString("value"));
    char *raw = cJSON_PrintUnformatted(json); CHECK(!evcc_select_parse(raw,&opts) && opts.count == 0); free(raw); cJSON_Delete(json);
    json = cJSON_Parse("{\"options\":[\"null\",\"ev1\",\"ev4\"],\"vehicle\":{\"name\":\"Demo car\"}}");
    char huge[1800]; memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0;
    cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(json,"vehicle"), "details",huge);
    CHECK(ha_evcc_control_attrs(json,false,out,sizeof(out))); CHECK(evcc_select_parse(out,&opts) && opts.count==3); CHECK(strstr(out,"Demo car")); CHECK(!strstr(out,"details")); cJSON_Delete(json);
    const char *contract="{\"version\":1,\"pages\":[{\"id\":\"demo\",\"widgets\":[{\"id\":\"w\",\"type\":\"wallbox_evcc\",\"rect\":{\"x\":0,\"y\":0,\"w\":320,\"h\":360},\"entity_id\":\"select.mode\",\"evcc_vehicle_entity_id\":\"select.car\",\"evcc_target_soc_entity_id\":\"number.target\",\"evcc_power_entity_id\":\"sensor.power\",\"evcc_soc_entity_id\":\"sensor.soc\",\"evcc_connected_entity_id\":\"binary_sensor.connected\",\"evcc_charging_entity_id\":\"binary_sensor.charging\",\"evcc_disabled_entity_id\":\"binary_sensor.disabled\",\"evcc_effective_soc_entity_id\":\"sensor.effective\",\"evcc_energy_entity_id\":\"sensor.energy\",\"evcc_remaining_entity_id\":\"sensor.remaining\"}]}]}";
    layout_validation_result_t validation; bool valid = layout_validate_json(contract,&validation);
    for (unsigned i=0; !valid && i<validation.count; i++) fprintf(stderr,"%s\n",validation.messages[i]);
    CHECK(valid);
    json=cJSON_Parse(contract); char entities[12][APP_MAX_ENTITY_ID_LEN]={{0}};
    ha_layout_entity_set_t set={.ids=&entities[0][0],.capacity=12}; ha_layout_entities_collect(json,&set); CHECK(set.count==11);
    cJSON *widget=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json,"pages"),0),"widgets"),0);
    char primary[APP_MAX_ENTITY_ID_LEN]={0},secondary[APP_MAX_ENTITY_ID_LEN]={0},tertiary[APP_MAX_ENTITY_ID_LEN]={0},extra[8][APP_MAX_ENTITY_ID_LEN]={{0}};
    evcc_slots_from_json(widget,primary,secondary,tertiary,extra,8); CHECK(!strcmp(primary,"select.mode")); CHECK(!strcmp(tertiary,"number.target")); CHECK(!strcmp(extra[5],"sensor.effective")); CHECK(!strcmp(extra[6],"sensor.energy")); CHECK(!strcmp(extra[7],"sensor.remaining"));
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_target_soc_entity_id",cJSON_CreateString("light.invalid")); raw=cJSON_PrintUnformatted(json); CHECK(!layout_validate_json(raw,&validation)); free(raw); cJSON_Delete(json);
    /* The optional header: valid keys pass, a wrong mode, maximum or domain does not; its four site sensors are
     * subscribed and land in the forecast slots; the flow splits evcc's signed readings. */
    json=cJSON_Parse(contract); widget=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json,"pages"),0),"widgets"),0);
    cJSON_AddStringToObject(widget,"evcc_header","flow"); cJSON_AddNumberToObject(widget,"evcc_power_max_kw",22);
    cJSON_AddStringToObject(widget,"evcc_pv_power_entity_id","sensor.pv"); cJSON_AddStringToObject(widget,"evcc_battery_power_entity_id","sensor.battery");
    cJSON_AddStringToObject(widget,"evcc_grid_power_entity_id","sensor.grid"); cJSON_AddStringToObject(widget,"evcc_home_power_entity_id","sensor.home");
    raw=cJSON_PrintUnformatted(json); CHECK(layout_validate_json(raw,&validation)); free(raw);
    char site_ids[16][APP_MAX_ENTITY_ID_LEN]={{0}}; ha_layout_entity_set_t site_set={.ids=&site_ids[0][0],.capacity=16};
    ha_layout_entities_collect(json,&site_set); CHECK(site_set.count==15);
    char mode[16], pv[APP_MAX_ENTITY_ID_LEN], bat[APP_MAX_ENTITY_ID_LEN], grid[APP_MAX_ENTITY_ID_LEN], home[APP_MAX_ENTITY_ID_LEN]; float max_kw=0;
    char *const site[EVCC_SITE_COUNT]={pv,bat,grid,home};
    evcc_header_from_json(widget,mode,sizeof(mode),&max_kw,site); CHECK(!strcmp(mode,"flow")); CHECK(max_kw==22.0f); CHECK(!strcmp(home,"sensor.home"));
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_header",cJSON_CreateString("graph")); raw=cJSON_PrintUnformatted(json); CHECK(!layout_validate_json(raw,&validation)); free(raw);
    evcc_header_from_json(widget,mode,sizeof(mode),&max_kw,site); CHECK(!mode[0]);
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_header",cJSON_CreateString("power"));
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_power_max_kw",cJSON_CreateNumber(80)); raw=cJSON_PrintUnformatted(json); CHECK(!layout_validate_json(raw,&validation)); free(raw);
    evcc_header_from_json(widget,mode,sizeof(mode),&max_kw,site); CHECK(max_kw==EVCC_POWER_MAX_KW_DEFAULT);
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_power_max_kw",cJSON_CreateNumber(11));
    cJSON_ReplaceItemInObjectCaseSensitive(widget,"evcc_grid_power_entity_id",cJSON_CreateString("switch.grid")); raw=cJSON_PrintUnformatted(json); CHECK(!layout_validate_json(raw,&validation)); free(raw);
    cJSON_Delete(json);
    float kw; CHECK(evcc_power_kw("3700","W",&kw) && fabsf(kw-3.7f)<0.001f); CHECK(evcc_power_kw("3.7","kW",&kw) && kw==3.7f); CHECK(!evcc_power_kw("3.7","A",&kw));
    evcc_flow_t flow; evcc_flow_compute(8.4f,-1.0f,2.2f,1.2f,11.0f,&flow);
    CHECK(flow.in[EVCC_FLOW_BATTERY_OUT]==0 && flow.out[EVCC_FLOW_BATTERY_IN]==1.0f && flow.in[EVCC_FLOW_GRID_IN]==2.2f && flow.out[EVCC_FLOW_EXPORT]==0);
    CHECK(fabsf(flow.scale-13.2f)<0.001f);
    evcc_flow_compute(5.0f,0.5f,-3.0f,2.5f,0.0f,&flow); CHECK(flow.out[EVCC_FLOW_EXPORT]==3.0f && fabsf(flow.scale-5.5f)<0.001f);
    lookup(true); lookup(false);
    ha_evcc_lookup_request(); cJSON *r=request(); uint32_t stale=id;
    ha_evcc_lookup_request(); cJSON *newr=request(); CHECK(!ha_evcc_lookup_handle_result(stale,true,NULL));
    ha_evcc_lookup_cancel(); CHECK(!ha_evcc_lookup_handle_result(id,true,NULL)); cJSON_Delete(r); cJSON_Delete(newr);
    ha_evcc_lookup_request(); r=request(); CHECK(!ha_evcc_lookup_is_due(15100)); raw=ha_evcc_lookup_json(); CHECK(strstr(raw,"error")); free(raw); cJSON_Delete(r);
    test_evcc_sessions();
    printf("evcc: %u checks passed\n", checks); return 0;
}
