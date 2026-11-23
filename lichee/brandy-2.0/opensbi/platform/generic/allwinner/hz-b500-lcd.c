/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * HZ-B500 RGB18 LCD diagnostic output before Linux starts.
 * Register layout and programming order follow this BSP's sun8iw20 CCU,
 * pwm-sunxi-group (v200), and disp2 lowlevel_v2x TCON drivers.
 * DE framebuffer SBI console, or explicitly requested diagnostic color bars.
 */

#include <libfdt.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_timer.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include "hz-b500-lcd.h"
#include "hz-b500-fb.h"

#define CCU_BASE		0x02001000UL
#define PIO_BASE		0x02000000UL
#define PWM_BASE		0x02000c00UL
#define TCON_TOP_BASE	0x05460000UL
#define TCON_BASE	0x05461000UL

#define PLL_VIDEO0	(CCU_BASE + 0x040)
#define PWM_BGR		(CCU_BASE + 0x7ac)
#define DPSS_BGR		(CCU_BASE + 0xabc)
#define TCON_CLK		(CCU_BASE + 0xb60)
#define TCON_BGR		(CCU_BASE + 0xb7c)
#define BGR_ENABLE	((1U << 16) | 1U)
#define PLL_ENABLE	((1U << 31) | (1U << 30) | (1U << 29))
#define PLL_LOCK		(1U << 28)
#define PLL_GATE		(1U << 27)

struct lcd_config {
	u32 x, y, ht, hbp, hspw, vt, vbp, vspw;
	u32 mhz, polarity, brightness, rb_swap, phase, sync;
};

static u32 reg_read(unsigned long addr)
{
	return readl((void *)addr);
}

static void reg_write(unsigned long addr, u32 value)
{
	writel(value, (void *)addr);
}

static void reg_update(unsigned long addr, u32 mask, u32 value)
{
	reg_write(addr, (reg_read(addr) & ~mask) | value);
}

static u32 dt_u32(const void *fdt, int node, const char *name, u32 fallback)
{
	int len;
	const fdt32_t *prop = fdt_getprop(fdt, node, name, &len);

	return prop && len == sizeof(*prop) ? fdt32_to_cpu(*prop) : fallback;
}

static int lcd_config_read(const void *fdt, int node, struct lcd_config *c)
{
	int len;
	const fdt32_t *bl = fdt_getprop(fdt, node, "lcd_bl_en", &len);

	/* The opt-in supports only this board's RGB18 + PWM7 + PG15 wiring. */
	if (!bl || len != 4 * sizeof(*bl) ||
	    fdt32_to_cpu(bl[1]) != 6 || fdt32_to_cpu(bl[2]) != 15 ||
	    fdt32_to_cpu(bl[3]) != 0 ||
	    dt_u32(fdt, node, "lcd_used", 0) != 1 ||
	    dt_u32(fdt, node, "lcd_if", ~0U) != 0 ||
	    dt_u32(fdt, node, "lcd_hv_if", 0) != 0 ||
	    dt_u32(fdt, node, "lcd_pwm_used", 0) != 1 ||
	    dt_u32(fdt, node, "lcd_pwm_ch", ~0U) != 7 ||
	    dt_u32(fdt, node, "lcd_pwm_freq", 0) != 50000 ||
	    dt_u32(fdt, node, "lcd_frm", 0) != 1)
		return SBI_ENOTSUPP;

	c->x = dt_u32(fdt, node, "lcd_x", 0);
	c->y = dt_u32(fdt, node, "lcd_y", 0);
	c->ht = dt_u32(fdt, node, "lcd_ht", 0);
	c->hbp = dt_u32(fdt, node, "lcd_hbp", 0);
	c->hspw = dt_u32(fdt, node, "lcd_hspw", 0);
	c->vt = dt_u32(fdt, node, "lcd_vt", 0);
	c->vbp = dt_u32(fdt, node, "lcd_vbp", 0);
	c->vspw = dt_u32(fdt, node, "lcd_vspw", 0);
	c->mhz = dt_u32(fdt, node, "lcd_dclk_freq", 0);
	c->polarity = dt_u32(fdt, node, "lcd_pwm_pol", 0);
	c->brightness = dt_u32(fdt, node, "lcd_backlight", 197);
	c->rb_swap = dt_u32(fdt, node, "lcd_rb_swap", 0);
	c->phase = dt_u32(fdt, node, "lcd_hv_clk_phase", 0);
	c->sync = dt_u32(fdt, node, "lcd_hv_sync_polarity", 0);

	if (!c->x || c->x > 4096 || !c->y || c->y > 4096 ||
	    c->ht > 8192 || c->ht <= c->x + c->hbp ||
	    c->vt > 4095 || c->vt <= c->y + c->vbp ||
	    c->hbp > 4096 || c->vbp > 4096 ||
	    c->hspw > c->hbp || c->vspw > c->vbp ||
	    c->hspw > 1024 || c->vspw > 1024 ||
	    c->mhz < 12 || c->mhz > 100 || c->polarity > 1 ||
	    c->brightness > 255 || c->rb_swap > 1 || c->phase > 3 ||
	    c->sync > 3)
		return SBI_EINVAL;

	return 0;
}

