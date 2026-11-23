/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle_clock.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

int vehicle_sync_system_time(struct vehicle_clock_sync *sync,const struct vehicle_state *state,uint64_t now) {
    struct tm local;
    if(sync->synchronized || state->simulated || !vehicle_clock_time(state,now,&local)) return 0;
    /* Retained display time is allowed to advance indefinitely, but a system
     * clock correction must still originate from a recently received sample. */
    if(now-state->signals[V_TIME_HOUR].updated_ms>5000) return 0;
    struct tm converted=local;
    /* CAN carries local time. TZ comes from the system UCI configuration;
     * mktime chooses the applicable standard/daylight offset for this date. */
    errno=0;
    time_t seconds=mktime(&converted);
    if(seconds==(time_t)-1) {if(!errno) errno=EOVERFLOW;return -1;}
    /* Reject unrepresentable/normalized dates, including the DST spring gap. */
    if(seconds<0 || converted.tm_year!=local.tm_year || converted.tm_mon!=local.tm_mon ||
       converted.tm_mday!=local.tm_mday || converted.tm_hour!=local.tm_hour ||
       converted.tm_min!=local.tm_min || converted.tm_sec!=local.tm_sec) {
        errno=EINVAL;return -1;
    }
    struct timespec time={.tv_sec=seconds,
        .tv_nsec=(long)((now-state->signals[V_TIME_HOUR].updated_ms)%1000)*1000000};
    /* /var/run is RAM on the target. Keep the once-per-boot decision across
     * service restarts, and serialize simultaneous service instances. */
    int fd=open(sync->marker_path,O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(fd<0) return -1;
    int result=-1;
    if(flock(fd,LOCK_EX|LOCK_NB)<0) {
        if(errno==EWOULDBLOCK) result=0;
    } else {
        char marker;
        ssize_t n=pread(fd,&marker,1,0);
        if(n==1 && marker=='1') {sync->synchronized=true;result=0;}
        else if(n>=0 && clock_settime(CLOCK_REALTIME,&time)==0) {
            sync->synchronized=true;
            ssize_t written=pwrite(fd,"1",1,0);
            if(written==1) result=1;
            else if(written>=0) errno=EIO;
        }
    }
    int saved_errno=errno;
    close(fd);errno=saved_errno;
    return result;
}
