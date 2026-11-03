// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, Sharkle (r5p1) back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_sharkle.c
 * functions: sprd_campw_init/@0x6438, sprd_cam_pw_on/@0x6848,
 *            sprd_cam_pw_off/@0x6a3c, sprd_cam_domain_eb/@0x6670,
 *            sprd_cam_domain_disable/@0x679c
 */

#define pr_fmt(fmt)	"sprd_campd_r5p1: %d %d %s : " fmt

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

#define R5P1_POLL_MAX	30

struct r5p1_syscon {
	struct regmap *map;	/* +0x0 */
	u32 reg;		/* +0x8 */
	u32 mask;		/* +0xc */
};

struct r5p1_priv {
	struct clk *clk_cphy_cfg_gate_eb;	/* 0x620 */
	struct clk *clk_gate_eb;		/* 0x628 */
	struct clk *clk_mm_eb;			/* 0x630 */
	struct clk *clk_mm_ahb;			/* 0x638 */
	struct clk *clk_mm_ahb_parent;		/* 0x640 */
	struct clk *clk_mm_ahb_p;		/* 0x648 */
	struct r5p1_syscon syscon[6];		/* 0x650 */
};

#define PRIV(info)	((struct r5p1_priv *)((u8 *)(info) + 0x620))

/* shutdown_en, force_shutdown, pd_mm_state, anlg_apb_eb, qos_threshold_mm, aon-apb-mm-eb */
static const char * const r5p1_syscon_name[] = {
	"shutdown_en",
	"force_shutdown",
	"pd_mm_state",
	"anlg_apb_eb",
	"qos_threshold_mm",
	"aon-apb-mm-eb",
};

static int sprd_campw_init(struct platform_device *pdev,
			   struct camsys_power_info *info)
{
	struct r5p1_priv *priv = PRIV(info);
	struct device *dev = &pdev->dev;
	struct device_node *nd = pdev->dev.of_node;
	u32 args[2];
	int i;

	priv->clk_cphy_cfg_gate_eb = devm_clk_get(dev, "clk_cphy_cfg_gate_eb");
	if (IS_ERR_OR_NULL(priv->clk_cphy_cfg_gate_eb))
		return -ENOMEM;

	priv->clk_gate_eb = devm_clk_get(dev, "clk_gate_eb");
	if (IS_ERR_OR_NULL(priv->clk_gate_eb))
		return -ENOMEM;

	priv->clk_mm_eb = devm_clk_get(dev, "clk_mm_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_eb))
		return -ENOMEM;

	priv->clk_mm_ahb = devm_clk_get(dev, "clk_mm_ahb");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb))
		return -ENOMEM;

	priv->clk_mm_ahb_parent = devm_clk_get(dev, "clk_mm_ahb_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_parent))
		return -ENOMEM;

	priv->clk_mm_ahb_p = clk_get_parent(priv->clk_mm_ahb);
	if (IS_ERR_OR_NULL(priv->clk_mm_ahb_p))
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(priv->syscon); i++) {
		struct regmap *map;

		map = syscon_regmap_lookup_by_phandle_args(nd,
							   r5p1_syscon_name[i],
							   2, args);
		if (IS_ERR_OR_NULL(map)) {
			pr_err("fail to read %s regmap\n", r5p1_syscon_name[i]);
			continue;
		}

		priv->syscon[i].map = map;
		priv->syscon[i].reg = args[0];
		priv->syscon[i].mask = args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", r5p1_syscon_name[i],
			 args[0], args[1]);
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

	return 0;
}

static int sprd_cam_domain_eb(struct camsys_power_info *info)
{
	struct r5p1_priv *priv = PRIV(info);

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_parent);
	clk_prepare_enable(priv->clk_mm_ahb);
	clk_prepare_enable(priv->clk_mm_eb);
	clk_prepare_enable(priv->clk_gate_eb);
	clk_prepare_enable(priv->clk_cphy_cfg_gate_eb);

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *info)
{
	struct r5p1_priv *priv = PRIV(info);

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	clk_disable_unprepare(priv->clk_cphy_cfg_gate_eb);
	clk_disable_unprepare(priv->clk_gate_eb);
	clk_disable_unprepare(priv->clk_mm_eb);

	clk_set_parent(priv->clk_mm_ahb, priv->clk_mm_ahb_p);
	clk_disable_unprepare(priv->clk_mm_ahb);

	return 0;
}

static int sprd_cam_pw_on(struct camsys_power_info *info)
{
	struct r5p1_priv *priv = PRIV(info);
	struct r5p1_syscon *sc = priv->syscon;
	u32 value = 0;
	int ret, i;

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask, 0);

	for (i = 0; i < R5P1_POLL_MAX; i++) {
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

	pr_err("fail to power on mm domain\n");
	return -EIO;
}

static int sprd_cam_pw_off(struct camsys_power_info *info)
{
	struct r5p1_priv *priv = PRIV(info);
	struct r5p1_syscon *sc = priv->syscon;
	u32 value = 0;
	int ret, i = 0;

	if (sc[1].map)
		regmap_update_bits(sc[1].map, sc[1].reg, sc[1].mask, 0);

	if (sc[0].map)
		regmap_update_bits(sc[0].map, sc[0].reg, sc[0].mask,
				   0xffffffff);

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sc[2].map)
			break;

		ret = regmap_read(sc[2].map, sc[2].reg, &value);
		if (ret) {
			pr_err("fail to get power state 0x%x\n", value);
			break;
		}
		value &= sc[2].mask;

		if (value == sc[2].mask)
			return 0;

		i++;
	} while (i < R5P1_POLL_MAX);

	pr_err("fail to power off cam sys, ret %d, read count %d\n", -EIO, i);
	return -EIO;
}

const struct camsys_power_ops camsys_power_ops_le = {
	.init = sprd_campw_init,
	.on = sprd_cam_pw_on,
	.off = sprd_cam_pw_off,
	.eb = sprd_cam_domain_eb,
	.disable = sprd_cam_domain_disable,
};
