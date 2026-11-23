/* SPDX-License-Identifier: GPL-2.0-only */
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/sw/lv_draw_sw_gradient.h"
#include "system_fonts.h"
#include "navigation.h"
#include "vehicle_ui.h"
#include "page_art.h"
#include "settings_store.h"
#include "settings.h"
#include "page_header.h"
#include "music_player.h"
#include "bt_music.h"
#include "android_auto.h"
#include "vehicle_body.h"
#include "mode/Car_Launcher.h"
#include "UI/ui.h"
#include <spawn.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>

static unsigned rendered;
/* The comparison runs are separate processes; wall-clock minute changes must
 * not masquerade as a rendering mismatch. CAN expiry still uses monotonic time. */
time_t __wrap_time(time_t *out) {time_t fixed=1792756800;if(out)*out=fixed;return fixed;}
static unsigned char frame[600][1024][3];
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *pixels)
{
    for(int y=area->y1;y<=area->y2;y++) for(int x=area->x1;x<=area->x2;x++) {
        frame[y][x][0]=pixels->ch.red;frame[y][x][1]=pixels->ch.green;frame[y][x][2]=pixels->ch.blue;pixels++;
    }
    rendered++;lv_disp_flush_ready(drv);
}
static void save_frame(const char *path)
{
    FILE *file=fopen(path,"wb");assert(file);
    fprintf(file,"P6\n1024 600\n255\n");assert(fwrite(frame,1,sizeof(frame),file)==sizeof(frame));fclose(file);
}
static void no_gradients(lv_obj_t *object)
{
    assert(lv_obj_get_style_bg_grad_dir(object,0)==LV_GRAD_DIR_NONE);
    for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)no_gradients(lv_obj_get_child(object,i));
}
static void black_background(lv_obj_t *object)
{
    lv_color_t color=lv_obj_get_style_bg_color(object,0);
    assert(!color.ch.red && !color.ch.green && !color.ch.blue);no_gradients(object);
}
static lv_obj_t *find_label(lv_obj_t *obj,const char *text)
{
    if(lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),text))return obj;
    for(uint32_t i=0;i<lv_obj_get_child_cnt(obj);i++) {
        lv_obj_t *found=find_label(lv_obj_get_child(obj,i),text);if(found)return found;
    }
    return NULL;
}
static void run(unsigned milliseconds)
{
    for(unsigned i=0;i<milliseconds;i+=10) {
        lv_tick_inc(10);preview_vehicle_poll();hz_ui_poll();lv_timer_handler();usleep(10000);
    }
}
static lv_obj_t *header_button(lv_obj_t *screen,const char *symbol)
{
    lv_obj_t *text=find_label(screen,symbol);assert(text);
    lv_obj_t *button=lv_obj_get_parent(text),*bar=lv_obj_get_parent(button);
    assert(lv_obj_get_parent(bar)==screen && lv_obj_get_width(bar)==1024 && lv_obj_get_height(bar)==HZ_HEADER_HEIGHT);
    return button;
}
static void check_header(lv_obj_t *screen,const char *title)
{
    lv_obj_t *button=header_button(screen,LV_SYMBOL_HOME),*bar=lv_obj_get_parent(button);
    assert(find_label(bar,title));assert(lv_obj_get_x(bar)==0 && lv_obj_get_y(bar)==0);
    assert(lv_obj_get_style_bg_color(bar,0).full==lv_color_black().full);
    assert(!find_label(bar,LV_SYMBOL_LEFT));
    const char *symbols[]={LV_SYMBOL_HOME};
    for(unsigned i=0;i<1;i++) {
        button=header_button(screen,symbols[i]);
        assert(lv_obj_get_parent(button)==bar && lv_obj_get_width(button)==64 && lv_obj_get_height(button)==50);
        lv_area_t bounds;lv_obj_get_coords(button,&bounds);
        lv_point_t point={(bounds.x1+bounds.x2)/2,(bounds.y1+bounds.y2)/2};
        assert(lv_indev_search_obj(screen,&point)==button);
    }
}
static void press_key(const char *text)
{
    lv_obj_t *caption=find_label(lv_layer_top(),text);assert(caption);
    lv_event_send(lv_obj_get_parent(caption),LV_EVENT_CLICKED,NULL);
}
int preview_rk_page(const char *page);
static lv_obj_t *find_type(lv_obj_t *object,const lv_obj_class_t *type)
{
    if(lv_obj_check_type(object,type))return object;
    for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++) {
        lv_obj_t *found=find_type(lv_obj_get_child(object,i),type);if(found)return found;
    }
    return NULL;
}
static void settings_category(const char *name)
{
    lv_obj_t *text=find_label(lv_scr_act(),name);assert(text);
    /* The title can share the category name; find the sidebar by geometry. */
    lv_obj_t *sidebar=lv_obj_get_child(lv_scr_act(),1);text=find_label(sidebar,name);assert(text);
    lv_obj_t *button=lv_obj_get_parent(text);lv_area_t bounds;lv_obj_get_coords(button,&bounds);
    assert(bounds.x1>=0 && bounds.x1<=1 && bounds.x2==334 && bounds.y1>=64 && bounds.y2<600);
    lv_point_t point={200,(bounds.y1+bounds.y2)/2};
    assert(lv_indev_search_obj(lv_scr_act(),&point)==button);
    lv_event_send(button,LV_EVENT_CLICKED,NULL);run(110);
    assert(lv_obj_has_state(button,LV_STATE_CHECKED));
}
static lv_point_t pointer_position;
static lv_indev_state_t pointer_state=LV_INDEV_STATE_RELEASED;
static void read_scroll_pointer(lv_indev_drv_t *driver,lv_indev_data_t *data)
{
    (void)driver;data->point=pointer_position;data->state=pointer_state;
}
static void drag_up(int x,int y,int distance)
{
    pointer_position=(lv_point_t){x,y};pointer_state=LV_INDEV_STATE_PRESSED;run(40);
    for(int step=1;step<=4;step++) {pointer_position.y=y-distance*step/4;run(40);}
    pointer_state=LV_INDEV_STATE_RELEASED;run(400);
}
static void check_scrollbar_gutter(lv_obj_t *viewport)
{
    lv_obj_update_layout(viewport);lv_area_t horizontal,vertical;
    lv_obj_get_scrollbar_area(viewport,&horizontal,&vertical);
    assert(vertical.x1<=vertical.x2 && vertical.y1<=vertical.y2);
    for(unsigned i=0;i<lv_obj_get_child_cnt(viewport);i++) {
        lv_obj_t *child=lv_obj_get_child(viewport,i);lv_area_t bounds;lv_obj_get_coords(child,&bounds);
        assert(bounds.x2+_lv_obj_get_ext_draw_size(child)<vertical.x1);
    }
}
static void check_settings_scrolling(lv_obj_t *settings)
{
    lv_obj_t *sidebar=lv_obj_get_child(settings,1);
    settings_category("Basic Setup");
    lv_obj_t *pane=lv_obj_get_parent(lv_obj_get_parent(find_label(settings,"Tank capacity")));
    assert(lv_obj_has_flag(sidebar,LV_OBJ_FLAG_SCROLLABLE) && lv_obj_has_flag(pane,LV_OBJ_FLAG_SCROLLABLE));
    assert(lv_obj_get_scroll_dir(sidebar)==LV_DIR_VER && lv_obj_get_scroll_dir(pane)==LV_DIR_VER);
    /* Exercise overflowing viewports without adding artificial settings. */
    lv_obj_set_height(sidebar,300);lv_obj_set_height(pane,230);lv_obj_update_layout(settings);
    check_scrollbar_gutter(sidebar);check_scrollbar_gutter(pane);
    static lv_indev_drv_t driver;lv_indev_drv_init(&driver);driver.type=LV_INDEV_TYPE_POINTER;
    driver.read_cb=read_scroll_pointer;lv_indev_t *input=lv_indev_drv_register(&driver);
    drag_up(200,280,128);
    assert(lv_obj_get_scroll_y(sidebar)>0 && lv_obj_get_scroll_y(pane)==0);
    assert(hz_settings_scroll_at(200,150,-10000));run(40);assert(lv_obj_get_scroll_y(sidebar)==0);
    drag_up(700,280,128);
    assert(lv_obj_get_scroll_y(pane)>0 && lv_obj_get_scroll_y(sidebar)==0);
    int right=lv_obj_get_scroll_y(pane);
    assert(hz_settings_scroll_at(700,150,-48));run(40);assert(lv_obj_get_scroll_y(pane)<right);
    right=lv_obj_get_scroll_y(pane);
    assert(hz_settings_scroll_at(200,180,48));run(40);
    assert(lv_obj_get_scroll_y(sidebar)>0 && lv_obj_get_scroll_y(pane)==right);
    assert(!hz_settings_scroll_at(400,32,48) && !hz_settings_scroll_at(500,560,48));
    lv_obj_t *cover=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(cover);
    lv_obj_set_pos(cover,0,0);lv_obj_set_size(cover,1024,600);lv_obj_update_layout(cover);
    assert(!hz_settings_scroll_at(700,150,48));lv_obj_del(cover);
    assert(lv_obj_get_scroll_y(settings)==0 && lv_obj_get_y(lv_obj_get_child(settings,0))==0);
    lv_obj_scroll_to_y(sidebar,0,LV_ANIM_OFF);lv_obj_set_height(sidebar,536);
    settings_category("Backlight control");assert(lv_obj_get_scroll_y(pane)==0);
    /* A vertical swipe starting on a slider must scroll, never commit a value. */
    lv_obj_set_height(pane,240);lv_obj_update_layout(settings);
    check_scrollbar_gutter(pane);
    struct hz_settings_snapshot before,after;hz_settings_snapshot(&before);
    drag_up(900,208,80);hz_settings_snapshot(&after);
    assert(lv_obj_get_scroll_y(pane)>0 && before.values.brightness==after.values.brightness);
    lv_indev_reset(input,NULL);lv_obj_set_height(pane,422);settings_category("Basic Setup");run(110);
    lv_obj_invalidate(settings);lv_refr_now(NULL);
    for(unsigned y=64;y<600;y++)assert(!memcmp(frame[y][351],frame[64][351],3));
    puts("PASS: independent vertical touch/wheel scrolling, fixed header/footer/divider, category reset and slider swipe protection");
}
static void check_settings(void)
{
    assert(!preview_rk_page("settings"));run(400);lv_obj_t *settings=lv_scr_act();
    assert(lv_obj_get_width(settings)==1024 && lv_obj_get_height(settings)==600);
    assert(!hz_page_art_ready(settings));
    lv_obj_invalidate(settings);lv_refr_now(NULL);
    for(unsigned y=64;y<600;y++)assert(!memcmp(frame[y][351],frame[64][351],3));
    settings_category("Backlight control");
    lv_obj_t *slider=find_type(settings,&lv_slider_class);assert(slider);
    struct hz_settings_snapshot state;hz_settings_snapshot(&state);assert(state.values.brightness==128);
    lv_slider_set_value(slider,64,LV_ANIM_OFF);lv_event_send(slider,LV_EVENT_VALUE_CHANGED,NULL);
    hz_settings_snapshot(&state);assert(state.values.brightness==128); /* Dragging never commits. */
    lv_event_send(slider,LV_EVENT_RELEASED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.brightness==64 && find_label(settings,"25%"));
    lv_obj_t *dimmed=NULL,*display_pane=lv_obj_get_parent(slider);
    for(unsigned i=0;i<lv_obj_get_child_cnt(display_pane);i++) {
        lv_obj_t *child=lv_obj_get_child(display_pane,i);
        if(child!=slider && lv_obj_check_type(child,&lv_slider_class))dimmed=child;
    }
    assert(dimmed && lv_slider_get_value(dimmed)==64);
    lv_slider_set_value(dimmed,32,LV_ANIM_OFF);lv_event_send(dimmed,LV_EVENT_RELEASED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.dimmed_brightness==32 && find_label(settings,"13%"));
    hz_settings_lights(true);run(110);assert(find_label(settings,"Vehicle lights: on"));
    hz_settings_lights(false);run(110);assert(find_label(settings,"Vehicle lights: off"));
    assert(hz_settings_change(HZ_DIMMED_BRIGHTNESS,64));run(110);
    lv_refr_now(NULL);save_frame("out/target-ui/tests/settings-display.ppm");
    settings_category("Sound");slider=find_type(settings,&lv_slider_class);assert(slider);
    check_scrollbar_gutter(lv_obj_get_parent(slider));
    lv_slider_set_value(slider,9,LV_ANIM_OFF);lv_event_send(slider,LV_EVENT_RELEASED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.volume==9 && find_label(settings,"9 / 20"));
    lv_obj_t *mute_row=lv_obj_get_parent(find_label(settings,"Mute"));
    lv_obj_t *mute=lv_obj_get_child(mute_row,1);lv_obj_add_state(mute,LV_STATE_CHECKED);
    lv_event_send(mute,LV_EVENT_VALUE_CHANGED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.mute==1 && find_label(mute,"ON"));
    lv_refr_now(NULL);save_frame("out/target-ui/tests/settings-sound.ppm");
    settings_category("Connections");
    lv_obj_t *name=find_label(settings,"SWL-BT  " LV_SYMBOL_RIGHT);assert(name);
    /* Tap the name row: on-screen keyboard; Save renames, store reflects the reported name. */
    lv_event_send(lv_obj_get_parent(name),LV_EVENT_CLICKED,NULL);run(30);
    assert(find_label(lv_layer_top(),"SWL-BT_"));
    for(int i=0;i<6;i++)press_key("Delete");
    press_key("aA");assert(find_label(lv_layer_top(),"q"));press_key("aA"); /* Shift toggles letter case. */
    const char *typed[]={"H","Z","-","B","5","0","0"};for(int i=0;i<7;i++)press_key(typed[i]);
    assert(find_label(lv_layer_top(),"HZ-B500_"));
    lv_refr_now(NULL);save_frame("out/target-ui/tests/settings-bt-name.ppm");
    press_key(LV_SYMBOL_OK " Save");run(110);
    assert(!find_label(lv_layer_top(),"HZ-B500_"));
    hz_settings_snapshot(&state);assert(!strcmp(state.values.bluetooth_name,"HZ-B500"));
    assert(find_label(settings,"HZ-B500  " LV_SYMBOL_RIGHT));
    assert(hz_settings_rename("SWL-BT") && !hz_settings_rename("bad name") && !hz_settings_rename(""));run(110);
    lv_obj_t *bluetooth=lv_obj_get_child(lv_obj_get_parent(find_label(settings,"Allow connections")),1);
    assert(find_label(bluetooth,"OFF"));lv_obj_add_state(bluetooth,LV_STATE_CHECKED);
    lv_event_send(bluetooth,LV_EVENT_VALUE_CHANGED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.bluetooth==1 && find_label(bluetooth,"ON"));
    lv_refr_now(NULL);save_frame("out/target-ui/tests/settings-connections.ppm");
    assert(hz_settings_change(HZ_BLUETOOTH,0));run(110);
    /* Picture: pattern and all four sliders fit without scrolling; dragging
     * only previews, releasing saves. */
    settings_category("Picture");
    lv_obj_t *picture_pane=lv_obj_get_parent(find_label(settings,"Saturation"));
    while(picture_pane && lv_obj_get_height(picture_pane)!=422)picture_pane=lv_obj_get_parent(picture_pane);
    assert(picture_pane);lv_obj_t *picture[4];unsigned pictures=0;
    for(unsigned i=0;i<lv_obj_get_child_cnt(picture_pane);i++) {
        lv_obj_t *child=lv_obj_get_child(picture_pane,i);
        if(lv_obj_check_type(child,&lv_slider_class)) {assert(pictures<4);picture[pictures++]=child;}
    }
    assert(pictures==4);lv_obj_update_layout(settings);
    for(unsigned i=0;i<4;i++)assert(lv_slider_get_value(picture[i])==50 && lv_obj_get_y(picture[i])+12<=410);
    lv_slider_set_value(picture[3],70,LV_ANIM_OFF);lv_event_send(picture[3],LV_EVENT_VALUE_CHANGED,NULL);
    hz_settings_snapshot(&state);assert(state.values.picture[3]==50 && find_label(settings,"70"));
    lv_event_send(picture[3],LV_EVENT_RELEASED,NULL);run(110);
    hz_settings_snapshot(&state);assert(state.values.picture[3]==70 && state.values.picture[0]==50);
    lv_refr_now(NULL);save_frame("out/target-ui/tests/settings-picture.ppm");
    assert(hz_settings_change(HZ_LCD_SATURATION,50));run(110);
    settings_category("System information");assert(find_label(settings,"Allwinner F133 / D1s"));
    assert(!find_label(settings,"Platform:  RK3568 Linux"));
    hz_home_open();run(400);assert(!preview_rk_page("settings"));run(400);
    settings_category("Backlight control");assert(find_label(settings,"25%"));
    assert(hz_settings_change(HZ_BRIGHTNESS,128));assert(hz_settings_change(HZ_VOLUME,18));assert(hz_settings_change(HZ_MUTE,0));
    settings_category("Basic Setup");assert(find_label(settings,"63 litres") && find_label(settings,"4000 Hz"));
    check_settings_scrolling(settings);
    lv_obj_t *bar=lv_obj_get_child(settings,0),*home=lv_obj_get_child(bar,0);
    lv_event_send(home,LV_EVENT_CLICKED,NULL);run(400);assert(lv_scr_act()==Car_Launcher_Get_Screen());
    puts("PASS: split settings sidebar hit targets, live category changes, release-only brightness, volume/mute, navigation and session retention");
}
static void check_prepared_pages(void)
{
    const char *pages[]={"settings"};
    const char *titles[]={"Basic Setup"};
    for(unsigned page=0;page<1;page++) {
        hz_home_open();run(400);assert(!preview_rk_page(pages[page]));run(400);
        char path[160];snprintf(path,sizeof(path),"out/target-ui/tests/page-%s-%s.ppm",
            getenv("HZ_UI_STATIC_CACHE")?"live":"cached",pages[page]);
        lv_obj_invalidate(lv_scr_act());lv_refr_now(NULL);save_frame(path);
        black_background(lv_scr_act());
        check_header(lv_scr_act(),titles[page]);
        const unsigned corners[][2]={{0,0},{0,599},{1023,599}};
        for(unsigned i=0;i<3;i++) {
            const unsigned char *pixel=frame[corners[i][1]][corners[i][0]];
            assert(!pixel[0] && !pixel[1] && !pixel[2]);
        }
    }
    hz_home_open();run(400);
}
static void check_icon_colors(lv_obj_t *home)
{
    unsigned char *normal=malloc(sizeof(frame)),*pressed=malloc(sizeof(frame));assert(normal && pressed);
    lv_obj_invalidate(home);lv_refr_now(NULL);memcpy(normal,frame,sizeof(frame));
    /* The reference uses actual LVGL with its gradient cache disabled. Comparing
     * two background-cache modes alone would reproduce the same color bug. */
    lv_gradient_set_cache_size(0);lv_obj_invalidate(home);lv_refr_now(NULL);
    assert(!memcmp(normal,frame,sizeof(frame)));
    lv_gradient_set_cache_size(LV_GRAD_CACHE_DEF_SIZE);
    unsigned buttons=0;
    for(uint32_t i=0;i<lv_obj_get_child_cnt(home);i++) {
        lv_obj_t *button=lv_obj_get_child(home,i);
        if(!lv_obj_check_type(button,&lv_btn_class))continue;
        lv_area_t bounds;lv_obj_get_coords(button,&bounds);
        lv_point_t point={(bounds.x1+bounds.x2)/2,(bounds.y1+bounds.y2)/2};
        lv_obj_t *hit=lv_indev_search_obj(home,&point);
        while(hit && hit!=button)hit=lv_obj_get_parent(hit);
        assert(hit==button); /* Invisible static widgets retain real touch targets. */
        buttons++;
        lv_obj_add_state(button,LV_STATE_PRESSED);
        lv_event_send(button,LV_EVENT_PRESSED,NULL);
        for(unsigned tick=0;tick<220;tick+=20) {lv_tick_inc(20);lv_anim_refr_now();lv_refr_now(NULL);}
        memcpy(pressed,frame,sizeof(frame));
        char path[160];snprintf(path,sizeof(path),"out/target-ui/tests/press-%s-%u.ppm",getenv("HZ_UI_STATIC_CACHE")?"live":"cached",buttons-1);save_frame(path);
        lv_gradient_set_cache_size(0);lv_obj_invalidate(home);lv_refr_now(NULL);
        assert(!memcmp(pressed,frame,sizeof(frame)));
        lv_gradient_set_cache_size(LV_GRAD_CACHE_DEF_SIZE);
        lv_obj_clear_state(button,LV_STATE_PRESSED);
        lv_event_send(button,LV_EVENT_RELEASED,NULL);
        for(unsigned tick=0;tick<220;tick+=20) {lv_tick_inc(20);lv_anim_refr_now();lv_refr_now(NULL);}
        assert(!memcmp(normal,frame,sizeof(frame)));
    }
    assert(buttons==7);free(pressed);free(normal);
    puts("PASS: all seven icon colors and pressed/released partial redraws match uncached full-frame rendering");
}
static const char *socket_path,*ubus_program;
extern char **environ;
static void rpc(const char *method,char *json)
{
    char *args[]={(char *)ubus_program,"-s",(char *)socket_path,"call","vehicle",(char *)method,json,NULL};
    pid_t pid;int status;
    assert(!posix_spawn(&pid,ubus_program,NULL,NULL,args,environ));
    assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static unsigned colored(unsigned x1,unsigned y1,unsigned x2,unsigned y2,unsigned channel)
{
    unsigned count=0;
    for(unsigned y=y1;y<y2;y++)for(unsigned x=x1;x<x2;x++) {
        const unsigned char *p=frame[y][x];
        if(p[channel]>p[(channel+1)%3]+50 && p[channel]>p[(channel+2)%3]+50)count++;
    }
    return count;
}
static unsigned char lights_off[600][1024][3];
/* Pixels that became brighter than the lights-off frame without gaining a hue:
 * the beam artwork is drawn white. */
static unsigned brightened_neutral(unsigned x1,unsigned y1,unsigned x2,unsigned y2)
{
    unsigned count=0;
    for(unsigned y=y1;y<y2;y++)for(unsigned x=x1;x<x2;x++) {
        const unsigned char *p=frame[y][x],*q=lights_off[y][x];
        unsigned high=p[0],low=p[0];
        for(unsigned c=1;c<3;c++) {if(p[c]>high)high=p[c];if(p[c]<low)low=p[c];}
        if(p[0]+p[1]+p[2]>q[0]+q[1]+q[2]+30u && high-low<=12)count++;
    }
    return count;
}
static struct vehicle_decoder body_decoder;
static bool body_in_process;
static void body_inject(unsigned id,const char *data)
{
    if(!body_in_process) {
        char json[80];snprintf(json,sizeof(json),"{\"id\":%u,\"data\":\"%s\"}",id,data);rpc("inject",json);return;
    }
    struct can_frame frame={.can_id=id,.can_dlc=strlen(data)/2};struct vehicle_event event;
    assert(frame.can_dlc<=8);
    for(unsigned i=0;i<frame.can_dlc;i++)assert(sscanf(data+2*i,"%2hhx",&frame.data[i])==1);
    vehicle_frame(&body_decoder,&frame,lv_tick_get(),&event);
    hz_vehicle_body_update(&body_decoder.state);
}
static void check_vehicle_body(lv_obj_t *home,bool in_process)
{
    body_in_process=in_process;if(in_process)vehicle_init(&body_decoder,"body-render",true);
    run(400); /* Finish the return-to-Home slide before inspecting body pixels. */
    assert(!find_label(home,"Doors unknown") && !find_label(home,"Lights unknown"));
    unsigned body_brightness=frame[400][180][0];assert(body_brightness>150);
    body_inject(0x2fc,"000000");body_inject(0x21a,"00");run(100);
    assert(find_label(home,"All doors closed") && find_label(home,"Lights off"));
    assert(frame[330][180][0]>40); /* Actual body pixels replace the clock panel. */
    assert(frame[400][180][0]==body_brightness); /* Unknown state must not dim the car. */
    const unsigned door_bytes[]={1,4,16,64};
    const unsigned areas[][4]={{65,310,145,380},{235,310,300,380},{75,380,150,450},{225,380,285,450}};
    for(unsigned i=0;i<4;i++) {
        char data[16];snprintf(data,sizeof(data),"00%02X00",door_bytes[i]);
        body_inject(0x2fc,data);run(100);assert(find_label(home,"1 door open"));
        assert(colored(areas[i][0],areas[i][1],areas[i][2],areas[i][3],0)>25);
    }
    body_inject(0x2fc,"005501");run(100);
    assert(find_label(home,"4 doors open, trunk open") && colored(140,468,225,508,0)>100);
    memcpy(lights_off,frame,sizeof(frame));
    body_inject(0x21a,"04");run(100);
    assert(find_label(home,"Low beam") && brightened_neutral(0,100,336,205)>100);
    lv_refr_now(NULL);save_frame("out/target-ui/tests/home-doors-low-beam.ppm");
    body_inject(0x21a,"07");run(100);
    assert(find_label(home,"High beam") && brightened_neutral(0,100,336,205)>100);
    lv_refr_now(NULL);save_frame("out/target-ui/tests/home-doors-high-beam.ppm");
    body_inject(0x2fc,"000000");body_inject(0x21a,"00");run(100);
    assert(find_label(home,"All doors closed") && find_label(home,"Lights off"));
    /* Turn signals blink in the artwork's amber, not recolored white. */
    unsigned amber=0;
    for(unsigned step=0;step<12 && amber<=100;step++) {
        body_inject(0x1f6,"30");run(100);amber=0;
        for(unsigned y=84;y<568;y++)for(unsigned x=16;x<352;x++) {
            const unsigned char *p=frame[y][x];
            if(p[0]>180 && p[1]>140 && p[2]<90)amber++;
        }
    }
    assert(amber>100);
    body_inject(0x1f6,"00");run(100);
    if(in_process) {vehicle_disconnect(&body_decoder);hz_vehicle_body_update(&body_decoder.state);}
    puts("PASS: CAN drives all four spatial door indicators, trunk polygon and low/high beam artwork without unknown-state captions");
}
/* Compare rendered hand pixels with the artwork's ticks, independently of
 * the production value-to-angle formula. */
static void check_speed_alignment(void)
{
    run(400); /* Finish the navigation animation before sampling screen pixels. */
    const lv_img_dsc_t *dial=&ui_img_837434941;
    const int speeds[]={80,0,160,220};
    lv_area_t area;lv_obj_get_coords(ui_KmTable,&area);
    unsigned char *background=malloc(sizeof(frame));assert(background);
    double worst=0;
    for(unsigned i=0;i<sizeof(speeds)/sizeof(speeds[0]);i++) {
        int speed=speeds[i];char json[80];
        snprintf(json,sizeof(json),"{\"signal\":\"speed_kph\",\"value\":%d}",speed);
        rpc("set",json);run(100);
        lv_obj_add_flag(ui_KmPoint,LV_OBJ_FLAG_HIDDEN);lv_refr_now(NULL);
        memcpy(background,frame,sizeof(frame));
        lv_obj_clear_flag(ui_KmPoint,LV_OBJ_FLAG_HIDDEN);lv_refr_now(NULL);
        double nominal=(150.0+speed*240.0/220.0)*M_PI/180.0;
        double tx=0,ty=0,hx=0,hy=0;unsigned ticks=0,hand=0;
        for(int y=0;y<275;y++)for(int x=0;x<275;x++) {
            double dx=x-137.0,dy=y-137.0,r=hypot(dx,dy);
            if(fabs(remainder(atan2(dy,dx)-nominal,2*M_PI))>10*M_PI/180)continue;
            const unsigned char *p=dial->data+4*(y*275+x);
            if(r>116 && r<124 && fabs(remainder(atan2(dy,dx)-nominal,2*M_PI))<4*M_PI/180 &&
               p[0]>180 && p[1]>180 && p[2]>180 && p[3]>180) {tx+=dx;ty+=dy;ticks++;}
            int sx=area.x1+x,sy=area.y1+y;
            const unsigned char *actual=frame[sy][sx],*before=background+3*(sy*1024+sx);
            if(r>50 && r<95 && actual[0]>180 && actual[1]>180 && actual[2]>180 &&
               (actual[0]>before[0]+30 || actual[1]>before[1]+30 || actual[2]>before[2]+30)) {hx+=dx;hy+=dy;hand++;}
        }
        assert(ticks>10 && hand>30);
        double error=remainder(atan2(hy,hx)-atan2(ty,tx),2*M_PI)*180/M_PI;
        fprintf(stderr,"Speed hand at %d km/h: %.3f degrees from printed tick\n",speed,error);
        if(fabs(error)>worst)worst=fabs(error);
        char path[100];snprintf(path,sizeof(path),"out/target-ui/tests/speed-%d.ppm",speed);save_frame(path);
    }
    free(background);
    assert(worst<0.5);
    puts("PASS: rendered speed hand aligns with artwork ticks at 0, 80, 160 and 220 km/h");
}
static void set_pdc(int value)
{
    rpc("inject",value?"{\"id\":586,\"data\":\"05\"}":"{\"id\":586,\"data\":\"06\"}");
}
void Car_Dashboard_Vehicle_Mode(void);
int main(int argc,char **argv)
{
    int demo=argc==2 && !strcmp(argv[1],"--demo");
    assert(argc==3 || demo);socket_path=argv[1];if(!demo)ubus_program=argv[2];
    lv_init();dashboard_fonts_init();
    static lv_color_t pixels[1024*40];static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer,pixels,NULL,1024*40);
    static lv_disp_drv_t display;lv_disp_drv_init(&display);
    display.draw_buf=&buffer;display.flush_cb=flush;display.hor_res=1024;display.ver_res=600;
    lv_disp_drv_register(&display);
    hz_ui_set_vehicle_mode(!demo);
    if(!demo)Car_Dashboard_Vehicle_Mode();
    Car_Launcher_Gui();hz_home_open();
    lv_obj_t *home=lv_scr_act();assert(home==Car_Launcher_Get_Screen());
    black_background(home);
    lv_refr_now(NULL);save_frame(getenv("HZ_UI_STATIC_CACHE")?"out/target-ui/tests/home-software.ppm":"out/target-ui/tests/home-cached.ppm");
    lv_obj_t *home_header=lv_obj_get_parent(find_label(home,"Home"));
    assert(lv_obj_get_parent(home_header)==home && lv_obj_get_height(home_header)==HZ_HEADER_HEIGHT);
    assert(!find_label(home_header,LV_SYMBOL_HOME) && !find_label(home_header,LV_SYMBOL_LEFT));
    check_icon_colors(home);
    /* Without a companion driver the BT Music page says so instead of inventing a phone. */
    hz_bt_music_open();run(400);
    assert(lv_scr_act()!=home && find_label(lv_scr_act(),"BT Music") &&
           find_label(lv_scr_act(),"Companion service not available") && find_label(lv_scr_act(),LV_SYMBOL_PLAY));
    lv_refr_now(NULL);save_frame("out/target-ui/tests/bt-music.ppm");
    hz_home_open();run(400);assert(lv_scr_act()==home);
    hz_android_auto_open(true);run(100);
    assert(find_label(lv_scr_act(),"Android Auto") && find_label(lv_scr_act(),"Android Auto runs only on the device."));
    hz_home_open();run(400);assert(lv_scr_act()==home);
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned i=0;i<12;i++) {lv_obj_invalidate(home);lv_refr_now(NULL);}
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("Home full redraw benchmark: %.1f ms/frame (%s cache)\n",
        ((end.tv_sec-begin.tv_sec)*1000.0+(end.tv_nsec-begin.tv_nsec)/1000000.0)/12,
        getenv("HZ_UI_STATIC_CACHE")?"disabled":"enabled");
    assert(!lv_obj_has_flag(home,LV_OBJ_FLAG_SCROLLABLE));
    assert(!find_label(home,"PDC"));assert(!find_label(home,"Parking sensors"));
    assert(!find_label(home,"LED") && !find_label(home,"Gallery") && !find_label(home,"Clock"));
    lv_obj_t *dashboard=find_label(home,"Dashboard");assert(dashboard);
    lv_obj_t *music=find_label(home,"Music");assert(music);
    lv_event_send(music,LV_EVENT_CLICKED,NULL);
    for(unsigned elapsed=20;elapsed<=320;elapsed+=20) {
        lv_tick_inc(20);lv_anim_refr_now();lv_refr_now(NULL);
        if(elapsed==80 || elapsed==160 || elapsed==240) {
            char path[160];snprintf(path,sizeof(path),"out/target-ui/tests/slide-%s-%u.ppm",getenv("HZ_UI_TRANSITION_CACHE")?"live":"cached",elapsed);
            save_frame(path);
        }
    }
    run(80);
    assert(!lv_obj_has_flag(music,LV_OBJ_FLAG_HIDDEN));
    assert(!lv_disp_get_default()->prev_scr);
    assert(find_label(lv_scr_act(),"Synthwave"));
    check_header(lv_scr_act(),"Music Player");
    black_background(lv_scr_act());
    assert(find_label(lv_scr_act(),"Audio test"));
    assert(find_label(lv_scr_act(),"02:10"));
    assert(find_label(lv_scr_act(),"Ready"));
    lv_obj_t *play=find_label(lv_scr_act(),LV_SYMBOL_PLAY);assert(play);
    lv_event_send(lv_obj_get_parent(play),LV_EVENT_CLICKED,NULL);run(150);
    assert(find_label(lv_scr_act(),"Playback is available on the target"));
    lv_obj_t *music_screen=lv_scr_act();
    hz_home_open();run(400);
    assert(!hz_page_art_ready(music_screen));
    lv_event_send(music,LV_EVENT_CLICKED,NULL);run(400);
    if(!getenv("HZ_UI_STATIC_CACHE"))assert(hz_page_art_ready(music_screen));
    assert(find_label(music_screen,"Playback is available on the target"));
    lv_event_send(header_button(music_screen,LV_SYMBOL_HOME),LV_EVENT_CLICKED,NULL);run(400);
    assert(lv_scr_act()==home && hz_music_poll()->status==HZ_MUSIC_STOPPED);
    assert(!hz_page_art_ready(music_screen));
    lv_obj_t *video=find_label(home,"Video");assert(video);
    lv_event_send(video,LV_EVENT_CLICKED,NULL);run(400);
    assert(find_label(lv_scr_act(),"Big Buck Bunny"));
    assert(!hz_page_art_ready(home)); /* Home's DMA background is released for the decoder. */
    black_background(lv_scr_act());
    check_header(lv_scr_act(),"Big Buck Bunny");
    assert(find_label(lv_scr_act(),"00:00 / 09:56"));
    play=find_label(lv_scr_act(),LV_SYMBOL_PLAY " Play");assert(play);
    lv_event_send(lv_obj_get_parent(play),LV_EVENT_CLICKED,NULL);run(150);
    assert(find_label(lv_scr_act(),"Video playback is available on the target"));
    save_frame("out/target-ui/tests/video.ppm");
    lv_event_send(header_button(lv_scr_act(),LV_SYMBOL_HOME),LV_EVENT_CLICKED,NULL);run(100);
    assert(lv_scr_act()==home);
    if(!getenv("HZ_UI_STATIC_CACHE"))assert(hz_page_art_ready(home));
    if(demo) {
        check_vehicle_body(home,true);
        check_prepared_pages();
        check_settings();
        lv_event_send(dashboard,LV_EVENT_CLICKED,NULL);run(2200);
        lv_obj_t *hands[]={ui_KmPoint,ui_TMeterpoint,ui_TempPoint,ui_OilPoint};
        for(unsigned i=0;i<4;i++)assert(!lv_obj_has_flag(hands[i],LV_OBJ_FLAG_HIDDEN));
        lv_obj_t *canvas=lv_obj_get_parent(ui_Container1);
        assert(lv_obj_get_style_transform_zoom(canvas,0)==256);
        assert(lv_obj_get_x(canvas)==112 && lv_obj_get_y(canvas)==60);
        assert(lv_img_get_zoom(ui_KmTable)==256);
        check_header(lv_scr_act(),"Dashboard");
        save_frame("out/target-ui/tests/dashboard-demo.ppm");
        puts("PASS: shared 64-pixel headers, main HMI without navigation, Home-only subpages and media cleanup");
        puts("PASS: HMI manual preview shows all four Dashboard hands at native 800x480, centred at 112,60 without scaling");
        return 0;
    }
    assert(!preview_vehicle_start(argv[1],0));run(300);
    check_vehicle_body(home,false);
    assert(find_label(home,"--:--"));
    rpc("inject","{\"id\":760,\"data\":\"0E253816AFEA07FF\"}");run(1100);
    assert(find_label(home,"14:37"));
    rpc("inject","{\"id\":760,\"data\":\"FFFFFFFFFFFFFFFF\"}");run(1100);
    assert(find_label(home,"--:--"));
    rpc("inject","{\"id\":760,\"data\":\"0E253816AFEA07FF\"}");run(1100);
    assert(find_label(home,"14:37"));
    lv_obj_t *title=find_label(lv_layer_top(),"Parking sensors unavailable");
    assert(title && lv_obj_has_flag(lv_obj_get_parent(title),LV_OBJ_FLAG_HIDDEN));
    lv_event_send(dashboard,LV_EVENT_CLICKED,NULL);run(200);
    assert(lv_scr_act()==ui_Screen1);assert(lv_obj_get_width(ui_Screen1)==1024);
    assert(lv_obj_get_height(ui_Screen1)==600);
    lv_obj_t *canvas=lv_obj_get_parent(ui_Container1);
    assert(lv_obj_get_width(canvas)==800 && lv_obj_get_height(canvas)==480);
    assert(lv_obj_get_style_transform_zoom(canvas,0)==256);
    assert(lv_obj_get_x(canvas)==112 && lv_obj_get_y(canvas)==60);
    assert(lv_obj_has_flag(ui_TMeterpoint,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(ui_TempPoint,LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(ui_Gear),"-"));
    save_frame("out/target-ui/tests/dashboard.ppm");
    check_speed_alignment();
    rpc("inject","{\"id\":422,\"data\":\"00000000000000F0\"}");
    rpc("inject","{\"id\":422,\"data\":\"80008000800090F1\"}");run(100);
    assert(!lv_obj_has_flag(ui_KmPoint,LV_OBJ_FLAG_HIDDEN));
    assert(lv_img_get_angle(ui_KmPoint)==1100);
    rpc("inject","{\"id\":170,\"data\":\"5F59FF0010278099\"}");run(200);
    assert(!lv_obj_has_flag(ui_TMeterpoint,LV_OBJ_FLAG_HIDDEN));
    assert(lv_img_get_angle(ui_TMeterpoint)==2500*2430/8000);
    run(250); /* the diagnostic file is rewritten at most every 200 ms */
    if(getenv("HZ_UI_DIAGNOSTICS"))
        assert(!rename(getenv("HZ_UI_DIAGNOSTICS"),"out/target-ui/tests/dashboard-diagnostic.json"));
    /* A new RPM target moves through intermediate angles, rather than jumping. */
    rpc("inject","{\"id\":170,\"data\":\"5F59FF00204E8099\"}");
    int old_angle=2500*2430/8000,target_angle=5000*2430/8000;bool intermediate=false;
    for(unsigned i=0;i<15;i++) {
        run(10);int angle=lv_img_get_angle(ui_TMeterpoint);
        if(angle>old_angle && angle<target_angle)intermediate=true;
    }
    assert(intermediate && lv_img_get_angle(ui_TMeterpoint)==target_angle);
    rpc("inject","{\"id\":841,\"data\":\"800C300700\"}");run(200); /* 31.5L / 63L = 50%. */
    assert(!lv_obj_has_flag(ui_OilPoint,LV_OBJ_FLAG_HIDDEN) && lv_img_get_angle(ui_OilPoint)==1350);
    rpc("inject","{\"id\":464,\"data\":\"8BFF63CD5D37CDA8\"}");run(200);
    assert(!lv_obj_has_flag(ui_TempPoint,LV_OBJ_FLAG_HIDDEN) && lv_img_get_angle(ui_TempPoint)==573);
    save_frame("out/target-ui/tests/dashboard-coolant-91.ppm");
    const char gears[]="RN123456";
    for(unsigned i=0;i<sizeof(gears)-1;i++) {
        char request[80];snprintf(request,sizeof(request),"{\"id\":466,\"data\":\"%02X\"}",(unsigned char)gears[i]);
        rpc("inject",request);run(100);
        assert(lv_label_get_text(ui_Gear)[0]==gears[i] && !lv_label_get_text(ui_Gear)[1]);
    }
    save_frame("out/target-ui/tests/dashboard-manual-gear.ppm");
    rpc("inject","{\"id\":466,\"data\":\"D20FFF0CF0FFFFFF\"}");run(100);
    assert(!strcmp(lv_label_get_text(ui_Gear),"6")); /* Ignore the automatic format. */
    rpc("inject","{\"id\":466,\"data\":\"3F\"}");run(100);
    assert(!strcmp(lv_label_get_text(ui_Gear),"-"));
    rpc("inject","{\"id\":466,\"data\":\"52\"}");run(100);
    assert(!strcmp(lv_label_get_text(ui_Gear),"R"));
    run(1100);assert(!strcmp(lv_label_get_text(ui_Gear),"-"));
    rpc("inject","{\"id\":466,\"data\":\"4E\"}");run(100);
    assert(!strcmp(lv_label_get_text(ui_Gear),"N"));
    puts("PASS: private adapter gear R/N/1..6 reaches Dashboard; native frames ignored, unknown/expiry clear it, fresh gear restores it");
    rpc("inject","{\"id\":464,\"data\":\"FFFF63CD5D37CDA8\"}");run(100);
    assert(lv_obj_has_flag(ui_TempPoint,LV_OBJ_FLAG_HIDDEN));
    rpc("inject","{\"id\":464,\"data\":\"8BFF63CD5D37CDA8\"}");run(100);
    assert(!lv_obj_has_flag(ui_TempPoint,LV_OBJ_FLAG_HIDDEN));
    puts("PASS: 0x1D0 published 91 C sample drives the coolant dial; unavailable sample hides it and valid data restores it");
    rpc("inject","{\"id\":538,\"data\":\"07\"}");run(100);
    assert(lv_obj_has_state(ui_High_beam,LV_STATE_CHECKED) && !lv_obj_has_state(ui_Low_beam,LV_STATE_CHECKED));
    rpc("inject","{\"id\":538,\"data\":\"06\"}");run(100);
    assert(lv_obj_has_state(ui_High_beam,LV_STATE_CHECKED) && !lv_obj_has_state(ui_Low_beam,LV_STATE_CHECKED));
    rpc("inject","{\"id\":538,\"data\":\"04\"}");run(100);
    assert(!lv_obj_has_state(ui_High_beam,LV_STATE_CHECKED) && lv_obj_has_state(ui_Low_beam,LV_STATE_CHECKED));
    rpc("inject","{\"id\":538,\"data\":\"00\"}");run(100);
    assert(!lv_obj_has_state(ui_High_beam,LV_STATE_CHECKED) && !lv_obj_has_state(ui_Low_beam,LV_STATE_CHECKED));
    puts("PASS: RPM interpolation, 0x349 63-litre fuel gauge and low/high beam indicators over ubus");
    /* Dashboard Home restores HMI. */
    check_header(ui_Screen1,"Dashboard");
    lv_event_send(header_button(ui_Screen1,LV_SYMBOL_HOME),LV_EVENT_CLICKED,NULL);run(100);assert(lv_scr_act()==home);
    set_pdc(1);run(200);title=find_label(lv_layer_top(),"Parking sensors active");assert(title);
    lv_obj_t *panel=lv_obj_get_parent(title);assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_width(panel)==1024 && lv_obj_get_height(panel)==600 && !lv_obj_get_x(panel) && !lv_obj_get_y(panel));
    rpc("inject","{\"id\":450,\"data\":\"FFFFFFFF053264C8\"}");run(100);
    assert(frame[0][0][0]==0 && frame[0][0][1]==0 && frame[0][0][2]==0);
    assert(frame[187][785][0]>240 && frame[187][785][1]<10 && frame[187][785][2]<10);
    assert(frame[180][770][0]>240 && frame[180][770][1]<10); /* Transparent car corners do not erase the arc. */
    assert(frame[147][882][1]>100 && frame[147][882][0]<10);
    unsigned car_pixels=0;
    for(unsigned y=180;y<420;y++)for(unsigned x=250;x<775;x++)
        if(frame[y][x][0]>20 || frame[y][x][1]>20 || frame[y][x][2]>20)car_pixels++;
    assert(car_pixels>10000);
    save_frame("out/target-ui/tests/pdc.ppm");
    assert(lv_scr_act()==home);hz_dashboard_open();run(100);
    assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));assert(lv_scr_act()==ui_Screen1);
    set_pdc(0);run(200);assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_child_cnt(panel)==1); /* Decoded car image is released on close. */
    assert(lv_scr_act()==ui_Screen1);
    set_pdc(1);run(200);assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_child_cnt(panel)==2);
    run(2100);assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
    run(1100);
    run(800);
    assert(lv_obj_has_flag(ui_TempPoint,LV_OBJ_FLAG_HIDDEN)); /* Coolant expires after five seconds. */
    /* Vehicle expiry uses real monotonic time; animation-frame comparisons
     * advance LVGL ticks without sleeping. Allow the next 1 Hz clock poll. */
    assert(!find_label(home,"--:--"));
    struct tm retained;assert(preview_vehicle_time(&retained));
    set_pdc(0);run(200);assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
    rpc("inject","{\"id\":760,\"data\":\"FFFFFFFFFFFFFFFF\"}");run(1100);
    assert(find_label(home,"--:--"));
    hz_home_open();run(400);unsigned before_idle=rendered;run(300);
    assert(rendered==before_idle); /* No changed telemetry or clock strings. */
    puts("PASS: unchanged vehicle/music/clock strings do not redraw the idle HMI");
    puts("PASS: full-screen graphical PDC, activation retained past 2s, CAN clock retained past 5s, explicit off and invalid calendar clearing");
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    assert(memory.total_size<=6*1024*1024);assert(memory.free_size>1024*1024);assert(rendered>10);
    assert(memory.max_used<5*1024*1024);
    printf("PASS: target HMI boot, Dashboard/Home buttons, 1024x600 layout, CAN speed/PDC on/off/expiry over ubus, %u renders; LVGL heap %u/%u bytes, peak %u\n",
        rendered,(unsigned)(memory.total_size-memory.free_size),(unsigned)memory.total_size,(unsigned)memory.max_used);
    /* No CJK font in the image: a Chinese character has no real glyph and must not crash. */
    lv_font_glyph_dsc_t chinese;
    assert(!lv_font_get_glyph_dsc(dashboard_font(22),&chinese,0x4e2d,0) || chinese.is_placeholder);
    puts("PASS: without a CJK font, Chinese characters fall back to a placeholder");
    return 0;
}
