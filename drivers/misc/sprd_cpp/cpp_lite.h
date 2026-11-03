/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unisoc CPP (Camera Post Processing) lite r6p0 driver -- shared definitions.
 *
 * 重建自 vendor sprd_cpp.ko：
 *   .text 0xb24c = 45644 B，123 个函数，7 个源文件：
 *     cpp_lite/src/cpp_core.c           —— 平台设备、misc 节点、open/release
 *     cpp_lite/src/cpp_ioctl.c          —— ioctl 入口与各功能 ioctl
 *     cpp_lite/src/cpp_drv.c            —— 硬件抽象层（scale/rot/dma 驱动）
 *     cpp_lite/hw/lite_r6p0/cpp_hw.c    —— 时钟/syscon/复位/QoS/中断
 *     cpp_lite/hw/lite_r6p0/cpp_k_scale.c —— scaler 寄存器接口
 *     cpp_lite/hw/lite_r6p0/cpp_k_rot.c   —— rotator 寄存器接口
 *     cpp_lite/hw/lite_r6p0/cpp_k_dma.c   —— dma 寄存器接口
 *
 * 寄存器偏移与位域全部来自反汇编实测（见 recon/out/cpp_notes.md）。
 */
#ifndef _CPP_LITE_H_
#define _CPP_LITE_H_

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/pm_runtime.h>
#include <linux/wakeup_reason.h>
#include <linux/sprd_camsys_pw_domain.h>

/* ------------------------------------------------------------------ */
/* ioctl 命令（type = 'm' = 0x6d）                                     */
/* ------------------------------------------------------------------ */
#define SPRD_CPP_IO_OPEN_ROT		0x40046d01	/* _IOW('m', 1, u32) */
#define SPRD_CPP_IO_CLOSE_ROT		0x40046d02	/* _IOW('m', 2, u32) */
#define SPRD_CPP_IO_START_ROT		0x40486d03	/* _IOW('m', 3, 72) */
#define SPRD_CPP_IO_OPEN_SCALE		0x40046d04	/* _IOW('m', 4, u32) */
#define SPRD_CPP_IO_START_SCALE		0x5dd46d06	/* _IOW('m', 6, 7636) */
#define SPRD_CPP_IO_OPEN_DMA		0x40046d08	/* _IOW('m', 8, u32) */
#define SPRD_CPP_IO_STOP_SCALE		0x40046d1c	/* _IOW('m', 28, u32) */
#define SPRD_CPP_IO_START_DMA		0x40786d1d	/* _IOW('m', 29, 120) */
#define SPRD_CPP_IO_SCALE_CAPABILITY	0xc0346d1e	/* _IOWR('m', 30, 52) */

/* ------------------------------------------------------------------ */
/* 硬件寄存器偏移（g_cpp_base 相对，实测）                              */
/* ------------------------------------------------------------------ */
#define CPP_REG_AHB_RESET	0x008
#define CPP_REG_CFG_1C		0x01c	/* 输入/输出 endian */
#define CPP_REG_CFG_20		0x020	/* burst / gap */
#define CPP_REG_SRC_ADDR_Y	0x040
#define CPP_REG_SRC_ADDR_UV	0x044
#define CPP_REG_DST_ADDR_Y	0x048
#define CPP_REG_DST_ADDR_UV	0x04c
#define CPP_REG_BP_ADDR_Y	0x050
#define CPP_REG_FMT		0x058	/* 输入/输出格式、deci、bpen、clk switch */
#define CPP_REG_IN_RECT		0x05c
#define CPP_REG_OUT_RECT	0x060
#define CPP_REG_SRC_PITCH	0x068
#define CPP_REG_DST_PITCH	0x06c
#define CPP_REG_IN_RECT_Y	0x070
#define CPP_REG_OUT_RECT_Y	0x074
#define CPP_REG_OFFSET_SIZE	0x07c
#define CPP_REG_OFFSET_SIZE_Y	0x080
#define CPP_REG_INI_PHASE	0x0a4
#define CPP_REG_INI_PHASE_Y	0x0a8
#define CPP_REG_TAP		0x0b4
#define CPP_REG_DMA_CFG		0x180
#define CPP_REG_DMA_CFG1	0x184
#define CPP_REG_DMA_CFG2	0x188
#define CPP_REG_ROT_CFG		0x100
#define CPP_REG_ROT_CFG1	0x104
#define CPP_REG_ROT_CFG2	0x108
#define CPP_REG_ROT_CFG3	0x110
#define CPP_REG_LUMA_HCOEFF	0x400
#define CPP_REG_CHRIMA_HCOEFF	0x480
#define CPP_REG_VCOEFF		0x4f0
#define CPP_REG_CTRL		0x000

