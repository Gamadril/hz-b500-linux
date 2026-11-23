/* SPDX-License-Identifier: GPL-2.0-only */
#include "video_plane.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
typedef uint32_t u32;
typedef int32_t s32;
#include HZ_DISPLAY_HEADER
static struct disp_layer_config2 current;
static unsigned sets,closes;
static unsigned brightness;
static bool fail_open,fail_get,fail_set,ignore_set;
static char picture_path[64],picture_text[8];
int __wrap_open(const char *path,int flags,...)
{
    if(!strncmp(path,"/sys/",5)) { /* hz_lcd_picture */
        assert(flags==(O_WRONLY|O_CLOEXEC));snprintf(picture_path,sizeof(picture_path),"%s",path);
        if(fail_open){errno=ENOENT;return -1;}return 42;
    }
    assert(!strcmp(path,"/dev/disp") && flags==(O_RDWR|O_CLOEXEC));
    if(fail_open){errno=ENODEV;return -1;}return 42;
}
int __wrap_close(int fd){assert(fd==42);closes++;return 0;}
ssize_t __wrap_write(int fd,const void *data,size_t size)
{
    assert(fd==42 && size<sizeof(picture_text));memcpy(picture_text,data,size);picture_text[size]=0;return size;
}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    assert(fd==42);va_list ap;va_start(ap,request);unsigned long *args=va_arg(ap,unsigned long *);va_end(ap);
    if(request==DISP_LCD_SET_BRIGHTNESS) {
        assert(!args[0] && args[1]<=255 && !args[2] && !args[3]);
        brightness=args[1];return fail_set?-1:0;
    }
    assert(!args[0] && args[2]==1 && !args[3]);
    struct disp_layer_config2 *config=(void *)args[1];
    /* No access to the framebuffer's channel 1 or any other video layer. */
    assert(!config->channel && !config->layer_id);
    if(request==DISP_LAYER_GET_CONFIG2){
        if(fail_get){errno=EIO;return -1;}*config=current;return 0;
    }
    assert(request==DISP_LAYER_SET_CONFIG2);sets++;
    if(fail_set){errno=EIO;return -1;}
    struct disp_layer_config2 disabled={0};assert(!memcmp(config,&disabled,sizeof(disabled)));
    if(!ignore_set)current=*config;
    return 0;
}
static void stale(void)
{
    current=(struct disp_layer_config2){.enable=1};
    current.info.zorder=17;
    current.info.screen_win=(struct disp_rect){112,78,800,450};
    current.info.fb.fd=123;
}
int main(void)
{
    assert(!hz_video_plane_recover() && !sets && closes==1);
    stale();assert(!hz_video_plane_recover() && !current.enable && sets==1);
    stale();current.info.screen_win.width=1024;
    struct disp_layer_config2 before=current;
    assert(!hz_video_plane_recover() && !memcmp(&current,&before,sizeof(current)) && sets==1);
    stale();current.info.zorder=16;before=current;
    assert(!hz_video_plane_recover() && !memcmp(&current,&before,sizeof(current)) && sets==1);
    fail_get=true;assert(hz_video_plane_recover()<0 && sets==1);fail_get=false;
    stale();fail_set=true;assert(hz_video_plane_recover()<0 && current.enable);fail_set=false;
    ignore_set=true;assert(hz_video_plane_recover()<0 && current.enable);ignore_set=false;
    unsigned closed=closes;fail_open=true;assert(hz_video_plane_recover()<0 && closes==closed);
    assert(hz_lcd_brightness(128)<0 && closes==closed);fail_open=false;
    assert(hz_lcd_brightness(256)<0 && closes==closed);
    assert(!hz_lcd_brightness(128) && brightness==128 && closes==closed+1);
    assert(!hz_lcd_brightness(0) && brightness==0);
    assert(!hz_lcd_brightness(255) && brightness==255);
    fail_set=true;assert(hz_lcd_brightness(64)<0);
    /* Linked with preview_guard.c like the HMI: fopen() on /sys/ would fail here. */
    assert(!hz_lcd_picture(2,55) && !strcmp(picture_path,"/sys/class/disp/disp/attr/enhance_hue") && !strcmp(picture_text,"55\n"));
    assert(!hz_lcd_picture(0,40) && !strcmp(picture_path,"/sys/class/disp/disp/attr/enhance_bright"));
    assert(hz_lcd_picture(4,50)<0 && hz_lcd_picture(0,101)<0);
    fail_open=1;assert(hz_lcd_picture(3,50)<0);fail_open=0;
    puts("PASS: picture settings reach the display-engine sysfs files through open(), past the fopen guard");
    puts("PASS: stale HMI video plane is cleared and verified; unrelated planes and framebuffer are preserved; driver failures are reported");
}
