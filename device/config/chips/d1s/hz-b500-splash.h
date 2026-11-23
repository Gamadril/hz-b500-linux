/* SPDX-License-Identifier: BSD-2-Clause */
/* Shared, little-endian on-disk/RAM format. No pointers or compiler structs. */
#ifndef HZ_B500_SPLASH_H
#define HZ_B500_SPLASH_H
#define HZ_SPLASH_BASE 0x43c00000UL
#define HZ_SPLASH_RESERVE 0x300000UL
#define HZ_SPLASH_META (HZ_SPLASH_BASE + HZ_SPLASH_RESERVE - 512)
#define HZ_SPLASH_WIDTH 1024U
#define HZ_SPLASH_HEIGHT 600U
#define HZ_SPLASH_STRIDE (HZ_SPLASH_WIDTH * 4U)
#define HZ_SPLASH_BYTES (HZ_SPLASH_STRIDE * HZ_SPLASH_HEIGHT)
#define HZ_SPLASH_SECTORS 6144U
#define HZ_SPLASH_TYPE 0xda

static inline unsigned int hz_splash_u32(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static inline void hz_splash_put32(unsigned char *p, unsigned int v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static inline unsigned int hz_splash_crc(const void *data, unsigned int size)
{
	const unsigned char *p = data;
	unsigned int crc = ~0U, i;
	while (size--) {
		crc ^= *p++;
		for (i = 0; i < 8; ++i)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
	}
	return ~crc;
}

static inline int hz_splash_header_valid(const unsigned char *h)
{
	const char magic[] = "HB5SPL01";
	unsigned int i;
	for (i = 0; i < 8; ++i)
		if (h[i] != (unsigned char)magic[i]) return 0;
	return hz_splash_u32(h + 8) == 1 &&
		hz_splash_u32(h + 12) == HZ_SPLASH_WIDTH &&
		hz_splash_u32(h + 16) == HZ_SPLASH_HEIGHT &&
		hz_splash_u32(h + 20) == HZ_SPLASH_STRIDE &&
		hz_splash_u32(h + 24) == 1 && /* little-endian ARGB8888 */
		hz_splash_u32(h + 28) == HZ_SPLASH_BYTES &&
		hz_splash_u32(h + 36) == 1 && /* enabled */
		hz_splash_u32(h + 40) == hz_splash_crc(h, 40);
}
#endif