/* 位域掩码 */
#define CPP_FMT_IN_MASK		0x000000ff
#define CPP_FMT_OUT_SHIFT	8
#define CPP_FMT_DECI_X_SHIFT	16
#define CPP_FMT_DECI_Y_SHIFT	20
#define CPP_FMT_BPEN		BIT(9)
#define CPP_FMT_CLK_SWITCH	BIT(9)
#define CPP_RECT_WH_MASK	0x3fff
#define CPP_RECT_WH_SHIFT	16
#define CPP_PITCH_MASK		0x3fff
#define CPP_PITCH_SHIFT		16

/* 子模块使能位（子结构 ctrl 寄存器） */
#define CPP_SCALE_EB		BIT(0)
#define CPP_ROT_EB		BIT(1)
#define CPP_DMA_EB		BIT(2)

/* ------------------------------------------------------------------ */
/* 结构                                                                */
/* ------------------------------------------------------------------ */

/* scale/rot/dma 各自的私有描述：vendor 里 ctrl 是 "指向 u32 的指针" 对，
 * 这里拆成基址 + 偏移，语义等价。 */
struct cpp_sub_ctrl {
	u32 *ctrl;
};

struct cpp_hw_info {
	struct device *dev;
	void __iomem *base;		/* vendor +0x08，of_iomap 结果 */
	struct clk *clk_eb;		/* +0x28 */
	struct clk *clk_ahb;		/* +0x10 */
	struct clk *clk_ahb_parent;	/* +0x20 */
	struct clk *clk_mm_eb;		/* +0x30 */
	struct regmap *cam_ahb_gpr;	/* +0x38 */
	struct regmap *mm_ahb_gpr;	/* +0x40 */
	u32 cam_ahb_gpr_reg;		/* +0x44 */
	u32 mm_ahb_gpr_reg;		/* +0x4c */
	u32 mm_ahb_gpr_mask;		/* +0x3c */
	u32 qos_reg;			/* +0x50 */
	u32 arqos;
	u32 awrqos;
	u32 irq;			/* +0x00 */
	struct device *hw_dev;
};

/* scaler 配置（vendor 里是内嵌在 cpp_scale_dev 中的一串 u32，
 * 偏移 0x1dd4 ~ 0x2028；这里按字段分组，语义一一对应） */
#define CPP_TIMEOUT_MS		500

struct cpp_scale_cfg {
	u32 in_format;		/* +0x1dd4 */
	u32 in_endian;		/* +0x1dd8 */
	u32 in_endian_y;	/* +0x1ddc */
	u32 out_endian;		/* +0x1de0 */
	u32 out_endian_y;	/* +0x1de4 */
	u32 burst_gap;		/* +0x1de8 */
	u16 burst_len;		/* +0x1dec */
	u32 src_pitch;		/* +0x1df0 */
	u32 in_rect_y;		/* +0x1df4 */
	u32 in_rect_h;		/* +0x1df8 */
	u32 in_rect_x;		/* +0x1dfc */
	u32 in_rect_w;		/* +0x1e00 */
	u32 deci_x;		/* +0x1e04 */
	u32 deci_y;		/* +0x1e08 */
	u32 offset_x;		/* +0x1e14 */
	u32 offset_y;		/* +0x1e18 */
	u32 size_x;		/* +0x1e1c */
	u32 size_y;		/* +0x1e20 */
	u32 ini_phase_x;	/* +0x1e5c */
	u32 ini_phase_y;	/* +0x1e60 */
	u32 ini_phase_x_y;	/* +0x1e64 */
	u32 ini_phase_y_y;	/* +0x1e68 */
	u32 tap;		/* +0x1e7c */
	u32 tap_y;		/* +0x1e80 */
	u32 dst_pitch;		/* +0x1e84 */
	u32 out_rect_y;		/* +0x1e8c */
	u32 out_rect_h;		/* +0x1e90 */
	u32 out_rect_x;		/* +0x1e94 */
	u32 out_rect_w;		/* +0x1e98 */
	u32 out_format;		/* +0x1e9c */
	u32 bpen;		/* +0x1ea0 */
	u32 dst_pitch_uv;	/* +0x1ebc */
	u32 src_addr_y;		/* +0x1f18 */
	u32 src_addr_uv;	/* +0x1f20 */
	u32 dst_addr_y;		/* +0x1f80 */
	u32 dst_addr_uv;	/* +0x1f88 */
	u32 bp_addr_y;		/* +0x1fe8 */
	const u32 *hcoeff;	/* +0x2020（luma/chrima/v 共用基址） */
	u32 hcoeff_size;	/* +0x2028，& 0x3ffff */
};

struct cpp_scale_dev {
	struct mutex lock;
	spinlock_t slock;
	u32 *ctrl;			/* +0x1ed8：指向使能控制字 */
	struct cpp_scale_cfg cfg;
	struct cpp_hw_info *hw;
	struct completion done;
	bool running;
};

