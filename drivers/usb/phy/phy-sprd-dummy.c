// SPDX-License-Identifier: GPL-2.0
/*
 * Spreadtrum/Unisoc dummy USB PHY.
 *
 * Source-level reconstruction of the vendor module "phy-sprd-dummy.ko"
 * (label "sprd-dummy-phy", compatible "sprd,usb-dummy-phy") from the
 * vendor_boot ramdisk of the Sunelan Q968 (UMS9157 / T157).
 *
 * The binary is a stub PHY: it allocates a struct usb_phy, fills in the
 * callbacks (all of which return 0 / do nothing), registers it with
 * usb_add_phy_dev() and enables runtime PM.  The device tree node
 * "dummyphy" (base DTB, compatible "sprd,usb-dummy-phy") binds to it.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/usb/phy.h>

struct sprd_dummy_phy {
	struct usb_phy phy;
};

static int sprd_dummy_phy_init(struct usb_phy *phy)
{
	return 0;
}

static void sprd_dummy_phy_shutdown(struct usb_phy *phy)
{
}

static int sprd_dummy_phy_set_vbus(struct usb_phy *phy, int on)
{
	return 0;
}

static int sprd_dummy_phy_vbus_notify(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	return 0;
}

static int sprd_dummy_phy_notify_connect(struct usb_phy *phy,
					 enum usb_device_speed speed)
{
	return 0;
}

static int sprd_dummy_phy_notify_disconnect(struct usb_phy *phy,
					    enum usb_device_speed speed)
{
	return 0;
}

static enum usb_charger_type sprd_dummy_phy_charger_detect(struct usb_phy *phy)
{
	return UNKNOWN_TYPE;
}

static int sprd_dummy_phy_probe(struct platform_device *pdev)
{
	struct sprd_dummy_phy *sphy;
	struct usb_phy *phy;
	int ret;

	dev_info(&pdev->dev, "%s enter\n", __func__);

	sphy = devm_kmalloc(&pdev->dev, sizeof(*sphy), GFP_KERNEL | __GFP_ZERO);
	if (!sphy)
		return -ENOMEM;

	phy = &sphy->phy;
	platform_set_drvdata(pdev, sphy);

	phy->dev = &pdev->dev;
	phy->label = "sprd-dummy-phy";
	phy->type = USB_PHY_TYPE_USB2;

	phy->vbus_nb.notifier_call = sprd_dummy_phy_vbus_notify;
	phy->init = sprd_dummy_phy_init;
	phy->shutdown = sprd_dummy_phy_shutdown;
	phy->set_vbus = sprd_dummy_phy_set_vbus;
	phy->notify_connect = sprd_dummy_phy_notify_connect;
	phy->notify_disconnect = sprd_dummy_phy_notify_disconnect;
	phy->charger_detect = sprd_dummy_phy_charger_detect;

	ret = usb_add_phy_dev(phy);
	if (ret)
		dev_err(&pdev->dev, "fail to add phy\n");
	else
		pm_runtime_enable(&pdev->dev);

	return ret;
}

static int sprd_dummy_phy_remove(struct platform_device *pdev)
{
	struct sprd_dummy_phy *sphy = platform_get_drvdata(pdev);

	usb_remove_phy(&sphy->phy);
	return 0;
}

static const struct of_device_id sprd_dummy_phy_match[] = {
	{ .compatible = "sprd,usb-dummy-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_dummy_phy_match);

static struct platform_driver sprd_dummy_phy_driver = {
	.probe = sprd_dummy_phy_probe,
	.remove = sprd_dummy_phy_remove,
	.driver = {
		.name = "sprd-dummy-phy",
		.of_match_table = sprd_dummy_phy_match,
	},
};

static int __init sprd_dummy_phy_module_init(void)
{
	return platform_driver_register(&sprd_dummy_phy_driver);
}

static void __exit sprd_dummy_phy_module_exit(void)
{
	platform_driver_unregister(&sprd_dummy_phy_driver);
}

module_init(sprd_dummy_phy_module_init);
module_exit(sprd_dummy_phy_module_exit);

MODULE_DESCRIPTION("DesignWare USB SPRD Dummy PHY");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Surong Pang <surong.pang@unisoc.com>");
