/* SPDX-License-Identifier: GPL-2.0-only */
/* Host helper for Bash: configure a custom baud and copy stdin to the UART. */
#define _GNU_SOURCE
#include <asm/termbits.h>
#include <asm/ioctls.h>
#include <linux/tty.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

static volatile sig_atomic_t stopping;
static void stop(int signal) {(void)signal;stopping=1;}
int main(int argc,char **argv) {
    if(argc!=3) {fprintf(stderr,"Usage: serial-send DEVICE BAUD\n");return 2;}
    char *end;unsigned long baud=strtoul(argv[2],&end,10);
    if(*end || baud<9600 || baud>1500000) return 2;
    struct sigaction action={.sa_handler=stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);
    int fd=open(argv[1],O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) {perror("serial-send: open");return 1;}
    struct termios2 old,t;
    int discipline,exclusive=0,configured=0,result=1,busy=0;
    if(flock(fd,LOCK_EX|LOCK_NB) || ioctl(fd,TIOCGEXCL,&busy)) goto fail;
    if(busy) {errno=EBUSY;goto fail;}
    if(ioctl(fd,TIOCEXCL)) goto fail;
    exclusive=1;
    if(ioctl(fd,TCGETS2,&old) || ioctl(fd,TIOCGETD,&discipline)) goto fail;
    if(discipline!=N_TTY) {errno=EBUSY;goto fail;}
    memset(&t,0,sizeof(t));
    t.c_cflag=BOTHER|CS8|CLOCAL|CREAD;t.c_ispeed=baud;t.c_ospeed=baud;
    t.c_cc[VMIN]=1;
    if(ioctl(fd,TCSETS2,&t)) goto fail;
    configured=1;
    if(ioctl(fd,TCGETS2,&t)) goto fail;
    if(t.c_ispeed!=baud || t.c_ospeed!=baud) {errno=EINVAL;goto fail;}
    fprintf(stderr,"serial-send: opened %s, %lu baud 8N1, no flow control\n",argv[1],baud);
    while(!stopping) {
        char data[512];ssize_t n=read(STDIN_FILENO,data,sizeof(data));
        if(n<0 && errno==EINTR) continue;
        if(n<0) goto fail;
        if(!n) break;
        for(ssize_t offset=0;offset<n && !stopping;) {
            struct pollfd p={.fd=fd,.events=POLLOUT};
            int ready=poll(&p,1,2000);
            if(ready<0 && errno==EINTR) continue;
            if(ready<0) goto fail;
            if(!ready) {errno=ETIMEDOUT;goto fail;}
            if(p.revents&(POLLERR|POLLHUP|POLLNVAL)) {errno=ENODEV;goto fail;}
            ssize_t written=write(fd,data+offset,n-offset);
            if(written<0 && (errno==EINTR || errno==EAGAIN)) continue;
            if(written<=0) goto fail;
            offset+=written;
        }
    }
    /* Do not restore the old baud before the final queued frame is sent. */
    if(!stopping && ioctl(fd,TCSBRK,1)) goto fail;
    /* A USB driver may report an empty URB queue while the adapter is still
     * shifting its final packet. Keep the baud stable for a bounded packet
     * guard after drain; otherwise closing at a different old baud can
     * truncate the last frame. */
    if(!stopping) {
        uint64_t guard_ns=512ULL*10*1000000000/baud+20000000;
        struct timespec guard={guard_ns/1000000000,guard_ns%1000000000};
        while(nanosleep(&guard,&guard)<0 && errno==EINTR && !stopping) {}
    }
    result=stopping?130:0;
    goto cleanup;
fail:
    perror("serial-send");
cleanup:
    if(configured && ioctl(fd,TCSETS2,&old)) {perror("serial-send: restore termios");result=1;}
    if(exclusive) ioctl(fd,TIOCNXCL);
    close(fd);
    return result;
}
