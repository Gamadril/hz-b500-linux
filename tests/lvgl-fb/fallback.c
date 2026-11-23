/* Fault injection against the actual framebuffer backend; no device access. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static int fake_open(const char *path, int flags, ...);
static int fake_close(int fd);
static int fake_ioctl(int fd, unsigned long request, ...);
static void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int fake_munmap(void *addr, size_t length);
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#include "package/gui/littlevgl-8/lv_drivers/display/sunxifb.c"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap

static uint32_t frames[24], rotate_frame[12];
static unsigned int pans, g2d_calls, cache_flushes, ready_calls, closes, unmaps;
static unsigned int allocs, frees, g2d_closes, mem_closes;
static bool fail_g2d, fail_mem, fail_alloc, fail_address, fail_info, fail_map;
static unsigned int displayed;

static int fake_open(const char *path, int flags, ...) { return 4; }
static int fake_close(int fd) { assert(fd == 4); closes++; return 0; }
static int fake_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    assert(fd == 4);
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (request == FBIOGET_FSCREENINFO) {
        *(struct fb_fix_screeninfo *)arg = (struct fb_fix_screeninfo) {
            .line_length = 16, .smem_len = sizeof(frames), .smem_start = 0xff800000
        };
    } else if (request == FBIOGET_VSCREENINFO) {
        if (fail_info) { errno = EIO; return -1; }
        *(struct fb_var_screeninfo *)arg = (struct fb_var_screeninfo) {
            .xres = 4, .yres = 3, .xres_virtual = 4, .yres_virtual = 6,
            .bits_per_pixel = 32
        };
    } else if (request == FBIOPAN_DISPLAY) {
        displayed = ((struct fb_var_screeninfo *)arg)->yoffset;
        pans++;
    } else {
        assert(request == FBIO_CACHE_SYNC || request == FBIO_ENABLE_CACHE);
    }
    return 0;
}
static void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    assert(length == sizeof(frames) && fd == 4);
    if (fail_map) { errno = ENOMEM; return MAP_FAILED; }
    return frames;
}
static int fake_munmap(void *addr, size_t length)
{
    assert(addr == frames && length == sizeof(frames));
    unmaps++;
    return 0;
}
bool sunxifb_g2d_init(uint32_t bpp) { return !fail_g2d; }
void sunxifb_g2d_deinit(void) { g2d_closes++; }
bool sunxifb_mem_init(void) { return !fail_mem; }
void sunxifb_mem_deinit(void) { mem_closes++; }
void *sunxifb_mem_alloc(size_t size, char *label)
{
    allocs++;
    assert(size == sizeof(rotate_frame));
    if (fail_alloc) { errno = ENODEV; return NULL; }
    return rotate_frame;
}
void sunxifb_mem_free(void **data, char *label)
{
    assert(*data == rotate_frame);
    *data = NULL;
    frees++;
}
void *sunxifb_mem_get_phyaddr(void *data)
{ return fail_address ? NULL : (void *)(uintptr_t)0x43000000; }
void sunxifb_mem_flush_cache(void *data, size_t size)
{
    assert(data == rotate_frame && size == sizeof(rotate_frame));
    cache_flushes++;
}
int sunxifb_g2d_blit_to_fb(uintptr_t src, uint32_t sw, uint32_t sh,
        uint32_t sx, uint32_t sy, uint32_t scw, uint32_t sch,
        uintptr_t dst, uint32_t dw, uint32_t dh, uint32_t dx,
        uint32_t dy, uint32_t dcw, uint32_t dch, g2d_blt_flags_h rotated)
{
    assert(src && dst && sw && sh && scw && sch && dw && dh && dcw && dch);
    g2d_calls++;
    return 0;
}
void lv_disp_flush_ready(lv_disp_drv_t *drv) { ready_calls++; }
bool lv_disp_flush_is_last(lv_disp_drv_t *drv) { return true; }

static void check_cpu_fallback(void)
{
    uint32_t width, height;
    lv_disp_drv_t drv = {0};
    lv_area_t full = {0, 0, 3, 2}, pixel = {1, 1, 1, 1};
    lv_color_t colors[12], changed = {.full = 0x12345678};
    for (unsigned int i = 0; i < 12; i++) colors[i].full = 0xff000000 + i;
    sunxifb_init(LV_DISP_ROT_NONE);
    sunxifb_get_sizes(&width, &height);
    assert(width == 4 && height == 3 && !sinfo.g2d_en);
    sunxifb_flush(&drv, &full, colors);
    assert(pans == 1 && displayed == 3 && ready_calls == 1);
    assert(memcmp(frames, colors, sizeof(colors)) == 0);
    assert(memcmp(frames + 12, colors, sizeof(colors)) == 0);
    sunxifb_flush(&drv, &pixel, &changed);
    assert(pans == 2 && displayed == 0 && ready_calls == 2);
    assert(frames[5] == changed.full && frames[17] == changed.full);
    assert(frames[0] == colors[0].full && frames[23] == colors[11].full);
    assert(g2d_calls == 0 && cache_flushes == 0);
    sunxifb_exit();
    assert(closes == 1 && unmaps == 1);
    sunxifb_exit(); /* Cleanup is idempotent. */
    assert(closes == 1 && unmaps == 1);
    sunxifb_get_sizes(&width, &height);
    assert(width == 0 && height == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    fail_alloc = !strcmp(argv[1], "allocation") || !strcmp(argv[1], "rotation");
    fail_address = !strcmp(argv[1], "address");
    fail_mem = !strcmp(argv[1], "ion-open");
    fail_g2d = !strcmp(argv[1], "g2d-open");
    fail_info = !strcmp(argv[1], "fb-info");
    fail_map = !strcmp(argv[1], "fb-map");
    if (fail_info || fail_map || !strcmp(argv[1], "rotation")) {
        uint32_t width, height;
        lv_disp_drv_t drv = {0};
        lv_area_t area = {0, 0, 0, 0};
        lv_color_t color = {.full = 1};
        sunxifb_init(LV_DISP_ROT_90);
        sunxifb_get_sizes(&width, &height);
        assert(width == 0 && height == 0);
        sunxifb_flush(&drv, &area, &color);
        assert(ready_calls == 1 && !pans && !g2d_calls && !cache_flushes);
        assert(closes == 1);
        assert(unmaps == (!fail_info && !fail_map));
    } else if (!strcmp(argv[1], "success")) {
        uint32_t width, height;
        lv_disp_drv_t drv = {0};
        lv_area_t area = {0, 0, 2, 3};
        lv_color_t colors[12] = {0};
        sunxifb_init(LV_DISP_ROT_90);
        sunxifb_get_sizes(&width, &height);
        assert(width == 3 && height == 4);
        sunxifb_flush(&drv, &area, colors);
        assert(g2d_calls == 1 && cache_flushes == 1 && pans == 1);
        sunxifb_exit();
        assert(allocs == 1 && frees == 1 && g2d_closes == 1 && mem_closes == 1);
    } else {
        check_cpu_fallback();
        assert(g2d_closes == !fail_g2d);
        assert(mem_closes == (!fail_g2d && !fail_mem));
        assert(frees == fail_address);
    }
    printf("PASS: %s\n", argv[1]);
    return 0;
}
