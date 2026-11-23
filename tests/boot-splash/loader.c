/* Actual Boot0 loading function, extracted by check.sh with an SD read model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../../device/config/chips/d1s/hz-b500-splash.h"
static unsigned char mbr[512], image[512+HZ_SPLASH_BYTES];
static int fail_read;
static unsigned flushes;
static void flush_dcache_range(unsigned long start,unsigned long end)
{
    assert(end>start && end<=HZ_SPLASH_BASE+HZ_SPLASH_RESERVE);
    ++flushes;
}
static unsigned long mmc_bread(int dev,unsigned long start,unsigned count,void *dst)
{
    assert(dev==0);
    if(start==0 && count==1){memcpy(dst,mbr,512);return 1;}
    assert(start==73728 || start==73729);
    if(start==73728){assert(count==1);memcpy(dst,image,512);return 1;}
    assert(count==HZ_SPLASH_BYTES/512);
    memcpy(dst,image+512,HZ_SPLASH_BYTES);
    return fail_read?count-1:count;
}
#include "loader.inc"
int main(int argc,char **argv)
{
    assert(argc==2);
    assert(mmap((void *)HZ_SPLASH_BASE,HZ_SPLASH_RESERVE,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void *)HZ_SPLASH_BASE);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(image,1,sizeof(image),f)==sizeof(image));fclose(f);
    mbr[510]=0x55;mbr[511]=0xaa;mbr[466]=HZ_SPLASH_TYPE;
    hz_splash_put32(mbr+470,73728);hz_splash_put32(mbr+474,HZ_SPLASH_SECTORS);
    load_hz_splash(0);assert(hz_splash_header_valid((void *)HZ_SPLASH_META));
    assert(flushes==3);
    assert(!memcmp((void *)HZ_SPLASH_BASE,image+512,HZ_SPLASH_BYTES));
    /* Even if stale RAM happens to match the image, a short read is failure. */
    fail_read=1;load_hz_splash(0);assert(!hz_splash_header_valid((void *)HZ_SPLASH_META));
    fail_read=0;image[512]^=1;load_hz_splash(0);assert(!hz_splash_header_valid((void *)HZ_SPLASH_META));
    image[512]^=1;image[12]^=1;load_hz_splash(0);assert(!hz_splash_header_valid((void *)HZ_SPLASH_META));
    image[12]^=1;mbr[466]=0x83;load_hz_splash(0);assert(!hz_splash_header_valid((void *)HZ_SPLASH_META));
    mbr[466]=HZ_SPLASH_TYPE;load_hz_splash(2);assert(!hz_splash_header_valid((void *)HZ_SPLASH_META));
    puts("PASS: Boot0 SD p2 loader, CRC/dimensions, stale RAM, short reads and wrong-medium rejection");
}
