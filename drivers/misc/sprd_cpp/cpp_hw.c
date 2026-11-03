// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- clock / syscon / irq layer (cpp_hw.c).
 *
 * 重建自 vendor `cpp_lite/hw/lite_r6p0/cpp_hw.c`。
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "cpp_lite.h"

#define HW_ERR(fmt, ...) \
	pr_err("CPP_HW: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define INT_ERR(fmt, ...) \
	pr_err("CPP_INT: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

/* CPP 中断状态寄存器（vendor 里在 hw 结构 +0x54 附近派生） */
#define CPP_REG_INT_STS		0x030
#define CPP_REG_INT_MASK	0x034
#define CPP_REG_INT_CLR		0x038

#define CPP_INT_SCALE_DONE	BIT(0)
#define CPP_INT_ROT_DONE	BIT(1)
#define CPP_INT_DMA_DONE	BIT(2)
#define CPP_INT_MMU_ERR		BIT(3)

void __iomem *g_cpp_base;

int cpphw_probe(struct platform_device *pdev, struct cpp_hw_info *hw)
{
	struct device_node *nd;
	void __iomem *base;
	int irq;

	if (!pdev || !hw) {
		HW_ERR("fail to get pdev %p, hw_info %p\n", pdev, hw);
		return -EINVAL;
	}

	nd = pdev->dev.of_node;

	hw->dev = &pdev->dev;
	hw->hw_dev = &pdev->dev;

	hw->clk_eb = devm_clk_get(&pdev->dev, "cpp_eb");
	if (IS_ERR_OR_NULL(hw->clk_eb)) {
		HW_ERR("fail to get cpp eb clk\n");
		return -ENODEV;
	}

	hw->clk_ahb = devm_clk_get(&pdev->dev, "cpp_ahb");
	if (IS_ERR_OR_NULL(hw->clk_ahb)) {
		HW_ERR("fail to get cpp ahb clk\n");
		return -ENODEV;
	}

	hw->clk_ahb_parent = clk_get_parent(hw->clk_ahb);
	if (!hw->clk_ahb_parent) {
		HW_ERR("fail to get cpp ahb parent clk\n");
		return -ENODEV;
	}

	hw->cam_ahb_gpr = syscon_regmap_lookup_by_phandle(nd,
					"sprd,cam-ahb-syscon");
	if (IS_ERR_OR_NULL(hw->cam_ahb_gpr)) {
		HW_ERR("fail to get syscon %s %d, %p\n",
		       "sprd,cam-ahb-syscon", 0, hw->cam_ahb_gpr);
		hw->cam_ahb_gpr = NULL;
	}

	hw->mm_ahb_gpr = syscon_regmap_lookup_by_phandle_args(nd,
					"mm_ahb_gpr", 0, NULL);
	if (IS_ERR_OR_NULL(hw->mm_ahb_gpr)) {
		HW_ERR("fail to get mm_ahb_gpr\n");
		hw->mm_ahb_gpr = NULL;
	}

	base = of_iomap(nd, 0);
	if (!base) {
		HW_ERR("fail to get cpp base\n");
		return -ENOMEM;
	}
	hw->base = base;
	g_cpp_base = base;

	if (of_property_read_u32(nd, "sprd,cpp-qos", &hw->qos_reg))
		hw->qos_reg = 0;

	irq = irq_of_parse_and_map(nd, 0);
	if (!irq) {
		HW_ERR("fail to get cpp irq %d\n", irq);
		return -ENODEV;
	}
	hw->irq = irq;

	dev_dbg(&pdev->dev, "sprd cpp probe pdev name %s\n",
		dev_name(&pdev->dev));

	return 0;
}

int cpphw_ioctl(u32 cmd, void *arg)
{
	struct cpp_hw_info *hw = arg;

	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	switch (cmd) {
	case SPRD_CPP_IO_OPEN_ROT:
	case SPRD_CPP_IO_OPEN_SCALE:
	case SPRD_CPP_IO_OPEN_DMA:
		return cpphw_clk_enable(hw);
	case SPRD_CPP_IO_CLOSE_ROT:
	case SPRD_CPP_IO_STOP_SCALE:
		return cpphw_clk_disable(hw);
	default:
		return 0;
	}
}

int cpphw_bypass_path_support(void)
{
	return 1;
}

int cpphw_slice_support(void)
{
	return 1;
}

int cpphw_zoom_up_support(void)
{
	return 1;
}

int cpphw_qos_set(struct cpp_hw_info *hw, u32 arqos, u32 awrqos)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	hw->arqos = arqos;
	hw->awrqos = awrqos;

	if (!hw->mm_ahb_gpr || !hw->qos_reg)
		return 0;

	regmap_update_bits(hw->mm_ahb_gpr, hw->qos_reg,
			   0xff00, arqos << 8);
	regmap_update_bits(hw->mm_ahb_gpr, hw->qos_reg,
			   0xff, awrqos);

	dev_dbg(hw->dev, "awrqos %d arqos %d\n", awrqos, arqos);

	return 0;
}

int cpphw_mmu_set(struct cpp_hw_info *hw)
{
	if (!hw || !hw->base) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	/* 使能 cpp 侧的 iommu bypass 与 page fault 中断 */
	writel(0x3, hw->base + CPP_REG_INT_MASK);

	return 0;
}

static void cpphw_reset_bit(struct cpp_hw_info *hw, int bit, u32 delay_us)
{
	u32 val;

	if (!hw || !hw->base)
		return;

	val = readl(hw->base + CPP_REG_AHB_RESET);
	val |= BIT(bit);
	writel(val, hw->base + CPP_REG_AHB_RESET);

	__const_udelay(delay_us);

	val = readl(hw->base + CPP_REG_AHB_RESET);
	val &= ~BIT(bit);
	writel(val, hw->base + CPP_REG_AHB_RESET);

	__const_udelay(delay_us);
}

