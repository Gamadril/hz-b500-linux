/* SPDX-License-Identifier: GPL-2.0-only */
#include "linux_display.h"
#ifdef HZ_USE_G2D
#include "g2d_display.h"
#include "lvgl/src/draw/sw/lv_draw_sw.h"
#endif
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

static int fbfd=-1, touchfd=-1, console=-1, console_mode=-1;
static int framebuffer_owned, framebuffer_visible;
static uint8_t *mapping;
static struct fb_var_screeninfo screen;
static struct fb_fix_screeninfo fixed;
static struct input_absinfo xrange, yrange;
static int touch_xcode, touch_ycode, slot, has_slots, contact, x, y, pressed, dropped;
static int x_pending, y_pending, pressed_pending;

static int open_touch(const char *path)
{
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) return -1;
    char name[128]={0};
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0 ||
       (!strstr(name,"goodix")&&!strstr(name,"Goodix")&&!strstr(name,"gt9"))) {
        close(fd);return -1;
    }
    touch_xcode=ABS_MT_POSITION_X; touch_ycode=ABS_MT_POSITION_Y;
    if(ioctl(fd,EVIOCGABS(touch_xcode),&xrange)<0 || ioctl(fd,EVIOCGABS(touch_ycode),&yrange)<0) {
        touch_xcode=ABS_X;touch_ycode=ABS_Y;
        if(ioctl(fd,EVIOCGABS(touch_xcode),&xrange)<0 || ioctl(fd,EVIOCGABS(touch_ycode),&yrange)<0) {
            close(fd);return -1;
        }
    }
    if(xrange.maximum<=xrange.minimum || yrange.maximum<=yrange.minimum) {close(fd);return -1;}
    struct input_absinfo slots;
    has_slots=ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&slots)==0;
    fprintf(stderr,"UI touch: %s (%s)\n",path,name);
    return fd;
}

void hz_display_close(void)
{
    /* sunxi_fb_release() is a no-op. POWERDOWN disables only the framebuffer
     * layer on this BSP, leaving the LCD and other clients' video planes on. */
    if(framebuffer_owned && fbfd>=0 && ioctl(fbfd,FBIOBLANK,FB_BLANK_POWERDOWN)<0)
        perror("UI framebuffer release");
    framebuffer_owned=framebuffer_visible=0;
#ifdef HZ_USE_G2D
    hz_g2d_close();
#endif
    if(touchfd>=0) close(touchfd);
    if(mapping) munmap(mapping,fixed.smem_len);
    if(fbfd>=0) close(fbfd);
    if(console>=0) {
        if(console_mode>=0) ioctl(console,KDSETMODE,console_mode);
        close(console);
    }
    touchfd=fbfd=console=-1;mapping=NULL;console_mode=-1;
}

int hz_display_init(const char *fb, const char *touch)
{
    fbfd=open(fb,O_RDWR|O_CLOEXEC);
    if(fbfd<0) {perror(fb);return -1;}
    if(ioctl(fbfd,FBIOGET_FSCREENINFO,&fixed)<0 || ioctl(fbfd,FBIOGET_VSCREENINFO,&screen)<0) goto fail;
    /* Use native unrotated XRGB/ARGB8888 only. Honour stride and current pan
     * offsets; never write into reserved/padded memory outside the mapping. */
    uint64_t end=((uint64_t)screen.yoffset+screen.yres-1)*fixed.line_length+
                 ((uint64_t)screen.xoffset+screen.xres)*4;
    if(screen.xres!=1024 || screen.yres!=600 || screen.bits_per_pixel!=32 ||
       screen.red.offset!=16 || screen.green.offset!=8 || screen.blue.offset!=0 ||
       screen.red.length!=8 || screen.green.length!=8 || screen.blue.length!=8 ||
       screen.red.msb_right || screen.green.msb_right || screen.blue.msb_right ||
       (uint64_t)(screen.xoffset+screen.xres)*4>fixed.line_length || end>fixed.smem_len ||
       fixed.type!=FB_TYPE_PACKED_PIXELS || fixed.visual!=FB_VISUAL_TRUECOLOR) {
        fprintf(stderr,"UI needs a mapped 1024x600 XRGB8888 framebuffer\n");goto fail;
    }
    /* Keep framebuffer CPU fallback uncached. The G2D path renders into a
     * separate cached ION surface and imports scanout through a dma-buf fd. */
    unsigned long cache_args[2]={0,0};
    if(ioctl(fbfd,0x4631,cache_args)<0 && errno!=ENOTTY && errno!=EINVAL) goto fail;
    mapping=mmap(NULL,fixed.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);
    if(mapping==MAP_FAILED) {mapping=NULL;goto fail;}
    if(touch && strcmp(touch,"auto")) touchfd=open_touch(touch);
    else for(unsigned i=0;i<32 && touchfd<0;i++) {
        char path[64];snprintf(path,sizeof(path),"/dev/input/event%u",i);
        touchfd=open_touch(path);
    }
    if(touchfd<0) {fprintf(stderr,"Goodix touch not available\n");goto fail;}
    console=open("/dev/tty0",O_RDWR|O_NOCTTY|O_CLOEXEC);
    if(console>=0 && ioctl(console,KDGETMODE,&console_mode)==0)
        if(ioctl(console,KDSETMODE,KD_GRAPHICS)<0) {perror("KD_GRAPHICS");goto fail;}
#ifdef HZ_USE_G2D
    hz_g2d_open(fbfd,fixed.line_length/4,fixed.smem_len/fixed.line_length,screen.xoffset,screen.yoffset);
#endif
    framebuffer_owned=1;
    fprintf(stderr,"UI display: %s 1024x600\n",fb);
    return 0;
fail:
    hz_display_close();return -1;
}

