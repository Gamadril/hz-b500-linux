/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_GEAR_CALIBRATION_STORE_H
#define HZ_GEAR_CALIBRATION_STORE_H
#include "vehicle_calibration.h"
enum hz_gear_cal_action { HZ_GEAR_BEGIN, HZ_GEAR_CAPTURE, HZ_GEAR_SAVE, HZ_GEAR_CANCEL, HZ_GEAR_THRESHOLD };
struct hz_gear_cal_state {
    bool active,busy,dirty;
    struct vehicle_cal_table table;
    char message[192];
};
/* All adapter I/O runs on one worker. These functions never call LVGL. */
bool hz_gear_cal_action(enum hz_gear_cal_action action,char gear);
bool hz_gear_cal_threshold(char gear,int entry,int exit);
void hz_gear_cal_snapshot(struct hz_gear_cal_state *state);
#endif
