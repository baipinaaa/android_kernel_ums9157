// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) CSI2 receiver driver (receiver r3p1v2) - low level
 * register and PHY helpers.
 *
 * Reconstructed from sprd_sensor.ko (csi2/sprd/receiver_r3p1v2/csi_driver.c).
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/uaccess.h>

#include "sprd_sensor.h"

/* ioremap() protection used by the register accessors */
#define CSI_REG_SIZE			4

/* CSI controller registers */
#define CSI_IPG_BASE			0x0000
#define CSI_PHY_TESTCLR			0x0048
#define CSI_PHY_TESTCLR_OFFSET		0x004c
#define CSI_PHY_TESTCLR_S		0x00a0
#define CSI_PHY_TESTCLR_S_OFFSET	0x00a4

#define CSI_MAX_NUM			6
#define CSI_DUMP_MAX_NUM		4

/* dt info of one csi receiver, filled by csi_api_dt_node_init() */
struct csi_dt_info {
	u32 id;				/* +0x00 */
	u32 phy_id;			/* +0x04 */
	void __iomem *reg_base;		/* +0x08 */
	struct regmap *cam_ahb_syscon;	/* +0x10 */
	struct regmap *aon_apb_syscon;	/* +0x18 */
	u32 dw_phy_id;			/* +0x20 selects the anlg phy bit */
	u32 reg_size;			/* +0x24 */
	struct device_node *of_node;	/* +0x28 */
	struct clk *clk_csi_eb;		/* +0x30 */
	struct clk *clk_mipi_csi_src_eb;/* +0x38 */
	struct regmap *anlg_phy_g4;	/* +0x40 */
	struct regmap *anlg_phy_g4l;	/* +0x48 */
	u32 reserved1;			/* +0x50 */
	u8 mipi_state;			/* +0x58 */
	u8 pad2[7];			/* +0x59 */
	void *priv;			/* +0x60 */
	u32 bps_per_lane;		/* +0x68 set by csi_api_open() */
};

/*
 * per receiver driver context, the objects kept in s_csi_dt_info_p[].  This
 * is the object handed to csi_controller_enable/disable, csi_phy_power_down
 * and csi_mipi_clk_enable/disable.
 */
struct csi_dev {
	u32 id;				/* +0x00 */
	u32 phy_id;			/* +0x04 */
	void __iomem *reg_base;		/* +0x08 */
	void *reserved10;		/* +0x10 */
	void *reserved18;		/* +0x18 */
	struct clk *clk_csi_eb;		/* +0x20 */
	struct clk *clk_mipi_src_eb;	/* +0x28 */
	struct regmap *ctrl_syscon;	/* +0x30 */
	struct regmap *phy_syscon;	/* +0x38 bit15 of 0x13c gates the phy clk */
	struct regmap *cam_ahb2;	/* +0x40 */
	struct regmap *aon_apb2;	/* +0x48 */
	u32 dw_phy_id;			/* +0x50 */
	u32 reg_size;			/* +0x54 */
	u8 mipi_state;			/* +0x58 */
	u8 pad0[7];			/* +0x59 */
	void *priv;			/* +0x60 */
};

/* csi hw context, first argument of csi_ahb_reset() */
struct csi_hw_ctx {
	struct regmap *syscon;		/* +0x00 */
	u8 pad0[0x18];			/* +0x08 */
	u32 dw_phy_id;			/* +0x20 */
};

/* one CSI controller base, indexed by csi id */
void __iomem *s_csi_regbase[CSI_MAX_NUM];

/* PHY dump base, indexed by phy id */
void __iomem *csi_dump_regbase[CSI_DUMP_MAX_NUM];

struct mutex csi_dump_lock;

/* set when the HAL asked for the csi test pattern mode */
u8 csi_pattern;

/* set by SENSOR_IO_IF_DUMP, makes the next close dump the csi registers */
u8 csi_reg_dump_flag;

u8 csi_pattern_enable;

/* per receiver driver context, indexed by csi id */
struct csi_dev *s_csi_dt_info_p[CSI_MAX_NUM];

/* number of users of the csi eb clock */
static int csi_clk_ref_cnt;

int csi_reg_base_save(struct csi_dt_info *info, u32 id)
{
	if (!info) {
		pr_err("csi_driver: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	if (id >= CSI_MAX_NUM)
		return -EINVAL;

	s_csi_regbase[id] = info->reg_base;

	if (info->id < CSI_DUMP_MAX_NUM)
		csi_dump_regbase[info->id] = NULL;

	return 0;
}

/* read/modify/write of one CSI controller register */
int CSI_REG_MWR(u32 id, u32 offset, u32 mask, u32 value)
{
	void __iomem *base;
	u32 tmp;

	if (id >= CSI_MAX_NUM)
		return -EINVAL;

	base = s_csi_regbase[id];

	tmp = readl(base + offset);

	pr_debug("csi_driver: %d %d %s : reg_base 0x%x 0x%x\n", current->pid,
		 __LINE__, __func__, (u32)(unsigned long)(base + offset), tmp);

	writel((tmp & ~mask) | (value & mask), base + offset);

	return 0;
}

/* PHY register read through the testclr interface of the csi controller */
static void phy_reg_trace(u32 phy_id, u8 addr, u8 *val)
{
	void __iomem *base;

	if (phy_id >= CSI_DUMP_MAX_NUM)
		return;

	base = csi_dump_regbase[phy_id];
	if (!base) {
		pr_info("csi_driver: %d %d %s : CSI %d not used no need to dump\n",
			current->pid, __LINE__, __func__, phy_id);
		return;
	}

	writel(readl(base + CSI_PHY_TESTCLR_OFFSET) | 0x10000,
	       base + CSI_PHY_TESTCLR_OFFSET);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR) | 2, base + CSI_PHY_TESTCLR);
	udelay(1);
	writel((readl(base + CSI_PHY_TESTCLR_OFFSET) & ~0xff) | (addr & 0xff),
	       base + CSI_PHY_TESTCLR_OFFSET);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR) & ~2, base + CSI_PHY_TESTCLR);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR_OFFSET) & ~0x10000,
	       base + CSI_PHY_TESTCLR_OFFSET);
	udelay(1);

	*val = readl(base + CSI_PHY_TESTCLR_OFFSET) >> 8;

	pr_debug("csi_driver: %d %d %s : PHY Read addr %x value = 0x%x.\r\n",
		 current->pid, __LINE__, __func__, addr, *val);
}

