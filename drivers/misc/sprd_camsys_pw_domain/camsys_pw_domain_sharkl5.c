// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, Sharkl5 (r6p0) back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_sharkl5.c
 * functions: sprd_campw_init/@0xc84, sprd_cam_pw_on/@0x13a8,
 *            sprd_cam_pw_off/@0x1638, sprd_cam_domain_eb/@0x118c,
 *            sprd_cam_domain_disable/@0x1304
 */

#define pr_fmt(fmt)	"sprd_campd_r6p0: %d %d %s: " fmt

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mfd/syscon.h>

#include "sprd_camsys_pw_domain.h"

#define R6P0_POLL_MAX	30

struct r6p0_syscon {
	struct regmap *map;	/* +0x0 */
	u32 reg;		/* +0x8 */
	u32 mask;		/* +0xc */
};

struct r6p0_priv {
	u8 qos_ar;			/* 0x620 */
	u8 qos_aw;			/* 0x628 (padded) */
	struct r6p0_syscon syscon[6];	/* 0x630 */
	struct clk *mm_eb;		/* 0x690 */
	struct clk *mm_ahb_eb;		/* 0x698 */
	struct clk *clk_mm_ahb;		/* 0x6a0 */
	struct clk *clk_mm_ahb_parent;	/* 0x6a8 */
	struct clk *clk_mm_ahb_p;	/* 0x6b0 */
	struct clk *mm_mtx_eb;		/* 0x6b8 */
	struct clk *clk_mm_mtx;		/* 0x6c0 */
	struct clk *clk_mm_mtx_parent;	/* 0x6c8 */
	struct clk *clk_mm_mtx_p;	/* 0x6d0 */
};

#define PRIV(info)	((struct r6p0_priv *)((u8 *)(info) + 0x620))
#define QOS_DEFAULT	0x0d

/* force_shutdown, shutdown_en, power_state, qos_ar, qos_aw, aon-apb-mm-eb */
static const char * const r6p0_syscon_name[] = {
	"force_shutdown",
	"shutdown_en",
	"power_state",
	"qos_ar",
	"qos_aw",
	"aon-apb-mm-eb",
};

static u32 r6p0_bit_shift(u32 mask)
{
	return mask ? (u32)__builtin_ctz(mask) : 0;
}

static int sprd_campw_init(struct platform_device *pdev,
			   struct camsys_power_info *info)
{
	struct r6p0_priv *priv = PRIV(info);
	struct device_node *nd = pdev->dev.of_node;
	struct device_node *qos_nd;
	u32 args[2];
	int ret, i;

	priv->mm_eb = of_clk_get_by_name(nd, "mm_eb");
	ret = PTR_ERR_OR_ZERO(priv->mm_eb);
	if (IS_ERR_OR_NULL(priv->mm_eb)) {
		pr_err("Get mm_eb clk fail, ret %d\n", ret);
		return ret;
	}

	priv->mm_ahb_eb = of_clk_get_by_name(nd, "mm_ahb_eb");
	ret = PTR_ERR_OR_ZERO(priv->mm_ahb_eb);
	if (IS_ERR_OR_NULL(priv->mm_ahb_eb)) {
		pr_err("Get mm_ahb_eb clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_ahb = of_clk_get_by_name(nd, "clk_mm_ahb");
	ret = PTR_ERR_OR_ZERO(priv->clk_mm_ahb);
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb)) {
		pr_err("Get clk_mm_ahb clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_ahb_parent = of_clk_get_by_name(nd, "clk_mm_ahb_parent");
	ret = PTR_ERR_OR_ZERO(priv->clk_mm_ahb_parent);
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_parent)) {
		pr_err("Get mm_ahb_parent clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_ahb_p = clk_get_parent(priv->clk_mm_ahb);
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_p))
		return -ENOMEM;

