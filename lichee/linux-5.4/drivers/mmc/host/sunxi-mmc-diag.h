/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SUNXI_MMC_DIAG_H
#define SUNXI_MMC_DIAG_H

#include <linux/types.h>

struct platform_device;
struct mmc_command;
struct sunxi_mmc_host;

void sunxi_mmc_diag_init(struct sunxi_mmc_host *host,
			 struct platform_device *pdev);
void sunxi_mmc_diag_start(struct sunxi_mmc_host *host,
			  struct mmc_command *cmd, u32 cmd_val);
void sunxi_mmc_diag_error(struct sunxi_mmc_host *host, u32 mista, u32 idst);

#endif
