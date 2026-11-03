// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, Sharkl5Pro (r7p0) back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_sharkl5pro.c
 * functions: sprd_campw_init/@0x1b4, sprd_cam_pw_on/@0x810,
 *            sprd_cam_pw_off/@0xaa8, sprd_cam_domain_eb/@0x5a4,
 *            sprd_cam_domain_disable/@0x744
 */

#define pr_fmt(fmt)	"sprd_campd_r7p0: %d %d %s : " fmt

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

/* Camera mm power state polling: 30 x ~325us for on, 29 for off */
#define L5PRO_POLL_ON_MAX	30
#define L5PRO_POLL_OFF_MAX	29

struct l5pro_syscon {
	struct regmap *map;	/* +0x0 */
	u32 reg;		/* +0x8 */
	u32 mask;		/* +0xc */
};

struct l5pro_priv {
	u8 qos_ar;			/* 0x620 */
	u8 qos_aw;			/* 0x621 */
	u8 pad0[6];
	struct clk *clk_mm_eb;		/* 0x628 */
	struct clk *clk_mm_ahb_eb;	/* 0x630 */
	struct clk *clk_mm_ahb;		/* 0x638 */
	struct clk *clk_mm_ahb_parent;	/* 0x640 */
	struct clk *clk_mm_ahb_p;	/* 0x648 */
	struct clk *clk_mm_mtx;		/* 0x650 */
	struct clk *clk_mm_mtx_parent;	/* 0x658 */
	struct clk *clk_mm_mtx_p;	/* 0x660 */
	struct clk *clk_isppll;		/* 0x668 */
	struct l5pro_syscon syscon[6];	/* 0x670 */
};

#define PRIV(info)	((struct l5pro_priv *)((u8 *)(info) + 0x620))

/* force-shutdown, shutdown-en, power-state, qos-ar, qos-aw, aon-apb-mm-eb */
static const char * const l5pro_syscon_name[] = {
	"force-shutdown",
	"shutdown-en",
	"power-state",
	"qos-ar",
	"qos-aw",
	"aon-apb-mm-eb",
};

/* Lowest set bit index of a mask, 0 for an empty mask (CLZ(RBIT(x))). */
static u32 l5pro_bit_shift(u32 mask)
{
	return mask ? (u32)__builtin_ctz(mask) : 0;
}

/* 0x5a4, 416 bytes: clocks and qos bits on, called before pw_on */
static int l5pro_domain_eb(struct camsys_power_info *info)
{
	struct l5pro_priv *priv = PRIV(info);
	u32 shift;

	pr_debug("cb %p\n", (void *)priv);

	clk_prepare_enable(priv->clk_mm_eb);
	clk_prepare_enable(priv->clk_mm_ahb_eb);

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_parent);
	clk_prepare_enable(priv->clk_mm_ahb);

	clk_set_parent(priv->clk_mm_mtx, priv->clk_mm_mtx_parent);
	clk_prepare_enable(priv->clk_mm_mtx);

	clk_prepare_enable(priv->clk_isppll);

	shift = l5pro_bit_shift(priv->syscon[3].reg);
	if (priv->syscon[3].map)
		regmap_update_bits(priv->syscon[3].map,
				   priv->syscon[3].reg,
				   priv->syscon[3].mask,
				   (u32)priv->qos_ar << shift);

	shift = l5pro_bit_shift(priv->syscon[4].reg);
	if (priv->syscon[4].map)
		regmap_update_bits(priv->syscon[4].map,
				   priv->syscon[4].reg,
				   priv->syscon[4].mask,
				   (u32)priv->qos_aw << shift);

	return 0;
}

/* 0x744, 204 bytes */
static int l5pro_domain_disable(struct camsys_power_info *info)
{
	struct l5pro_priv *priv = PRIV(info);

	pr_debug("cb %p\n", (void *)priv);

	clk_disable_unprepare(priv->clk_isppll);

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_p);
	clk_disable_unprepare(priv->clk_mm_ahb);

	clk_set_parent(priv->clk_mm_mtx, priv->clk_mm_mtx_p);
	clk_disable_unprepare(priv->clk_mm_mtx);

	clk_disable_unprepare(priv->clk_mm_ahb_eb);
	clk_disable_unprepare(priv->clk_mm_eb);

	return 0;
}

/* 0x810, 664 bytes: release force-shutdown and poll the power-state */
static int l5pro_pw_on(struct camsys_power_info *info)
{
	struct l5pro_priv *priv = PRIV(info);
	struct l5pro_syscon *sc = priv->syscon;
	u32 value = 0;
	int ret, i;

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0);

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	for (i = 0; i < L5PRO_POLL_ON_MAX; i++) {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			continue;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			continue;
		value &= sc[2].mask;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			continue;
		value &= sc[2].mask;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			continue;
		value &= sc[2].mask;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret)
			continue;
		value &= sc[2].mask;

		if (!value) {
			pr_info("Done, i %d\n", i);
			return 0;
		}
	}

	pr_err("fail to pw on cam domain\n");
	return -EIO;
}

