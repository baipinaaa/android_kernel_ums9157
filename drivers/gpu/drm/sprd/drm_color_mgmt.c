// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 *
 * CTM（color transform matrix）相关的定点运算。
 *
 * 这里的三个函数（colorMatrix_multi / parse_ctm / dpu_cm_set）在两棵源树里
 * 都不存在，是从 sprd-drm.ko 反汇编重建的，详见
 * android_kernel_ums9157-master/ums9157-recon/08-基线移植工作量.md 的 8.110-8.112。
 *
 * 定点格式：色彩矩阵系数以 10000 为 1.0，用 s16 存（10000 = 1.0，32767 ≈ 3.27）。
 * 乘法后的归一化不是简单的 >> 14，而是「乘 0x68db8bad 再算术右移 44 位」：
 * 2^44 / 0x68db8bad = 17592186044416 / 1760607149 ≈ 9993.2，即除以 10000。
 * 紧跟着的 lsr #63 + add 是把算术右移的「向下取整」修正成「向零舍入」，
 * 负结果时 +1 —— 这一步不能省，省了会出现 -0.5 变成 -1 的偏色。
 */

#define pr_fmt(fmt) "[drm][%20s] " fmt, __func__

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <drm/drm_crtc.h>

#include "drm_color_mgmt.h"
#include "sprd_dpu.h"

/*
 * 除以 10000 的定点乘法，对应设备里的
 *   mul   wN, wA, wB        ; s16 * s16
 *   smull xN, wN, w10       ; * 0x68db8bad
 *   lsr   xM, xN, #0x3f     ; 取符号位当舍入量
 *   asr   xN, xN, #0x2c     ; >> 44
 *   add   wN, wN, wM        ; 向零舍入
 */
static inline s32 cm_div10000(s16 a, s16 b)
{
	s64 prod = (s64)((s32)a * (s32)b) * 0x68db8bad;
	s32 q = (s32)(prod >> 44);

	return q + (s32)((u64)prod >> 63);
}

/*
 * out = m1 * m2，两个输入和一个输出都是 s16[3][4]：
 *   [i][0..2] 是 3x3 的系数，[i][3] 是这一行的偏移量（只在 j==0 时加一次）。
 * 24 字节的结构刚好能被 dpu_cm_set 当成 6 个 u32 写进 6 个寄存器，
 * 也是 dpu_cm_set 尾部「先用 3 个 8 字节比较、相等就一个寄存器都不写」的原因。
 *
 * 设备把 3x3 全展开了（每个 j 各一组 mul/smull/lsr/asr/add），循环只剩
 * 最外层的 i（x8 从 0 到 0x18，步长 8，即 3 次）。这里保留双重循环的写法，
 * 编译器在当前 -O2 下会给出同样的展开。
 */
void colorMatrix_multi(s16 *out, const s16 *m1, const s16 *m2)
{
	int i, j, k;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			u16 acc = (u16)out[i * 4 + j];

			for (k = 0; k < 3; k++)
				acc += cm_div10000(m1[i * 4 + k],
						   m2[k * 4 + j]);
			if (j == 0)
				acc += (u16)m1[i * 4 + 3];
			out[i * 4 + j] = (s16)acc;
		}
	}
}

/*
 * 把 drm_color_ctm.matrix[9]（每个是 Q0.32 的 s64，但设备只取低 32 位）
 * 重排成 s16[3][4]。
 *
 * 设备里的两步：
 *   第一步（0x1cc8-0x1cf8）：从取出的 16 个 u16 里跳过每第 4 个
 *     （tst (i+1), #3 -> i=3,7,11,15 跳过），得到 12 个，写进栈上的
 *     s16 mid[12]（行优先的 3x4）。
 *   第二步（0x1cfc-0x1d50）：转置成列优先，目标下标是
 *     dst = (i == 3*(i/3)) ? i/3 : 4*i - 11*(i/3)
 *     展开就是 0,4,8,1,5,9,2,6,10,3,7,11 —— 正是 s16[3][4] 的转置。
 *   其中 0xaaaaaaab >> 33 是除以 3 的定点魔数，msub 的乘数 11 与本式一致。
 *
 * matrix[8] 被设备当成「待应用」标志：为 0 就什么都不做，非 0 就处理完
 * 立刻清零（0x1c90 `str xzr,[x1,#0x40]`），避免同一个 ctm 被重复应用。
 * 函数会写这个位置，所以参数不能声明成 const。
 *
 * 返回值：0 成功、1 没有待应用的 ctm、18 元素超限（设备里是 UBSan 的
 * 越界保护分支，16 次循环最多产出 12 个元素，正常路径走不到）。
 */
