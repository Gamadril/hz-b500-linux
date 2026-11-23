/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_SPEEDOMETER_H
#define HZ_SPEEDOMETER_H

/* The retained dial's 0..220 ticks span 237.8 degrees. The hand artwork's
 * axis starts 1.3 degrees clockwise of zero; LVGL angles use tenths of a degree. */
static inline int dashboard_speed_angle(int speed)
{
    if(speed<0)speed=0;
    if(speed>220)speed=220;
    return -13+(speed*2378+110)/220;
}
#endif
