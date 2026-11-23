/* SPDX-License-Identifier: GPL-2.0-only */
#include "settings.h"
#include "settings_store.h"
#include "gear_calibration.h"
#include "page_header.h"
#include "navigation.h"
#include "transition.h"
#include "system_fonts.h"
#include "ui_update.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/utsname.h>

enum category { BASIC, SOUND, DISPLAY, PICTURE, CONNECTIONS, ABOUT, CATEGORY_COUNT };
static const char *titles[]={"Basic Setup","Sound","Backlight control","Picture","Connections","System information"};
static const char *icons[]={LV_SYMBOL_SETTINGS,LV_SYMBOL_VOLUME_MAX,LV_SYMBOL_IMAGE,LV_SYMBOL_EYE_OPEN,LV_SYMBOL_BLUETOOTH,LV_SYMBOL_LIST};
static lv_obj_t *screen,*sidebar,*pane,*heading,*notice,*refresh_button,*navigation[CATEGORY_COUNT];
static lv_obj_t *controls[HZ_SETTING_COUNT],*readings[8];
static lv_obj_t *control_values[HZ_SETTING_COUNT];
static enum category selected=BASIC;
static bool syncing;
static bool previewed[HZ_SETTING_COUNT]; /* dragged value shown on the display, not yet confirmed */
#define BG 0x000000
#define LINE 0x4e4e4e
#define TEXT 0xf4f4f4
#define MUTED 0xb1b1b1
#define ACCENT 0xffffff
#define SCROLLBAR_GUTTER 16
#define PANE_WIDTH 624

