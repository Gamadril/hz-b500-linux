/* SPDX-License-Identifier: GPL-2.0-only */
/* Run actual LVGL and the production backend; emulate only device/DMA calls. */
#include "linux_display.h"
#include "g2d_display.h"
#include "transition.h"
#include "g2d_driver_enh.h"
#include "lvgl/src/draw/sw/lv_draw_sw.h"
#include <ion_mem_alloc.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {NONE,NO_ENGINE,NO_EXPORT,NO_MEMORY,NO_FRAME,NO_DMA,NO_FD,NO_UPLOAD};
static int failure,allocated,opened,closed,engine_live,export_live,mapped;
static unsigned fills,copies,presents,cache_calls,blends,scales;
static unsigned fail_request;
static unsigned fail_after;
static bool partial_failure;
static bool framebuffer_enabled;
static unsigned framebuffer_enables;
static unsigned char scanout[4800*601];
static lv_color_t expected[1024*600];
struct allocation {void *pointer;unsigned size;int fd;};
static struct allocation buffers[6];
static struct SunxiMemOpsS memory;
static struct allocation *find_pointer(void *pointer) {
    for(unsigned i=0;i<6;i++)if(buffers[i].pointer==pointer)return &buffers[i];
    abort();
}
static int memory_open(void) {if(failure==NO_MEMORY)return -1;opened++;return 0;}
static void memory_close(void) {assert(!allocated);closed++;}
static void *allocate(int size) {
    if(failure==NO_FRAME || (failure==NO_UPLOAD && allocated))return NULL;
    for(unsigned i=0;i<6;i++)if(!buffers[i].pointer) {
        buffers[i]=(struct allocation){malloc(size),size,8+(int)i};assert(buffers[i].pointer);allocated++;
        return buffers[i].pointer;
    }
    abort();
}
static void release(void *pointer) {struct allocation *b=find_pointer(pointer);free(pointer);memset(b,0,sizeof(*b));allocated--;}
static void *dma_address(void *pointer) {(void)find_pointer(pointer);return failure==NO_DMA?NULL:(void *)0x10000000;}
static int buffer_fd(void *pointer) {return failure==NO_FD?-1:find_pointer(pointer)->fd;}
static void sync_cache(void *pointer,int size) {
    for(unsigned i=0;i<6;i++) {
        uintptr_t start=(uintptr_t)buffers[i].pointer,end=start+buffers[i].size;
        if(start && (uintptr_t)pointer>=start && (uintptr_t)pointer+(unsigned)size<=end) {cache_calls++;return;}
    }
    abort();
}
struct SunxiMemOpsS *GetMemAdapterOpsS(void) {return &memory;}
int __wrap_open(const char *path,int flags,...) {
    (void)flags;
    if(!strcmp(path,"/dev/fb0"))return 3;
    if(!strcmp(path,"/dev/input/event1"))return 4;
    if(!strcmp(path,"/dev/tty0"))return 5;
    if(!strcmp(path,"/dev/g2d")) {
        if(failure==NO_ENGINE) {errno=ENOENT;return -1;}
        assert(!engine_live);engine_live=1;return 6;
    }
    errno=ENOENT;return -1;
}
int __wrap_close(int fd) {
    if(fd==6) {assert(engine_live);engine_live=0;}
    if(fd==7) {assert(export_live);export_live=0;}
    return 0;
}
void *__wrap_mmap(void *address,size_t size,int prot,int flags,int fd,off_t offset) {
    (void)address;(void)prot;(void)flags;assert(size==sizeof(scanout) && fd==3 && !offset);mapped++;return scanout;
}
int __wrap_munmap(void *address,size_t size) {assert(address==scanout && size==sizeof(scanout));mapped--;return 0;}
ssize_t __wrap_read(int fd,void *buffer,size_t size) {(void)buffer;(void)size;assert(fd==4);errno=EAGAIN;return -1;}
static lv_color_t *image_pixels(const g2d_image_enh *image) {
    assert(image->format==G2D_FORMAT_ARGB8888 && !image->use_phy_addr);
    assert(!image->laddr[0] && !image->haddr[0]);
    assert(image->clip_rect.x>=0 && image->clip_rect.y>=0);
    assert((unsigned)image->clip_rect.x+image->clip_rect.w<=image->width);
    assert((unsigned)image->clip_rect.y+image->clip_rect.h<=image->height);
    if(image->fd==7) {
        assert(image->width==1200 && image->height==601);return (lv_color_t *)scanout;
    }
    for(unsigned i=0;i<6;i++)if(buffers[i].pointer && buffers[i].fd==image->fd) {
        assert(image->width*image->height*sizeof(lv_color_t)<=buffers[i].size);return buffers[i].pointer;
    }
    abort();
}
static void copy_image(g2d_blt_h *operation,bool partial) {
    g2d_image_enh *src=&operation->src_image_h,*dst=&operation->dst_image_h;
    lv_color_t *source=image_pixels(src),*target=image_pixels(dst);
    /* F133's RCQ ioctl dispatches zero flags to the alpha mixer. Its explicit
     * ROT_0 command selects the plain copy engine, as Tina's fbdev backend does. */
    if(operation->flag_h==G2D_BLT_NONE_H) {
        assert(src->mode==G2D_PIXEL_ALPHA && dst->fd!=7 && dst->fd!=8);
        for(unsigned y=0;y<dst->height;y++)for(unsigned x=0;x<dst->width;x++) {
            lv_color_t color=source[(y*src->height/dst->height)*src->width+x*src->width/dst->width];
            assert(color.ch.alpha==255);target[y*dst->width+x]=color;
        }
        scales++;return;
    }
    assert(operation->flag_h==G2D_ROT_0 && src->clip_rect.w==dst->clip_rect.w && src->clip_rect.h==dst->clip_rect.h);
    unsigned height=partial?src->clip_rect.h/2:src->clip_rect.h;
    for(unsigned y=0;y<height;y++)memcpy(target+(y+dst->clip_rect.y)*dst->width+dst->clip_rect.x,
        source+(y+src->clip_rect.y)*src->width+src->clip_rect.x,src->clip_rect.w*sizeof(*source));
    if(dst->fd==7) {assert(dst->clip_rect.x==3 && dst->clip_rect.y==1);presents++;}else copies++;
}
int __wrap_ioctl(int fd,unsigned long request,...) {
    va_list args;va_start(args,request);
    if(request==KDSETMODE) {assert(fd==5);(void)va_arg(args,int);va_end(args);return 0;}
    if(fd==3 && request==FBIOBLANK) {
        int mode=va_arg(args,int);va_end(args);assert(mapped);
        if(mode==FB_BLANK_UNBLANK) {
            assert(presents>0);framebuffer_enabled=true;framebuffer_enables++;
        } else {assert(mode==FB_BLANK_POWERDOWN);framebuffer_enabled=false;}
        return 0;
    }
    if(fd==3 && request==0x4632) {
        (void)va_arg(args,int);va_end(args);
        if(failure==NO_EXPORT) {errno=ENOTTY;return -1;}
        assert(!export_live);export_live=1;return 7;
    }
    void *arg=va_arg(args,void *);va_end(args);
    if(fd==3 && request==FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f=arg;memset(f,0,sizeof(*f));f->line_length=4800;f->smem_len=sizeof(scanout);f->visual=FB_VISUAL_TRUECOLOR;return 0;
    }
    if(fd==3 && request==FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v=arg;memset(v,0,sizeof(*v));v->xres=1024;v->yres=600;
        v->bits_per_pixel=32;v->xoffset=3;v->yoffset=1;v->red.offset=16;v->green.offset=8;
        v->red.length=v->green.length=v->blue.length=8;return 0;
    }
    if(fd==3 && request==0x4631)return 0;
    if(fd==4 && request==EVIOCGNAME(128)) {strcpy(arg,"gt9xxnew_ts");return 0;}
    if(fd==4 && (request==EVIOCGABS(ABS_MT_POSITION_X) || request==EVIOCGABS(ABS_MT_POSITION_Y))) {
        struct input_absinfo *range=arg;memset(range,0,sizeof(*range));range->maximum=1023;return 0;
    }
    if(fd==5 && request==KDGETMODE) {*(int *)arg=KD_TEXT;return 0;}
    if(fd==6 && request==G2D_CMD_FILLRECT_H) {
        g2d_fillrect_h *operation=arg;g2d_image_enh *dst=&operation->dst_image_h;
        lv_color_t *target=image_pixels(dst);assert(dst->fd!=7 && cache_calls);
        if(fail_request==request && !partial_failure)goto failed;
        unsigned height=fail_request==request?dst->clip_rect.h/2:dst->clip_rect.h;
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<dst->clip_rect.w;x++)
            target[(y+dst->clip_rect.y)*dst->width+x+dst->clip_rect.x].full=dst->color;
        fills++;
        if(fail_request==request)goto failed;
        return 0;
    }
    if(fd==6 && request==G2D_CMD_BITBLT_H) {
        assert(cache_calls);
        if(fail_request==request && !partial_failure)goto failed;
        copy_image(arg,fail_request==request);
        if(fail_request==request)goto failed;
        return 0;
    }
    if(fd==6 && request==G2D_CMD_BLD_H) {
        g2d_bld *operation=arg;g2d_image_enh *bottom=&operation->src_image[0],*top=&operation->src_image[1],*dst=&operation->dst_image;
        assert(operation->bld_cmd==G2D_BLD_SRCOVER && bottom->fd==dst->fd && dst->fd!=7);
        assert(bottom->mode==G2D_GLOBAL_ALPHA && top->mode==G2D_PIXEL_ALPHA);
        lv_color_t *base=image_pixels(bottom),*source=image_pixels(top),*target=image_pixels(dst);
        bool fail=fail_request==request && fail_after--==0;
        if(fail && !partial_failure)goto failed;
        unsigned height=fail?dst->clip_rect.h/2:dst->clip_rect.h;
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<dst->clip_rect.w;x++) {
            lv_color_t fg=source[(y+top->clip_rect.y)*top->width+x+top->clip_rect.x];
            unsigned index=(y+bottom->clip_rect.y)*bottom->width+x+bottom->clip_rect.x;
            target[index]=fg.ch.alpha==255?fg:lv_color_mix(fg,base[index],fg.ch.alpha);
        }
        blends++;if(fail)goto failed;return 0;
    }
    errno=EINVAL;return -1;
