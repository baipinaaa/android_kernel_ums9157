// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron touchscreen - sysfs 属性。
 *
 * 二进制对照：
 *   hyn_create_sysfs          0x4578 ( 252)
 *   hyn_release_sysfs         0x5890 (  52)
 *   hyn_checkbuf_show/store   0x4674 / 0x471c
 *   hyn_check_diff            0x4724 (1156)
 *   hyntpfactorytest_show/store 0x4ba8 / 0x4bd0
 *   hyn_fwupgradeapp_show/store 0x4c04 / 0x4c0c
 *   hyn_tprwreg_show/store    0x4ddc / 0x514c
 *   hyn_fwupdate_show/store   0x53fc / 0x5404
 *   hyn_tpfwver_show/store    0x5460 / 0x5888
 *   tp_fw_version_show/store  0x7d6c / 0x7da8
 *   ts_suspend_show/store     0x7db0 / 0x7db8（store 内联 hyn_suspend/hyn_resume）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "hynitron_common.h"

/* hyntpfwver ------------------------------------------------------------ */
static ssize_t hyn_tpfwver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	int ret;

	if (!ts)
		return -ENODEV;

	mutex_lock(&g_procrw_mutex);

	ret = hynitron_touch_setmode(ts->client, 1, false);
	if (ret < 0) {
		pr_err("[HYN]%s : ret = %d. hyn_tpfwver_show:write debug info command fail.\n",
		       "hyn_tpfwver_show", ret);
		mutex_unlock(&g_procrw_mutex);
		return ret;
	}

	msleep(10);

	ret = cst3xx_firmware_info(ts->client);
	if (ret < 0) {
		pr_err("get tp fw version fail!\n");
		hynitron_touch_setmode(ts->client, 0, false);
		mutex_unlock(&g_procrw_mutex);
		return ret;
	}

	hynitron_touch_setmode(ts->client, 0, false);
	mutex_unlock(&g_procrw_mutex);

	return snprintf(buf, PAGE_SIZE, "%d\n", ts->chip_ic_fw_version);
}

static ssize_t hyn_tpfwver_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return count;
}

/* hynfwupdate ----------------------------------------------------------- */
static ssize_t hyn_fwupdate_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t hyn_fwupdate_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	unsigned int val = 0;
	int ret;

	pr_err("[HYN][Info]hyn_fwupdate_store enter.\n");

	if (!ts)
		return -ENODEV;

	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	if (val == 1) {
		ret = hyn_boot_update_fw(ts->client);
		if (ret < 0)
			return ret;
	}

	return count;
}

/* hyntprwreg ------------------------------------------------------------ */
static ssize_t hyn_tprwreg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	u8 data[8] = { 0 };
	int ret;

	if (!ts)
		return -ENODEV;

	mutex_lock(&g_procrw_mutex);

	ret = hynitron_touch_setmode(ts->client, 1, false);
	if (ret < 0) {
		pr_err("[HYN]%s : ret = %d. hyn_tprwreg_show:write debug info command fail.\n",
		       "hyn_tprwreg_show", ret);
		mutex_unlock(&g_procrw_mutex);
		return ret;
	}

	msleep(10);

	ret = cst3xx_firmware_info(ts->client);
	if (ret < 0) {
		pr_err("[HYN]%s : ret = %d. hyn_tprwreg_show:Read version resgister fail.\n",
		       "hyn_tprwreg_show", ret);
		hynitron_touch_setmode(ts->client, 0, false);
		mutex_unlock(&g_procrw_mutex);
		return ret;
	}

	ret = hyn_i2c_read_bytes(0xd000, data, 4, 2);
	if (ret < 0) {
		pr_err("[HYN]%s : ret = %d. hyn_tprwreg_show:Read version resgister fail.\n",
		       "hyn_tprwreg_show", ret);
	}

	hynitron_touch_setmode(ts->client, 0, false);
	mutex_unlock(&g_procrw_mutex);

	return snprintf(buf, PAGE_SIZE,
			"value_0: 0x%02X,value_1: 0x%02X,value_2: 0x%02X,value_3: 0x%02X,value_4: 0x%02X.\n",
			data[0], data[1], data[2], data[3], data[4]);
}

static ssize_t hyn_tprwreg_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	unsigned int addr = 0, value = 0;
	u8 data[4];
	int ret;

	if (!ts)
		return -ENODEV;

	if (sscanf(buf, "%x %x", &addr, &value) != 2)
		return -EINVAL;

	mutex_lock(&g_procrw_mutex);

	ret = hynitron_touch_setmode(ts->client, 1, false);
	if (ret < 0) {
		pr_err("[HYN]%s : ret = %d. hyn_tprwreg_show:write normal mode fail.\n",
		       "hyn_tprwreg_show", ret);
		mutex_unlock(&g_procrw_mutex);
		return ret;
	}

	data[0] = addr & 0xff;
	data[1] = (addr >> 8) & 0xff;
	data[2] = value & 0xff;
	data[3] = (value >> 8) & 0xff;

	ret = hyn_i2c_write_bytes(addr & 0xffff, &data[2], 2, 2);
	if (ret < 0)
		pr_err("[HYN]%s : ret = %d. hyn_tprwreg_show:write debug info command fail.\n",
		       "hyn_tprwreg_show", ret);

	hynitron_touch_setmode(ts->client, 0, false);
	mutex_unlock(&g_procrw_mutex);

	return count;
}

