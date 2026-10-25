/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "popup_contract_regression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "layout/evcc_slots.h"
#include "ha/ha_evcc_sessions.h"
#include "ha/ha_evcc_vehicle_names.h"
#include "ui/ui_evcc_sessions_view.h"
#include "ui/ui_widget_factory.h"
#include "ui/ui_i18n.h"
#include "ui/theme/ui_style.h"
esp_err_t w_wallbox_evcc_create(const ui_widget_def_t *,lv_obj_t *,ui_widget_instance_t *);
void w_wallbox_evcc_apply_state(ui_widget_instance_t *,const ha_state_t *);
void w_wallbox_evcc_set_visible(ui_widget_instance_t *,bool);
static unsigned checks;
static unsigned draws;
static uint32_t now;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"wallbox line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint32_t tick(void) { return now; }
static void advance(unsigned ms) { now += ms; lv_timer_handler(); lv_timer_handler();  sim_popup_contract_check(); }
static void drawn(lv_event_t *event) { (void)event; draws++; }
static lv_obj_t *find(lv_obj_t *root, const char *text, const lv_obj_class_t *kind)
{
    if (kind && lv_obj_check_type(root,kind)) return root;
    if (text && lv_obj_check_type(root,&lv_label_class)) {
        const char *shown = lv_label_get_text(root); size_t n = strlen(shown);
        if (!strcmp(shown,text) || (n>3 && !strcmp(shown+n-3,"...") && !strncmp(shown,text,n-3))) return root;
    }
    for (uint32_t i=0;i<lv_obj_get_child_count(root);i++) { lv_obj_t *found=find(lv_obj_get_child(root,i),text,kind); if(found) return found; }
    return NULL;
}
static void click(lv_obj_t *root,const char *text)
{
    lv_obj_t *obj=find(root,text,NULL); if (!obj) fprintf(stderr,"missing label %s\n",text); CHECK(obj); lv_obj_t *parent=lv_obj_get_parent(obj);
    CHECK(lv_obj_check_type(parent,&lv_button_class)); lv_obj_send_event(parent,LV_EVENT_CLICKED,NULL); advance(100);
}
static void apply(ui_widget_instance_t *widget,const char *id,const char *raw,const char *attrs)
{
    ha_state_t state={0}; snprintf(state.entity_id,sizeof(state.entity_id),"%s",id); snprintf(state.state,sizeof(state.state),"%s",raw);
    snprintf(state.attributes_json,sizeof(state.attributes_json),"%s",attrs); sim_host_state_add(&state); w_wallbox_evcc_apply_state(widget,&state); lv_obj_update_layout(widget->obj);
}
static void sent(const char *domain,const char *service,const char *entity,const char *option,int number)
{
    const sim_service_calls_t *calls=sim_host_service_calls(); CHECK(calls->count==1); CHECK(!strcmp(calls->domain,domain)); CHECK(!strcmp(calls->service,service));
    cJSON *data=cJSON_Parse(calls->data); CHECK(data);
    CHECK(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data,"entity_id")),entity));
    cJSON *value=cJSON_GetObjectItemCaseSensitive(data,option?"option":"value");
    CHECK(option ? cJSON_IsString(value) && !strcmp(value->valuestring,option) : cJSON_IsNumber(value) && value->valueint==number);
    cJSON_Delete(data);
}
int sim_wallbox_regression(void)
{
    now=lv_tick_get(); lv_tick_set_cb(tick); sim_host_set_online(true);
    ui_widget_def_t def={.w=320,.h=360}; strcpy(def.entity_id,"select.mode"); strcpy(def.secondary_entity_id,"select.car"); strcpy(def.tertiary_entity_id,"number.target");
    strcpy(def.type,"wallbox_evcc");
    strcpy(def.extra_entity_id[0],"sensor.power"); strcpy(def.extra_entity_id[1],"sensor.soc"); strcpy(def.extra_entity_id[2],"binary_sensor.connected"); strcpy(def.extra_entity_id[3],"binary_sensor.charging"); strcpy(def.extra_entity_id[6],"sensor.energy"); strcpy(def.extra_entity_id[7],"sensor.remaining");
    ui_widget_instance_t widget={0}; CHECK(ui_widget_factory_create(&def,lv_screen_active(),&widget)==ESP_OK);
    const char *modes="{\"options\":[\"off\",\"smart\",\"now\"]}";
    const char *cars="{\"options\":[\"null\",\"ev1\",\"A\\\"B\\\\C\"],\"vehicle\":{\"name\":\"Demo car\"}}";
    const char *bounds="{\"min\":20,\"max\":100,\"step\":5,\"unit_of_measurement\":\"%\"}";
    apply(&widget,"select.mode","smart",modes); apply(&widget,"select.car","ev1",cars); apply(&widget,"number.target","100",bounds);
    apply(&widget,"sensor.power","7400","{\"unit_of_measurement\":\"W\"}"); apply(&widget,"binary_sensor.connected","on","{}"); apply(&widget,"binary_sensor.charging","on","{}");
    apply(&widget,"sensor.energy","12800","{\"unit_of_measurement\":\"Wh\"}"); apply(&widget,"sensor.remaining","13980","{\"unit_of_measurement\":\"s\"}");
    CHECK(find(widget.obj,"12.8 kWh",NULL)); CHECK(find(widget.obj,"3:53 h",NULL));
    lv_obj_t *car_button = lv_obj_get_parent(find(widget.obj,"Demo car",NULL)); CHECK(lv_obj_get_width(car_button)==def.w-24); CHECK(lv_obj_get_x(car_button)==12); /* as wide as the status line, name left-aligned */
    CHECK(find(widget.obj,"7.4 kW",NULL)); CHECK(find(widget.obj,"Charging",NULL));
    /* Idle timer ticks must not invalidate the card. Prove the draw probe works
     * first, then leave the confirmed state unchanged across four timer ticks. */
    lv_obj_add_event_cb(widget.obj,drawn,LV_EVENT_DRAW_MAIN,NULL);
    lv_obj_invalidate(widget.obj); advance(300); CHECK(draws>0);
    advance(300); draws=0;
    for (int i=0;i<4;i++) advance(300);
    CHECK(draws==0);
    /* Cached hidden pages keep state events, without replaying all bindings on show. */
    ui_widget_factory_set_visible(&widget,false); lv_obj_set_hidden(widget.obj,true);
    apply(&widget,"sensor.power","1700","{\"unit_of_measurement\":\"W\"}");
    ui_widget_factory_set_visible(&widget,true); lv_obj_set_hidden(widget.obj,false);
    CHECK(find(widget.obj,"1.7 kW",NULL));
    apply(&widget,"sensor.power","7400","{\"unit_of_measurement\":\"W\"}");
    sim_host_service_reset(ESP_OK); click(widget.obj,"Off"); sent("select","select_option","select.mode","off",0); CHECK(find(widget.obj,"Waiting...",NULL));
    /* A second tap while pending cannot issue another command. HA mode confirmation
     * leaves actual Charging visible until the charging sensor changes. */
    click(widget.obj,"Fast"); CHECK(sim_host_service_calls()->count==1);
    apply(&widget,"select.mode","off",modes); CHECK(find(widget.obj,"Charging",NULL));
    lv_color_t off_bg = lv_obj_get_style_bg_color(lv_obj_get_parent(find(widget.obj,"Off",NULL)), LV_PART_MAIN);
    lv_color_t smart_bg = lv_obj_get_style_bg_color(lv_obj_get_parent(find(widget.obj,"Smart",NULL)), LV_PART_MAIN);
    CHECK(off_bg.red != smart_bg.red || off_bg.green != smart_bg.green || off_bg.blue != smart_bg.blue);
    sim_host_service_reset(ESP_OK); click(widget.obj,"Smart"); sent("select","select_option","select.mode","smart",0); apply(&widget,"select.mode","smart",modes);
    sim_host_service_reset(ESP_OK); click(widget.obj,"Fast"); sent("select","select_option","select.mode","now",0); advance(10100); CHECK(find(widget.obj,"Not confirmed",NULL));
    advance(6100); CHECK(!find(widget.obj,"Not confirmed",NULL)); CHECK(find(widget.obj,"Charging",NULL));
    apply(&widget,"select.mode","now",modes);
    sim_host_service_reset(ESP_FAIL); click(widget.obj,"Off"); CHECK(find(widget.obj,"Not confirmed",NULL)); CHECK(!find(widget.obj,"Waiting...",NULL));
    /* Vehicle selection remains enabled with unknown SOC and preserves escaping. */
    sim_host_service_reset(ESP_OK); click(widget.obj,"Demo car");
    /* HA refreshes the same selection repeatedly. Keep the popup and its rows
     * usable across those updates, including unrelated attribute changes. */
    lv_obj_t *picker = lv_obj_get_child(lv_layer_top(),0); CHECK(picker);
    for (int i=0;i<4;i++) {
        apply(&widget,"select.car","ev1",cars); advance(300);
        CHECK(lv_obj_get_child_count(lv_layer_top())==1); CHECK(lv_obj_get_child(lv_layer_top(),0)==picker);
    }
    apply(&widget,"select.car","ev1","{\"friendly_name\":\"Updated name\",\"options\":[\"null\",\"ev1\",\"A\\\"B\\\\C\"],\"vehicle\":{\"name\":\"Demo car\"}}");
    CHECK(lv_obj_get_child(lv_layer_top(),0)==picker); CHECK(sim_host_service_calls()->count==0);
    click(lv_layer_top(),"A\"B\\C"); sent("select","select_option","select.car","A\"B\\C",0);
    apply(&widget,"select.car","A\"B\\C",cars); CHECK(find(widget.obj,"-- %",NULL));
    click(widget.obj,"Demo car"); sim_host_service_reset(ESP_OK); click(lv_layer_top(),"Guest vehicle"); sent("select","select_option","select.car","null",0); apply(&widget,"select.car","null",cars);
    CHECK(find(widget.obj,"Guest vehicle",NULL)); CHECK(!find(widget.obj,"No vehicle",NULL));
    CHECK(ui_i18n_init("de")==ESP_OK); apply(&widget,"select.car","null",cars);
    CHECK(find(widget.obj,"Gastfahrzeug",NULL)); click(widget.obj,"Gastfahrzeug");
    CHECK(find(lv_layer_top(),"Gastfahrzeug",NULL));
    w_wallbox_evcc_set_visible(&widget,false); w_wallbox_evcc_set_visible(&widget,true);
    CHECK(ui_i18n_init("en")==ESP_OK); apply(&widget,"select.car","null",cars);
    /* Guest selection stays usable without SOC. Actual choice/list changes
     * invalidate captured row indexes instead of sending a stale command. */
    click(widget.obj,"Guest vehicle"); CHECK(lv_obj_get_child_count(lv_layer_top())==1);
    apply(&widget,"select.car","null",cars); CHECK(lv_obj_get_child_count(lv_layer_top())==1);
    apply(&widget,"select.car","ev1",cars); CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    click(widget.obj,"Demo car");
    apply(&widget,"select.car","ev1","{\"options\":[\"ev1\",\"null\",\"A\\\"B\\\\C\"],\"vehicle\":{\"name\":\"Demo car\"}}");
    CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    click(widget.obj,"Demo car");
    apply(&widget,"select.car","ev1","{\"options\":[\"ev1\",\"null\"],\"vehicle\":{\"name\":\"Demo car\"}}");
    CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    click(widget.obj,"Demo car"); apply(&widget,"select.car","unavailable","{}");
    CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    apply(&widget,"select.car","null",cars);
    sim_host_service_reset(ESP_OK); click(widget.obj,"100 %"); lv_obj_t *slider=find(lv_layer_top(),NULL,&lv_slider_class); CHECK(slider);
    lv_slider_set_value(slider,13,LV_ANIM_OFF); lv_obj_send_event(slider,LV_EVENT_VALUE_CHANGED,NULL); CHECK(sim_host_service_calls()->count==0); click(lv_layer_top(),"Apply"); sent("number","set_value","number.target",NULL,85);
    apply(&widget,"number.target","85",bounds); CHECK(find(widget.obj,"85 %",NULL));
    click(widget.obj,"85 %"); slider=find(lv_layer_top(),NULL,&lv_slider_class); CHECK(slider);
    lv_slider_set_value(slider,10,LV_ANIM_OFF); lv_obj_send_event(slider,LV_EVENT_VALUE_CHANGED,NULL);
    for (int i=0;i<4;i++) { apply(&widget,"number.target","85",bounds); CHECK(find(lv_layer_top(),NULL,&lv_slider_class)==slider); CHECK(lv_slider_get_value(slider)==10); }
    apply(&widget,"number.target","85","{\"min\":20,\"max\":100,\"step\":10,\"unit_of_measurement\":\"%\"}");
    CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    apply(&widget,"number.target","85",bounds); click(widget.obj,"85 %");
    apply(&widget,"number.target","90",bounds); CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    apply(&widget,"number.target","85",bounds);
    const char *soc_values[]={"0","10","50","80","100","unknown","unavailable","nan","101","-1"};
    for (int i=0;i<10;i++) {
        apply(&widget,"sensor.soc",soc_values[i],"{\"unit_of_measurement\":\"%\"}");
        lv_obj_t *a=lv_obj_get_child(widget.obj,4),*b=lv_obj_get_child(widget.obj,5);
        int n=i<5?atoi(soc_values[i])*100:0; CHECK(lv_bar_get_value(a)==(n>2000?2000:n)); CHECK(lv_bar_get_value(b)==(n>2000?n-2000:0));
        if(i>=5) CHECK(find(widget.obj,"-- %",NULL));
    }
    click(widget.obj,"85 %"); CHECK(lv_obj_get_child_count(lv_layer_top())); w_wallbox_evcc_set_visible(&widget,false); CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    w_wallbox_evcc_set_visible(&widget,true); click(widget.obj,"85 %"); sim_host_set_online(false); advance(300); CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    CHECK(find(widget.obj,"Unavailable",NULL));
    sim_host_service_reset(ESP_OK); click(widget.obj,"Off"); CHECK(sim_host_service_calls()->count==0); sim_host_set_online(true); advance(300);
    CHECK(find(widget.obj,"Charging",NULL));
    cJSON *sessions=cJSON_Parse("[{\"created\":\"2026-10-10T11:54:00+02:00\",\"finished\":\"2026-10-10T15:00:00+02:00\",\"vehicle\":\"id4\",\"loadpoint\":\"Driveway\",\"chargedEnergy\":20.2}]");
    apply(&widget,"binary_sensor.connected","off","{}"); advance(300);
    ha_evcc_sessions_seed("select.mode","Driveway",sessions); cJSON_Delete(sessions); advance(300);
    CHECK(find(widget.obj,"0.0 kW",NULL)); CHECK(find(widget.obj,"20.2 kWh",NULL)); CHECK(find(widget.obj,"Last session · id4",NULL)); CHECK(find(widget.obj,"No vehicle",NULL));
    CHECK(!lv_obj_is_visible(lv_obj_get_child(widget.obj,4))); CHECK(!lv_obj_is_visible(find(widget.obj,"85 %",NULL)));
    sim_host_service_reset(ESP_OK); click(widget.obj,"No vehicle"); CHECK(!sim_host_service_calls()->count); CHECK(!lv_obj_get_child_count(lv_layer_top()));
    click(widget.obj,"Off"); sent("select","select_option","select.mode","off",0); apply(&widget,"select.mode","off",modes);
    click(widget.obj,"Sessions"); CHECK(find(lv_layer_top(),"Start",NULL)); CHECK(find(lv_layer_top(),"Vehicle",NULL)); CHECK(find(lv_layer_top(),"Charged",NULL)); CHECK(find(lv_layer_top(),"20.2 kWh",NULL)); CHECK(!find(lv_layer_top(),"Total",NULL));
    w_wallbox_evcc_set_visible(&widget,false); CHECK(!lv_obj_get_child_count(lv_layer_top()));
    w_wallbox_evcc_set_visible(&widget,true); click(widget.obj,"Sessions"); lv_obj_delete(widget.obj); advance(300); CHECK(lv_obj_get_child_count(lv_layer_top())==0);
    /* Older installations keep their raw mode options and a percentage select
     * fallback. Both commands use the complete HA options without guessing. */
    strcpy(def.tertiary_entity_id,"select.target");
    CHECK(w_wallbox_evcc_create(&def,lv_screen_active(),&widget)==ESP_OK);
    const char *legacy="{\"options\":[\"off\",\"pv\",\"minpv\",\"now\"]}";
    apply(&widget,"select.mode","pv",legacy);
    apply(&widget,"select.target","80","{\"options\":[\"0\",\"80\",\"100\"]}");
    sim_host_service_reset(ESP_OK); click(widget.obj,"pv");
    apply(&widget,"select.mode","pv",legacy); CHECK(lv_obj_get_child_count(lv_layer_top())==1);
    click(lv_layer_top(),"minpv");
    sent("select","select_option","select.mode","minpv",0); apply(&widget,"select.mode","minpv",legacy);
    sim_host_service_reset(ESP_OK); click(widget.obj,"80 %");
    apply(&widget,"select.target","80","{\"options\":[\"0\",\"80\",\"100\"]}"); CHECK(lv_obj_get_child_count(lv_layer_top())==1);
    click(lv_layer_top(),"100");
    sent("select","select_option","select.target","100",0); apply(&widget,"select.target","100","{\"options\":[\"0\",\"80\",\"100\"]}");
    lv_obj_delete(widget.obj); advance(300);
    /* HA option translations label every row, even before that car is selected. */
    CHECK(w_wallbox_evcc_create(&def,lv_screen_active(),&widget)==ESP_OK);
    cJSON *names_request=ha_evcc_vehicle_names_next_request(91,now); CHECK(names_request); cJSON_Delete(names_request);
    cJSON *names=cJSON_Parse("{\"resources\":{\"component.evcc_intg.entity.select.vehiclename.state.ev1\":\"Demo #1\",\"component.evcc_intg.entity.select.vehiclename.state.ev4\":\"Demo ID.4\"}}");
    ha_evcc_vehicle_names_result(91,true,names); cJSON_Delete(names);
    apply(&widget,"select.mode","smart",modes); apply(&widget,"binary_sensor.connected","on","{}");
    const char *named_cars="{\"options\":[\"null\",\"ev1\",\"ev4\"]}";
    apply(&widget,"select.car","null",named_cars); click(widget.obj,"Guest vehicle");
    CHECK(find(lv_layer_top(),"Demo #1",NULL)); CHECK(find(lv_layer_top(),"Demo ID.4",NULL));
    CHECK(!find(lv_layer_top(),"ev1",NULL)); CHECK(!find(lv_layer_top(),"ev4",NULL));
    apply(&widget,"select.car","null",named_cars); CHECK(find(lv_layer_top(),"Demo ID.4",NULL));
    sim_host_service_reset(ESP_OK); click(lv_layer_top(),"Demo ID.4"); sent("select","select_option","select.car","ev4",0);
    lv_obj_delete(widget.obj); advance(300);
    printf("wallbox regression: %u checks passed\n",checks); return 0;
}
