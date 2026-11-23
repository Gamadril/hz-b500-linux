/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_STATIC_SCENE_H
#define HZ_STATIC_SCENE_H
#include "lvgl/lvgl.h"
/* Call before adding live controls. Replaces the current decoration with an
 * opaque raster image; failure preserves the original objects and styles. */
bool hz_static_scene_cache(lv_obj_t *screen);
void hz_scene_raster(lv_obj_t *screen,lv_color_t *pixels);
#endif
