/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../device/config/chips/d1s/hz-b500-splash.h"
typedef uint8_t u8;
#include "../../lichee/brandy-2.0/opensbi/platform/generic/allwinner/hz-b500-font-8x16.h"

static unsigned char pixels[HZ_SPLASH_BYTES], header[512];
static void fail(const char *s) { fprintf(stderr, "boot-logo: %s\n", s); exit(1); }
static FILE *open_file(const char *s, const char *mode)
{
	FILE *f = fopen(s, mode);
	if (!f) { perror(s); exit(1); }
	return f;
}
static void text(const char *s, unsigned y, unsigned scale, unsigned color)
{
	unsigned x = (HZ_SPLASH_WIDTH - strlen(s) * 8 * scale) / 2;
	for (; *s; ++s, x += 8 * scale)
		for (unsigned j = 0; j < 16 * scale; ++j)
			for (unsigned i = 0; i < 8 * scale; ++i)
				if (glyphs[(unsigned char)*s - 32][j / scale] & (0x80 >> (i / scale)))
					hz_splash_put32(pixels + ((y + j) * HZ_SPLASH_WIDTH + x + i) * 4, color);
}
int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--verify")) {
		FILE *f = open_file(argv[2], "rb");
		if (fread(header, 1, 512, f) != 512 || !hz_splash_header_valid(header) ||
		    fread(pixels, 1, sizeof(pixels), f) != sizeof(pixels) ||
		    hz_splash_u32(header + 32) != hz_splash_crc(pixels, sizeof(pixels)))
			fail("invalid header, dimensions or pixel checksum");
		fclose(f);
		puts("Valid HZ-B500 1024x600 ARGB8888 boot logo");
		return 0;
	}
	const char *output;
	if (argc == 3 && !strcmp(argv[1], "--default")) {
		output = argv[2];
		for (unsigned i = 0; i < sizeof(pixels); i += 4)
			hz_splash_put32(pixels + i, 0xff101820);
		text("HZ-B500", 232, 5, 0xffe5e9ed);
		text("Linux", 346, 2, 0xff8dabc2);
	} else if (argc == 4 && !strcmp(argv[1], "--raw")) {
		output = argv[3];
		FILE *f = open_file(argv[2], "rb");
		if (fread(pixels, 1, sizeof(pixels), f) != sizeof(pixels) || fgetc(f) != EOF)
			fail("raw input must contain exactly 2457600 bytes (BGRA byte order)");
		fclose(f);
	} else if (argc == 4 && !strcmp(argv[1], "--bmp")) {
		output = argv[3];
		FILE *f = open_file(argv[2], "rb");
		unsigned char b[54];
		if (fread(b, 1, sizeof(b), f) != sizeof(b) || b[0] != 'B' || b[1] != 'M' ||
		    hz_splash_u32(b + 14) < 40 || hz_splash_u32(b + 18) != HZ_SPLASH_WIDTH ||
		    ((int32_t)hz_splash_u32(b + 22) != 600 && (int32_t)hz_splash_u32(b + 22) != -600) ||
		    b[26] != 1 || b[27] || (b[28] != 24 && b[28] != 32) || b[29] ||
		    hz_splash_u32(b + 30) || hz_splash_u32(b + 10) < 54)
			fail("BMP must be uncompressed 1024x600 RGB, 24 or 32 bits");
		if (fseek(f, hz_splash_u32(b + 10), SEEK_SET)) fail("BMP seek failed");
		for (unsigned row = 0; row < 600; ++row) {
			unsigned y = (int32_t)hz_splash_u32(b + 22) < 0 ? row : 599 - row;
			for (unsigned x = 0; x < 1024; ++x) {
				unsigned char *p = pixels + (y * 1024 + x) * 4;
				if (fread(p, 1, 3, f) != 3 || (b[28] == 32 && fgetc(f) == EOF))
					fail("truncated BMP");
				p[3] = 255;
			}
		}
		fclose(f);
	} else {
		fprintf(stderr, "Usage: %s --default OUTPUT | --raw INPUT OUTPUT | --bmp INPUT OUTPUT | --verify IMAGE\n", argv[0]);
		return 2;
	}
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
	FILE *f = open_file(output, "wb");
	if (fwrite(header, 1, 512, f) != 512 || fwrite(pixels, 1, sizeof(pixels), f) != sizeof(pixels) || fclose(f))
		fail("write failed");
	return 0;
}
