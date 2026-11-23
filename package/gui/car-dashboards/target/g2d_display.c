/* SPDX-License-Identifier: GPL-2.0-only */
#include "g2d_display.h"
#include "lvgl/src/draw/sw/lv_draw_sw.h"
#include "g2d_driver_enh.h"
#include <ion_mem_alloc.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1024
#define HEIGHT 600
#define FRAME_BYTES (WIDTH*HEIGHT*sizeof(lv_color_t))
#define UPLOAD_ROWS 64u
#define UPLOAD_BYTES (WIDTH*UPLOAD_ROWS*sizeof(lv_color_t))
#define MIN_PIXELS 16384u
#define SCALE_BYTES (1024u*1024u)
#define FBIO_GET_IONFD 0x4632
_Static_assert(sizeof(lv_color_t)==4,"G2D HMI requires 32-bit LVGL pixels");
static int engine=-1,scanout=-1,frame_fd=-1,upload_fd=-1;
static struct SunxiMemOpsS *memory;
static lv_color_t *frame,*upload;
static lv_color_t *background,*rollback,*scaled;
static int background_fd=-1,scaled_fd=-1;
static struct {lv_color_t *pixels;int fd;} snapshots[2];
static unsigned fb_stride,fb_rows,fb_x,fb_y;
static bool failed;