	priv->mm_mtx_eb = of_clk_get_by_name(nd, "mm_mtx_eb");
	ret = PTR_ERR_OR_ZERO(priv->mm_mtx_eb);
	if (IS_ERR_OR_NULL(priv->mm_mtx_eb)) {
		pr_err("Get mm_mtx_eb clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_mtx = of_clk_get_by_name(nd, "clk_mm_mtx");
	ret = PTR_ERR_OR_ZERO(priv->clk_mm_mtx);
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx)) {
		pr_err("Get clk_mm_mtx clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_mtx_parent = of_clk_get_by_name(nd,
						     "clk_mm_mtx_parent");
	ret = PTR_ERR_OR_ZERO(priv->clk_mm_mtx_parent);
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_parent)) {
		pr_err("Get clk_mm_mtx_parent clk fail, ret %d\n", ret);
		return ret;
	}

	priv->clk_mm_mtx_p = clk_get_parent(priv->clk_mm_mtx);
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_p))
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(priv->syscon); i++) {
		struct regmap *map;

		map = syscon_regmap_lookup_by_phandle_args(nd,
							   r6p0_syscon_name[i],
							   2, args);
		if (IS_ERR_OR_NULL(map)) {
			pr_err("fail to read %s regmap\n", r6p0_syscon_name[i]);
			continue;
		}

		priv->syscon[i].map = map;
		priv->syscon[i].reg = args[0];
		priv->syscon[i].mask = args[1];
		pr_debug("DTS[%s]%p, 0x%x, 0x%x\n", r6p0_syscon_name[i], map,
			 args[0], args[1]);
	}

	qos_nd = of_parse_phandle(nd, "mm_qos_threshold", 0);
	if (!qos_nd) {
		priv->qos_ar = QOS_DEFAULT;
		priv->qos_aw = QOS_DEFAULT;
		pr_err("read mm qos threshold fail, default[%x %x]\n",
		       priv->qos_ar, priv->qos_aw);
	} else {
		if (of_property_read_variable_u8_array(qos_nd, "arqos-threshold",
						       &priv->qos_ar, 1, 0)) {
			priv->qos_ar = QOS_DEFAULT;
			pr_err("read arqos-threshold fail, default %d\n",
			       priv->qos_ar);
		}

		if (of_property_read_variable_u8_array(qos_nd, "awqos-threshold",
						       &priv->qos_aw, 1, 0)) {
			priv->qos_aw = QOS_DEFAULT;
			pr_err("read awqos-threshold fail, default %d\n",
			       priv->qos_aw);
		}
	}

	if (priv->syscon[5].map)
		regmap_update_bits(priv->syscon[5].map, priv->syscon[5].reg,
				   priv->syscon[5].mask, 0);

	if (priv->syscon[1].map)
		regmap_update_bits(priv->syscon[1].map, priv->syscon[1].reg,
				   priv->syscon[1].mask, 0);

	if (priv->syscon[0].map)
		regmap_update_bits(priv->syscon[0].map, priv->syscon[0].reg,
				   priv->syscon[0].mask, 0xffffffff);

	pr_debug("Read DTS OK\n");
	return 0;
}

static int sprd_cam_domain_eb(struct camsys_power_info *info)
{
	struct r6p0_priv *priv = PRIV(info);
	struct r6p0_syscon *sc = priv->syscon;
	u32 shift, ret;

	pr_debug("cb %p\n", (void *)priv);

	ret = clk_prepare_enable(priv->mm_eb);
	if (ret) {
		pr_err("ret = 0x%x\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mm_ahb_eb);
	if (ret) {
		pr_err("ret = 0x%x\n", ret);
		return ret;
	}

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_parent);
	ret = clk_prepare_enable(priv->clk_mm_ahb);
	if (ret) {
		pr_err("ret = 0x%x\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mm_mtx_eb);
	if (ret) {
		pr_err("ret = 0x%x\n", ret);
		return ret;
	}

	clk_set_parent(priv->clk_mm_mtx, priv->clk_mm_mtx_parent);
	ret = clk_prepare_enable(priv->clk_mm_mtx);
	if (ret) {
		pr_err("ret = 0x%x\n", ret);
		return ret;
	}

	if (sc[3].map) {
		shift = r6p0_bit_shift(sc[3].reg);
		regmap_update_bits(sc[3].map, sc[3].reg, sc[3].mask,
				   (u32)priv->qos_ar << shift);
	}

	if (sc[4].map) {
		shift = r6p0_bit_shift(sc[4].reg);
		regmap_update_bits(sc[4].map, sc[4].reg, sc[4].mask,
				   (u32)priv->qos_aw << shift);
	}

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *info)
{
	struct r6p0_priv *priv = PRIV(info);

	pr_debug("cb %p\n", (void *)priv);

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_p);
	clk_disable_unprepare(priv->clk_mm_ahb);

	clk_set_parent(priv->clk_mm_mtx, priv->clk_mm_mtx_p);
	clk_disable_unprepare(priv->clk_mm_mtx);

	clk_disable_unprepare(priv->mm_mtx_eb);
	clk_disable_unprepare(priv->mm_ahb_eb);
	clk_disable_unprepare(priv->mm_eb);

	return 0;
}

static int sprd_cam_pw_on(struct camsys_power_info *info)
{
	struct r6p0_priv *priv = PRIV(info);
	struct r6p0_syscon *sc = priv->syscon;
	u32 value = 0;
	int ret, i;

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0);

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	for (i = 0; i < R6P0_POLL_MAX; i++) {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			continue;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			continue;
		value &= sc[2].mask;

		if (!value) {
			pr_info("Done, i %d\n", i);
			return 0;
		}
	}

	pr_err("failed, power_state1=0x%x\n", value);
	pr_err("fail to power on cam domain\n");
	return -EIO;
}

static int sprd_cam_pw_off(struct camsys_power_info *info)
{
	struct r6p0_priv *priv = PRIV(info);
	struct r6p0_syscon *sc = priv->syscon;
	u32 value = 0, expect;
	int ret, i = 0;

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask,
				   0xffffffff);

	expect = 7u << r6p0_bit_shift(sc[2].mask);

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			break;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			break;
		value &= sc[2].mask;

		if (value == expect) {
			pr_info("Done, read count %d, cb: %p\n", i,
				(void *)__builtin_return_address(0));
			return 0;
		}

		i++;
	} while (i < R6P0_POLL_MAX);

	pr_err("failed, ret: %d, count: %d, cb: %p\n", -EIO, i,
	       (void *)__builtin_return_address(0));
	return -EIO;
}

const struct camsys_power_ops camsys_power_ops_l5 = {
	.init = sprd_campw_init,
	.on = sprd_cam_pw_on,
	.off = sprd_cam_pw_off,
	.eb = sprd_cam_domain_eb,
	.disable = sprd_cam_domain_disable,
};
