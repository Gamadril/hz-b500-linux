/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Samuel Holland <samuel@sholland.org>
 */

#include <platform_override.h>
#include <thead/c9xx_encoding.h>
#include <thead/c9xx_pmu.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_scratch.h>
#include <libfdt.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/irqchip/plic.h>
#include "hz-b500-lcd.h"

#define SUN20I_D1_CCU_BASE		((void *)0x02001000)
#define SUN20I_D1_RISCV_CFG_BASE	((void *)0x06010000)
#define SUN20I_D1_PPU_BASE		((void *)0x07001000)
#define SUN20I_D1_PRCM_BASE		((void *)0x07010000)

/*
 * CCU
 */

#define CCU_BGR_ENABLE			(BIT(16) | BIT(0))

#define RISCV_CFG_BGR_REG		0xd0c
#define PPU_BGR_REG			0x1ac

static unsigned long csr_mxstatus;
static unsigned long csr_mhcr;
static unsigned long csr_mhint;

static void sun20i_d1_csr_save(void)
{
	/* Save custom CSRs. */
	csr_mxstatus	= csr_read(THEAD_C9XX_CSR_MXSTATUS);
	csr_mhcr	= csr_read(THEAD_C9XX_CSR_MHCR);
	csr_mhint	= csr_read(THEAD_C9XX_CSR_MHINT);

	/* Flush and disable caches. */
	csr_write(THEAD_C9XX_CSR_MCOR, 0x22);
	csr_write(THEAD_C9XX_CSR_MHCR, 0x0);
}

static void sun20i_d1_csr_restore(void)
{
	/* Invalidate caches and the branch predictor. */
	csr_write(THEAD_C9XX_CSR_MCOR, 0x70013);

	/* Restore custom CSRs, including the cache state. */
	csr_write(THEAD_C9XX_CSR_MXSTATUS, csr_mxstatus);
	csr_write(THEAD_C9XX_CSR_MHCR, csr_mhcr);
	csr_write(THEAD_C9XX_CSR_MHINT, csr_mhint);
}

/*
 * PPU
 */

#define PPU_PD_ACTIVE_CTRL		0x2c

static void sun20i_d1_ppu_save(void)
{
	/* Enable MMIO access. Do not assume S-mode leaves the clock enabled. */
	writel_relaxed(CCU_BGR_ENABLE, SUN20I_D1_PRCM_BASE + PPU_BGR_REG);

	/* Activate automatic power-down during the next WFI. */
	writel_relaxed(1, SUN20I_D1_PPU_BASE + PPU_PD_ACTIVE_CTRL);
}

static void sun20i_d1_ppu_restore(void)
{
	/* Disable automatic power-down. */
	writel_relaxed(0, SUN20I_D1_PPU_BASE + PPU_PD_ACTIVE_CTRL);
}

/*
 * RISCV_CFG
 */

#define RESET_ENTRY_LO_REG		0x0004
#define RESET_ENTRY_HI_REG		0x0008
#define WAKEUP_EN_REG			0x0020
#define WAKEUP_MASK_REG(i)		(0x0024 + 4 * (i))

static void sun20i_d1_riscv_cfg_save(void)
{
	struct plic_data *plic = plic_get();
	u32 *plic_sie = plic->pm_data;

	/* Enable MMIO access. Do not assume S-mode leaves the clock enabled. */
	writel_relaxed(CCU_BGR_ENABLE, SUN20I_D1_CCU_BASE + RISCV_CFG_BGR_REG);

	/*
	 * Copy the SIE bits to the wakeup registers. D1 has 160 "real"
	 * interrupt sources, numbered 16-175. These are the ones that map to
	 * the wakeup mask registers (the offset is for GIC compatibility). So
	 * copying SIE to the wakeup mask needs some bit manipulation.
	 */
	for (int i = 0; i < PLIC_IE_WORDS(plic) - 1; i++)
		writel_relaxed(plic_sie[i] >> 16 | plic_sie[i + 1] << 16,
			       SUN20I_D1_RISCV_CFG_BASE + WAKEUP_MASK_REG(i));

	/* Enable PPU wakeup for interrupts. */
	writel_relaxed(1, SUN20I_D1_RISCV_CFG_BASE + WAKEUP_EN_REG);
}

static void sun20i_d1_riscv_cfg_restore(void)
{
	/* Disable PPU wakeup for interrupts. */
	writel_relaxed(0, SUN20I_D1_RISCV_CFG_BASE + WAKEUP_EN_REG);
}

static void sun20i_d1_riscv_cfg_init(void)
{
	u64 entry = sbi_scratch_thishart_ptr()->warmboot_addr;

	/* Enable MMIO access. */
	writel_relaxed(CCU_BGR_ENABLE, SUN20I_D1_CCU_BASE + RISCV_CFG_BGR_REG);

	/* Program the reset entry address. */
	writel_relaxed(entry, SUN20I_D1_RISCV_CFG_BASE + RESET_ENTRY_LO_REG);
	writel_relaxed(entry >> 32, SUN20I_D1_RISCV_CFG_BASE + RESET_ENTRY_HI_REG);
}

