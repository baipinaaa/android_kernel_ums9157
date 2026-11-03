// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain, Pike2 (r4p0) back end.
 *
 * Reconstructed from the vendor source file
 *   bsp/modules/common/camera/power/kernel/sprd_camsys_pw_domain_pike2.c
 * functions: sprd_cam_pw_domain_init/@0x5af4, sprd_cam_pw_on/@0x6004,
 *            sprd_cam_pw_off/@0x593c, sprd_cam_domain_eb/@0x5e20,
 *            sprd_cam_domain_disable/@0x5f5c
 *
 * Unlike the other back ends, Pike2 looks its three syscons up by plain
 * phandle (no register/mask args) and therefore uses fixed register
 * offsets, which are named below.
 */

#define pr_fmt(fmt)	"sprd_campd_r4p0: %d %d %s : " fmt

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

/* camera power control registers (aon_apb_gpr / cam_ahb_gpr / pmu_apb_gpr) */
#define CAM_PWR_FORCE_SHUTDOWN		0x0038
#define CAM_PWR_SHUTDOWN_EN		0x003c
#define CAM_PWR_POWER_STATE		0x0040
#define CAM_PWR_LPC_CFG			0x0044
#define CAM_PWR_CHIP_ID0		0x0050
#define CAM_PWR_CHIP_ID1		0x0054

#define CAM_PWR_SHUTDOWN_MASK		BIT(0)
#define CAM_PWR_STATE_MASK		GENMASK(2, 0)
#define CAM_PWR_OFF_STATE		GENMASK(2, 0)

#define R4P0_POLL_MAX			30

struct r4p0_priv {
	struct clk *cam_ahb_clk;		/* 0x620 */
	struct clk *cam_ahb_clk_default;	/* 0x628 */
	struct clk *cam_ahb_clk_parent;		/* 0x630 */
	struct clk *cam_mm_eb;			/* 0x638 */
	struct clk *cam_ahb_clk_p;		/* 0x640 (saved parent) */
	struct clk *cam_ckg_eb;			/* 0x648 */
	struct regmap *aon_apb_gpr;		/* 0x650 */
	struct regmap *cam_ahb_gpr;		/* 0x658 */
	struct regmap *pmu_apb_gpr;		/* 0x660 */
	u32 chip_id0;				/* 0x668 */
	u32 chip_id1;				/* 0x66c */
};

#define PRIV(info)	((struct r4p0_priv *)((u8 *)(info) + 0x620))

static int sprd_cam_pw_domain_init(struct platform_device *pdev,
				   struct camsys_power_info *info)
{
	struct r4p0_priv *priv = PRIV(info);
	struct device *dev = &pdev->dev;
	struct device_node *nd = pdev->dev.of_node;

	priv->cam_ahb_clk = devm_clk_get(dev, "cam-ahb-clk");
	if (IS_ERR_OR_NULL(priv->cam_ahb_clk)) {
		pr_err("cam pw domain init fail, cam_ahb_clk\n");
		return -ENOMEM;
	}

	priv->cam_ahb_clk_default = devm_clk_get(dev, "cam-ahb-clk-default");
	if (IS_ERR_OR_NULL(priv->cam_ahb_clk_default)) {
		pr_err("cam pw domain init fail, cam_ahb_clk_default\n");
		return -ENOMEM;
	}

	priv->cam_ahb_clk_parent = devm_clk_get(dev, "cam-ahb-clk-parent");
	if (IS_ERR_OR_NULL(priv->cam_ahb_clk_parent)) {
		pr_err("cam pw domain init fail, cam_ahb_clk_parent\n");
		return -ENOMEM;
	}

	priv->cam_mm_eb = devm_clk_get(dev, "cam-mm-eb");
	if (IS_ERR_OR_NULL(priv->cam_mm_eb)) {
		pr_err("cam pw domain init fail, cam_mm_eb\n");
		return -ENOMEM;
	}

	priv->cam_ahb_clk_p = clk_get_parent(priv->cam_ahb_clk);
	if (IS_ERR_OR_NULL(priv->cam_ahb_clk_p))
		return -ENOMEM;

	priv->cam_ckg_eb = devm_clk_get(dev, "cam-ckg-eb");
	if (IS_ERR_OR_NULL(priv->cam_ckg_eb)) {
		pr_err("cam pw domain init fail, cam_ckg_eb\n");
		return -ENOMEM;
	}

	priv->aon_apb_gpr = syscon_regmap_lookup_by_phandle(nd,
							"sprd,aon-apb-syscon");
	if (IS_ERR_OR_NULL(priv->aon_apb_gpr)) {
		pr_err("cam pw domain init fail, aon_apb_gpr\n");
		return -ENODEV;
	}

	priv->cam_ahb_gpr = syscon_regmap_lookup_by_phandle(nd,
							"sprd,cam-ahb-syscon");
	if (IS_ERR_OR_NULL(priv->cam_ahb_gpr)) {
		pr_err("cam pw domain init fail, cam_ahb_gpr\n");
		return -ENODEV;
	}

	priv->pmu_apb_gpr = syscon_regmap_lookup_by_phandle(nd,
							"sprd,syscon-pmu-apb");
	if (IS_ERR_OR_NULL(priv->pmu_apb_gpr)) {
		pr_err("cam pw domain init fail, pmu_apb_gpr\n");
		return -ENODEV;
	}

	if (regmap_read(priv->aon_apb_gpr, CAM_PWR_CHIP_ID0,
			&priv->chip_id0)) {
		pr_err("Read chip id0 error\n");
		return -EIO;
	}

