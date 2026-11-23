/* SPDX-License-Identifier: GPL-2.0-only */
#include "ui_update.h"
#include "transition.h"
#include "lvgl/lvgl.h"
#include "system_fonts.h"
#include "navigation.h"
#include "video_ui.h"
#include "video_player.h"
#include "page_header.h"
#include "page_art.h"

static lv_obj_t *screen,*status_label,*time_label,*slider,*play_label,*play_button,*stop_button;
static lv_timer_t *timer;
static bool leaving,dragging;
static void update(lv_timer_t *unused) {
    (void)unused;
    const struct hz_video_state *state=hz_video_poll();
    if(leaving && state->idle) {
        hz_video_clear_stale_plane();
        lv_timer_pause(timer);leaving=false;hz_home_open();return;
    }
    bool playing=state->status==HZ_VIDEO_PLAYING || state->status==HZ_VIDEO_OPENING;
    hz_label_text(play_label,playing?LV_SYMBOL_PAUSE " Pause":LV_SYMBOL_PLAY " Play");
    const char *text=leaving?"Closing video...":state->status==HZ_VIDEO_ERROR?state->message:
        state->status==HZ_VIDEO_OPENING?"Opening video...":state->status==HZ_VIDEO_CLOSING?"Stopping...":
        state->status==HZ_VIDEO_PLAYING?"Playing":state->status==HZ_VIDEO_PAUSED?"Paused":"Ready";
    hz_label_text(status_label,text);
    hz_label_text_fmt(time_label,"%02u:%02u / %02u:%02u",state->position/60,state->position%60,
        state->duration/60,state->duration%60);
    lv_slider_set_range(slider,0,state->duration);
    if(!dragging) lv_slider_set_value(slider,state->position,LV_ANIM_OFF);
}
static void play(lv_event_t *event) {
    (void)event;if(leaving)return;
    enum hz_video_status state=hz_video_poll()->status;
    if(state==HZ_VIDEO_PLAYING || state==HZ_VIDEO_OPENING) hz_video_pause();
    else hz_video_play();
    update(NULL);
}
static void stop(lv_event_t *event) {(void)event;if(!leaving)hz_video_stop();update(NULL);}
static void back(lv_event_t *event) {
    (void)event;leaving=true;hz_video_stop();
    lv_obj_add_state(play_button,LV_STATE_DISABLED);lv_obj_add_state(stop_button,LV_STATE_DISABLED);
    lv_obj_add_state(slider,LV_STATE_DISABLED);update(NULL);
}
static void seek(lv_event_t *event) {
    if(leaving)return;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED) dragging=true;
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        dragging=false;hz_video_seek(lv_slider_get_value(slider));
    }
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int size) {
    lv_obj_t *object=lv_label_create(parent);hz_label_text(object,text);
    lv_obj_set_style_text_font(object,dashboard_font(size),0);
    lv_obj_set_style_text_color(object,lv_color_hex(0xe7e7e7),0);lv_obj_set_pos(object,x,y);return object;
}
static lv_obj_t *button(const char *text,int x,lv_event_cb_t callback,lv_obj_t **caption) {
    lv_obj_t *object=lv_btn_create(screen);lv_obj_set_pos(object,x,540);lv_obj_set_size(object,125,48);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x3f3f3f),0);
    lv_obj_t *text_object=label(object,text,0,0,18);lv_obj_center(text_object);
    lv_obj_add_event_cb(object,callback,LV_EVENT_CLICKED,NULL);
    if(caption)*caption=text_object;
    return object;
}
void hz_video_open(void) {
    uint32_t started=lv_tick_get();bool first=screen==NULL;
    if(!screen) {
        screen=lv_obj_create(NULL);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen,lv_color_black(),0);
        hz_page_header_create(screen,"Big Buck Bunny","Video",back,true);
        status_label=label(screen,"Ready",787,540,16);lv_obj_set_width(status_label,213);
        lv_label_set_long_mode(status_label,LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t *viewport=lv_obj_create(screen);lv_obj_set_pos(viewport,112,78);lv_obj_set_size(viewport,800,450);
        lv_obj_remove_style_all(viewport);lv_obj_set_pos(viewport,112,78);lv_obj_set_size(viewport,800,450);
        lv_obj_set_style_bg_color(viewport,lv_color_black(),0);lv_obj_set_style_bg_opa(viewport,LV_OPA_COVER,0);
        lv_obj_clear_flag(viewport,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
        play_button=button(LV_SYMBOL_PLAY " Play",112,play,&play_label);
        stop_button=button(LV_SYMBOL_STOP " Stop",247,stop,NULL);
        slider=lv_slider_create(screen);lv_obj_set_pos(slider,397,551);lv_obj_set_size(slider,360,8);
        lv_slider_set_range(slider,0,HZ_VIDEO_DURATION);lv_obj_add_event_cb(slider,seek,LV_EVENT_ALL,NULL);
        time_label=label(screen,"00:00 / 09:56",440,568,16);
        timer=lv_timer_create(update,100,NULL);
    }
    leaving=dragging=false;
    lv_obj_clear_state(play_button,LV_STATE_DISABLED);lv_obj_clear_state(stop_button,LV_STATE_DISABLED);
    lv_obj_clear_state(slider,LV_STATE_DISABLED);
    hz_page_profile("video",started,first);
    hz_page_art_release(lv_scr_act()); /* Home's 2.4 MB DMA background goes back to the decoder. */
    hz_screen_load_anim(screen,LV_SCR_LOAD_ANIM_MOVE_LEFT,300,0,false);lv_timer_resume(timer);update(NULL);
}
