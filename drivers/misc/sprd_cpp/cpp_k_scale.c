// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- scaler register interface (cpp_k_scale.c).
 *
 * 重建自 vendor `cpp_lite/hw/lite_r6p0/cpp_k_scale.c`。
 * 每个接口把 `struct cpp_scale_dev` 里的配置字段（vendor 偏移见注释）
 * 拼进 CPP 的 scaler 寄存器。寄存器偏移与位域都取自反汇编实测。
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sched.h>

#include "cpp_lite.h"

#define SCALE_ERR(fmt, ...) \
	pr_err("SCALE_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define SCALE_DBG(fmt, ...) \
	pr_debug("SCALE_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

static inline void cpp_write(unsigned int off, u32 val)
{
	writel(val, g_cpp_base + off);
}

static inline u32 cpp_read(unsigned int off)
{
	return readl(g_cpp_base + off);
}

/* 用 clr/mask 的方式改写位域，等价于二进制里的 bfxil/bfi 组合 */
static inline void cpp_update(unsigned int off, u32 mask, u32 val)
{
	cpp_write(off, (cpp_read(off) & ~mask) | (val & mask));
}

int cpp_k_scale_dev_start(struct cpp_scale_dev *sc)
{
	unsigned long flags;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&sc->slock, flags);
	*sc->ctrl |= CPP_SCALE_EB;
	spin_unlock_irqrestore(&sc->slock, flags);

	return 0;
}

int cpp_k_scale_dev_stop(struct cpp_scale_dev *sc)
{
	unsigned long flags;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&sc->slock, flags);
	*sc->ctrl &= ~CPP_SCALE_EB;
	spin_unlock_irqrestore(&sc->slock, flags);

	return 0;
}

int cpp_k_scale_dev_enable(struct cpp_scale_dev *sc)
{
	unsigned long flags;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&sc->slock, flags);
	*sc->ctrl |= CPP_SCALE_EB;
	spin_unlock_irqrestore(&sc->slock, flags);

	return 0;
}

int cpp_k_scale_dev_disable(struct cpp_scale_dev *sc)
{
	unsigned long flags;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&sc->slock, flags);
	*sc->ctrl &= ~CPP_SCALE_EB;
	spin_unlock_irqrestore(&sc->slock, flags);

	return 0;
}

int cpp_k_scale_src_pitch_set(struct cpp_scale_dev *sc)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_SRC_PITCH, CPP_PITCH_MASK,
		   sc->cfg.src_pitch & CPP_PITCH_MASK);

	return 0;
}

int cpp_k_scale_des_pitch_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.dst_pitch & CPP_PITCH_MASK) << CPP_PITCH_SHIFT;
	cpp_update(CPP_REG_DST_PITCH, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	cpp_update(CPP_REG_DST_PITCH, CPP_PITCH_MASK,
		   sc->cfg.dst_pitch_uv & CPP_PITCH_MASK);

	return 0;
}

int cpp_k_scale_input_rect_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_IN_RECT, CPP_RECT_WH_MASK,
		   sc->cfg.in_rect_x & CPP_RECT_WH_MASK);
	val = (sc->cfg.in_rect_w & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_IN_RECT, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	val = (sc->cfg.in_rect_y & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_IN_RECT_Y, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	cpp_update(CPP_REG_IN_RECT_Y, CPP_RECT_WH_MASK,
		   sc->cfg.in_rect_h & CPP_RECT_WH_MASK);

	return 0;
}

int cpp_k_scale_output_rect_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_OUT_RECT, CPP_RECT_WH_MASK,
		   sc->cfg.out_rect_x & CPP_RECT_WH_MASK);
	val = (sc->cfg.out_rect_w & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_OUT_RECT, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	val = (sc->cfg.out_rect_y & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_OUT_RECT_Y, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	cpp_update(CPP_REG_OUT_RECT_Y, CPP_RECT_WH_MASK,
		   sc->cfg.out_rect_h & CPP_RECT_WH_MASK);

	return 0;
}

int cpp_k_scale_input_format_set(struct cpp_scale_dev *sc)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_FMT, CPP_FMT_IN_MASK, sc->cfg.in_format);

	return 0;
}

int cpp_k_scale_output_format_set(struct cpp_scale_dev *sc)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_FMT, CPP_FMT_IN_MASK << CPP_FMT_OUT_SHIFT,
		   sc->cfg.out_format << CPP_FMT_OUT_SHIFT);

	return 0;
}

int cpp_k_scale_deci_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.deci_x & 0xf) << CPP_FMT_DECI_X_SHIFT;
	cpp_update(CPP_REG_FMT, 0xf << CPP_FMT_DECI_X_SHIFT, val);
	val = (sc->cfg.deci_y & 0xf) << CPP_FMT_DECI_Y_SHIFT;
	cpp_update(CPP_REG_FMT, 0xf << CPP_FMT_DECI_Y_SHIFT, val);

	return 0;
}

int cpp_k_scale_input_endian_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.in_endian & 0xff) << 8;
	cpp_update(CPP_REG_CFG_1C, 0xff00, val);
	val = (sc->cfg.in_endian_y & 0xff) << 24;
	cpp_update(CPP_REG_CFG_1C, 0xff000000, val);

	return 0;
}

int cpp_k_scale_output_endian_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_CFG_1C, 0xff, sc->cfg.out_endian & 0xff);
	val = (sc->cfg.out_endian_y & 0xff) << 16;
	cpp_update(CPP_REG_CFG_1C, 0xff0000, val);

	return 0;
}