static void phy_reg_trace_s(u32 phy_id, u8 addr, u8 *val)
{
	void __iomem *base;

	if (phy_id >= CSI_DUMP_MAX_NUM)
		return;

	base = csi_dump_regbase[phy_id];
	if (!base) {
		pr_info("csi_driver: %d %d %s : CSI %d not used no need to dump\n",
			current->pid, __LINE__, __func__, phy_id);
		return;
	}

	writel(readl(base + CSI_PHY_TESTCLR_S_OFFSET) | 0x10000,
	       base + CSI_PHY_TESTCLR_S_OFFSET);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR_S) | 2, base + CSI_PHY_TESTCLR_S);
	udelay(1);
	writel((readl(base + CSI_PHY_TESTCLR_S_OFFSET) & ~0xff) | (addr & 0xff),
	       base + CSI_PHY_TESTCLR_S_OFFSET);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR_S) & ~2, base + CSI_PHY_TESTCLR_S);
	udelay(1);
	writel(readl(base + CSI_PHY_TESTCLR_S_OFFSET) & ~0x10000,
	       base + CSI_PHY_TESTCLR_S_OFFSET);
	udelay(1);

	*val = readl(base + CSI_PHY_TESTCLR_S_OFFSET) >> 8;

	pr_debug("csi_driver: %d %d %s : PHY_S Read addr %x value = 0x%x.\r\n",
		 current->pid, __LINE__, __func__, addr, *val);
}

/* physical address helpers, used for the anlg_phy regs */
int reg_mwr(u32 addr, u32 mask, u32 val)
{
	void __iomem *base;
	u32 tmp;

	base = ioremap(addr, CSI_REG_SIZE);
	if (!base) {
		pr_info("csi_driver: %d %d %s : 0x%x: ioremap failed\n",
			current->pid, __LINE__, __func__, addr);
		return -1;
	}

	tmp = readl(base);
	writel((tmp & ~mask) | (val & mask), base);
	dsb(sy);
	iounmap(base);

	return 0;
}

int reg_wr(u32 addr, u32 val)
{
	void __iomem *base;

	base = ioremap(addr, CSI_REG_SIZE);
	if (!base) {
		pr_info("csi_driver: %d %d %s : 0x%x: ioremap failed\n",
			current->pid, __LINE__, __func__, addr);
		return -1;
	}

	writel(val, base);
	dsb(sy);
	iounmap(base);

	return 0;
}

int reg_rd(u32 addr)
{
	void __iomem *base;
	u32 val;

	base = ioremap(addr, CSI_REG_SIZE);
	if (!base) {
		pr_info("csi_driver: %d %d %s : 0x%x: ioremap failed\n",
			current->pid, __LINE__, __func__, addr);
		return -1;
	}

	val = readl(base);

	pr_debug("csi_driver: %d %d %s : 0x%x: val %x\n", current->pid,
		 __LINE__, __func__, addr, val);

	iounmap(base);

	return val;
}

int reg_dump_rd(u32 addr, int size, const char *tag)
{
	void __iomem *base;
	int i;

	base = ioremap(addr, size);
	if (!base) {
		pr_info("csi_driver: %d %d %s : 0x%x: ioremap failed\n",
			current->pid, __LINE__, __func__, addr);
		return -1;
	}

	pr_info("csi_driver: %d %d %s : 0x%x: dump reg\n", current->pid,
		__LINE__, __func__, addr);

	for (i = 0; i <= size; i += 0x10)
		pr_info("csi_driver: %d %d %s : %s 0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			current->pid, __LINE__, __func__, tag, (unsigned long)i,
			readl(base + i), readl(base + i + 4),
			readl(base + i + 8), readl(base + i + 0xc));

	iounmap(base);

	return 0;
}

/* dump the register window of one csi controller */
int csi_reg_trace(u32 id)
{
	void __iomem *base;
	unsigned long flags;
	int i;

	if (id >= CSI_MAX_NUM)
		return -EINVAL;

	spin_lock_irqsave((spinlock_t *)&csi_dump_lock + id, flags);

	if (id == CSI_DUMP_MAX_NUM)
		goto out;

	base = csi_dump_regbase[id];
	if (!base) {
		pr_info("csi_driver: %d %d %s : CSI %d not used no need to dump\n",
			current->pid, __LINE__, __func__, id);
		goto out;
	}

	pr_info("csi_driver: %d %d %s : CSI %d reg list\n", current->pid,
		__LINE__, __func__, id);

	for (i = 0; i < 0x100; i += 0x10)
		pr_info("csi_driver: %d %d %s : 0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			current->pid, __LINE__, __func__, (unsigned long)i,
			readl(base + i), readl(base + i + 4),
			readl(base + i + 8), readl(base + i + 0xc));
out:
	spin_unlock_irqrestore((spinlock_t *)&csi_dump_lock + id, flags);

	return 0;
}

/* put the analog phy testclr pin of one phy in a known state */
int csi_phy_testclr_init(struct csi_dt_info *info)
{
	switch (info->dw_phy_id) {
	case 0:
		regmap_update_bits(info->cam_ahb_syscon, 0x30, 1, 0);
		break;
	case 1:
		regmap_update_bits(info->aon_apb_syscon, 0x30, 1, 0);
		break;
	case 2:
		regmap_update_bits(info->cam_ahb_syscon, 0x5c, 0x10, 0x10);
		break;
	case 3:
		regmap_update_bits(info->aon_apb_syscon, 0x5c, 0x10, 0x10);
		break;
	case 4:
	case 5:
		regmap_update_bits(info->cam_ahb_syscon, 0x5c, 0x10, 0);
		break;
	case 6:
	case 7:
		regmap_update_bits(info->aon_apb_syscon, 0x5c, 0x10, 0);
		break;
	case 8:
		regmap_update_bits(info->cam_ahb_syscon, 0x30, 1, 1);
		break;
	case 9:
		regmap_update_bits(info->aon_apb_syscon, 0x30, 1, 1);
		break;
	default:
		pr_err("csi_driver: %d %d %s : fail to get valid phy id %d\n",
		       current->pid, __LINE__, __func__, info->dw_phy_id);
		break;
	}

	return 0;
}

