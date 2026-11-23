/* SPDX-License-Identifier: GPL-2.0-only */
/* Rasterized and live clipped glyphs can differ at the outermost LCD column.
 * Require exact interior pixels and bound that one-column clipping difference. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FILE *open_frame(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);char line[40];
    assert(fgets(line,sizeof(line),file) && !strcmp(line,"P6\n"));
    assert(fgets(line,sizeof(line),file) && !strcmp(line,"1024 600\n"));
    assert(fgets(line,sizeof(line),file) && !strcmp(line,"255\n"));return file;
}
int main(int argc,char **argv)
{
    assert(argc==3);FILE *left=open_frame(argv[1]),*right=open_frame(argv[2]);
    unsigned edge_pixels=0;unsigned char a[3072],b[3072];
    for(unsigned y=0;y<600;y++) {
        assert(fread(a,1,sizeof(a),left)==sizeof(a) && fread(b,1,sizeof(b),right)==sizeof(b));
        if(memcmp(a+3,b+3,sizeof(a)-6)) {fprintf(stderr,"Slide interior differs at row %u\n",y);return 1;}
        edge_pixels+=!!memcmp(a,b,3)+!!memcmp(a+3069,b+3069,3);
    }
    assert(fgetc(left)==EOF && fgetc(right)==EOF);fclose(left);fclose(right);
    if(edge_pixels>64) {fprintf(stderr,"Slide clipping differs in %u edge pixels\n",edge_pixels);return 1;}
    printf("PASS: exact slide interior; %u outer-column glyph-clipping pixels differ\n",edge_pixels);return 0;
}
