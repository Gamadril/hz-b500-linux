/* Test adapter: staged positions, acknowledged commands and failure injection. */
#ifndef CALIBRATION_FIXTURE_H
#define CALIBRATION_FIXTURE_H
#include "vehicle_calibration.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
struct calibration_fixture {
    pthread_mutex_t mutex;
    bool active,fresh,reject_capture,reject_save,reject_thresholds,reject_threshold_read,drop,thresholds_initialized;
    int forward,side;
    unsigned captures,saves,cancels,threshold_sets;
    struct vehicle_cal_position staged[8],saved[8];
    struct vehicle_cal_threshold staged_thresholds[8],saved_thresholds[8];
};
#define CALIBRATION_FIXTURE_INIT {.mutex=PTHREAD_MUTEX_INITIALIZER,.fresh=true,.forward=12500,.side=-4250}
static void fixture_reply(struct calibration_fixture *f,const char *command,char reply[VEHICLE_CAL_REPLY_SIZE]) {
    pthread_mutex_lock(&f->mutex);reply[0]='\a';reply[1]=0;
    if(!f->thresholds_initialized) {
        for(unsigned i=0;i<8;i++)f->staged_thresholds[i]=f->saved_thresholds[i]=(struct vehicle_cal_threshold){8000,10000};
        f->thresholds_initialized=true;
    }
    if(!strcmp(command,"GB")) {
        if(!f->active) {
            f->active=true;memcpy(f->staged,f->saved,sizeof(f->staged));
            memcpy(f->staged_thresholds,f->saved_thresholds,sizeof(f->staged_thresholds));strcpy(reply,"\r");
        }
    } else if(f->active) {
        if(!strcmp(command,"GP")) {
            if(f->fresh)snprintf(reply,VEHICLE_CAL_REPLY_SIZE,"GP1,%.3f,%.3f\r",f->forward/1000.0,f->side/1000.0);
            else strcpy(reply,"GP0\r");
        } else if(!strcmp(command,"GL")) {
            size_t used=0;reply[0]=0;
            for(unsigned i=0;i<8;i++) used+=(size_t)snprintf(reply+used,VEHICLE_CAL_REPLY_SIZE-used,
                "GC%c,%d,%.3f,%.3f\r",VEHICLE_CAL_GEARS[i],f->staged[i].set,f->staged[i].forward/1000.0,f->staged[i].side/1000.0);
        } else if(!strcmp(command,"GT") && !f->reject_threshold_read) {
            size_t used=0;reply[0]=0;
            for(unsigned i=0;i<8;i++)used+=(size_t)snprintf(reply+used,VEHICLE_CAL_REPLY_SIZE-used,
                "GT%c,%.3f,%.3f\r",VEHICLE_CAL_GEARS[i],f->staged_thresholds[i].entry/1000.0,f->staged_thresholds[i].exit/1000.0);
        } else if(!strncmp(command,"GT",2) && vehicle_cal_command(command) && !f->reject_thresholds) {
            char gear;double entry,exit;
            if(sscanf(command,"GT%c,%lf,%lf",&gear,&entry,&exit)==3) {
                f->staged_thresholds[vehicle_cal_gear(gear)]=(struct vehicle_cal_threshold){(int)(entry*1000+0.5),(int)(exit*1000+0.5)};
                f->threshold_sets++;strcpy(reply,"\r");
            }
        } else if(!strcmp(command,"GX")) {
            f->active=false;f->cancels++;memcpy(f->staged,f->saved,sizeof(f->staged));strcpy(reply,"\r");
            memcpy(f->staged_thresholds,f->saved_thresholds,sizeof(f->staged_thresholds));
        } else if(!strcmp(command,"GS")) {
            bool any=false;for(unsigned i=0;i<8;i++)any|=f->staged[i].set;
            if(any && !f->reject_save) {
                memcpy(f->saved,f->staged,sizeof(f->saved));
                memcpy(f->saved_thresholds,f->staged_thresholds,sizeof(f->saved_thresholds));
                f->active=false;f->saves++;strcpy(reply,"\r");
            }
        } else if(strlen(command)==3 && command[0]=='G' && command[1]=='C') {
            int i=vehicle_cal_gear(command[2]);
            if(i>=0 && f->fresh && !f->reject_capture) {
                f->staged[i]=(struct vehicle_cal_position){true,f->forward,f->side};f->captures++;strcpy(reply,"\r");
            }
        }
    }
    if(f->drop) {reply[0]=0;f->drop=false;}
    pthread_mutex_unlock(&f->mutex);
}
#endif
