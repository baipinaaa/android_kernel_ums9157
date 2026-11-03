/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _GSP_DEV_H
#define _GSP_DEV_H

#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/types.h>

struct gsp_core;
struct gsp_dev;
struct gsp_sync_timeline;

#define for_each_gsp_core(core, gsp) \
	list_for_each_entry((core), &(gsp)->cores, list)

#define for_each_gsp_core_safe(core, tmp, gsp) \
	list_for_each_entry_safe((core), (tmp), &(gsp)->cores, list)

#define GSP_MAX_IO_CNT(gsp)	((gsp)->io_cnt)

#define GSP_DEVICE_NAME		"sprd-gsp"

/*
 * 字段偏移来自 sprd-gsp.ko 的反汇编（各 accessor 的 ldr 立即数）：
 *   gsp_dev_to_device     ldr x0, [x0, #0x80]
 *   gsp_dev_to_core_cnt   ldr w0, [x0, #0x20]
 *   gsp_dev_to_io_cnt     ldr w0, [x0, #0x24]
 *   gsp_dev_to_interface  ldr x0, [x0, #0x88]
 *   gsp_dev_is_idle       ldr x8, [x0, #0x90]!   （cores 是链表头）
 * miscdevice 落在 +0x30（gsp_dev_alloc 里 `str w8, [x0, #0x30]!` 写 mdev.minor
 * 之前把 x0 前索引到 +0x30），比 9620 原版晚 8 字节，多出来的就是下面两个 bool。
 */
struct gsp_dev {
	char name[32];			/* +0x00 */
	u32 core_cnt;			/* +0x20 */
	u32 io_cnt;			/* +0x24 */
	bool resume_flag;		/* +0x28 */
	bool vrr_enabled;		/* +0x29 */

	struct miscdevice mdev;		/* +0x30 */
	struct device *dev;		/* +0x80 */

	struct gsp_interface *interface;	/* +0x88 */

	struct list_head cores;		/* +0x90 */
};

int gsp_dev_verify(struct gsp_dev *gsp);

struct device *gsp_dev_to_device(struct gsp_dev *gsp);
struct gsp_interface *gsp_dev_to_interface(struct gsp_dev *gsp);

int gsp_dev_is_idle(struct gsp_dev *gsp);
int gsp_dev_is_suspend(struct gsp_dev *gsp);
int gsp_dev_is_suspending(struct gsp_dev *gsp);

struct gsp_core *gsp_dev_to_core(struct gsp_dev *gsp, int index);
#endif
