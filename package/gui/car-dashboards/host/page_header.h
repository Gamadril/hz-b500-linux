/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_PAGE_HEADER_H
#define HZ_PAGE_HEADER_H
#include "lvgl/lvgl.h"
#define HZ_HEADER_HEIGHT 64
struct hz_page_header { lv_obj_t *bar,*title,*context; };
/* Home runs leave, or returns Home when leave is NULL.
 * A page can supply its media/stopwatch cleanup callback before navigating.
 * navigation=false omits the button on the main HMI page. */
struct hz_page_header hz_page_header_create(lv_obj_t *screen,const char *title,
    const char *context,lv_event_cb_t leave,bool navigation);
#endif
