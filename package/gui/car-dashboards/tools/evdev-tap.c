/* SPDX-License-Identifier: GPL-2.0-only */
/* Inject a development-test tap into the touchscreen already read by HMI. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int emit(int fd,unsigned short type,unsigned short code,int value) {
    struct input_event event={.type=type,.code=code,.value=value};
    ssize_t n;do n=write(fd,&event,sizeof(event));while(n<0&&errno==EINTR);
    return n==sizeof(event)?0:-1;
}
int main(int argc,char **argv) {
    if(argc!=4){fprintf(stderr,"Usage: evdev-tap DEVICE X Y (1024x600)\n");return 2;}
    char *end;long x=strtol(argv[2],&end,10);if(*end||x<0||x>1023)return 2;
    long y=strtol(argv[3],&end,10);if(*end||y<0||y>599)return 2;
    int fd=open(argv[1],O_WRONLY|O_CLOEXEC);if(fd<0){perror("open input");return 1;}
    struct input_absinfo ax,ay,slots;
    if(ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)<0||ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay)<0){perror("touch axes");return 1;}
    int has_slots=ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&slots)==0;
    int rx=ax.minimum+x*(ax.maximum-ax.minimum)/1023,ry=ay.minimum+y*(ay.maximum-ay.minimum)/599;
    int failed=0;
    if(has_slots)failed|=emit(fd,EV_ABS,ABS_MT_SLOT,0);
    failed|=emit(fd,EV_ABS,ABS_MT_TRACKING_ID,1);
    failed|=emit(fd,EV_ABS,ABS_MT_POSITION_X,rx);failed|=emit(fd,EV_ABS,ABS_MT_POSITION_Y,ry);
    failed|=emit(fd,EV_KEY,BTN_TOUCH,1);
    if(!has_slots)failed|=emit(fd,EV_SYN,SYN_MT_REPORT,0);
    failed|=emit(fd,EV_SYN,SYN_REPORT,0);usleep(150000);
    failed|=emit(fd,EV_ABS,ABS_MT_TRACKING_ID,-1);failed|=emit(fd,EV_KEY,BTN_TOUCH,0);
    if(!has_slots)failed|=emit(fd,EV_SYN,SYN_MT_REPORT,0);
    failed|=emit(fd,EV_SYN,SYN_REPORT,0);close(fd);
    if(failed){perror("inject tap");return 1;}
    printf("Tap %ld,%ld injected\n",x,y);return 0;
}
