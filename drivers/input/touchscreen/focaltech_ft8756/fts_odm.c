// SPDX-License-Identifier: GPL-2.0
/*
 * FocalTech ft8756 SPI touchscreen driver - board specific test interface.
 *
 * The handset exposes the factory test entry points through a small sysfs
 * tree below /sys/kernel/ctp< N >/ :
 *
 *     ctp< N >/ctp_test/    rawdata, diff, selftest, short test, ...
 *     ctp< N >/ctp_update/  fw_update, fw_path, fw_erase_flash_test, ...
 *     ctp< N >/ctp_func/    gesture, glove, hall, tp_enable, ...
 *
 * Every attribute is a plain show/store pair that forwards to the driver
 * object registered for the sensor instance it was opened through.
 *
 * Reconstructed from focaltech_ft8756_spi_ts.ko.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "focaltech_core.h"

/* number of touchscreens the factory test interface supports */
#define FTS_FUNC_TEST_MAX	5

/* instance counter, also used to name the kobject ("ctp", "ctp1", ...) */
u8 input_num;
EXPORT_SYMBOL_GPL(input_num);

/* one entry per registered instance, NULL when unused */
void *ts_func_test_device[FTS_FUNC_TEST_MAX];

struct kobject *ts_func_test_obj;
struct kobject *ts_func_ctp_test_obj;
struct kobject *ts_func_ctp_update_obj;
struct kobject *ts_func_ctp_func_obj;

/**
 * register_ts_func_test_device() - publish one instance
 * @dev: driver object, opaque to this layer
 *
 * The slot is derived from @input_num, so the instances have to register in
 * the same order as ts_gen_func_test_init() numbers them.
 */
void register_ts_func_test_device(void *dev)
{
	u8 n = input_num;

	if ((u8)(n - 1) >= FTS_FUNC_TEST_MAX)
		return;

	ts_func_test_device[n - 1] = dev;
	__module_get(THIS_MODULE);
}
EXPORT_SYMBOL_GPL(register_ts_func_test_device);

/**
 * unregister_ts_func_test_device() - drop one instance
 * @dev: driver object, unused
 */
void unregister_ts_func_test_device(void *dev)
{
	u8 n = input_num;

	if ((u8)(n - 1) >= FTS_FUNC_TEST_MAX)
		return;

	ts_func_test_device[n - 1] = NULL;
	module_put(THIS_MODULE);
}
EXPORT_SYMBOL_GPL(unregister_ts_func_test_device);