static g2d_image_enh image(int fd,unsigned width,unsigned height,unsigned x,unsigned y,unsigned w,unsigned h) {
    g2d_image_enh result={0};
    result.fd=fd;result.width=width;result.height=height;
    result.format=G2D_FORMAT_ARGB8888;result.mode=G2D_GLOBAL_ALPHA;result.alpha=255;
    result.clip_rect=(g2d_rect){(int)x,(int)y,w,h};
    /* fd mode lets the BSP import the real DMA buffer into the G2D IOMMU. */
    return result;
}
static void sync_rect(lv_color_t *base,unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h) {
    SunxiMemFlushCache(memory,base+y*stride+x,((h-1)*stride+w)*sizeof(*base));
}
static bool submit(unsigned request,void *parameters) {
    if(engine<0 || failed)return false;
    if(ioctl(engine,request,parameters)>=0)return true;
    perror("UI G2D request failed; using CPU rendering/presentation");failed=true;
    return false;
}
void hz_g2d_close(void) {
    for(unsigned i=0;i<2;i++)if(snapshots[i].pixels)hz_g2d_snapshot_free(snapshots[i].pixels);
    if(background)SunxiMemPfree(memory,background);
    if(scaled)SunxiMemPfree(memory,scaled);
    free(rollback);rollback=NULL;
    if(upload)SunxiMemPfree(memory,upload);
    if(frame)SunxiMemPfree(memory,frame);
    if(memory)SunxiMemClose(memory);
    if(scanout>=0)close(scanout);
    if(engine>=0)close(engine);
    frame=upload=NULL;memory=NULL;engine=scanout=frame_fd=upload_fd=-1;failed=false;
    background=scaled=NULL;background_fd=scaled_fd=-1;
}
lv_color_t *hz_g2d_open(int framebuffer,unsigned stride,unsigned rows,unsigned xoffset,unsigned yoffset) {
    hz_g2d_close();
    const char *setting=getenv("HZ_UI_G2D");
    if(setting && !strcmp(setting,"0")) {
        fprintf(stderr,"UI G2D disabled by HZ_UI_G2D=0; using CPU framebuffer presentation\n");return NULL;
    }
    if(stride<WIDTH || rows<HEIGHT || xoffset>stride-WIDTH || yoffset>rows-HEIGHT)return NULL;
    engine=open("/dev/g2d",O_RDWR|O_CLOEXEC);
    if(engine<0)goto unavailable;
    /* This BSP ioctl returns an owned fd directly. Its exporter takes its own
     * dma-buf reference; do not use the older broken FBIOGET_DMABUF path. */
    scanout=ioctl(framebuffer,FBIO_GET_IONFD,0);
    if(scanout<0)goto unavailable;
    struct SunxiMemOpsS *ops=GetMemAdapterOpsS();
    if(!ops || SunxiMemOpen(ops)<0)goto unavailable;
    memory=ops;
    frame=SunxiMemPalloc(memory,FRAME_BYTES);
    if(!frame || !SunxiMemGetPhysicAddressCpu(memory,frame))goto unavailable;
    frame_fd=SunxiMemGetBufferFd(memory,frame);
    if(frame_fd<0)goto unavailable;
    fb_stride=stride;fb_rows=rows;fb_x=xoffset;fb_y=yoffset;
    memset(frame,0,FRAME_BYTES);
    fprintf(stderr,"UI G2D: cached 1024x600 DMA surface (%u bytes), fills/copies, alpha compositing, scaling and presentation\n",(unsigned)FRAME_BYTES);
    return frame;
unavailable:
    hz_g2d_close();fprintf(stderr,"UI G2D unavailable; using CPU framebuffer presentation\n");return NULL;
}
lv_color_t *hz_g2d_buffer(void) {return frame;}
lv_color_t *hz_g2d_snapshot_alloc(void) {
    if(!memory || failed)return NULL;
    for(unsigned i=0;i<2;i++)if(!snapshots[i].pixels) {
        lv_color_t *pixels=SunxiMemPalloc(memory,FRAME_BYTES);
        if(!pixels)return NULL;
        int fd=-1;
        if(SunxiMemGetPhysicAddressCpu(memory,pixels))fd=SunxiMemGetBufferFd(memory,pixels);
        if(fd<0) {SunxiMemPfree(memory,pixels);return NULL;}
        snapshots[i].pixels=pixels;snapshots[i].fd=fd;return pixels;
    }
    return NULL;
}
void hz_g2d_snapshot_ready(lv_color_t *pixels) {
    for(unsigned i=0;i<2;i++)if(pixels && pixels==snapshots[i].pixels)SunxiMemFlushCache(memory,pixels,FRAME_BYTES);
}
void hz_g2d_snapshot_free(lv_color_t *pixels) {
    for(unsigned i=0;i<2;i++)if(pixels && pixels==snapshots[i].pixels) {
        SunxiMemPfree(memory,pixels);snapshots[i].pixels=NULL;snapshots[i].fd=-1;return;
    }
}
static int cached_source_fd(const lv_color_t *pixels) {
    if(pixels && pixels==frame)return frame_fd;
    if(pixels && pixels==background)return background_fd;
    for(unsigned i=0;i<2;i++)if(pixels && pixels==snapshots[i].pixels)return snapshots[i].fd;
    return -1;
}
bool hz_g2d_is_surface(const lv_color_t *pixels) {return cached_source_fd(pixels)>=0;}
lv_color_t *hz_g2d_background_alloc(void) {
    if(!memory || failed || background)return NULL;
    background=SunxiMemPalloc(memory,FRAME_BYTES);
    if(background && SunxiMemGetPhysicAddressCpu(memory,background)) {
        background_fd=SunxiMemGetBufferFd(memory,background);
        if(background_fd>=0)return background;
    }
    hz_g2d_background_free(background);return NULL;
}
void hz_g2d_background_ready(void) {
    if(background)SunxiMemFlushCache(memory,background,FRAME_BYTES);
}
void hz_g2d_background_free(lv_color_t *pixels) {
    if(pixels && pixels==background) {SunxiMemPfree(memory,background);background=NULL;background_fd=-1;}
}
bool hz_g2d_present(void) {
    if(!frame || failed)return false;
    SunxiMemFlushCache(memory,frame,FRAME_BYTES);
    /* On F133 RCQ, NONE selects the mixer, where global alpha also enables
     * the old destination as an overlay. ROT_0 is an explicit raw copy and
     * matches the existing Tina framebuffer presentation path. */
    g2d_blt_h operation={0};operation.flag_h=G2D_ROT_0;
    operation.src_image_h=image(frame_fd,WIDTH,HEIGHT,0,0,WIDTH,HEIGHT);
    operation.dst_image_h=image(scanout,fb_stride,fb_rows,fb_x,fb_y,WIDTH,HEIGHT);
    return submit(G2D_CMD_BITBLT_H,&operation);
}
static bool allocate_upload(void) {
    if(upload)return true;
    upload=SunxiMemPalloc(memory,UPLOAD_BYTES);
    if(!upload)return false;
    if(!SunxiMemGetPhysicAddressCpu(memory,upload))goto fail;
    upload_fd=SunxiMemGetBufferFd(memory,upload);
    if(upload_fd>=0)return true;
fail:
    SunxiMemPfree(memory,upload);upload=NULL;return false;
}
static bool target_context(lv_draw_ctx_t *context) {
    return frame && !failed && hz_g2d_is_surface(context->buf) && context->buf_area->x1==0 &&
        context->buf_area->y1==0 && context->buf_area->x2==WIDTH-1 && context->buf_area->y2==HEIGHT-1;
}
static void alpha_blend(lv_draw_ctx_t *context,const lv_draw_sw_blend_dsc_t *dsc,const lv_area_t *area) {
    lv_color_t *destination=context->buf;int destination_fd=cached_source_fd(destination);
    if(!allocate_upload())goto software;
    if(!rollback)rollback=malloc(UPLOAD_BYTES);
    if(!rollback)goto software;
    unsigned x=area->x1,y=area->y1,w=lv_area_get_width(area),h=lv_area_get_height(area);
    unsigned stride=lv_area_get_width(dsc->blend_area);
    bool masked=dsc->mask_buf && dsc->mask_res!=LV_DRAW_MASK_RES_FULL_COVER;
    unsigned mask_stride=masked?lv_area_get_width(dsc->mask_area):0;
    for(unsigned offset=0;offset<h;offset+=UPLOAD_ROWS) {
        unsigned count=LV_MIN(h-offset,UPLOAD_ROWS);
        for(unsigned j=0;j<count;j++)for(unsigned i=0;i<w;i++) {
            lv_color_t color=dsc->src_buf?dsc->src_buf[(y+offset+j-dsc->blend_area->y1)*stride+x+i-dsc->blend_area->x1]:dsc->color;
            unsigned opacity=dsc->opa;
            if(masked) {
                unsigned mask=dsc->mask_buf[(y+offset+j-dsc->mask_area->y1)*mask_stride+x+i-dsc->mask_area->x1];
                unsigned cover=dsc->src_buf?LV_OPA_MAX:LV_OPA_COVER;
                opacity=dsc->opa>LV_OPA_MAX?mask:mask>=cover?opacity:(mask*opacity)>>8;
            }
            color.ch.alpha=opacity;
            upload[j*w+i]=color;
        }
        /* Preserve the current tile: a failed translucent request must never
         * blend partially modified destination pixels a second time. */
        for(unsigned j=0;j<count;j++)memcpy(rollback+j*w,destination+(y+offset+j)*WIDTH+x,w*sizeof(*destination));
        SunxiMemFlushCache(memory,upload,w*count*sizeof(*upload));
        sync_rect(destination,WIDTH,x,y+offset,w,count);
        g2d_bld operation={0};operation.bld_cmd=G2D_BLD_SRCOVER;
        operation.dst_image=image(destination_fd,WIDTH,HEIGHT,x,y+offset,w,count);
        operation.src_image[0]=operation.dst_image; /* RCQ input 0 is bottom. */
        operation.src_image[1]=image(upload_fd,w,count,0,0,w,count);
        operation.src_image[1].mode=G2D_PIXEL_ALPHA;
        bool ok=submit(G2D_CMD_BLD_H,&operation);
        sync_rect(destination,WIDTH,x,y+offset,w,count);
        if(!ok) {
            for(unsigned j=0;j<count;j++)memcpy(destination+(y+offset+j)*WIDTH+x,rollback+j*w,w*sizeof(*destination));
            lv_area_t remaining=*area;remaining.y1=y+offset;
            const lv_area_t *previous=context->clip_area;context->clip_area=&remaining;
            lv_draw_sw_blend_basic(context,dsc);context->clip_area=previous;return;
        }
    }
    return;
software:
    lv_draw_sw_blend_basic(context,dsc);
}
static void blend(lv_draw_ctx_t *context,const lv_draw_sw_blend_dsc_t *dsc) {
    /* Layer buffers and decoded images may be ordinary malloc memory. Only the
     * owned full drawing surface can be a DMA destination here. Small blends
     * stay in software to avoid per-row ioctl and cache-maintenance overhead. */
    lv_area_t clipped;
    if(!target_context(context) || dsc->opa<LV_OPA_MIN ||
       dsc->blend_mode!=LV_BLEND_MODE_NORMAL ||
       !_lv_area_intersect(&clipped,dsc->blend_area,context->clip_area) ||
       clipped.x1<0 || clipped.y1<0 || clipped.x2>=WIDTH || clipped.y2>=HEIGHT ||
       lv_area_get_size(&clipped)<MIN_PIXELS || lv_area_get_width(&clipped)<3)goto software;
    if(dsc->mask_buf && dsc->mask_res==LV_DRAW_MASK_RES_TRANSP)return;
    if(dsc->opa<=LV_OPA_MAX || (dsc->mask_buf && dsc->mask_res!=LV_DRAW_MASK_RES_FULL_COVER)) {
        if(!lv_disp_get_default()->driver->antialiasing)goto software;
        alpha_blend(context,dsc,&clipped);return;
    }
    unsigned x=clipped.x1,y=clipped.y1,w=lv_area_get_width(&clipped),h=lv_area_get_height(&clipped);
    lv_color_t *destination=context->buf;int destination_fd=cached_source_fd(destination);
    if(!dsc->src_buf) {
        g2d_fillrect_h operation={0};
        operation.dst_image_h=image(destination_fd,WIDTH,HEIGHT,x,y,w,h);
        operation.dst_image_h.color=dsc->color.full;
        sync_rect(destination,WIDTH,x,y,w,h);
        bool ok=submit(G2D_CMD_FILLRECT_H,&operation);
        sync_rect(destination,WIDTH,x,y,w,h);
        if(ok)return;
    } else {
        uintptr_t source=(uintptr_t)dsc->src_buf;
        if(source>=(uintptr_t)destination && source<(uintptr_t)destination+FRAME_BYTES)goto software;
        int cached_fd=cached_source_fd(dsc->src_buf);
        if(cached_fd>=0 && lv_area_get_width(dsc->blend_area)==WIDTH) {
            g2d_blt_h operation={0};operation.flag_h=G2D_ROT_0;
            operation.src_image_h=image(cached_fd,WIDTH,HEIGHT,x-dsc->blend_area->x1,y-dsc->blend_area->y1,w,h);
            operation.dst_image_h=image(destination_fd,WIDTH,HEIGHT,x,y,w,h);
            sync_rect(destination,WIDTH,x,y,w,h);
            bool ok=submit(G2D_CMD_BITBLT_H,&operation);sync_rect(destination,WIDTH,x,y,w,h);
            if(ok)return;
            goto software;
        }
        if(!allocate_upload())goto software;
        unsigned stride=lv_area_get_width(dsc->blend_area);
        const lv_color_t *row=dsc->src_buf+(y-dsc->blend_area->y1)*stride+x-dsc->blend_area->x1;
        /* Copy ordinary image memory into an owned DMA upload tile. Never
         * submit a malloc pointer or a guessed physical address to G2D. */
        for(unsigned offset=0;offset<h;offset+=UPLOAD_ROWS) {
            unsigned count=h-offset<UPLOAD_ROWS?h-offset:UPLOAD_ROWS;
            for(unsigned j=0;j<count;j++)memcpy(upload+j*w,row+(offset+j)*stride,w*sizeof(*row));
            SunxiMemFlushCache(memory,upload,w*count*sizeof(*upload));
            g2d_blt_h operation={0};operation.flag_h=G2D_ROT_0;
            operation.src_image_h=image(upload_fd,w,count,0,0,w,count);
            operation.dst_image_h=image(destination_fd,WIDTH,HEIGHT,x,y+offset,w,count);
            sync_rect(destination,WIDTH,x,y+offset,w,count);
            bool ok=submit(G2D_CMD_BITBLT_H,&operation);
            sync_rect(destination,WIDTH,x,y+offset,w,count);
            if(!ok)goto software;
        }
        return;
    }
software:
    /* This branch handles opaque writes; repeating them repairs partial writes.
     * Translucent requests use the per-tile rollback in alpha_blend instead. */
    lv_draw_sw_blend_basic(context,dsc);
}
static void draw_image(lv_draw_ctx_t *context,const lv_draw_img_dsc_t *dsc,const lv_area_t *coords,const uint8_t *pixels,lv_img_cf_t format) {
    /* Scale an opaque image into a bounded work surface, then crop via a raw
     * copy. Clipping the scaler's destination directly would stretch its input.
     * Non-integral edges, alpha images, recoloring, masks and rotation retain
     * LVGL's renderer. No current HMI artwork requires a scaled alpha image. */
    unsigned w=lv_area_get_width(coords),h=lv_area_get_height(coords),zoom=dsc->zoom;
    if(!target_context(context) || !zoom || zoom==LV_IMG_ZOOM_NONE || dsc->angle ||
       dsc->opa<=LV_OPA_MAX || dsc->blend_mode!=LV_BLEND_MODE_NORMAL || dsc->recolor_opa ||
       format!=LV_IMG_CF_TRUE_COLOR || lv_draw_mask_is_any(context->clip_area) ||
       w*h*sizeof(lv_color_t)>UPLOAD_BYTES || (w*zoom)%256 || (h*zoom)%256 ||
       (dsc->pivot.x*(int)zoom)%256 || (dsc->pivot.y*(int)zoom)%256)goto software;
    unsigned dw=w*zoom/256,dh=h*zoom/256;
    if(!dw || !dh || dw*dh*sizeof(lv_color_t)>SCALE_BYTES)goto software;
    int left=coords->x1+dsc->pivot.x-dsc->pivot.x*(int)zoom/256;
    int top=coords->y1+dsc->pivot.y-dsc->pivot.y*(int)zoom/256;
    lv_area_t destination_area={left,top,left+(int)dw-1,top+(int)dh-1},clip;
    if(!_lv_area_intersect(&clip,&destination_area,context->clip_area))return;
    if(clip.x1<0 || clip.y1<0 || clip.x2>=WIDTH || clip.y2>=HEIGHT || !allocate_upload())goto software;
    if(!scaled) {
        scaled=SunxiMemPalloc(memory,SCALE_BYTES);
        if(scaled && SunxiMemGetPhysicAddressCpu(memory,scaled))scaled_fd=SunxiMemGetBufferFd(memory,scaled);
        if(scaled_fd<0) {if(scaled)SunxiMemPfree(memory,scaled);scaled=NULL;goto software;}
    }
    memcpy(upload,pixels,w*h*sizeof(*upload));
    /* RCQ NONE invokes the scaler/mixer. PIXEL_ALPHA avoids enabling the old
     * destination as the upper input; opaque RGB has alpha 255 in this upload. */
    for(unsigned i=0;i<w*h;i++)upload[i].ch.alpha=255;
    SunxiMemFlushCache(memory,upload,w*h*sizeof(*upload));
    SunxiMemFlushCache(memory,scaled,dw*dh*sizeof(*scaled));
    g2d_blt_h operation={0};operation.flag_h=G2D_BLT_NONE_H;
    operation.src_image_h=image(upload_fd,w,h,0,0,w,h);operation.src_image_h.mode=G2D_PIXEL_ALPHA;
    operation.dst_image_h=image(scaled_fd,dw,dh,0,0,dw,dh);
    bool ok=submit(G2D_CMD_BITBLT_H,&operation);
    SunxiMemFlushCache(memory,scaled,dw*dh*sizeof(*scaled));
    if(!ok)goto software;
    unsigned cw=lv_area_get_width(&clip),ch=lv_area_get_height(&clip);
    operation.flag_h=G2D_ROT_0;
    operation.src_image_h=image(scaled_fd,dw,dh,clip.x1-left,clip.y1-top,cw,ch);
    lv_color_t *destination=context->buf;
    operation.dst_image_h=image(cached_source_fd(destination),WIDTH,HEIGHT,clip.x1,clip.y1,cw,ch);
    sync_rect(destination,WIDTH,clip.x1,clip.y1,cw,ch);
    ok=submit(G2D_CMD_BITBLT_H,&operation);sync_rect(destination,WIDTH,clip.x1,clip.y1,cw,ch);
    if(ok)return;
software:
    lv_draw_sw_img_decoded(context,dsc,coords,pixels,format);
}
static void draw_rect(lv_draw_ctx_t *context,const lv_draw_rect_dsc_t *dsc,const lv_area_t *coords) {
    /* Keep the root opaque for LVGL coverage decisions, but don't fill an
     * entire DMA rectangle immediately before replacing it with a known opaque
     * cached page. Borders/shadows retain the ordinary rectangle renderer. */
    if(target_context(context) && dsc->bg_img_src && dsc->bg_img_opa==LV_OPA_COVER &&
       !dsc->bg_img_recolor_opa && !dsc->bg_img_tiled &&
       lv_area_get_width(coords)==WIDTH && lv_area_get_height(coords)==HEIGHT &&
       lv_img_src_get_type(dsc->bg_img_src)==LV_IMG_SRC_VARIABLE) {
        const lv_img_dsc_t *image=dsc->bg_img_src;
        if(image->header.cf==LV_IMG_CF_TRUE_COLOR && image->header.w==WIDTH && image->header.h==HEIGHT &&
           cached_source_fd((const lv_color_t *)image->data)>=0) {
            lv_draw_rect_dsc_t adjusted=*dsc;adjusted.bg_opa=LV_OPA_TRANSP;
            lv_draw_sw_rect(context,&adjusted,coords);return;
        }
    }
    lv_draw_sw_rect(context,dsc,coords);
}
void hz_g2d_draw_ctx_init(lv_disp_drv_t *driver,lv_draw_ctx_t *context) {
    lv_draw_sw_init_ctx(driver,context);
    ((lv_draw_sw_ctx_t *)context)->blend=blend;
    context->draw_img_decoded=draw_image;
    context->draw_rect=draw_rect;
}