/* ahb reset bits of the four csi receivers, indexed by csi id */
static const u32 c_csi_ahb_reset_tab[CSI_DUMP_MAX_NUM] = {
	0x800, 0x400, 0x200, 0x100
};

/* anlg phy power down registers / masks, indexed by phy id */
static const u32 c_csi_phy_pd_reg_tab[10] = {
	0x2c, 0x2c, 0x58, 0x58, 0x58, 0x68, 0x58, 0x68, 0x2c, 0x2c
};

static const u32 c_csi_phy_pd_mask_tab[10] = {
	0x40, 0x40, 0x80, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x40
};

/*
 * pulse the analog phy testclr pin of one receiver.  Phy 0/2/4/5/8 are
 * driven through the aon apb syscon, the remaining ones through cam ahb.
 */
void csi_phy_testclr(u32 id, struct csi_dt_info *info)
{
	struct regmap *syscon;
	unsigned long flags;

	if (info->dw_phy_id <= 8 && ((1 << info->dw_phy_id) & 0x135))
		syscon = info->aon_apb_syscon;
	else
		syscon = info->cam_ahb_syscon;

	if (id >= CSI_MAX_NUM)
		return;

	spin_lock_irqsave((spinlock_t *)&csi_dump_lock + id, flags);

	if (info->dw_phy_id > 9) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy id %d\n",
		       current->pid, __LINE__, __func__, info->dw_phy_id);
		goto out;
	}

	if ((1 << info->dw_phy_id) & 0xfc) {
		regmap_update_bits(syscon, 0xa8, 1, 1);
		CSI_REG_MWR(id, 0x48, 1, 1);
		CSI_REG_MWR(id, 0xa0, 1, 1);
		regmap_update_bits(syscon, 0xa8, 1, 0);
		CSI_REG_MWR(id, 0x48, 1, 0);
		CSI_REG_MWR(id, 0xa0, 1, 0);
	} else {
		CSI_REG_MWR(id, 0x48, 1, 1);
		CSI_REG_MWR(id, 0x48, 1, 0);
	}

out:
	spin_unlock_irqrestore((spinlock_t *)&csi_dump_lock + id, flags);
}

/* power the anlg phy up or down (on != 0 -> power down) */
void csi_phy_power_down(struct csi_dev *phy, u32 unused, int on)
{
	struct regmap *syscon;
	u32 id, reg, mask;

	if (!phy || !phy->clk) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	id = phy->dw_phy_id;

	if (id <= 8 && ((1 << id) & 0x135))
		syscon = phy->aon_apb2;
	else
		syscon = phy->cam_ahb2;

	if (id >= 10) {
		pr_err("csi_driver: %d %d %s : fail to get valid csi_rx id\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	reg = c_csi_phy_pd_reg_tab[id];
	mask = c_csi_phy_pd_mask_tab[id];

	if (on)
		regmap_update_bits(syscon, reg, mask, ~mask);
	else
		regmap_update_bits(syscon, reg, mask, mask);

	if ((id & ~1) == 2)	/* phy 2 and 3 */
		regmap_update_bits(syscon, 0x68, 0x40, on ? 0 : 0x40);
}

/* toggle the ahb reset of one csi receiver */
int csi_ahb_reset(struct csi_hw_ctx *ctx, u32 id)
{
	u32 bit;

	if (!ctx) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	pr_info("csi_driver: %d %d %s : csi, id %d phy %d\n", current->pid,
		__LINE__, __func__, id, ctx->dw_phy_id);

	if (id >= CSI_DUMP_MAX_NUM) {
		pr_err("csi_driver: %d %d %s : fail to get valid csi id %d\n",
		       current->pid, __LINE__, __func__, id);
		bit = 0;
	} else {
		bit = c_csi_ahb_reset_tab[id];
	}

	regmap_update_bits(ctx->ctrl_syscon, 0xc8, bit, bit);
	udelay(1);
	regmap_update_bits(ctx->ctrl_syscon, 0xc8, bit, ~bit);

	return 0;
}

/* ahb enable bits of the four receivers, indexed by csi id */
static const u32 c_csi_ahb_en_tab[CSI_DUMP_MAX_NUM] = {
	0x1000, 0x2000, 0x4000, 0x8000
};

/* enable one csi controller and wait until its ahb clock is alive */
void csi_controller_enable(struct csi_dev *ctx)
{
	u32 bit;

	if (!ctx) {
		pr_err("csi_driver: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n", current->pid,
		__LINE__, __func__, __func__, ctx->id, ctx->dw_phy_id);

	if (!ctx->ctrl_syscon) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	if (ctx->id >= CSI_DUMP_MAX_NUM) {
		pr_err("csi_driver: %d %d %s : fail to get valid csi id\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	bit = c_csi_ahb_en_tab[ctx->id];
	csi_dump_regbase[ctx->id] = ctx->reg_base;

	do {
		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d enable\n",
			current->pid, __LINE__, __func__, __func__,
			ctx->id, ctx->dw_phy_id);
		regmap_update_bits(ctx->ctrl_syscon, 0x0c, bit, bit);
		udelay(10);
	} while (!(reg_rd(0x3000000c) & bit));
}

/* disable one csi controller */
void csi_controller_disable(struct csi_dev *ctx)
{
	u32 bit;

	if (!ctx) {
		pr_err("csi_driver: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	if (!ctx->ctrl_syscon) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n", current->pid,
		__LINE__, __func__, __func__, ctx->id, ctx->dw_phy_id);

	if (ctx->id >= CSI_DUMP_MAX_NUM) {
		pr_err("csi_driver: %d %d %s : fail to get valid csi id\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	bit = c_csi_ahb_reset_tab[ctx->id];
	csi_dump_regbase[ctx->id] = NULL;

	regmap_update_bits(ctx->ctrl_syscon, 0xc8, bit, ~bit);
	udelay(1);
	regmap_update_bits(ctx->ctrl_syscon, 0xc8, bit, ~bit);
}

/* the two csi path helpers are pure bookkeeping in the vendor driver */
void phy_csi_path_cfg(struct csi_dev *ctx)
{
	if (!ctx) {
		pr_err("csi_driver: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	if (!ctx->ctrl_syscon) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n", current->pid,
		__LINE__, __func__, __func__, ctx->id, ctx->dw_phy_id);
}

void phy_csi_path_clr_cfg(struct csi_dev *ctx)
{
	if (!ctx) {
		pr_err("csi_driver: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	if (!ctx->ctrl_syscon) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n", current->pid,
		__LINE__, __func__, __func__, ctx->id, ctx->dw_phy_id);
}

/*
 * calibrate the cphy oscillator and program the resulting loop counter into
 * the three lane registers.
 */
void cphy_osc_result_cal(u32 phy_id, u32 bps_per_lane)
{
	u8 sel, n_loop_hi, n_loop_lo;
	u32 mul, n_loop, ratio, reg;

	phy_read(phy_id, 0x3b, &sel);

	switch (sel >> 6) {
	case 2:
		mul = 4;
		break;
	case 3:
		mul = 8;
		break;
	default:
		mul = (sel >> 6) + 1;
		break;
	}

	pr_info("csi_driver: %d %d %s : n_win_set0 %d bps_per_lane %d\n",
		current->pid, __LINE__, __func__, mul, bps_per_lane);

	phy_read(phy_id, 0x48, &n_loop_hi);
	phy_read(phy_id, 0x49, &n_loop_lo);

	reg = n_loop_hi | (n_loop_lo << 8);
	ratio = (bps_per_lane * mul) << 11;
	n_loop = div_u64((u64)reg * 0x6ce0, ratio) + 0xffed;

	phy_write(phy_id, 0x96, n_loop & 0x7f, 0x7f);
	phy_write(phy_id, 0xb6, n_loop & 0x7f, 0x7f);
	phy_write(phy_id, 0xd6, n_loop & 0x7f, 0x7f);

	pr_info("csi_driver: %d %d %s : n_loop %d %d %d %d\n", current->pid,
		__LINE__, __func__, n_loop & 0xffff, n_loop_hi, n_loop_lo, reg);
}

/*
 * The anlg phy is accessed through the testclr/testdata registers of the
 * receiver: 0x4c holds bit16 = testclr, bit15 = testen and bits[7:0] = data,
 * 0x48 bit1 is the clock toggle.  Every step is followed by a 1us delay.
 */
void phy_read(u32 id, u32 addr, u8 *val)
{
	u32 reg;

	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x4c, 0x10000, 0x10000);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 2);
	udelay(1);
	CSI_REG_MWR(id, 0x4c, 0xff, addr);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 0);
	udelay(1);
	CSI_REG_MWR(id, 0x4c, 0x10000, 0);
	udelay(1);

	reg = readl(s_csi_regbase[id] + 0x4c);
	udelay(1);

	*val = reg >> 8;

	pr_debug("csi_driver: %d %d %s : PHY Read addr %x value = 0x%x.\r\n",
		 current->pid, __LINE__, __func__, addr, reg >> 8);
}

/* write mask bits of one phy register, keeping the other bits untouched */
void phy_write(u32 id, u32 addr, u32 value, u32 mask)
{
	u32 reg, data;

	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x4c, 0x10000, 0x10000);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 2);
	udelay(1);
	CSI_REG_MWR(id, 0x4c, 0xff, addr);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 0);
	udelay(1);

	reg = readl(s_csi_regbase[id] + 0x4c);
	udelay(1);

	data = (~mask & (reg >> 8)) | (mask & value);

	CSI_REG_MWR(id, 0x4c, 0x10000, 0);
	udelay(1);
	CSI_REG_MWR(id, 0x4c, 0xff, data);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 2);
	udelay(1);
	CSI_REG_MWR(id, 0x48, 2, 0);
	udelay(1);

	reg = readl(s_csi_regbase[id] + 0x4c);
	udelay(1);

	pr_debug("csi_driver: %d %d %s : PHY write addr %x value = 0x%x.\r\n",
		 current->pid, __LINE__, __func__, addr, reg >> 8);
}

/* shut down / release the phy stream of one receiver */
void csi_shut_down_phy(u32 en, u32 id)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x08, 1, (en & 0xff) ? 0 : 1);
}