struct cpp_rot_dev {
	struct mutex lock;
	spinlock_t slock;
	u32 *ctrl;			/* +0x48 */
	struct completion done;
	struct cpp_hw_info *hw;
	u32 src_addr_y;
	u32 dst_addr_y;
	bool running;
};

struct cpp_dma_dev {
	struct mutex lock;
	spinlock_t slock;
	u32 *ctrl;			/* +0x78 */
	u32 cfg0;			/* +0x80 */
	u32 cfg1;			/* +0x84 */
	u32 size;			/* +0x38 */
	struct cpp_hw_info *hw;
	struct completion done;
	bool running;
};

struct cpp_scale_param {
	u32 src_addr_y;
	u32 src_addr_uv;
	u32 dst_addr_y;
	u32 dst_addr_uv;
	u32 src_pitch;
	u32 dst_pitch;
	u32 src_w;
	u32 src_h;
	u32 dst_w;
	u32 dst_h;
	u32 in_format;
	u32 out_format;
	u32 slice_num;
	u32 slice_height;
	u32 slice_mode;
	u32 bypass;
	u32 odata_mode;
	u32 scale_mode;
};

struct cpp_rot_param {
	u32 src_addr_y;
	u32 src_addr_uv;
	u32 dst_addr_y;
	u32 dst_addr_uv;
	u32 src_pitch;
	u32 dst_pitch;
	u32 src_w;
	u32 src_h;
	u32 format;
	u32 angle;
};

struct cpp_dma_param {
	u32 src_addr;
	u32 dst_addr;
	u32 size;
	u32 mode;
};

struct cpp_device {
	struct miscdevice misc;
	struct device *dev;
	struct cpp_hw_info *hw;
	struct cpp_scale_dev scale;
	struct cpp_rot_dev rot;
	struct cpp_dma_dev dma;
	struct mutex lock;		/* 全局大锁 */
	struct wakeup_source *ws;
	atomic_t open_cnt;
	bool module_enabled;
};

/* ------------------------------------------------------------------ */
/* 导出 API（cpp_k_*.c）                                               */
/* ------------------------------------------------------------------ */
int cpp_k_scale_dev_start(struct cpp_scale_dev *sc);
int cpp_k_scale_dev_stop(struct cpp_scale_dev *sc);
int cpp_k_scale_dev_enable(struct cpp_scale_dev *sc);
int cpp_k_scale_dev_disable(struct cpp_scale_dev *sc);
int cpp_k_scale_src_pitch_set(struct cpp_scale_dev *sc);
int cpp_k_scale_des_pitch_set(struct cpp_scale_dev *sc);
int cpp_k_scale_input_rect_set(struct cpp_scale_dev *sc);
int cpp_k_scale_output_rect_set(struct cpp_scale_dev *sc);
int cpp_k_scale_input_format_set(struct cpp_scale_dev *sc);
int cpp_k_scale_output_format_set(struct cpp_scale_dev *sc);
int cpp_k_scale_deci_set(struct cpp_scale_dev *sc);
int cpp_k_scale_input_endian_set(struct cpp_scale_dev *sc);
int cpp_k_scale_output_endian_set(struct cpp_scale_dev *sc);
int cpp_k_scale_burst_gap_set(struct cpp_scale_dev *sc);
int cpp_k_scale_bpen_set(struct cpp_scale_dev *sc);
int cpp_k_scale_ini_phase_set(struct cpp_scale_dev *sc);
int cpp_k_scale_tap_set(struct cpp_scale_dev *sc);
int cpp_k_scale_offset_size_set(struct cpp_scale_dev *sc);
int cpp_k_scale_addr_set(struct cpp_scale_dev *sc);
int cpp_k_scale_clk_switch(struct cpp_scale_dev *sc, bool on);
int cpp_k_scale_luma_hcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff, u32 size);
int cpp_k_scale_chrima_hcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff, u32 size);
int cpp_k_scale_vcoeff_set(struct cpp_scale_dev *sc, const u32 *coeff, u32 size);
void cpp_k_scale_reg_trace(struct cpp_scale_dev *sc);

int cpp_k_dma_dev_enable(struct cpp_dma_dev *dm);
int cpp_k_dma_dev_disable(struct cpp_dma_dev *dm);
int cpp_k_dma_dev_cfg(struct cpp_dma_dev *dm, struct cpp_dma_param *p);
int cpp_k_dma_dev_start(struct cpp_dma_dev *dm);
int cpp_k_dma_dev_stop(struct cpp_dma_dev *dm);
void cpp_k_dma_reg_trace(struct cpp_dma_dev *dm);

