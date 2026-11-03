// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, QogirL6 (r8p0) back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_qogirl6.c
 * functions: sprd_campw_init/@0x4fec, sprd_cam_pw_on/@0x54c8,
 *            sprd_cam_pw_off/@0x5760, sprd_cam_domain_eb/@0x52e0,
 *            sprd_cam_domain_disable/@0x53fc
 */

#define pr_fmt(fmt)	"sprd_campd_r8p0: %d %d %s : " fmt

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

#define R8P0_POLL_MAX	30

struct r8p0_syscon {
	struct regmap *map;
	u32 reg;
	u32 mask;
};

struct r8p0_priv {
	struct clk *clk_mm_eb;		/* 0x620 */
	struct clk *clk_mm_ahb_eb;	/* 0x628 */
	struct clk *clk_mm_ahb;		/* 0x630 */
	struct clk *clk_mm_ahb_parent;	/* 0x638 */
	struct clk *clk_mm_ahb_p;	/* 0x640 */
	struct clk *clk_mm_mtx;		/* 0x648 */
	struct clk *clk_mm_mtx_parent;	/* 0x650 */
	struct clk *clk_mm_mtx_p;	/* 0x658 */
	struct clk *clk_isppll;		/* 0x660 */
	struct r8p0_syscon syscon[4];	/* 0x670 */
};

#define PRIV(info)	((struct r8p0_priv *)((u8 *)(info) + 0x620))

/* force-shutdown, shutdown-en, power-state, aon-apb-mm-eb */
static const char * const r8p0_syscon_name[] = {
	"force-shutdown",
	"shutdown-en",
	"power-state",
	"aon-apb-mm-eb",
};

static int sprd_campw_init(struct platform_device *pdev,
			   struct camsys_power_info *info)
{
	struct r8p0_priv *priv = PRIV(info);
	struct device_node *nd = pdev->dev.of_node;
	u32 args[2];
	int i;

	priv->clk_mm_eb = of_clk_get_by_name(nd, "clk_mm_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_eb))
		return -ENOMEM;

	priv->clk_mm_ahb_eb = of_clk_get_by_name(nd, "clk_mm_ahb_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_eb))
		return -ENOMEM;

	priv->clk_mm_ahb = of_clk_get_by_name(nd, "clk_mm_ahb");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb))
		return -ENOMEM;

	priv->clk_mm_ahb_parent = of_clk_get_by_name(nd, "clk_mm_ahb_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_parent))
		return -ENOMEM;

	priv->clk_mm_ahb_p = clk_get_parent(priv->clk_mm_ahb);
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_p))
		return -ENOMEM;

	priv->clk_mm_mtx = of_clk_get_by_name(nd, "clk_mm_mtx");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx))
		return -ENOMEM;

	priv->clk_mm_mtx_parent = of_clk_get_by_name(nd, "clk_mm_mtx_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_parent))
		return -ENOMEM;

	priv->clk_mm_mtx_p = clk_get_parent(priv->clk_mm_mtx);
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_p))
		return -ENOMEM;

	priv->clk_isppll = of_clk_get_by_name(nd, "clk_isppll");
	if (IS_ERR_OR_NULL(priv->clk_isppll))
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(priv->syscon); i++) {
		struct regmap *map;

		map = syscon_regmap_lookup_by_phandle_args(nd,
							   r8p0_syscon_name[i],
							   2, args);
		if (IS_ERR_OR_NULL(map)) {
			pr_err("fail to read %s reg_map 0x%lx %d\n",
			       r8p0_syscon_name[i], (unsigned long)map,
			       (int)PTR_ERR_OR_ZERO(map));
			continue;
		}

		priv->syscon[i].map = map;
		priv->syscon[i].reg = args[0];
		priv->syscon[i].mask = args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", r8p0_syscon_name[i],
			 args[0], args[1]);
	}

	if (priv->syscon[3].map)
		regmap_update_bits(priv->syscon[3].map, priv->syscon[3].reg,
				   priv->syscon[3].mask, 0);

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
	struct r8p0_priv *priv = PRIV(info);

	pr_debug("cb %p\n", (void *)priv);

	clk_prepare_enable(priv->clk_mm_eb);
	clk_prepare_enable(priv->clk_mm_ahb_eb);

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_parent);
	clk_prepare_enable(priv->clk_mm_ahb);

	clk_set_parent(priv->clk_mm_mtx, priv->clk_mm_mtx_parent);
	clk_prepare_enable(priv->clk_mm_mtx);

	clk_prepare_enable(priv->clk_isppll);

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *info)
{
	struct r8p0_priv *priv = PRIV(info);

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

static int sprd_cam_pw_on(struct camsys_power_info *info)
{
	struct r8p0_priv *priv = PRIV(info);
	struct r8p0_syscon *sc = priv->syscon;
	u32 value = 0;
	int ret, i;

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0);

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	for (i = 0; i < R8P0_POLL_MAX; i++) {
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

	pr_err("fail to pw on cam domain\n");
	return -EIO;
}

static int sprd_cam_pw_off(struct camsys_power_info *info)
{
	struct r8p0_priv *priv = PRIV(info);
	struct r8p0_syscon *sc = priv->syscon;
	u32 value = 0, expect;
	int i = 0;

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0xffffffff);

	expect = 7u << (sc[2].mask ? (u32)__builtin_ctz(sc[2].mask) : 0);

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			break;

		if (regmap_read(sc[2].map, sc[2].reg, &value)) {
			pr_err("fail to get power_state : 0x%x\n", value);
			return -EIO;
		}
		value &= sc[2].mask;

		if (i > R8P0_POLL_MAX)
			break;

		i++;
	} while (value != expect);

	if (value == expect) {
		pr_info("Done, read count %d, cb: %pS\n", i,
			(void *)__builtin_return_address(0));
		return 0;
	}

	pr_err("fail to pw off. ret: %d, count: %d, cb: %pS\n", -EIO, i,
	       (void *)__builtin_return_address(0));
	return -EIO;
}

const struct camsys_power_ops camsys_power_ops_qogirl6 = {
	.init = sprd_campw_init,
	.on = sprd_cam_pw_on,
	.off = sprd_cam_pw_off,
	.eb = sprd_cam_domain_eb,
	.disable = sprd_cam_domain_disable,
};
