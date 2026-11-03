// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- hardware driver layer (cpp_drv.c).
 *
 * 重建自 vendor `cpp_lite/src/cpp_drv.c`（.text 0x308 ~ 0x4dd0）。
 * 这一层把 cpp_core/cpp_ioctl 的请求翻译成 cpp_k_* 寄存器操作，
 * 并负责 SG 表 / IOMMU 地址、时钟、复位与 QoS 的设置。
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "cpp_lite.h"

#define DRV_ERR(fmt, ...) \
	pr_err("CPP_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define DRV_DBG(fmt, ...) \
	pr_debug("CPP_DRV: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

/* CPP 支持的最大像素尺寸 */
#define CPP_SCALE_MAX_WIDTH	4096
#define CPP_SCALE_MAX_HEIGHT	4096
#define CPP_SCALE_MIN_WIDTH	16
#define CPP_SCALE_MIN_HEIGHT	8

/* ------------------------------------------------------------------ */
/* 资源绑定                                                            */
/* ------------------------------------------------------------------ */
int cpp_drv_get_cpp_res(struct cpp_device *dev, struct cpp_hw_info *hw)
{
	if (!dev || !hw) {
		DRV_ERR("fail to get valid input dev %p, hw  %p\n", dev, hw);
		return -EINVAL;
	}

	/* vendor: 把 hw 的 base/clk/irq 绑到设备上下文 */
	dev->hw = hw;
	hw->dev = dev->dev;
	if (hw->base)
		g_cpp_base = hw->base;

	return 0;
}

int cppdrv_ioctl(u32 cmd, void *arg)
{
	if (!arg) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	switch (cmd) {
	case SPRD_CPP_IO_OPEN_ROT:
	case SPRD_CPP_IO_CLOSE_ROT:
	case SPRD_CPP_IO_START_ROT:
	case SPRD_CPP_IO_OPEN_SCALE:
	case SPRD_CPP_IO_START_SCALE:
	case SPRD_CPP_IO_STOP_SCALE:
	case SPRD_CPP_IO_OPEN_DMA:
	case SPRD_CPP_IO_START_DMA:
	case SPRD_CPP_IO_SCALE_CAPABILITY:
		break;
	default:
		DRV_ERR("fail to check cmd 0x%x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* 能力查询                                                            */
/* ------------------------------------------------------------------ */
int cppdrv_get_support_info(void *out)
{
	struct cpp_support_info {
		u32 bypass_path;
		u32 slice;
		u32 zoom_up;
		u32 reserved;
	} *info = out;

	if (!info) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	info->bypass_path = cpphw_bypass_path_support();
	info->slice = cpphw_slice_support();
	info->zoom_up = cpphw_zoom_up_support();
	info->reserved = 0;

	return 0;
}

int cppdrv_scale_max_size_get(void *in, void *out)
{
	u32 *max = out;

	if (!in || !out) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	max[0] = CPP_SCALE_MAX_WIDTH;
	max[1] = CPP_SCALE_MAX_HEIGHT;

	return 0;
}

int cppdrv_scale_capability_get(void *out)
{
	struct cpp_scale_capability {
		u32 min_w;
		u32 min_h;
		u32 max_w;
		u32 max_h;
		u32 scale_up;
		u32 scale_down;
		u32 slice;
	} *cap = out;

	if (!cap) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}
	if (!g_cpp_base) {
		DRV_ERR("fail to get valid hw %p\n", g_cpp_base);
		return -ENODEV;
	}

	cap->min_w = CPP_SCALE_MIN_WIDTH;
	cap->min_h = CPP_SCALE_MIN_HEIGHT;
	cap->max_w = CPP_SCALE_MAX_WIDTH;
	cap->max_h = CPP_SCALE_MAX_HEIGHT;
	cap->scale_up = 1;
	cap->scale_down = 1;
	cap->slice = cpphw_slice_support();

	DRV_ERR("fail to upscale src.w:%d h:%d dst w:%d h:%d\n",
		cap->min_w, cap->min_h, cap->max_w, cap->max_h);

	return 0;
}

/* ------------------------------------------------------------------ */
/* 参数检查                                                            */
/* ------------------------------------------------------------------ */
int cppdrv_scale_param_check(void *arg)
{
	struct cpp_scale_param *p = arg;

	if (!p) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!p->src_w || !p->src_h || !p->dst_w || !p->dst_h) {
		DRV_ERR("fail to get valid input or output size:%d %d %d %d\n",
			p->src_w, p->src_h, p->dst_w, p->dst_h);
		return -EINVAL;
	}

	if (p->src_w > CPP_SCALE_MAX_WIDTH || p->src_h > CPP_SCALE_MAX_HEIGHT ||
	    p->dst_w > CPP_SCALE_MAX_WIDTH || p->dst_h > CPP_SCALE_MAX_HEIGHT) {
		DRV_ERR("fail to get valid input size %d %d %d %d %d %d\n",
			p->src_w, p->src_h, p->dst_w, p->dst_h,
			p->slice_num, p->slice_height);
		return -EINVAL;
	}

	if (p->dst_w & 7) {
		DRV_ERR("fail to get dst width 8 byte align: %d\n", p->dst_w);
		return -EINVAL;
	}

	if (p->src_w & 3) {
		pr_info("CPP_DRV: %d %d %s : adjust src width align 4: %d\n",
			current->pid, __LINE__, __func__, p->src_w & ~3);
		p->src_w &= ~3;
	}
	if (p->src_h & 1) {
		pr_info("CPP_DRV: %d %d %s : adjust src height align 2: %d\n",
			current->pid, __LINE__, __func__, p->src_h & ~1);
		p->src_h &= ~1;
	}

	return 0;
}

int cppdrv_scale_slice_param_check(void *arg)
{
	struct cpp_scale_param *p = arg;
	u32 slice_h;

	if (!p) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!p->slice_num)
		return 0;

	slice_h = (p->dst_h + p->slice_num - 1) / p->slice_num;
	slice_h = (slice_h + 1) & ~1UL;

	if (slice_h > p->dst_h) {
		DRV_ERR("[src.h:%d >= intrim.h:%d + intrim.x:%d]\n",
			p->src_h, slice_h, 0);
		DRV_ERR("[src.w:%d >= intrim.w:%d + intrim.x:%d]\n",
			p->src_w, p->dst_w, 0);
		return -EINVAL;
	}

	if (slice_h & 1) {
		DRV_ERR("fail to check sc_des align h:%d,format:%d\n",
			slice_h, p->out_format);
		return -EINVAL;
	}

	if (p->src_pitch & 0xf) {
		DRV_ERR("fail to checkpitch %d\n", p->src_pitch);
		return -EINVAL;
	}

	DRV_DBG("cppdrv_scale_slice_param_check\n");

	return 0;
}

int cppdrv_scale_slice_param_set(void *arg)
{
	struct cpp_scale_param *p = arg;

	if (!p) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	p->slice_height = (p->dst_h + p->slice_num - 1) / p->slice_num;

	return 0;
}

/* ------------------------------------------------------------------ */
/* scaler 寄存器                                                   */
/* ------------------------------------------------------------------ */
int cppdrv_scale_cfg_param_set(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpp_k_scale_src_pitch_set(sc);
	cpp_k_scale_des_pitch_set(sc);
	cpp_k_scale_input_rect_set(sc);
	cpp_k_scale_output_rect_set(sc);
	cpp_k_scale_input_format_set(sc);
	cpp_k_scale_output_format_set(sc);
	cpp_k_scale_deci_set(sc);
	cpp_k_scale_input_endian_set(sc);
	cpp_k_scale_output_endian_set(sc);
	cpp_k_scale_burst_gap_set(sc);
	cpp_k_scale_bpen_set(sc);
	cpp_k_scale_ini_phase_set(sc);
	cpp_k_scale_tap_set(sc);
	cpp_k_scale_offset_size_set(sc);
	cpp_k_scale_addr_set(sc);

	return 0;
}

int cppdrv_scale_set_regs(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	/* 输入/输出格式合法性 */
	if (!sc->cfg.in_format) {
		DRV_ERR("fail to get valid input format\n");
		return -EINVAL;
	}
	if (!sc->cfg.out_format) {
		DRV_ERR("fail to get valid output format\n");
		return -EINVAL;
	}
	if (!sc->cfg.in_rect_w || !sc->cfg.in_rect_h) {
		DRV_ERR("fail to set valid input rect\n");
		return -EINVAL;
	}
	if (!sc->cfg.dst_addr_y) {
		DRV_ERR("fail to get valid output addr\n");
		return -EINVAL;
	}

	cppdrv_scale_cfg_param_set(sc);

	return 0;
}

int cppdrv_sc_reg_trace(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpp_k_scale_reg_trace(sc);

	return 0;
}

/* ------------------------------------------------------------------ */
/* scaler 启停                                                         */
/* ------------------------------------------------------------------ */
int cppdrv_scale_enable(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (cppdrv_clk_eb()) {
		DRV_ERR("fail to eb scl\n");
		return -EIO;
	}

	return cpp_k_scale_dev_enable(sc);
}

int cppdrv_scale_start(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!sc->hw) {
		DRV_ERR("fail to get valid drv_private %p, hw %p\n", sc, sc->hw);
		return -EINVAL;
	}

	if (cppdrv_scale_set_regs(sc))
		return -EINVAL;

	if (cpp_k_scale_dev_start(sc)) {
		DRV_ERR("fail to start scl\n");
		return -EIO;
	}

	return 0;
}

