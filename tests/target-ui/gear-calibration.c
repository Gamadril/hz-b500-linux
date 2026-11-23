#define _GNU_SOURCE
#include "lvgl/lvgl.h"
#include "gear_calibration.h"
#include "gear_calibration_store.h"
#include "system_fonts.h"
#include "../vehicle/calibration-fixture.h"
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static struct calibration_fixture fixture=CALIBRATION_FIXTURE_INIT;
static atomic_bool stopped;
static int listener;
static lv_color_t pixels[1024*600];
static unsigned char frame[600][1024][3];
static lv_point_t pointer_position;
static lv_indev_state_t pointer_state=LV_INDEV_STATE_RELEASED;
static void read_pointer(lv_indev_drv_t *driver,lv_indev_data_t *data) {
    (void)driver;data->point=pointer_position;data->state=pointer_state;
}
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *colors) {
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++) {
        frame[y][x][0]=colors->ch.red;frame[y][x][1]=colors->ch.green;frame[y][x][2]=colors->ch.blue;colors++;
    }
    lv_disp_flush_ready(driver);
}
static void *adapter_owner(void *unused) {
    (void)unused;
    while(!atomic_load(&stopped)) {
        struct pollfd p={.fd=listener,.events=POLLIN};if(poll(&p,1,50)<=0)continue;
        int fd=accept(listener,NULL,NULL);assert(fd>=0);
        char command[VEHICLE_CAL_COMMAND_SIZE]={0};unsigned used=0;
        for(;;) {char c;assert(read(fd,&c,1)==1);if(c=='\r')break;assert(used+1<sizeof(command));command[used++]=c;}
        char reply[VEHICLE_CAL_REPLY_SIZE];fixture_reply(&fixture,command,reply);
        for(size_t i=0;i<strlen(reply);i++)assert(send(fd,reply+i,1,MSG_NOSIGNAL)==1);
        close(fd);
    }
    return NULL;
}
static void run(unsigned milliseconds) {
    for(unsigned i=0;i<milliseconds;i+=10) {lv_tick_inc(10);lv_timer_handler();usleep(2000);}
}
static lv_obj_t *find(lv_obj_t *parent,const char *text) {
    if(lv_obj_check_type(parent,&lv_label_class) && !strcmp(lv_label_get_text(parent),text))return parent;
    for(unsigned i=0;i<lv_obj_get_child_cnt(parent);i++) {lv_obj_t *o=find(lv_obj_get_child(parent,i),text);if(o)return o;}
    return NULL;
}
static lv_obj_t *control(const char *text) {lv_obj_t *o=find(lv_scr_act(),text);assert(o);return lv_obj_get_parent(o);}
static void click(const char *text) {lv_event_send(control(text),LV_EVENT_CLICKED,NULL);run(100);}
static void done(void) {
    for(unsigned i=0;i<1000;i++) {
        struct hz_gear_cal_state state;hz_gear_cal_snapshot(&state);
        if(!state.busy) {run(120);return;}run(20);
    }
    assert(!"calibration worker timeout");
}
static void angles(int forward,int side,bool fresh) {
    pthread_mutex_lock(&fixture.mutex);fixture.forward=forward;fixture.side=side;fixture.fresh=fresh;pthread_mutex_unlock(&fixture.mutex);
    for(unsigned i=0;i<1000;i++) {
        struct hz_gear_cal_state state;hz_gear_cal_snapshot(&state);
        if(state.table.fresh==fresh && (!fresh || (state.table.forward==forward && state.table.side==side)))return;
        run(20);
    }
    assert(!"fresh measurement timeout");
}
static void green(const char *gear,bool expected) {
    bool actual=lv_obj_get_style_bg_color(control(gear),0).full==lv_color_hex(0x237b47).full;
    assert(actual==expected);
}
static lv_obj_t *find_prefix(lv_obj_t *parent,const char *prefix) {
    if(lv_obj_check_type(parent,&lv_label_class) && !strncmp(lv_label_get_text(parent),prefix,strlen(prefix)))return parent;
    for(unsigned i=0;i<lv_obj_get_child_cnt(parent);i++) {
        lv_obj_t *object=find_prefix(lv_obj_get_child(parent,i),prefix);if(object)return object;
    }
    return NULL;
}
static lv_obj_t *jitter_row(char gear) {
    char prefix[]={gear,' ','e','n','t','r','y',':',0};
    lv_obj_t *text=find_prefix(lv_scr_act(),prefix);assert(text);return lv_obj_get_parent(text);
}
static lv_obj_t *jitter_slider(char gear,bool exit) {
    lv_obj_t *object=lv_obj_get_child(jitter_row(gear),exit?3:2);
    assert(lv_obj_check_type(object,&lv_slider_class));return object;
}
static void slide(char gear,bool exit,int value) {
    lv_obj_t *slider=jitter_slider(gear,exit);
    assert(!lv_obj_has_state(slider,LV_STATE_DISABLED));
    pthread_mutex_lock(&fixture.mutex);unsigned before=fixture.threshold_sets;pthread_mutex_unlock(&fixture.mutex);
    lv_event_send(slider,LV_EVENT_PRESSED,NULL);lv_slider_set_value(slider,value,LV_ANIM_OFF);
    lv_event_send(slider,LV_EVENT_VALUE_CHANGED,NULL);run(120);
    pthread_mutex_lock(&fixture.mutex);assert(before==fixture.threshold_sets);pthread_mutex_unlock(&fixture.mutex);
    lv_event_send(slider,LV_EVENT_RELEASED,NULL);done();
}
static void screenshot(const char *path) {
    lv_obj_invalidate(lv_scr_act());lv_refr_now(NULL);
    FILE *image=fopen(path,"wb");assert(image);fprintf(image,"P6\n1024 600\n255\n");
    assert(fwrite(frame,1,sizeof(frame),image)==sizeof(frame));fclose(image);
}
int main(int argc,char **argv) {
    assert(argc==2);assert(strlen(argv[1])<sizeof(((struct sockaddr_un *)0)->sun_path));
    setenv("HZ_GEAR_CALIBRATION_SOCKET",argv[1],1);
    listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);assert(listener>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};snprintf(address.sun_path,sizeof(address.sun_path),"%s",argv[1]);
    assert(!bind(listener,(struct sockaddr *)&address,sizeof(address)) && !listen(listener,4));
    fixture.saved[0]=(struct vehicle_cal_position){true,1000,2000};
    pthread_t owner;assert(!pthread_create(&owner,NULL,adapter_owner,NULL));
    lv_init();dashboard_fonts_init();static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,NULL,1024*600);
    lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=1024;display.ver_res=600;
    display.draw_buf=&buffer;display.flush_cb=flush;lv_disp_drv_register(&display);
    lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;
    input.read_cb=read_pointer;lv_indev_drv_register(&input);
    hz_gear_calibration_open();run(400);lv_obj_t *screen=lv_scr_act();
    lv_obj_t *knob=lv_obj_get_parent(control("R"));
    assert(lv_obj_get_width(knob)==360 && lv_obj_get_height(knob)==360);
    assert(lv_obj_get_x(control("R"))<lv_obj_get_x(control("1")) && lv_obj_get_x(control("1"))<lv_obj_get_x(control("3")) && lv_obj_get_x(control("3"))<lv_obj_get_x(control("5")));
    assert(lv_obj_get_y(control("R"))==lv_obj_get_y(control("1")) && lv_obj_get_y(control("1"))==lv_obj_get_y(control("3")) && lv_obj_get_y(control("3"))==lv_obj_get_y(control("5")));
    assert(lv_obj_get_y(control("N"))>lv_obj_get_y(control("1")) && lv_obj_get_y(control("N"))<lv_obj_get_y(control("2")));
    assert(lv_obj_get_x(control("1"))==lv_obj_get_x(control("2")) && lv_obj_get_x(control("3"))==lv_obj_get_x(control("4")) && lv_obj_get_x(control("5"))==lv_obj_get_x(control("6")));
    assert(lv_obj_has_state(control("Save position"),LV_STATE_DISABLED));green("R",false);
    click("Start / resume");done();struct hz_gear_cal_state state;hz_gear_cal_snapshot(&state);assert(state.active);green("R",true);
    assert(state.table.thresholds_known);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_threshold_read=true;pthread_mutex_unlock(&fixture.mutex);
    click("Start / resume");done();hz_gear_cal_snapshot(&state);
    assert(state.active && !state.table.thresholds_known && lv_obj_has_state(jitter_slider('R',false),LV_STATE_DISABLED));
    assert(!hz_gear_cal_threshold('R',1000,2000)); /* Never edit using stale defaults after GT read failure. */
    pthread_mutex_lock(&fixture.mutex);fixture.reject_threshold_read=false;pthread_mutex_unlock(&fixture.mutex);
    click("Start / resume");done();hz_gear_cal_snapshot(&state);assert(state.active && state.table.thresholds_known);
    assert(!hz_gear_cal_threshold('7',1000,2000) && !hz_gear_cal_threshold('R',0,2000) &&
        !hz_gear_cal_threshold('R',3000,2000) && !hz_gear_cal_threshold('R',1000,180001));
    click("Show jitter correction");
    lv_obj_t *panel=lv_obj_get_parent(jitter_row('R'));
    lv_area_t slider_area;lv_obj_get_coords(jitter_slider('R',false),&slider_area);
    pthread_mutex_lock(&fixture.mutex);unsigned before_swipe=fixture.threshold_sets;pthread_mutex_unlock(&fixture.mutex);
    pointer_position=(lv_point_t){(slider_area.x1+slider_area.x2)/2,(slider_area.y1+slider_area.y2)/2};
    pointer_state=LV_INDEV_STATE_PRESSED;run(40);
    for(unsigned i=0;i<4;i++) {pointer_position.y-=20;run(40);}
    pointer_state=LV_INDEV_STATE_RELEASED;run(400);
    assert(lv_obj_get_scroll_y(panel)>0);
    pthread_mutex_lock(&fixture.mutex);assert(fixture.threshold_sets==before_swipe);pthread_mutex_unlock(&fixture.mutex);
    lv_obj_scroll_to_y(panel,2000,LV_ANIM_OFF);run(120);lv_area_t last,panel_area;
    lv_obj_get_coords(jitter_slider('6',true),&last);lv_obj_get_coords(panel,&panel_area);
    assert(last.y1>=panel_area.y1 && last.y2<=panel_area.y2); /* Last gear is reachable. */
    lv_obj_scroll_to_y(panel,0,LV_ANIM_OFF);run(120);
    click("6");run(400);lv_obj_get_coords(jitter_slider('6',true),&last);
    assert(last.y1>=panel_area.y1 && last.y2<=panel_area.y2); /* Knob selection jumps to its sliders. */
    lv_obj_scroll_to_y(panel,0,LV_ANIM_OFF);run(120);
    for(unsigned i=0;i<8;i++) {
        assert(lv_slider_get_min_value(jitter_slider(VEHICLE_CAL_GEARS[i],false))==1);
        assert(lv_slider_get_max_value(jitter_slider(VEHICLE_CAL_GEARS[i],true))==180000);
        assert(lv_slider_get_value(jitter_slider(VEHICLE_CAL_GEARS[i],false))==8000);
    }
    angles(0,0,false); /* Radius tuning must work without fresh measurements. */
    slide('R',false,3000);slide('R',true,5000);
    hz_gear_cal_snapshot(&state);assert(state.dirty && state.table.thresholds[0].entry==3000 && state.table.thresholds[0].exit==5000);
    pthread_mutex_lock(&fixture.mutex);assert(fixture.saved_thresholds[0].entry==8000);fixture.reject_thresholds=true;pthread_mutex_unlock(&fixture.mutex);
    slide('R',false,6000); /* Entry crossing exit moves the paired radius, but BEL restores both. */
    assert(lv_slider_get_value(jitter_slider('R',false))==3000 && lv_slider_get_value(jitter_slider('R',true))==5000);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_thresholds=false;pthread_mutex_unlock(&fixture.mutex);
    slide('N',false,6000);slide('N',true,4000); /* Tuning before capture; moving exit also lowers entry. */
    hz_gear_cal_snapshot(&state);assert(state.table.thresholds[1].entry==4000 && state.table.thresholds[1].exit==4000);
    click("Show position calibration");
    angles(12500,-4250,true);click("1");
    click("Save position");assert(!fixture.captures); /* Disabled until held steady. */
    run(2300);assert(!lv_obj_has_state(control("Save position"),LV_STATE_DISABLED));
    pthread_mutex_lock(&fixture.mutex);fixture.reject_capture=true;pthread_mutex_unlock(&fixture.mutex);
    click("Save position");done();green("1",false);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_capture=false;pthread_mutex_unlock(&fixture.mutex);
    angles(12500,-4250,true);run(2300);click("Save position");done();green("1",true);
    click("Show jitter correction");slide('R',true,9000);
    assert(strstr(lv_label_get_text(lv_obj_get_child(jitter_row('R'),4)),"Entry overlap: none | Exit overlap: 1"));
    slide('R',false,6000);
    assert(strstr(lv_label_get_text(lv_obj_get_child(jitter_row('R'),4)),"Entry overlap: 1"));
    slide('R',true,14000);
    assert(strstr(lv_label_get_text(lv_obj_get_child(jitter_row('R'),4)),"Exit reaches center: 1"));
    assert(lv_obj_get_style_text_color(lv_obj_get_child(jitter_row('R'),4),0).full==lv_color_hex(0xff8585).full);
    slide('R',false,3000);slide('R',true,5000);click("Show position calibration");
    pthread_mutex_lock(&fixture.mutex);assert(!fixture.saved[2].set);pthread_mutex_unlock(&fixture.mutex);
    angles(0,0,false);run(120);assert(lv_obj_has_state(control("Save position"),LV_STATE_DISABLED));
    const char *remaining[]={"N","2","3","4","5","6"};
    for(unsigned i=0;i<6;i++) {
        angles(2000+(int)i*1000,-6000+(int)i*1500,true);click(remaining[i]);run(2300);
        click("Save position");done();green(remaining[i],true);
    }
    click("Show jitter correction");screenshot("out/target-ui/tests/gear-jitter.ppm");click("Show position calibration");
    pthread_mutex_lock(&fixture.mutex);fixture.reject_save=true;pthread_mutex_unlock(&fixture.mutex);
    click("Finish & save");done();hz_gear_cal_snapshot(&state);assert(state.active && state.dirty);
    pthread_mutex_lock(&fixture.mutex);fixture.reject_save=false;pthread_mutex_unlock(&fixture.mutex);
    click("Finish & save");done();hz_gear_cal_snapshot(&state);assert(!state.active && !state.dirty);
    screenshot("out/target-ui/tests/gear-calibration.ppm");
    click("Show jitter correction");
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.saved_thresholds[0].entry==3000 && fixture.saved_thresholds[0].exit==5000 &&
        fixture.saved_thresholds[1].entry==4000 && fixture.saved_thresholds[1].exit==4000);
    fixture.active=true;memcpy(fixture.staged,fixture.saved,sizeof(fixture.staged));
    memcpy(fixture.staged_thresholds,fixture.saved_thresholds,sizeof(fixture.staged_thresholds));pthread_mutex_unlock(&fixture.mutex);
    click("Start / resume");done();hz_gear_cal_snapshot(&state);assert(state.active); /* Adopt GB BEL + GL after UI restart. */
    slide('R',true,4000);click("Show position calibration");
    angles(45000,50000,true);click("1");run(2300);click("Save position");done();
    click("Cancel / back");done();assert(lv_scr_act()!=screen);
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.saved[2].forward==12500 && fixture.saved[2].side==-4250 && fixture.saved_thresholds[0].exit==5000);
    pthread_mutex_unlock(&fixture.mutex);
    atomic_store(&stopped,true);pthread_join(owner,NULL);close(listener);assert(!unlink(argv[1]));
    puts("PASS: compact round knob, gear ordering, fresh/stable capture gating, green only after ACK, GL restore, rejected capture/save, explicit persistence and cancel rollback");
    puts("PASS: all eight entry/exit sliders, touch scrolling without writes, release-only GT, no freshness dependency, paired limits, BEL rollback, overlap severity, threshold persistence and cancel rollback");
    return 0;
}
