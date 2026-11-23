// SPDX-License-Identifier: GPL-2.0
/* Opt-in D1s SD0 snapshots. No MMIO writes or FIFO reads. */
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "sunxi-mmc.h"
#include "sunxi-mmc-diag.h"

struct sunxi_mmc_diag_snapshot {
	u64 time_ns;
	u32 clock, actual_clock, width, timing;
	u32 opcode, flags;
	u32 arg, cmd_val, blocks, blksz, sg_len;
	u32 timeout_ns, timeout_clks, int_sum, wait_dma, retry;
	u32 ctrl[20]; /* 0x00..0x4c, excludes the FIFO at 0x200 */
	u32 dma[6];   /* 0x80..0x94 */
	u32 timing_regs[6];
	u32 pf[7];
	u32 ccu[3];
	u32 desc0[4], current_desc[4];
	bool has_data, current_desc_valid;
};

#define SUNXI_MMC_DIAG_INIT_EVENTS 64

struct sunxi_mmc_diag_cmd {
	u64 time_ns;
	u32 clock, opcode, arg, flags, cmd_val, mista, idst, resp[4];
	bool start;
};

struct sunxi_mmc_diag {
	struct device *dev;
	void __iomem *pio;
	void __iomem *ccu;
	struct work_struct work;
	struct work_struct init_work;
	bool started, captured;
	bool init_started, init_captured;
	u32 init_head, init_count;
	struct sunxi_mmc_diag_cmd init_trace[SUNXI_MMC_DIAG_INIT_EVENTS];
	struct sunxi_mmc_diag_snapshot init_baseline, init_failure;
	struct sunxi_mmc_diag_snapshot baseline, failure;
};

static const u16 timing_offsets[] = { 0x5c, 0x100, 0x140, 0x144, 0x148, 0x800 };
static const u16 pf_offsets[] = { 0xf0, 0x100, 0x104, 0x114, 0x340, 0x344, 0x350 };
static const u16 ccu_offsets[] = { 0x20, 0x830, 0x84c };

/* Caller holds host->lock. Read status before the IRQ handler acknowledges it. */
static void sunxi_mmc_diag_snapshot(struct sunxi_mmc_host *host,
				    struct mmc_command *cmd, u32 cmd_val,
				    struct sunxi_mmc_diag_snapshot *s)
{
	struct sunxi_mmc_diag *d = host->diag;
	struct mmc_data *data = host->mrq->data;
	u32 *desc = host->sg_cpu;
	u64 current_dma;
	unsigned int i;

	s->time_ns = ktime_get_ns();
	s->clock = host->mmc->ios.clock;
	s->actual_clock = host->mmc->actual_clock;
	s->width = host->mmc->ios.bus_width;
	s->timing = host->mmc->ios.timing;
	s->opcode = cmd->opcode;
	s->flags = cmd->flags;
	s->arg = cmd->arg;
	s->cmd_val = cmd_val;
	s->has_data = !!data;
	if (data) {
		s->blocks = data->blocks;
		s->blksz = data->blksz;
		s->sg_len = data->sg_len;
		s->timeout_ns = data->timeout_ns;
		s->timeout_clks = data->timeout_clks;
	}
	s->int_sum = host->int_sum;
	s->wait_dma = host->wait_dma;
	s->retry = host->retry_cnt;
	for (i = 0; i < ARRAY_SIZE(s->ctrl); i++)
		s->ctrl[i] = readl(host->reg_base + i * 4);
	for (i = 0; i < ARRAY_SIZE(s->dma); i++)
		s->dma[i] = readl(host->reg_base + 0x80 + i * 4);
	for (i = 0; i < ARRAY_SIZE(timing_offsets); i++)
		s->timing_regs[i] = readl(host->reg_base + timing_offsets[i]);
	for (i = 0; i < ARRAY_SIZE(pf_offsets); i++)
		s->pf[i] = readl(d->pio + pf_offsets[i]);
	for (i = 0; i < ARRAY_SIZE(ccu_offsets); i++)
		s->ccu[i] = readl(d->ccu + ccu_offsets[i]);

