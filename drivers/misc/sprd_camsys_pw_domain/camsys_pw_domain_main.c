// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc camera subsystem power domain driver, generic part.
 *
 * Reconstructed from sprd_camsys_pw_domain.ko (.text 0x6d18 bytes,
 * 55 functions).  The binary contains eight back ends, one per supported
 * SoC, all selected through the device tree compatible string.
 */

#define pr_fmt(fmt)	"sprd_campd: %d %d %s : " fmt

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

#include "sprd_camsys_pw_domain.h"

/* .bss+0x000 */
struct camsys_power_info *pw_info;
static struct lock_class_key cam_pw_mlock_key;

/* .data+0x078, exported to the camera drivers through the notify API */
static BLOCKING_NOTIFIER_HEAD(mmsys_chain);

/* ------------------------------------------------------------------ */
/* notify API (0x222c / 0x2254, 40 bytes each)                        */
/* ------------------------------------------------------------------ */

int sprd_mm_pw_notify_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mmsys_chain, nb);
}
EXPORT_SYMBOL(sprd_mm_pw_notify_unregister);

int sprd_mm_pw_notify_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mmsys_chain, nb);
}
EXPORT_SYMBOL(sprd_mm_pw_notify_register);

/* ------------------------------------------------------------------ */
/* Global camera mm power on/off (0x227c / 0x24dc)                    */
/* ------------------------------------------------------------------ */

int sprd_glb_mm_pw_on_cfg(void)
{
	struct camsys_power_info *info = pw_info;
	int ret = 0;

	if (!info || !info->inited) {
		pr_err("fail to check drv init. cb: %p, ret %d\n",
		       (void *)info, -ENODEV);
		return -ENODEV;
	}

	pr_debug("power on state %d, cb %p\n", atomic_read(&info->refcnt),
		 (void *)info);
	mutex_lock(&info->mlock);

	if (atomic_inc_return(&info->refcnt) == 1) {
		ret = info->ops->eb(1);
		if (ret)
			goto err;

		ret = info->ops->on(info);
		if (ret)
			goto err;

		blocking_notifier_call_chain(&mmsys_chain, 1, NULL);
	}

	mutex_unlock(&info->mlock);
	return 0;

err:
	atomic_dec(&info->refcnt);
	mutex_unlock(&info->mlock);
	return ret;
}
EXPORT_SYMBOL(sprd_glb_mm_pw_on_cfg);

int sprd_glb_mm_pw_off_cfg(void)
{
	struct camsys_power_info *info = pw_info;
	int ret = 0;

	if (!info || !info->inited) {
		pr_err("fail to check drv init. cb: %p, ret %d\n",
		       (void *)info, -ENODEV);
		return -ENODEV;
	}

	pr_debug("power off state %d, cb %p\n", atomic_read(&info->refcnt),
		 (void *)info);
	mutex_lock(&info->mlock);

	if (atomic_dec_return(&info->refcnt) == 0) {
		blocking_notifier_call_chain(&mmsys_chain, 0, NULL);

		ret = info->ops->off(info);
		if (ret)
			goto err;

		ret = info->ops->disable(0);
		if (ret)
			goto err;
	}

	mutex_unlock(&info->mlock);
	return 0;

err:
	mutex_unlock(&info->mlock);
	return ret;
}
EXPORT_SYMBOL(sprd_glb_mm_pw_off_cfg);

/* ------------------------------------------------------------------ */
/* platform driver (0x26a4 probe, 0x2838 remove)                      */
/* ------------------------------------------------------------------ */

#define CAMPW_PRIV_SIZE		0x160

static const struct of_device_id sprd_campw_match_table[] = {
	{ .compatible = "sprd,pike2-camsys-domain",
	  .data = &camsys_power_ops_pike2 },
	{ .compatible = "sprd,sharkl3-camsys-domain",
	  .data = &camsys_power_ops_l3 },
	{ .compatible = "sprd,sharkl5pro-camsys-domain",
	  .data = &camsys_power_ops_l5pro },
	{ .compatible = "sprd,sharkle-camsys-domain",
	  .data = &camsys_power_ops_le },
	{ .compatible = "sprd,qogirl6-camsys-domain",
	  .data = &camsys_power_ops_qogirl6 },
	{ .compatible = "sprd,qogirn6pro-camsys-domain",
	  .data = &camsys_power_ops_qogirn6pro },
	{ .compatible = "sprd,sharkl5-camsys-domain",
	  .data = &camsys_power_ops_l5 },
	{ .compatible = "sprd,qogirl6l-camsys-domain",
	  .data = &camsys_power_ops_qogirn6l },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_campw_match_table);


static int sprd_campw_probe(struct platform_device *pdev)
{
	struct device_node *nd = pdev->dev.of_node;
	struct camsys_power_info *info;
	const struct of_device_id *match;
	int ret;

	info = devm_kmalloc(&pdev->dev,
			    sizeof(struct camsys_power_info) + CAMPW_PRIV_SIZE,
			    GFP_KERNEL);
	pw_info = info;
	if (!info || IS_ERR(info)) {
		pr_err("fail to alloc pw_info\n");
		return -ENOMEM;
	}

	match = of_match_node(sprd_campw_match_table, nd);
	if (!match || !match->data || IS_ERR(match->data)) {
		pr_err("fail to parse sprd_campw_match_table item\n");
		return -ENODEV;
	}

	info->ops = match->data;

	ret = info->ops->init(pdev, info);
	if (ret)
		return ret;

	info->domain_name = kstrdup(nd->name, GFP_KERNEL);
	info->genpd.name = info->domain_name;
	pm_genpd_init(&info->genpd, NULL, true);
	of_genpd_add_provider_simple(nd, &info->genpd);
	__mutex_init(&info->mlock, "&pw_info->mlock", &cam_pw_mlock_key);
	platform_set_drvdata(pdev, info);
	info->inited = 1;

	return 0;
}

static int sprd_campw_remove(struct platform_device *pdev)
{
	struct camsys_power_info *info = platform_get_drvdata(pdev);

	if (!info || !info->inited) {
		pr_err("fail to check drv init: cb: %p, ret %d\n",
		       (void *)info, -ENODEV);
		return -ENODEV;
	}

	pm_genpd_remove(&info->genpd);
	kfree(info->domain_name);
	info->inited = 0;

	return 0;
}

static struct platform_driver sprd_campw_driver = {
	.probe = sprd_campw_probe,
	.remove = sprd_campw_remove,
	.driver = {
		.name = "sprd-camsys-pw-domain",
		.of_match_table = sprd_campw_match_table,
	},
};

static int __init sprd_campw_driver_init(void)
{
	return platform_driver_register(&sprd_campw_driver);
}

static void __exit sprd_campw_driver_exit(void)
{
	platform_driver_unregister(&sprd_campw_driver);
}

module_init(sprd_campw_driver_init);
module_exit(sprd_campw_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Unisoc camera subsystem power domain driver");
MODULE_AUTHOR("Unisoc camera team");
