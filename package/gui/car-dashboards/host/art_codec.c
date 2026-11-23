/* SPDX-License-Identifier: GPL-2.0-only */
#include "art_codec.h"
#include <stdlib.h>
#define LIMIT (1024u*600u)
static uint32_t hash_word(uint32_t hash,uint32_t word) {
    for(unsigned i=0;i<4;i++) {hash^=(word>>(8*i))&255;hash*=16777619u;}return hash;
}
bool hz_art_read(FILE *file,struct hz_art_header *header,uint32_t *pixels,unsigned capacity) {
    if(fread(header,sizeof(*header),1,file)!=1 || header->magic!=HZ_ART_MAGIC ||
       !header->width || header->width>1024 || !header->height || header->height>600 ||
       header->x>1024-header->width || header->y>600-header->height ||
       !header->runs || header->runs>header->width*header->height)return false;
    if(!pixels)return true;
    unsigned size=header->width*header->height,used=0;uint32_t hash=2166136261u;
    if(capacity<size)return false;
    for(unsigned i=0;i<header->runs;i++) {
        uint32_t run[2];if(fread(run,sizeof(run),1,file)!=1 || !run[0] || run[0]>size-used)return false;
        hash=hash_word(hash_word(hash,run[0]),run[1]);
        for(unsigned j=0;j<run[0];j++)pixels[used++]=run[1];
    }
    return used==size && hash==header->checksum && fgetc(file)==EOF;
}
bool hz_art_write(FILE *file,const struct hz_art_header *geometry,const uint32_t *pixels) {
    struct hz_art_header header=*geometry;header.magic=HZ_ART_MAGIC;header.runs=0;header.checksum=2166136261u;
    unsigned size=header.width*header.height;
    if(!size || size>LIMIT)return false;
    for(unsigned i=0;i<size;) {
        unsigned end=i+1;while(end<size && pixels[end]==pixels[i])end++;
        header.runs++;header.checksum=hash_word(hash_word(header.checksum,end-i),pixels[i]);i=end;
    }
    if(fwrite(&header,sizeof(header),1,file)!=1)return false;
    for(unsigned i=0;i<size;) {
        unsigned end=i+1;while(end<size && pixels[end]==pixels[i])end++;
        uint32_t run[2]={end-i,pixels[i]};if(fwrite(run,sizeof(run),1,file)!=1)return false;i=end;
    }
    return true;
}