	if (!data || !desc)
		return;
	/* Coherent descriptor memory only; never dereference device buffer addresses. */
	for (i = 0; i < ARRAY_SIZE(s->desc0); i++)
		s->desc0[i] = READ_ONCE(desc[i]);
	current_dma = (u64)s->dma[4] << host->des_addr_shift;
	if (current_dma >= host->sg_dma &&
	    current_dma - host->sg_dma <= PAGE_SIZE * host->req_page_count - 16 &&
	    !((current_dma - host->sg_dma) & 15)) {
		desc = host->sg_cpu + (current_dma - host->sg_dma);
		for (i = 0; i < ARRAY_SIZE(s->current_desc); i++)
			s->current_desc[i] = READ_ONCE(desc[i]);
		s->current_desc_valid = true;
	}
}

/* Keep the most recent initialization events, without printing in IRQ context. */
static void sunxi_mmc_diag_trace(struct sunxi_mmc_host *host,
				struct mmc_command *cmd, u32 cmd_val,
				u32 mista, u32 idst, bool start)
{
	struct sunxi_mmc_diag *d = host->diag;
	struct sunxi_mmc_diag_cmd *event;
	unsigned int i;

	if (d->init_captured || host->mmc->ios.clock > 400000)
		return;
	event = &d->init_trace[d->init_head];
	event->time_ns = ktime_get_ns();
	event->clock = host->mmc->ios.clock;
	event->opcode = cmd->opcode;
	event->arg = cmd->arg;
	event->flags = cmd->flags;
	event->cmd_val = cmd_val;
	event->mista = mista;
	event->idst = idst;
	event->start = start;
	for (i = 0; i < ARRAY_SIZE(event->resp); i++)
		event->resp[i] = start ? 0 : readl(host->reg_base + SDXC_REG_RESP0 + i * 4);
	d->init_head = (d->init_head + 1) % SUNXI_MMC_DIAG_INIT_EVENTS;
	if (d->init_count < SUNXI_MMC_DIAG_INIT_EVENTS)
		d->init_count++;
}

static void sunxi_mmc_diag_print(struct device *dev, const char *label,
				 const struct sunxi_mmc_diag_snapshot *s)
{
	unsigned int i;

	dev_info(dev, "SD-DIAG %s t=%llu us clock=%u actual=%u width_enum=%u timing=%u cmd=%u flags=%08x arg=%08x launch=%08x blocks=%u blksz=%u sg=%u timeout=%u ns+%u clks int_sum=%08x wait_dma=%u retry=%u\n",
		 label, div_u64(s->time_ns, 1000), s->clock, s->actual_clock,
		 s->width, s->timing, s->opcode, s->flags, s->arg, s->cmd_val, s->blocks, s->blksz,
		 s->sg_len, s->timeout_ns, s->timeout_clks, s->int_sum,
		 s->wait_dma, s->retry);
	for (i = 0; i < ARRAY_SIZE(s->ctrl); i += 4)
		dev_info(dev, "SD-DIAG %s MMC+%03x: %08x %08x %08x %08x\n",
			 label, i * 4, s->ctrl[i], s->ctrl[i + 1],
			 s->ctrl[i + 2], s->ctrl[i + 3]);
	dev_info(dev, "SD-DIAG %s DMA+080: %08x %08x %08x %08x CHDA=%08x CBDA=%08x\n",
		 label, s->dma[0], s->dma[1], s->dma[2], s->dma[3],
		 s->dma[4], s->dma[5]);
	dev_info(dev, "SD-DIAG %s NTSR=%08x THLD=%08x DRV=%08x SAMP=%08x DS=%08x HS400=%08x\n",
		 label, s->timing_regs[0], s->timing_regs[1], s->timing_regs[2],
		 s->timing_regs[3], s->timing_regs[4], s->timing_regs[5]);
	dev_info(dev, "SD-DIAG %s PIO PF_CFG0=%08x PF_DAT=%08x PF_DRV0=%08x PF_PULL0=%08x POW_SEL=%08x POW_CTL=%08x PF_POWER=%08x\n",
		 label, s->pf[0], s->pf[1], s->pf[2], s->pf[3], s->pf[4],
		 s->pf[5], s->pf[6]);
	dev_info(dev, "SD-DIAG %s CCU PLL_PERIPH0=%08x MMC0_CLK=%08x MMC_BGR=%08x\n",
		 label, s->ccu[0], s->ccu[1], s->ccu[2]);
	if (!s->has_data)
		return;
	dev_info(dev, "SD-DIAG %s DESC0: %08x %08x %08x %08x\n",
		 label, s->desc0[0], s->desc0[1], s->desc0[2], s->desc0[3]);
	if (s->current_desc_valid)
		dev_info(dev, "SD-DIAG %s DESC_CURRENT: %08x %08x %08x %08x\n",
			 label, s->current_desc[0], s->current_desc[1],
			 s->current_desc[2], s->current_desc[3]);
}