/* hynfwupgradeapp ------------------------------------------------------- */
static ssize_t hyn_fwupgradeapp_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t hyn_fwupgradeapp_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	unsigned int val = 0;

	pr_err("[HYN][Info]hyn_fwupgradeapp_store enter.\n");

	if (!ts)
		return -ENODEV;

	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	if (val == 1)
		hyn_boot_update_fw(ts->client);

	pr_err("[HYN][Info]hyn_fwupgradeapp_store exit.\n");
	return count;
}

/* hyntpfactorytest ------------------------------------------------------ */
static ssize_t hyntpfactorytest_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	u8 data = 0;

	if (!ts)
		return -ENODEV;

	hyn_i2c_read_byte(ts->client, 0xab, &data);

	return snprintf(buf, PAGE_SIZE, "%d\n", data);
}

static ssize_t hyntpfactorytest_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	return count;
}

/* hyncheckbuf ----------------------------------------------------------- */
static ssize_t hyn_checkbuf_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t hyn_checkbuf_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	return count;
}

void hyn_check_diff(u8 *buf, int len)
{
	pr_err("[HYN][Error]CLEAR POINT.\n");
}

/* tp_fw_version --------------------------------------------------------- */
static ssize_t tp_fw_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);

	if (!ts)
		return -ENODEV;

	return snprintf(buf, PAGE_SIZE, "0x%x\n", ts->chip_ic_fw_version);
}

static ssize_t tp_fw_version_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	return count;
}

/* ts_suspend ------------------------------------------------------------ */
static ssize_t ts_suspend_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t ts_suspend_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hyn_ts_data *ts = dev_get_drvdata(dev);
	unsigned int val = 0;
	int i;

	if (!ts)
		return -ENODEV;

	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	if (val == 1) {
		pr_err("[HYN]%s: Enter\n", "hyn_suspend");

		if (!ts->irq_enabled) {
			disable_irq(ts->irq);
			ts->irq_enabled = 1;
		}

		hyn_enter_deep_sleep(ts);

		pr_err("[HYN]%s: Exit(%d)\n", "hyn_suspend", 0);
	} else if (val == 0) {
		pr_err("[HYN]%s: Enter\n", "hyn_resume");

		hyn_reset_proc(0x28);

		pr_err("[HYN]%s: Enter\n", "hyn_release_all_finger");
		for (i = 0; i < 5; i++) {
			input_mt_slot(ts->input_dev, i);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			input_event(ts->input_dev, EV_ABS, ABS_MT_POSITION_X, 0);
			input_event(ts->input_dev, EV_ABS, ABS_MT_POSITION_Y, 0);
		}
		input_event(ts->input_dev, EV_SYN, SYN_REPORT, 0);
		pr_err("[HYN]%s: Exit(%d)\n", "hyn_release_all_finger", 0);

		hynitron_touch_setmode(ts->client, 0, false);

		if (ts->irq_enabled) {
			enable_irq(ts->irq);
			ts->irq_enabled = 0;
		}

		pr_err("[HYN]%s: Exit(%d)\n", "hyn_resume", 0);
	}

	return count;
}

/* 属性表（与二进制 .data 一一对应） -------------------------------------- */
static DEVICE_ATTR(hyntpfwver, 0644,
		   hyn_tpfwver_show, hyn_tpfwver_store);
static DEVICE_ATTR(hynfwupdate, 0644,
		   hyn_fwupdate_show, hyn_fwupdate_store);
static DEVICE_ATTR(hyntprwreg, 0644,
		   hyn_tprwreg_show, hyn_tprwreg_store);
static DEVICE_ATTR(hynfwupgradeapp, 0644,
		   hyn_fwupgradeapp_show, hyn_fwupgradeapp_store);
static DEVICE_ATTR(hyntpfactorytest, 0644,
		   hyntpfactorytest_show, hyntpfactorytest_store);
static DEVICE_ATTR(hyncheckbuf, 0644,
		   hyn_checkbuf_show, hyn_checkbuf_store);

static DEVICE_ATTR(ts_suspend, 0644, ts_suspend_show, ts_suspend_store);
static DEVICE_ATTR(tp_fw_version, 0444,
		   tp_fw_version_show, tp_fw_version_store);

static struct attribute *hyn_attributes[] = {
	&dev_attr_hyntpfwver.attr,
	&dev_attr_hynfwupdate.attr,
	&dev_attr_hyntprwreg.attr,
	&dev_attr_hynfwupgradeapp.attr,
	&dev_attr_hyntpfactorytest.attr,
	&dev_attr_hyncheckbuf.attr,
	NULL,
};

struct attribute_group hyn_attribute_group = {
	.attrs = hyn_attributes,
};

static struct attribute *tp_sysfs_attrs[] = {
	&dev_attr_ts_suspend.attr,
	&dev_attr_tp_fw_version.attr,
	NULL,
};

struct attribute_group tp_attr_group = {
	.attrs = tp_sysfs_attrs,
};

int hyn_create_sysfs(struct hyn_ts_data *ts)
{
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_create_sysfs");

	ret = sysfs_create_group(&ts->dev->kobj, &hyn_attribute_group);
	if (ret < 0)
		pr_err("[HYN][Info]%s() - ERROR: sysfs_create_group() failed.\n",
		       "hyn_create_sysfs");

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_create_sysfs", 0);
	return ret;
}

void hyn_release_sysfs(struct hyn_ts_data *ts)
{
	if (ts && ts->dev)
		sysfs_remove_group(&ts->dev->kobj, &hyn_attribute_group);
}
