// SPDX-License-Identifier: GPL-2.0
/*
 * Spreadtrum/Unisoc "common" USB PHY event notifier.
 *
 * Source-level reconstruction of the vendor module "phy-sprd-commonphy.ko"
 * (description "DesignWare SPRD USBPHY_EVENT") from the vendor_boot ramdisk
 * of the Sunelan Q968 (UMS9157 / T157).
 *
 * Unlike the other PHY modules this one registers no platform driver: it
 * simply owns a small notifier hub that other modules (charger, type-C,
 * displayport) can register with through the two exported symbols
 * register_sprd_usbphy_notifier() and call_sprd_usbphy_event_notifiers().
 *
 * Recovered from the binary:
 *   - a 16-byte hub object allocated with kmalloc() at module init,
 *     { int id; struct raw_notifier_head nh; }, id initialised to 1;
 *   - both entry points reject anything but id 0 with -EINVAL and also
 *     return -EINVAL when the hub has not been initialised yet;
 *   - the call path prints KERN_INFO "[%s]id(%d),val(%ld)\n" before
 *     walking the chain (the compiler passes a literal 0 for id because the
 *     only surviving path is id == 0).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/slab.h>

struct sprd_usbphy_event {
	int id;
	struct raw_notifier_head nh;
};

static struct sprd_usbphy_event *usbphy_event_sprd;

int register_sprd_usbphy_notifier(struct notifier_block *nb, int id)
{
	if (id)
		return -EINVAL;
	if (!nb)
		return -EINVAL;
	if (!usbphy_event_sprd)
		return -EINVAL;

	return raw_notifier_chain_register(&usbphy_event_sprd->nh, nb);
}
EXPORT_SYMBOL(register_sprd_usbphy_notifier);

int call_sprd_usbphy_event_notifiers(int id, unsigned long val, void *data)
{
	if (id)
		return -EINVAL;
	if (!usbphy_event_sprd)
		return -EINVAL;

	printk(KERN_INFO "[%s]id(%d),val(%ld)\n", __func__, id, val);

	return raw_notifier_call_chain(&usbphy_event_sprd->nh, val, data);
}
EXPORT_SYMBOL(call_sprd_usbphy_event_notifiers);

static int __init sprd_usbphy_event_init(void)
{
	usbphy_event_sprd = kmalloc(sizeof(*usbphy_event_sprd), GFP_KERNEL);
	if (!usbphy_event_sprd)
		return -ENOMEM;

	usbphy_event_sprd->id = 1;
	usbphy_event_sprd->nh.head = NULL;

	return 0;
}

static void __exit sprd_usbphy_event_exit(void)
{
	kfree(usbphy_event_sprd);
	usbphy_event_sprd = NULL;
}

module_init(sprd_usbphy_event_init);
module_exit(sprd_usbphy_event_exit);

MODULE_DESCRIPTION("DesignWare SPRD USBPHY_EVENT");
MODULE_LICENSE("GPL v2");