/**
 * ts_gen_func_test_init() - create the /sys/kernel/ctp<N> tree
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ts_gen_func_test_init(void)
{
	char name[6];
	char num[3];
	int ret = 0;

	strlcpy(name, "ctp", sizeof(name));
	if (input_num) {
		snprintf(num, sizeof(num), "%d", input_num);
		strlcat(name, num, sizeof(name));
		input_num = input_num + 1;
	} else {
		input_num = 1;
	}

	ts_func_test_obj = kobject_create_and_add(name, NULL);
	if (!ts_func_test_obj) {
		pr_err("%s: unable to create kobject\n", __func__);
		return -ENOMEM;
	}

	ts_func_ctp_test_obj = kobject_create_and_add("ctp_test",
						      ts_func_test_obj);
	if (!ts_func_ctp_test_obj) {
		pr_err("%s: unable to create kobject-ts_func_test_obj\n",
		       __func__);
		ret = -ENOMEM;
		goto err_test;
	}

	ts_func_ctp_update_obj = kobject_create_and_add("ctp_update",
							ts_func_test_obj);
	if (!ts_func_ctp_update_obj) {
		pr_err("%s: unable to create kobject-ts_func_test_obj\n",
		       __func__);
		ret = -ENOMEM;
		goto err_update;
	}

	ts_func_ctp_func_obj = kobject_create_and_add("ctp_func",
						      ts_func_test_obj);
	if (!ts_func_ctp_func_obj) {
		pr_err("%s: unable to create kobject-ts_func_ctp_func_obj\n",
		       __func__);
		ret = -ENOMEM;
		goto err_func;
	}

	ret = sysfs_create_group(ts_func_ctp_test_obj, &ts_func_ctp_test_grp);
	if (ret) {
		pr_err("%s: failed to create attributes- ts_func_ctp_test_grp\n",
		       __func__);
		goto err_test_grp;
	}

	ret = sysfs_create_group(ts_func_ctp_update_obj,
				 &ts_func_ctp_update_grp);
	if (ret) {
		pr_err("%s: failed to create attributes- ts_func_ctp_update_grp\n",
		       __func__);
		sysfs_remove_group(ts_func_ctp_test_obj, &ts_func_ctp_test_grp);
		goto err_test_grp;
	}

	ret = sysfs_create_group(ts_func_ctp_func_obj, &ts_func_ctp_func_grp);
	if (ret) {
		pr_err("%s: failed to create attributes- ts_func_ctp_func_grp\n",
		       __func__);
		sysfs_remove_group(ts_func_ctp_test_obj, &ts_func_ctp_test_grp);
		sysfs_remove_group(ts_func_ctp_update_obj,
				   &ts_func_ctp_update_grp);
		goto err_test_grp;
	}

	return 0;

err_test_grp:
	kobject_put(ts_func_ctp_update_obj);
err_update:
	kobject_put(ts_func_ctp_func_obj);
err_func:
	kobject_put(ts_func_ctp_test_obj);
err_test:
	kobject_put(ts_func_test_obj);

	return ret;
}
EXPORT_SYMBOL_GPL(ts_gen_func_test_init);

/*
 * The lcd driver reaches back into the touch driver through these hooks: the
 * vendor panel code needs to know whether the touch controller has to be
 * reset while the panel is being powered, and wants the touch side to be
 * suspended/resumed in lockstep with it.  All of them dispatch through the
 * first registered instance.
 */
#define FTS_INST0	(ts_func_test_device[0])

static int fts_ts_call(void *d, unsigned int off)
{
	int (*fn)(void);

	if (!d)
		return 0;

	fn = *(int (**)(void))((char *)d + off);
	if (!fn)
		return 0;

	return fn();
}

/**
 * ts_suspend_for_lcd_async_use() - ask the touch side to suspend
 *
 * Return: the callback result, 0 when no instance is registered.
 */
int ts_suspend_for_lcd_async_use(void)
{
	return fts_ts_call(FTS_INST0, 0x08);
}
EXPORT_SYMBOL_GPL(ts_suspend_for_lcd_async_use);

/**
 * ts_resume_for_lcd_async_use() - ask the touch side to resume
 *
 * Return: the callback result, 0 when no instance is registered.
 */
int ts_resume_for_lcd_async_use(void)
{
	return fts_ts_call(FTS_INST0, 0x10);
}
EXPORT_SYMBOL_GPL(ts_resume_for_lcd_async_use);

/**
 * ts_reset_for_lcd_use() - reset the controller, used while the lcd probes
 *
 * Return: the callback result, 0 when no instance is registered.
 */
int ts_reset_for_lcd_use(void)
{
	return fts_ts_call(FTS_INST0, 0x18);
}
EXPORT_SYMBOL_GPL(ts_reset_for_lcd_use);

/**
 * ts_suspend_need_lcd_reset_high() - lcd gate has to be released on suspend
 *
 * Return: 1 when the controller wants the reset line high while the panel
 * is off, 0 otherwise.
 */
int ts_suspend_need_lcd_reset_high(void)
{
	return fts_ts_call(FTS_INST0, 0x20) != 0;
}
EXPORT_SYMBOL_GPL(ts_suspend_need_lcd_reset_high);

/**
 * ts_supend_need_power_reset_high() - power rail has to stay up on suspend
 *
 * Return: 1 when the controller wants the reset line high while the power
 * rail is off, 0 otherwise.
 */
int ts_supend_need_power_reset_high(void)
{
	return fts_ts_call(FTS_INST0, 0x28) != 0;
}
EXPORT_SYMBOL_GPL(ts_supend_need_power_reset_high);

/**
 * fts_ts_resume_work() - resume worker body
 * @work: the queued work item
 */
void fts_ts_resume_work(struct work_struct *work)
{
	fts_ts_resume();
}
