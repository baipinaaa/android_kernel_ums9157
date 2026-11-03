// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc VPU power domain driver (vendor build found on the Sunelan Q968 /
 * UMS9157 T157 vendor_boot ramdisk, module name "sprd-vpu-pw-domain.ko").
 *
 * This file is a source reconstruction of that module.  It is NOT the same
 * revision as drivers/soc/sprd/domain/sprd_vpu_pm_domain.c (that one is the
 * later, cleaner rewrite which is built with obj-y).  The device revision
 * additionally implements the "cali" (calibration) boot mode, the explicit
 * PMU_APB_PIXELPLL sequencing and a delayed work that re-arms the runtime
 * autosuspend delay after probe.
 *
 * Recovered object layout (devm_kmalloc(1744) == 0x6d0 bytes, NOT zeroed):
 *   0x000  struct device *dev
 *   0x008  struct generic_pm_domain pd     (pd.name at +0x518 -> 0x520,
 *                                          pd.power_off/power_on at
 *                                          0x548/0x550)
 *   0x5f8  struct regmap *auto_shutdown_map
 *   0x600  struct regmap *force_shutdown_map
 *   0x608  struct regmap *status_map
 *   0x610  struct regmap *pixelpll_map
 *   0x618  struct regmap *domain_eb_map
 *   0x620  struct delayed_work delay_work (0x88 bytes)
 *   0x6a8  u32 auto_shutdown_reg, force_shutdown_reg, status_reg,
 *              pixelpll_reg, domain_eb_reg
 *   0x6bc  u32 auto_shutdown_mask, force_shutdown_mask, status_mask,
 *              pixelpll_mask, domain_eb_mask
 *
 * The vendor code prints its own "sprd-vpu-pd: " prefix, so plain printk()
 * is used for all messages except the very first probe trace (dev_info).
 */

#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define VPU_PD_DELAY_US		300
#define VPU_PD_POLL_TIMEOUT	0x62	/* iterations, ~ 0x62 * 300us */
#define VPU_PD_AUTOSUSPEND_MS	3000
#define VPU_PD_DELAY_WORK	(10 * HZ)

struct sprd_vpu_pd {
	struct device *dev;
	struct generic_pm_domain pd;
	struct regmap *auto_shutdown_map;
	struct regmap *force_shutdown_map;
	struct regmap *status_map;
	struct regmap *pixelpll_map;
	struct regmap *domain_eb_map;
	struct delayed_work delay_work;
	u32 auto_shutdown_reg;
	u32 force_shutdown_reg;
	u32 status_reg;
	u32 pixelpll_reg;
	u32 domain_eb_reg;
	u32 auto_shutdown_mask;
	u32 force_shutdown_mask;
	u32 status_mask;
	u32 pixelpll_mask;
	u32 domain_eb_mask;
};

/*
 * The line numbers reported by the original module were 186/199 (power off),
 * 115/133/143 (power on), 232 (probe) and 156 (cali shutdown).
 */