static lv_obj_t *label(lv_obj_t *parent,const char *text,int size,uint32_t color)
{
    lv_obj_t *object=lv_label_create(parent);lv_label_set_text(object,text);
    lv_obj_set_style_text_font(object,dashboard_font(size),0);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE);
    return object;
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int width,int height)
{
    lv_obj_t *object=lv_obj_create(parent);lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y);lv_obj_set_size(object,width,height);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLLABLE);return object;
}
static void line(lv_obj_t *object)
{
    lv_obj_set_style_border_color(object,lv_color_hex(LINE),0);
    lv_obj_set_style_border_width(object,1,0);
    lv_obj_set_style_border_side(object,LV_BORDER_SIDE_BOTTOM,0);
}
static void scrollable(lv_obj_t *object)
{
    lv_obj_add_flag(object,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(object,LV_DIR_VER);lv_obj_set_scrollbar_mode(object,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(object,lv_color_hex(ACCENT),LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(object,LV_OPA_60,LV_PART_SCROLLBAR);
    lv_obj_set_style_width(object,3,LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(object,5,LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(object,SCROLLBAR_GUTTER,LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(object,12,LV_PART_MAIN);
}
bool hz_settings_scroll_at(int x,int y,int distance)
{
    if(!screen || lv_scr_act()!=screen)return false;
    lv_point_t point={x,y};
    if(lv_indev_search_obj(lv_layer_top(),&point))return false;
    lv_obj_t *object=lv_indev_search_obj(screen,&point);
    while(object && object!=sidebar && object!=pane)object=lv_obj_get_parent(object);
    if(!object)return false;
    lv_obj_update_layout(object);
    lv_obj_scroll_to_y(object,lv_obj_get_scroll_y(object)+distance,LV_ANIM_OFF);
    return true;
}
static lv_obj_t *button(lv_obj_t *parent,int x,int y,int width,int height)
{
    lv_obj_t *object=lv_btn_create(parent);lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y);lv_obj_set_size(object,width,height);
    lv_obj_set_style_bg_color(object,lv_color_hex(BG),0);
    lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x404040),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x373737),LV_STATE_CHECKED);
    lv_obj_set_style_border_color(object,lv_color_hex(ACCENT),LV_STATE_CHECKED);
    lv_obj_set_style_border_width(object,4,LV_STATE_CHECKED);
    lv_obj_set_style_border_side(object,LV_BORDER_SIDE_LEFT,LV_STATE_CHECKED);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLLABLE);return object;
}
static lv_obj_t *row(const char *name,int index,int height)
{
    lv_obj_t *object=box(pane,0,index*height,lv_pct(100),height);line(object);
    lv_obj_t *text=label(object,name,22,TEXT);lv_obj_align(text,LV_ALIGN_LEFT_MID,8,0);
    return object;
}
static lv_obj_t *value_row(const char *name,int index)
{
    lv_obj_t *object=row(name,index,66),*value=label(object,"",21,MUTED);
    lv_obj_set_width(value,285);lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_RIGHT,0);
    lv_label_set_long_mode(value,LV_LABEL_LONG_DOT);
    lv_obj_align(value,LV_ALIGN_RIGHT_MID,-8,0);return value;
}
static void apply(lv_event_t *event)
{
    if(syncing)return;
    lv_indev_t *input=lv_indev_get_act();
    if(input && lv_indev_get_scroll_obj(input))return;
    enum hz_setting key=(enum hz_setting)(intptr_t)lv_event_get_user_data(event);
    lv_obj_t *object=lv_event_get_target(event);
    int value=(HZ_SETTING_DISPLAY(key) || key==HZ_VOLUME)?lv_slider_get_value(object):lv_obj_has_state(object,LV_STATE_CHECKED);
    hz_settings_change(key,value);
}
static void slider_text(enum hz_setting key,int value)
{
    char text[32];
    if(key==HZ_BRIGHTNESS || key==HZ_DIMMED_BRIGHTNESS)snprintf(text,sizeof(text),"%d%%",(value*100+127)/255);
    else if(HZ_SETTING_PICTURE(key))snprintf(text,sizeof(text),"%d",value);
    else snprintf(text,sizeof(text),"%d / 20",value);
    hz_label_text(control_values[key],text);
}
static void slider_value(lv_event_t *event)
{
    if(syncing)return;
    enum hz_setting key=(enum hz_setting)(intptr_t)lv_event_get_user_data(event);
    int value=lv_slider_get_value(lv_event_get_target(event));
    slider_text(key,value);
    if(HZ_SETTING_DISPLAY(key)) {hz_settings_preview(key,value);previewed[key]=true;} /* visible while dragging, saved on release */
}
static void slider_at(const char *name,enum hz_setting key,int minimum,int maximum,int y,int height,int bar)
{
    lv_obj_t *object=row(name,0,height);lv_obj_set_y(object,y);
    control_values[key]=label(object,"",22,ACCENT);lv_obj_align(control_values[key],LV_ALIGN_RIGHT_MID,-8,0);
    controls[key]=lv_slider_create(pane);lv_obj_set_pos(controls[key],28,y+bar);
    lv_obj_add_flag(controls[key],LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_size(controls[key],PANE_WIDTH-SCROLLBAR_GUTTER-56,12);lv_slider_set_range(controls[key],minimum,maximum);
    lv_obj_set_ext_click_area(controls[key],24);
    lv_obj_set_style_bg_color(controls[key],lv_color_hex(LINE),LV_PART_MAIN);
    lv_obj_set_style_bg_color(controls[key],lv_color_hex(ACCENT),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(controls[key],lv_color_hex(TEXT),LV_PART_KNOB);
    lv_obj_add_event_cb(controls[key],apply,LV_EVENT_RELEASED,(void *)(intptr_t)key);
    lv_obj_add_event_cb(controls[key],slider_value,LV_EVENT_VALUE_CHANGED,(void *)(intptr_t)key);
}
static void slider(const char *name,enum hz_setting key,int minimum,int maximum,int y) {slider_at(name,key,minimum,maximum,y,76,118);}
static void toggle(const char *name,enum hz_setting key,int y)
{
    lv_obj_t *object=row(name,0,68);lv_obj_set_y(object,y);
    controls[key]=button(object,0,0,82,42);lv_obj_add_flag(controls[key],LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align(controls[key],LV_ALIGN_RIGHT_MID,-10,0);
    lv_obj_t *text=label(controls[key],"OFF",20,TEXT);lv_obj_center(text);
    lv_obj_add_event_cb(controls[key],apply,LV_EVENT_VALUE_CHANGED,(void *)(intptr_t)key);
}
static void note(const char *text,int y)
{
    lv_obj_t *object=label(pane,text,17,MUTED);lv_obj_set_pos(object,0,y);
    lv_obj_set_width(object,lv_pct(100));lv_obj_set_style_pad_hor(object,8,0);
    lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP);
}
static void calibrate_gears(lv_event_t *event) {(void)event;hz_gear_calibration_open();}
static void enabled(lv_obj_t *object,bool enable)
{
    if(enable)lv_obj_clear_state(object,LV_STATE_DISABLED);else lv_obj_add_state(object,LV_STATE_DISABLED);
    hz_style_opa(object,enable?LV_OPA_COVER:LV_OPA_40);
}
static void update(lv_timer_t *timer)
{
    (void)timer;if(!screen || lv_scr_act()!=screen)return;
    struct hz_settings_snapshot state;hz_settings_snapshot(&state);
    hz_label_text(notice,state.message);enabled(refresh_button,!state.busy);
    syncing=true;
    const int values[]={state.values.brightness,state.values.volume,state.values.mute,state.values.speaker,state.values.dimmed_brightness,state.values.bluetooth,
        state.values.picture[0],state.values.picture[1],state.values.picture[2],state.values.picture[3]};
    for(int key=0;key<HZ_SETTING_COUNT;key++)if(controls[key]) {
        bool display=HZ_SETTING_DISPLAY(key);
        enabled(controls[key],state.ready && !state.busy && (display || state.values.audio_available));
        if(display || key==HZ_VOLUME) {
            /* A dragged value being saved stays on the display; one that was not
             * committed (rejected, or the drag became a scroll) is undone. */
            if(!lv_slider_is_dragged(controls[key]) && !(display && state.busy)) {
                if(lv_slider_get_value(controls[key])!=values[key])lv_slider_set_value(controls[key],values[key],LV_ANIM_OFF);
                /* Once settled, show what is saved: the picture value, or the backlight
                 * level the vehicle lights select (a dimmed preset dragged by day). */
                if(previewed[key]) {previewed[key]=false;hz_settings_preview_restore(key);}
                slider_text(key,values[key]);
            }
        } else {
            if(values[key])lv_obj_add_state(controls[key],LV_STATE_CHECKED);
            else lv_obj_clear_state(controls[key],LV_STATE_CHECKED);
            hz_label_text(lv_obj_get_child(controls[key],0),values[key]?"ON":"OFF");
        }
    }
    char text[80];
    if(selected==BASIC) {
        snprintf(text,sizeof(text),"%d litres",state.values.fuel_litres);hz_label_text(readings[1],text);
        snprintf(text,sizeof(text),"%d Hz",state.values.speed_clock_hz);hz_label_text(readings[2],text);
        hz_label_text(readings[3],state.values.timezone);
    } else if(selected==SOUND) {
        hz_label_text(readings[1],state.preview?"Preview":!state.values.audio_available?"Unavailable":
            state.values.audio_acknowledged?"Acknowledged":"Awaiting acknowledgement");
    } else if(selected==CONNECTIONS) {
        snprintf(text,sizeof(text),"%s  " LV_SYMBOL_RIGHT,*state.values.bluetooth_name?state.values.bluetooth_name:"Not reported");
        hz_label_text(readings[0],text);
        hz_label_text(readings[1],state.values.source);
    } else if(selected==DISPLAY) {
        hz_label_text(readings[1],state.lights_on?"Vehicle lights: on":"Vehicle lights: off");
    }
    syncing=false;
}
/* Bluetooth name editor: a small on-screen keyboard built from buttons, since
 * the trimmed LVGL 8.1 has no textarea/keyboard widgets. */
enum { KEY_BACKSPACE=1, KEY_SHIFT, KEY_CANCEL, KEY_SAVE };
static lv_obj_t *name_editor,*name_text,*name_letters[26];
static char name_buffer[HZ_BLUETOOTH_NAME_MAX+1];
static bool name_lower;
static void name_show(void)
{
    char text[HZ_BLUETOOTH_NAME_MAX+2];snprintf(text,sizeof(text),"%s_",name_buffer);hz_label_text(name_text,text);
    for(int i=0;i<26;i++) {char letter[2]={(char)((name_lower?'a':'A')+i),0};hz_label_text(name_letters[i],letter);}
}
static void name_key(lv_event_t *event)
{
    intptr_t key=(intptr_t)lv_event_get_user_data(event);size_t length=strlen(name_buffer);
    if(key==KEY_BACKSPACE) {if(length)name_buffer[length-1]=0;}
    else if(key==KEY_SHIFT)name_lower=!name_lower;
    else if(key==KEY_SAVE || key==KEY_CANCEL) {
        if(key==KEY_SAVE && !hz_settings_rename(name_buffer)) {
            struct hz_settings_snapshot state;hz_settings_snapshot(&state);
            if(state.ready && !state.busy)return; /* Empty name: keep editing. */
        }
        lv_obj_del_async(name_editor);name_editor=NULL;return;
    } else if(length<HZ_BLUETOOTH_NAME_MAX) {
        char letter=(char)key;if(name_lower && letter>='A' && letter<='Z')letter+='a'-'A';
        name_buffer[length]=letter;name_buffer[length+1]=0;
    }
    name_show();
}
static lv_obj_t *name_button(const char *text,intptr_t key,int x,int y,int width)
{
    lv_obj_t *object=button(name_editor,x,y,width,62);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x262626),0);lv_obj_set_style_radius(object,6,0);
    lv_obj_t *caption=label(object,text,22,TEXT);lv_obj_center(caption);
    lv_obj_add_event_cb(object,name_key,LV_EVENT_CLICKED,(void *)key);return caption;
}
static void edit_name(lv_event_t *event)
{
    (void)event;struct hz_settings_snapshot state;hz_settings_snapshot(&state);
    if(name_editor || !state.ready || state.busy || !state.values.audio_available)return;
    snprintf(name_buffer,sizeof(name_buffer),"%.*s",HZ_BLUETOOTH_NAME_MAX,hz_settings_valid_name(state.values.bluetooth_name)?state.values.bluetooth_name:"");
    name_lower=false;
    name_editor=box(lv_layer_top(),0,0,lv_pct(100),lv_pct(100));lv_obj_add_flag(name_editor,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(name_editor,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(name_editor,LV_OPA_COVER,0);
    lv_obj_t *title=label(name_editor,"Bluetooth name",22,MUTED);lv_obj_set_pos(title,24,20);
    lv_obj_t *field=box(name_editor,24,56,976,56);line(field);
    name_text=label(field,"",30,TEXT); /* 30 px is already cached by other pages. */lv_obj_align(name_text,LV_ALIGN_LEFT_MID,8,0);
    lv_obj_t *hint=label(name_editor,"Up to 16 characters, no spaces. Settings shows the name the module reports.",17,MUTED);
    lv_obj_set_pos(hint,24,124);
    static const char *rows[]={"1234567890","QWERTYUIOP","ASDFGHJKL-","ZXCVBNM_."};
    for(int row=0;row<4;row++)for(int i=0;rows[row][i];i++) {
        char text[2]={rows[row][i],0};
        lv_obj_t *caption=name_button(text,rows[row][i],12+i*101,166+row*72,95);
        if(text[0]>='A' && text[0]<='Z')name_letters[text[0]-'A']=caption;
    }
    name_button("aA",KEY_SHIFT,12,454,196);name_button("Delete",KEY_BACKSPACE,214,454,196); /* FA4 icon font lacks the backspace glyph. */
    name_button("Cancel",KEY_CANCEL,416,454,290);name_button(LV_SYMBOL_OK " Save",KEY_SAVE,712,454,300);
    name_show();
}
/* Test pattern for the picture sliders: black-to-colour ramps show hue,
 * saturation and clipping; the squares show black level (0/5/10 %) and
 * white level (90/95/100 %). */
static void test_pattern(void)
{
    const int width=PANE_WIDTH-SCROLLBAR_GUTTER-16;
    const uint32_t ramps[]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    for(int i=0;i<4;i++) {
        lv_obj_t *ramp=box(pane,8,4+i*30,width,26);
        lv_obj_set_style_bg_opa(ramp,LV_OPA_COVER,0);lv_obj_set_style_bg_color(ramp,lv_color_black(),0);
        lv_obj_set_style_bg_grad_color(ramp,lv_color_hex(ramps[i]),0);lv_obj_set_style_bg_grad_dir(ramp,LV_GRAD_DIR_HOR,0);
    }
    const uint8_t levels[]={0x00,0x0d,0x1a,0xe6,0xf2,0xff};const char *captions[]={"0","5","10","90","95","100"};
    for(int i=0;i<6;i++) {
        lv_obj_t *square=box(pane,8+i*(width/6),126,width/6-8,40);
        lv_obj_set_style_bg_opa(square,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(square,lv_color_make(levels[i],levels[i],levels[i]),0);
        lv_obj_t *text=label(square,captions[i],17,i<3?0x808080:0x404040);lv_obj_center(text);
    }
}
static void select_category(enum category category)
{
    selected=category;lv_obj_clean(pane);memset(controls,0,sizeof(controls));memset(control_values,0,sizeof(control_values));memset(readings,0,sizeof(readings));
    hz_label_text(heading,titles[category]);
    for(int i=0;i<CATEGORY_COUNT;i++) {
        if(i==(int)category)lv_obj_add_state(navigation[i],LV_STATE_CHECKED);
        else lv_obj_clear_state(navigation[i],LV_STATE_CHECKED);
    }
    switch(category) {
    case BASIC:
        hz_label_text(value_row("Vehicle",0),"BMW E90 / manual");
        readings[1]=value_row("Tank capacity",1);readings[2]=value_row("Speed counter clock",2);
        readings[3]=value_row("Timezone",3);
        lv_obj_t *clock=value_row("Date and time",4);hz_label_text(clock,"Automatic from vehicle");
        lv_obj_t *calibration=button(pane,8,350,PANE_WIDTH-SCROLLBAR_GUTTER-16,56);
        lv_obj_t *calibration_text=label(calibration,"Gear calibration",22,TEXT);lv_obj_center(calibration_text);
        lv_obj_add_event_cb(calibration,calibrate_gears,LV_EVENT_CLICKED,NULL);break;
    case SOUND:
        slider("Volume",HZ_VOLUME,0,20,0);toggle("Mute",HZ_MUTE,170);toggle("Internal speaker",HZ_SPEAKER,238);
        readings[1]=value_row("Companion",0);lv_obj_set_y(lv_obj_get_parent(readings[1]),306);
        note("Speaker and volume use the AB5301B audio service.",392);break;
    case DISPLAY:
        slider("Normal backlight",HZ_BRIGHTNESS,16,255,0);
        slider("Dimmed backlight",HZ_DIMMED_BRIGHTNESS,16,255,188);
        readings[1]=label(pane,"",17,MUTED);lv_obj_set_pos(readings[1],8,334);
        note("Dimmed backlight applies when vehicle lights are on.\nPanel resolution: 1024 x 600",364);break;
    case PICTURE: {
        /* 50 is neutral. Grey black: lower Brightness, raise Contrast. */
        test_pattern();
        const char *names[]={"Brightness","Contrast","Hue","Saturation"};
        for(int i=0;i<4;i++)slider_at(names[i],HZ_LCD_BRIGHT+i,0,100,174+i*60,34,44);
        break;
    }
    case CONNECTIONS:
        toggle("Allow connections",HZ_BLUETOOTH,0);
        readings[0]=value_row("Bluetooth name",1);readings[1]=value_row("Audio source",2);
        lv_obj_t *name_row=lv_obj_get_parent(readings[0]);lv_obj_add_flag(name_row,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(name_row,lv_color_hex(0x404040),LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(name_row,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_add_event_cb(name_row,edit_name,LV_EVENT_CLICKED,NULL);
        note("Off disconnects the phone and blocks reconnects; the module stays\nvisible to phone searches. Tap the name to change it. Wireless AA is experimental.",302);break;
    case ABOUT: {
        hz_label_text(value_row("Device",0),"HZ-B500");hz_label_text(value_row("Processor",1),"Allwinner F133 / D1s");
        struct utsname system;const char *kernel=uname(&system)?"Unknown":system.release;
        struct hz_settings_snapshot state;hz_settings_snapshot(&state);
        hz_label_text(value_row(state.preview?"Preview host kernel":"Linux kernel",2),kernel);
        char graphics[32];snprintf(graphics,sizeof(graphics),"LVGL %d.%d.%d",LVGL_VERSION_MAJOR,LVGL_VERSION_MINOR,LVGL_VERSION_PATCH);
        hz_label_text(value_row("Graphics",3),graphics);
        note("Linux settings are separate from the stock Melis system.",302);break;
    }
    default:break;
    }
    update(NULL);
    lv_obj_update_layout(pane);lv_obj_scroll_to_y(pane,0,LV_ANIM_OFF);
}
static void category_clicked(lv_event_t *event)
{
    select_category((enum category)(intptr_t)lv_event_get_user_data(event));
}
static void refresh(lv_event_t *event) {(void)event;hz_settings_refresh();update(NULL);}
void hz_settings_open(void)
{
    uint32_t start=lv_tick_get();bool first=screen==NULL;
    if(first) {
        screen=lv_obj_create(NULL);lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
        lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
        struct hz_page_header header=hz_page_header_create(screen,titles[BASIC],"Settings",NULL,true);
        heading=header.title;
        sidebar=box(screen,0,64,352,536);scrollable(sidebar);
        lv_obj_set_style_border_color(sidebar,lv_color_hex(LINE),0);lv_obj_set_style_border_width(sidebar,1,0);
        lv_obj_set_style_border_side(sidebar,LV_BORDER_SIDE_RIGHT,0);
        const char *groups[]={"Vehicle","Equipment","System"};int ys[]={16,118,412};
        for(int i=0;i<3;i++) {lv_obj_t *text=label(sidebar,groups[i],20,MUTED);lv_obj_set_pos(text,24,ys[i]);}
        int positions[]={48,150,214,278,342,444};
        for(int i=0;i<CATEGORY_COUNT;i++) {
            navigation[i]=button(sidebar,0,positions[i],lv_pct(100),58);line(navigation[i]);
            lv_obj_t *icon=label(navigation[i],icons[i],24,TEXT);lv_obj_align(icon,LV_ALIGN_LEFT_MID,26,0);
            lv_obj_t *text=label(navigation[i],titles[i],22,TEXT);lv_obj_align(text,LV_ALIGN_LEFT_MID,82,0);
            lv_obj_add_event_cb(navigation[i],category_clicked,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        }
        pane=box(screen,376,90,PANE_WIDTH,422);scrollable(pane);
        notice=label(screen,"",16,MUTED);lv_obj_set_pos(notice,384,534);lv_obj_set_width(notice,470);
        lv_label_set_long_mode(notice,LV_LABEL_LONG_WRAP);
        refresh_button=button(screen,874,528,126,52);line(refresh_button);
        lv_obj_t *text=label(refresh_button,LV_SYMBOL_REFRESH " Refresh",18,TEXT);lv_obj_center(text);
        lv_obj_add_event_cb(refresh_button,refresh,LV_EVENT_CLICKED,NULL);
        lv_timer_create(update,100,NULL);select_category(BASIC);
    }
    hz_settings_refresh();hz_page_profile("settings",start,first);
    /* This page stays live: flattening it into prepared art would freeze the
     * selected category, settings values and hardware/error feedback. */
    hz_screen_load_anim(screen,LV_SCR_LOAD_ANIM_MOVE_LEFT,300,0,false);
}