/* tell the receiver how many data lanes are in use */
void csi_set_on_lanes(u32 lanes, u32 id)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x04, 7, lanes - 1);
}

/* unmask every receiver interrupt */
void csi_event_enable(u32 id)
{
	void __iomem *base;

	udelay(10);

	if (id >= CSI_MAX_NUM)
		return;

	base = s_csi_regbase[id];

	writel(0x1ffffff, base + 0x28);
	udelay(10);
	writel(0xffffff, base + 0x2c);
	udelay(10);
	writel(7, base + 0x78);
}

/* stop the receiver: drop its enable bit and reset phy and controller */
void csi_close(u32 id)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x08, 1, 0);

	csi_reset_phy(id);
	csi_reset_controller(id);
}

/* enable the register dump of the next csi error */
void csi_api_reg_trace(void)
{
	csi_reg_dump_flag = 1;
}

/* copy the runtime part of a dt info into the global per receiver copy */
int csi_set_dt_node_data(struct csi_dt_info *src, u32 id)
{
	struct csi_dt_info *dst;

	if (!src) {
		pr_err("csi_api: %d %d %s : fail to get the csi dt info\n",
		       current->pid, __LINE__, __func__);
		return -1;
	}

	if (id >= CSI_MAX_NUM)
		return -1;

	dst = s_csi_dt_info_p[id];

	dst->mipi_state = src->mipi_state;
	dst->priv = src->priv;

	return 0;
}

/* power up the analog front end of the three dphy lanes */
void dphy_afe_cali(u32 unused, u32 id)
{
	phy_write(id, 0x90, 0x60, 0x60);
	phy_write(id, 0xb0, 0x60, 0x60);
	phy_write(id, 0xd0, 0x60, 0x60);

	udelay(10);

	phy_write(id, 0x90, 0, 0x60);
	phy_write(id, 0xb0, 0, 0x60);
	phy_write(id, 0xd0, 0, 0x60);
}

/* reset the pixel/line controller of one receiver */
void csi_reset_controller(u32 id)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x10, 1, 0);
	CSI_REG_MWR(id, 0x10, 1, 1);
}

/* reset the phy of one receiver */
void csi_reset_phy(u32 id)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x0c, 1, 0);
	CSI_REG_MWR(id, 0x0c, 1, 1);
}

/* anlg phy 2p2l 2lane testclr bits / register, indexed by phy id */
static const u32 c_csi_2p2l_bit_tab[10] = {
	0x800000, 0x800000, 0x1, 0x1, 0x1, 0x10000, 0x1, 0x10000, 0x800000, 0x800000
};

