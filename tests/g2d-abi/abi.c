/* Compare the actual userspace and kernel ioctl layouts, including payloads. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include G2D_HEADER

#define FIELD(type, member) printf(#type "." #member "=%zu\n", offsetof(type, member))
#define SIZE(type) printf(#type "=%zu\n", sizeof(type))
int main(void)
{
    SIZE(g2d_image_enh);
    FIELD(g2d_image_enh, bbuff); FIELD(g2d_image_enh, color);
    FIELD(g2d_image_enh, format); FIELD(g2d_image_enh, laddr);
    FIELD(g2d_image_enh, haddr); FIELD(g2d_image_enh, width);
    FIELD(g2d_image_enh, height); FIELD(g2d_image_enh, align);
    FIELD(g2d_image_enh, clip_rect); FIELD(g2d_image_enh, resize);
    FIELD(g2d_image_enh, coor);
    FIELD(g2d_image_enh, gamut); FIELD(g2d_image_enh, bpremul);
    FIELD(g2d_image_enh, alpha); FIELD(g2d_image_enh, mode);
    FIELD(g2d_image_enh, fd); FIELD(g2d_image_enh, use_phy_addr);
    FIELD(g2d_image_enh, color_range);
    SIZE(g2d_fillrect_h); SIZE(g2d_blt_h); SIZE(g2d_bld);
    SIZE(g2d_maskblt); SIZE(struct mixer_para);
    FIELD(g2d_blt_h, src_image_h); FIELD(g2d_blt_h, dst_image_h);
    FIELD(g2d_bld, dst_image); FIELD(g2d_bld, src_image);
    printf("commands=%u,%u,%u,%lu\n", G2D_CMD_FILLRECT_H,
           G2D_CMD_BITBLT_H, G2D_CMD_BLD_H, (unsigned long)G2D_CMD_MIXER_TASK);
    printf("copy flags: explicit ROT_0=%#x, mixer NONE=%#x\n",G2D_ROT_0,G2D_BLT_NONE_H);

    g2d_fillrect_h fill;
    memset(&fill, 0, sizeof(fill));
    fill.dst_image_h.width = 1024;
    fill.dst_image_h.height = 1200;
    fill.dst_image_h.clip_rect.w = 1024;
    fill.dst_image_h.clip_rect.h = 600;
    fill.dst_image_h.laddr[0] = 0xff800000;
    fill.dst_image_h.alpha = 255;
    fill.dst_image_h.mode = G2D_PIXEL_ALPHA;
    fill.dst_image_h.use_phy_addr = 1;
    /* Identical serialized requests must reach the kernel, not just sizes. */
    const unsigned char *bytes = (const unsigned char *)&fill;
    for (size_t i = 0; i < sizeof(fill); i++) printf("%02x", bytes[i]);
    putchar('\n');
    return 0;
}
