/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_NAVIGATION_H
#define HZ_NAVIGATION_H
void hz_dashboard_open(void);
void hz_home_open(void);
void hz_ui_set_vehicle_mode(int enabled);
int hz_ui_vehicle_mode(void);
void hz_ui_poll(void);
#endif
