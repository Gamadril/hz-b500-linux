/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "gear_calibration_store.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static struct hz_gear_cal_state state={.message="Start calibration while the vehicle is stationary."};
static bool started,pending;
static enum hz_gear_cal_action requested;
static char selected;
static struct vehicle_cal_threshold requested_threshold;
static uint64_t measured;
static uint64_t now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int exchange(const char *command,struct vehicle_cal_table *table) {
    const char *path=getenv("HZ_GEAR_CALIBRATION_SOCKET");
    char reply[VEHICLE_CAL_REPLY_SIZE];
    int result=vehicle_cal_request(path?path:VEHICLE_CAL_SOCKET,command,reply,sizeof(reply),8500);
    return result?result:vehicle_cal_parse(command,reply,table);
}
static void *worker(void *unused) {
    (void)unused;pthread_mutex_lock(&mutex);
    for(;;) {
        while(!pending && !state.active)pthread_cond_wait(&wake,&mutex);
        if(!pending) {
            struct timespec until;clock_gettime(CLOCK_REALTIME,&until);
            until.tv_nsec+=200000000;if(until.tv_nsec>=1000000000) {until.tv_sec++;until.tv_nsec-=1000000000;}
            pthread_cond_timedwait(&wake,&mutex,&until);
            if(!pending && !state.active)continue;
        }
        bool action=pending;enum hz_gear_cal_action job=requested;char gear=selected;
        struct vehicle_cal_table table=state.table;
        struct vehicle_cal_threshold threshold=requested_threshold;
        pending=false;pthread_mutex_unlock(&mutex);
        int result=0;bool opened=false,reviewed=false,thresholds_read=false;
        if(!action) result=exchange("GP",&table);
        else if(job==HZ_GEAR_BEGIN) {
            table.positions_known=false;
            table.thresholds_known=false;
            result=exchange("GB",&table);
            if(result==0 || result==VEHICLE_CAL_REJECTED) {
                opened=result==0;
                result=exchange("GL",&table);reviewed=result==0;
                if(reviewed) {
                    opened=true; /* Adopt a session surviving UI restart. */
                    thresholds_read=exchange("GT",&table)==0;
                }
            }
        } else if(job==HZ_GEAR_CAPTURE) {
            char command[]={'G','C',gear,0};result=exchange(command,&table);
            if(!result) {
                table.positions[vehicle_cal_gear(gear)].set=true;
                table.positions_known=false;
                struct vehicle_cal_table review=table;
                reviewed=!exchange("GL",&review);
                if(reviewed)table=review;
            }
        } else if(job==HZ_GEAR_THRESHOLD) {
            char command[VEHICLE_CAL_COMMAND_SIZE];
            snprintf(command,sizeof(command),"GT%c,%d.%03d,%d.%03d",gear,
                threshold.entry/1000,threshold.entry%1000,threshold.exit/1000,threshold.exit%1000);
            result=exchange(command,&table);
            if(!result) {
                table.thresholds[vehicle_cal_gear(gear)]=threshold;
                struct vehicle_cal_table review=table;
                if(!exchange("GT",&review))table=review;
            }
        } else result=exchange(job==HZ_GEAR_SAVE?"GS":"GX",&table);
        pthread_mutex_lock(&mutex);
        if(!action) {
            state.table.fresh=!result && table.fresh;
            if(!result && table.fresh) {
                state.table.forward=table.forward;state.table.side=table.side;measured=now_ms();
            }
        } else {
            state.busy=false;
            if(job==HZ_GEAR_BEGIN) {
                state.active=opened || (result!=VEHICLE_CAL_REJECTED && result!=VEHICLE_CAL_UNAVAILABLE && result!=0);
                state.table.fresh=false;
                state.table.positions_known=false;state.table.thresholds_known=false;
                if(reviewed) {state.table=table;state.table.fresh=false;state.dirty=false;}
                snprintf(state.message,sizeof(state.message),"%s",!result?(thresholds_read?
                    "CAN paused. Select a gear and hold it still for two seconds.":
                    "Positions loaded; jitter settings unavailable. Check adapter firmware, then Start / resume."):
                    opened?"Calibration started, but the position table could not be read. Retry Start or Cancel.":
                    "Adapter unavailable or calibration state uncertain. Retry Start or Cancel.");
            } else if(!result && job==HZ_GEAR_CAPTURE) {
                state.table=table;state.dirty=true;
                snprintf(state.message,sizeof(state.message),reviewed?
                    "Gear %c recorded. Finish & save writes the table to the adapter.":
                    "Gear %c recorded; position readback failed. Start / resume to review it.",gear);
            } else if(!result && job==HZ_GEAR_THRESHOLD) {
                state.table=table;state.dirty=true;
                snprintf(state.message,sizeof(state.message),"Gear %c correction confirmed. Finish & save stores positions and radii.",gear);
            } else if(!result) {
                state.active=false;state.table.fresh=false;state.dirty=false;
                if(job==HZ_GEAR_CANCEL) {
                    memset(state.table.positions,0,sizeof(state.table.positions));
                    state.table.positions_known=false;
                    state.table.thresholds_known=false;
                }
                snprintf(state.message,sizeof(state.message),"%s",job==HZ_GEAR_SAVE?
                    "Calibration saved on the adapter. CAN reception resumed.":"Calibration closed. Previously saved positions remain in use.");
            } else {
                state.table.fresh=false;
                if(job==HZ_GEAR_THRESHOLD && result!=VEHICLE_CAL_REJECTED)state.table.thresholds_known=false;
                snprintf(state.message,sizeof(state.message),"%s",job==HZ_GEAR_THRESHOLD?
                    (result==VEHICLE_CAL_REJECTED?"Correction rejected. Previous confirmed radii restored.":
                     "Correction outcome unknown. Start / resume to read the adapter settings."):
                    job==HZ_GEAR_CAPTURE?
                    "Position was not confirmed. Check the sensors and hold the gear still.":
                    job==HZ_GEAR_SAVE?(result==VEHICLE_CAL_REJECTED?
                        "Save was rejected. Calibration remains open; retry Finish & save or Cancel.":
                        "Save outcome is unknown. Start / resume to review the table, then save or cancel."):
                    "Cancel was not confirmed. Use Start / resume, then Cancel; the adapter may still be calibrating.");
            }
        }
    }
    return NULL;
}
static bool queue(enum hz_gear_cal_action action,char gear,int entry,int exit) {
    if(action<HZ_GEAR_BEGIN || action>HZ_GEAR_THRESHOLD ||
       ((action==HZ_GEAR_CAPTURE || action==HZ_GEAR_THRESHOLD) && vehicle_cal_gear(gear)<0) ||
       (action==HZ_GEAR_THRESHOLD && (entry<=0 || entry>exit || exit>180000)))return false;
    pthread_mutex_lock(&mutex);bool accepted=false;
    if(!state.busy && (action==HZ_GEAR_BEGIN || state.active) &&
       (action!=HZ_GEAR_CAPTURE || (state.table.fresh && now_ms()-measured<=1000)) &&
       (action!=HZ_GEAR_THRESHOLD || state.table.thresholds_known)) {
        if(!started) {
            pthread_t thread;
            if(pthread_create(&thread,NULL,worker,NULL))goto done;
            pthread_detach(thread);started=true;
        }
        requested=action;selected=gear;requested_threshold=(struct vehicle_cal_threshold){entry,exit};
        pending=true;state.busy=true;accepted=true;
        snprintf(state.message,sizeof(state.message),"%s",action==HZ_GEAR_BEGIN?"Starting calibration...":
            action==HZ_GEAR_CAPTURE?"Recording position...":action==HZ_GEAR_THRESHOLD?"Updating jitter correction...":
            action==HZ_GEAR_SAVE?"Saving calibration...":"Canceling calibration...");
        pthread_cond_signal(&wake);
    }
done:pthread_mutex_unlock(&mutex);return accepted;
}
bool hz_gear_cal_action(enum hz_gear_cal_action action,char gear) {
    if(action==HZ_GEAR_THRESHOLD)return false;
    return queue(action,gear,0,0);
}
bool hz_gear_cal_threshold(char gear,int entry,int exit) {
    return queue(HZ_GEAR_THRESHOLD,gear,entry,exit);
}
void hz_gear_cal_snapshot(struct hz_gear_cal_state *out) {
    pthread_mutex_lock(&mutex);*out=state;
    if(!measured || now_ms()-measured>1000)out->table.fresh=false;
    pthread_mutex_unlock(&mutex);
}