static int sun20i_d1_hart_suspend(u32 suspend_type, ulong mmode_resume_addr)
{
	/* Use the generic code for retentive suspend. */
	if (!(suspend_type & SBI_HSM_SUSP_NON_RET_BIT))
		return SBI_ENOTSUPP;

	plic_suspend();
	sun20i_d1_ppu_save();
	sun20i_d1_riscv_cfg_save();
	sun20i_d1_csr_save();

	/*
	 * If no interrupt is pending, this will power down the CPU power
	 * domain. Otherwise, this will fall through, and the generic HSM
	 * code will jump to the resume address.
	 */
	wfi();

	return 0;
}

static void sun20i_d1_hart_resume(void)
{
	sun20i_d1_csr_restore();
	sun20i_d1_riscv_cfg_restore();
	sun20i_d1_ppu_restore();
	plic_resume();
}

static const struct sbi_hsm_device sun20i_d1_ppu = {
	.name		= "sun20i-d1-ppu",
	.hart_suspend	= sun20i_d1_hart_suspend,
	.hart_resume	= sun20i_d1_hart_resume,
};

static const struct sbi_cpu_idle_state sun20i_d1_cpu_idle_states[] = {
	{
		.name			= "cpu-nonretentive",
		.suspend_param		= SBI_HSM_SUSPEND_NON_RET_DEFAULT,
		.local_timer_stop	= true,
		.entry_latency_us	= 40,
		.exit_latency_us	= 67,
		.min_residency_us	= 1100,
		.wakeup_latency_us	= 67,
	},
	{ }
};

#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/timer/aclint_mtimer.h>
#include <sbi_utils/ipi/aclint_mswi.h>

static struct aclint_mswi_data sun20i_d1_mswi = {
	.addr		= 0x14000000,
	.size		= ACLINT_MSWI_SIZE,
	.first_hartid	= 0,
	.hart_count	= 1,
};

static struct aclint_mtimer_data sun20i_d1_mtimer = {
	.mtime_freq	= 24000000,
	/* C900 CLINT has no MMIO MTIME; OpenSBI reads the time CSR. */
	.mtime_addr	= 0,
	.mtime_size	= 0,
	.mtimecmp_addr	= 0x14000000 + CLINT_MTIMER_OFFSET + ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size	= ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid	= 0,
	.hart_count	= 1,
	/* Match the thead,c900-clint quirks in fdt_timer_mtimer.c. */
	.has_64bit_mmio	= false,
	.has_shared_mtime = true,
};

static int sun20i_d1_early_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		/* Always initialize UART0 (115200 baud, 24MHz clock, reg_shift 2, reg_width 4) */
		uart8250_init(0x02500000, 24000000, 115200, 2, 4, 0, 0);

		/* Fallback CLINT/MTIMER setup if not in DTB */
		rc = aclint_mswi_cold_init(&sun20i_d1_mswi);
		if (rc && rc != SBI_EALREADY)
			return rc;

		rc = aclint_mtimer_cold_init(&sun20i_d1_mtimer, NULL);
		if (rc && rc != SBI_EALREADY)
			return rc;
	}

	rc = generic_early_init(cold_boot);
	if (rc && sbi_console_get_device())
		return 0;

	return rc;
}

static int sun20i_d1_final_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		void *fdt = fdt_get_address_rw();

		/* Delegate PLIC to S-mode (T-HEAD C906 custom PLIC control) */
		writel_relaxed(1, (void *)0x101ffffc);

		sun20i_d1_riscv_cfg_init();
		sbi_hsm_set_device(&sun20i_d1_ppu);

		rc = fdt_add_cpu_idle_states(fdt, sun20i_d1_cpu_idle_states);
		if (rc)
			return rc;

		hz_b500_lcd_init(fdt);
	}

	return generic_final_init(cold_boot);
}

static int sun20i_d1_extensions_init(struct sbi_hart_features *hfeatures)
{
	int rc;

	rc = generic_extensions_init(hfeatures);
	if (rc)
		return rc;

	thead_c9xx_register_pmu_device();

	/* auto-detection doesn't work on t-head c9xx cores */
	/* D1 has 29 mhpmevent csrs, but only 3-9,13-17 have valid value */
	hfeatures->mhpm_mask = 0x0003e3f8;
	hfeatures->mhpm_bits = 64;

	return 0;
}

static int sun20i_d1_platform_init(const void *fdt, int nodeoff,
				   const struct fdt_match *match)
{
	generic_platform_ops.early_init = sun20i_d1_early_init;
	generic_platform_ops.final_init = sun20i_d1_final_init;
	generic_platform_ops.extensions_init = sun20i_d1_extensions_init;

	return 0;
}

static const struct fdt_match sun20i_d1_match[] = {
	{ .compatible = "allwinner,sun20i-d1" },
	{ .compatible = "allwinner,d1" },
	{ .compatible = "arm,sun20iw1p1" },
	{ .compatible = "allwinner,sun20iw1p1" },
	{ },
};

const struct fdt_driver sun20i_d1 = {
	.match_table	= sun20i_d1_match,
	.init		= sun20i_d1_platform_init,
};
