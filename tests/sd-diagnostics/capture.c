/* Exercise actual capture functions against bounded mock MMIO regions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define __iomem
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define PAGE_SIZE 4096U
#define MMC_ALL_SEND_CID 2
#define MMC_READ_MULTIPLE_BLOCK 18
#define SDXC_INTERRUPT_ERROR_BIT ((1U << 8) | (1U << 9))
#define SDXC_IDMAC_FATAL_BUS_ERROR (1U << 2)
#define SDXC_IDMAC_ABNORMAL_INTERRUPT_SUM (1U << 9)
#include "registers.inc"
#define mmc_readl(host, reg) readl((host)->reg_base + SDXC_##reg)

struct device { int unused; };
struct work_struct { int unused; };
struct mmc_command { u32 opcode, arg, flags; };
struct mmc_data { u32 blocks, blksz, sg_len, timeout_ns, timeout_clks; };
struct mmc_request { struct mmc_command *cmd; struct mmc_data *data; };
struct mmc_host {
	struct { u32 clock, bus_width, timing; } ios;
	u32 actual_clock;
};
struct sunxi_mmc_diag;
struct sunxi_mmc_host {
	struct sunxi_mmc_diag *diag;
	struct mmc_host *mmc;
	struct mmc_request *mrq;
	void *reg_base, *sg_cpu;
	u64 sg_dma;
	u32 req_page_count, des_addr_shift, int_sum, retry_cnt;
	bool wait_dma;
};

static u32 controller[0x804 / 4], pio[0x354 / 4], ccu[0x850 / 4];
static u32 descriptors[PAGE_SIZE / 4];
static unsigned int reads, queued;

static u64 ktime_get_ns(void) { return 123456789; }

static u32 readl(const void *address)
{
	uintptr_t p = (uintptr_t)address;
	uintptr_t base = (uintptr_t)controller;
	size_t off;

	reads++;
	if (p >= base && p < base + sizeof(controller)) {
		off = p - base;
		assert(off % 4 == 0);
		assert(off <= 0x4c || (off >= 0x80 && off <= 0x94) ||
		       off == 0x5c || off == 0x100 || off == 0x140 ||
		       off == 0x144 || off == 0x148 || off == 0x800);
		return controller[off / 4];
	}
	base = (uintptr_t)pio;
	if (p >= base && p < base + sizeof(pio)) {
		off = p - base;
		assert(off == 0xf0 || off == 0x100 || off == 0x104 ||
		       off == 0x114 || off == 0x340 || off == 0x344 || off == 0x350);
		return pio[off / 4];
	}
	base = (uintptr_t)ccu;
	assert(p >= base && p < base + sizeof(ccu));
	off = p - base;
	assert(off == 0x20 || off == 0x830 || off == 0x84c);
	return ccu[off / 4];
}

static void schedule_work(struct work_struct *work)
{
	(void)work;
	/* The mocked IRQ status must still be present when the capture is queued. */
	assert(controller[SDXC_REG_RINTR / 4] & SDXC_INTERRUPT_ERROR_BIT);
	queued++;
}

#include "capture.inc"

