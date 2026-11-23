/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_CLOCK_H
#define VEHICLE_CLOCK_H
#include "vehicle.h"
struct vehicle_clock_sync {
    bool synchronized;
    const char *marker_path;
};
/* 1: clock set, 0: already synchronized or no live sample, -1: error (errno). */
int vehicle_sync_system_time(struct vehicle_clock_sync *, const struct vehicle_state *, uint64_t now);
#endif