static int lcd_clock_start(u32 mhz)
{
	u32 saved = reg_read(PLL_VIDEO0);
	unsigned int i;

	/* 24 MHz * N / 4 = pixel clock * 6; TCON uses its /6 divider. */
	reg_write(PLL_VIDEO0, PLL_ENABLE | ((mhz - 1) << 8));
	for (i = 0; i < 1000; i++) {
		if (reg_read(PLL_VIDEO0) & PLL_LOCK)
			break;
		sbi_timer_udelay(10);
	}
	if (i == 1000) {
		reg_write(PLL_VIDEO0, saved);
		return SBI_ETIMEDOUT;
	}
	reg_update(PLL_VIDEO0, PLL_GATE, PLL_GATE);
	/* PLL_VIDEO0 (mux 0), M=1, N=1, gate on: 348 MHz for 58 MHz. */
	reg_write(TCON_CLK, 1U << 31);
	reg_update(DPSS_BGR, BGR_ENABLE, BGR_ENABLE);
	reg_update(TCON_BGR, BGR_ENABLE, BGR_ENABLE);
	reg_update(PWM_BGR, BGR_ENABLE, BGR_ENABLE);
	sbi_timer_udelay(10);
	return 0;
}

static void lcd_start(const struct lcd_config *c, bool pattern)
{
	u32 delay = c->vt - c->y;
	u32 phase = ((c->phase & 1) ? 2U << 28 : 0) |
		    ((c->phase & 2) ? 1U << 26 : 0);
	unsigned int i;
	static const u32 seeds[] = { 1, 3, 5, 7, 11, 13 };
	static const u32 tables[] = {
		0x01010000, 0x15151111, 0x57575555, 0x7f7f7777,
	};

	delay = delay < 18 ? 10 : delay - 8;
	if (delay > 31)
		delay = 31;

	/* Select LCD0 for PD output; preserve the other TCON routes. */
	reg_update(TCON_TOP_BASE, (1U << 8) | (1U << 15), 0);
	reg_write(TCON_BASE + 0x000, 0);
	reg_write(TCON_BASE + 0x004, 0); /* no interrupts before Linux */
	reg_write(TCON_BASE + 0x08c, 0x0fffffff); /* tristate while configuring */
	reg_write(TCON_BASE + 0x040,
		  (c->rb_swap << 23) | (delay << 4) | (pattern ? 1U : 0U));
	reg_write(TCON_BASE + 0x044, 6);
	reg_write(TCON_BASE + 0x048, ((c->x - 1) << 16) | (c->y - 1));
	reg_write(TCON_BASE + 0x04c,
		  ((c->ht - 1) << 16) | (c->hbp ? c->hbp - 1 : 0));
	reg_write(TCON_BASE + 0x050,
		  ((c->vt * 2) << 16) | (c->vbp ? c->vbp - 1 : 0));
	reg_write(TCON_BASE + 0x054,
		  ((c->hspw ? c->hspw - 1 : 0) << 16) |
		  (c->vspw ? c->vspw - 1 : 0));
	reg_write(TCON_BASE + 0x058, 0); /* parallel RGB */
	reg_write(TCON_BASE + 0x088, phase | (c->sync << 24));
	reg_write(TCON_BASE + 0x084, 0); /* LVDS off */
	reg_write(TCON_BASE + 0x100, 0); /* color enhancement off */
	reg_write(TCON_BASE + 0x180, 0); /* color mapping off */
	reg_write(TCON_BASE + 0x1f0, ((c->mhz * 15) << 16) | 3);
	for (i = 0; i < 6; i++)
		reg_write(TCON_BASE + 0x014 + i * 4, seeds[i]);
	for (i = 0; i < 4; i++)
		reg_write(TCON_BASE + 0x02c + i * 4, tables[i]);
	reg_write(TCON_BASE + 0x010, 1U << 31); /* RGB666 dither */

	/* PD0..21 = LCD0 mux 2; preserve PD22 and all unrelated pins. */
	reg_write(PIO_BASE + 0x090, 0x22222222);
	reg_write(PIO_BASE + 0x094, 0x22222222);
	reg_update(PIO_BASE + 0x098, 0x00ffffff, 0x00222222);
	/* Four drive-strength bits per pin on D1s: level 2 = BSP's 30 mA. */
	reg_write(PIO_BASE + 0x0a4, 0x22222222);
	reg_write(PIO_BASE + 0x0a8, 0x22222222);
	reg_update(PIO_BASE + 0x0ac, 0x00ffffff, 0x00222222);
	reg_update(PIO_BASE + 0x0b4, 0xffffffff, 0);
	reg_update(PIO_BASE + 0x0b8, 0x00000fff, 0);
	reg_write(TCON_BASE + 0x000, (1U << 31) | (1U << 1));
	reg_write(TCON_BASE + 0x08c, 0);
	reg_write(TCON_BASE + 0x044, (0xfU << 28) | 6);
	reg_update(TCON_BASE + 0x040, 1U << 31, 1U << 31);
}

