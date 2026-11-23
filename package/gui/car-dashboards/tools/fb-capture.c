/* SPDX-License-Identifier: GPL-2.0-only */
/* Read the current framebuffer viewport and write a PNG, without changing it. */
#define _GNU_SOURCE
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

int main(int argc,char **argv) {
    if(argc!=3) {fprintf(stderr,"Usage: fb-capture FRAMEBUFFER NEW_PNG\n");return 2;}
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);
    if(fd<0) {perror("framebuffer open");return 1;}
    struct fb_var_screeninfo v;struct fb_fix_screeninfo f;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v) || ioctl(fd,FBIOGET_FSCREENINFO,&f)) {
        perror("framebuffer info");close(fd);return 1;
    }
    uint64_t end=(uint64_t)(v.yoffset+v.yres-1)*f.line_length+(uint64_t)(v.xoffset+v.xres)*4;
    if(!v.xres || !v.yres || v.xres>8192 || v.yres>8192 || v.bits_per_pixel!=32 ||
       v.red.length!=8 || v.green.length!=8 || v.blue.length!=8 ||
       v.red.offset>24 || v.green.offset>24 || v.blue.offset>24 ||
       (uint64_t)(v.xoffset+v.xres)*4>f.line_length || end>f.smem_len) {
        fprintf(stderr,"Unsupported framebuffer layout\n");close(fd);return 1;
    }
    unsigned char *map=mmap(NULL,f.smem_len,PROT_READ,MAP_SHARED,fd,0);
    if(map==MAP_FAILED) {perror("framebuffer map");close(fd);return 1;}
    FILE *output=fopen(argv[2],"wbx");
    if(!output) {perror("PNG open");munmap(map,f.smem_len);close(fd);return 1;}
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    png_infop info=png?png_create_info_struct(png):NULL;
    unsigned char *row=malloc((size_t)v.xres*3);
    int result=1;
    if(!png || !info || !row || setjmp(png_jmpbuf(png))) goto cleanup;
    png_init_io(png,output);
    png_set_IHDR(png,info,v.xres,v.yres,8,PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    for(unsigned y=0;y<v.yres;y++) {
        const unsigned char *pixels=map+(size_t)(v.yoffset+y)*f.line_length+(size_t)v.xoffset*4;
        for(unsigned x=0;x<v.xres;x++) {
            uint32_t pixel;memcpy(&pixel,pixels+(size_t)x*4,4);
            row[x*3]=(pixel>>v.red.offset)&255;
            row[x*3+1]=(pixel>>v.green.offset)&255;
            row[x*3+2]=(pixel>>v.blue.offset)&255;
        }
        png_write_row(png,row);
    }
    png_write_end(png,info);result=0;
cleanup:
    if(png)png_destroy_write_struct(&png,info?&info:NULL);
    free(row);
    if(fclose(output))result=1;
    munmap(map,f.smem_len);close(fd);
    return result;
}