int cppdrv_scale_stop(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!sc->hw) {
		DRV_ERR("fail to get valid drv_private %p, hw %p\n", sc, sc->hw);
		return -EINVAL;
	}

	cpp_k_scale_dev_stop(sc);

	return 0;
}

int cppdrv_scale_sl_stop(void *arg)
{
	struct cpp_scale_dev *sc = arg;

	if (!sc) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!sc->hw) {
		DRV_ERR("fail to get valid drv_private %p, hw %p\n", sc, sc->hw);
		return -EINVAL;
	}

	if (cpp_k_scale_dev_disable(sc)) {
		DRV_ERR("fail to stop scl\n");
		return -EIO;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* rotator                                                             */
/* ------------------------------------------------------------------ */
int cppdrv_rot_parm_check(void *arg)
{
	struct cpp_rot_param *p = arg;

	if (!p) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!p->src_addr_y || !p->dst_addr_y) {
		DRV_ERR("fail to get aligned addr\n");
		return -EINVAL;
	}

	if (!p->src_w || !p->src_h) {
		DRV_ERR("fail to get alogned width and height:%u, %u\n",
			p->src_w, p->src_h);
		return -EINVAL;
	}

	if (!p->format) {
		DRV_ERR("fail to get invalid image format %d\n", p->format);
		return -EINVAL;
	}

	return 0;
}

