/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_G2D_DISPLAY_H
#define HZ_G2D_DISPLAY_H
#include "lvgl/lvgl.h"
/* One cached DMA drawing surface; framebuffer scanout remains kernel-owned. */
lv_color_t *hz_g2d_open(int framebuffer,unsigned stride,unsigned rows,unsigned xoffset,unsigned yoffset);
void hz_g2d_close(void);
lv_color_t *hz_g2d_buffer(void);
bool hz_g2d_present(void);
void hz_g2d_draw_ctx_init(lv_disp_drv_t *,lv_draw_ctx_t *);
/* At most one immutable full-screen decoration cache, outside LVGL's heap. */
lv_color_t *hz_g2d_background_alloc(void);
void hz_g2d_background_ready(void);
void hz_g2d_background_free(lv_color_t *);
/* Two temporary page rasters, used only for a running slide animation. */
lv_color_t *hz_g2d_snapshot_alloc(void);
void hz_g2d_snapshot_ready(lv_color_t *);
void hz_g2d_snapshot_free(lv_color_t *);
bool hz_g2d_is_surface(const lv_color_t *);
#endif