static void sunxi_mmc_diag_work(struct work_struct *work)
{
	struct sunxi_mmc_diag *d = container_of(work, struct sunxi_mmc_diag, work);

	if (d->started)
		sunxi_mmc_diag_print(d->dev, "first-cmd18-before-start", &d->baseline);
	sunxi_mmc_diag_print(d->dev, "first-cmd18-error-before-clear", &d->failure);
}

static void sunxi_mmc_diag_init_work(struct work_struct *work)
{
	struct sunxi_mmc_diag *d = container_of(work, struct sunxi_mmc_diag, init_work);
	u32 first = (d->init_head + SUNXI_MMC_DIAG_INIT_EVENTS - d->init_count) %
		SUNXI_MMC_DIAG_INIT_EVENTS;
	unsigned int i;

	dev_info(d->dev, "SD-DIAG INIT-TRACE events=%u (S=start, I=IRQ; recorded before status clear)\n",
		 d->init_count);
	for (i = 0; i < d->init_count; i++) {
		const struct sunxi_mmc_diag_cmd *e =
			&d->init_trace[(first + i) % SUNXI_MMC_DIAG_INIT_EVENTS];

		dev_info(d->dev, "SD-DIAG INIT %c t=%llu us clock=%u cmd=%u arg=%08x flags=%08x reg=%08x mista=%08x idst=%08x resp=%08x,%08x,%08x,%08x\n",
			 e->start ? 'S' : 'I', div_u64(e->time_ns, 1000), e->clock,
			 e->opcode, e->arg, e->flags, e->cmd_val, e->mista, e->idst,
			 e->resp[0], e->resp[1], e->resp[2], e->resp[3]);
	}
	if (d->init_started)
		sunxi_mmc_diag_print(d->dev, "first-cmd2-before-start", &d->init_baseline);
	sunxi_mmc_diag_print(d->dev, "first-cmd2-error-before-clear", &d->init_failure);
}

static void sunxi_mmc_diag_cancel(void *data)
{
	struct sunxi_mmc_diag *d = data;

	cancel_work_sync(&d->work);
	cancel_work_sync(&d->init_work);
}

