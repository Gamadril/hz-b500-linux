/* SPDX-License-Identifier: GPL-2.0-only */
#include "art_codec.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
void hz_art_record(const char *);

static void convert(const char *source,const char *directory,const char *name,unsigned width,unsigned height)
{
    png_image image={.version=PNG_IMAGE_VERSION};
    if(!png_image_begin_read_from_file(&image,source)) {fprintf(stderr,"PDC artwork: %s\n",image.message);exit(1);}
    if(image.width!=width || image.height!=height) {fprintf(stderr,"Artwork has unexpected dimensions: %s\n",source);exit(1);}
    image.format=PNG_FORMAT_RGBA;
    uint8_t *rgba=malloc(PNG_IMAGE_SIZE(image));
    uint32_t *pixels=malloc(image.width*image.height*sizeof(*pixels));
    if(!rgba || !pixels || !png_image_finish_read(&image,NULL,rgba,0,NULL))exit(1);
    for(unsigned i=0;i<image.width*image.height;i++) {
        pixels[i]=((uint32_t)rgba[4*i+3]<<24)|((uint32_t)rgba[4*i]<<16)|((uint32_t)rgba[4*i+1]<<8)|rgba[4*i+2];
    }
    char path[512];
    if(snprintf(path,sizeof(path),"%s/%s.hzp",directory,name)>=(int)sizeof(path))exit(1);
    FILE *file=fopen(path,"wb");if(!file) {perror(path);exit(1);}
    struct hz_art_header header={.width=image.width,.height=image.height,.x=(1024-image.width)/2,.y=(600-image.height)/2};
    if(!hz_art_write(file,&header,pixels) || fclose(file))exit(1);
    hz_art_record(name);
    free(rgba);free(pixels);png_image_free(&image);
}
void hz_pdc_art(const char *source,const char *directory) {convert(source,directory,"pdc-car",526,241);}
void hz_launcher_art(const char *source,const char *directory)
{
    const char *names[]={"dashboard","music","media","settings","gamepad","bt-music","android-auto","android-auto-usb"};
    for(unsigned i=0;i<8;i++) {
        char path[512],name[64];snprintf(path,sizeof(path),"%s/%s.png",source,names[i]);
        snprintf(name,sizeof(name),"launcher-%s",names[i]);convert(path,directory,name,96,96);
    }
}
void hz_vehicle_art(const char *source,const char *directory)
{
    const char *names[]={"body","body_full","door_lf","door_rf","door_lr","door_rr","low_beam","full_beam","turn_left","turn_right"};
    const unsigned widths[]={75,75,15,15,11,10,173,109,34,34},heights[]={166,166,39,38,35,35,82,76,179,179};
    for(unsigned i=0;i<10;i++) {
        char path[512],name[64];snprintf(path,sizeof(path),"%s/%s.png",source,names[i]);
        snprintf(name,sizeof(name),"vehicle-%s",names[i]);convert(path,directory,name,widths[i],heights[i]);
    }
}
