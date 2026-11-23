/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_UI_H
#define VEHICLE_UI_H
#include "vehicle.h"
/* Start one worker; all LVGL changes happen in poll on the UI thread. */
int preview_vehicle_start(const char *socket_path, int pdc_only);
void preview_vehicle_poll(void);
bool preview_vehicle_time(struct tm *);
#endif