void sunxi_mmc_diag_init(struct sunxi_mmc_host *host,
			 struct platform_device *pdev)
{
	struct sunxi_mmc_diag *d;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!of_property_read_bool(pdev->dev.of_node, "allwinner,sd-diag-capture"))
		return;
	/* These pin/clock offsets describe D1s SD0 only. */
	if (!(IS_ENABLED(CONFIG_ARCH_SUN20IW1P1) || IS_ENABLED(CONFIG_ARCH_SUN8IW20)) || !res ||
	    res->start != 0x04020000 || host->phy_index != 0 ||
	    !of_device_is_compatible(pdev->dev.of_node, "allwinner,sunxi-mmc-v5p3x")) {
		dev_warn(&pdev->dev, "SD-DIAG unsupported controller; capture disabled\n");
		return;
	}
	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		goto disabled;
	d->dev = &pdev->dev;
	/* Do not claim resources already owned by the pinctrl and CCU drivers. */
	d->pio = devm_ioremap(&pdev->dev, 0x02000000, 0x354);
	d->ccu = devm_ioremap(&pdev->dev, 0x02001000, 0x850);
	if (!d->pio || !d->ccu)
		goto disabled;
	INIT_WORK(&d->work, sunxi_mmc_diag_work);
	INIT_WORK(&d->init_work, sunxi_mmc_diag_init_work);
	if (devm_add_action_or_reset(&pdev->dev, sunxi_mmc_diag_cancel, d))
		goto disabled;
	host->diag = d;
	dev_info(&pdev->dev, "SD-DIAG armed: CMD2 initialization trace and CMD18 read capture; deferred output\n");
	return;
disabled:
	dev_warn(&pdev->dev, "SD-DIAG allocation failed; capture disabled\n");
}

void sunxi_mmc_diag_start(struct sunxi_mmc_host *host,
			  struct mmc_command *cmd, u32 cmd_val)
{
	struct sunxi_mmc_diag *d = host->diag;

	if (!d || !host->mrq)
		return;
	sunxi_mmc_diag_trace(host, cmd, cmd_val, 0, 0, true);
	if (cmd->opcode == MMC_ALL_SEND_CID && !d->init_started && !d->init_captured) {
		sunxi_mmc_diag_snapshot(host, cmd, cmd_val, &d->init_baseline);
		d->init_started = true;
	}
	if (d->started || d->captured || cmd->opcode != MMC_READ_MULTIPLE_BLOCK ||
	    !host->mrq->data)
		return;
	sunxi_mmc_diag_snapshot(host, cmd, cmd_val, &d->baseline);
	d->started = true;
}

void sunxi_mmc_diag_error(struct sunxi_mmc_host *host, u32 mista, u32 idst)
{
	struct sunxi_mmc_diag *d = host->diag;
	struct mmc_request *mrq = host->mrq;

	if (!d || !mrq)
		return;
	if (!d->init_captured && host->mmc->ios.clock <= 400000)
		sunxi_mmc_diag_trace(host, mrq->cmd, mmc_readl(host, REG_CMDR),
				     mista, idst, false);
	if (!((mista & SDXC_INTERRUPT_ERROR_BIT) ||
	      (idst & (SDXC_IDMAC_FATAL_BUS_ERROR | SDXC_IDMAC_ABNORMAL_INTERRUPT_SUM))))
		return;
	if (mrq->cmd->opcode == MMC_ALL_SEND_CID && !d->init_captured) {
		sunxi_mmc_diag_snapshot(host, mrq->cmd,
					mmc_readl(host, REG_CMDR), &d->init_failure);
		d->init_failure.ctrl[SDXC_REG_MISTA / 4] = mista;
		d->init_failure.dma[(SDXC_REG_IDST - 0x80) / 4] = idst;
		d->init_captured = true;
		schedule_work(&d->init_work);
		return;
	}
	if (d->captured || !mrq->data || mrq->cmd->opcode != MMC_READ_MULTIPLE_BLOCK)
		return;
	sunxi_mmc_diag_snapshot(host, mrq->cmd,
				mmc_readl(host, REG_CMDR), &d->failure);
	/* Preserve the exact status values observed at IRQ entry. */
	d->failure.ctrl[SDXC_REG_MISTA / 4] = mista;
	d->failure.dma[(SDXC_REG_IDST - 0x80) / 4] = idst;
	d->captured = true;
	schedule_work(&d->work);
}
