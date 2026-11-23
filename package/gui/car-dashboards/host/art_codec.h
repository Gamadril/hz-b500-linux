/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_ART_CODEC_H
#define HZ_ART_CODEC_H
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
struct hz_art_header {uint32_t magic,width,height,x,y,runs,checksum;};
#define HZ_ART_MAGIC 0x31505a48u
/* Run payloads use (pixel count, ARGB8888). Dimensions and total expansion are
 * checked before allocation/writes; a checksum rejects incomplete/stale media. */
bool hz_art_read(FILE *,struct hz_art_header *,uint32_t *pixels,unsigned capacity);
bool hz_art_write(FILE *,const struct hz_art_header *,const uint32_t *pixels);
#endif
