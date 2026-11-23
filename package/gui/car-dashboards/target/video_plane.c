/* SPDX-License-Identifier: GPL-2.0-only */
#include "video_plane.h"
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
typedef uint32_t u32;
typedef int32_t s32;
#ifndef HZ_DISPLAY_HEADER
#define HZ_DISPLAY_HEADER "sunxi_display2.h"
#endif
#include HZ_DISPLAY_HEADER

int hz_lcd_brightness(unsigned value)
{
    if(value>255)return -1;
    int fd=open("/dev/disp",O_RDWR|O_CLOEXEC);
    if(fd<0) {perror("UI brightness open");return -1;}
    unsigned long args[4]={0,value,0,0};
    int result=ioctl(fd,DISP_LCD_SET_BRIGHTNESS,args);
    if(result<0)perror("UI brightness");
    else fprintf(stderr,"UI brightness: %u/255\n",value);
    close(fd);return result;
}

/* sunxi disp sysfs; enhance_hue is added by our kernel. open(), not fopen():
 * preview_guard.c blocks fopen() on /sys/ in this binary. */
int hz_lcd_picture(unsigned index,unsigned value)
{
    static const char *names[]={"bright","contrast","hue","saturation"};
    if(index>3 || value>100)return -1;
    char path[64];snprintf(path,sizeof(path),"/sys/class/disp/disp/attr/enhance_%s",names[index]);
    int fd=open(path,O_WRONLY|O_CLOEXEC);
    if(fd<0) {perror(path);return -1;}
    char text[8];int length=snprintf(text,sizeof(text),"%u\n",value);
    int result=write(fd,text,length)==length?0:-1;
    if(result)perror(path);
    close(fd);return result;
}

int hz_video_plane_recover(void)
{
    int fd=open("/dev/disp",O_RDWR|O_CLOEXEC);
    if(fd<0){perror("UI video plane open");return -1;}
    struct disp_layer_config2 config={.channel=0,.layer_id=0};
    unsigned long args[4]={0,(unsigned long)&config,1,0};
    int result=-1;
    if(ioctl(fd,DISP_LAYER_GET_CONFIG2,args)<0){perror("UI video plane query");goto done;}
    result=0;
    /* A killed player cannot run its layer destructor. Match the HMI video
     * footprint exactly; the framebuffer and other display layers are retained. */
    if(!config.enable || config.info.zorder!=17 ||
       config.info.screen_win.x!=112 || config.info.screen_win.y!=78 ||
       config.info.screen_win.width!=800 || config.info.screen_win.height!=450)goto done;
    memset(&config,0,sizeof(config)); /* channel 0, layer 0, disabled */
    result=-1;
    if(ioctl(fd,DISP_LAYER_SET_CONFIG2,args)<0){perror("UI video plane disable");goto done;}
    if(ioctl(fd,DISP_LAYER_GET_CONFIG2,args)<0){perror("UI video plane verify");goto done;}
    if(config.enable){fprintf(stderr,"UI video plane remains enabled\n");goto done;}
    fprintf(stderr,"UI cleared stale video plane\n");
    result=0;
done:
    close(fd);return result;
}
