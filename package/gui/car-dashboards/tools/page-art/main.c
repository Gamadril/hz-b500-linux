/* SPDX-License-Identifier: GPL-2.0-only */
#include "lvgl/lvgl.h"
#include "mode/Car_Launcher.h"
#include "navigation.h"
#include "system_fonts.h"
#include "page_art.h"
#include "art_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static lv_color_t frame[1024*600],crop[1024*600];
static FILE *manifest;
void hz_art_record(const char *name) {if(fprintf(manifest,"%s.hzp\n",name)<0)exit(1);}
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *pixels) {
    unsigned width=lv_area_get_width(area);
    for(int y=area->y1;y<=area->y2;y++) {memcpy(frame+y*1024+area->x1,pixels,width*4);pixels+=width;}
    lv_disp_flush_ready(driver);
}
static void settle(void) {for(unsigned i=0;i<400;i+=20) {lv_tick_inc(20);lv_anim_refr_now();}lv_refr_now(NULL);}
static unsigned buttons(lv_obj_t *object,lv_obj_t **list,unsigned count) {
    if(lv_obj_check_type(object,&lv_btn_class))list[count++]=object;
    for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)count=buttons(lv_obj_get_child(object,i),list,count);
    return count;
}
static void save(const char *root,const char *name,int index,lv_area_t area) {
    char path[320];if(index<0)snprintf(path,sizeof(path),"%s/%s.hzp",root,name);else snprintf(path,sizeof(path),"%s/%s-%d.hzp",root,name,index);
    unsigned w=lv_area_get_width(&area),h=lv_area_get_height(&area);
    for(unsigned y=0;y<h;y++)memcpy(crop+y*w,frame+(y+area.y1)*1024+area.x1,w*4);
    FILE *file=fopen(path,"wb");if(!file) {perror(path);exit(1);}
    struct hz_art_header header={.width=w,.height=h,.x=area.x1,.y=area.y1};
    if(!hz_art_write(file,&header,(uint32_t *)crop) || fclose(file))exit(1);
    const char *name_start=strrchr(path,'/');
    if(fprintf(manifest,"%s\n",name_start?name_start+1:path)<0)exit(1);
}
int preview_rk_page(const char *);
void hz_pdc_art(const char *,const char *);
void hz_vehicle_art(const char *,const char *);
void hz_launcher_art(const char *,const char *);
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    char path[320];const char *files[]={"NotoSans-Regular.ttf","fontawesome-webfont.ttf"};
    const char *settings[]={"DASHBOARD_FONT","DASHBOARD_ICON_FONT"};
    for(unsigned i=0;i<2;i++) {snprintf(path,sizeof(path),"%s/%s",argv[2],files[i]);setenv(settings[i],path,1);}
    setenv("HZ_UI_STATIC_CACHE","0",1);setenv("HZ_UI_TRANSITION_CACHE","0",1);
    mkdir(argv[1],0755);snprintf(path,sizeof(path),"%s/art.list",argv[1]);
    manifest=fopen(path,"w");if(!manifest) {perror(path);return 1;}
    hz_pdc_art(HZ_PDC_SOURCE,argv[1]);hz_vehicle_art(HZ_VEHICLE_SOURCE,argv[1]);hz_launcher_art(HZ_LAUNCHER_SOURCE,argv[1]);
    setenv("HZ_UI_PAGE_ART",argv[1],1);lv_init();dashboard_fonts_init();
    static lv_color_t pixels[1024*40];lv_disp_draw_buf_t buffer;lv_disp_draw_buf_init(&buffer,pixels,NULL,1024*40);
    lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=1024;driver.ver_res=600;driver.draw_buf=&buffer;driver.flush_cb=flush;
    lv_disp_drv_register(&driver);hz_ui_set_vehicle_mode(1);Car_Launcher_Gui();
    const char *pages[]={"home","music"};
    for(unsigned page=0;page<2;page++) {
        if(page) {hz_home_open();preview_rk_page(pages[page]);settle();}
        lv_obj_t *screen=lv_scr_act(),*live[16],*button[64];
        unsigned count=hz_launcher_live(pages[page],live,16);
        /* Opacity preserves flex/grid geometry; HIDDEN removes live controls
         * from layout and shifts the static artwork around them. */
        lv_opa_t opacity[16];
        for(unsigned i=0;i<count;i++) {opacity[i]=lv_obj_get_style_opa(live[i],0);lv_obj_set_style_opa(live[i],LV_OPA_TRANSP,0);}
        lv_obj_invalidate(screen);lv_refr_now(NULL);save(argv[1],pages[page],-1,(lv_area_t){0,0,1023,599});
        unsigned total=buttons(screen,button,0);
        for(unsigned i=0;i<total;i++) {
            lv_obj_add_state(button[i],LV_STATE_PRESSED);settle();
            lv_area_t area;lv_obj_get_coords(button[i],&area);int margin=_lv_obj_get_ext_draw_size(button[i])+4;
            area.x1=LV_MAX(0,area.x1-margin);area.y1=LV_MAX(0,area.y1-margin);
            area.x2=LV_MIN(1023,area.x2+margin);area.y2=LV_MIN(599,area.y2+margin);
            save(argv[1],pages[page],i,area);lv_obj_clear_state(button[i],LV_STATE_PRESSED);settle();
        }
        for(unsigned i=0;i<count;i++)lv_obj_set_style_opa(live[i],opacity[i],0);
        printf("Prepared %s with %u pressed-state tiles\n",pages[page],total);
    }
    return fclose(manifest)?1:0;
}
