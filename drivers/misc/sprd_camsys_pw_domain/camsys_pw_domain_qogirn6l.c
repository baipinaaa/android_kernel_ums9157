// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, QogirN6L back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_qogirn6l.c
 * functions: sprd_campw_init/@0x28d4, sprd_cam_pw_on/@0x2dfc,
 *            sprd_cam_pw_off/@0x3570, sprd_cam_domain_eb/@0x2b94,
 *            sprd_cam_domain_disable/@0x2d14
 *
 * QogirN6L has three independent power domains (cam / isp / dcam) which are
 * switched on and off individually through their own syscon pairs.
 */

#define pr_fmt(fmt)	"sprd_campd_qogirn6l: %d %d %s : " fmt

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

#define QOGIRN6L_POLL_MAX	30

struct qogirn6l_syscon {
	struct regmap *map;	/* +0x0 */
	u32 reg;		/* +0x8 */
	u32 mask;		/* +0xc */
};

struct qogirn6l_priv {
	struct clk *clk_mm_eb;			/* 0x620 */
	struct clk *clk_ckg_en;			/* 0x628 */
	struct clk *clk_mm_mtx_data_en;		/* 0x630 */
	struct clk *clk_mm_mtx_data;		/* 0x638 */
	struct clk *clk_blk_cfg_en;		/* 0x640 */
	struct clk *sys_cfg_mtx_busmon_en;	/* 0x648 */
	struct clk *sys_mst_busmon_en;		/* 0x650 */
	struct clk *clk_mm_mtx_defalut;		/* 0x658 */
	struct clk *clk_mm_mtx_parent;		/* 0x660 */
	struct qogirn6l_syscon syscon[10];	/* 0x668 */
};

#define PRIV(info)	((struct qogirn6l_priv *)((u8 *)(info) + 0x620))

/*
 * force_shutdown, shutdown_en, camera_power_state, isp_force_shutdown,
 * isp_shutdown_en, isp_power_state, dcam_force_shutdown, dcam_shutdown_en,
 * dcam_power_state, aon_apb_mm_eb
 */
static const char * const qogirn6l_syscon_name[] = {
	"force_shutdown",
	"shutdown_en",
	"camera_power_state",
	"isp_force_shutdown",
	"isp_shutdown_en",
	"isp_power_state",
	"dcam_force_shutdown",
	"dcam_shutdown_en",
	"dcam_power_state",
	"aon_apb_mm_eb",
};

static int sprd_campw_init(struct platform_device *pdev,
			   struct camsys_power_info *info)
{
	struct qogirn6l_priv *priv = PRIV(info);
	struct device_node *nd = pdev->dev.of_node;
	u32 args[2];
	int i;

	priv->clk_mm_eb = of_clk_get_by_name(nd, "clk_mm_eb");
	if (IS_ERR_OR_NULL(priv->clk_mm_eb))
		return -ENOMEM;

	priv->clk_ckg_en = of_clk_get_by_name(nd, "clk_ckg_en");
	if (IS_ERR_OR_NULL(priv->clk_ckg_en))
		return -ENOMEM;

	priv->clk_mm_mtx_data_en = of_clk_get_by_name(nd,
						      "clk_mm_mtx_data_en");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_data_en))
		return -ENOMEM;

	priv->clk_mm_mtx_data = of_clk_get_by_name(nd, "clk_mm_mtx_data");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_data))
		return -ENOMEM;

	priv->clk_blk_cfg_en = of_clk_get_by_name(nd, "clk_blk_cfg_en");
	if (IS_ERR_OR_NULL(priv->clk_blk_cfg_en))
		return -ENOMEM;

	priv->sys_cfg_mtx_busmon_en = of_clk_get_by_name(nd,
						"sys_cfg_mtx_busmon_en");
	if (IS_ERR_OR_NULL(priv->sys_cfg_mtx_busmon_en))
		return -ENOMEM;

	priv->sys_mst_busmon_en = of_clk_get_by_name(nd, "sys_mst_busmon_en");
	if (IS_ERR_OR_NULL(priv->sys_mst_busmon_en))
		return -ENOMEM;

	priv->clk_mm_mtx_defalut = of_clk_get_by_name(nd,
						      "clk_mm_mtx_defalut");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_defalut))
		return -ENOMEM;

	priv->clk_mm_mtx_parent = of_clk_get_by_name(nd, "clk_mm_mtx_parent");
	if (IS_ERR_OR_NULL(priv->clk_mm_mtx_parent))
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(priv->syscon); i++) {
		struct regmap *map;

		map = syscon_regmap_lookup_by_phandle_args(nd,
						qogirn6l_syscon_name[i], 2, args);
		if (IS_ERR_OR_NULL(map)) {
			pr_err("fail to read %s regmap\n",
			       qogirn6l_syscon_name[i]);
			continue;
		}

		priv->syscon[i].map = map;
		priv->syscon[i].reg = args[0];
		priv->syscon[i].mask = args[1];
		pr_debug("dts[%s] 0x%x 0x%x\n", qogirn6l_syscon_name[i],
			 args[0], args[1]);
	}

	return 0;
}

static int qogirn6l_clk_enable(struct clk *clk)
{
	int ret;

	if (IS_ERR_OR_NULL(clk))
		return 0;

	ret = clk_prepare(clk);
	if (ret)
		return ret;

	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		return ret;
	}

	return 0;
}

