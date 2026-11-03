/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unisoc camera subsystem power domain driver.
 *
 * Reconstructed from the vendor module sprd_camsys_pw_domain.ko
 * (bsp/modules/common/camera/power/kernel/, kernel 5.4.210).
 *
 * Global layout of the driver private data (0x780 bytes, devm_kmalloc'ed):
 *
 *   0x000  u32                inited
 *   0x004  atomic_t           refcnt
 *   0x008  struct mutex       mlock        ("&pw_info->mlock")
 *   0x028  struct generic_pm_domain genpd (0x78 bytes)
 *   0x540  char              *domain_name
 *   0x568  const char        *name
 *   0x570  const char        *name2
 *   0x618  struct camsys_power_ops *ops
 *   0x620  ...                SoC specific area (see each camsys_pw_domain_*.c)
 *   0x780  end
 */

#ifndef _SPRD_CAMSYS_PW_DOMAIN_H
#define _SPRD_CAMSYS_PW_DOMAIN_H

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

/* The polling loops of the vendor driver always sleep 300..350 us. */
#define CAMPW_POLL_US_MIN	300
#define CAMPW_POLL_US_MAX	350

/* Number of generic power domains provided by this driver. */
#define CAMPW_MAX_DOMAINS	1

struct camsys_power_info;

/*
 * Per-chip back end.  The vendor binary calls the members in this order:
 *	init(pdev, info)	+0x00
 *	on(info)		+0x08
 *	off(info)		+0x10
 *	eb(int on)		+0x18
 *	disable(int on)		+0x20
 */
struct camsys_power_ops {
	int (*init)(struct platform_device *pdev, struct camsys_power_info *info);
	int (*on)(struct camsys_power_info *info);
	int (*off)(struct camsys_power_info *info);
	int (*eb)(int on);
	int (*disable)(int on);
};

struct camsys_power_info {
	u32 inited;			/* +0x000 */
	atomic_t refcnt;		/* +0x004 */
	struct mutex mlock;		/* +0x008 */
	u8 pad0[0x28 - 0x008 - sizeof(struct mutex)];

	struct generic_pm_domain genpd;	/* +0x028 */
	u8 pad1[0x540 - (0x028 + sizeof(struct generic_pm_domain))];

	char *domain_name;		/* +0x540 */
	u8 pad2[0x568 - 0x540 - sizeof(char *)];

	const char *name;		/* +0x568 */
	const char *name2;		/* +0x570 */
	u8 pad3[0x618 - 0x570 - 2 * sizeof(char *)];

	struct camsys_power_ops *ops;	/* +0x618 */
	/* SoC specific data follows, 0x620 .. 0x77f */
	u8 priv[];
};

/* Global cookie, "pw_info" in the vendor binary. */
extern struct camsys_power_info *pw_info;

/* Back ends, one per supported SoC. */
extern const struct camsys_power_ops camsys_power_ops_l5pro;	/* r7p0 */
extern const struct camsys_power_ops camsys_power_ops_l5;	/* r6p0 */
extern const struct camsys_power_ops camsys_power_ops_l3;	/* r5p0 */
extern const struct camsys_power_ops camsys_power_ops_le;	/* r5p1 */
extern const struct camsys_power_ops camsys_power_ops_qogirl6;	/* r8p0 */
extern const struct camsys_power_ops camsys_power_ops_qogirn6l;	/* qogirn6l */
extern const struct camsys_power_ops camsys_power_ops_qogirn6pro; /* qogirn6pro */
extern const struct camsys_power_ops camsys_power_ops_pike2;	/* r4p0 */

/* Camera mm domain power on/off, exported for the camera drivers. */
int sprd_glb_mm_pw_on_cfg(void);
int sprd_glb_mm_pw_off_cfg(void);
int sprd_mm_pw_notify_register(struct notifier_block *nb);
int sprd_mm_pw_notify_unregister(struct notifier_block *nb);

#endif /* _SPRD_CAMSYS_PW_DOMAIN_H */
