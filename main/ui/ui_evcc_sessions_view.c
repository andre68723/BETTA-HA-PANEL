/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ui/ui_evcc_sessions_view.h"
#include "ui/ui_i18n.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include <stdio.h>
#include <time.h>
const char *ui_evcc_sessions_status(evcc_sessions_status_t status, bool empty)
{
    if(status==EVCC_SESSIONS_READY) return empty?ui_i18n_get("evcc.sessions_empty","No completed sessions"):"";
    if(status==EVCC_SESSIONS_UNSUPPORTED) return ui_i18n_get("evcc.sessions_unsupported","Sessions unavailable");
    if(status==EVCC_SESSIONS_ERROR) return ui_i18n_get("evcc.sessions_error","Could not load sessions");
    return ui_i18n_get("evcc.sessions_loading","Loading sessions...");
}
void ui_evcc_session_date(int64_t timestamp, char *out, size_t capacity)
{
    time_t t=(time_t)timestamp; struct tm date;
    if(!localtime_r(&t,&date)) { snprintf(out,capacity,"--"); return; }
    strftime(out,capacity,"%d.%m. · %H:%M",&date);
}
static lv_obj_t *cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, ui_text_role_t role, bool right)
{
    lv_obj_t *label=lv_label_create(parent); ui_text_set(label,role,true);
    lv_label_set_text(label,text); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label,x,y); lv_obj_set_size(label,w,h);
    lv_obj_set_style_text_align(label,right?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT,0);
    lv_obj_set_style_text_color(label,lv_color_hex(role==UI_TEXT_META?APP_UI_COLOR_TEXT_MUTED:APP_UI_COLOR_TEXT_PRIMARY),0);
    return label;
}
void ui_evcc_sessions_view(lv_obj_t *body, lv_obj_t *status, const evcc_sessions_t *sessions)
{
    lv_label_set_text(status,ui_evcc_sessions_status(sessions->status,sessions->count==0));
    lv_obj_clean(body);
    if(sessions->status!=EVCC_SESSIONS_READY || !sessions->count) return;
    int w=lv_obj_get_width(body), a=w*39/100, b=w*32/100;
    int meta=lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    int line=lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    cell(body,ui_i18n_get("evcc.start","Start"),0,0,a-8,meta,UI_TEXT_META,false);
    cell(body,ui_i18n_get("evcc.car","Vehicle"),a,0,b-8,meta,UI_TEXT_META,false);
    cell(body,ui_i18n_get("evcc.charged","Charged"),a+b,0,w-a-b,meta,UI_TEXT_META,true);
    int y=meta+12;
    for(unsigned i=0;i<sessions->count;i++) {
        const evcc_session_t *row=&sessions->rows[i];
        char date[40],energy[40]; time_t t=(time_t)row->start; struct tm local;
        if(localtime_r(&t,&local)) strftime(date,sizeof(date),"%d.%m.%Y\n%H:%M",&local); else snprintf(date,sizeof(date),"--");
        snprintf(energy,sizeof(energy),"%.1f kWh",row->energy);
        cell(body,date,0,y,a-8,line*2,UI_TEXT_LABEL,false);
        cell(body,row->vehicle[0]?row->vehicle:"--",a,y,b-8,line*2,UI_TEXT_LABEL,false);
        cell(body,energy,a+b,y,w-a-b,line,UI_TEXT_LABEL,true);
        y+=line*2+12;
        lv_obj_t *separator=lv_obj_create(body); lv_obj_remove_style_all(separator);
        lv_obj_set_pos(separator,0,y-5); lv_obj_set_size(separator,w,1);
        lv_obj_set_style_bg_color(separator,lv_color_hex(APP_UI_COLOR_CARD_BORDER),0); lv_obj_set_style_bg_opa(separator,LV_OPA_COVER,0);
    }
}
