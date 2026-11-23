/* SPDX-License-Identifier: GPL-2.0-only */
/* Native board fixture: no decoder or DMA buffer is needed for a black plane. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
typedef uint32_t u32;
typedef int32_t s32;
#include HZ_DISPLAY_HEADER
int main(int argc,char **argv)
{
    if(argc!=2 || (strcmp(argv[1],"create") && strcmp(argv[1],"inspect") && strcmp(argv[1],"clear")))return 2;
    int fd=open("/dev/disp",O_RDWR|O_CLOEXEC);if(fd<0){perror("open display");return 1;}
    struct disp_layer_config2 config={.channel=0,.layer_id=0};
    unsigned long args[4]={0,(unsigned long)&config,1,0};
    if(ioctl(fd,DISP_LAYER_GET_CONFIG2,args)<0){perror("get video plane");return 1;}
    if(!strcmp(argv[1],"create")) {
        if(config.enable){fprintf(stderr,"Refusing to replace an enabled video plane\n");return 1;}
        config=(struct disp_layer_config2){.enable=1};
        config.info.mode=LAYER_MODE_COLOR;config.info.zorder=17;
        config.info.alpha_mode=1;config.info.alpha_value=255;config.info.color=0xff000000;
        config.info.screen_win=(struct disp_rect){112,78,800,450};
        if(ioctl(fd,DISP_LAYER_SET_CONFIG2,args)<0){perror("create test plane");return 1;}
        usleep(50000);
        if(ioctl(fd,DISP_LAYER_GET_CONFIG2,args)<0)return 1;
        if(!config.enable)return 1;
    }
    if(!strcmp(argv[1],"clear") && config.enable) {
        if(config.info.mode!=LAYER_MODE_COLOR || config.info.zorder!=17 ||
           config.info.screen_win.x!=112 || config.info.screen_win.y!=78 ||
           config.info.screen_win.width!=800 || config.info.screen_win.height!=450 ||
           config.info.color!=0xff000000) {
            fprintf(stderr,"Refusing to clear a plane outside this test fixture\n");return 1;
        }
        memset(&config,0,sizeof(config));
        if(ioctl(fd,DISP_LAYER_SET_CONFIG2,args)<0)return 1;
        if(ioctl(fd,DISP_LAYER_GET_CONFIG2,args)<0 || config.enable)return 1;
    }
    printf("VIDEO_PLANE_ENABLED=%u\n",config.enable);
    close(fd);return 0;
}
