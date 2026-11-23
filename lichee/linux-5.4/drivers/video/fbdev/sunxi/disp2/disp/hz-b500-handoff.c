/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_B500_HANDOFF_TEST
#include "dev_disp.h"
#include "hz-b500-handoff.h"
#include <linux/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/of_address.h>
#endif

#define BOOT_FB 0x43c00000UL
#define BOOT_SIZE 0x300000UL
static bool adopting;
static u32 pwm_duty;

bool hz_b500_adopting_display(void) { return adopting; }
u32 hz_b500_handoff_pwm_duty(void) { return pwm_duty; }
void hz_b500_handoff_complete(void)
{
	if (adopting)
		pr_info("OpenSBI splash: native display active\n");
	adopting = false;
}

/* Opt in only to the exact firmware layout we know how to adopt. Keeping an
 * identity mapping until domain destruction makes the old scanout address valid
 * before, during and after the first frame-boundary switch to Linux's buffer.
 * Reserve its IOVA as well as its physical RAM: other DMA clients share this
 * domain and must never allocate this address. */
bool hz_b500_prepare_handoff(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *reserved, *lcd;
	struct resource res;
	struct iommu_domain *domain;
	void __iomem *ccu;
	void __iomem *pio;
	void __iomem *de = (void __iomem *)g_disp_drv.reg_base[DISP_MOD_DE];
	void __iomem *tcon = (void __iomem *)g_disp_drv.reg_base[DISP_MOD_LCD0];
	const char *fb;
	u32 x = 0, y = 0, ht = 0, vt = 0, hbp = 0, vbp = 0, clock = 0, boot = 0, pwm;
	bool valid;
	int ret;
	adopting = false;
	if (!of_property_read_bool(dev->of_node, "allwinner,opensbi-splash-active"))
		return false;
	if (of_property_read_u32(dev->of_node, "boot_disp", &boot) || boot != 0x100)
		return false;
	if (of_property_read_string(dev->of_node, "boot_fb0", &fb) ||
	    strcmp(fb, "43c00000,400,258,20,1000,0,0,0,0"))
		return false;
	reserved = of_find_node_by_path("/reserved-memory/framebuffer@43c00000");
	ret = reserved ? of_address_to_resource(reserved, 0, &res) : -EINVAL;
	of_node_put(reserved);
	if (ret || res.start != BOOT_FB || resource_size(&res) != BOOT_SIZE)
		return false;
	lcd = of_find_compatible_node(NULL, NULL, "allwinner,sunxi-lcd0");
	if (!lcd) return false;
	of_property_read_u32(lcd, "lcd_x", &x);
	of_property_read_u32(lcd, "lcd_y", &y);
	of_property_read_u32(lcd, "lcd_ht", &ht);
	of_property_read_u32(lcd, "lcd_vt", &vt);
	of_property_read_u32(lcd, "lcd_hbp", &hbp);
	of_property_read_u32(lcd, "lcd_vbp", &vbp);
	of_property_read_u32(lcd, "lcd_dclk_freq", &clock);
	of_node_put(lcd);
	if (x != 1024 || y != 600 || ht != 1400 || vt != 650 ||
	    hbp != 160 || vbp != 23 || clock != 58)
		return false;
	ccu = ioremap(0x02001000, 0x1000);
	if (!ccu) return false;
	pio = ioremap(0x02000000, 0x1000);
	if (!pio) { iounmap(ccu); return false; }
	pwm = readl(pio + 0xde4);
	valid = (readl(ccu + 0x600) & 0x8f00000f) == 0x81000003 &&
		(readl(ccu + 0x40) & 0xff00) == 0x3900 &&
		(readl(ccu + 0x60c) & 0x10001) == 0x10001 &&
		(readl(ccu + 0xb60) & 0x8f000000) == 0x80000000 &&
		(readl(tcon) & 0x80000000) &&
		(readl(tcon + 0x40) & 0x80000007) == 0x80000000 &&
		readl(tcon + 0x44) == 0xf0000006 &&
		readl(tcon + 0x48) == 0x03ff0257 &&
		readl(tcon + 0x4c) == 0x0577009f &&
		readl(tcon + 0x50) == 0x05140016 &&
		(readl(de + 0x100000) & 1) &&
		readl(de + 0x10000c) == 0x025703ff &&
		readl(de + 0x103000) == 0xff000003 &&
		readl(de + 0x103004) == 0x025703ff &&
		readl(de + 0x103008) == 0 &&
		readl(de + 0x10300c) == 4096 &&
		readl(de + 0x103010) == BOOT_FB &&
		readl(de + 0x103080) == 0 &&
		readl(de + 0x103088) == 0x025703ff &&
		readl(de + 0x101000) == 0x101 &&
		readl(de + 0x101080) == 1 &&
		readl(pio + 0x90) == 0x22222222 &&
		readl(pio + 0x94) == 0x22222222 &&
		(readl(pio + 0x98) & 0x0fffffff) == 0x05222222 &&
		(readl(pio + 0x124) >> 28) == 1 &&
		(readl(pio + 0x130) & BIT(15)) &&
		(readl(pio + 0xc80) & BIT(7)) &&
		readl(pio + 0xde0) == BIT(8) &&
		(pwm >> 16) == 479 && (pwm & 0xffff) <= 480;
	pwm_duty = (pwm & 0xffff) * 20000U / 480U;
	iounmap(pio);
	iounmap(ccu);
	if (!valid) {
		dev_warn(dev, "OpenSBI display state incompatible; cold takeover\n");
		return false;
	}
	domain = iommu_get_domain_for_dev(dev);
	if (!domain || iommu_dma_reserve_iova(dev, BOOT_FB, BOOT_SIZE))
		return false;
	ret = iommu_map(domain, BOOT_FB, BOOT_FB, BOOT_SIZE, IOMMU_READ);
	if (ret) {
		dev_warn(dev, "OpenSBI framebuffer identity map failed: %d\n", ret);
		return false;
	}
	adopting = true;
	dev_info(dev, "OpenSBI splash: adopting running display, identity mapping reserved\n");
	return true;
}
