/* SPDX-License-Identifier: GPL-2.0 */


#ifndef __SPRD_PHY_H__
#define __SPRD_PHY_H__

#include <linux/usb/phy.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>

enum sprd_usbphy_event_mode {
	SPRD_USBPHY_EVENT_TYPEC = 0,
	SPRD_USBPHY_EVENT_MAX,
};

struct sprd_hsphy_ops {
	void (*dpdm_switch_to_phy)(struct usb_phy *x, bool enable);
	bool (*get_dpdm_from_phy)(struct usb_phy *x);
};



struct sprd_common_hsphy {
	struct device		*dev;
	struct usb_phy		 phy;
	struct sprd_hsphy_ops		ops;
};

#if IS_ENABLED(CONFIG_SPRD_COMMONPHY)
extern int call_sprd_usbphy_event_notifiers(unsigned int id, unsigned long val, void *v);
extern int register_sprd_usbphy_notifier(struct notifier_block *nb, unsigned int id);
#else
static inline int call_sprd_usbphy_event_notifiers(unsigned int id, unsigned long val, void *v)
{
	return 0;
}
static inline int register_sprd_usbphy_notifier(struct notifier_block *nb, unsigned int id)
{
	return 0;
}
#endif 

#endif 