static void lcd_backlight_start(const struct lcd_config *c)
{
	/* 24 MHz / 480 = 50 kHz; same polarity encoding as Linux's PWM driver. */
	reg_update(PWM_BASE + 0x080, 1U << 7, 0);
	reg_update(PWM_BASE + 0x02c, (3U << 7) | 0xf, 0);
	reg_update(PWM_BASE + 0x040, 1U << 7, 1U << 7);
	reg_write(PWM_BASE + 0x1e0, c->polarity ? 0 : 1U << 8);
	reg_write(PWM_BASE + 0x1e4,
		  (479U << 16) | (480 * c->brightness / 255));
	reg_update(PIO_BASE + 0x098, 0xfU << 24, 5U << 24); /* PD22 PWM7 */
	reg_update(PIO_BASE + 0x0ac, 0xfU << 24, 0); /* 10 mA */
	reg_update(PIO_BASE + 0x0b8, 3U << 12, 1U << 12); /* pull up */
	reg_update(PWM_BASE + 0x080, 1U << 7, 1U << 7);
	/* PG15: set the output latch before selecting GPIO output. */
	reg_update(PIO_BASE + 0x130, 1U << 15, 1U << 15);
	reg_update(PIO_BASE + 0x138, 0xfU << 28, 0);
	reg_update(PIO_BASE + 0x144, 3U << 30, 0);
	reg_update(PIO_BASE + 0x124, 0xfU << 28, 1U << 28);
}

void hz_b500_lcd_init(const void *fdt)
{
	struct lcd_config config;
	int node, rc;
	u32 line;
	bool framebuffer;

	node = fdt_node_offset_by_compatible(fdt, -1, "allwinner,sunxi-lcd0");
	if (node < 0)
		return;
	framebuffer = !!fdt_getprop(fdt, node, "allwinner,opensbi-fb-console", NULL);
	framebuffer |= !!fdt_getprop(fdt, node, "allwinner,opensbi-splash", NULL);
	if (!framebuffer &&
	    !fdt_getprop(fdt, node, "allwinner,opensbi-lcd-test-pattern", NULL))
		return;
	if (!fdt_node_is_enabled(fdt, node))
		return;
	rc = lcd_config_read(fdt, node, &config);
	if (rc) {
		sbi_printf("[HZ-B500 LCD] unsupported panel configuration (%d)\n", rc);
		return;
	}
	rc = lcd_clock_start(config.mhz);
	if (rc) {
		sbi_printf("[HZ-B500 LCD] video PLL lock timeout; continuing boot\n");
		return;
	}
	/* Hold PG15 low while the scanout source and framebuffer are prepared. */
	reg_update(PIO_BASE + 0x130, 1U << 15, 0);
	reg_update(PIO_BASE + 0x124, 0xfU << 28, 1U << 28);
	lcd_start(&config, !framebuffer);
	if (framebuffer) {
		rc = hz_b500_fb_init((void *)fdt, config.x, config.y);
		if (rc) {
			sbi_printf("[HZ-B500 FB] skipped (%d); backlight off until Linux\n", rc);
			return;
		}
	}
	/* Let the panel see several complete frames before enabling the LEDs. */
	sbi_timer_mdelay(50);
	line = (reg_read(TCON_BASE + 0x0fc) >> 16) & 0xfff;
	lcd_backlight_start(&config);
	sbi_printf("[HZ-B500 LCD] %ux%u @ %u MHz, RGB18 %s, PWM7/PG15, line=%u\n",
		   config.x, config.y, config.mhz,
		   framebuffer ? "framebuffer" : "color bars", line);
}
