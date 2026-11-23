/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_CALIBRATION_H
#define VEHICLE_CALIBRATION_H
#include <stdbool.h>
#include <stddef.h>
#define VEHICLE_CAL_SOCKET "/var/run/vehicle-calibration.sock"
#define VEHICLE_CAL_GEARS "RN123456"
#define VEHICLE_CAL_REPLY_SIZE 512
#define VEHICLE_CAL_COMMAND_SIZE 64
struct vehicle_cal_position { bool set; int forward,side; }; /* millidegrees */
struct vehicle_cal_threshold { int entry,exit; }; /* millidegrees */
struct vehicle_cal_table {
    bool fresh,positions_known,thresholds_known;
    int forward,side;
    struct vehicle_cal_position positions[8];
    struct vehicle_cal_threshold thresholds[8];
};
/* Bit masks of other calibrated gears, with wrapped forward/side distances. */
struct vehicle_cal_overlap { unsigned entry,exit,center; };
struct vehicle_cal_overlap vehicle_cal_overlaps(const struct vehicle_cal_table *table,unsigned gear);
enum { VEHICLE_CAL_OK=0, VEHICLE_CAL_REJECTED=-1,
       VEHICLE_CAL_TRANSPORT=-2, VEHICLE_CAL_PROTOCOL=-3, VEHICLE_CAL_UNAVAILABLE=-4 };
int vehicle_cal_gear(char gear);
bool vehicle_cal_command(const char *command);
/* Transactional parser: malformed replies never partially update the table. */
int vehicle_cal_parse(const char *command,const char *reply,struct vehicle_cal_table *table);
int vehicle_cal_serial(int fd,const char *command,char *reply,size_t size,unsigned timeout_ms);
int vehicle_cal_request(const char *path,const char *command,char *reply,size_t size,unsigned timeout_ms);
#endif