	if (regmap_read(priv->aon_apb_gpr, CAM_PWR_CHIP_ID1,
			&priv->chip_id1)) {
		pr_err("Read chip id1 error\n");
		return -EIO;
	}

	pr_info("chip_id0 %x, chip_id1 %x\n", priv->chip_id0, priv->chip_id1);
	pr_info("cam_pw_domain: cam_pw_domain_init.\n");

	return 0;
}

/* vendor helper that hands the LPC setting to the two APB syscons */
static void sprd_mm_lpc_ctrl(struct r4p0_priv *priv, bool on)
{
	pr_debug("%s\n", __func__);

	if (regmap_update_bits(priv->cam_ahb_gpr, CAM_PWR_LPC_CFG, BIT(0), on))
		pr_err("reg config fail\n");

	if (regmap_update_bits(priv->pmu_apb_gpr, CAM_PWR_LPC_CFG, BIT(0), on))
		pr_err("reg config fail\n");
}

static void sprd_mm_lpc_ctrl_delay(void)
{
	__const_udelay(0x418958);
}

static int sprd_cam_domain_eb(struct camsys_power_info *info)
{
	struct r4p0_priv *priv = PRIV(info);
	int ret;

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	ret = clk_prepare(priv->cam_ahb_clk);
	if (ret)
		return ret;
	ret = clk_enable(priv->cam_ahb_clk);
	if (ret) {
		clk_unprepare(priv->cam_ahb_clk);
		return ret;
	}

	clk_set_parent(priv->cam_ahb_clk, priv->cam_ahb_clk_parent);

	ret = clk_prepare(priv->cam_mm_eb);
	if (ret)
		return ret;
	ret = clk_enable(priv->cam_mm_eb);
	if (ret) {
		clk_unprepare(priv->cam_mm_eb);
		return ret;
	}

	sprd_mm_lpc_ctrl(priv, true);

	sprd_mm_lpc_ctrl_delay();

	sprd_mm_lpc_ctrl(priv, false);

	pr_debug("open mm lpc\n");

	return 0;
}

static int sprd_cam_domain_disable(struct camsys_power_info *info)
{
	struct r4p0_priv *priv = PRIV(info);

	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));

	clk_set_parent(priv->cam_ahb_clk, priv->cam_ahb_clk_default);
	clk_disable(priv->cam_ahb_clk);
	clk_unprepare(priv->cam_ahb_clk);

	clk_disable(priv->cam_mm_eb);
	clk_unprepare(priv->cam_mm_eb);

	clk_disable(priv->cam_ckg_eb);
	clk_unprepare(priv->cam_ckg_eb);

	return 0;
}

static int sprd_cam_pw_on(struct camsys_power_info *info)
{
	struct r4p0_priv *priv = PRIV(info);
	u32 value0 = 0, value1 = 0;
	int ret, i;

	dmb();

	regmap_update_bits(priv->pmu_apb_gpr, CAM_PWR_SHUTDOWN_EN,
			   CAM_PWR_SHUTDOWN_MASK, 0);
	regmap_update_bits(priv->aon_apb_gpr, CAM_PWR_FORCE_SHUTDOWN,
			   CAM_PWR_SHUTDOWN_MASK, 0);

	for (i = 0; i < R4P0_POLL_MAX; i++) {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		ret = regmap_read(priv->aon_apb_gpr, CAM_PWR_POWER_STATE,
				  &value0);
		if (ret)
			continue;
		ret = regmap_read(priv->cam_ahb_gpr, CAM_PWR_POWER_STATE,
				  &value1);
		if (ret)
			continue;

		value0 &= CAM_PWR_STATE_MASK;
		value1 &= CAM_PWR_STATE_MASK;

		if (!value0 && !value1) {
			pr_info("Done, i %d\n", i);
			return 0;
		}
	}

	pr_err("fail to pw on mm domain \n");
	return -EIO;
}

static int sprd_cam_pw_off(struct camsys_power_info *info)
{
	struct r4p0_priv *priv = PRIV(info);
	u32 value = 0;
	int ret, i = 0;

	regmap_update_bits(priv->pmu_apb_gpr, CAM_PWR_SHUTDOWN_EN,
			   CAM_PWR_SHUTDOWN_MASK, CAM_PWR_SHUTDOWN_MASK);
	regmap_update_bits(priv->aon_apb_gpr, CAM_PWR_FORCE_SHUTDOWN,
			   CAM_PWR_SHUTDOWN_MASK, CAM_PWR_SHUTDOWN_MASK);

	pr_info("cam_pw_domain:cam_pw_off set OK.\n");

	do {
		usleep_range(CAMPW_POLL_US_MIN, CAMPW_POLL_US_MAX);

		ret = regmap_read(priv->aon_apb_gpr, CAM_PWR_POWER_STATE,
				  &value);
		if (ret)
			break;
		value &= CAM_PWR_STATE_MASK;

		if (value == CAM_PWR_OFF_STATE)
			return 0;

		i++;
	} while (i < R4P0_POLL_MAX);

	pr_err("cam domain pw off failed 0x%x\n", value);
	pr_err("cam domain pw off failed, ret: %d, count: %d!\n", -EIO, i);
	pr_debug("cb %pS\n", (void *)__builtin_return_address(0));
	return -EIO;
}

const struct camsys_power_ops camsys_power_ops_pike2 = {
	.init = sprd_cam_pw_domain_init,
	.on = sprd_cam_pw_on,
	.off = sprd_cam_pw_off,
	.eb = sprd_cam_domain_eb,
	.disable = sprd_cam_domain_disable,
};
