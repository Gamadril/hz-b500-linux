/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle_calibration.h"
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <linux/tty.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile sig_atomic_t stopping;
static void stop_signal(int sig) {(void)sig;stopping=1;}
struct serial_owner { int fd,net;bool attached,raw;const char *name; };
static int attach(struct serial_owner *owner) {
    int discipline=N_SLCAN;
    if(ioctl(owner->fd,TIOCSETD,&discipline))return -1;
    owner->attached=true;
    char actual[IFNAMSIZ]={0};if(ioctl(owner->fd,SIOCGIFNAME,actual))return -1;
    struct ifreq ifr={0};snprintf(ifr.ifr_name,sizeof(ifr.ifr_name),"%s",actual);
    if(strcmp(actual,owner->name)) {
        snprintf(ifr.ifr_newname,sizeof(ifr.ifr_newname),"%s",owner->name);
        if(ioctl(owner->net,SIOCSIFNAME,&ifr))return -1;
        snprintf(ifr.ifr_name,sizeof(ifr.ifr_name),"%s",owner->name);
    }
    if(ioctl(owner->net,SIOCGIFFLAGS,&ifr))return -1;
    ifr.ifr_flags|=IFF_UP;if(ioctl(owner->net,SIOCSIFFLAGS,&ifr))return -1;
    owner->raw=false;return 0;
}
static int calibration(struct serial_owner *owner,const char *command,char *reply,size_t size) {
    if(!vehicle_cal_command(command))return VEHICLE_CAL_REJECTED;
    if(owner->attached) {
        if(strcmp(command,"GB"))return VEHICLE_CAL_REJECTED;
        int discipline=N_TTY;
        if(ioctl(owner->fd,TIOCSETD,&discipline))return VEHICLE_CAL_TRANSPORT;
        owner->attached=false;
        owner->raw=true;
    }
    char barrier[VEHICLE_CAL_REPLY_SIZE];
    if(strcmp(command,"GB") && strcmp(command,"GP") && strcmp(command,"GL") && strcmp(command,"GT") && strcmp(command,"GX")) {
        /* A tagged read is a barrier against a late bare acknowledgment from
         * an earlier timed-out command. Never retry capture/save blindly. */
        int result=vehicle_cal_serial(owner->fd,"GP",barrier,sizeof(barrier),2000);
        if(result)return result;
    }
    int result=vehicle_cal_serial(owner->fd,command,reply,size,!strcmp(command,"GS")?5000:2000);
    if(!strcmp(command,"GB")) {
        /* Detaching the line discipline can leave the CR of an old CAN frame.
         * Confirm calibration with a tagged GP reply before returning GB's ACK. */
        if(result==VEHICLE_CAL_OK || result==VEHICLE_CAL_REJECTED) {
            int probe=vehicle_cal_serial(owner->fd,"GP",barrier,sizeof(barrier),2000);
            if(probe) {
                if(probe==VEHICLE_CAL_REJECTED && attach(owner))return VEHICLE_CAL_TRANSPORT;
                return probe;
            }
            fprintf(stderr,"vehicle-slcan: calibration active; CAN forwarding paused\n");
        }
    } else if((!strcmp(command,"GS") || !strcmp(command,"GX")) && result==VEHICLE_CAL_OK) {
        if(attach(owner))return VEHICLE_CAL_TRANSPORT;
        fprintf(stderr,"vehicle-slcan: calibration closed; SLCAN reception restored\n");
    }
    return result;
}
static int control_socket(const char *path) {
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if(strlen(path)>=sizeof(address.sun_path)) {errno=ENAMETOOLONG;return -1;}
    snprintf(address.sun_path,sizeof(address.sun_path),"%s",path);
    struct stat st;
    if(!lstat(path,&st)) {
        if(!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) {errno=EEXIST;return -1;}
        int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(probe<0)return -1;
        int connected=connect(probe,(struct sockaddr *)&address,sizeof(address));int error=errno;close(probe);
        if(!connected || error!=ECONNREFUSED) {errno=EADDRINUSE;return -1;}
        if(unlink(path))return -1;
    } else if(errno!=ENOENT)return -1;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    mode_t mask=umask(0077);int result=bind(fd,(struct sockaddr *)&address,sizeof(address));umask(mask);
    if(result || listen(fd,4)) {close(fd);return -1;}
    return fd;
}
static void control_client(int listener,struct serial_owner *owner) {
    int client=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(client<0)return;
    char command[VEHICLE_CAL_COMMAND_SIZE]={0};unsigned used=0;bool complete=false;
    for(unsigned attempt=0;attempt<sizeof(command)+2;attempt++) {
        struct pollfd p={.fd=client,.events=POLLIN};if(poll(&p,1,150)<=0)break;
        char c;ssize_t n=recv(client,&c,1,0);if(n!=1)break;
        if(c=='\n')continue;
        if(c=='\r') {complete=true;break;}
        if(used+1>=sizeof(command))break;
        command[used++]=c;
    }
    char reply[VEHICLE_CAL_REPLY_SIZE]={0};
    int result=complete?calibration(owner,command,reply,sizeof(reply)):VEHICLE_CAL_REJECTED;
    if(result==VEHICLE_CAL_REJECTED) {reply[0]='\a';reply[1]=0;}
    if(result==VEHICLE_CAL_OK || result==VEHICLE_CAL_REJECTED) {
        size_t sent=0,size=strlen(reply);
        while(sent<size) {
            struct pollfd p={.fd=client,.events=POLLOUT};if(poll(&p,1,200)<=0)break;
            ssize_t n=send(client,reply+sent,size-sent,MSG_NOSIGNAL);if(n<=0)break;
            sent+=(size_t)n;
        }
    }
    close(client);
}
int main(int argc,char **argv) {
    const char *device="/dev/ttyS3",*name="can0",*control=VEHICLE_CAL_SOCKET;unsigned baud=750000;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--device")&&i+1<argc) device=argv[++i];
        else if(!strcmp(argv[i],"--interface")&&i+1<argc) name=argv[++i];
        else if(!strcmp(argv[i],"--calibration-socket")&&i+1<argc) control=argv[++i];
        else if(!strcmp(argv[i],"--baud")&&i+1<argc) {
            char *end;unsigned long n=strtoul(argv[++i],&end,10);
            if(*end||n<9600||n>1500000) return 2;
            baud=n;
        } else {fprintf(stderr,"Usage: vehicle-slcan [--device /dev/ttyS3] [--interface can0] [--baud 750000] [--calibration-socket PATH]\n");return 2;}
    }
    if(strlen(name)>=IFNAMSIZ) return 2;
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);
    int fd=open(device,O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) {perror("vehicle-slcan: open");return 1;}
    struct termios2 old,t;
    int original=N_TTY,result=1,listener=-1;
    int configured=0,exclusive=0,already_exclusive=0;
    struct serial_owner owner={.fd=fd,.net=-1,.name=name};
    if(flock(fd,LOCK_EX|LOCK_NB)) goto fail;
    if(ioctl(fd,TIOCGEXCL,&already_exclusive)) goto fail;
    if(already_exclusive) {errno=EBUSY;goto fail;}
    if(ioctl(fd,TIOCEXCL)) goto fail;
    exclusive=1;
    if(ioctl(fd,TCGETS2,&old)||ioctl(fd,TIOCGETD,&original)) goto fail;
    if(original!=N_TTY) {errno=EBUSY;goto fail;}
    t=old;t.c_iflag=0;t.c_oflag=0;t.c_lflag=0;
    t.c_cflag=BOTHER|CS8|CLOCAL|CREAD;t.c_ispeed=baud;t.c_ospeed=baud;
    t.c_cc[VMIN]=1;t.c_cc[VTIME]=0;
    if(ioctl(fd,TCSETS2,&t)) goto fail;
    configured=1;
    owner.net=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(owner.net<0 || attach(&owner))goto fail;
    listener=control_socket(control);if(listener<0)goto fail;
    fprintf(stderr,"vehicle-slcan: %s %u baud -> %s (bridge auto-open; no CAN commands sent)\n",device,baud,name);
    while(!stopping) {
        struct pollfd p[]={{.fd=fd,.events=0},{.fd=listener,.events=POLLIN}};int r=poll(p,2,500);
        if(r<0&&errno!=EINTR) goto fail;
        if(r>0&&(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))) {errno=ENODEV;goto fail;}
        if(r>0 && (p[1].revents&POLLIN))control_client(listener,&owner);
    }
    result=0;goto cleanup;
fail:perror("vehicle-slcan");
cleanup:
    if(listener>=0) {close(listener);unlink(control);}
    if(!owner.attached && owner.raw && configured) {
        char reply[VEHICLE_CAL_REPLY_SIZE];vehicle_cal_serial(fd,"GX",reply,sizeof(reply),1000);
    }
    if(owner.net>=0)close(owner.net);
    if(owner.attached) ioctl(fd,TIOCSETD,&original);
    if(configured) ioctl(fd,TCSETS2,&old);
    if(exclusive) ioctl(fd,TIOCNXCL);
    close(fd);return result;
}
