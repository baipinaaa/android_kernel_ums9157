// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- dma register interface (cpp_k_dma.c).
 *
 * 重建自 vendor `cpp_lite/hw/lite_r6p0/cpp_k_dma.c`。
 * 使能位在 ctrl 字的 bit2；配置寄存器组在 0x180 ~ 0x188。
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sched.h>

#include "cpp_lite.h"

#define DMA_ERR(fmt, ...) \
	pr_err("DMA_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define DMA_DBG(fmt, ...) \
	pr_debug("DMA_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

int cpp_k_dma_dev_enable(struct cpp_dma_dev *dm)
{
	unsigned long flags;

	if (!dm) {
		DMA_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&dm->slock, flags);
	*dm->ctrl |= CPP_DMA_EB;
	spin_unlock_irqrestore(&dm->slock, flags);

	return 0;
}

int cpp_k_dma_dev_disable(struct cpp_dma_dev *dm)
{
	unsigned long flags;

	if (!dm) {
		DMA_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&dm->slock, flags);
	*dm->ctrl &= ~CPP_DMA_EB;
	spin_unlock_irqrestore(&dm->slock, flags);

	return 0;
}

/*
 * cpp_k_dma_dev_cfg() —— dma 通道配置：
 *   0x180 <- cfg0        (vendor dm +0x80)
 *   0x184 <- cfg1        (vendor dm +0x84)
 *   0x188 <- size & 0xfffff（vendor dm +0x38，bfxil 低 20 位）
 */
int cpp_k_dma_dev_cfg(struct cpp_dma_dev *dm, struct cpp_dma_param *p)
{
	u32 val;

	if (!dm || !p) {
		DMA_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	dm->cfg0 = p->src_addr;
	dm->cfg1 = p->dst_addr;
	dm->size = p->size;

	writel(dm->cfg0, g_cpp_base + CPP_REG_DMA_CFG);
	writel(dm->cfg1, g_cpp_base + CPP_REG_DMA_CFG1);

	val = readl(g_cpp_base + CPP_REG_DMA_CFG2);
	val = (val & ~0xfffff) | (dm->size & 0xfffff);
	writel(val, g_cpp_base + CPP_REG_DMA_CFG2);

	DMA_DBG("dma cfg 0x%08x 0x%08x size 0x%x\n", dm->cfg0, dm->cfg1,
		dm->size);

	return 0;
}

int cpp_k_dma_dev_start(struct cpp_dma_dev *dm)
{
	unsigned long flags;

	if (!dm) {
		DMA_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&dm->slock, flags);
	*dm->ctrl |= CPP_DMA_EB;
	spin_unlock_irqrestore(&dm->slock, flags);

	return 0;
}

int cpp_k_dma_dev_stop(struct cpp_dma_dev *dm)
{
	unsigned long flags;

	if (!dm) {
		DMA_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&dm->slock, flags);
	*dm->ctrl &= ~CPP_DMA_EB;
	spin_unlock_irqrestore(&dm->slock, flags);

	return 0;
}

void cpp_k_dma_reg_trace(struct cpp_dma_dev *dm)
{
	if (!dm)
		return;

	DMA_DBG("dma reg 0x180=0x%08x 0x184=0x%08x 0x188=0x%08x\n",
		readl(g_cpp_base + CPP_REG_DMA_CFG),
		readl(g_cpp_base + CPP_REG_DMA_CFG1),
		readl(g_cpp_base + CPP_REG_DMA_CFG2));
}