int main(void)
{
	struct sunxi_mmc_diag diag = { .pio = pio, .ccu = ccu };
	struct mmc_host mmc = { .ios = { 15000000, 2, 0 } };
	struct mmc_command cmd = { .opcode = 18, .arg = 95720 };
	struct mmc_data data = { .blocks = 128, .blksz = 512, .sg_len = 16 };
	struct mmc_request mrq = { .cmd = &cmd, .data = &data };
	struct sunxi_mmc_host host = {
		.diag = &diag, .mmc = &mmc, .mrq = &mrq,
		.reg_base = controller, .sg_cpu = descriptors,
		.sg_dma = 0x41000000, .req_page_count = 1,
		.des_addr_shift = 2, .wait_dma = true,
	};
	u32 copy[ARRAY_SIZE(controller)];
	unsigned int saved_reads;

	controller[SDXC_REG_CHDA / 4] = (host.sg_dma + 16) >> 2;
	descriptors[0] = 0x8000001a;
	descriptors[4] = 0x80000012;
	pio[0xf0 / 4] = 0x00222222;
	pio[0x104 / 4] = 0x00222222;
	pio[0x114 / 4] = 0x00000555;
	ccu[0x830 / 4] = 0x81000013;
	memcpy(copy, controller, sizeof(copy));
	sunxi_mmc_diag_start(&host, &cmd, 0x80000352);
	assert(diag.started && queued == 0);
	assert(diag.baseline.clock == 15000000 && diag.baseline.blocks == 128);
	assert(diag.baseline.pf[0] == 0x00222222);
	assert(diag.baseline.current_desc_valid);
	assert(diag.baseline.current_desc[0] == 0x80000012);
	assert(memcmp(copy, controller, sizeof(copy)) == 0);
	saved_reads = reads;
	sunxi_mmc_diag_start(&host, &cmd, 0x80000352);
	assert(reads == saved_reads);
	sunxi_mmc_diag_error(&host, 0, 0);
	assert(reads == saved_reads && queued == 0);

	controller[SDXC_REG_RINTR / 4] = 1U << 9;
	controller[SDXC_REG_CBCR / 4] = 4096;
	pio[0xf0 / 4] = 0x00222522; /* Simulate a pin being remuxed. */
	memcpy(copy, controller, sizeof(copy));
	sunxi_mmc_diag_error(&host, 1U << 9, 4);
	assert(diag.captured && queued == 1);
	assert(diag.failure.ctrl[SDXC_REG_MISTA / 4] == (1U << 9));
	assert(diag.failure.dma[2] == 4);
	assert(diag.failure.ctrl[SDXC_REG_CBCR / 4] == 4096);
	assert(diag.failure.pf[0] == 0x00222522);
	assert(diag.baseline.pf[0] == 0x00222222);
	assert(memcmp(copy, controller, sizeof(copy)) == 0);
	saved_reads = reads;
	sunxi_mmc_diag_error(&host, 1U << 9, 4);
	assert(reads == saved_reads && queued == 1);

	memset(&diag.failure, 0, sizeof(diag.failure));
	diag.captured = false;
	controller[SDXC_REG_CHDA / 4] = (host.sg_dma + PAGE_SIZE) >> 2;
	sunxi_mmc_diag_error(&host, 1U << 9, 0);
	assert(!diag.failure.current_desc_valid && queued == 2);

	host.diag = NULL;
	saved_reads = reads;
	sunxi_mmc_diag_start(&host, &cmd, 0);
	sunxi_mmc_diag_error(&host, 1U << 9, 0);
	assert(reads == saved_reads && queued == 2);

	/* Initialization commands have no data and no descriptor allocation. */
	memset(&diag, 0, sizeof(diag));
	diag.pio = pio;
	diag.ccu = ccu;
	host.diag = &diag;
	host.sg_cpu = NULL;
	mrq.data = NULL;
	mmc.ios.clock = 400000;
	cmd.opcode = 8;
	cmd.flags = 0x15;
	cmd.arg = 0x1aa;
	controller[SDXC_REG_RINTR / 4] = 1U << 8;
	/* Expected pre-CID errors are traced, but do not freeze the capture. */
	sunxi_mmc_diag_start(&host, &cmd, 0x80000148);
	sunxi_mmc_diag_error(&host, 1U << 8, 0);
	assert(!diag.init_captured && queued == 2 && diag.init_count == 2);
	assert(diag.init_trace[0].start && diag.init_trace[0].arg == 0x1aa);
	assert(!diag.init_trace[1].start && diag.init_trace[1].mista == (1U << 8));
	/* Ring wrapping retains the last 64 events, without printing or work. */
	for (unsigned int i = 0; i < 70; i++) {
		cmd.arg = i;
		sunxi_mmc_diag_start(&host, &cmd, 0x80000148);
	}
	assert(diag.init_count == 64 && diag.init_head == 8 && queued == 2);
	assert(diag.init_trace[diag.init_head].arg == 6);
	cmd.opcode = 2;
	cmd.arg = 0;
	cmd.flags = 0x67;
	sunxi_mmc_diag_start(&host, &cmd, 0x800001c2);
	assert(diag.init_started && !diag.init_baseline.has_data);
	assert(diag.init_baseline.opcode == 2 && diag.init_baseline.flags == 0x67);
	assert(diag.init_baseline.cmd_val == 0x800001c2);
	controller[SDXC_REG_RESP0 / 4] = 0xc0ffee;
	controller[SDXC_REG_CMDR / 4] = 0x1c2;
	memcpy(copy, controller, sizeof(copy));
	sunxi_mmc_diag_error(&host, 1U << 8, 0);
	assert(diag.init_captured && queued == 3 && !diag.captured);
	assert(!diag.init_failure.has_data && !diag.init_failure.current_desc_valid);
	assert(diag.init_failure.ctrl[SDXC_REG_MISTA / 4] == (1U << 8));
	assert(diag.init_trace[(diag.init_head + 63) % 64].resp[0] == 0xc0ffee);
	assert(memcmp(copy, controller, sizeof(copy)) == 0);
	saved_reads = reads;
	sunxi_mmc_diag_error(&host, 1U << 8, 0);
	sunxi_mmc_diag_start(&host, &cmd, 0x800001c2);
	assert(reads == saved_reads && queued == 3);
	assert(!diag.started && !diag.captured);
	puts("PASS: baseline/error capture is bounded, read-only, preserves IRQ status, avoids FIFO, and bounds descriptor access");
	puts("PASS: no-data CMD2 capture, initialization ring wrap, response trace and one-time freeze");
	return 0;
}
