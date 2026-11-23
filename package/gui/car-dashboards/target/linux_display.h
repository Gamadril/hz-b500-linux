/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_LINUX_DISPLAY_H
#define HZ_LINUX_DISPLAY_H
#include "lvgl/lvgl.h"
int hz_display_init(const char *fb, const char *touch);
void hz_display_close(void);
void hz_display_configure(lv_disp_drv_t *,lv_disp_draw_buf_t *,lv_color_t *,unsigned);
void hz_display_flush(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
void hz_touch_read(lv_indev_drv_t *, lv_indev_data_t *);
void hz_touch_discard(void);
#endif
