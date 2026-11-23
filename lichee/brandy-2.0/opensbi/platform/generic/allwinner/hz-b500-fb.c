/* SPDX-License-Identifier: BSD-2-Clause */
/* HZ-B500 early SBI console: one DE2 UI layer, no heap or interrupts. */
#include <libfdt.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_timer.h>
#include "hz-b500-fb.h"
#include "hz-b500-font-8x16.h"
#include "../../../../../../device/config/chips/d1s/hz-b500-splash.h"

#define CCU             0x02001000UL
#define DE              0x05000000UL
#define GLB             (DE + 0x100000)
#define BLD             (DE + 0x101000)
#define UI              (DE + 0x103000)
#define TCON            0x05461000UL
#define FB_BASE         0x43c00000UL
#define FB_RESERVE      0x00300000UL
#define FB_WIDTH        1024U
#define FB_HEIGHT       600U
#define FB_BYTES        (FB_WIDTH * FB_HEIGHT * 4)
#define CELL_W          8U
#define CELL_H          16U
#define TEXT_ROWS       (FB_HEIGHT / CELL_H)
#define TEXT_ROW_BYTES  (FB_WIDTH * CELL_H * 4U)
#define TEXT_BYTES      (TEXT_ROWS * TEXT_ROW_BYTES)
#define TEXT_COLOR      0xffaaaaaaU /* Linux's normal console foreground. */

static const struct sbi_console_device *serial_console;
static bool console_active;
static unsigned int column, row;

static u32 rd(unsigned long addr) { return readl((void *)addr); }
static void wr(unsigned long addr, u32 value) { writel(value, (void *)addr); }
static void update(unsigned long addr, u32 mask, u32 value)
{
	wr(addr, (rd(addr) & ~mask) | value);
}

static void clean_pixels(unsigned long start, unsigned long end)
{
#ifndef HZ_B500_FB_TEST
	register unsigned long addr asm("a0") = start & ~63UL;
	for (; addr < end; addr += 64)
		asm volatile(".long 0x0295000b" : : "r"(addr) : "memory");
	asm volatile(".long 0x01b0000b" : : : "memory"); /* sync.s */
#endif
}

static void clear_screen(void)
{
	memset((void *)FB_BASE, 0, FB_BYTES);
	clean_pixels(FB_BASE, FB_BASE + FB_BYTES);
}

static void newline(void)
{
	column = 0;
	if (++row >= TEXT_ROWS) {
		unsigned long *dst = (unsigned long *)FB_BASE;
		const unsigned long *src = (void *)(FB_BASE + TEXT_ROW_BYTES);
		unsigned long *last = (void *)(FB_BASE + TEXT_BYTES - TEXT_ROW_BYTES);
		unsigned long *end = (void *)(FB_BASE + TEXT_BYTES);

		/* Aligned RV64 copies move the overlapping text area upward. */
		while (dst < last)
			*dst++ = *src++;
		while (dst < end)
			*dst++ = 0xff000000ff000000UL;
		row = TEXT_ROWS - 1;
		clean_pixels(FB_BASE, FB_BASE + TEXT_BYTES);
	}
}

static void draw_character(unsigned char ch)
{
	unsigned int x, y;
	if (!console_active)
		return;
	/* Kernel clock gating/reset or a new layer address ends firmware access. */
	if (!(rd(CCU + 0x60c) & 1) || !(rd(CCU + 0x600) & (1U << 31)) ||
	    !(rd(GLB) & 1) ||
	    rd(UI + 0x10) != FB_BASE) {
		console_active = false;
		return;
	}
	if (ch == '\r') { column = 0; return; }
	if (ch == '\n') { newline(); return; }
	if (ch == '\b') { if (column) column--; return; }
	if (ch == '\t') { column = (column + 8) & ~7U; return; }
	if (ch < 32 || ch >= 127)
		return;
	if (column >= FB_WIDTH / CELL_W)
		newline();
	for (y = 0; y < CELL_H; y++) {
		u32 *pixels = (u32 *)FB_BASE + (row * CELL_H + y) * FB_WIDTH +
			     column * CELL_W;
		u8 bits = glyphs[ch - 32][y];
		for (x = 0; x < CELL_W; x++)
			pixels[x] = (bits & (0x80U >> x)) ?
				    TEXT_COLOR : 0xff000000;
		clean_pixels((unsigned long)pixels, (unsigned long)(pixels + CELL_W));
	}
	column++;
}

