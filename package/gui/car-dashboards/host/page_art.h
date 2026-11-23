/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_PAGE_ART_H
#define HZ_PAGE_ART_H
#include "lvgl/lvgl.h"
bool hz_page_art_load(lv_obj_t *screen,const char *name);
void hz_page_art_bind(lv_obj_t *screen,const char *name,lv_obj_t *const *live,unsigned count);
bool hz_page_art_ready(lv_obj_t *screen);
/* Load a standalone <name>.hzp image; pixels stay allocated for the UI lifetime. */
bool hz_art_image(const char *name,lv_img_dsc_t *image);
/* Free the screen's plate once it is unloaded, like non-Home pages. Video uses
 * this to return Home's DMA background to the decoder; Home rebinds on return. */
void hz_page_art_release(lv_obj_t *screen);
/* The exporter uses these same live-object boundaries and widget ordering. */
unsigned hz_launcher_live(const char *page,lv_obj_t **objects,unsigned capacity);
#endif
