/* SPDX-License-Identifier: GPL-2.0-only */
#include "transition.h"
#include "static_scene.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HZ_USE_G2D
#include "g2d_display.h"
#endif
#ifdef HZ_TARGET_UI
struct page_cache {
    lv_obj_t *screen;
    lv_img_dsc_t image;
    const void *background;
    lv_color_t *pixels;
    lv_opa_t background_opa,image_opa;
    lv_grad_dir_t gradient;
    bool dma;
    unsigned count;
    bool hidden[];
};
static struct page_cache *pages[2];
static void finished(lv_event_t *event);
static void release_page(struct page_cache *page,bool restore) {
    if(!page)return;
    lv_obj_remove_event_cb(page->screen,finished);
    if(restore) {
        lv_obj_set_style_bg_img_src(page->screen,page->background,0);
        lv_obj_set_style_bg_img_opa(page->screen,page->image_opa,0);
        lv_obj_set_style_bg_grad_dir(page->screen,page->gradient,0);
        lv_obj_set_style_bg_opa(page->screen,page->background_opa,0);
        /* Timers may change text, but page creation/deletion is not performed
         * during a slide. Restore only surviving original child slots. */
        unsigned count=LV_MIN(page->count,lv_obj_get_child_cnt(page->screen));
        for(unsigned i=0;i<count;i++)if(!page->hidden[i])lv_obj_clear_flag(lv_obj_get_child(page->screen,i),LV_OBJ_FLAG_HIDDEN);
    }
    lv_img_cache_invalidate_src(&page->image);
#ifdef HZ_USE_G2D
    if(page->dma)hz_g2d_snapshot_free(page->pixels);else
#endif
    free(page->pixels);
    for(unsigned i=0;i<2;i++)if(pages[i]==page)pages[i]=NULL;
    free(page);
}
static void finished(lv_event_t *event) {
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_SCREEN_LOADED || code==LV_EVENT_SCREEN_UNLOADED || code==LV_EVENT_DELETE)
        release_page(lv_event_get_user_data(event),code!=LV_EVENT_DELETE);
}
static struct page_cache *capture(lv_obj_t *screen) {
    lv_disp_t *display=lv_obj_get_disp(screen);
    if(lv_disp_get_hor_res(display)!=1024 || lv_disp_get_ver_res(display)!=600 ||
       lv_obj_get_style_opa(screen,0)!=LV_OPA_COVER || lv_obj_get_style_bg_opa(screen,0)!=LV_OPA_COVER)return NULL;
    unsigned count=lv_obj_get_child_cnt(screen);
    struct page_cache *page=calloc(1,sizeof(*page)+count*sizeof(bool));if(!page)return NULL;
#ifdef HZ_USE_G2D
    page->pixels=hz_g2d_snapshot_alloc();page->dma=page->pixels!=NULL;
#endif
    if(!page->pixels)page->pixels=malloc(1024*600*sizeof(lv_color_t));
    if(!page->pixels) {free(page);return NULL;}
    page->screen=screen;page->count=count;
    lv_obj_set_pos(screen,0,0);hz_scene_raster(screen,page->pixels);
    page->background=lv_obj_get_style_bg_img_src(screen,0);
    page->background_opa=lv_obj_get_style_bg_opa(screen,0);
    page->image_opa=lv_obj_get_style_bg_img_opa(screen,0);
    page->gradient=lv_obj_get_style_bg_grad_dir(screen,0);
    page->image.header.cf=LV_IMG_CF_TRUE_COLOR;page->image.header.w=1024;page->image.header.h=600;
    page->image.data=(const uint8_t *)page->pixels;page->image.data_size=1024*600*sizeof(lv_color_t);
#ifdef HZ_USE_G2D
    if(page->dma)hz_g2d_snapshot_ready(page->pixels);
#endif
    for(unsigned i=0;i<count;i++) {
        lv_obj_t *child=lv_obj_get_child(screen,i);page->hidden[i]=lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(child,LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_NONE,0);
    lv_obj_set_style_bg_img_src(screen,&page->image,0);
    lv_obj_set_style_bg_img_opa(screen,LV_OPA_COVER,0);
    lv_obj_add_event_cb(screen,finished,LV_EVENT_ALL,page);
    return page;
}
#endif
void hz_screen_load_anim(lv_obj_t *screen,lv_scr_load_anim_t animation,uint32_t duration,uint32_t delay,bool auto_delete) {
#ifdef HZ_TARGET_UI
    lv_disp_t *display=lv_obj_get_disp(screen);
    const char *setting=getenv("HZ_UI_TRANSITION_CACHE");
    if(duration && animation!=LV_SCR_LOAD_ANIM_NONE && !delay && !display->prev_scr &&
       screen!=lv_scr_act() && (!setting || strcmp(setting,"0"))) {
        uint32_t start=lv_tick_get();
        for(unsigned i=0;i<2;i++)release_page(pages[i],true);
        pages[0]=capture(lv_scr_act());
        if(pages[0])pages[1]=capture(screen);
        if(!pages[0] || !pages[1]) {
            for(unsigned i=0;i<2;i++)release_page(pages[i],true);
        } else if(getenv("HZ_UI_PROFILE"))fprintf(stderr,"UI transition raster: %u ms, two temporary 1024x600 pages\n",lv_tick_elaps(start));
    }
#endif
    lv_scr_load_anim(screen,animation,duration,delay,auto_delete);
}
