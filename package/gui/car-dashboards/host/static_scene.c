/* SPDX-License-Identifier: GPL-2.0-only */
#include "static_scene.h"
#include "lvgl/src/draw/sw/lv_draw_sw.h"
#include "lvgl/src/core/lv_refr.h"
#include <stdlib.h>
#include <stdio.h>
#ifdef HZ_USE_G2D
#include "g2d_display.h"
#endif
struct scene {lv_img_dsc_t image;lv_color_t *pixels;bool dma;};
void hz_scene_raster(lv_obj_t *screen,lv_color_t *pixels) {
    lv_disp_t *display=lv_obj_get_disp(screen);
    lv_obj_update_layout(screen);
    lv_area_t area={0,0,lv_disp_get_hor_res(display)-1,lv_disp_get_ver_res(display)-1};
    lv_draw_sw_ctx_t context;
#ifdef HZ_USE_G2D
    if(hz_g2d_is_surface(pixels))hz_g2d_draw_ctx_init(display->driver,&context.base_draw);else
#endif
    lv_draw_sw_init_ctx(display->driver,&context.base_draw);
    context.base_draw.buf=pixels;context.base_draw.buf_area=&area;context.base_draw.clip_area=&area;
    lv_disp_t *previous=_lv_refr_get_disp_refreshing();_lv_refr_set_disp_refreshing(display);
    lv_obj_redraw(&context.base_draw,screen);
    _lv_refr_set_disp_refreshing(previous);lv_draw_sw_deinit_ctx(display->driver,&context.base_draw);
}
static void release(lv_event_t *event) {
    struct scene *scene=lv_event_get_user_data(event);
    lv_img_cache_invalidate_src(&scene->image);
#ifdef HZ_USE_G2D
    if(scene->dma)hz_g2d_background_free(scene->pixels);else
#endif
    free(scene->pixels);
    free(scene);
}
bool hz_static_scene_cache(lv_obj_t *screen) {
    if(getenv("HZ_UI_STATIC_CACHE") && !strcmp(getenv("HZ_UI_STATIC_CACHE"),"0"))return false;
    lv_disp_t *display=lv_obj_get_disp(screen);
    unsigned width=lv_disp_get_hor_res(display),height=lv_disp_get_ver_res(display);
    if(width!=1024 || height!=600)return false;
    uint32_t start=lv_tick_get();
    struct scene *scene=calloc(1,sizeof(*scene));if(!scene)return false;
#ifdef HZ_USE_G2D
    scene->pixels=hz_g2d_background_alloc();scene->dma=scene->pixels!=NULL;
#endif
    if(!scene->pixels)scene->pixels=malloc(width*height*sizeof(*scene->pixels));
    if(!scene->pixels) {free(scene);return false;}
    hz_scene_raster(screen,scene->pixels);
    scene->image.header.cf=LV_IMG_CF_TRUE_COLOR;scene->image.header.w=width;scene->image.header.h=height;
    scene->image.data_size=width*height*sizeof(*scene->pixels);scene->image.data=(const uint8_t *)scene->pixels;
#ifdef HZ_USE_G2D
    if(scene->dma)hz_g2d_background_ready();
#endif
    lv_obj_clean(screen);
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_NONE,0);
    /* A screen background must remain opaque during press/slide invalidation.
     * Attach the raster to the screen itself rather than making the root
     * transparent and relying on a child image to establish screen coverage. */
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_bg_img_src(screen,&scene->image,0);
    lv_obj_set_style_bg_img_opa(screen,LV_OPA_COVER,0);
    lv_obj_add_event_cb(screen,release,LV_EVENT_DELETE,scene);
    if(getenv("HZ_UI_PROFILE"))fprintf(stderr,"UI static background: %u ms, %u bytes (%s)\n",lv_tick_elaps(start),scene->image.data_size,scene->dma?"DMA":"CPU");
    return true;
}
