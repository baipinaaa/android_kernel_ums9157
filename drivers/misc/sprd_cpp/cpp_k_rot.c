// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- rotator register interface (cpp_k_rot.c).
 *
 * 重建自 vendor `cpp_lite/hw/lite_r6p0/cpp_k_rot.c`。
 * 使能位在 ctrl 字的 bit1；矩形/步长寄存器组在 0x100 ~ 0x110。
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sched.h>

#include "cpp_lite.h"

#define ROT_ERR(fmt, ...) \
	pr_err("CPP_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define ROT_DBG(fmt, ...) \
	pr_debug("CPP_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

int cpp_k_rot_dev_enable(struct cpp_rot_dev *rot)
{
	unsigned long flags;

	if (!rot) {
		ROT_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&rot->slock, flags);
	*rot->ctrl |= CPP_ROT_EB;
	spin_unlock_irqrestore(&rot->slock, flags);

	return 0;
}

int cpp_k_rot_dev_disable(struct cpp_rot_dev *rot)
{
	unsigned long flags;

	if (!rot) {
		ROT_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&rot->slock, flags);
	*rot->ctrl &= ~CPP_ROT_EB;
	spin_unlock_irqrestore(&rot->slock, flags);

	return 0;
}

int cpp_k_rot_dev_start(struct cpp_rot_dev *rot)
{
	unsigned long flags;

	if (!rot) {
		ROT_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&rot->slock, flags);
	*rot->ctrl |= CPP_ROT_EB;
	spin_unlock_irqrestore(&rot->slock, flags);

	return 0;
}

int cpp_k_rot_dev_stop(struct cpp_rot_dev *rot)
{
	unsigned long flags;

	if (!rot) {
		ROT_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&rot->slock, flags);
	*rot->ctrl &= ~CPP_ROT_EB;
	spin_unlock_irqrestore(&rot->slock, flags);

	return 0;
}

/*
 * cpp_k_rot_parm_set() —— 把 rotator 参数写进寄存器：
 *   0x100 <- src addr y            (vendor param +0x5c)
 *   0x104 <- dst addr y            (vendor param +0x60)
 *   0x108 <- src pitch (低 16) + w  (vendor param +0x64/+0x68)
 *   0x110 <- 格式与角度控制
 */
int cpp_k_rot_parm_set(struct cpp_rot_dev *rot, struct cpp_rot_param *p)
{
	u32 val;

	if (!rot || !p) {
		ROT_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	writel(p->src_addr_y, g_cpp_base + CPP_REG_ROT_CFG);
	writel(p->dst_addr_y, g_cpp_base + CPP_REG_ROT_CFG1);

	val = (p->src_w & 0xffff) | ((p->src_h & 0xffff) << 16);
	writel(val, g_cpp_base + CPP_REG_ROT_CFG2);

	val = (p->format & 0xffff) | ((p->angle & 0x7) << 16);
	writel(val, g_cpp_base + CPP_REG_ROT_CFG3);

	ROT_DBG("cpp_k_rot_parm_set src 0x%x dst 0x%x format 0x%x angle %d\n",
		p->src_addr_y, p->dst_addr_y, p->format, p->angle);

	return 0;
}

void cpp_k_rot_reg_trace(struct cpp_rot_dev *rot)
{
	if (!rot)
		return;

	ROT_DBG("rot reg 0x100=0x%08x 0x104=0x%08x 0x108=0x%08x 0x110=0x%08x\n",
		readl(g_cpp_base + CPP_REG_ROT_CFG),
		readl(g_cpp_base + CPP_REG_ROT_CFG1),
		readl(g_cpp_base + CPP_REG_ROT_CFG2),
		readl(g_cpp_base + CPP_REG_ROT_CFG3));
}
