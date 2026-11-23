/* SPDX-License-Identifier: GPL-2.0-only */
#include "ui_update.h"
#include "vehicle_ui.h"
#include "lvgl/lvgl.h"
#include "system_fonts.h"
#include "speedometer.h"
#include "coolant_gauge.h"
#include "pdc_view.h"
#if defined(HZ_COMBINED_UI) && !defined(PREVIEW_PDC_APP)
#include "vehicle_body.h"
#include "settings_store.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_VEHICLE_UBUS
#include "vehicle_client.h"
#include <pthread.h>
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static struct vehicle_state latest;
static char *socket_path;
static bool active;
static void receive_state(const struct vehicle_state *s,void *arg) {
    (void)arg;pthread_mutex_lock(&state_lock);latest=*s;pthread_mutex_unlock(&state_lock);
}
static void *worker(void *arg) {
    (void)arg;const struct vehicle_callbacks cb={.state=receive_state};
    vehicle_client_run(socket_path,&cb,NULL);return NULL;
}
#endif
#if (defined(PREVIEW_LVGL_DASHBOARD) || defined(HZ_COMBINED_UI)) && !defined(PREVIEW_PDC_APP)
#include "UI/ui.h"
#elif !defined(PREVIEW_PDC_APP)
void Car_Dashboard_Vehicle(int speed,int rpm,int left,int right);
#endif
static lv_obj_t *status_label,*pdc_panel;
static int standalone;
#if (defined(PREVIEW_LVGL_DASHBOARD) || defined(HZ_COMBINED_UI)) && !defined(PREVIEW_PDC_APP)
struct needle {lv_obj_t *object;int from,target;uint32_t start;bool known;};
static struct needle needles[4];
/* Interpolate successive samples without restarting an unchanged target. */
static void needle_update(unsigned id,lv_obj_t *object,bool valid,int target) {
    if(!object)return;
    struct needle *n=&needles[id];
    if(n->object!=object) {*n=(struct needle){.object=object};}
    if(!valid) {lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);n->known=false;return;}
    lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);
    uint32_t now=lv_tick_get();
    if(!n->known) {n->known=true;n->from=n->target=target;n->start=now;}
    else if(n->target!=target) {n->from=lv_img_get_angle(object);n->target=target;n->start=now;}
    unsigned elapsed=lv_tick_elaps(n->start);
    int angle=elapsed>=60?n->target:n->from+(n->target-n->from)*(int)elapsed/60;
    if(lv_img_get_angle(object)!=angle)lv_img_set_angle(object,angle);
}
static void checked(lv_obj_t *object,bool enabled) {
    if(!object)return;
    if(enabled)lv_obj_add_state(object,LV_STATE_CHECKED);
    else lv_obj_clear_state(object,LV_STATE_CHECKED);
}
#endif
#if defined(HZ_COMBINED_UI) && !defined(PREVIEW_PDC_APP)
/* Opt-in RAM diagnostic, written only by the LVGL thread. */
static void diagnostic(const struct vehicle_state *s,uint64_t now) {
    const char *path=getenv("HZ_UI_DIAGNOSTICS");
    static uint64_t last;
    if(!path || !*path || now-last<200) return;
    last=now;
    char temporary[512];
    if(snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary)) return;
    FILE *file=fopen(temporary,"w");if(!file)return;
    fprintf(file,"{\"schema\":1,\"pid\":%ld,\"monotonic_ms\":%llu,\"sequence\":%llu,\"connected\":%s,\"dashboard_active\":%s",
        (long)getpid(),(unsigned long long)now,(unsigned long long)s->sequence,
        s->connected?"true":"false",ui_Screen1 && lv_scr_act()==ui_Screen1?"true":"false");
    lv_obj_t *hands[]={ui_KmPoint,ui_TMeterpoint};
    const unsigned ids[]={V_SPEED,V_RPM};const char *names[]={"speed","rpm"};
    for(unsigned i=0;i<2;i++) {
        const struct vehicle_signal *v=&s->signals[ids[i]];
        fprintf(file,",\"%s\":{\"valid\":%s,\"value\":%d,\"exists\":%s,\"hidden\":%s,\"visible\":%s,\"angle\":%d}",
            names[i],v->valid?"true":"false",v->value,hands[i]?"true":"false",
            !hands[i] || lv_obj_has_flag(hands[i],LV_OBJ_FLAG_HIDDEN)?"true":"false",
            hands[i] && lv_obj_is_visible(hands[i])?"true":"false",hands[i]?lv_img_get_angle(hands[i]):0);
    }
    struct tm calendar;
    fprintf(file,",\"calendar_valid\":%s}\n",vehicle_clock_time(s,now,&calendar)?"true":"false");
    if(fclose(file)==0) (void)rename(temporary,path);
}
#endif
bool preview_vehicle_time(struct tm *out) {
#ifdef HAVE_VEHICLE_UBUS
    if(!active) return false;
    struct vehicle_state s;
    pthread_mutex_lock(&state_lock);s=latest;pthread_mutex_unlock(&state_lock);
    return vehicle_clock_time(&s,vehicle_now_ms(),out);
#else
    (void)out;return false;
#endif
}
int preview_vehicle_start(const char *path,int pdc_only) {
#ifndef HAVE_VEHICLE_UBUS
    (void)path;(void)pdc_only;
    fprintf(stderr,"Vehicle support needs local ubus dependencies; run package/utils/vehicle/build-host.sh then rebuild previews\n");return -1;
#else
    standalone=pdc_only;
    socket_path=path?strdup(path):NULL;
    if(path&&!socket_path) return -1;
    pthread_t thread;
    if(pthread_create(&thread,NULL,worker,NULL)) {free(socket_path);return -1;}
    pthread_detach(thread);active=true;
    status_label=lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(status_label,dashboard_font(16),0);
    lv_obj_set_style_bg_color(status_label,lv_color_hex(0x101010),0);
    lv_obj_set_style_bg_opa(status_label,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(status_label,lv_color_hex(0xffffff),0);
    lv_obj_align(status_label,LV_ALIGN_BOTTOM_MID,0,-2);
    pdc_panel=hz_pdc_create();return 0;
#endif
}
void preview_vehicle_poll(void) {
#ifdef HAVE_VEHICLE_UBUS
    if(!active) return;
    static uint32_t last_tick;
    if(lv_tick_elaps(last_tick)<16) return;
    last_tick=lv_tick_get();
    struct vehicle_state s;
    pthread_mutex_lock(&state_lock);s=latest;pthread_mutex_unlock(&state_lock);
    uint64_t now=vehicle_now_ms();
    for(unsigned i=0;i<VEHICLE_SIGNAL_COUNT;i++) {
        if(s.signals[i].valid && !vehicle_signal_fresh(&s,i,now)) s.signals[i].valid=false;
    }
    const struct vehicle_signal *v=s.signals;
#if defined(HZ_COMBINED_UI) && !defined(PREVIEW_PDC_APP)
    hz_vehicle_body_update(&s);
    /* Unknown CAN data must not undo the last reported lighting policy. */
    hz_settings_lights(v[V_LIGHTS].valid?v[V_LIGHTS].value!=0:-1);
#endif
    char speed[24],rpm[24];
    if(v[V_SPEED].valid) snprintf(speed,sizeof(speed),"%d",v[V_SPEED].value);else strcpy(speed,"--");
    if(v[V_RPM].valid) snprintf(rpm,sizeof(rpm),"%d",v[V_RPM].value);else strcpy(rpm,"--");
    hz_label_text_fmt(status_label,"%s | %s | Speed %s km/h | RPM %s",
        s.simulated?"SIMULATED":"Vehicle",s.connected?"connected":"unavailable",speed,rpm);
#if (defined(PREVIEW_LVGL_DASHBOARD) || defined(HZ_COMBINED_UI)) && !defined(PREVIEW_PDC_APP)
    lv_obj_t *pointers[]={ui_KmPoint,ui_TMeterpoint,ui_TempPoint,ui_OilPoint};
    const unsigned ids[]={V_SPEED,V_RPM,V_COOLANT,V_FUEL};
    const int maximum[]={220,8000,150,100},angle[]={0,2430,900,900};
    for(unsigned i=0;i<4;i++) if(pointers[i]) {
        int n=v[ids[i]].value;if(n<0)n=0;if(n>maximum[i])n=maximum[i];
        needle_update(i,pointers[i],v[ids[i]].valid,i==0?dashboard_speed_angle(n):
                      i==2?dashboard_coolant_angle(n):(i==3?900:0)+n*angle[i]/maximum[i]);
    }
    checked(ui_High_beam,v[V_LIGHTS].valid && v[V_LIGHTS].value==2);
    checked(ui_Low_beam,v[V_LIGHTS].valid && v[V_LIGHTS].value==1);
    if(ui_Left)hz_style_opa(ui_Left,v[V_LEFT].valid&&v[V_LEFT].value?255:50);
    if(ui_Right)hz_style_opa(ui_Right,v[V_RIGHT].valid&&v[V_RIGHT].value?255:50);
    /* Unmapped demonstration mileage must not appear as telemetry. */
    lv_obj_t *unknown[]={ui_ODO,ui_TRIP};
    for(unsigned i=0;i<sizeof(unknown)/sizeof(unknown[0]);i++)if(unknown[i])hz_label_text(unknown[i],"--");
    if(ui_Gear) {
        char gear[2]={'-',0};
        if(v[V_MANUAL_GEAR].valid) {
            int value=v[V_MANUAL_GEAR].value;
            if(value==V_GEAR_REVERSE) gear[0]='R';
            else if(value==V_GEAR_NEUTRAL) gear[0]='N';
            else if(value>=1 && value<=6) gear[0]='0'+value;
        }
        hz_label_text(ui_Gear,gear);
    }
#endif
#if (!defined(PREVIEW_LVGL_DASHBOARD) || defined(HZ_COMBINED_UI)) && !defined(PREVIEW_PDC_APP)
    void Car_Dashboard_Vehicle(int,int,int,int);
    Car_Dashboard_Vehicle(v[V_SPEED].valid?v[V_SPEED].value:-1,v[V_RPM].valid?v[V_RPM].value:-1,
                         v[V_LEFT].valid?v[V_LEFT].value:0,v[V_RIGHT].valid?v[V_RIGHT].value:0);
#endif
    bool show=standalone || (v[V_PDC_ACTIVE].valid&&v[V_PDC_ACTIVE].value);
#if defined(HZ_COMBINED_UI) && !defined(PREVIEW_PDC_APP)
    void hz_video_obscured(bool);
    hz_video_obscured(show);
#endif
    static int was_visible;
    if(show) {
        lv_obj_clear_flag(pdc_panel,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(pdc_panel);
    } else lv_obj_add_flag(pdc_panel,LV_OBJ_FLAG_HIDDEN);
    if(show!=was_visible) {
        fprintf(stderr,"PDC overlay: %s\n",show?"on":"off");
        was_visible=show;
    }
    hz_pdc_update(&s);
    static uint64_t logged_sequence;
#if defined(HZ_COMBINED_UI) && !defined(PREVIEW_PDC_APP)
    diagnostic(&s,now);
#endif
    if(s.sequence!=logged_sequence && getenv("VEHICLE_DEBUG")) {
        logged_sequence=s.sequence;
        fprintf(stderr,"Vehicle UI: generation %s sequence %llu speed %s rpm %s pdc_active %d front_left_cm %d\n",s.generation,(unsigned long long)s.sequence,speed,rpm,
                v[V_PDC_ACTIVE].valid?v[V_PDC_ACTIVE].value:-1,v[V_PDC_FL].valid?v[V_PDC_FL].value:-1);
    }
#endif
}
