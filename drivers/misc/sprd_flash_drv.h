/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sprd_flash_drv.h - UNISOC camera flash LED framework interface
 *
 * Shared between the framework (sprd_flash_drv.c) and the flash IC drivers
 * (flash_ic_ocp8137.c, ...).  Reconstructed from the UMS9157 (T157) vendor
 * modules, the layouts below are byte compatible with the vendor binaries:
 *
 *   sprd_flash_ic_ops	80 bytes  (10 function pointers, order is ABI)
 *   sprd_flash_info	48 bytes  (offset 0x28 is a name pointer)
 *   sprd_flash_element	 8 bytes  (u16 value, u16 index, u32 reserved)
 *   sprd_flash_ctrl	20 bytes
 *   sprd_flash_cfg	16 bytes
 */

#ifndef _SPRD_FLASH_DRV_H
#define _SPRD_FLASH_DRV_H

#include <linux/types.h>

#define SPRD_FLASH_MAX_IC	2

/**
 * struct sprd_flash_info - 48 byte information blob
 *
 * Opaque to the framework: produced by the flash IC driver and handed back to
 * userspace, except for @name which is also cached in the device and printed by
 * the "test" attribute for :c:type:`FLASH_TYPE_NAMED` chips.
 */
struct sprd_flash_info {
	u64 reserved[5];	/* 0x00 vendor private */
	void *name;		/* 0x28 chip name */
};

/* One element ("level"), 8 bytes on the stack of the caller. */
struct sprd_flash_element {
	u16 value;		/* 0x00 element value / level */
	u16 index;		/* 0x02 element index */
	u32 reserved;
};

/**
 * struct sprd_flash_ic_ops - callbacks implemented by a flash IC driver
 *
 * Order is load bearing: it is the layout of the 80 byte table the vendor
 * ships in .rodata.  Every callback gets the IC private data and the LED mask
 * (bit 0 = LED 0, bit 1 = LED 1).
 */
struct sprd_flash_ic_ops {
	int (*open_torch)(void *data, u32 led_mask);	/* 0x00 */
	int (*close_torch)(void *data, u32 led_mask);	/* 0x08 */
	int (*open_preflash)(void *data, u32 led_mask);	/* 0x10 */
	int (*close_preflash)(void *data, u32 led_mask);	/* 0x18 */
	int (*open_highlight)(void *data, u32 led_mask);	/* 0x20 */
	int (*close_highlight)(void *data, u32 led_mask);	/* 0x28 */
	int (*cfg_value_torch)(void *data, u32 index,
			       struct sprd_flash_element *e);	/* 0x30 */
	int (*cfg_value_preflash)(void *data, u32 index,
				  struct sprd_flash_element *e);	/* 0x38 */
	int (*cfg_value_highlight)(void *data, u32 index,
				   struct sprd_flash_element *e);	/* 0x40 */
	struct sprd_flash_info (*get_flash_info)(void *data, u32 index,
						 void *arg);	/* 0x48 */
};

/* Control command handed to sprd_flash_ctrl(). */
struct sprd_flash_ctrl {
	u32 enable[2];		/* 0x00 per LED "is used" flags */
	u32 mode[2];		/* 0x08 per LED mode, last one wins */
	u32 index;		/* 0x10 IC index (0..1) */
};

/* "Level" command handed to sprd_flash_cfg(). */
struct sprd_flash_cfg {
	u32 reserved;		/* 0x00 */
	u8 index;		/* 0x04 IC index (0..1) */
	u8 pad0[3];
	u8 mode;		/* 0x08 0 = torch, 1 = preflash, 2 = highlight */
	u8 pad1[2];
	u8 element;		/* 0x0b element index */
	u16 element_a;		/* 0x0c */
	u16 element_b;		/* 0x0e */
};

/* Mode values accepted by sprd_flash_ctrl(). */
#define FLASH_MODE_OFF			0
#define FLASH_MODE_OPEN_PREFLASH	1
#define FLASH_MODE_OPEN_TORCH		2
#define FLASH_MODE_OFF_PREFLASH		16
#define FLASH_MODE_OPEN_HIGHLIGHT	17
#define FLASH_MODE_OFF_HIGHLIGHT	48
#define FLASH_MODE_INVALID		67	/* 'C' - only used for logging */

#define FLASH_TYPE_NAMED		9

int sprd_flash_register(struct sprd_flash_ic_ops *ops, void *data, u8 index);
int sprd_flash_ctrl(struct sprd_flash_ctrl *ctrl);
int sprd_flash_cfg(struct sprd_flash_cfg *cfg);
struct sprd_flash_info sprd_flash_get_info(u8 index, u32 led_index, void *arg);

#endif /* _SPRD_FLASH_DRV_H */