static const u32 c_csi_2p2l_reg_tab[10] = {
	0x34, 0x34, 0x58, 0x58, 0x58, 0x6c, 0x58, 0x6c, 0x34, 0x34
};

/* pulse testclr of the 2p2l / 2lane capable analog phys */
void csi_2p2l_2lane_phy_testclr(struct csi_dt_info *info)
{
	struct regmap *syscon;
	u32 bit, reg;

	if (info->dw_phy_id >= 10) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy id %d\n",
		       current->pid, __LINE__, __func__, info->dw_phy_id);
		return;
	}

	bit = c_csi_2p2l_bit_tab[info->dw_phy_id];
	reg = c_csi_2p2l_reg_tab[info->dw_phy_id];

	if (info->dw_phy_id <= 8 && ((1 << info->dw_phy_id) & 0x135))
		syscon = info->aon_apb_syscon;
	else
		syscon = info->cam_ahb_syscon;

	regmap_update_bits(syscon, reg, bit, bit);
	udelay(1);
	regmap_update_bits(syscon, reg, bit, ~bit);

	if ((info->dw_phy_id & ~1) == 2) {	/* phy 2 and 3 */
		regmap_update_bits(syscon, 0x6c, 0x10000, 0x10000);
		udelay(1);
		regmap_update_bits(syscon, 0x6c, 0x10000, ~0x10000);
	}
}

