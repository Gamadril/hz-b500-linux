/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle_clock.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned clock_calls;
static int clock_error;
static struct timespec requested;
int __wrap_clock_settime(clockid_t clock,const struct timespec *time) {
    assert(clock==CLOCK_REALTIME);
    requested=*time;clock_calls++;
    if(clock_error) {errno=clock_error;return -1;}
    return 0;
}
static void sample(struct vehicle_decoder *d,int year,int month,int day,int hour,int minute,int second,uint64_t now) {
    struct can_frame f={.can_id=0x2f8,.can_dlc=8,
        .data={hour,minute,second,day,(month<<4)|15,year&255,year>>8,255}};
    struct vehicle_event e;
    vehicle_frame(d,&f,now,&e);
}
static struct vehicle_clock_sync context(const char *dir,const char *name,char path[512]) {
    assert(snprintf(path,512,"%s/%s",dir,name)<512);
    return (struct vehicle_clock_sync){.marker_path=path};
}
static void expect_utc(int year,int month,int day,int hour,int minute,int second,long nsec) {
    struct tm utc;
    assert(gmtime_r(&requested.tv_sec,&utc));
    assert(utc.tm_year==year-1900 && utc.tm_mon==month-1 && utc.tm_mday==day);
    assert(utc.tm_hour==hour && utc.tm_min==minute && utc.tm_sec==second && requested.tv_nsec==nsec);
}
int main(int argc,char **argv) {
    assert(argc==2);
    assert(!setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1));tzset();
    char paths[8][512];
    struct vehicle_clock_sync sync=context(argv[1],"boot",paths[0]);
    struct vehicle_decoder d;
    vehicle_init(&d,"clock",false);d.state.connected=true;
    assert(!vehicle_sync_system_time(&sync,&d.state,1000));
    sample(&d,2026,10,22,14,37,56,1000);
    d.state.simulated=true;
    assert(!vehicle_sync_system_time(&sync,&d.state,1250) && !clock_calls);
    assert(access(paths[0],F_OK)<0 && errno==ENOENT);
    d.state.simulated=false;d.state.connected=false;
    assert(!vehicle_sync_system_time(&sync,&d.state,1250));
    d.state.connected=true;
    assert(!vehicle_sync_system_time(&sync,&d.state,999));
    assert(!vehicle_sync_system_time(&sync,&d.state,6001));
    sample(&d,2026,2,30,14,0,0,1000);
    assert(!vehicle_sync_system_time(&sync,&d.state,1000) && !clock_calls);
    sample(&d,2026,10,22,14,37,56,1000);
    clock_error=EPERM;
    assert(vehicle_sync_system_time(&sync,&d.state,1250)==-1 && errno==EPERM);
    assert(!sync.synchronized && clock_calls==1);
    clock_error=0;
    assert(vehicle_sync_system_time(&sync,&d.state,2250)==1 && clock_calls==2);
    expect_utc(2026,10,22,12,37,57,250000000);
    assert(sync.synchronized);
    /* Repeated samples, changed vehicle time and service restart in one boot. */
    sample(&d,2027,1,1,0,0,0,3000);
    assert(!vehicle_sync_system_time(&sync,&d.state,3000) && clock_calls==2);
    struct vehicle_clock_sync restarted={.marker_path=paths[0]};
    assert(!vehicle_sync_system_time(&restarted,&d.state,3000) && clock_calls==2);
    assert(restarted.synchronized);
    /* A new boot has an empty RAM marker directory and may synchronize again. */
    struct vehicle_clock_sync next=context(argv[1],"next-boot",paths[1]);
    assert(vehicle_sync_system_time(&next,&d.state,3000)==1 && clock_calls==3);
    expect_utc(2026,12,31,23,0,0,0);
    struct vehicle_clock_sync summer=context(argv[1],"summer",paths[2]);
    sample(&d,2026,7,1,14,0,0,4000);
    assert(vehicle_sync_system_time(&summer,&d.state,4000)==1);
    expect_utc(2026,7,1,12,0,0,0);
    struct vehicle_clock_sync gap=context(argv[1],"dst-gap",paths[3]);
    sample(&d,2026,3,29,2,30,0,5000);
    unsigned before=clock_calls;
    assert(vehicle_sync_system_time(&gap,&d.state,5000)==-1 && errno==EINVAL);
    assert(clock_calls==before && !gap.synchronized);
    sample(&d,2026,3,29,3,0,0,6000);
    assert(vehicle_sync_system_time(&gap,&d.state,6000)==1);
    expect_utc(2026,3,29,1,0,0,0);
    /* A configurable timezone changes UTC conversion, without code changes. */
    assert(!setenv("TZ","UTC0",1));tzset();
    struct vehicle_clock_sync utc=context(argv[1],"utc",paths[4]);
    sample(&d,2026,10,22,14,37,56,7000);
    assert(vehicle_sync_system_time(&utc,&d.state,7000)==1);
    expect_utc(2026,10,22,14,37,56,0);
    struct vehicle_clock_sync past=context(argv[1],"past",paths[5]);
    sample(&d,1,1,1,0,0,0,8000);before=clock_calls;
    assert(vehicle_sync_system_time(&past,&d.state,8000)==-1 && clock_calls==before);
    struct vehicle_clock_sync future=context(argv[1],"future",paths[6]);
    sample(&d,2040,1,1,0,0,0,9000);
    assert(vehicle_sync_system_time(&future,&d.state,9000)==1);
    expect_utc(2040,1,1,0,0,0,0);
    puts("PASS: once-per-boot system time, restart marker, retry, Berlin CET/CEST, receive age, invalid/simulated data and configurable timezone (clock syscall mocked)");
}