static void mirror_putc(char ch)
{
	if (serial_console->console_putc)
		serial_console->console_putc(ch);
	else if (serial_console->console_puts)
		serial_console->console_puts(&ch, 1);
	draw_character(ch);
}

static int mirror_getc(void)
{
	return serial_console->console_getc ? serial_console->console_getc() : -1;
}

static struct sbi_console_device fb_console = {
	.name = "hz-b500-lcd+uart",
	.console_putc = mirror_putc,
	.console_getc = mirror_getc,
};

static int validate_memory(const void *fdt)
{
	int reserved, memory, len;
	const fdt32_t *reg;

	memory = fdt_path_offset(fdt, "/memory@40000000");
	reg = fdt_getprop(fdt, memory, "reg", &len);
	if (!reg || len != 16 || fdt32_to_cpu(reg[0]) ||
	    fdt32_to_cpu(reg[1]) != 0x40000000 || fdt32_to_cpu(reg[2]) ||
	    fdt32_to_cpu(reg[3]) < FB_BASE + FB_RESERVE - 0x40000000)
		return SBI_EINVAL;
	reserved = fdt_path_offset(fdt, "/reserved-memory/framebuffer@43c00000");
	reg = fdt_getprop(fdt, reserved, "reg", &len);
	if (!reg || len != 16 || fdt32_to_cpu(reg[0]) ||
	    fdt32_to_cpu(reg[1]) != FB_BASE || fdt32_to_cpu(reg[2]) ||
	    fdt32_to_cpu(reg[3]) != FB_RESERVE)
		return SBI_EINVAL;
	return 0;
}

static int prepare_handoff(void *fdt, bool splash)
{
	int disp, len, rc;
	const fdt32_t *boot_disp;
	char boot_fb[96];
	disp = fdt_node_offset_by_compatible(fdt, -1, "allwinner,sunxi-disp");
	if (disp < 0)
		return SBI_ENOTSUPP;
	boot_disp = fdt_getprop(fdt, disp, "boot_disp", &len);
	if (boot_disp && (len != 4 || fdt32_to_cpu(*boot_disp)))
		return SBI_ENOTSUPP;
	/* Space for framebuffer/adoption metadata; FDT lives below the kernel. */
	if (fdt_totalsize(fdt) > 0xf0000)
		return SBI_EINVAL;
	rc = fdt_open_into(fdt, fdt, fdt_totalsize(fdt) + 512);
	if (rc)
		return rc;
	disp = fdt_node_offset_by_compatible(fdt, -1, "allwinner,sunxi-disp");
	sbi_snprintf(boot_fb, sizeof(boot_fb), "%lx,%x,%x,%x,%x,0,0,0,0",
		     FB_BASE, FB_WIDTH, FB_HEIGHT, 32, FB_WIDTH * 4);
	rc = fdt_setprop_string(fdt, disp, "boot_fb0", boot_fb);
	if (!rc)
		rc = fdt_setprop(fdt, disp, "allwinner,opensbi-framebuffer", NULL, 0);
	if (!rc && splash)
		rc = fdt_setprop(fdt, disp, "allwinner,opensbi-splash-active", NULL, 0);
	if (!rc && splash)
		rc = fdt_setprop_u32(fdt, disp, "boot_disp", 0x100); /* LCD0 */
	if (rc) {
		fdt_delprop(fdt, disp, "boot_fb0");
		fdt_delprop(fdt, disp, "allwinner,opensbi-framebuffer");
		fdt_delprop(fdt, disp, "allwinner,opensbi-splash-active");
		fdt_setprop_u32(fdt, disp, "boot_disp", 0);
	}
	return rc;
}

