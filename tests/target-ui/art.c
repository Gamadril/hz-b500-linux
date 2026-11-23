/* SPDX-License-Identifier: GPL-2.0-only */
#include "art_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const uint32_t pixels[12]={0xff112233,0xff112233,0xff112233,0xff556677,0xff556677,0xff556677,0xff112233,0xffaabbcc,0xffaabbcc,0xffaabbcc,0xffaabbcc,0xffaabbcc};
    struct hz_art_header header={.width=4,.height=3,.x=10,.y=20};uint32_t decoded[12];
    FILE *file=tmpfile();assert(file && hz_art_write(file,&header,pixels));rewind(file);
    assert(hz_art_read(file,&header,decoded,12) && !memcmp(pixels,decoded,sizeof(pixels)));
    rewind(file);assert(!hz_art_read(file,&header,decoded,11));
    rewind(file);assert(fread(&header,sizeof(header),1,file)==1);header.width=1025;
    rewind(file);assert(fwrite(&header,sizeof(header),1,file)==1);rewind(file);assert(!hz_art_read(file,&header,decoded,12));
    fclose(file);file=tmpfile();header=(struct hz_art_header){.width=4,.height=3};assert(hz_art_write(file,&header,pixels));
    assert(!fseek(file,sizeof(header),SEEK_SET));uint32_t bad=13;assert(fwrite(&bad,4,1,file)==1);
    rewind(file);assert(!hz_art_read(file,&header,decoded,12));fclose(file);
    file=tmpfile();assert(hz_art_write(file,&(struct hz_art_header){.width=4,.height=3},pixels));
    assert(!fseek(file,sizeof(header)+4,SEEK_SET));bad=0;assert(fwrite(&bad,4,1,file)==1);
    rewind(file);assert(!hz_art_read(file,&header,decoded,12));fclose(file);
    puts("PASS: artwork lossless round trip, capacity/dimension bounds, run overflow and checksum validation");
    return 0;
}