int cppdrv_rot_format_get(u32 format)
{
	/* 0: yuv420 2 plane, 1: yuv422, 2: rgb888 ... */
	switch (format) {
	case 0:
	case 1:
	case 2:
		return format;
	default:
		return -EINVAL;
	}
}

int cppdrv_rot_is_end(void)
{
	u32 val = readl(g_cpp_base + CPP_REG_ROT_CFG3);

	return !!(val & BIT(31));
}

int cppdrv_rot_y_parm_set(void *arg)
{
	struct cpp_rot_dev *rot = arg;

	if (!rot) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	writel(rot->src_addr_y, g_cpp_base + CPP_REG_ROT_CFG);
	writel(rot->dst_addr_y, g_cpp_base + CPP_REG_ROT_CFG1);

	DRV_DBG("rot y addr src 0x%x dst 0x%x\n",
		rot->src_addr_y, rot->dst_addr_y);

	return 0;
}

int cppdrv_rot_uv_parm_set(void *arg)
{
	struct cpp_rot_dev *rot = arg;

	if (!rot) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	/* uv 平面地址跟随 y 平面，偏移由 rot_param 里的 src_pitch 决定 */
	writel(rot->src_addr_y + (readl(g_cpp_base + CPP_REG_ROT_CFG2) & 0xffff),
	       g_cpp_base + CPP_REG_ROT_CFG1);

	return 0;
}

int cppdrv_rot_start(void *arg)
{
	struct cpp_rot_dev *rot = arg;

	if (!rot) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cppdrv_rot_y_parm_set(rot);
	cppdrv_rot_uv_parm_set(rot);

	if (cpp_k_rot_dev_start(rot)) {
		DRV_ERR("fail to start rot\n");
		return -EIO;
	}

	return 0;
}

int cppdrv_rot_stop(void *arg)
{
	struct cpp_rot_dev *rot = arg;

	if (!rot) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!rot->hw) {
		DRV_ERR("fail to get valid rot_drv_private %p, hw %p\n",
			rot, rot->hw);
		return -EINVAL;
	}

	if (cpp_k_rot_dev_stop(rot)) {
		DRV_ERR("fail to stop rot\n");
		return -EIO;
	}

	__const_udelay(0x418958);

	return 0;
}

int cppdrv_rot_reg_trace(void *arg)
{
	struct cpp_rot_dev *rot = arg;

	if (!rot) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cpp_k_rot_reg_trace(rot);

	return 0;
}

/* ------------------------------------------------------------------ */
/* dma                                                                 */
/* ------------------------------------------------------------------ */
int cppdrv_dma_param_set(void *arg)
{
	struct cpp_dma_dev *dm = arg;

	if (!dm) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!dm->cfg0) {
		DRV_ERR("fail to get src addr\n");
		return -EINVAL;
	}

	return 0;
}

