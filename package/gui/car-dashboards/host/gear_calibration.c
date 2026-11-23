/* SPDX-License-Identifier: GPL-2.0-only */
#include "gear_calibration.h"
#include "gear_calibration_store.h"
#include "page_header.h"
#include "navigation.h"
#include "settings.h"
#include "transition.h"
#include "system_fonts.h"
#include "ui_update.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static lv_obj_t *screen,*gears[8],*notice,*instruction,*measurement,*hint,*recorded,*capture,*finish,*begin,*cancel;
static lv_obj_t *jitter_panel,*jitter_toggle,*jitter_scroll_hint,*legend,*entry_sliders[8],*exit_sliders[8];
static lv_obj_t *entry_values[8],*exit_values[8],*overlap_labels[8];
static bool jitter_view;
static int dragging=-1;
static int selected=-1,leave;
static bool closing,tracking;
static uint32_t stable_since;
static int anchor_forward,anchor_side;
static void update(lv_timer_t *timer);
static lv_obj_t *label(lv_obj_t *parent,const char *text,int size,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);
    lv_obj_set_style_text_font(o,dashboard_font(size),0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    return o;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width) {
    lv_obj_t *o=lv_btn_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,56);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x1e1e1e),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,8,0);lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x4e4e4e),0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *text_object=label(o,text,20,0xf4f4f4);lv_obj_center(text_object);return o;
}
static void enabled(lv_obj_t *o,bool yes) {
    if(yes)lv_obj_clear_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);
    hz_style_opa(o,yes?255:90);
}
static void navigate(void) {
    if(leave==2)hz_home_open();else hz_settings_open();
    leave=0;closing=false;
}
static void exit_to(int destination) {
    struct hz_gear_cal_state state;hz_gear_cal_snapshot(&state);
    if(state.busy)return;
    leave=destination;
    if(!state.active)navigate();
    else if(hz_gear_cal_action(HZ_GEAR_CANCEL,0))closing=true;
}
static void exit_clicked(lv_event_t *event) {exit_to((int)(intptr_t)lv_event_get_user_data(event));}
static void home_clicked(lv_event_t *event) {(void)event;exit_to(2);}
static void select_gear(lv_event_t *event) {
    if(lv_obj_has_state(lv_event_get_target(event),LV_STATE_DISABLED))return;
    selected=(int)(intptr_t)lv_event_get_user_data(event);tracking=false;stable_since=lv_tick_get();
    if(jitter_view)lv_obj_scroll_to_view(lv_obj_get_parent(entry_sliders[selected]),LV_ANIM_ON);
}
static void action_clicked(lv_event_t *event) {
    if(lv_obj_has_state(lv_event_get_target(event),LV_STATE_DISABLED))return;
    enum hz_gear_cal_action action=(enum hz_gear_cal_action)(intptr_t)lv_event_get_user_data(event);
    if(action==HZ_GEAR_CAPTURE && selected<0)return;
    if(hz_gear_cal_action(action,selected<0?0:VEHICLE_CAL_GEARS[selected])) {
        tracking=false;stable_since=lv_tick_get();
    }
}
static void toggle_jitter(lv_event_t *event) {
    (void)event;jitter_view=!jitter_view;update(NULL);
}
static void slider_changed(lv_event_t *event) {
    unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *slider=lv_event_get_target(event);
    if(lv_obj_has_state(slider,LV_STATE_DISABLED))return;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED || code==LV_EVENT_VALUE_CHANGED) {
        if(selected!=(int)i) {tracking=false;stable_since=lv_tick_get();}
        selected=(int)i;dragging=(int)i;
        int entry=lv_slider_get_value(entry_sliders[i]),exit=lv_slider_get_value(exit_sliders[i]);
        if(entry>exit) {
            if(slider==entry_sliders[i])lv_slider_set_value(exit_sliders[i],entry,LV_ANIM_OFF);
            else lv_slider_set_value(entry_sliders[i],exit,LV_ANIM_OFF);
        }
    } else if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        if(dragging!=(int)i)return;
        int entry=lv_slider_get_value(entry_sliders[i]),exit=lv_slider_get_value(exit_sliders[i]);
        dragging=-1;
        lv_indev_t *input=lv_indev_get_act();
        if(code==LV_EVENT_RELEASED && (!input || !lv_indev_get_scroll_obj(input)))
            hz_gear_cal_threshold(VEHICLE_CAL_GEARS[i],entry,exit);
    }
    update(NULL);
}
static void gear_list(char *out,size_t size,unsigned mask) {
    size_t used=0;out[0]=0;
    for(unsigned i=0;i<8;i++)if(mask&(1u<<i))
        used+=(size_t)snprintf(out+used,size-used,"%s%c",used?" ":"",VEHICLE_CAL_GEARS[i]);
    if(!used)snprintf(out,size,"none");
}
static void visibility(lv_obj_t *object,bool show) {
    if(show)lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void update(lv_timer_t *timer) {
    (void)timer;if(!screen || lv_scr_act()!=screen)return;
    struct hz_gear_cal_state state;hz_gear_cal_snapshot(&state);
    if(closing && !state.busy) {if(!state.active) {navigate();return;}closing=false;leave=0;}
    visibility(jitter_panel,jitter_view);
    visibility(jitter_scroll_hint,jitter_view);
    visibility(measurement,!jitter_view);visibility(hint,!jitter_view);
    visibility(recorded,!jitter_view);visibility(legend,!jitter_view);visibility(capture,!jitter_view);
    hz_label_text(lv_obj_get_child(jitter_toggle,0),jitter_view?"Show position calibration":"Show jitter correction");
    hz_label_text(instruction,jitter_view?
        "Vehicle stationary. Tune entry and exit radii; review overlaps before saving.":
        "Vehicle stationary. Select the gear, hold it steady, then save its position.");
    hz_label_text(notice,dragging>=0?"Previewing correction. Release the slider to apply it to the adapter.":state.message);
    if(state.active && state.table.fresh && !state.busy) {
        if(!tracking || llabs((long long)state.table.forward-anchor_forward)>500 ||
           llabs((long long)state.table.side-anchor_side)>500) {
            anchor_forward=state.table.forward;anchor_side=state.table.side;
            stable_since=lv_tick_get();tracking=true;
        }
        hz_label_text_fmt(measurement,"Forward: %.3f deg\nSide: %.3f deg",state.table.forward/1000.0,state.table.side/1000.0);
    } else {tracking=false;stable_since=lv_tick_get();hz_label_text(measurement,state.active?"Waiting for fresh sensor angles...":"Sensor angles appear during calibration.");}
    bool settled=tracking && lv_tick_elaps(stable_since)>=2000;
    hz_label_text(hint,!state.active?"Start calibration, then select a gear.":selected<0?"Select the gear held by the shifter.":
        !state.table.fresh?"Fresh sensor measurements are required.":settled?"Position steady. Save position to confirm.":"Hold the selected gear still for two seconds.");
    if(selected>=0 && state.table.positions[selected].set && state.table.positions_known)
        hz_label_text_fmt(recorded,"Recorded: %.3f / %.3f deg",state.table.positions[selected].forward/1000.0,state.table.positions[selected].side/1000.0);
    else hz_label_text(recorded,!state.table.positions_known?"Start / resume to read recorded positions.":
        selected<0?"Select a gear to review its recorded position.":"This position has not been recorded.");
    struct vehicle_cal_table preview=state.table;
    if(dragging>=0 || state.busy) {
        for(unsigned i=0;i<8;i++)preview.thresholds[i]=(struct vehicle_cal_threshold){
            lv_slider_get_value(entry_sliders[i]),lv_slider_get_value(exit_sliders[i])};
    }
    bool any=false;
    for(unsigned i=0;i<8;i++) {
        bool editable=state.active && !state.busy && state.table.thresholds_known;
        if(dragging<0 && !state.busy && state.table.thresholds_known) {
            lv_slider_set_value(entry_sliders[i],state.table.thresholds[i].entry,LV_ANIM_OFF);
            lv_slider_set_value(exit_sliders[i],state.table.thresholds[i].exit,LV_ANIM_OFF);
        }
        enabled(entry_sliders[i],editable);enabled(exit_sliders[i],editable);
        if(state.table.thresholds_known) {
            hz_label_text_fmt(entry_values[i],"%c entry: %.3f deg",VEHICLE_CAL_GEARS[i],preview.thresholds[i].entry/1000.0);
            hz_label_text_fmt(exit_values[i],"Exit: %.3f deg",preview.thresholds[i].exit/1000.0);
        } else {
            hz_label_text_fmt(entry_values[i],"%c entry: --",VEHICLE_CAL_GEARS[i]);hz_label_text(exit_values[i],"Exit: --");
        }
        struct vehicle_cal_overlap overlap=vehicle_cal_overlaps(&preview,i);
        bool conflict=overlap.entry || overlap.exit;
        if(!state.table.thresholds_known)hz_label_text(overlap_labels[i],"Start / resume to read jitter correction.");
        else if(!state.table.positions_known)hz_label_text(overlap_labels[i],"Start / resume to read recorded positions.");
        else if(!preview.positions[i].set)hz_label_text(overlap_labels[i],"Capture this gear to check overlaps.");
        else if(conflict) {
            char entry[24],exit[24],center[24];gear_list(entry,sizeof(entry),overlap.entry);
            gear_list(exit,sizeof(exit),overlap.exit);gear_list(center,sizeof(center),overlap.center);
            hz_label_text_fmt(overlap_labels[i],"Entry overlap: %s | Exit overlap: %s\nExit reaches center: %s",entry,exit,center);
        } else hz_label_text(overlap_labels[i],"No overlap with other recorded gears.");
        lv_color_t warning_color=lv_color_hex(overlap.center?0xff8585:conflict?0xffc067:0xb1b1b1);
        if(lv_obj_get_style_text_color(overlap_labels[i],0).full!=warning_color.full)
            lv_obj_set_style_text_color(overlap_labels[i],warning_color,0);
        any|=state.table.positions[i].set;
        lv_color_t color=lv_color_hex(state.table.positions[i].set?0x237b47:0x1b1b1b);
        if(lv_obj_get_style_bg_color(gears[i],0).full!=color.full)lv_obj_set_style_bg_color(gears[i],color,0);
        int width=selected==(int)i?3:1;
        if(lv_obj_get_style_border_width(gears[i],0)!=width)lv_obj_set_style_border_width(gears[i],width,0);
        color=lv_color_hex(selected==(int)i?0x69b9ff:conflict?0xffc067:0x5e5e5e);
        if(lv_obj_get_style_border_color(gears[i],0).full!=color.full)lv_obj_set_style_border_color(gears[i],color,0);
        enabled(gears[i],state.active && !state.busy);
        hz_style_opa(gears[i],state.busy?90:255);
    }
    enabled(capture,state.active && !state.busy && selected>=0 && settled);
    enabled(finish,state.active && !state.busy && any);
    enabled(begin,!state.busy);enabled(cancel,!state.busy);
}
void hz_gear_calibration_open(void) {
    if(!screen) {
        screen=lv_obj_create(NULL);lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen,lv_color_black(),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
        lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        hz_page_header_create(screen,"Gear calibration","Settings",home_clicked,true);
        instruction=label(screen,"",19,0xb1b1b1);
        lv_obj_set_pos(instruction,28,82);lv_obj_set_width(instruction,968);
        lv_obj_t *knob=lv_obj_create(screen);lv_obj_remove_style_all(knob);
        lv_obj_set_pos(knob,90,150);lv_obj_set_size(knob,360,360);
        lv_obj_set_style_radius(knob,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_bg_color(knob,lv_color_hex(0x121212),0);lv_obj_set_style_bg_opa(knob,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(knob,2,0);lv_obj_set_style_border_color(knob,lv_color_hex(0x4e4e4e),0);
        lv_obj_clear_flag(knob,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
        const int x[]={60,180,140,140,220,220,300,300};
        const int y[]={100,180,100,260,100,260,100,260};
        /* Four top gates R-1-3-5, central N, three bottom gates 2-4-6. */
        lv_obj_t *rail=lv_obj_create(knob);lv_obj_remove_style_all(rail);
        lv_obj_set_pos(rail,60,177);lv_obj_set_size(rail,240,6);lv_obj_set_style_bg_opa(rail,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(rail,lv_color_hex(0x707070),0);
        for(unsigned col=0;col<4;col++) {
            lv_obj_t *gate=lv_obj_create(knob);lv_obj_remove_style_all(gate);
            lv_obj_set_pos(gate,57+80*col,100);lv_obj_set_size(gate,6,col?160:80);
            lv_obj_set_style_bg_color(gate,lv_color_hex(0x707070),0);lv_obj_set_style_bg_opa(gate,LV_OPA_COVER,0);
        }
        for(unsigned i=0;i<8;i++) {
            char text[]={VEHICLE_CAL_GEARS[i],0};gears[i]=button(knob,text,x[i]-24,y[i]-24,48);
            lv_obj_set_height(gears[i],48);
            lv_obj_set_style_text_font(lv_obj_get_child(gears[i],0),dashboard_font(30),0);
            lv_obj_add_event_cb(gears[i],select_gear,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        }
        measurement=label(screen,"",24,0xf4f4f4);lv_obj_set_pos(measurement,530,136);lv_obj_set_width(measurement,460);
        hint=label(screen,"",20,0xb1b1b1);lv_obj_set_pos(hint,530,232);lv_obj_set_width(hint,454);
        recorded=label(screen,"",17,0xb1b1b1);lv_obj_set_pos(recorded,530,304);lv_obj_set_width(recorded,454);
        legend=label(screen,"Blue: selected  |  Green: recorded\nFinish & save stores positions and radii.",17,0xffffff);
        lv_obj_set_pos(legend,530,350);
        jitter_toggle=button(screen,"Show jitter correction",90,108,360);
        lv_obj_set_height(jitter_toggle,34);lv_obj_add_event_cb(jitter_toggle,toggle_jitter,LV_EVENT_CLICKED,NULL);
        jitter_panel=lv_obj_create(screen);lv_obj_remove_style_all(jitter_panel);
        lv_obj_set_pos(jitter_panel,510,132);lv_obj_set_size(jitter_panel,494,246);
        lv_obj_set_scroll_dir(jitter_panel,LV_DIR_VER);lv_obj_set_scrollbar_mode(jitter_panel,LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_bottom(jitter_panel,14,0);
        lv_obj_set_style_width(jitter_panel,4,LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(jitter_panel,lv_color_hex(0x707070),LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(jitter_panel,LV_OPA_COVER,LV_PART_SCROLLBAR);
        jitter_scroll_hint=label(screen,"Scroll for all eight gears. Amber: overlap; red: center reached.",14,0xb1b1b1);
        lv_obj_set_pos(jitter_scroll_hint,530,384);lv_obj_set_width(jitter_scroll_hint,460);
        lv_obj_t *explanation=label(jitter_panel,"Entry accepts a gear; exit holds it against jitter.\nRelease to apply. Finish & save stores all radii.",16,0xb1b1b1);
        lv_obj_set_pos(explanation,18,0);lv_obj_set_width(explanation,450);
        for(unsigned i=0;i<8;i++) {
            lv_obj_t *row=lv_obj_create(jitter_panel);lv_obj_remove_style_all(row);
            lv_obj_set_pos(row,18,58+(int)i*112);lv_obj_set_size(row,450,104);
            lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);
            entry_values[i]=label(row,"",17,0xf4f4f4);lv_obj_set_pos(entry_values[i],0,0);
            exit_values[i]=label(row,"",17,0xf4f4f4);lv_obj_set_pos(exit_values[i],236,0);
            entry_sliders[i]=lv_slider_create(row);exit_sliders[i]=lv_slider_create(row);
            lv_obj_t *sliders[]={entry_sliders[i],exit_sliders[i]};
            for(unsigned j=0;j<2;j++) {
                lv_slider_set_range(sliders[j],1,180000);
                lv_obj_add_flag(sliders[j],LV_OBJ_FLAG_SCROLL_CHAIN_VER);
                lv_obj_set_pos(sliders[j],14+(int)j*236,38);lv_obj_set_size(sliders[j],178,8);
                lv_obj_set_ext_click_area(sliders[j],14);
                lv_obj_set_style_bg_color(sliders[j],lv_color_hex(0x69b9ff),LV_PART_INDICATOR);
                lv_obj_set_style_bg_color(sliders[j],lv_color_hex(0x69b9ff),LV_PART_KNOB);
                const lv_event_code_t events[]={LV_EVENT_PRESSED,LV_EVENT_VALUE_CHANGED,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST};
                for(unsigned k=0;k<sizeof(events)/sizeof(events[0]);k++)
                    lv_obj_add_event_cb(sliders[j],slider_changed,events[k],(void *)(uintptr_t)i);
            }
            overlap_labels[i]=label(row,"",14,0xb1b1b1);lv_obj_set_pos(overlap_labels[i],0,60);
            lv_obj_set_width(overlap_labels[i],450);
        }
        capture=button(screen,"Save position",530,404,210);finish=button(screen,"Finish & save",760,404,230);
        begin=button(screen,"Start / resume",530,478,210);cancel=button(screen,"Cancel / back",760,478,230);
        lv_obj_add_event_cb(capture,action_clicked,LV_EVENT_CLICKED,(void *)HZ_GEAR_CAPTURE);
        lv_obj_add_event_cb(finish,action_clicked,LV_EVENT_CLICKED,(void *)HZ_GEAR_SAVE);
        lv_obj_add_event_cb(begin,action_clicked,LV_EVENT_CLICKED,(void *)HZ_GEAR_BEGIN);
        lv_obj_add_event_cb(cancel,exit_clicked,LV_EVENT_CLICKED,(void *)1);
        notice=label(screen,"",16,0xb1b1b1);lv_obj_set_pos(notice,28,550);lv_obj_set_width(notice,968);
        lv_label_set_long_mode(notice,LV_LABEL_LONG_WRAP);lv_timer_create(update,100,NULL);
    }
    closing=false;leave=0;tracking=false;
    hz_screen_load_anim(screen,LV_SCR_LOAD_ANIM_MOVE_LEFT,300,0,false);
    update(NULL);
}