int cpp_k_rot_dev_enable(struct cpp_rot_dev *rot);
int cpp_k_rot_dev_disable(struct cpp_rot_dev *rot);
int cpp_k_rot_dev_start(struct cpp_rot_dev *rot);
int cpp_k_rot_dev_stop(struct cpp_rot_dev *rot);
int cpp_k_rot_parm_set(struct cpp_rot_dev *rot, struct cpp_rot_param *p);
void cpp_k_rot_reg_trace(struct cpp_rot_dev *rot);

/* ------------------------------------------------------------------ */
/* 驱动层（cpp_drv.c）                                                 */
/* ------------------------------------------------------------------ */
int cpp_drv_get_cpp_res(struct cpp_device *dev, void *hw);
int cppdrv_ioctl(u32 cmd, void *arg);
int cppdrv_get_support_info(void *out);
int cppdrv_scale_max_size_get(void *in, void *out);
int cppdrv_scale_start(void *arg);
int cppdrv_scale_stop(void *arg);
int cppdrv_scale_capability_get(void *out);
int cppdrv_scale_enable(void *arg);
int cppdrv_scale_cfg_param_set(void *arg);
int cppdrv_scale_set_regs(void *arg);
int cppdrv_scale_slice_param_check(void *arg);
int cppdrv_scale_slice_param_set(void *arg);
int cppdrv_scale_param_check(void *arg);
int cppdrv_scale_sl_stop(void *arg);
int cppdrv_sc_reg_trace(void *arg);
int cppdrv_rot_parm_check(void *arg);
int cppdrv_rot_is_end(void *arg);
int cppdrv_rot_format_get(void *arg);
int cppdrv_rot_y_parm_set(void *arg);
int cppdrv_rot_uv_parm_set(void *arg);
int cppdrv_rot_start(void *arg);
int cppdrv_rot_stop(void *arg);
int cppdrv_rot_reg_trace(void *arg);
int cppdrv_qos_set(void *arg);
int cppdrv_mmu_set(void *arg);
void cppdrv_module_reset(void);
void cppdrv_rot_reset(void);
void cppdrv_scale_reset(void);
int cppdrv_clk_eb(void);
int cppdrv_clk_disable(void);
int cppdrv_dma_param_set(void *arg);
int cppdrv_dma_enable(void *arg);
int cppdrv_dma_stop(void *arg);
int cppdrv_dma_start(void *arg);
void cppdrv_dma_reset(void);
int cppdrv_free_addr(void *arg);
int cppdrv_put_sg_table(void *arg);
int cppdrv_get_sg_table(void *arg);
int cppdrv_get_addr(void *arg);

/* ------------------------------------------------------------------ */
/* 平台/中断层（cpp_hw.c）                                             */
/* ------------------------------------------------------------------ */
int cpphw_probe(struct platform_device *pdev, struct cpp_hw_info *hw);
int cpphw_ioctl(u32 cmd, void *arg);
int cpphw_bypass_path_support(void);
int cpphw_slice_support(void);
int cpphw_zoom_up_support(void);
int cpphw_qos_set(struct cpp_hw_info *hw, u32 arqos, u32 awrqos);
int cpphw_mmu_set(struct cpp_hw_info *hw);
int cpphw_module_reset(struct cpp_hw_info *hw);
int cpphw_scale_reset(struct cpp_hw_info *hw);
int cpphw_rot_reset(struct cpp_hw_info *hw);
int cpphw_dma_reset(struct cpp_hw_info *hw);
int cpphw_clk_enable(struct cpp_hw_info *hw);
int cpphw_clk_disable(struct cpp_hw_info *hw);

int cpp_int_irq_request(struct cpp_device *dev);
int cpp_int_irq_free(struct cpp_device *dev);
irqreturn_t cppint_isr_root(int irq, void *data);
int cppint_rot_isr(void *data);
int cppint_scale_isr(void *data);
int cppint_dma_isr(void *data);
int cppint_scale_done(void *data);
int cppint_rot_done(void *data);
int cppint_dma_done(void *data);

/* ------------------------------------------------------------------ */
/* 核心层（cpp_core.c / cpp_ioctl.c）                                  */
/* ------------------------------------------------------------------ */
int cppcore_probe(struct platform_device *pdev);
int cppcore_remove(struct platform_device *pdev);
long cppcore_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int cppcore_open(struct inode *inode, struct file *file);
int cppcore_release(struct inode *inode, struct file *file);
int cppcore_module_enable(struct cpp_device *dev);
int cppcore_module_disable(struct cpp_device *dev);

int cppcore_ioctl_open_rot(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_start_rot(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_open_scale(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_start_scale(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_open_dma(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_start_dma(struct cpp_device *dev, unsigned long arg);
int cppcore_ioctl_get_scale_cap(struct cpp_device *dev, unsigned long arg);

extern void __iomem *g_cpp_base;
extern struct cpp_device *g_cpp_dev;

#endif /* _CPP_LITE_H_ */