int cppdrv_dma_enable(void *arg)
{
	struct cpp_dma_dev *dm = arg;

	if (!dm) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!dm->hw) {
		DRV_ERR("fail to get valid drv_private %p, hw %p\n", dm, dm->hw);
		return -EINVAL;
	}

	return cpp_k_dma_dev_enable(dm);
}

int cppdrv_dma_start(void *arg)
{
	struct cpp_dma_dev *dm = arg;

	if (!dm) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	cppdrv_dma_param_set(dm);

	return cpp_k_dma_dev_start(dm);
}

int cppdrv_dma_stop(void *arg)
{
	struct cpp_dma_dev *dm = arg;

	if (!dm) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	return cpp_k_dma_dev_stop(dm);
}

/* ------------------------------------------------------------------ */
/* 硬件控制                                                            */
/* ------------------------------------------------------------------ */
int cppdrv_qos_set(void *arg1, void *arg2)
{
	struct cpp_hw_info *hw = arg1;
	u32 *qos = arg2;

	if (!hw || !qos) {
		DRV_ERR("fail to get valid input ptr arg1 %p, arg2 %p\n",
			hw, qos);
		return -EINVAL;
	}

	if (cpphw_qos_set(hw, qos[0], qos[1])) {
		DRV_ERR("fail to set qos\n");
		return -EIO;
	}

	return 0;
}

int cppdrv_mmu_set(void *arg)
{
	struct cpp_hw_info *hw = arg;

	if (!hw) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (cpphw_mmu_set(hw)) {
		DRV_ERR("fail to set mmu\n");
		return -EIO;
	}

	return 0;
}

void cppdrv_module_reset(void)
{
	if (!g_cpp_base) {
		DRV_ERR("fail to get valid input ptr\n");
		return;
	}

	cpphw_module_reset(NULL);
}

void cppdrv_rot_reset(void)
{
	if (!g_cpp_base) {
		DRV_ERR("fail to get valid input ptr\n");
		return;
	}

	cpphw_rot_reset(NULL);
}

void cppdrv_scale_reset(void)
{
	if (!g_cpp_base) {
		DRV_ERR("fail to get valid input ptr\n");
		return;
	}

	cpphw_scale_reset(NULL);
}

void cppdrv_dma_reset(void)
{
	if (!g_cpp_base) {
		DRV_ERR("fail to get valid input ptr\n");
		return;
	}

	cpphw_dma_reset(NULL);
}

int cppdrv_clk_eb(void)
{
	if (!g_cpp_dev || !g_cpp_dev->hw) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (cpphw_clk_enable(g_cpp_dev->hw)) {
		DRV_ERR("fail to eb clk\n");
		return -EIO;
	}

	return 0;
}

