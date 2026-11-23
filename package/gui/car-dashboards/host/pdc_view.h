/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_PDC_VIEW_H
#define HZ_PDC_VIEW_H
#include "lvgl/lvgl.h"
#include "vehicle.h"
lv_obj_t *hz_pdc_create(void);
void hz_pdc_update(const struct vehicle_state *);
#endif