/* gate on the csi and mipi source clocks of one receiver */
int csi_mipi_clk_enable(u32 id)
{
	struct csi_dev *info = s_csi_dt_info_p[id];
	void __iomem *map;
	int ret;

	if (!info) {
		pr_err("csi_api: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	pr_debug("csi_api: %d %d %s : cnt 0x%x 0x%x\n", current->pid,
		 __LINE__, __func__, csi_clk_ref_cnt, reg_rd(0x3000000c));

	if (!info->clk_csi_eb) {
		pr_err("csi_api: %d %d %s : fail to csi mipi clk enable\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	/* the aon apb block must have its mipi csi clock bits set first */
	if (~reg_rd(0x3000000c) & 0xf000) {
		map = __ioremap(0x3000000c, 1, 0x680000000707);
		if (!map) {
			pr_err("csi_api: %d %d %s : 0x%x: ioremap failed\n",
			       current->pid, __LINE__, __func__, 0x3000000c);
			return -1;
		}
		writel(readl(map) | 0xf000, map);
		iounmap(map);
	}

	ret = clk_prepare(info->clk_csi_eb);
	if (ret)
		goto err;

	ret = clk_enable(info->clk_csi_eb);
	if (ret) {
		clk_unprepare(info->clk_csi_eb);
		goto err;
	}

	reg_mwr(0x3000000c, 4, 4);

	regmap_update_bits(info->phy_syscon, 0x13c, 0x8000, 0x8000);
	regmap_update_bits(info->phy_syscon, 0x0c, 4, 4);

	if (!csi_pattern_enable) {
		pr_info("csi_api: %d %d %s : don't need enable ipg mode\n",
			current->pid, __LINE__, __func__);
		reg_mwr(0x649100a0, 0x1f, 0x1f);
		return 0;
	}

	ret = clk_prepare(info->clk_mipi_src_eb);
	if (ret)
		goto err;

	ret = clk_enable(info->clk_mipi_src_eb);
	if (ret)
		clk_unprepare(info->clk_mipi_src_eb);

	return 0;

err:
	pr_err("csi_api: %d %d %s : fail to csi eb clk\n", current->pid,
	       __LINE__, __func__, ret);
	return ret;
}

/* ungated counterpart of csi_mipi_clk_enable() */
void csi_mipi_clk_disable(u32 id)
{
	struct csi_dev *info = s_csi_dt_info_p[id];
	void __iomem *map;

	if (!info) {
		pr_err("csi_api: %d %d %s : fail to get valid dt_info ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	if (!info->clk_csi_eb) {
		pr_err("csi_api: %d %d %s : fail to csi mipi clk disable\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	pr_debug("csi_api: %d %d %s : cnt 0x%x 0x%x\n", current->pid,
		 __LINE__, __func__, csi_clk_ref_cnt, reg_rd(0x3000000c));

	regmap_update_bits(info->phy_syscon, 0x13c, 0x8000, ~0x8000);
	regmap_update_bits(info->phy_syscon, 0x0c, 4, ~4);

	if (!--csi_clk_ref_cnt) {
		map = __ioremap(0x3000000c, 1, 0x680000000707);
		if (!map) {
			pr_err("csi_api: %d %d %s : 0x%x: ioremap failed\n",
			       current->pid, __LINE__, __func__, 0x3000000c);
		} else {
			writel(readl(map) & 0xffff0fff, map);
			iounmap(map);
		}
	}

	clk_disable(info->clk_csi_eb);
	clk_unprepare(info->clk_csi_eb);

	if (!csi_pattern_enable) {
		pr_info("csi_api: %d %d %s : don't need disable ipg mode\n",
			current->pid, __LINE__, __func__);
		return;
	}

	clk_disable(info->clk_mipi_src_eb);
	clk_unprepare(info->clk_mipi_src_eb);
}

/* close one receiver: trace if asked, then unwind the open sequence */
int csi_api_close(u32 unused, u32 id)
{
	struct csi_dev *info;

	if (id >= CSI_MAX_NUM)
		return -EINVAL;

	info = s_csi_dt_info_p[id];
	if (!info) {
		pr_err("csi_api: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	if (csi_reg_dump_flag == 1)
		csi_reg_trace(info->id);

	if (csi_pattern_enable)
		csi_ipg_mode_cfg(info->id, 0);

	CSI_REG_MWR(info->id, 0x08, 1, 0);

	csi_reset_phy(info->id);
	csi_reset_controller(info->id);
	csi_controller_disable(info);
	csi_phy_power_down(info, 1);
	csi_mipi_clk_disable(id);

	pr_info("csi_api: %d %d %s : csi api close ret: %d\n", current->pid,
		__LINE__, __func__, 0);

	return 0;
}

/* nothing to switch in this hardware revision */
int csi_api_switch(void)
{
	return 0;
}

/* start the receiver with the given number of lanes and unmask its irqs */
void csi_start(u32 lanes, u32 id)
{
	void __iomem *base;

	if (id >= CSI_MAX_NUM)
		return;

	base = s_csi_regbase[id];

	CSI_REG_MWR(id, 0x04, 7, lanes - 1);
	udelay(10);
	writel(0x1ffffff, base + 0x28);
	udelay(10);
	writel(0xffffff, base + 0x2c);
	udelay(10);
	writel(7, base + 0x78);
}

/* the analog phy configuration is encoded in the dt, nothing to do here */
int csi_api_mipi_phy_cfg(void)
{
	return 0;
}

/*
 * open one receiver: program the phy id, reset and ungated it, then start
 * the controller.  pattern != 0 enables the ipg test pattern mode.
 */
int csi_api_open(u32 a, u32 phy_id, u32 lanes, u32 id, u32 pattern,
		 u32 use_phy_id, void *priv)
{
	struct csi_dev *info = s_csi_dt_info_p[id];
	void __iomem *base;
	u32 val, lane_val;
	int ret;

	if (id >= CSI_MAX_NUM)
		return -EINVAL;

	csi_pattern_enable = pattern;

	if (!info) {
		pr_err("csi_api: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return -EINVAL;
	}

	if (use_phy_id) {
		pr_info("csi_api: %d %d %s : orign phy_id: %d\n", current->pid,
			__LINE__, __func__, phy_id);

		if (id == 0)
			phy_id = 8;
		else if (id == 1)
			phy_id = 9;

		info->dw_phy_id = phy_id;
	}

	pr_info("csi_api: %d %d %s : csi open phy_id: %d\n", current->pid,
		__LINE__, __func__, phy_id);

	ret = csi_ahb_reset((struct csi_hw_ctx *)&info->ctrl_syscon, info->id);
	if (ret)
		goto err;

	info->priv = priv;

	ret = csi_mipi_clk_enable(id);
	if (ret) {
		pr_err("csi_api: %d %d %s : fail to csi mipi clk enable\n",
		       current->pid, __LINE__, __func__);
		csi_mipi_clk_disable(id);
		goto err;
	}

	udelay(1);

	info->bps_per_lane = a;

	pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n", current->pid,
		__LINE__, "phy_csi_path_cfg", "phy_csi_path_cfg", info->id,
		info->dw_phy_id);

	csi_phy_power_down(info, 0);
	csi_phy_testclr_init((struct csi_dt_info *)&info->ctrl_syscon);
	csi_controller_enable(info);
	csi_phy_init(info, info->id);

	base = s_csi_regbase[info->id];

	lane_val = ((info->dw_phy_id & ~1) == 5) ? 3 : ((lanes - 1) & 7);
	CSI_REG_MWR(info->id, 0x04, 7, lane_val);
	udelay(10);
	writel(0x1ffffff, base + 0x28);
	udelay(10);
	writel(0xffffff, base + 0x2c);
	udelay(10);
	writel(7, base + 0x78);

	if (csi_pattern_enable)
		csi_ipg_mode_cfg(info->id, 1);

	csi_reg_dump_flag = 0;

	return 0;

err:
	pr_err("csi_api: %d %d %s : fail to open csi api %d\n", current->pid,
	       __LINE__, __func__, ret);
	csi_api_close(0, id);

	return ret;
}

/* bring up the cphy clock/data recovery of one receiver */
void cphy_cdr_init(struct csi_dev *info, u32 phy_id)
{
	u8 val;

	phy_write(phy_id, 0x6e, 0x01, 0x01);
	phy_write(phy_id, 0x61, 0x82, 0xff);
	phy_write(phy_id, 0x3b, 0x01, 0x01);
	phy_write(phy_id, 0x62, 0x01, 0x01);
	phy_write(phy_id, 0x60, 0xf1, 0xff);
	phy_write(phy_id, 0x3c, 0xfb, 0x0c);

	phy_write(phy_id, 0x50, 0x40, 0x40);
	phy_write(phy_id, 0x50, 0x04, 0x04);
	phy_write(phy_id, 0x50, 0x01, 0x01);
	phy_write(phy_id, 0x50, 0x02, 0x02);

	phy_write(phy_id, 0x63, 0x08, 0x08);
	phy_write(phy_id, 0x63, 0x80, 0x80);
	phy_write(phy_id, 0x3b, 0x08, 0x08);

	phy_write(phy_id, 0x92, 0x08, 0x08);
	phy_write(phy_id, 0x92, 0x80, 0x80);
	phy_write(phy_id, 0xb2, 0x08, 0x08);
	phy_write(phy_id, 0xb2, 0x80, 0x80);
	phy_write(phy_id, 0xd2, 0x08, 0x08);
	phy_write(phy_id, 0xd2, 0x80, 0x80);

	phy_write(phy_id, 0x90, 0x40, 0x40);
	phy_write(phy_id, 0x90, 0x20, 0x20);
	phy_write(phy_id, 0xb0, 0x40, 0x40);
	phy_write(phy_id, 0xb0, 0x20, 0x20);
	phy_write(phy_id, 0xd0, 0x40, 0x40);
	phy_write(phy_id, 0xd0, 0x20, 0x20);

	phy_write(phy_id, 0x68, 0x01, 0x01);

	/* wait for the three lanes to report their pll as locked */
	do {
		phy_read(phy_id, 0x8f, &val);
		val >>= 7;
	} while (val != 1);

	phy_write(phy_id, 0x90, 0xdf, 0x20);

	do {
		phy_read(phy_id, 0xaf, &val);
		val >>= 7;
	} while (val != 1);

	phy_write(phy_id, 0xb0, 0xdf, 0x20);

	do {
		phy_read(phy_id, 0xcf, &val);
		val >>= 7;
	} while (val != 1);

	phy_write(phy_id, 0xd0, 0xdf, 0x20);

	phy_write(phy_id, 0x94, 0x10, 0x7f);
	phy_write(phy_id, 0xb4, 0x10, 0x7f);
	phy_write(phy_id, 0xd4, 0x10, 0x7f);
	phy_write(phy_id, 0x91, 0x06, 0x0f);
	phy_write(phy_id, 0xb1, 0x06, 0x0f);
	phy_write(phy_id, 0xd1, 0x06, 0x0f);
	phy_write(phy_id, 0x95, 0x40, 0x7f);
	phy_write(phy_id, 0xb5, 0x40, 0x7f);
	phy_write(phy_id, 0xd5, 0x40, 0x7f);

	phy_write(phy_id, 0x63, 0xf7, 0x08);
	phy_write(phy_id, 0x63, 0x7f, 0x80);
	phy_write(phy_id, 0x3b, 0xf7, 0x08);
	phy_write(phy_id, 0x92, 0xf7, 0x08);
	phy_write(phy_id, 0x92, 0x7f, 0x80);
	phy_write(phy_id, 0xb2, 0xf7, 0x08);
	phy_write(phy_id, 0xb2, 0x7f, 0x80);
	phy_write(phy_id, 0xd2, 0xf7, 0x08);
	phy_write(phy_id, 0xd2, 0x7f, 0x80);

	/* wait for the clock lane to settle, then calibrate the oscillator */
	do {
		phy_read(phy_id, 0x3e, &val);
		val = (val >> 4) & 1;
	} while (!val);

	cphy_osc_result_cal(phy_id, info->bps_per_lane);
}

/*
 * bring up the analog phy of one receiver.  The exact sequence depends on
 * the phy wired to that receiver.
 */
void csi_phy_init(struct csi_dev *info, u32 id)
{
	struct csi_hw_ctx *ctx;
	struct regmap *syscon;

	if (!info) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy ptr\n",
		       current->pid, __LINE__, __func__);
		return;
	}

	ctx = (struct csi_hw_ctx *)&info->ctrl_syscon;

	csi_ahb_reset(ctx, info->id);
	csi_reset_controller(id);

	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x08, 1, 1);

	csi_reset_phy(id);

	if (info->dw_phy_id <= 8 && ((1 << info->dw_phy_id) & 0x135))
		syscon = info->aon_apb2;
	else
		syscon = info->cam_ahb2;

	csi_phy_testclr(info->id, (struct csi_dt_info *)&info->ctrl_syscon);

	if (info->dw_phy_id > 9) {
		pr_err("csi_driver: %d %d %s : fail to get valid phy id %d\n",
		       current->pid, __LINE__, __func__, info->dw_phy_id);
		return;
	}

	switch (info->dw_phy_id) {
	case 0:
	case 1:
		CSI_REG_MWR(id, 0x70, 1, 0);
		CSI_REG_MWR(id, 0x48, 4, 4);

		phy_write(id, 0x62, 0x01, 0x01);
		phy_write(id, 0x6e, 0x01, 0x01);
		phy_write(id, 0x61, 0x82, 0x82);
		phy_write(id, 0x60, 0x81, 0x81);

		/* combo dphy needs its unused lanes powered down */
		if (info->priv == (void *)0xfffff) {
			pr_err("csi_driver: %d %d %s : combo dphy pn swap\n",
			       current->pid, __LINE__, __func__);
			phy_write(id, 0x01, 0x01, 0x01);
			phy_write(id, 0x3d, 0x80, 0x80);
			phy_write(id, 0x4d, 0x80, 0x80);
			phy_write(id, 0x5d, 0x80, 0x80);
			phy_write(id, 0x6d, 0x80, 0x80);
			phy_write(id, 0x7d, 0x80, 0x80);
		}

		phy_write(id, 0x90, 0x60, 0x60);
		phy_write(id, 0xb0, 0x60, 0x60);
		phy_write(id, 0xd0, 0x60, 0x60);
		udelay(10);
		phy_write(id, 0x90, 0x00, 0x60);
		phy_write(id, 0xb0, 0x00, 0x60);
		phy_write(id, 0xd0, 0x00, 0x60);

		CSI_REG_MWR(id, 0x48, 4, 0);
		break;

	case 2:
	case 3:
		CSI_REG_MWR(id, 0x70, 0x20, 0);
		CSI_REG_MWR(id, 0x08, 2, 2);
		CSI_REG_MWR(id, 0x0c, 2, 2);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_clr_cfg",
			"phy_csi_path_clr_cfg", info->id, info->dw_phy_id);

		csi_2p2l_2lane_phy_testclr((struct csi_dt_info *)ctx);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_cfg",
			"phy_csi_path_cfg", info->id, info->dw_phy_id);
		break;

	case 4:
	case 6:
		CSI_REG_MWR(id, 0x70, 0x60, 0x60);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_clr_cfg",
			"phy_csi_path_clr_cfg", info->id, info->dw_phy_id);

		csi_2p2l_2lane_phy_testclr((struct csi_dt_info *)ctx);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_cfg",
			"phy_csi_path_cfg", info->id, info->dw_phy_id);
		break;

	case 5:
	case 7:
		CSI_REG_MWR(id, 0x70, 0x20, 0x20);
		CSI_REG_MWR(id, 0x08, 2, 2);
		CSI_REG_MWR(id, 0x0c, 2, 2);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_clr_cfg",
			"phy_csi_path_clr_cfg", info->id, info->dw_phy_id);

		csi_2p2l_2lane_phy_testclr((struct csi_dt_info *)ctx);

		pr_info("csi_driver: %d %d %s : %s csi, id %d phy %d\n",
			current->pid, __LINE__, "phy_csi_path_cfg",
			"phy_csi_path_cfg", info->id, info->dw_phy_id);

		regmap_update_bits(syscon, 0x68, 0x1c, 0x1c);
		regmap_update_bits(syscon, 0xac, 0xe00, 0xe00);
		break;

	case 8:
	case 9:
		CSI_REG_MWR(id, 0x70, 1, 1);
		CSI_REG_MWR(id, 0x48, 4, 4);

		phy_write(id, 0x3c, 0x08, 0xff);

		regmap_update_bits(syscon, 0x40, 4, 4);

		cphy_cdr_init(info, id);

		phy_write(id, 0x69, 0x0c, 0x0c);
		phy_write(id, 0x3b, 0x09, 0x09);

		CSI_REG_MWR(id, 0x48, 4, 0);
		break;
	}
}