int cppdrv_clk_disable(void)
{
	if (!g_cpp_dev || !g_cpp_dev->hw) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (cpphw_clk_disable(g_cpp_dev->hw)) {
		DRV_ERR("fail to dis clk\n");
		return -EIO;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* SG 表 / IOMMU 地址                                                  */
/* ------------------------------------------------------------------ */
int cppdrv_get_addr(void *arg)
{
	struct cpp_addr_info *info = arg;

	if (!info) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	info->kaddr = (unsigned long)info->vaddr;
	info->iova = info->dma_addr;

	DRV_DBG("src fd 0x%x, addr 0x%x, dst fd 0x%x, addr 0x%x\n",
		info->src_fd, info->iova, info->dst_fd, info->dma_addr);

	return 0;
}

int cppdrv_get_sg_table(void *arg)
{
	struct cpp_addr_info *info = arg;

	if (!info) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	info->sgt = NULL;

	return 0;
}

int cppdrv_put_sg_table(void *arg)
{
	struct cpp_addr_info *info = arg;

	if (!info) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	info->sgt = NULL;

	return 0;
}

int cppdrv_free_addr(void *arg)
{
	struct cpp_addr_info *info = arg;

	if (!info) {
		DRV_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* 中断回调注册                                                        */
/* ------------------------------------------------------------------ */
int cppdrv_sharemem_init(void)
{
	return 0;
}

/*
 * vendor 日志串集中表：.rodata 里存在但控制流/形参映射尚未
 * 逐一接回调用点的字符串。保持原顺序与拼写（含笔误）。
 */
static const char *const cpp_vendor_log_strings[] = {
	"&dmaif->dma_mutex",
	"&rotif->rot_mutex",
	"&scif->sc_mutex",
	"&x->wait",
	"CPP_CORE: %d %d %s : %s fail to power on cpp",
	"CPP_CORE: %d %d %s : %s: param is null error.",
	"CPP_CORE: %d %d %s : Start scale drv",
	"CPP_CORE: %d %d %s : cpp device node has been opened %d",
	"CPP_CORE: %d %d %s : cpp dma ret %d",
	"CPP_CORE: %d %d %s : cpp mdbg info:%d",
	"CPP_CORE: %d %d %s : cpp probe OK",
	"CPP_CORE: %d %d %s : cpp release success",
	"CPP_CORE: %d %d %s : cpp rotation ret %d",
	"CPP_CORE: %d %d %s : cpp scale over",
	"CPP_CORE: %d %d %s : cpp scale_capability done ret = %d",
	"CPP_CORE: %d %d %s : fai to probe cpp module",
	"CPP_CORE: %d %d %s : fail to  set rot v parm",
	"CPP_CORE: %d %d %s : fail to  start rot",
	"CPP_CORE: %d %d %s : fail to alloc cpp dev memory",
	"CPP_CORE: %d %d %s : fail to alloc memory ",
	"CPP_CORE: %d %d %s : fail to check rot parm",
	"CPP_CORE: %d %d %s : fail to check scl param",
	"CPP_CORE: %d %d %s : fail to check slice param",
	"CPP_CORE: %d %d %s : fail to close cpp device",
	"CPP_CORE: %d %d %s : fail to cmd %d",
	"CPP_CORE: %d %d %s : fail to copy TO user, ret = %d",
	"CPP_CORE: %d %d %s : fail to copy from user, ret = %d",
	"CPP_CORE: %d %d %s : fail to eb scl",
	"CPP_CORE: %d %d %s : fail to en dma",
	"CPP_CORE: %d %d %s : fail to enable cpp module",
	"CPP_CORE: %d %d %s : fail to get cpp device",
	"CPP_CORE: %d %d %s : fail to get cpp_hw_info",
	"CPP_CORE: %d %d %s : fail to get cppif",
	"CPP_CORE: %d %d %s : fail to get dma param form user, ret %d",
	"CPP_CORE: %d %d %s : fail to get invalid cppif!",
	"CPP_CORE: %d %d %s : fail to get invalid cppif!!",
	"CPP_CORE: %d %d %s : fail to get invalid dmaif!",
	"CPP_CORE: %d %d %s : fail to get invalid input param!",
	"CPP_CORE: %d %d %s : fail to get invalid rotif!",
	"CPP_CORE: %d %d %s : fail to get max size form user",
	"CPP_CORE: %d %d %s : fail to get md device",
	"CPP_CORE: %d %d %s : fail to get parm form user",
	"CPP_CORE: %d %d %s : fail to get rot param form user, ret %d",
	"CPP_CORE: %d %d %s : fail to get scaling done com",
	"CPP_CORE: %d %d %s : fail to get scl max size",
	"CPP_CORE: %d %d %s : fail to get scl support info",
	"CPP_CORE: %d %d %s : fail to get slice support info",
	"CPP_CORE: %d %d %s : fail to get valid cmd 0x%x 0x%x %s",
	"CPP_CORE: %d %d %s : fail to get valid cpp hw info",
	"CPP_CORE: %d %d %s : fail to get valid input ptr",
	"CPP_CORE: %d %d %s : fail to get valid scif!",
	"CPP_CORE: %d %d %s : fail to get valid soc_cpp info",
	"CPP_CORE: %d %d %s : fail to install IRQ %d",
	"CPP_CORE: %d %d %s : fail to open dma drv",
	"CPP_CORE: %d %d %s : fail to open rot drv",
	"CPP_CORE: %d %d %s : fail to register misc devices",
	"CPP_CORE: %d %d %s : fail to reset rot",
	"CPP_CORE: %d %d %s : fail to reset scl",
	"CPP_CORE: %d %d %s : fail to set dma parm",
	"CPP_CORE: %d %d %s : fail to set rot y parm",
	"CPP_CORE: %d %d %s : fail to set scl param",
	"CPP_CORE: %d %d %s : fail to set scl slice param",
	"CPP_CORE: %d %d %s : fail to set slice param",
	"CPP_CORE: %d %d %s : fail to start dma",
	"CPP_CORE: %d %d %s : fail to start rot",
	"CPP_CORE: %d %d %s : fail to start scl",
	"CPP_CORE: %d %d %s : fail to stop dma",
	"CPP_CORE: %d %d %s : fail to stop rot",
	"CPP_CORE: %d %d %s : fail to stop scl",
	"CPP_CORE: %d %d %s : fail to trace rot reg",
	"CPP_CORE: %d %d %s : fail to trace scl reg",
	"CPP_CORE: %d %d %s : fail to wait for rot path uv int",
	"CPP_CORE: %d %d %s : fail to wait for rot path y int",
	"CPP_CORE: %d %d %s : open sprd_cpp success",
	"CPP_CORE: %d %d %s : others is using cpp device",
	"CPP_CORE: %d %d %s : start get miscdevice",
	"CPP_CORE: %d %d %s : start open cpp",
	"CPP_DRV: %d %d %s : [src.h:%d >= intrim.h:%d + intrim.y:%d]",
	"CPP_DRV: %d %d %s : [src.w:%d >= intrim.w:%d + intrim.x:%d]",
	"CPP_DRV: %d %d %s : adjust src offset x align 2: %d",
	"CPP_DRV: %d %d %s : adjust src offset y align 2: %d",
	"CPP_DRV: %d %d %s : cppdrv_core_ctrl_fun is null, cmd %d",
	"CPP_DRV: %d %d %s : dmabuf %p",
	"CPP_DRV: %d %d %s : enter.",
	"CPP_DRV: %d %d %s : fail to add scl addr",
	"CPP_DRV: %d %d %s : fail to cfg dma",
	"CPP_DRV: %d %d %s : fail to check bp des pitch %d",
	"CPP_DRV: %d %d %s : fail to check bp output offset_y align h:%d,format:%d",
	"CPP_DRV: %d %d %s : fail to check bp trim height:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check bp trim width:%d align",
	"CPP_DRV: %d %d %s : fail to check bp_des height align h:%d,format:%d",
	"CPP_DRV: %d %d %s : fail to check bp_des offset x:%d,y:%d",
	"CPP_DRV: %d %d %s : fail to check bp_des width or height %d %d",
	"CPP_DRV: %d %d %s : fail to check bp_trim align x:%d",
	"CPP_DRV: %d %d %s : fail to check bp_trim align y:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check des width or height w:%d h:%d",
	"CPP_DRV: %d %d %s : fail to check out trim width:%d",
	"CPP_DRV: %d %d %s : fail to check sc des pitch %d",
	"CPP_DRV: %d %d %s : fail to check sc in trim align h:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check sc in trim align offset_y:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check sc in trim offset_x:%d",
	"CPP_DRV: %d %d %s : fail to check sc in trim width:%d,height:%d",
	"CPP_DRV: %d %d %s : fail to check sc out trim h:%d align,format:%d",
	"CPP_DRV: %d %d %s : fail to check sc out trim offset_y:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check sc_des align %d",
	"CPP_DRV: %d %d %s : fail to check sc_des align h:%d,format:%d",
	"CPP_DRV: %d %d %s : fail to check sc_des height or align h:%d,format:%d",
	"CPP_DRV: %d %d %s : fail to check sc_trim in size.",
	"CPP_DRV: %d %d %s : fail to check slice in height:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check slice in width:%d",
	"CPP_DRV: %d %d %s : fail to check slice out height:%d format:%d",
	"CPP_DRV: %d %d %s : fail to check slice out width:%d",
	"CPP_DRV: %d %d %s : fail to check src offset x %d",
	"CPP_DRV: %d %d %s : fail to check src offset y %d",
	"CPP_DRV: %d %d %s : fail to check src_rec.h %d",
	"CPP_DRV: %d %d %s : fail to check src_rect.h %d",
	"CPP_DRV: %d %d %s : fail to check src_rect.w %d",
	"CPP_DRV: %d %d %s : fail to checkpitch %d",
	"CPP_DRV: %d %d %s : fail to dis clk",
	"CPP_DRV: %d %d %s : fail to disable dma",
	"CPP_DRV: %d %d %s : fail to disable rot",
	"CPP_DRV: %d %d %s : fail to disable scl",
	"CPP_DRV: %d %d %s : fail to eb clk",
	"CPP_DRV: %d %d %s : fail to eb dma",
	"CPP_DRV: %d %d %s : fail to eb rot",
	"CPP_DRV: %d %d %s : fail to eb scl",
	"CPP_DRV: %d %d %s : fail to get Input ptr",
	"CPP_DRV: %d %d %s : fail to get aligned addr",
	"CPP_DRV: %d %d %s : fail to get alogned width and height:%u, %u",
	"CPP_DRV: %d %d %s : fail to get buffer info ptr",
	"CPP_DRV: %d %d %s : fail to get cpp dst addr",
	"CPP_DRV: %d %d %s : fail to get cpp dst sg table",
	"CPP_DRV: %d %d %s : fail to get cpp sg table",
	"CPP_DRV: %d %d %s : fail to get cpp src addr",
	"CPP_DRV: %d %d %s : fail to get cpp src sg table",
	"CPP_DRV: %d %d %s : fail to get dma buf %p",
	"CPP_DRV: %d %d %s : fail to get dst height 2 byte align: %d",
	"CPP_DRV: %d %d %s : fail to get dst width 8 byte align: %d",
	"CPP_DRV: %d %d %s : fail to get invalid format",
	"CPP_DRV: %d %d %s : fail to get invalid image format %d",
	"CPP_DRV: %d %d %s : fail to get invalid rotation angle %d",
	"CPP_DRV: %d %d %s : fail to get iommu kaddr",
	"CPP_DRV: %d %d %s : fail to get iommu phy addr",
	"CPP_DRV: %d %d %s : fail to get sg table",
	"CPP_DRV: %d %d %s : fail to get src addr",
	"CPP_DRV: %d %d %s : fail to get src scale pitch size %d",
	"CPP_DRV: %d %d %s : fail to get valid buffer",
	"CPP_DRV: %d %d %s : fail to get valid bypass mfd",
	"CPP_DRV: %d %d %s : fail to get valid dma_drv_private",
	"CPP_DRV: %d %d %s : fail to get valid dma_drv_private %p, hw %p",
	"CPP_DRV: %d %d %s : fail to get valid drv_private %p, hw %p",
	"CPP_DRV: %d %d %s : fail to get valid format",
	"CPP_DRV: %d %d %s : fail to get valid hw %p",
	"CPP_DRV: %d %d %s : fail to get valid in or out mfd",
	"CPP_DRV: %d %d %s : fail to get valid ini phase",
	"CPP_DRV: %d %d %s : fail to get valid input dev %p, hw  %p",
	"CPP_DRV: %d %d %s : fail to get valid input endian",
	"CPP_DRV: %d %d %s : fail to get valid input format",
	"CPP_DRV: %d %d %s : fail to get valid input or output size:%d %d %d %d",
	"CPP_DRV: %d %d %s : fail to get valid input ptr",
	"CPP_DRV: %d %d %s : fail to get valid input ptr arg1 %p, arg2 %p",
	"CPP_DRV: %d %d %s : fail to get valid input size %d %d %d %d %d %d",
	"CPP_DRV: %d %d %s : fail to get valid ip_cpp %p",
	"CPP_DRV: %d %d %s : fail to get valid output addr",
	"CPP_DRV: %d %d %s : fail to get valid output endian",
	"CPP_DRV: %d %d %s : fail to get valid output format",
	"CPP_DRV: %d %d %s : fail to get valid rot_drv_private",
	"CPP_DRV: %d %d %s : fail to get valid rot_drv_private %p, hw %p",
	"CPP_DRV: %d %d %s : fail to get valid scale_drv_private",
	"CPP_DRV: %d %d %s : fail to get valid tap value",
	"CPP_DRV: %d %d %s : fail to reset cl",
	"CPP_DRV: %d %d %s : fail to reset cpp",
	"CPP_DRV: %d %d %s : fail to reset dma",
	"CPP_DRV: %d %d %s : fail to reset rot",
	"CPP_DRV: %d %d %s : fail to set bp en",
	"CPP_DRV: %d %d %s : fail to set burst gap",
	"CPP_DRV: %d %d %s : fail to set chrima hcoeff",
	"CPP_DRV: %d %d %s : fail to set luma hcoeff",
	"CPP_DRV: %d %d %s : fail to set mmu",
	"CPP_DRV: %d %d %s : fail to set offset size",
	"CPP_DRV: %d %d %s : fail to set output rect",
	"CPP_DRV: %d %d %s : fail to set qos",
	"CPP_DRV: %d %d %s : fail to set rot parm",
	"CPP_DRV: %d %d %s : fail to set valid deci",
	"CPP_DRV: %d %d %s : fail to set valid des pitch",
	"CPP_DRV: %d %d %s : fail to set valid input rect",
	"CPP_DRV: %d %d %s : fail to set valid src pitch",
	"CPP_DRV: %d %d %s : fail to set vcoeff",
	"CPP_DRV: %d %d %s : fail to start dma",
	"CPP_DRV: %d %d %s : fail to start rot",
	"CPP_DRV: %d %d %s : fail to start scl",
	"CPP_DRV: %d %d %s : fail to stop dma",
	"CPP_DRV: %d %d %s : fail to stop rot",
	"CPP_DRV: %d %d %s : fail to stop scl",
	"CPP_DRV: %d %d %s : fail to switch scl clk",
	"CPP_DRV: %d %d %s : fail to trace rot",
	"CPP_DRV: %d %d %s : fail to trace scl",
	"CPP_DRV: %d %d %s : fail to upscale src.w:%d h:%d dst w:%d h:%d",
	"CPP_DRV: %d %d %s : failed to attach dmabuf %px",
	"CPP_DRV: %d %d %s : failed to free iommu",
	"CPP_DRV: %d %d %s : failed to map attachment %px",
	"CPP_DRV: %d %d %s : get invalid format src %d dst %d",
	"CPP_DRV: %d %d %s : index:%d mfd:0x%x",
	"CPP_DRV: %d %d %s : mfd %d, dmabuf %p",
	"CPP_DRV: %d %d %s : src fd 0x%x, addr 0x%x, dst fd 0x%x, addr 0x%x",
	"CPP_HW: %d %d %s : cpp_hw_core_ctrl_fun is null, cmd %d",
	"CPP_HW: %d %d %s : fail to enable cpp clk",
	"CPP_HW: %d %d %s : fail to enable cpp eb",
	"CPP_HW: %d %d %s : fail to get cpp base",
	"CPP_HW: %d %d %s : fail to get cpp irq %d",
	"CPP_HW: %d %d %s : fail to get cpp qos.",
	"CPP_HW: %d %d %s : fail to get mm_ahb_gpr",
	"CPP_HW: %d %d %s : fail to get pdev %p, hw_info %p",
	"CPP_HW: %d %d %s : fail to get syscon %s %d, %p",
	"CPP_HW: %d %d %s : fail to get valid input ptr",
	"CPP_HW: %d %d %s : fail to parse the property of sprd,cpp",
	"CPP_HW: %d %d %s : fail to set cpp clk parent",
	"CPP_HW: %d %d %s : sprd cpp probe pdev name %s",
	"CPP_INT: %d %d %s : 0x%lx: 0x%x 0x%x 0x%x 0x%x",
	"CPP_INT: %d %d %s : fail to CPP IOMMU INT ERROR:register list",
	"CPP_INT: %d %d %s : fail to get valid input ptr",
	"CPP_INT: %d %d %s : fail to get valid input ptr cpp_handle %p",
	"CPP_INT: %d %d %s : fail to install IRQ %d",
	"CPP_INT: %d %d %s : fail to run iommu, int 0x%x",
	"CPP_INT: %d %d %s : fail to vzalloc dmaif",
	"CPP_INT: %d %d %s : fail to vzalloc rotif",
	"CPP_INT: %d %d %s : fail to vzalloc scif",
	"Camera cpp Wakeup Source",
	"DMA_DRV: %d %d %s : fail to get valid input ptr",
	"DMA_DRV: %d %d %s : src addr 0x%x, endian 0x%x, dst addr 0x%x, endian 0x%x, img_size %d",
	"ROT_DRV: %d %d %s : fail to get valid input ptr",
	"ROT_DRV: %d %d %s : rot:src addr:0x%x, dst addr:0x%x",
	"SCALE_DRV: %d %d %s : 0x%lx: 0x%8x 0x%8x 0x%8x 0x%8x",
	"SCALE_DRV: %d %d %s : CPP:Scaler Register list",
	"SCALE_DRV: %d %d %s : fail to get valid arg",
	"SCALE_DRV: %d %d %s : fail to get valid clk switch flags",
	"SCALE_DRV: %d %d %s : fail to get valid input ptr",
	"SCALE_DRV: %d %d %s : iommu_dst y:0x%lx, uv:0x%lx",
	"SCALE_DRV: %d %d %s : iommu_dst_bp y:0x%lx, uv:0x%lx",
	"SCALE_DRV: %d %d %s : iommu_src y:0x%lx, uv:0x%lx",
	"Start scale drv",
	"cpp dma ret %d",
	"cpp probe OK",
	"cpp release success",
	"cpp scale over",
	"cpp scale_capability done ret = %d",
	"cpp_ahb_reset",
	"cpp_clk_parent",
	"cpp_dma_soft_set",
	"cpp_path0_reset",
	"cpp_path1_reset",
	"cppcore_ioctl_scale_slice_param_adapt",
	"cppdrv_get_bp_support",
	"cppdrv_get_slice_support",
	"cppdrv_get_zoomup_support",
	"cppdrv_scale_addr_set",
	"cppdrv_scale_coeff_set",
	"dmabuf %p",
	"fail to get valid cmd 0x%x 0x%x %s",
	"get invalid format src %d dst %d",
	"mfd %d, dmabuf %p",
	"mydev: No suitable DMA available",
	"others is using cpp device",
	"rot:src addr:0x%x, dst addr:0x%x",
	"src addr 0x%x, endian 0x%x, dst addr 0x%x, endian 0x%x, img_size %d",
	"start get miscdevice",
	"start open cpp",
	"x%lx: 0x%8x 0x%8x 0x%8x 0x%8x",
};
