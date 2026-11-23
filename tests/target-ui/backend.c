/* SPDX-License-Identifier: GPL-2.0-only */
#include "linux_display.h"
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static uint8_t memory[4800*602];
static int bad_format, map_fail, mapped, restored, cache_disabled, ready;
static int blanked, enables, disables, last_flush=1, blank_fail;
static struct input_event events[64];
static unsigned count,index_event;
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if(!strcmp(path,"/dev/fb0"))return 3;
    if(!strcmp(path,"/dev/input/event1"))return 4;
    if(!strcmp(path,"/dev/tty0"))return 5;
    errno=ENOENT;return -1;
}
int __wrap_close(int fd) {if(fd==3)assert(!mapped);return 0;}
int __wrap_ioctl(int fd,unsigned long request,...)
{
    va_list args;va_start(args,request);
    if(request==KDSETMODE) {if(va_arg(args,int)==KD_TEXT)restored++;va_end(args);return 0;}
    if(fd==3 && request==FBIOBLANK) {
        int mode=va_arg(args,int);va_end(args);assert(mapped);
        if(mode==FB_BLANK_POWERDOWN)disables++;
        else {assert(mode==FB_BLANK_UNBLANK && last_flush);enables++;}
        if(blank_fail) {errno=EIO;return -1;}
        blanked=mode==FB_BLANK_POWERDOWN;return 0;
    }
    void *ptr=va_arg(args,void *);va_end(args);
    if(fd==3 && request==FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f=ptr;memset(f,0,sizeof(*f));
        f->line_length=4800;f->smem_len=sizeof(memory);f->visual=FB_VISUAL_TRUECOLOR;return 0;
    }
    if(fd==3 && request==FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v=ptr;memset(v,0,sizeof(*v));
        v->xres=1024;v->yres=600;v->bits_per_pixel=bad_format?16:32;v->xoffset=3;v->yoffset=1;
        v->red.offset=16;v->green.offset=8;v->red.length=v->green.length=v->blue.length=8;return 0;
    }
    if(fd==3 && request==0x4631) {assert(!mapped);cache_disabled=1;return 0;}
    if(fd==4 && request==EVIOCGNAME(128)) {strcpy(ptr,"gt9xxnew_ts");return 0;}
    if(fd==4 && (request==EVIOCGABS(ABS_MT_POSITION_X) || request==EVIOCGABS(ABS_MT_POSITION_Y))) {
        struct input_absinfo *a=ptr;memset(a,0,sizeof(*a));
        a->maximum=request==EVIOCGABS(ABS_MT_POSITION_X)?1023:599;return 0;
    }
    if(fd==5 && request==KDGETMODE) {*(int *)ptr=KD_TEXT;return 0;}
    errno=EINVAL;return -1;
}
void *__wrap_mmap(void *address,size_t length,int prot,int flags,int fd,off_t offset)
{
    (void)address;(void)length;(void)prot;(void)flags;(void)fd;(void)offset;
    assert(cache_disabled);if(map_fail)return MAP_FAILED;mapped=1;return memory;
}
int __wrap_munmap(void *address,size_t length) {(void)length;assert(address==memory);mapped=0;return 0;}
ssize_t __wrap_read(int fd,void *buffer,size_t size)
{
    assert(fd==4 && size==sizeof(struct input_event));
    if(index_event==count) {errno=EAGAIN;return -1;}
    memcpy(buffer,&events[index_event++],size);return size;
}
void lv_disp_flush_ready(lv_disp_drv_t *drv) {(void)drv;ready++;}
bool lv_disp_flush_is_last(lv_disp_drv_t *drv) {(void)drv;return last_flush;}
void lv_disp_draw_buf_init(lv_disp_draw_buf_t *buffer,void *first,void *second,uint32_t pixels)
{
    memset(buffer,0,sizeof(*buffer));buffer->buf1=first;buffer->buf2=second;buffer->size=pixels;
}
static void event(unsigned type,unsigned code,int value)
{
    if(index_event==count)index_event=count=0;
    assert(count<64);events[count++]=(struct input_event){.type=type,.code=code,.value=value};
}
int main(void)
{
    bad_format=1;assert(hz_display_init("/dev/fb0","auto")<0);assert(!mapped);
    bad_format=0;map_fail=1;assert(hz_display_init("/dev/fb0","auto")<0);assert(!mapped);
    assert(!enables && !disables); /* Failed initialization must not blank another owner. */
    map_fail=0;assert(!hz_display_init("/dev/fb0","auto"));assert(mapped);
    assert(!enables && !disables);
    lv_color_t fallback[40];lv_disp_drv_t display={0};lv_disp_draw_buf_t buffer;
    hz_display_configure(&display,&buffer,fallback,40);
    assert(!display.direct_mode && buffer.buf1==fallback && buffer.size==40);
    assert(display.hor_res==1024 && display.ver_res==600 && display.flush_cb==hz_display_flush);
    memset(memory,0xa5,sizeof(memory));lv_color_t pixels[4];memset(pixels,0x22,sizeof(pixels));
    lv_area_t area={.x1=10,.y1=20,.x2=11,.y2=21};
    last_flush=0;hz_display_flush(NULL,&area,pixels);assert(ready==1 && !enables);
    last_flush=1;hz_display_flush(NULL,&area,pixels);assert(ready==2 && enables==1);
    for(unsigned i=0;i<sizeof(memory);i++) {
        int changed=(i>=21*4800+13*4 && i<21*4800+15*4) || (i>=22*4800+13*4 && i<22*4800+15*4);
        assert(memory[i]==(changed?0x22:0xa5));
    }
    lv_indev_data_t data={0};
    event(EV_KEY,BTN_TOUCH,1);event(EV_ABS,ABS_MT_POSITION_X,100);event(EV_ABS,ABS_MT_POSITION_Y,200);
    event(EV_ABS,ABS_MT_TRACKING_ID,0);event(EV_SYN,SYN_MT_REPORT,0);
    event(EV_ABS,ABS_MT_POSITION_X,700);event(EV_ABS,ABS_MT_POSITION_Y,550);
    event(EV_ABS,ABS_MT_TRACKING_ID,1);event(EV_SYN,SYN_MT_REPORT,0);event(EV_SYN,SYN_REPORT,0);
    hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_PR && data.point.x==100 && data.point.y==200);
    event(EV_KEY,BTN_TOUCH,0);event(EV_SYN,SYN_REPORT,0);hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_REL);
    event(EV_KEY,BTN_TOUCH,1);event(EV_ABS,ABS_MT_POSITION_X,300);hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_REL);
    event(EV_SYN,SYN_REPORT,0);hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_PR && data.point.x==300);
    event(EV_SYN,SYN_DROPPED,0);event(EV_KEY,BTN_TOUCH,1);event(EV_SYN,SYN_REPORT,0);
    hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_REL);
    event(EV_KEY,BTN_TOUCH,0);event(EV_SYN,SYN_REPORT,0);event(EV_KEY,BTN_TOUCH,1);event(EV_SYN,SYN_REPORT,0);
    hz_touch_read(NULL,&data);assert(data.state==LV_INDEV_STATE_PR);
    hz_display_close();assert(!mapped && restored==1 && blanked && disables==1);
    hz_display_close();assert(disables==1 && restored==1); /* atexit/failure cleanup is idempotent. */
    assert(!hz_display_init("/dev/fb0","auto"));assert(mapped && blanked && enables==1);
    last_flush=0;hz_display_flush(NULL,&area,pixels);assert(blanked && enables==1);
    last_flush=1;hz_display_flush(NULL,&area,pixels);assert(!blanked && enables==2);
    hz_display_close();assert(!mapped && blanked && disables==2);
    assert(!hz_display_init("/dev/fb0","auto"));
    blank_fail=1;hz_display_close();assert(!mapped && disables==3 && restored==3);
    puts("PASS: native fbdev format/failure cleanup, first-frame unblank, stop/restart layer release, idempotent/error cleanup, cache-before-map, stride/pan offsets and Goodix input");
    return 0;
}