/*
 * enable or disable the ipg test pattern generator of one receiver.  When
 * enabled the pattern fills the first window with a fixed gradient.
 */
void csi_ipg_mode_cfg(u32 id, u32 on)
{
	if (id >= CSI_MAX_NUM)
		return;

	CSI_REG_MWR(id, 0x08, 1, 1);
	CSI_REG_MWR(id, 0x0c, 1, 1);
	CSI_REG_MWR(id, 0x10, 1, 1);

	if (!on) {
		CSI_REG_MWR(id, 0x14, 1, 0);
		goto out;
	}

	CSI_REG_MWR(id, 0x14, 0x3fe00000, 0x1b6 << 21);
	CSI_REG_MWR(id, 0x90, 3, 0);
	CSI_REG_MWR(id, 0x14, 0x1fe000, 0xc2 << 13);

	CSI_REG_MWR(id, 0x14, 0x1ff0, 0x124 << 4);
	CSI_REG_MWR(id, 0x90, 3, 0);
	CSI_REG_MWR(id, 0x14, 8, 8);
	CSI_REG_MWR(id, 0x14, 4, 0);
	CSI_REG_MWR(id, 0x14, 2, 2);

	/* window 0 geometry: width, height and stride */
	CSI_REG_MWR(id, 0x50, 0x3ff00000, 0);
	CSI_REG_MWR(id, 0x50, 0xffc00, 0);
	CSI_REG_MWR(id, 0x50, 0x3ff, 0x3ff);

	CSI_REG_MWR(id, 0x54, 0x3ff00000, 0);
	CSI_REG_MWR(id, 0x54, 0xffc00, 0xffc00);
	CSI_REG_MWR(id, 0x54, 0x3ff, 0);

	CSI_REG_MWR(id, 0x58, 0x3ff00000, 0x3ff00000);
	CSI_REG_MWR(id, 0x58, 0xffc00, 0);
	CSI_REG_MWR(id, 0x58, 0x3ff, 0);

	CSI_REG_MWR(id, 0x5c, 3, 2);

	CSI_REG_MWR(id, 0x6c, 0x1ffe000, 0x1ffe000);
	CSI_REG_MWR(id, 0x6c, 0x1fff, 0x1fff);

	CSI_REG_MWR(id, 0x14, 1, 1);

out:
	pr_info("csi_driver: %d %d %s : CSI IPG enable %d\n", current->pid,
		__LINE__, __func__, on);
}