failed:
    fail_request=0;errno=EIO;return -1;
}
static void check_scanout(void) {
    lv_color_t *frame=hz_g2d_buffer();assert(frame);
    for(unsigned y=0;y<601;y++)for(unsigned x=0;x<1200;x++) {
        lv_color_t pixel=((lv_color_t *)scanout)[y*1200+x];
        if(y>=1 && x>=3 && x<1027)assert(pixel.full==frame[(y-1)*1024+x-3].full);
        else assert(pixel.full==0xa5a5a5a5);
    }
}
static void scene(lv_obj_t *screen) {
    lv_obj_remove_style_all(screen);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x182233),0);
    lv_obj_t *box=lv_obj_create(screen);lv_obj_remove_style_all(box);lv_obj_set_pos(box,100,80);lv_obj_set_size(box,400,200);
    lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);lv_obj_set_style_bg_color(box,lv_color_hex(0x880011),0);
    lv_obj_t *alpha=lv_obj_create(screen);lv_obj_remove_style_all(alpha);lv_obj_set_pos(alpha,200,120);lv_obj_set_size(alpha,400,200);
    lv_obj_set_style_bg_opa(alpha,128,0);lv_obj_set_style_bg_color(alpha,lv_color_hex(0x00aacc),0);
    lv_obj_t *layer=lv_obj_create(screen);lv_obj_remove_style_all(layer);lv_obj_set_pos(layer,30,350);lv_obj_set_size(layer,180,180);
    lv_obj_set_style_bg_opa(layer,LV_OPA_COVER,0);lv_obj_set_style_bg_color(layer,lv_color_hex(0x33cc55),0);
    lv_obj_set_style_radius(layer,30,0);lv_obj_set_style_shadow_width(layer,20,0);lv_obj_set_style_shadow_opa(layer,100,0);
    lv_obj_set_style_opa(layer,128,0); /* ordinary LVGL layer storage, not a DMA allocation */
    static lv_color_t pixels[300*180];for(unsigned i=0;i<300*180;i++)pixels[i]=lv_color_hex((i*37)&0xffffff);
    static lv_img_dsc_t picture={.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=300,.h=180},.data_size=sizeof(pixels),.data=(const uint8_t *)pixels};
    lv_obj_t *img=lv_img_create(screen);lv_img_set_src(img,&picture);lv_obj_set_pos(img,800,400); /* clipping at right edge */
}
static void cleanup_counts(void) {assert(!allocated && !mapped && !engine_live && !export_live && opened==closed);}
static void transition_frames(lv_disp_t *display,lv_scr_load_anim_t animation) {
    lv_obj_t *old=lv_scr_act(),*next=lv_obj_create(NULL);
    lv_obj_remove_style_all(next);lv_obj_set_style_bg_opa(next,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(next,lv_color_hex(0x0e4385),0);
    lv_color_t *reference=malloc(3*sizeof(expected));assert(reference);
    int baseline=allocated;
    for(unsigned mode=0;mode<2;mode++) {
        lv_scr_load(old);lv_obj_set_pos(old,0,0);lv_refr_now(display);
        if(mode)assert(!setenv("HZ_UI_TRANSITION_CACHE","0",1));
        hz_screen_load_anim(next,animation,300,0,false);
        assert(allocated==baseline+(mode?0:2));
        unsigned saved=0;
        for(unsigned elapsed=20;elapsed<=320;elapsed+=20) {
            lv_tick_inc(20);lv_anim_refr_now();lv_refr_now(display);
            if(elapsed==80 || elapsed==160 || elapsed==240) {
                if(!mode)memcpy(reference+saved*1024*600,hz_g2d_buffer(),sizeof(expected));
                else assert(!memcmp(reference+saved*1024*600,hz_g2d_buffer(),sizeof(expected)));
                saved++;
            }
        }
        assert(saved==3 && allocated==baseline && !display->prev_scr);
        assert(lv_obj_get_child_cnt(old) && !lv_obj_has_flag(lv_obj_get_child(old,0),LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_get_style_bg_img_src(next,0));
    }
    assert(!unsetenv("HZ_UI_TRANSITION_CACHE"));
    lv_scr_load(old);lv_obj_set_pos(old,0,0);
    hz_screen_load_anim(next,LV_SCR_LOAD_ANIM_MOVE_LEFT,300,0,false);
    lv_tick_inc(60);lv_anim_refr_now();lv_refr_now(display);
    lv_scr_load(old);lv_obj_set_pos(old,0,0);lv_refr_now(display);
    assert(allocated==baseline);
    assert(!display->prev_scr && !display->draw_prev_over_act);
    lv_obj_del(next);free(reference);lv_refr_now(display);
    puts("PASS: native DMA cached slide frames equal live LVGL frames at three positions; both page buffers and hidden states restored after completion");
}
static void extended_paths(lv_disp_t *display) {
    lv_draw_ctx_t *ctx=display->driver->draw_ctx;
    lv_area_t full={0,0,1023,599},area={13,29,412,228};
    ctx->buf=hz_g2d_buffer();ctx->buf_area=&full;ctx->clip_area=&full;
    lv_disp_t *previous=_lv_refr_get_disp_refreshing();_lv_refr_set_disp_refreshing(display);
    static lv_opa_t mask[400*200];
    static lv_color_t source[400*200];
    for(unsigned i=0;i<400*200;i++) {mask[i]=(i*13)%256;source[i]=lv_color_hex(i*157);}
    lv_draw_sw_blend_dsc_t dsc={.blend_area=&area,.opa=159,.mask_buf=mask,.mask_area=&area,
        .mask_res=LV_DRAW_MASK_RES_CHANGED,.color=lv_color_hex(0x409ac0)};
    for(unsigned mode=0;mode<4;mode++) {
        dsc.src_buf=mode&1?source:NULL;dsc.opa=mode<2?159:255;
        lv_area_t clipped={51,48,367,208};ctx->clip_area=mode&1?&clipped:&full;
        for(unsigned i=0;i<1024*600;i++)expected[i]=lv_color_hex(i*19);
        memcpy(ctx->buf,expected,sizeof(expected));
        ctx->buf=expected;lv_draw_sw_blend_basic(ctx,&dsc);ctx->buf=hz_g2d_buffer();
        unsigned before=blends;
        if(mode==3) {fail_request=G2D_CMD_BLD_H;fail_after=1;partial_failure=true;}
        ((lv_draw_sw_ctx_t *)ctx)->blend(ctx,&dsc);
        if(memcmp(expected,ctx->buf,sizeof(expected)))for(unsigned i=0;i<1024*600;i++)if(expected[i].full!=((lv_color_t *)ctx->buf)[i].full) {
            fprintf(stderr,"alpha mode %u mismatch at %u: software %08x hardware %08x\n",mode,i,expected[i].full,((lv_color_t *)ctx->buf)[i].full);break;
        }
        assert(blends>before && !memcmp(expected,ctx->buf,sizeof(expected)));
        if(mode==3)assert(!fail_request); /* Tile 0 stays blended exactly once. */
    }
    ctx->clip_area=&full;
    _lv_refr_set_disp_refreshing(previous);
}
static void scale_and_background(lv_disp_t *display) {
    lv_color_t *background=hz_g2d_background_alloc();assert(background);
    for(unsigned i=0;i<1024*600;i++)background[i]=lv_color_hex(i*37);
    hz_g2d_background_ready();
    lv_draw_ctx_t *ctx=display->driver->draw_ctx;lv_area_t full={0,0,1023,599},area={-30,-40,993,559};
    ctx->buf=hz_g2d_buffer();ctx->buf_area=&full;ctx->clip_area=&full;
    lv_draw_sw_blend_dsc_t blend={.blend_area=&area,.src_buf=background,.opa=255};
    ((lv_draw_sw_ctx_t *)ctx)->blend(ctx,&blend);
    for(unsigned y=0;y<560;y++)for(unsigned x=0;x<994;x++)
        assert(((lv_color_t *)ctx->buf)[y*1024+x].full==background[(y+40)*1024+x+30].full);
    hz_g2d_background_free(background);assert(allocated==2);
    static lv_color_t pixels[100*80];for(unsigned i=0;i<100*80;i++)pixels[i]=lv_color_hex(0x2694d8);
    lv_draw_img_dsc_t dsc;lv_draw_img_dsc_init(&dsc);dsc.zoom=512;dsc.pivot=(lv_point_t){50,40};
    area=(lv_area_t){-10,10,89,89}; /* Scaled image crosses left and top edges. */
    memset(ctx->buf,0,sizeof(expected));
    ctx->draw_img_decoded(ctx,&dsc,&area,(const uint8_t *)pixels,LV_IMG_CF_TRUE_COLOR);
    assert(scales);
    for(unsigned y=0;y<600;y++)for(unsigned x=0;x<1024;x++)
        assert(((lv_color_t *)ctx->buf)[y*1024+x].full==(y<130 && x<140?pixels[0].full:0));
}
int main(void) {
    memory=(struct SunxiMemOpsS){.open=memory_open,.close=memory_close,.palloc=allocate,.pfree=release,
        .cpu_get_phyaddr=dma_address,.get_bufferFd=buffer_fd,.flush_cache=sync_cache};
    assert(!setenv("HZ_UI_G2D","0",1));
    assert(!hz_display_init("/dev/fb0","auto"));assert(!hz_g2d_buffer());
    assert(!engine_live && !export_live && !allocated);hz_display_close();cleanup_counts();
    assert(!unsetenv("HZ_UI_G2D"));
    for(failure=NO_ENGINE;failure<=NO_FD;failure++) {
        assert(!hz_display_init("/dev/fb0","auto"));assert(!hz_g2d_buffer());hz_display_close();cleanup_counts();
    }
    failure=NONE;lv_init();assert(!hz_display_init("/dev/fb0","auto"));assert(hz_g2d_buffer());
    assert(!framebuffer_enabled && !framebuffer_enables);
    memset(scanout,0xa5,sizeof(scanout));
    lv_color_t fallback[1024*40];lv_disp_draw_buf_t buffer;lv_disp_drv_t driver;lv_disp_drv_init(&driver);
    hz_display_configure(&driver,&buffer,fallback,1024*40);assert(driver.direct_mode && buffer.size==1024*600);
    lv_disp_t *display=lv_disp_drv_register(&driver);scene(lv_scr_act());lv_refr_now(display);
    lv_obj_t *picture=lv_obj_get_child(lv_scr_act(),3);
    lv_area_t covered={810,410,840,440};lv_cover_check_info_t info={.area=&covered,.res=LV_COVER_RES_COVER};
    lv_event_send(picture,LV_EVENT_COVER_CHECK,&info);assert(info.res==LV_COVER_RES_COVER);
    assert(fills && copies && blends && presents==1);
    assert(framebuffer_enabled && framebuffer_enables==1);check_scanout();
    scale_and_background(display);
    transition_frames(display,LV_SCR_LOAD_ANIM_MOVE_LEFT);
    transition_frames(display,LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    lv_obj_invalidate(lv_scr_act());lv_refr_now(display);
    memcpy(expected,hz_g2d_buffer(),sizeof(expected));
    /* A partial failed fill must be fully repaired by the real software path. */
    fail_request=G2D_CMD_FILLRECT_H;partial_failure=true;lv_obj_invalidate(lv_scr_act());lv_refr_now(display);
    assert(!memcmp(expected,hz_g2d_buffer(),sizeof(expected)));check_scanout();
    assert(!fail_request && !hz_g2d_present());
    lv_disp_remove(display);hz_display_close();assert(!framebuffer_enabled);cleanup_counts();
    /* Upload allocation failure preserves correct software image rendering. */
    failure=NO_UPLOAD;assert(!hz_display_init("/dev/fb0","auto"));memset(scanout,0xa5,sizeof(scanout));
    lv_disp_drv_init(&driver);hz_display_configure(&driver,&buffer,fallback,1024*40);
    display=lv_disp_drv_register(&driver);scene(lv_scr_act());lv_refr_now(display);
    assert(!memcmp(expected,hz_g2d_buffer(),sizeof(expected)));check_scanout();
    /* A partially failed scanout copy is repaired by CPU presentation. */
    fail_request=G2D_CMD_BITBLT_H;partial_failure=true;lv_obj_invalidate(lv_scr_act());lv_refr_now(display);check_scanout();
    lv_disp_remove(display);hz_display_close();cleanup_counts();
    failure=NONE;assert(!hz_display_init("/dev/fb0","auto"));
    lv_disp_drv_init(&driver);hz_display_configure(&driver,&buffer,fallback,1024*40);
    display=lv_disp_drv_register(&driver);extended_paths(display);
    lv_disp_remove(display);hz_display_close();cleanup_counts();
    puts("PASS: production G2D/LVGL fills, alpha/masked image composition, clipped DMA background/scaling and presentation; software pixel comparison, stride/pan, partial alpha-error rollback and cleanup");
    return 0;
}
