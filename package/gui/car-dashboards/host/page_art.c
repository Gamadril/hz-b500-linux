/* SPDX-License-Identifier: GPL-2.0-only */
#include "page_art.h"
#include "art_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HZ_USE_G2D
#include "g2d_display.h"
#endif
struct saved_style {struct saved_style *next;lv_obj_t *object;lv_opa_t opa,bg,shadow,border,outline;};
struct button_art;
struct plate {struct plate *next;lv_obj_t *screen,*overlay;lv_img_dsc_t image,pressed;lv_color_t *pixels,*tile;bool dma,evict_pending,evicting,release;char name[24];struct saved_style *styles;struct button_art *buttons;const void *old_background;lv_grad_dir_t old_gradient;};
struct button_art {struct button_art *next;struct plate *plate;lv_obj_t *object;unsigned index;};
static struct plate *plates;
static const char *asset_root(void) {const char *root=getenv("HZ_UI_PAGE_ART");return root?root:"/usr/share/hz-ui/art";}
static void tile_opacity(void *object,int32_t opacity) {
    lv_obj_set_style_img_opa(object,opacity,0);
    if(opacity)lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void fade_tile(lv_obj_t *object,unsigned opacity) {
    lv_anim_del(object,tile_opacity);
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,object);
    lv_anim_set_exec_cb(&animation,tile_opacity);lv_anim_set_time(&animation,100);
    lv_anim_set_values(&animation,lv_obj_get_style_img_opa(object,0),opacity);lv_anim_start(&animation);
}
static FILE *open_asset(const char *name,int button) {
    char path[320];
    if(button<0)snprintf(path,sizeof(path),"%s/%s.hzp",asset_root(),name);
    else snprintf(path,sizeof(path),"%s/%s-%d.hzp",asset_root(),name,button);
    return fopen(path,"rb");
}
bool hz_art_image(const char *name,lv_img_dsc_t *image) {
    FILE *file=open_asset(name,-1);if(!file)return false;
    struct hz_art_header header;uint32_t *pixels=NULL;
    bool valid=hz_art_read(file,&header,NULL,0) && header.width<=1024 && header.height<=600 &&
        (pixels=malloc(header.width*header.height*4));
    if(valid) {rewind(file);valid=hz_art_read(file,&header,pixels,header.width*header.height);}
    fclose(file);
    if(!valid) {free(pixels);return false;}
    *image=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR_ALPHA,.w=header.width,.h=header.height},
        .data_size=header.width*header.height*4,.data=(const uint8_t *)pixels};
    return true;
}
static void button_event(lv_event_t *event);
static void plate_deleted(lv_event_t *event);
static void evict(void *argument);
static void free_plate(struct plate *plate,bool restore) {
    /* LVGL 8.3 frees the async info after calling us. Cancelling the current
     * callback here would free that info twice. */
    if(!plate->evicting)lv_async_call_cancel(evict,plate);
    lv_obj_remove_event_cb(plate->screen,plate_deleted);
    if(restore) {
        lv_obj_set_style_bg_img_src(plate->screen,plate->old_background,0);
        lv_obj_set_style_bg_grad_dir(plate->screen,plate->old_gradient,0);
        if(plate->overlay)lv_obj_del(plate->overlay);
    }
    while(plate->buttons) {
        struct button_art *art=plate->buttons;plate->buttons=art->next;
        if(lv_obj_is_valid(art->object))lv_obj_remove_event_cb(art->object,button_event);
        free(art);
    }
    while(plate->styles) {
        struct saved_style *style=plate->styles;plate->styles=style->next;
        if(restore && lv_obj_is_valid(style->object)) {
            lv_obj_set_style_opa(style->object,style->opa,0);lv_obj_set_style_bg_opa(style->object,style->bg,0);
            lv_obj_set_style_shadow_opa(style->object,style->shadow,0);lv_obj_set_style_border_opa(style->object,style->border,0);
            lv_obj_set_style_outline_opa(style->object,style->outline,0);
        }
        free(style);
    }
    struct plate **link=&plates;while(*link && *link!=plate)link=&(*link)->next;
    if(*link)*link=plate->next;
    lv_img_cache_invalidate_src(&plate->image);lv_img_cache_invalidate_src(&plate->pressed);
#ifdef HZ_USE_G2D
    if(plate->dma)hz_g2d_background_free(plate->pixels);else
#endif
    free(plate->pixels);
    free(plate->tile);free(plate);
}
static void evict(void *argument) {
    struct plate *plate=argument;lv_disp_t *display=lv_obj_get_disp(plate->screen);
    plate->evict_pending=false;
    if(plate->screen!=lv_scr_act() && plate->screen!=display->prev_scr) {plate->evicting=true;free_plate(plate,true);}
}
static void plate_deleted(lv_event_t *event) {
    struct plate *plate=lv_event_get_user_data(event);
    if(lv_event_get_code(event)==LV_EVENT_DELETE)free_plate(plate,false);
    else if(lv_event_get_code(event)==LV_EVENT_SCREEN_UNLOADED && (strcmp(plate->name,"home") || plate->release) && !plate->evict_pending) {
        plate->evict_pending=true;lv_async_call(evict,plate);
    }
}
static struct plate *find_plate(lv_obj_t *screen) {
    for(struct plate *plate=plates;plate;plate=plate->next)if(plate->screen==screen)return plate;
    return NULL;
}
bool hz_page_art_ready(lv_obj_t *screen) {return find_plate(screen)!=NULL;}
void hz_page_art_release(lv_obj_t *screen) {struct plate *plate=find_plate(screen);if(plate)plate->release=true;}
bool hz_page_art_load(lv_obj_t *screen,const char *name) {
    const char *setting=getenv("HZ_UI_STATIC_CACHE");
    if(setting && !strcmp(setting,"0"))return false;
    if(lv_disp_get_hor_res(lv_obj_get_disp(screen))!=1024 || lv_disp_get_ver_res(lv_obj_get_disp(screen))!=600)return false;
    if(find_plate(screen))return true;
    FILE *file=open_asset(name,-1);if(!file)return false;
    struct hz_art_header header;bool valid=hz_art_read(file,&header,NULL,0);
    if(!valid || header.width!=1024 || header.height!=600) {fclose(file);return false;}
    struct plate *plate=calloc(1,sizeof(*plate));if(!plate) {fclose(file);return false;}
#ifdef HZ_USE_G2D
    /* The persistent background allocation belongs to Home; other pages use
     * ordinary cached CPU memory and retain the normal upload fallback. */
    if(!strcmp(name,"home")) {plate->pixels=hz_g2d_background_alloc();plate->dma=plate->pixels!=NULL;}
#endif
    if(!plate->pixels)plate->pixels=malloc(1024*600*4);
    rewind(file);valid=plate->pixels && hz_art_read(file,&header,(uint32_t *)plate->pixels,1024*600);fclose(file);
    if(!valid) {
#ifdef HZ_USE_G2D
        if(plate->dma)hz_g2d_background_free(plate->pixels);else
#endif
        free(plate->pixels);free(plate);return false;
    }
    plate->screen=screen;snprintf(plate->name,sizeof(plate->name),"%s",name);
    plate->old_background=lv_obj_get_style_bg_img_src(screen,0);plate->old_gradient=lv_obj_get_style_bg_grad_dir(screen,0);
    plate->image.header.cf=LV_IMG_CF_TRUE_COLOR;plate->image.header.w=1024;plate->image.header.h=600;
    plate->image.data_size=1024*600*4;plate->image.data=(uint8_t *)plate->pixels;
#ifdef HZ_USE_G2D
    if(plate->dma)hz_g2d_background_ready();
#endif
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_NONE,0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_set_style_bg_img_src(screen,&plate->image,0);
    lv_obj_set_style_bg_img_opa(screen,LV_OPA_COVER,0);
    plate->next=plates;plates=plate;
    lv_obj_add_event_cb(screen,plate_deleted,LV_EVENT_ALL,plate);return true;
}
static void button_event(lv_event_t *event) {
    struct button_art *art=lv_event_get_user_data(event);struct plate *plate=art->plate;
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {fade_tile(plate->overlay,0);return;}
    if(code!=LV_EVENT_PRESSED)return;
    lv_anim_del(plate->overlay,tile_opacity);
    lv_obj_add_flag(plate->overlay,LV_OBJ_FLAG_HIDDEN);
    FILE *file=open_asset(plate->name,art->index);if(!file)return;
    struct hz_art_header header;
    if(!hz_art_read(file,&header,NULL,0)) {fclose(file);return;}
    lv_img_cache_invalidate_src(&plate->pressed);
    lv_color_t *tile=realloc(plate->tile,header.width*header.height*4);
    if(!tile) {fclose(file);return;}plate->tile=tile;
    rewind(file);bool valid=hz_art_read(file,&header,(uint32_t *)tile,header.width*header.height);fclose(file);
    if(!valid)return;
    plate->pressed.header.cf=LV_IMG_CF_TRUE_COLOR;plate->pressed.header.w=header.width;plate->pressed.header.h=header.height;
    plate->pressed.data_size=header.width*header.height*4;plate->pressed.data=(uint8_t *)tile;
    lv_img_set_src(plate->overlay,&plate->pressed);lv_obj_set_pos(plate->overlay,header.x,header.y);
    tile_opacity(plate->overlay,0);fade_tile(plate->overlay,LV_OPA_COVER);
}
static bool live_tree(lv_obj_t *object,lv_obj_t *const *live,unsigned count) {
    for(unsigned i=0;i<count;i++)if(object==live[i])return true;
    for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)if(live_tree(lv_obj_get_child(object,i),live,count))return true;
    return false;
}
static void mask_art(lv_obj_t *object,lv_obj_t *const *live,unsigned count,struct plate *plate,unsigned *button) {
    bool keep=false;for(unsigned i=0;i<count;i++)if(object==live[i])keep=true;
    if(lv_obj_check_type(object,&lv_btn_class)) {
        struct button_art *art=keep?NULL:malloc(sizeof(*art));
        if(art) {*art=(struct button_art){plate->buttons,plate,object,*button};plate->buttons=art;lv_obj_add_event_cb(object,button_event,LV_EVENT_ALL,art);}
        (*button)++;
    }
    if(keep)return;
    struct saved_style *saved=malloc(sizeof(*saved));
    if(!saved)return;
    *saved=(struct saved_style){plate->styles,object,lv_obj_get_style_opa(object,0),lv_obj_get_style_bg_opa(object,0),lv_obj_get_style_shadow_opa(object,0),lv_obj_get_style_border_opa(object,0),lv_obj_get_style_outline_opa(object,0)};
    plate->styles=saved;
    if(!live_tree(object,live,count)) {
        lv_obj_set_style_opa(object,LV_OPA_TRANSP,0);
        /* Keep traversing for nested button bindings; descendants are already
         * invisible through their ancestor's opacity and remain hit-testable. */
    } else {
        lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,0);lv_obj_set_style_shadow_opa(object,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_opa(object,LV_OPA_TRANSP,0);lv_obj_set_style_outline_opa(object,LV_OPA_TRANSP,0);
    }
    for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)mask_art(lv_obj_get_child(object,i),live,count,plate,button);
}
void hz_page_art_bind(lv_obj_t *screen,const char *name,lv_obj_t *const *live,unsigned count) {
    struct plate *plate=find_plate(screen);
    if(!plate && (!hz_page_art_load(screen,name) || !(plate=find_plate(screen))))return;
    if(plate->overlay)return;
    unsigned button=0;
    for(unsigned i=0;i<lv_obj_get_child_cnt(screen);i++)mask_art(lv_obj_get_child(screen,i),live,count,plate,&button);
    plate->overlay=lv_img_create(screen);lv_obj_clear_flag(plate->overlay,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_img_opa(plate->overlay,0,0);
    lv_obj_add_flag(plate->overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_background(plate->overlay);
}
