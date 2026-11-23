/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_COOLANT_GAUGE_H
#define HZ_COOLANT_GAUGE_H

/* The printed 40, 80 and 120 C ticks span a quarter turn. */
static inline int dashboard_coolant_angle(int temperature)
{
    if(temperature<40)temperature=40;
    if(temperature>120)temperature=120;
    return (temperature-40)*900/80;
}
#endif
