/* SPDX-License-Identifier: GPL-2.0-only */
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <linux/fs.h>
#include <jpeglib.h>
#include <png.h>
#include "hz-b500-splash.h"

#define ROOT_START 73728U
#define ROOT_SECTORS 1048576U
#define LOGO_START (ROOT_START + ROOT_SECTORS)
#define DISK_BYTES ((uint64_t)(LOGO_START + HZ_SPLASH_SECTORS) * 512)
#define DECODE_LIMIT (16U * 1024U * 1024U)
static unsigned char pixels[HZ_SPLASH_BYTES], header[512];
static void fail(const char *msg)
{
	fprintf(stderr, "boot-logo: %s\n", msg);
	exit(EXIT_FAILURE);
}
static void syserr(const char *msg) { perror(msg); exit(EXIT_FAILURE); }
static void *alloc(size_t n)
{
	void *p = malloc(n);
	if (!p) fail("out of memory");
	return p;
}
static void dimensions(unsigned w, unsigned h)
{
	if (!w || !h || w > 8192 || h > 8192 || (uint64_t)w * h * 4 > DECODE_LIMIT)
		fail("image exceeds limit: 8192 per side and 4,194,304 pixels");
}
static void jpeg_error(j_common_ptr c)
{
	(*c->err->output_message)(c);
	fail("JPEG decode failed");
}
static void png_error_exit(png_structp p, png_const_charp msg)
{
	(void)p;
	fail(msg);
}
/* Decode into bounded RGBA storage. Fatal errors occur before opening a destination. */
static unsigned char *decode(FILE *f, unsigned *w, unsigned *h)
{
	unsigned char sig[54];
	size_t count = fread(sig, 1, sizeof(sig), f);
	rewind(f);
	if (count >= 2 && sig[0] == 0xff && sig[1] == 0xd8) {
		struct jpeg_decompress_struct c;
		struct jpeg_error_mgr err;
		c.err = jpeg_std_error(&err);
		err.error_exit = jpeg_error;
		jpeg_create_decompress(&c);
		c.mem->max_memory_to_use = 4 * 1024 * 1024;
		jpeg_stdio_src(&c, f);
		jpeg_read_header(&c, TRUE);
		dimensions(c.image_width, c.image_height);
		c.out_color_space = JCS_RGB;
		jpeg_start_decompress(&c);
		*w = c.output_width; *h = c.output_height;
		dimensions(*w, *h);
		if (c.output_components != 3) fail("JPEG must decode to RGB");
		unsigned char *data = alloc((size_t)*w * *h * 4), *row = alloc(*w * 3);
		while (c.output_scanline < *h) {
			unsigned y = c.output_scanline;
			if (jpeg_read_scanlines(&c, &row, 1) != 1) fail("short JPEG scanline");
			for (unsigned x = 0; x < *w; ++x) {
				memcpy(data + ((size_t)y * *w + x) * 4, row + x * 3, 3);
				data[((size_t)y * *w + x) * 4 + 3] = 255;
			}
		}
		jpeg_finish_decompress(&c);
		/* libjpeg can recover a truncated image; do not install its repaired output. */
		if (err.num_warnings) fail("JPEG contains warnings or truncated data");
		jpeg_destroy_decompress(&c);
		free(row);
		return data;
	}
	if (count >= 8 && !png_sig_cmp(sig, 0, 8)) {
		png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, png_error_exit, NULL);
		if (!p) fail("PNG initialization failed");
		png_infop info = png_create_info_struct(p);
		if (!info) fail("PNG initialization failed");
		png_set_user_limits(p, 8192, 8192);
		png_init_io(p, f);
		png_read_info(p, info);
		*w = png_get_image_width(p, info); *h = png_get_image_height(p, info);
		dimensions(*w, *h);
		int color = png_get_color_type(p, info), depth = png_get_bit_depth(p, info);
		if (depth == 16) png_set_strip_16(p);
		if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(p);
		if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(p);
		int transparent = png_get_valid(p, info, PNG_INFO_tRNS);
		if (transparent) png_set_tRNS_to_alpha(p);
		if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(p);
		if (!(color & PNG_COLOR_MASK_ALPHA) && !transparent) png_set_filler(p, 255, PNG_FILLER_AFTER);
		png_set_interlace_handling(p);
		png_read_update_info(p, info);
		if (png_get_rowbytes(p, info) != (size_t)*w * 4) fail("unsupported PNG layout");
		unsigned char *data = alloc((size_t)*w * *h * 4);
		png_bytep *rows = alloc(*h * sizeof(*rows));
		for (unsigned y = 0; y < *h; ++y) rows[y] = data + (size_t)y * *w * 4;
		png_read_image(p, rows);
		png_read_end(p, info);
		png_destroy_read_struct(&p, &info, NULL);
		free(rows);
		return data;
	}
	if (count == sizeof(sig) && sig[0] == 'B' && sig[1] == 'M') {
		*w = hz_splash_u32(sig + 18);
		int32_t height = (int32_t)hz_splash_u32(sig + 22);
		if (height == INT32_MIN) fail("invalid BMP height");
		*h = height < 0 ? -height : height;
		dimensions(*w, *h);
		if (hz_splash_u32(sig + 14) < 40 || sig[26] != 1 || sig[27] ||
		    (sig[28] != 24 && sig[28] != 32) || sig[29] || hz_splash_u32(sig + 30) ||
		    hz_splash_u32(sig + 10) < 54) fail("BMP must be uncompressed 24/32-bit RGB");
		if (fseeko(f, hz_splash_u32(sig + 10), SEEK_SET)) syserr("BMP seek");
		unsigned stride = (*w * (sig[28] / 8) + 3) & ~3U;
		unsigned char *data = alloc((size_t)*w * *h * 4), *row = alloc(stride);
		for (unsigned y = 0; y < *h; ++y) {
			if (fread(row, 1, stride, f) != stride) fail("truncated BMP");
			unsigned char *dst = data + (size_t)(height < 0 ? y : *h - 1 - y) * *w * 4;
			for (unsigned x = 0; x < *w; ++x) {
				const unsigned char *src = row + x * (sig[28] / 8);
				dst[x * 4] = src[2]; dst[x * 4 + 1] = src[1]; dst[x * 4 + 2] = src[0];
				dst[x * 4 + 3] = 255; /* RGB BMP's fourth byte is reserved. */
			}
		}
		free(row);
		return data;
	}
	fail("input must be JPEG, PNG or uncompressed RGB BMP");
	return NULL;
}
static void artwork(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) syserr(path);
	unsigned w, h;
	unsigned char *src = decode(f, &w, &h);
	fclose(f);
	unsigned dw = HZ_SPLASH_WIDTH, dh = (uint64_t)h * dw / w;
	if (dh > HZ_SPLASH_HEIGHT) { dh = HZ_SPLASH_HEIGHT; dw = (uint64_t)w * dh / h; }
	if (!dw) dw = 1;
	if (!dh) dh = 1;
	unsigned ox = (HZ_SPLASH_WIDTH - dw) / 2, oy = (HZ_SPLASH_HEIGHT - dh) / 2;
	/* Composite over black before interpolation, so transparent edges stay clean. */
	for (size_t i = 0; i < (size_t)w * h * 4; i += 4)
		for (unsigned c = 0; c < 3; ++c) src[i + c] = (src[i + c] * src[i + 3] + 127) / 255;
	for (unsigned i = 3; i < sizeof(pixels); i += 4) pixels[i] = 255;
	for (unsigned y = 0; y < dh; ++y) {
		uint64_t fy = dh == 1 ? 0 : (uint64_t)y * (h - 1) * 65536 / (dh - 1);
		unsigned sy = fy >> 16, y1 = sy + (sy + 1 < h), wy = fy & 65535;
		for (unsigned x = 0; x < dw; ++x) {
			uint64_t fx = dw == 1 ? 0 : (uint64_t)x * (w - 1) * 65536 / (dw - 1);
			unsigned sx = fx >> 16, x1 = sx + (sx + 1 < w), wx = fx & 65535;
			unsigned char *dst = pixels + ((oy + y) * HZ_SPLASH_WIDTH + ox + x) * 4;
			for (unsigned c = 0; c < 3; ++c) {
				uint64_t a = src[((size_t)sy * w + sx) * 4 + c] * (65536ULL - wx) + src[((size_t)sy * w + x1) * 4 + c] * (uint64_t)wx;
				uint64_t b = src[((size_t)y1 * w + sx) * 4 + c] * (65536ULL - wx) + src[((size_t)y1 * w + x1) * 4 + c] * (uint64_t)wx;
				dst[2 - c] = (a * (65536 - wy) + b * wy + (1ULL << 31)) >> 32;
			}
		}
	}
	free(src);
	memcpy(header, "HB5SPL01", 8);
	hz_splash_put32(header + 8, 1);
	hz_splash_put32(header + 12, HZ_SPLASH_WIDTH);
	hz_splash_put32(header + 16, HZ_SPLASH_HEIGHT);
	hz_splash_put32(header + 20, HZ_SPLASH_STRIDE);
	hz_splash_put32(header + 24, 1);
	hz_splash_put32(header + 28, HZ_SPLASH_BYTES);
	hz_splash_put32(header + 32, hz_splash_crc(pixels, sizeof(pixels)));
	hz_splash_put32(header + 36, 1);
	hz_splash_put32(header + 40, hz_splash_crc(header, 40));
}
/* Positional, complete I/O: short transfers and EINTR never silently succeed. */
static int transfer(int fd, void *buf, size_t size, off_t at, int writing)
{
	unsigned char *p = buf;
	while (size) {
		ssize_t n = writing ? pwrite(fd, p, size, at) : pread(fd, p, size, at);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		p += n; size -= n; at += n;
	}
	return 0;
}
static int valid_image(int fd, off_t at)
{
	unsigned char h[512];
	if (transfer(fd, h, sizeof(h), at, 0) || !hz_splash_header_valid(h)) return 0;
	unsigned char *check = alloc(HZ_SPLASH_BYTES);
	int valid = !transfer(fd, check, HZ_SPLASH_BYTES, at + 512, 0) &&
		hz_splash_crc(check, HZ_SPLASH_BYTES) == hz_splash_u32(h + 32);
	free(check);
	return valid;
}
static void validate_mbr(int fd)
{
	unsigned char m[512];
	if (transfer(fd, m, sizeof(m), 0, 0) || m[510] != 0x55 || m[511] != 0xaa ||
	    m[450] != 0x83 || hz_splash_u32(m + 454) != ROOT_START ||
	    hz_splash_u32(m + 458) != ROOT_SECTORS || m[466] != HZ_SPLASH_TYPE ||
	    hz_splash_u32(m + 470) != LOGO_START || hz_splash_u32(m + 474) != HZ_SPLASH_SECTORS)
		fail("refusing write: disk does not have the HZ-B500 SD root/splash partition layout");
}
static uint64_t sys_number(const char *path)
{
	FILE *f = fopen(path, "r");
	unsigned long long n;
	if (!f || fscanf(f, "%llu", &n) != 1) fail("cannot inspect kernel SD partition geometry");
	fclose(f);
	return n;
}
static void check_node(dev_t dev, const char *name)
{
	char path[128];
	snprintf(path, sizeof(path), "/sys/class/block/%s/dev", name);
	FILE *f = fopen(path, "r");
	unsigned maj, min;
	if (!f || fscanf(f, "%u:%u", &maj, &min) != 2 || dev != makedev(maj, min))
		fail("SD device node does not match the kernel device");
	fclose(f);
}
static int destination(const char *image, off_t *at)
{
	int fd = open(image ? image : "/dev/mmcblk0p2", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) syserr(image ? image : "/dev/mmcblk0p2");
	if (flock(fd, LOCK_EX | LOCK_NB)) syserr("splash already in use");
	struct stat st;
	if (fstat(fd, &st)) syserr("stat splash destination");
	if (image) {
		if (!S_ISREG(st.st_mode) || (uint64_t)st.st_size != DISK_BYTES)
			fail("install-image requires a regular 551 MiB HZ-B500 SD image");
		validate_mbr(fd);
		*at = (off_t)LOGO_START * 512;
	} else {
		if (!S_ISBLK(st.st_mode)) fail("/dev/mmcblk0p2 is not a block device");
		/* Confirm that this node is the kernel's p2, not a renamed whole disk/root. */
		check_node(st.st_rdev, "mmcblk0p2");
		if (sys_number("/sys/class/block/mmcblk0p2/partition") != 2 ||
		    sys_number("/sys/class/block/mmcblk0p2/start") != LOGO_START ||
		    sys_number("/sys/class/block/mmcblk0p2/size") != HZ_SPLASH_SECTORS)
			fail("kernel SD partition geometry does not match the splash layout");
		FILE *f = fopen("/proc/self/mountinfo", "r");
		if (!f) syserr("inspect mounts");
		char *line = NULL;
		size_t cap = 0;
		while (getline(&line, &cap, f) >= 0) {
			unsigned maj, min;
			if (sscanf(line, "%*u %*u %u:%u", &maj, &min) == 2 && st.st_rdev == makedev(maj, min))
				fail("refusing to overwrite a mounted partition");
		}
		free(line);
		if (ferror(f)) syserr("read mounts");
		fclose(f);
		uint64_t size;
		if (ioctl(fd, BLKGETSIZE64, &size) || size != HZ_SPLASH_RESERVE) fail("wrong splash partition size");
		int disk = open("/dev/mmcblk0", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		if (disk < 0) syserr("open SD partition table");
		if (fstat(disk, &st) || !S_ISBLK(st.st_mode)) fail("SD parent is not a block device");
		check_node(st.st_rdev, "mmcblk0");
		validate_mbr(disk);
		close(disk);
		*at = 0;
	}
	return fd;
}
static void install_logo(int fd, off_t at)
{
	unsigned char zero[512] = {0};
	unsigned char readback[512];
	/* Publish the valid header last. Interruption leaves an invalid logo and cold boot fallback. */
	if (transfer(fd, zero, sizeof(zero), at, 1) || fsync(fd)) syserr("invalidate splash");
	if (transfer(fd, pixels, sizeof(pixels), at + 512, 1) || fsync(fd) ||
	    transfer(fd, header, sizeof(header), at, 1) || fsync(fd) ||
	    transfer(fd, readback, sizeof(readback), at, 0) || memcmp(readback, header, sizeof(header)) ||
	    !valid_image(fd, at)) {
		(void)transfer(fd, zero, sizeof(zero), at, 1);
		(void)fsync(fd);
		fail("splash write/readback failed; logo invalidated where possible");
	}
	puts("Boot logo installed and verified; it will appear on the next SD boot.");
}
int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "convert")) {
		artwork(argv[2]);
		int fd = open(argv[3], O_WRONLY | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0644);
		if (fd < 0) syserr(argv[3]);
		struct stat st;
		if (fstat(fd, &st) || !S_ISREG(st.st_mode)) fail("convert destination must be a regular file");
		if (ftruncate(fd, 0)) syserr("truncate converted image");
		if (transfer(fd, header, sizeof(header), 0, 1) || transfer(fd, pixels, sizeof(pixels), 512, 1) || fsync(fd))
			syserr("write converted image");
		if (close(fd)) syserr("close converted image");
		return 0;
	}
	if ((argc == 3 && !strcmp(argv[1], "install")) || (argc == 4 && !strcmp(argv[1], "install-image"))) {
		artwork(argv[2]);
		off_t at;
		int fd = destination(argc == 4 ? argv[3] : NULL, &at);
		install_logo(fd, at);
		close(fd);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "verify")) {
		int fd = open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
		if (fd < 0) syserr(argv[2]);
		if (!valid_image(fd, 0)) fail("invalid splash header, dimensions or pixel checksum");
		close(fd);
		puts("Valid HZ-B500 1024x600 ARGB8888 boot logo");
		return 0;
	}
	fprintf(stderr, "Usage: %s convert INPUT.{jpg,png,bmp} OUTPUT.bin\n"
		"       %s install INPUT.{jpg,png,bmp}   (updates /dev/mmcblk0p2)\n"
		"       %s install-image INPUT.{jpg,png,bmp} SD_IMAGE\n"
		"       %s verify FILE_OR_SPLASH_PARTITION\n", argv[0], argv[0], argv[0], argv[0]);
	return 2;
}
