/* SPDX-License-Identifier: GPL-2.0-only */
/* Native 1024x600 layout matching the user's Melis PDC car/range geometry. */
#include "pdc_view.h"
#include "art_codec.h"
#include "ui_update.h"
#include "system_fonts.h"
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *panel,*title,*car;
static lv_img_dsc_t body;
static uint32_t *body_pixels;
static bool body_attempted;
static int distance[8];

static uint32_t range_color(int cm,unsigned band)
{
    if(cm<0)return 0;
    if(band==0 && cm<=10)return 0xff0000;
    if(band==1 && cm<=50)return 0xffff00;
    if(band==2 && cm<100)return 0x008000;
    if(band==3 && cm<200)return 0x008000;
    return 0;
}
static void draw(lv_event_t *event)
{
    lv_draw_ctx_t *context=lv_event_get_draw_ctx(event);
    lv_area_t bounds;lv_obj_get_coords(panel,&bounds);
    lv_point_t center={bounds.x1+512,bounds.y1+300};
    static const unsigned angles[]={330,345,0,15,150,165,180,195};
    lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);arc.width=35;
    for(unsigned sensor=0;sensor<8;sensor++) for(unsigned band=0;band<4;band++) {
        uint32_t color=range_color(distance[sensor],band);
        if(!color)continue;
        arc.color=lv_color_hex(color);
        lv_draw_arc(context,&arc,&center,315+band*35,angles[sensor],angles[sensor]+15);
    }
}
static void load_car(void)
{
    if(car || body_attempted)return;
    body_attempted=true;
    const char *directory=getenv("HZ_UI_PAGE_ART");
    if(!directory)directory="/usr/share/hz-ui/art";
    char path[512];if(snprintf(path,sizeof(path),"%s/pdc-car.hzp",directory)>=(int)sizeof(path))return;
    FILE *file=fopen(path,"rb");if(!file) {fprintf(stderr,"PDC artwork unavailable: %s\n",path);return;}
    struct hz_art_header header;
    bool valid=hz_art_read(file,&header,NULL,0) && header.width==526 && header.height==241;
    if(valid) {
        rewind(file);body_pixels=malloc(526*241*sizeof(*body_pixels));
        valid=body_pixels && hz_art_read(file,&header,body_pixels,526*241);
    }
    fclose(file);
    if(!valid) {free(body_pixels);body_pixels=NULL;return;}
    body=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR_ALPHA,.w=526,.h=241},.data_size=526*241*4,.data=(const uint8_t *)body_pixels};
    car=lv_img_create(panel);lv_img_set_src(car,&body);lv_obj_center(car);
    lv_obj_clear_flag(car,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(title);
}
lv_obj_t *hz_pdc_create(void)
{
    panel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel,1024,600);lv_obj_set_pos(panel,0,0);
    lv_obj_set_style_bg_color(panel,lv_color_black(),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(panel,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel,draw,LV_EVENT_DRAW_MAIN,NULL);
    title=lv_label_create(panel);lv_obj_set_style_text_font(title,dashboard_font(18),0);
    lv_obj_set_style_text_color(title,lv_color_hex(0xb0b0b0),0);
    hz_label_text(title,"Parking sensors unavailable");lv_obj_align(title,LV_ALIGN_BOTTOM_MID,0,-20);
    for(unsigned i=0;i<8;i++)distance[i]=-1;
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    return panel;
}
void hz_pdc_update(const struct vehicle_state *state)
{
    if(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN))load_car();
    else {
        if(car) {lv_obj_del(car);car=NULL;lv_img_cache_invalidate_src(&body);}
        free(body_pixels);body_pixels=NULL;body_attempted=false;
    }
    bool changed[2]={false,false};
    for(unsigned i=0;i<8;i++) {
        const struct vehicle_signal *v=&state->signals[V_PDC_FL+i];
        int next=v->valid && v->value<255?v->value:-1;
        if(next!=distance[i]) {distance[i]=next;changed[i/4]=true;}
    }
    lv_area_t bounds;lv_obj_get_coords(panel,&bounds);
    for(unsigned half=0;half<2;half++)if(changed[half]) {
        lv_area_t area={bounds.x1+(half?90:752),bounds.y1+88,bounds.x1+(half?272:934),bounds.y1+512};
        lv_obj_invalidate_area(panel,&area);
    }
    const struct vehicle_signal *active=&state->signals[V_PDC_ACTIVE];
    hz_label_text(title,!active->valid?"Parking sensors unavailable":active->value?"Parking sensors active":"Parking sensors inactive");
}
