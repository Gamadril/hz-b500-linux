/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_UI_UPDATE_H
#define HZ_UI_UPDATE_H
#include "lvgl/lvgl.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
static inline void hz_page_profile(const char *name,uint32_t start,bool first) {
    if(getenv("HZ_UI_PROFILE"))fprintf(stderr,"UI page construction: %s %u ms (%s)\n",name,lv_tick_elaps(start),first?"first":"cached");
}
/* LVGL label setters invalidate even when the displayed string is identical. */
static inline void hz_label_text(lv_obj_t *label,const char *text) {
    if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);
}
static inline void hz_label_text_fmt(lv_obj_t *label,const char *format,...) {
    char text[256];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);hz_label_text(label,text);
}
static inline void hz_style_opa(lv_obj_t *object,lv_opa_t opacity) {
    if(lv_obj_get_style_opa(object,0)!=opacity)lv_obj_set_style_opa(object,opacity,0);
}
static inline void hz_style_text_color(lv_obj_t *object,lv_color_t color) {
    if(lv_obj_get_style_text_color(object,0).full!=color.full)lv_obj_set_style_text_color(object,color,0);
}
#endif