static int sprd_cam_domain_eb(struct camsys_power_info *info)
{
	struct qogirn6l_priv *priv = PRIV(info);
	int ret;

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	ret = qogirn6l_clk_enable(priv->clk_mm_eb);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->clk_ckg_en);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->clk_mm_mtx_data_en);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->clk_mm_mtx_data);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->clk_blk_cfg_en);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->sys_cfg_mtx_busmon_en);
	if (ret)
		return ret;

	ret = qogirn6l_clk_enable(priv->sys_mst_busmon_en);
	if (ret)
		return ret;

	clk_set_parent(priv->clk_mm_mtx_data, priv->clk_mm_mtx_parent);

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *info)
{
	struct qogirn6l_priv *priv = PRIV(info);

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	clk_set_parent(priv->clk_mm_mtx_data, priv->clk_mm_mtx_defalut);

	clk_disable_unprepare(priv->sys_mst_busmon_en);
	clk_disable_unprepare(priv->sys_cfg_mtx_busmon_en);
	clk_disable_unprepare(priv->clk_blk_cfg_en);
	clk_disable_unprepare(priv->clk_mm_mtx_data);
	clk_disable_unprepare(priv->clk_mm_mtx_data_en);
	clk_disable_unprepare(priv->clk_ckg_en);
	clk_disable_unprepare(priv->clk_mm_eb);

	return 0;
}

static int qogirn6l_pw_on_one(struct qogirn6l_syscon *sd,
			      struct qogirn6l_syscon *ss,
			      struct qogirn6l_syscon *sp, int *count)
{
	int i, ret;
	u32 value = 0;

	if (sd->map)
		regmap_update_bits(sd->map, sd->reg, sd->mask, 0);

	if (ss->map)
		regmap_update_bits(ss->map, ss->reg, ss->mask, 0);

	for (i = 0; i < QOGIRN6L_POLL_MAX; i++) {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sp->map)
			continue;

		ret = regmap_read(sp->map, sp->reg, &value);
		if (ret)
			continue;
		value &= sp->mask;

		if (!value) {
			*count = i;
			return 0;
		}
	}

	return -EIO;
}

static int sprd_cam_pw_on(struct camsys_power_info *info)
{
	struct qogirn6l_priv *priv = PRIV(info);
	struct qogirn6l_syscon *sc = priv->syscon;
	int i = 0, k = 0, l = 0;

	pr_debug("power on state %d, cb %pS\n", 1,
		 (void *)__builtin_return_address(0));

	if (qogirn6l_pw_on_one(&sc[0], &sc[1], &sc[2], &i)) {
		pr_err("fail to power on cam sys\n");
		return -EIO;
	}

	if (qogirn6l_pw_on_one(&sc[3], &sc[4], &sc[5], &k)) {
		pr_err("fail to pw on isp domain\n");
		return -EIO;
	}

	if (qogirn6l_pw_on_one(&sc[6], &sc[7], &sc[8], &l)) {
		pr_err("fail to pw on dcam domain\n");
		return -EIO;
	}

	if (priv->clk_blk_cfg_en && clk_prepare_enable(priv->clk_blk_cfg_en)) {
		pr_err("fail to power on blk_cfg_en\n");
		return -EIO;
	}

	pr_info("Done, i %d k %d l %d\n", i, k, l);
	pr_debug("Done, cb: %pS\n", (void *)__builtin_return_address(0));

	return 0;
}

static int qogirn6l_pw_off_one(struct qogirn6l_syscon *sd,
			       struct qogirn6l_syscon *ss,
			       struct qogirn6l_syscon *sp, int *count,
			       bool is_isp)
{
	u32 value = 0, expect;
	int ret, i = 0;

	if (ss->map)
		regmap_update_bits(ss->map, ss->reg, ss->mask, 0);

	if (sd->map)
		regmap_update_bits(sd->map, sd->reg, sd->mask, sd->mask);

	expect = 7u << (sp->mask ? (u32)__builtin_ctz(sp->mask) : 0);

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		if (!sp->map)
			break;

		ret = regmap_read(sp->map, sp->reg, &value);
		if (ret) {
			if (is_isp)
				pr_err("fail to get isp power state 0x%x\n", value);
			else
				pr_err("fail to get dcam power state 0x%x\n",
				       value);
			break;
		}
		value &= sp->mask;

		if (value == expect) {
			*count = i;
			return 0;
		}

		i++;
	} while (i < QOGIRN6L_POLL_MAX);

	if (is_isp)
		pr_err("fail to power off isp , ret %d, read count %d\n",
		       -EIO, i);
	else
		pr_err("fail to power off dcam , ret %d, read count %d\n",
		       -EIO, i);
	return -EIO;
}

static int sprd_cam_pw_off(struct camsys_power_info *info)
{
	struct qogirn6l_priv *priv = PRIV(info);
	struct qogirn6l_syscon *sc = priv->syscon;
	int i = 0, k = 0, l = 0;

	if (priv->clk_blk_cfg_en)
		clk_disable_unprepare(priv->clk_blk_cfg_en);

	if (qogirn6l_pw_off_one(&sc[6], &sc[7], &sc[8], &l, false)) {
		pr_err("fail to power off cam sys, ret %d, read count %d\n",
		       -EIO, l);
		return -EIO;
	}

	if (qogirn6l_pw_off_one(&sc[3], &sc[4], &sc[5], &k, true)) {
		pr_err("fail to power off cam sys, ret %d, read count %d\n",
		       -EIO, k);
		return -EIO;
	}

	if (qogirn6l_pw_off_one(&sc[0], &sc[1], &sc[2], &i, false)) {
		pr_err("fail to pw on cam domain\n");
		return -EIO;
	}

	pr_info("Done, i %d k %d l %d\n", i, k, l);

	return 0;
}

const struct camsys_power_ops camsys_power_ops_qogirn6l = {
	.init = sprd_campw_init,
	.on = sprd_cam_pw_on,
	.off = sprd_cam_pw_off,
	.eb = sprd_cam_domain_eb,
	.disable = sprd_cam_domain_disable,
};
