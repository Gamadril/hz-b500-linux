/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle.h"
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

int main(int argc,char **argv) {
    assert(argc==2);
    int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    assert(master>=0 && !grantpt(master) && !unlockpt(master));
    const char *path=ptsname(master);assert(path);
    int slave=open(path,O_RDWR|O_NOCTTY|O_CLOEXEC);assert(slave>=0);
    struct termios2 original,current;
    assert(!ioctl(slave,TCGETS2,&original));
    pid_t child=fork();assert(child>=0);
    if(!child) {
        execlp("bash","bash",argv[1],"--device",path,"--speed","80","--rpm","845",
               "--time","2026-10-25 14:37:56","--count","3","--hz","10",NULL);
        _exit(127);
    }
    char data[1024];size_t size=0;int status=0,finished=0,checked=0;
    for(unsigned attempt=0;attempt<400;attempt++) {
        struct pollfd p={.fd=master,.events=POLLIN};
        int ready=poll(&p,1,50);assert(ready>=0);
        if(ready>0) {
            ssize_t n=read(master,data+size,sizeof(data)-size);
            assert(n>0);size+=n;assert(size<sizeof(data));
            if(!checked) {
                assert(!ioctl(slave,TCGETS2,&current));
                assert(current.c_ispeed==750000 && current.c_ospeed==750000);
                assert((current.c_cflag&(CSIZE|PARENB|CSTOPB|CRTSCTS))==CS8);
                checked=1;
            }
        }
        if(!finished) {
            pid_t result=waitpid(child,&status,WNOHANG);assert(result>=0);
            finished=result==child;
        }
        if(finished && !ready) break;
    }
    assert(finished && WIFEXITED(status) && WEXITSTATUS(status)==0 && checked);
    assert(!ioctl(slave,TCGETS2,&current));
    assert(!memcmp(&original,&current,sizeof(original)));
    struct vehicle_decoder decoder;struct vehicle_event event;
    vehicle_init(&decoder,"serial-wire-test",false);
    unsigned frames=0;size_t offset=0;uint64_t now=1000;
    while(offset<size) {
        /* Three complete, eight-byte standard frames per sample, CR only. */
        assert(size-offset>=22 && data[offset]=='t' && data[offset+4]=='8' && data[offset+21]=='\r');
        char id[4]={0};memcpy(id,data+offset+1,3);
        struct can_frame f={.can_id=strtoul(id,NULL,16),.can_dlc=8};
        const unsigned ids[]={0x1a6,0x0aa,0x2f8};assert(f.can_id==ids[frames%3]);
        for(unsigned i=0;i<8;i++) {
            char byte[3]={0};memcpy(byte,data+offset+5+i*2,2);
            char *end;f.data[i]=strtoul(byte,&end,16);assert(!*end);
        }
        if(f.can_id==0x0aa) assert(f.data[4]==0x34 && f.data[5]==0x0d); /* Published 845 RPM example. */
        vehicle_frame(&decoder,&f,now++,&event);frames++;offset+=22;
    }
    assert(frames==9 && decoder.state.received_frames==9 && decoder.state.connected);
    assert(decoder.state.signals[V_SPEED].valid && decoder.state.signals[V_SPEED].value==80);
    assert(decoder.state.signals[V_RPM].valid && decoder.state.signals[V_RPM].value==845);
    struct tm calendar;
    assert(vehicle_clock_time(&decoder.state,now,&calendar));
    assert(calendar.tm_year==126 && calendar.tm_mon==9 && calendar.tm_mday==25);
    assert(calendar.tm_hour==14 && calendar.tm_min==37 && calendar.tm_sec>=56 && calendar.tm_sec<=57);
    /* The harness must be able to stop an infinite simulator by its PID. */
    child=fork();assert(child>=0);
    if(!child) {
        execlp("bash","bash",argv[1],"--device",path,"--speed","80","--rpm","845",NULL);
        _exit(127);
    }
    struct pollfd p={.fd=master,.events=POLLIN};assert(poll(&p,1,5000)>0);
    assert(kill(child,SIGTERM)==0);
    finished=0;
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(waitpid(child,&status,WNOHANG)==child) {finished=1;break;}
        usleep(10000);
    }
    assert(finished && WIFEXITED(status) && WEXITSTATUS(status)==143);
    assert(!ioctl(slave,TCGETS2,&current));assert(!memcmp(&original,&current,sizeof(original)));
    close(slave);close(master);
    puts("PASS: Bash simulator over a real PTY, 750000 baud 8N1, CR framing, production speed/RPM/calendar decoding and termios restoration");
    puts("PASS: stopping an infinite simulator terminates its serial sender and restores the port");
    return 0;
}