int cpp_k_scale_burst_gap_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.burst_gap & 0xffff) << 16;
	cpp_update(CPP_REG_CFG_20, 0xffff0000, val);
	cpp_update(CPP_REG_CFG_20, 0xffff, sc->cfg.burst_len);

	return 0;
}

int cpp_k_scale_bpen_set(struct cpp_scale_dev *sc)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_FMT, CPP_FMT_BPEN,
		   sc->cfg.bpen ? CPP_FMT_BPEN : 0);

	return 0;
}

int cpp_k_scale_ini_phase_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.ini_phase_x & 0xf) << 16;
	cpp_update(CPP_REG_INI_PHASE, 0xf0000, val);
	cpp_update(CPP_REG_INI_PHASE, 0x1fff, sc->cfg.ini_phase_y & 0x1fff);
	val = (sc->cfg.ini_phase_x_y & 0xf) << 16;
	cpp_update(CPP_REG_INI_PHASE_Y, 0xf0000, val);
	cpp_update(CPP_REG_INI_PHASE_Y, 0x1fff,
		   sc->cfg.ini_phase_y_y & 0x1fff);

	return 0;
}

int cpp_k_scale_tap_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_TAP, 0xff, sc->cfg.tap & 0xff);
	val = (sc->cfg.tap_y & 0xff) << 8;
	cpp_update(CPP_REG_TAP, 0xff00, val);

	return 0;
}

int cpp_k_scale_offset_size_set(struct cpp_scale_dev *sc)
{
	u32 val;

	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	val = (sc->cfg.offset_x & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_OFFSET_SIZE, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	cpp_update(CPP_REG_OFFSET_SIZE, CPP_RECT_WH_MASK,
		   sc->cfg.offset_y & CPP_RECT_WH_MASK);
	val = (sc->cfg.size_x & CPP_RECT_WH_MASK) << CPP_RECT_WH_SHIFT;
	cpp_update(CPP_REG_OFFSET_SIZE_Y, CPP_RECT_WH_MASK << CPP_RECT_WH_SHIFT, val);
	cpp_update(CPP_REG_OFFSET_SIZE_Y, CPP_RECT_WH_MASK,
		   sc->cfg.size_y & CPP_RECT_WH_MASK);

	return 0;
}

int cpp_k_scale_addr_set(struct cpp_scale_dev *sc)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_write(CPP_REG_SRC_ADDR_Y, sc->cfg.src_addr_y);
	cpp_write(CPP_REG_SRC_ADDR_UV, sc->cfg.src_addr_uv);
	cpp_write(CPP_REG_DST_ADDR_Y, sc->cfg.dst_addr_y);
	cpp_write(CPP_REG_DST_ADDR_UV, sc->cfg.dst_addr_uv);
	cpp_write(CPP_REG_BP_ADDR_Y, sc->cfg.bp_addr_y);

	SCALE_DBG("iommu_src y:0x%lx, uv:0x%lx\n",
		  (unsigned long)sc->cfg.src_addr_y,
		  (unsigned long)sc->cfg.src_addr_uv);
	SCALE_DBG("iommu_dst y:0x%lx, uv:0x%lx\n",
		  (unsigned long)sc->cfg.dst_addr_y,
		  (unsigned long)sc->cfg.dst_addr_uv);
	SCALE_DBG("iommu_dst_bp y:0x%lx, uv:0x%lx\n",
		  (unsigned long)sc->cfg.bp_addr_y, 0UL);

	return 0;
}

int cpp_k_scale_clk_switch(struct cpp_scale_dev *sc, bool on)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	cpp_update(CPP_REG_FMT, CPP_FMT_CLK_SWITCH,
		   on ? CPP_FMT_CLK_SWITCH : 0);

	return 0;
}

/* 三个 tap 系数表：luma / chrima / v 各占一段连续寄存器 */
static int cpp_k_scale_coeff_set(struct cpp_scale_dev *sc, u32 off,
				 const u32 *coeff, u32 size)
{
	u32 i, cnt;

	cnt = size & 0x3ffff;
	if (cnt > 0x100)
		cnt = 0x100;

	for (i = 0; i < cnt; i++)
		cpp_write(off + i * 4, coeff[i]);

	return 0;
}

int cpp_k_scale_luma_hcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff,
				u32 size)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	return cpp_k_scale_coeff_set(sc, CPP_REG_LUMA_HCOEFF, coeff, size);
}

int cpp_k_scale_chrima_hcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff,
				  u32 size)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	return cpp_k_scale_coeff_set(sc, CPP_REG_CHRIMA_HCOEFF, coeff, size);
}

int cpp_k_scale_vcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff, u32 size)
{
	if (!sc) {
		SCALE_ERR("fail to get valid input ptr\n");
		return -EFAULT;
	}

	return cpp_k_scale_coeff_set(sc, CPP_REG_VCOEFF, coeff, size);
}

void cpp_k_scale_reg_trace(struct cpp_scale_dev *sc)
{
	int i;

	if (!sc) {
		SCALE_ERR("fail to get valid arg\n");
		return;
	}

	SCALE_DBG("CPP:Scaler Register list\n");
	for (i = 0; i < 0x20; i++)
		SCALE_DBG("reg[0x%02x] = 0x%08x\n", i * 4,
			  cpp_read(i * 4));
}
