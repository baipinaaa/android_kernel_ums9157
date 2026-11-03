/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef __SPRD_DRM_COLOR_MGMT_H__
#define __SPRD_DRM_COLOR_MGMT_H__

#include <linux/types.h>

struct dpu_context;

#define CTM_MATRIX_LEN		12	/* s16[3][4]，3x3 系数 + 3 个偏移 */
#define CTM_SRC_LEN		9	/* drm_color_ctm.matrix[9]，Q0.32 */

/*
 * CTM 下发时要置的两个位，各 core 不同：
 *   r2p0 / lite_r2p0 / r4p0 / r5p0 -> enhance_en 的 BIT(3)，REG_CM_UPDATE 的 BIT(3)
 *   r6p0                           -> enhance_en 的 BIT(2)，REG_CM_UPDATE 的 BIT(0)
 * 证据：设备里 dpu_cm_set 的 5 个克隆，前四个都是 orr #8 两次，
 * r6p0 那个（.2080）是 orr #4 两次 + orr #1 一次。
 */
#define DPU_CM_EN_BIT		BIT(3)
#define DPU_CM_UPD_BIT		BIT(3)

#define PDP_SLP_CFG_SIZE	6
#define PDP_EPF_CFG_SIZE	14
#define PDP_HSV_LUT_SIZE	360
#define PDP_GAMMA_LUT_SIZE	256
#define PDP_GAMMA_LUT_NUM	3

/*
 * 以下是设备里 struct dpu_enhance 的逐字节镜像，布局全部来自反汇编实测，
 * 不是照抄 9620 源码（设备的这个结构体比两棵树都多 28 字节）。
 *
 * 证据：dpu_context_init 的克隆里 kmem_cache_alloc_trace(w2 = 0xbec)，
 * 而按 9620 源码（dpu/dpu_r2p0.c:235）逐字段手算是 0xbd0。
 * 0xbec - 0xbd0 = 0x1c = 2 + 24，正好是 cm_valid 与第二个 cm_cfg。
 *
 * 另一个证据：dpu_enhance_set 访问 0x00/0x0a/0x0c/0x12/0x1a/0x22/0x2a，
 * dpu_enhance_reload 访问 0x00/0x0a/0x0c/0x10/0x14/0x18，
 * dpu_cm_set 访问 0x04/0x1a/0x32 —— 全部落在下表的边界内：
 *   0x0a 是 slp_copy 的最后一个字节，0x0c/0x10/0x12/0x14/0x18 都在 epf_copy 里，
 *   0x1a/0x22/0x2a 分别是 cm_copy 三行的行首（每行 4 个 short = 8 字节），
 *   0x32 是 cm_new 的起点，0x04 是 cm_valid。
 *
 * dpu_cm_set 的 x21 就是 ctx->enhance（+0xd8），所以 x21+4 是 cm_valid，
 * 语义是"色彩矩阵是否已经应用过"。
 */

struct slp_cfg {
	u8 brightness;			/* +0x00 */
	u8 conversion_matrix;		/* +0x01 */
	u8 brightness_step;		/* +0x02 */
	u8 second_bright_factor;	/* +0x03 */
	u8 first_percent_th;		/* +0x04 */
	u8 first_max_bright_th;		/* +0x05 */
};

struct epf_cfg {
	u16 epsilon0;			/* +0x00 */
	u16 epsilon1;			/* +0x02 */
	u8 gain[8];			/* +0x04 */
	u8 max_diff;			/* +0x0c */
	u8 min_diff;			/* +0x0d */
};

struct cm_cfg {
	s16 coef[3][4];			/* +0x00，3x3 系数 + 3 个偏移 */
};

struct hsv_entry {
	u8 h;				/* +0x00 */
	u8 s;				/* +0x01 */
	u8 v;				/* +0x02 */
	u8 rsvd;			/* +0x03 */
};

struct hsv_lut {
	struct hsv_entry table[PDP_HSV_LUT_SIZE];	/* +0x00，1440 字节 */
};

struct gamma_lut {
	u16 table[PDP_GAMMA_LUT_NUM][PDP_GAMMA_LUT_SIZE];	/* +0x00，1536 字节 */
};

struct dpu_enhance {
	u32 enhance_en;			/* +0x000 */
	u8 cm_valid;			/* +0x004，厂商新增：CM 是否已应用过 */
	u8 rsvd;			/* +0x005 */
	struct slp_cfg slp_copy;	/* +0x006，6 字节 */
	struct epf_cfg epf_copy;	/* +0x00c，14 字节 */
	struct cm_cfg cm_copy;		/* +0x01a，24 字节：当前生效的矩阵 */
	struct cm_cfg cm_new;		/* +0x032，24 字节：厂商新增，待应用的矩阵 */
	struct hsv_lut hsv_copy;	/* +0x04a，1440 字节 */
	struct gamma_lut gamma_copy;	/* +0x5ea，1536 字节 */
					/* 对齐 4 后总长 0xbec */
};

void colorMatrix_multi(s16 *out, const s16 *m1, const s16 *m2);
int parse_ctm(s16 *out, u64 *matrix);
void dpu_cm_set(struct dpu_context *ctx, int force, u32 en_bit, u32 upd_bit);

#endif /* __SPRD_DRM_COLOR_MGMT_H__ */