int cpphw_module_reset(struct cpp_hw_info *hw)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpphw_reset_bit(hw, 0, 0x418958);

	if (hw->cam_ahb_gpr)
		regmap_update_bits(hw->cam_ahb_gpr, hw->cam_ahb_gpr_reg,
				   hw->mm_ahb_gpr_mask, 0);

	return 0;
}

int cpphw_scale_reset(struct cpp_hw_info *hw)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpphw_reset_bit(hw, 1, 0x147aeb8);

	return 0;
}

int cpphw_rot_reset(struct cpp_hw_info *hw)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpphw_reset_bit(hw, 2, 0x147aeb8);

	return 0;
}

int cpphw_dma_reset(struct cpp_hw_info *hw)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpphw_reset_bit(hw, 3, 0x147aeb8);

	return 0;
}

int cpphw_clk_enable(struct cpp_hw_info *hw)
{
	int ret;

	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(hw->clk_eb);
	if (ret) {
		HW_ERR("fail to enable cpp eb\n");
		return ret;
	}

	ret = clk_set_parent(hw->clk_ahb, hw->clk_ahb_parent);
	if (ret)
		HW_ERR("fail to set cpp ahb parent\n");

	ret = clk_prepare_enable(hw->clk_ahb);
	if (ret) {
		HW_ERR("fail to enable cpp ahb\n");
		return ret;
	}

	return 0;
}

int cpphw_clk_disable(struct cpp_hw_info *hw)
{
	if (!hw) {
		HW_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	clk_disable_unprepare(hw->clk_ahb);
	clk_disable_unprepare(hw->clk_eb);

	return 0;
}

/* ------------------------------------------------------------------ */
/* 中断                                                                */
/* ------------------------------------------------------------------ */
int cppint_scale_isr(void *data)
{
	struct cpp_scale_dev *sc = data;

	if (!sc) {
		INT_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	complete(&sc->done);

	return 0;
}

int cppint_rot_isr(void *data)
{
	struct cpp_rot_dev *rot = data;

	if (!rot) {
		INT_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	complete(&rot->done);

	return 0;
}

int cppint_dma_isr(void *data)
{
	struct cpp_dma_dev *dm = data;

	if (!dm) {
		INT_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	complete(&dm->done);

	return 0;
}

static void cppint_iommu_reg_trace(struct cpp_device *dev)
{
	unsigned long base = (unsigned long)g_cpp_base;
	int i;

	INT_ERR("cppint_iommu_reg_trace\n");
	for (i = 0; i < 4; i++)
		INT_ERR("0x%lx: 0x%x 0x%x 0x%x 0x%x\n",
			(unsigned long)(base + i * 0x10),
			readl(g_cpp_base + i * 0x10),
			readl(g_cpp_base + i * 0x10 + 4),
			readl(g_cpp_base + i * 0x10 + 8),
			readl(g_cpp_base + i * 0x10 + 0xc));
}

int cppint_scale_done(void *data)
{
	struct cpp_scale_dev *sc = data;

	if (!sc)
		return -EINVAL;

	sc->running = false;

	return 0;
}

int cppint_rot_done(void *data)
{
	struct cpp_rot_dev *rot = data;

	if (!rot)
		return -EINVAL;

	rot->running = false;

	return 0;
}

int cppint_dma_done(void *data)
{
	struct cpp_dma_dev *dm = data;

	if (!dm)
		return -EINVAL;

	dm->running = false;

	return 0;
}

irqreturn_t cppint_isr_root(int irq, void *data)
{
	struct cpp_device *dev = data;
	u32 sts;

	if (!dev) {
		INT_ERR("fail to get valid input ptr\n");
		return IRQ_NONE;
	}

	sts = readl(g_cpp_base + CPP_REG_INT_STS);
	if (!sts)
		return IRQ_NONE;

	/* 写 1 清中断 */
	writel(sts, g_cpp_base + CPP_REG_INT_CLR);

	if (sts & CPP_INT_SCALE_DONE) {
		cppint_scale_isr(&dev->scale);
		cppint_scale_done(&dev->scale);
	}
	if (sts & CPP_INT_ROT_DONE) {
		cppint_rot_isr(&dev->rot);
		cppint_rot_done(&dev->rot);
	}
	if (sts & CPP_INT_DMA_DONE) {
		cppint_dma_isr(&dev->dma);
		cppint_dma_done(&dev->dma);
	}
	if (sts & CPP_INT_MMU_ERR) {
		INT_ERR("fail to run iommu, int 0x%x\n", sts);
		cppint_iommu_reg_trace(dev);
	}

	return IRQ_HANDLED;
}

int cpp_int_irq_request(struct cpp_device *dev)
{
	int ret;

	if (!dev || !dev->hw) {
		INT_ERR("fail to get valid input ptr cpp_handle %p\n", dev);
		return -EINVAL;
	}

	init_completion(&dev->scale.done);
	init_completion(&dev->rot.done);
	init_completion(&dev->dma.done);

	mutex_init(&dev->scale.lock);
	mutex_init(&dev->rot.lock);
	mutex_init(&dev->dma.lock);

	ret = devm_request_threaded_irq(dev->dev, dev->hw->irq, NULL,
					cppint_isr_root, IRQF_ONESHOT,
					"cpp", dev);
	if (ret) {
		INT_ERR("fail to install IRQ %d\n", ret);
		return ret;
	}

	return 0;
}

int cpp_int_irq_free(struct cpp_device *dev)
{
	if (!dev)
		return -EINVAL;

	devm_free_irq(dev->dev, dev->hw->irq, dev);

	return 0;
}