int parse_ctm(s16 *out, u64 *matrix)
{
	u16 tmp[CTM_SRC_LEN * 2];
	s16 mid[CTM_MATRIX_LEN];
	int i, n = 0, q, dst;

	memset(out, 0, CTM_MATRIX_LEN * sizeof(*out));

	if (!matrix[8])
		return 1;
	matrix[8] = 0;

	/*
	 * 设备取每个 u64 的低 32 位，按 u32 连续铺开（8 个 u32 占满
	 * sp+0x18..0x38），之后按 u16 下标读。这里用同样的口径铺成 18 个 u16。
	 */
	for (i = 0; i < CTM_SRC_LEN; i++) {
		tmp[2 * i] = (u16)(matrix[i] & 0xffff);
		tmp[2 * i + 1] = (u16)((matrix[i] >> 16) & 0xffff);
	}

	for (i = 0; i < 16; i++) {
		if (((i + 1) & 3) == 0)
			continue;
		if (n > CTM_MATRIX_LEN - 1)
			return 18;
		mid[n++] = (s16)tmp[i];
	}

	for (i = 0; i < CTM_MATRIX_LEN; i++) {
		q = i / 3;
		dst = (i == 3 * q) ? q : 4 * i - 11 * q;
		out[dst] = mid[i];
	}

	return 0;
}

/*
 * 把 CTM 写进硬件。三个分支（设备 .text 0x21abc，440 字节）：
 *
 *   force && cm_valid    -> 用 cm_copy x cm_new 算一遍
 *   force && !cm_valid   -> 只有 cm_copy 可信，直接复制
 *   !force               -> 从 crtc 的 ctm 属性解析新矩阵到 cm_new，
 *                           置 cm_valid 后同上
 *
 * cm_valid 是厂商在 struct dpu_enhance 里新增的 u8（+0x04），
 * 9620 源码没有它，所以两棵源树里都没有这个函数。
 */
void dpu_cm_set(struct dpu_context *ctx, int force, u32 en_bit, u32 upd_bit)
{
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct dpu_enhance *enh = ctx->enhance;
	struct drm_property_blob *blob;
	u64 matrix[CTM_MATRIX_LEN / 4] = { 0 };
	s16 out[CTM_MATRIX_LEN] = { 0 };
	s16 zero[CTM_MATRIX_LEN] = { 0 };
	u32 val;
	int i;

	if (force) {
		if (enh->cm_valid)
			colorMatrix_multi(out, (s16 *)enh->cm_copy.coef,
					  (s16 *)enh->cm_new.coef);
		else
			memcpy(out, enh->cm_copy.coef, sizeof(out));
	} else {
		/* dpu = *(ctx - 8)，ctx 是 dpu 的成员，编译器自己算 */
		blob = dpu->crtc->base.state->ctm;
		if (!blob)
			return;

		if (parse_ctm(matrix, (u64 *)blob->data))
			return;

		/* level 6 = pr_info，`.rodata+0x71ce` */
		pr_info("ctm changed!\n");

		memcpy(enh->cm_new.coef, matrix, sizeof(out));
		colorMatrix_multi(out, (s16 *)enh->cm_copy.coef,
				  (s16 *)enh->cm_new.coef);
		enh->cm_valid = 1;
	}

	/* 设备在栈上把「旧值」清零了，所以这里等价于「算出来全是 0 就别写」 */
	if (!memcmp(out, zero, sizeof(out)))
		return;

	/* 12 个 s16 按小端两两打包成 6 个 u32，写到 0x280-0x294 */
	for (i = 0; i < 6; i++)
		writel(((u32 *)out)[i], ctx->base + 0x280 + i * 4);

	val = readl(ctx->base + 0x200);
	writel(val | upd_bit, ctx->base + 0x200);

	enh->enhance_en |= en_bit;
}
