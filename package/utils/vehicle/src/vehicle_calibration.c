/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle_calibration.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
int vehicle_cal_gear(char gear) {
    const char *found=gear?strchr(VEHICLE_CAL_GEARS,gear):NULL;
    return found?(int)(found-VEHICLE_CAL_GEARS):-1;
}
static bool radius(const char **cursor,int *out,char terminator) {
    const char *p=*cursor;int value=0,fraction=0,digits=0;
    if(*p<'0' || *p>'9')return false;
    while(*p>='0' && *p<='9') {
        value=value*10+(*p++-'0');if(value>180)return false;
    }
    if(*p=='.') {
        p++;
        while(*p>='0' && *p<='9') {
            if(++digits>3)return false;
            fraction=fraction*10+(*p++-'0');
        }
        if(!digits)return false;
    }
    while(digits++<3)fraction*=10;
    value=value*1000+fraction;
    if(value<=0 || value>180000 || *p!=terminator)return false;
    *cursor=p+(terminator?1:0);*out=value;return true;
}
bool vehicle_cal_command(const char *command) {
    if(!command)return false;
    if(!strncmp(command,"GT",2) && strlen(command)>2) {
        if(vehicle_cal_gear(command[2])<0 || command[3]!=',')return false;
        const char *p=command+4;int entry,exit;
        return radius(&p,&entry,',') && radius(&p,&exit,0) && entry<=exit;
    }
    return !strcmp(command,"GB") || !strcmp(command,"GP") || !strcmp(command,"GL") || !strcmp(command,"GT") ||
        !strcmp(command,"GS") || !strcmp(command,"GX") ||
        (strlen(command)==3 && command[0]=='G' && command[1]=='C' && vehicle_cal_gear(command[2])>=0);
}
static bool angle(const char **cursor,int *out,char terminator) {
    const char *p=*cursor;bool negative=*p=='-';if(*p=='-' || *p=='+')p++;
    if(*p<'0' || *p>'9')return false;
    int64_t value=0;
    while(*p>='0' && *p<='9') {
        value=value*10+(*p++-'0');if(value>INT_MAX/1000+1)return false;
    }
    if(*p++!='.')return false;
    for(unsigned i=0;i<3;i++) {
        if(*p<'0' || *p>'9')return false;
        value=value*10+(*p++-'0');
    }
    if(*p++!=terminator)return false;
    if(negative)value=-value;
    if(value<INT_MIN || value>INT_MAX)return false;
    *out=(int)value;*cursor=p;return true;
}
int vehicle_cal_parse(const char *command,const char *reply,struct vehicle_cal_table *table) {
    if(!vehicle_cal_command(command) || !reply || !table)return VEHICLE_CAL_PROTOCOL;
    if(!strcmp(reply,"\a"))return VEHICLE_CAL_REJECTED;
    struct vehicle_cal_table next=*table;
    if(!strcmp(command,"GP")) {
        if(!strcmp(reply,"GP0\r"))next.fresh=false;
        else {
            if(strncmp(reply,"GP1,",4))return VEHICLE_CAL_PROTOCOL;
            const char *p=reply+4;
            if(!angle(&p,&next.forward,',') || !angle(&p,&next.side,'\r') || *p)return VEHICLE_CAL_PROTOCOL;
            next.fresh=true;
        }
    } else if(!strcmp(command,"GL")) {
        const char *p=reply;
        for(unsigned i=0;i<8;i++) {
            char prefix[5]={'G','C',VEHICLE_CAL_GEARS[i],',',0};
            if(strncmp(p,prefix,4))return VEHICLE_CAL_PROTOCOL;
            p+=4;if((*p!='0' && *p!='1') || p[1]!=',')return VEHICLE_CAL_PROTOCOL;
            next.positions[i].set=*p=='1';p+=2;
            if(!angle(&p,&next.positions[i].forward,',') || !angle(&p,&next.positions[i].side,'\r'))return VEHICLE_CAL_PROTOCOL;
        }
        if(*p)return VEHICLE_CAL_PROTOCOL;
        next.positions_known=true;
    } else if(!strcmp(command,"GT")) {
        const char *p=reply;
        for(unsigned i=0;i<8;i++) {
            char prefix[5]={'G','T',VEHICLE_CAL_GEARS[i],',',0};
            if(strncmp(p,prefix,4))return VEHICLE_CAL_PROTOCOL;
            p+=4;
            if(!radius(&p,&next.thresholds[i].entry,',') || !radius(&p,&next.thresholds[i].exit,'\r') ||
               next.thresholds[i].entry>next.thresholds[i].exit)return VEHICLE_CAL_PROTOCOL;
        }
        if(*p)return VEHICLE_CAL_PROTOCOL;
        next.thresholds_known=true;
    } else if(strcmp(reply,"\r"))return VEHICLE_CAL_PROTOCOL;
    *table=next;return VEHICLE_CAL_OK;
}
static int64_t wrapped_difference(int a,int b) {
    int64_t difference=((int64_t)a-b)%360000;
    if(difference>180000)difference-=360000;
    if(difference< -180000)difference+=360000;
    return difference;
}
struct vehicle_cal_overlap vehicle_cal_overlaps(const struct vehicle_cal_table *table,unsigned gear) {
    struct vehicle_cal_overlap result={0};
    if(!table || gear>=8 || !table->positions_known || !table->thresholds_known || !table->positions[gear].set)return result;
    for(unsigned i=0;i<8;i++) {
        if(i==gear || !table->positions[i].set)continue;
        int64_t f=wrapped_difference(table->positions[gear].forward,table->positions[i].forward);
        int64_t s=wrapped_difference(table->positions[gear].side,table->positions[i].side);
        int64_t distance=f*f+s*s;
        int64_t entry=(int64_t)table->thresholds[gear].entry+table->thresholds[i].entry;
        int64_t exit=(int64_t)table->thresholds[gear].exit+table->thresholds[i].entry;
        int64_t own_exit=table->thresholds[gear].exit;
        if(distance<=entry*entry)result.entry|=1u<<i;
        if(distance<=exit*exit)result.exit|=1u<<i;
        if(distance<=own_exit*own_exit)result.center|=1u<<i;
    }
    return result;
}
static int64_t milliseconds(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
static int wait_fd(int fd,short events,int64_t deadline) {
    for(;;) {
        int64_t remaining=deadline-milliseconds();if(remaining<=0)return -1;
        struct pollfd p={.fd=fd,.events=events};int result=poll(&p,1,(int)remaining);
        if(result<0 && errno==EINTR)continue;
        return result>0 && (p.revents&(events|POLLHUP))?0:-1;
    }
}
static int write_all(int fd,const char *data,size_t size,int64_t deadline,bool socket) {
    size_t sent=0;
    while(sent<size) {
        if(wait_fd(fd,POLLOUT,deadline))return -1;
        ssize_t n=socket?send(fd,data+sent,size-sent,MSG_NOSIGNAL):write(fd,data+sent,size-sent);
        if(n<0 && (errno==EINTR || errno==EAGAIN))continue;
        if(n<=0)return -1;
        sent+=(size_t)n;
    }
    return 0;
}
int vehicle_cal_serial(int fd,const char *command,char *reply,size_t size,unsigned timeout_ms) {
    if(!vehicle_cal_command(command) || size<2)return VEHICLE_CAL_PROTOCOL;
    char request[VEHICLE_CAL_COMMAND_SIZE];
    if(strlen(command)+2>sizeof(request))return VEHICLE_CAL_PROTOCOL;
    snprintf(request,sizeof(request),"%s\r",command);
    int64_t deadline=milliseconds()+timeout_ms;
    if(write_all(fd,request,strlen(request),deadline,false))return VEHICLE_CAL_TRANSPORT;
    bool records=!strcmp(command,"GP") || !strcmp(command,"GL") || !strcmp(command,"GT");
    char line[96];size_t used=0,total=0;unsigned lines=0;
    for(;;) {
        if(wait_fd(fd,POLLIN,deadline))return VEHICLE_CAL_TRANSPORT;
        char c;ssize_t n=read(fd,&c,1);
        if(n<0 && (errno==EINTR || errno==EAGAIN))continue;
        if(n!=1)return VEHICLE_CAL_TRANSPORT;
        if(c=='\n')continue;
        if(c=='\a') {reply[0]='\a';reply[1]=0;return VEHICLE_CAL_REJECTED;}
        if(c!='\r') {
            if(used+1>=sizeof(line))return VEHICLE_CAL_PROTOCOL;
            line[used++]=c;continue;
        }
        line[used]=0;
        /* Queued SLCAN records and old bare acknowledgments may precede a
         * tagged GP/GL/GT response, especially when leaving N_SLCAN. */
        const char *prefix=!strcmp(command,"GP")?"GP":!strcmp(command,"GL")?"GC":"GT";
        bool wanted=records?!strncmp(line,prefix,2):used==0;
        if(wanted) {
            if(total+used+2>size)return VEHICLE_CAL_PROTOCOL;
            memcpy(reply+total,line,used);total+=used;reply[total++]='\r';reply[total]=0;
            if(records && strcmp(command,"GP")) {if(++lines<8) {used=0;continue;}}
            struct vehicle_cal_table table={0};return vehicle_cal_parse(command,reply,&table);
        }
        used=0;
    }
}
int vehicle_cal_request(const char *path,const char *command,char *reply,size_t size,unsigned timeout_ms) {
    if(!vehicle_cal_command(command) || !path || size<2)return VEHICLE_CAL_PROTOCOL;
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if(strlen(path)>=sizeof(address.sun_path))return VEHICLE_CAL_PROTOCOL;
    snprintf(address.sun_path,sizeof(address.sun_path),"%s",path);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)return VEHICLE_CAL_TRANSPORT;
    int result=VEHICLE_CAL_TRANSPORT;int64_t deadline=milliseconds()+timeout_ms;
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))) {
        if(errno!=EINPROGRESS || wait_fd(fd,POLLOUT,deadline)) {result=VEHICLE_CAL_UNAVAILABLE;goto done;}
        int error=0;socklen_t len=sizeof(error);
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len) || error) {result=VEHICLE_CAL_UNAVAILABLE;goto done;}
    }
    char request[VEHICLE_CAL_COMMAND_SIZE];
    if(strlen(command)+2>sizeof(request)) {result=VEHICLE_CAL_PROTOCOL;goto done;}
    snprintf(request,sizeof(request),"%s\r",command);
    if(write_all(fd,request,strlen(request),deadline,true))goto done;
    shutdown(fd,SHUT_WR);size_t used=0;
    for(;;) {
        if(wait_fd(fd,POLLIN,deadline))goto done;
        ssize_t n=recv(fd,reply+used,size-used-1,0);
        if(n<0 && (errno==EINTR || errno==EAGAIN))continue;
        if(n<0)goto done;
        if(!n)break;
        used+=(size_t)n;if(used+1>=size) {result=VEHICLE_CAL_PROTOCOL;goto done;}
    }
    reply[used]=0;
    {struct vehicle_cal_table table={0};result=used?vehicle_cal_parse(command,reply,&table):VEHICLE_CAL_TRANSPORT;}
done:close(fd);return result;
}
