/* Generate a small alpha/interlaced PNG; validate fitted opaque BGRA output. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include "../../device/config/chips/d1s/hz-b500-splash.h"
int main(int argc, char **argv)
{
	assert(argc == 3);
	if (!strcmp(argv[1], "generate")) {
		FILE *f = fopen(argv[2], "wb"); assert(f);
		png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); assert(p);
		png_infop info = png_create_info_struct(p); assert(info);
		assert(!setjmp(png_jmpbuf(p)));
		png_init_io(p, f);
		png_set_IHDR(p, info, 2, 1, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_ADAM7, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_write_info(p, info);
		unsigned char row[8] = {255, 0, 0, 128, 0, 0, 255, 255};
		png_bytep rows[1] = {row};
		png_write_image(p, rows); png_write_end(p, info);
		png_destroy_write_struct(&p, &info); assert(!fclose(f));
		return 0;
	}
	FILE *f = fopen(argv[2], "rb"); assert(f);
	unsigned char header[512], pixels[HZ_SPLASH_BYTES];
	assert(fread(header, 1, sizeof(header), f) == sizeof(header)); assert(hz_splash_header_valid(header));
	assert(fread(pixels, 1, sizeof(pixels), f) == sizeof(pixels)); fclose(f);
	for (unsigned i = 3; i < sizeof(pixels); i += 4) assert(pixels[i] == 255);
	/* 2:1 image fits as 1024x512, with 44 black rows at top and bottom. */
	assert(hz_splash_u32(pixels) == 0xff000000);
	assert(hz_splash_u32(pixels + 43 * HZ_SPLASH_STRIDE) == 0xff000000);
	assert(hz_splash_u32(pixels + 44 * HZ_SPLASH_STRIDE) == 0xff800000);
	assert(hz_splash_u32(pixels + 44 * HZ_SPLASH_STRIDE + 1023 * 4) == 0xff0000ff);
	assert(hz_splash_u32(pixels + 556 * HZ_SPLASH_STRIDE) == 0xff000000);
	assert(hz_splash_u32(pixels + 44 * HZ_SPLASH_STRIDE + 512 * 4) == 0xff400080);
	puts("PASS: interlaced PNG, alpha over black, bilinear resize and aspect-preserving letterbox");
	return 0;
}
