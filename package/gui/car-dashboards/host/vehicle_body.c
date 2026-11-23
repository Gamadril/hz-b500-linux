/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle_body.h"
#include "art_codec.h"
#include "system_fonts.h"
#include "ui_update.h"
#include <stdio.h>
#include <stdlib.h>

enum { BODY,FULL,LF,RF,LR,RR,LOW,HIGH,LEFT,RIGHT,IMAGE_COUNT };
static struct {
    const char *name;unsigned width,height;int x,y;
    lv_img_dsc_t descriptor;uint32_t *pixels;lv_obj_t *object;
} images[]={
    {"body",75,166,123,60},{"body_full",75,166,123,60},
    {"door_lf",15,39,123,115},{"door_rf",15,38,183,115},
    {"door_lr",11,35,127,153},{"door_rr",10,35,183,153},
    {"low_beam",173,82,74,0},{"full_beam",109,76,106,0},
    {"turn_left",34,179,113,58},{"turn_right",34,179,174,58}
};
static lv_obj_t *panel,*doors_label,*lights_label,*trunk;
static int scaled(int value) {return (value*461+128)/256;}
static void visible(lv_obj_t *object,bool show)
{
    if(!object)return;
    if(show==!lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))return;
    if(show)lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void load_image(unsigned index)
{
    const char *directory=getenv("HZ_UI_PAGE_ART");if(!directory)directory="/usr/share/hz-ui/art";
    char path[512];if(snprintf(path,sizeof(path),"%s/vehicle-%s.hzp",directory,images[index].name)>=(int)sizeof(path))return;
    FILE *file=fopen(path,"rb");if(!file)return;
    struct hz_art_header header;unsigned count=images[index].width*images[index].height;
    bool valid=hz_art_read(file,&header,NULL,0) && header.width==images[index].width && header.height==images[index].height;
    if(valid) {
        images[index].pixels=malloc(count*4);rewind(file);
        valid=images[index].pixels && hz_art_read(file,&header,images[index].pixels,count);
    }
    fclose(file);
    if(!valid) {free(images[index].pixels);images[index].pixels=NULL;return;}
    images[index].descriptor=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR_ALPHA,.w=header.width,.h=header.height},
        .data_size=count*4,.data=(const uint8_t *)images[index].pixels};
    lv_obj_t *object=images[index].object=lv_img_create(panel);lv_img_set_src(object,&images[index].descriptor);
    bool door=index>=LF && index<=RR;int pivot=door?images[index].width/2:0;
    lv_img_set_pivot(object,pivot,0);lv_img_set_zoom(object,461);
    lv_obj_set_pos(object,scaled(images[index].x-70)+scaled(pivot)-pivot,20+scaled(images[index].y));
    /* Artwork colors as drawn: beams are already white, turn signals amber. */
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void draw_trunk(lv_event_t *event)
{
    /* The reference project's trunk polygon, relative to its body image. */
    static const lv_point_t shape[]={{18,146},{56,146},{59,159},{50,163},{25,163},{15,159}};
    lv_area_t bounds;lv_obj_get_coords(panel,&bounds);lv_point_t points[6];
    for(unsigned i=0;i<6;i++)points[i]=(lv_point_t){bounds.x1+scaled(123-70+shape[i].x),bounds.y1+20+scaled(60+shape[i].y)};
    lv_draw_rect_dsc_t style;lv_draw_rect_dsc_init(&style);
    style.bg_color=lv_color_hex(0xff3c3c);style.bg_opa=LV_OPA_70;
    lv_draw_polygon(lv_event_get_draw_ctx(event),&style,points,6);
}
static lv_obj_t *caption(int y)
{
    lv_obj_t *object=lv_label_create(panel);lv_obj_set_pos(object,8,y);lv_obj_set_width(object,320);
    lv_obj_set_style_text_font(object,dashboard_font(17),0);lv_obj_set_style_text_color(object,lv_color_hex(0xb1b1b1),0);
    lv_obj_set_style_text_align(object,LV_TEXT_ALIGN_CENTER,0);return object;
}
lv_obj_t *hz_vehicle_body_create(lv_obj_t *parent)
{
    panel=lv_obj_create(parent);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,16,84);lv_obj_set_size(panel,336,484);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i=0;i<IMAGE_COUNT;i++)load_image(i);
    trunk=lv_obj_create(panel);lv_obj_remove_style_all(trunk);lv_obj_set_size(trunk,336,432);
    lv_obj_clear_flag(trunk,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(trunk,draw_trunk,LV_EVENT_DRAW_MAIN,NULL);
    doors_label=caption(432);lights_label=caption(458);hz_vehicle_body_update(NULL);return panel;
}
void hz_vehicle_body_update(const struct vehicle_state *state)
{
    if(!panel)return;
    bool doors_known=state && state->connected && state->signals[V_DOORS].valid;
    bool lights_known=state && state->connected && state->signals[V_LIGHTS].valid;
    unsigned doors=doors_known?(unsigned)state->signals[V_DOORS].value:0;
    int lights=lights_known?state->signals[V_LIGHTS].value:0;
    visible(images[FULL].object,!doors_known || !(doors&15));
    visible(images[BODY].object,doors_known && (doors&15));
    if(images[FULL].object)hz_style_opa(images[FULL].object,LV_OPA_COVER);
    for(unsigned i=0;i<4;i++) {
        lv_obj_t *object=images[LF+i].object;bool open=doors&(1u<<i);
        visible(object,doors_known && (doors&15));if(!object)continue;
        int angle=open?((i==0 || i==2)?400:3200):0;
        if(lv_img_get_angle(object)!=angle)lv_img_set_angle(object,angle);
        lv_opa_t opacity=open?LV_OPA_COVER:LV_OPA_TRANSP;
        if(lv_obj_get_style_img_recolor_opa(object,0)!=opacity) {
            lv_obj_set_style_img_recolor(object,lv_color_hex(0xff3c3c),0);
            lv_obj_set_style_img_recolor_opa(object,opacity,0);
        }
    }
    visible(trunk,doors_known && (doors&16));
    visible(images[LOW].object,lights_known && lights==1);visible(images[HIGH].object,lights_known && lights==2);
    bool flash=(lv_tick_get()/500)%2==0;
    visible(images[LEFT].object,state && state->connected && state->signals[V_LEFT].valid && state->signals[V_LEFT].value && flash);
    visible(images[RIGHT].object,state && state->connected && state->signals[V_RIGHT].valid && state->signals[V_RIGHT].value && flash);
    unsigned count=0;for(unsigned i=0;i<4;i++)count+=!!(doors&(1u<<i));
    char message[80];
    if(!doors_known)message[0]=0;
    else if(count)snprintf(message,sizeof(message),"%u door%s open%s",count,count==1?"":"s",doors&16?", trunk open":"");
    else snprintf(message,sizeof(message),"%s",doors&16?"Trunk open":"All doors closed");
    if(!images[FULL].object || !images[BODY].object)snprintf(message,sizeof(message),"Vehicle artwork unavailable");
    hz_label_text(doors_label,message);
    hz_label_text(lights_label,!lights_known?"":lights==2?"High beam":lights==1?"Low beam":"Lights off");
    hz_style_text_color(lights_label,lv_color_hex(!lights_known || !lights?0xb1b1b1:0xffffff));
}