/* 0xaa8, 476 bytes: assert force-shutdown and poll the power-state */
static int l5pro_pw_off(struct camsys_power_info *info)
{
	struct l5pro_priv *priv = PRIV(info);
	struct l5pro_syscon *sc = priv->syscon;
	u32 value = 0, expect;
	int i = 0;

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0xffffffff);

	expect = 7u << l5pro_bit_shift(sc[2].mask);

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			break;

		if (regmap_read(sc[2].map, sc[2].reg, &value)) {
			pr_err("fail to get power_state : 0x%x\n", value);
			return -EIO;
		}
		value &= sc[2].mask;

		if (i > L5PRO_POLL_OFF_MAX)
			break;

		i++;
	} while (value != expect);

	if (value == expect) {
		pr_info("Done, read count %d, cb: %pS\n", i,
			(void *)__builtin_return_address(0));
		return 0;
	}

	pr_err("fail to get power_state : 0x%x\n", value);
	pr_err("fail to pw off. ret: %d, count: %d, cb: %pS\n", -EIO, i,
	       (void *)__builtin_return_address(0));
	return -EIO;
}

/* 0x1b4, 1008 bytes */
static int l5pro_init(struct platform_device *pdev,
		      struct camsys_power_info *info)
{
	struct l5pro_priv *priv = PRIV(info);
	struct device_node *nd = pdev->dev.of_node;
	struct device_node *qos_nd;
	u32 args[2];
	int i, ret = 0;

	priv->clk_mm_eb = of_clk_get_by_name(nd, "clk_mm_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_eb))
		goto err_clk;

	priv->clk_mm_ahb_eb = of_clk_get_by_name(nd, "clk_mm_ahb_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_eb))
		goto err_clk;

	priv->clk_mm_ahb = of_clk_get_by_name(nd, "clk_mm_ahb");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb))
		goto err_clk;

	priv->clk_mm_ahb_parent = of_clk_get_by_name(nd, "clk_mm_ahb_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_parent))
		goto err_clk;

	priv->clk_mm_ahb_p = clk_get_parent(priv->clk_mm_ahb);
	if (!priv->clk_mm_ahb_p)
		goto err_clk;

	priv->clk_mm_mtx = of_clk_get_by_name(nd, "clk_mm_mtx");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx))
		goto err_clk;

	priv->clk_mm_mtx_parent = of_clk_get_by_name(nd, "clk_mm_mtx_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_parent))
		goto err_clk;

	priv->clk_mm_mtx_p = clk_get_parent(priv->clk_mm_mtx);
	if (!priv->clk_mm_mtx_p)
		goto err_clk;

	priv->clk_isppll = of_clk_get_by_name(nd, "clk_isppll");
	if (IS_ERR_OR_NULL(priv->clk_isppll))
		goto err_clk;

	for (i = 0; i < ARRAY_SIZE(priv->syscon); i++) {
		struct regmap *map;

		map = syscon_regmap_lookup_by_phandle_args(nd,
							   l5pro_syscon_name[i],
							   2, args);
		if (IS_ERR_OR_NULL(map)) {
			pr_err("fail to read %s reg_map 0x%lx %d\n",
			       l5pro_syscon_name[i], (unsigned long)map,
			       (int)PTR_ERR_OR_ZERO(map));
			continue;
		}

		priv->syscon[i].map = map;
		priv->syscon[i].reg = args[0];
		priv->syscon[i].mask = args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", l5pro_syscon_name[i],
			 args[0], args[1]);
	}

	qos_nd = of_parse_phandle(nd, "mm_qos_threshold", 0);
	if (IS_ERR_OR_NULL(qos_nd)) {
		priv->qos_ar = 0x0d;
		priv->qos_aw = 0x0d;
		pr_warn("fail to read mm qos threshold. Use default[%x %x]\n",
			0x0d, 0x0d);
	} else {
		if (of_property_read_u8(qos_nd, "arqos-threshold",
					&priv->qos_ar) < 0) {
			priv->qos_ar = 0x0d;
			pr_warn("fail to read arqos-threshold, default %d\n",
				0x0d);
		}
		if (of_property_read_u8(qos_nd, "awqos-threshold",
					&priv->qos_aw) < 0) {
			priv->qos_aw = 0x0d;
			pr_warn("fail to read awqos-threshold, default %d\n",
				0x0d);
		}
	}

	if (priv->syscon[5].map)
		regmap_update_bits(priv->syscon[5].map,
				   priv->syscon[5].reg,
				   priv->syscon[5].mask, 0xffffffff);

	return 0;

err_clk:
	pr_err("fail to read clk, ret %d\n", ret);
	return -ENOMEM;
}

const struct camsys_power_ops camsys_power_ops_l5pro = {
	.init = l5pro_init,
	.on = l5pro_pw_on,
	.off = l5pro_pw_off,
	.eb = l5pro_domain_eb,
	.disable = l5pro_domain_disable,
};