static int vpu_pw_off(struct generic_pm_domain *domain)
{
	struct sprd_vpu_pd *pd = container_of(domain, struct sprd_vpu_pd, pd);
	int ret;

	if (!pd->force_shutdown_map) {
		printk(KERN_INFO "sprd-vpu-pd: skip power off\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(pd->force_shutdown_map,
				 pd->force_shutdown_reg,
				 pd->force_shutdown_mask,
				 pd->force_shutdown_mask);
	if (ret) {
		printk(KERN_ERR "sprd-vpu-pd: regmap_update_bits failed %s, %d\n",
		       __func__, __LINE__);
		return ret;
	}

	if (pd->pixelpll_map) {
		printk(KERN_INFO "sprd-vpu-pd: PMU_APB_PIXELPLL pw off\n");
		ret = regmap_update_bits(pd->pixelpll_map,
					 pd->pixelpll_reg,
					 pd->pixelpll_mask,
					 ~pd->pixelpll_mask);
		if (ret) {
			printk(KERN_ERR "sprd-vpu-pd: regmap_update_bits failed %s, %d\n",
			       __func__, __LINE__);
			return ret;
		}
	}

	printk(KERN_INFO "sprd-vpu-pd: %s: %s OK\n", pd->pd.name, __func__);

	return 0;
}

static int vpu_pw_on(struct generic_pm_domain *domain)
{
	struct sprd_vpu_pd *pd = container_of(domain, struct sprd_vpu_pd, pd);
	u32 state = 0;
	int ret, i;

	if (pd->pixelpll_map) {
		printk(KERN_INFO "sprd-vpu-pd: PMU_APB_PIXELPLL pw on \n");
		ret = regmap_update_bits(pd->pixelpll_map,
					 pd->pixelpll_reg,
					 pd->pixelpll_mask,
					 pd->pixelpll_mask);
		if (ret) {
			printk(KERN_ERR "sprd-vpu-pd: regmap_update_bits failed %s, %d\n",
			       __func__, __LINE__);
			return ret;
		}
		udelay(VPU_PD_DELAY_US);
	}

	if (!pd->auto_shutdown_map) {
		printk(KERN_INFO "sprd-vpu-pd: skip power on\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(pd->auto_shutdown_map,
				 pd->auto_shutdown_reg,
				 pd->auto_shutdown_mask,
				 ~pd->auto_shutdown_mask);
	if (ret) {
		printk(KERN_ERR "sprd-vpu-pd: regmap_update_bits failed %s, %d\n",
		       __func__, __LINE__);
		return ret;
	}

	ret = regmap_update_bits(pd->force_shutdown_map,
				 pd->force_shutdown_reg,
				 pd->force_shutdown_mask,
				 ~pd->force_shutdown_mask);
	if (ret) {
		printk(KERN_ERR "sprd-vpu-pd: regmap_update_bits failed %s, %d\n",
		       __func__, __LINE__);
		return ret;
	}

	udelay(VPU_PD_DELAY_US);

	/* Poll until the power status bit is cleared (0 == powered up). */
	for (i = 0; i <= VPU_PD_POLL_TIMEOUT; i++) {
		udelay(VPU_PD_DELAY_US);
		regmap_read(pd->status_map, pd->status_reg, &state);
		if (!(state & pd->status_mask))
			break;
	}

	if (state & pd->status_mask) {
		printk(KERN_ERR "sprd-vpu-pd: %s set failed 0x%x\n",
		       __func__, state);
		return -ETIMEDOUT;
	}

	printk(KERN_INFO "sprd-vpu-pd: %s: %s OK\n", pd->pd.name, __func__);

	return 0;
}

static void vpu_delay_work(struct work_struct *work)
{
	struct sprd_vpu_pd *pd = container_of(to_delayed_work(work),
					      struct sprd_vpu_pd, delay_work);

	pm_runtime_set_autosuspend_delay(pd->dev, VPU_PD_AUTOSUSPEND_MS);
	dev_info(pd->dev, "vpu pd delay work done!\n");
}

static int vpu_pd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *chosen;
	struct of_phandle_args parent;
	struct of_phandle_args child;
	struct sprd_vpu_pd *pd;
	const char *bootargs;
	u32 args[2] = { 0 };
	bool cali = false;
	int ret;

	dev_info(&pdev->dev, "%s, %d\n", __func__, __LINE__);

	pd = devm_kmalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->dev = &pdev->dev;
	pd->pd.name = kstrdup(np->name, GFP_KERNEL);
	pd->pd.power_off = vpu_pw_off;
	pd->pd.power_on = vpu_pw_on;

	pd->auto_shutdown_map =
		syscon_regmap_lookup_by_phandle_args(np,
			"pmu-vpu-auto-shutdown-syscon", 2, args);
	if (IS_ERR(pd->auto_shutdown_map)) {
		dev_err(&pdev->dev, "Read Vpu Dts %s regmap fail\n",
			"pmu-vpu-auto-shutdown-syscon");
		pd->auto_shutdown_map = NULL;
		pd->auto_shutdown_reg = 0;
		pd->auto_shutdown_mask = 0;
	} else {
		pd->auto_shutdown_reg = args[0];
		pd->auto_shutdown_mask = args[1];
		dev_info(&pdev->dev,
			 "VPU syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			 "pmu-vpu-auto-shutdown-syscon", pd->auto_shutdown_map,
			 pd->auto_shutdown_reg, pd->auto_shutdown_mask);
	}

	pd->force_shutdown_map =
		syscon_regmap_lookup_by_phandle_args(np,
			"pmu-vpu-force-shutdown-syscon", 2, args);
	if (IS_ERR(pd->force_shutdown_map)) {
		dev_err(&pdev->dev, "Read Vpu Dts %s regmap fail\n",
			"pmu-vpu-force-shutdown-syscon");
		pd->force_shutdown_map = NULL;
		pd->force_shutdown_reg = 0;
		pd->force_shutdown_mask = 0;
	} else {
		pd->force_shutdown_reg = args[0];
		pd->force_shutdown_mask = args[1];
		dev_info(&pdev->dev,
			 "VPU syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			 "pmu-vpu-force-shutdown-syscon", pd->force_shutdown_map,
			 pd->force_shutdown_reg, pd->force_shutdown_mask);
	}

	pd->status_map =
		syscon_regmap_lookup_by_phandle_args(np,
			"pmu-pwr-status-syscon", 2, args);
	if (IS_ERR(pd->status_map)) {
		dev_err(&pdev->dev, "Read Vpu Dts %s regmap fail\n",
			"pmu-pwr-status-syscon");
		pd->status_map = NULL;
		pd->status_reg = 0;
		pd->status_mask = 0;
	} else {
		pd->status_reg = args[0];
		pd->status_mask = args[1];
		dev_info(&pdev->dev,
			 "VPU syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			 "pmu-pwr-status-syscon", pd->status_map,
			 pd->status_reg, pd->status_mask);
	}

	pd->pixelpll_map =
		syscon_regmap_lookup_by_phandle_args(np,
			"pmu-apb-pixelpll-syscon", 2, args);
	if (IS_ERR(pd->pixelpll_map)) {
		dev_err(&pdev->dev, "Read Vpu Dts %s regmap fail\n",
			"pmu-apb-pixelpll-syscon");
		pd->pixelpll_map = NULL;
		pd->pixelpll_reg = 0;
		pd->pixelpll_mask = 0;
	} else {
		pd->pixelpll_reg = args[0];
		pd->pixelpll_mask = args[1];
		dev_info(&pdev->dev,
			 "VPU syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			 "pmu-apb-pixelpll-syscon", pd->pixelpll_map,
			 pd->pixelpll_reg, pd->pixelpll_mask);
	}

	pd->domain_eb_map =
		syscon_regmap_lookup_by_phandle_args(np,
			"vpu-domain-eb-syscon", 2, args);
	if (IS_ERR(pd->domain_eb_map)) {
		dev_err(&pdev->dev, "Read Vpu Dts %s regmap fail\n",
			"vpu-domain-eb-syscon");
		pd->domain_eb_map = NULL;
		pd->domain_eb_reg = 0;
		pd->domain_eb_mask = 0;
	} else {
		pd->domain_eb_reg = args[0];
		pd->domain_eb_mask = args[1];
		dev_info(&pdev->dev,
			 "VPU syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			 "vpu-domain-eb-syscon", pd->domain_eb_map,
			 pd->domain_eb_reg, pd->domain_eb_mask);
	}

	chosen = of_find_node_opts_by_path("/chosen", NULL);
	if (!of_property_read_string(chosen, "bootargs", &bootargs) &&
	    strstr(bootargs, "androidboot.mode=cali")) {
		printk(KERN_INFO "sprd-vpu-pd: cali mode enter success!\n");

		if (pd->domain_eb_map) {
			ret = regmap_update_bits(pd->domain_eb_map,
						 pd->domain_eb_reg,
						 pd->domain_eb_mask,
						 ~pd->domain_eb_mask);
			if (ret) {
				printk(KERN_ERR "sprd-vpu-pd: cali regmap_update_bits failed %s, %d\n",
				       __func__, __LINE__);
				printk(KERN_ERR "sprd-vpu-pd: %s: calibration mode vpu shutdown failed\n",
				       __func__);
				cali = true;
				goto cali_done;
			}
		} else {
			printk(KERN_INFO "sprd-vpu-pd: NO VPU_DOMAIN_EB,bypass\n");
		}

		printk(KERN_INFO "sprd-vpu-pd: %s: calibration mode and not probe\n",
		       __func__);
		cali = true;
	}

cali_done:
	pm_genpd_init(&pd->pd, NULL, true);
	of_genpd_add_provider_simple(np, &pd->pd);

	child.np = np;
	child.args_count = 0;
	ret = of_parse_phandle_with_args(np, "power-domains",
					 "#power-domain-cells", 0, &parent);
	if (!ret) {
		if (of_genpd_add_subdomain(&parent, &child))
			printk(KERN_ERR "sprd-vpu-pd: %pOF failed to add subdomain: %pOF\n",
			       child.np, parent.np);
		else
			printk(KERN_INFO "sprd-vpu-pd: %pOF has as child subdomain: %pOF.\n",
			       child.np, parent.np);
	}

	__pm_runtime_set_status(&pdev->dev, RPM_ACTIVE);

	if (cali) {
		pm_runtime_enable(&pdev->dev);
		return 0;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, -1);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	INIT_DELAYED_WORK(&pd->delay_work, vpu_delay_work);
	queue_delayed_work(system_wq, &pd->delay_work, VPU_PD_DELAY_WORK);

	return 0;
}

static const struct of_device_id vpu_pd_of_match[] = {
	{ .compatible = "sprd,vpu-pd" },
	{ }
};
MODULE_DEVICE_TABLE(of, vpu_pd_of_match);

static struct platform_driver vpu_pd_driver = {
	.probe = vpu_pd_probe,
	.driver = {
		.name = "sprd-vpu-pd",
		.of_match_table = vpu_pd_of_match,
	},
};
module_platform_driver(vpu_pd_driver);

MODULE_AUTHOR("Chunlei Guo <chunlei.guo@unisoc.com>");
MODULE_DESCRIPTION("Spreadtrum vpu power domain driver");
MODULE_LICENSE("GPL v2");