/* read the phy id of one csi receiver from its dt node */
u32 csi_api_mipi_phy_cfg_init(struct device_node *np, u32 sensor_id)
{
	u32 phy_id = 0;

	if (of_property_read_u32(np, "sprd,phyid", &phy_id)) {
		pr_err("csi_api: %d %d %s : fail to get the sprd_phyid\n",
		       current->pid, __LINE__, __func__);
		return -1;
	}

	if (phy_id >= 0xa) {
		pr_err("csi_api: %d %d %s : %s:fail to parse phyid : phyid:%u\n",
		       current->pid, __LINE__, __func__, __func__, phy_id);
		return -1;
	}

	pr_info("csi_api: %d %d %s : sensor_id %d mipi_phy:phy_id:%u\n",
		current->pid, __LINE__, __func__, sensor_id, phy_id);

	return phy_id;
}

/* allocate and fill the per receiver context of one csi dt node */
int csi_api_dt_node_init(struct device *dev, struct device_node *np,
			 u32 sensor_id, u32 phy_id)
{
	struct resource res = { 0 };
	struct csi_dev *info;
	struct regmap *syscon;
	void __iomem *base;
	int ret = -EINVAL;

	info = devm_kmalloc(dev, sizeof(*info) + 0x10, GFP_KERNEL);
	if (!info)
		return -EINVAL;

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("csi_api: %d %d %s : csi2_init:fail to get address info\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}

	base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!base || IS_ERR(base)) {
		pr_err("csi_api: %d %d %s : csi_dt_init:fail to get csi regbase\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}

	info->reg_base = base;
	info->dw_phy_id = phy_id;

	pr_info("csi_api: %d %d %s : csi node name %s\n", current->pid,
		__LINE__, __func__, np->name);

	if (of_property_read_u32_index(np, "sprd,csi-id", 0, &info->id)) {
		pr_err("csi_api: %d %d %s : csi2_init:fail to get csi-id\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}

	pr_info("csi_api: %d %d %s : csi %d ,phy addr:0x%llx ,size:0x%llx , reg %lx\n",
		current->pid, __LINE__, __func__, info->id,
		(unsigned long long)res.start,
		(unsigned long long)resource_size(&res),
		(unsigned long)info->reg_base);

	info->clk_csi_eb = of_clk_get_by_name(np, "clk_csi_eb");
	info->clk_mipi_src_eb = of_clk_get_by_name(np, "mipi_csi_src_eb");

	syscon = syscon_regmap_lookup_by_phandle(np, "sprd,cam-ahb-syscon");
	if (!syscon || IS_ERR(syscon)) {
		pr_err("csi_api: %d %d %s : csi_dt_init:fail to get cam-ahb-syscon\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}
	info->ctrl_syscon = syscon;

	syscon = syscon_regmap_lookup_by_phandle(np, "sprd,aon-apb-syscon");
	if (!syscon || IS_ERR(syscon)) {
		pr_err("csi_api: %d %d %s : csi_dt_init:fail to get aon-apb-syscon\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}
	info->phy_syscon = syscon;

	syscon = syscon_regmap_lookup_by_phandle(np, "sprd,anlg_phy_g4_controller");
	if (!syscon || IS_ERR(syscon)) {
		pr_err("csi_api: %d %d %s : csi_dt_init:fail to get anlg_phy_g4_controller\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}
	info->cam_ahb2 = syscon;

	syscon = syscon_regmap_lookup_by_phandle(np, "sprd,anlg_phy_g4l_controller");
	if (!syscon || IS_ERR(syscon)) {
		pr_err("csi_api: %d %d %s : csi_dt_init:fail to get anlg_phy_g4l_controller\n",
		       current->pid, __LINE__, __func__);
		return ret;
	}
	info->aon_apb2 = syscon;

	if (info->id >= CSI_MAX_NUM || sensor_id >= CSI_MAX_NUM)
		return ret;

	s_csi_regbase[info->id] = info->reg_base;
	if (info->id < CSI_DUMP_MAX_NUM)
		csi_dump_regbase[info->id] = NULL;

	s_csi_dt_info_p[sensor_id] = info;

	pr_info("csi_api: %d %d %s : csi dt info:sensor_id :%d, csi_info:0x%p\n",
		current->pid, __LINE__, __func__, sensor_id, info);

	return 0;
}