int hz_b500_fb_init(void *fdt, u32 width, u32 height)
{
	u32 size = ((FB_HEIGHT - 1) << 16) | (FB_WIDTH - 1);
	unsigned int layer;
	int rc, lcd;
	bool splash;

	if (width != FB_WIDTH || height != FB_HEIGHT || !sbi_console_get_device())
		return SBI_ENOTSUPP;
	rc = validate_memory(fdt);
	if (rc)
		return rc;
	lcd = fdt_node_offset_by_compatible(fdt, -1, "allwinner,sunxi-lcd0");
	splash = !!fdt_getprop(fdt, lcd, "allwinner,opensbi-splash", NULL);
	if (splash && (!hz_splash_header_valid((const void *)HZ_SPLASH_META) ||
	    hz_splash_crc((const void *)FB_BASE, FB_BYTES) !=
			 hz_splash_u32((const void *)(HZ_SPLASH_META + 32))))
		return SBI_EINVAL;
	rc = prepare_handoff(fdt, splash);
	if (rc)
		return rc;

	if (splash)
		clean_pixels(FB_BASE, FB_BASE + FB_BYTES);
	else
		clear_screen();
	/* Already locked VIDEO0-4x / 4 = 348 MHz for the 58 MHz panel. */
	wr(CCU + 0x600, (1U << 31) | (1U << 24) | 3);
	update(CCU + 0x60c, 1U << 16, 0);
	update(CCU + 0x60c, (1U << 16) | 1U, (1U << 16) | 1U);
	sbi_timer_udelay(10);
	update(DE + 0x000, 1, 1);
	update(DE + 0x004, 1, 1);
	update(DE + 0x008, 1, 1);
	update(DE + 0x00c, 0xf, 0);
	update(DE + 0x010, 0xf, 0); /* mixer 0 -> port 0 */
	/* DE master 2 reads physical addresses until Linux initializes IOMMU. */
	update(CCU + 0x7bc, 1, 1);
	update(0x02010030, 1U << 2, 1U << 2);
	wr(GLB, 0);
	wr(GLB + 0x0c, size);
	for (layer = 0; layer < 4; layer++)
		wr(DE + 0x102000 + layer * 0x30, 0); /* video layers off */
	for (layer = 1; layer < 4; layer++)
		wr(UI + layer * 0x20, 0);
	wr(UI + 0x000, 0xff000003); /* ARGB8888, global alpha 255, layer 0 */
	wr(UI + 0x004, size);
	wr(UI + 0x008, 0);
	wr(UI + 0x00c, FB_WIDTH * 4);
	wr(UI + 0x010, FB_BASE);
	wr(UI + 0x080, 0);
	wr(UI + 0x088, size);
	wr(BLD + 0x000, 0x101); /* pipe 0 + opaque background */
	wr(BLD + 0x004, 0xff000000);
	wr(BLD + 0x008, size);
	wr(BLD + 0x00c, 0);
	wr(BLD + 0x080, 1); /* pipe 0 reads UI channel 1 */
	wr(BLD + 0x084, 0);
	wr(BLD + 0x088, 0xff000000);
	wr(BLD + 0x08c, size);
	wr(BLD + 0x090, 0x03010301);
	wr(BLD + 0x0fc, 0);
	wr(GLB, 1);
	wr(GLB + 0x008, 1);
	update(0x0546001c, 3, 0); /* DE port 0 -> LCD0 */
	update(TCON + 0x040, 7, 0); /* DE pixels */
	serial_console = sbi_console_get_device();
	console_active = !splash;
	column = row = 0;
	if (!splash)
		sbi_console_set_device(&fb_console);
	sbi_printf("[HZ-B500 FB] %s, ARGB8888 at 0x%lx\n",
		   splash ? "SD splash; Linux display adoption" : "early text; Linux cold takeover", FB_BASE);
	return 0;
}
