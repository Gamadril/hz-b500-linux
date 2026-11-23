/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_VEHICLE_BODY_H
#define HZ_VEHICLE_BODY_H
#include "lvgl/lvgl.h"
#include "vehicle.h"
lv_obj_t *hz_vehicle_body_create(lv_obj_t *parent);
/* Called on the UI thread with the subscriber's freshness-filtered snapshot. */
void hz_vehicle_body_update(const struct vehicle_state *state);
#endif