void hz_display_configure(lv_disp_drv_t *display,lv_disp_draw_buf_t *buffer,lv_color_t *fallback,unsigned fallback_pixels)
{
    display->direct_mode=0;
#ifdef HZ_USE_G2D
    lv_color_t *surface=hz_g2d_buffer();
    if(surface) {
        lv_disp_draw_buf_init(buffer,surface,NULL,1024*600);
        display->direct_mode=1;
        display->draw_ctx_init=hz_g2d_draw_ctx_init;
        display->draw_ctx_deinit=lv_draw_sw_deinit_ctx;
        display->draw_ctx_size=sizeof(lv_draw_sw_ctx_t);
    } else
#endif
    lv_disp_draw_buf_init(buffer,fallback,NULL,fallback_pixels);
    display->draw_buf=buffer;display->flush_cb=hz_display_flush;
    display->hor_res=1024;display->ver_res=600;
}

static void show_framebuffer(void)
{
    /* Reopening fbdev does not re-enable a blanked layer on this BSP. Wait
     * for a complete new frame so restart never exposes the previous HMI. */
    if(framebuffer_owned && !framebuffer_visible) {
        if(ioctl(fbfd,FBIOBLANK,FB_BLANK_UNBLANK)<0)perror("UI framebuffer enable");
        else framebuffer_visible=1;
    }
}

void hz_display_flush(lv_disp_drv_t *display,const lv_area_t *area,lv_color_t *pixels)
{
#ifdef HZ_USE_G2D
    if(hz_g2d_buffer() && pixels==hz_g2d_buffer()) {
        /* Direct mode may call flush for several dirty areas. Publish only the
         * completed frame, so the logo isn't replaced by slow drawing strips. */
        if(!lv_disp_flush_is_last(display)) {
            lv_disp_flush_ready(display);return;
        }
        if(hz_g2d_present()) {
            show_framebuffer();
            lv_disp_flush_ready(display);return;
        }
        /* Keep the cached full-size buffer after an engine error; the same
         * complete frame can still be presented by the CPU below. */
    }
#endif
    if(mapping && area->x1>=0 && area->y1>=0 && area->x2<1024 && area->y2<600 &&
       area->x2>=area->x1 && area->y2>=area->y1) {
        size_t bytes=(area->x2-area->x1+1)*sizeof(*pixels);
        for(int row=area->y1;row<=area->y2;row++) {
            uint8_t *dst=mapping+(row+screen.yoffset)*fixed.line_length+(area->x1+screen.xoffset)*4;
            memcpy(dst,pixels,bytes);pixels+=area->x2-area->x1+1;
        }
        if(lv_disp_flush_is_last(display))show_framebuffer();
    }
    lv_disp_flush_ready(display);
}

static int coordinate(int value,const struct input_absinfo *range,int extent)
{
    if(value<range->minimum)value=range->minimum;
    if(value>range->maximum)value=range->maximum;
    return (int)((int64_t)(value-range->minimum)*(extent-1)/(range->maximum-range->minimum));
}

/* After a full-screen child app (fbDOOM, Android Auto): drop taps meant for it
 * and, if a finger is still down (the corner exit), wait for it to lift. */
void hz_touch_discard(void)
{
    struct input_event event;unsigned long keys[KEY_CNT/(8*sizeof(long))+1]={0};
    if(touchfd<0)return;
    while(read(touchfd,&event,sizeof(event))==(ssize_t)sizeof(event)) {}
    pressed=pressed_pending=0;contact=0;
    dropped=ioctl(touchfd,EVIOCGKEY(sizeof(keys)),keys)>=0 &&
        (keys[BTN_TOUCH/(8*sizeof(long))]>>(BTN_TOUCH%(8*sizeof(long)))&1);
}
void hz_touch_read(lv_indev_drv_t *driver,lv_indev_data_t *data)
{
    (void)driver;struct input_event event;
    /* Keep the UI responsive even if the device floods input. Commit complete
     * SYN_REPORT batches only. Following SYN_DROPPED wait for every finger to
     * lift before accepting a fresh contact, preventing a spurious tap. */
    for(unsigned i=0;i<256;i++) {
        ssize_t count=read(touchfd,&event,sizeof(event));
        if(count!=(ssize_t)sizeof(event)) {
            if(count<0 && errno==EINTR)continue;
            if(count==0 || (count<0 && errno!=EAGAIN)) {pressed=pressed_pending=0;}
            break;
        }
        if(event.type==EV_SYN && event.code==SYN_DROPPED) {dropped=1;pressed=pressed_pending=0;continue;}
        if(dropped) {
            if(event.type==EV_KEY && event.code==BTN_TOUCH && !event.value)dropped=0;
            continue;
        }
        if(event.type==EV_ABS) {
            if(event.code==ABS_MT_SLOT)slot=event.value;
            else if((has_slots && slot==0) || (!has_slots && contact==0)) {
                if(event.code==touch_xcode)x_pending=coordinate(event.value,&xrange,1024);
                if(event.code==touch_ycode)y_pending=coordinate(event.value,&yrange,600);
                if(has_slots && event.code==ABS_MT_TRACKING_ID)pressed_pending=event.value>=0;
            }
        } else if(event.type==EV_KEY && event.code==BTN_TOUCH)pressed_pending=!!event.value;
        else if(event.type==EV_SYN && event.code==SYN_MT_REPORT)contact++;
        else if(event.type==EV_SYN && event.code==SYN_REPORT) {x=x_pending;y=y_pending;pressed=pressed_pending;contact=0;}
    }
    data->point.x=x;data->point.y=y;data->state=pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
}
