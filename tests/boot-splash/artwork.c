/* Deliberately asymmetric image: catches channel swaps and reversed rows. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../device/config/chips/d1s/hz-b500-splash.h"
static void pixel(FILE *f,unsigned x,unsigned y,int bytes,int alpha)
{
    unsigned char p[4]={x&255,y&255,(x+y)&255,alpha};
    assert(fwrite(p,1,bytes,f)==(unsigned)bytes);
}
int main(int argc,char **argv)
{
    assert(argc==4);
    FILE *f=fopen(argv[1],"wb");assert(f);
    for(unsigned y=0;y<600;y++)for(unsigned x=0;x<1024;x++)pixel(f,x,y,4,255);
    fclose(f);
    for(unsigned which=0;which<2;which++) {
        unsigned char h[54]={0};int bytes=which?4:3;
        memcpy(h,"BM",2);hz_splash_put32(h+2,54+1024*600*bytes);
        hz_splash_put32(h+10,54);hz_splash_put32(h+14,40);
        hz_splash_put32(h+18,1024);hz_splash_put32(h+22,which?(unsigned)-600:600);
        h[26]=1;h[28]=bytes*8;
        f=fopen(argv[2+which],"wb");assert(f);assert(fwrite(h,1,54,f)==54);
        for(unsigned row=0;row<600;row++)for(unsigned x=0;x<1024;x++)
            pixel(f,x,which?row:599-row,bytes,0);
        fclose(f);
    }
}
