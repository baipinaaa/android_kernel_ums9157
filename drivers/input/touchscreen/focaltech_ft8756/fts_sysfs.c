// SPDX-License-Identifier: GPL-2.0
/*
 * FocalTech ft8756 SPI touchscreen driver - factory test sysfs attributes.
 *
 * Every attribute below is a pure dispatcher: it works out which touch
 * instance the calling path belongs to, picks the driver object registered
 * for that instance and forwards to the callback stored in the slot that is
 * reserved for this very attribute.  The callbacks themselves live in the
 * driver half, this file only owns the sysfs ABI.
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

#define FTS_FUNC_TEST_MAX	5
#define FTS_PATH_LEN		0x32
#define FTS_SHOW_BUF_SIZE	0x1000

extern u8 input_num;
extern void *ts_func_test_device[FTS_FUNC_TEST_MAX];

/**
 * fts_get_input_idx() - work out the instance index from the sysfs path
 * @dev: device the attribute belongs to
 *
 * The attribute lives at .../ctp< N >/ctp_test/<name>, so the first digit in
 * the path is the instance number.
 *
 * Return: the index, or -EINVAL when no digit was found.
 */
static int fts_get_input_idx(struct device *dev)
{
	/* the buffer starts out as a valid, instance-less path so that a
	 * failing kobject_get_path() simply leaves the index at 0
	 */
	char path[FTS_PATH_LEN] = "/ctp/ctp_xxx";
	char *kp;
	int idx = -EINVAL;
	int i;

	kp = kobject_get_path(&dev->kobj, GFP_KERNEL);
	if (!kp)
		return -EINVAL;

	pr_info("**********path:%s.\n", kp);

	strlcpy(path, kp, sizeof(path));

	for (i = 0; i < FTS_PATH_LEN; i++) {
		if ((u8)(path[i] - '1') <= 8) {
			idx = path[i] - '0';
			break;
		}
	}

	kfree(kp);

	return idx;
}

/**
 * fts_attr_show() - forward one read to the callback of this attribute
 * @dev: device the attribute belongs to
 * @buf: sysfs output buffer
 * @off: offset of this attribute's callback inside the instance object
 *
 * Return: what the callback returned, or the length of the "NOT SUPPORTED"
 * notice when no callback is registered.
 */
static ssize_t fts_attr_show(struct device *dev, char *buf, unsigned int off)
{
	ssize_t (*fn)(void *self, char *buf);
	void *d;
	int idx;

	idx = fts_get_input_idx(dev);
	if (idx < 0 || idx >= FTS_FUNC_TEST_MAX)
		return -EINVAL;

	d = ts_func_test_device[idx];
	fn = d ? *(ssize_t (**)(void *, char *))((char *)d + off) : NULL;

	if (!fn)
		return snprintf(buf, FTS_SHOW_BUF_SIZE, "%s\n", "NOT SUPPORTED");

	return fn(*(void **)d, buf);
}

/**
 * fts_attr_store() - forward one write to the callback of this attribute
 * @dev: device the attribute belongs to
 * @buf: sysfs input buffer
 * @count: number of bytes written
 * @off: offset of this attribute's callback inside the instance object
 *
 * Return: what the callback returned, 0 when no callback is registered.
 */
static ssize_t fts_attr_store(struct device *dev, const char *buf,
			      size_t count, unsigned int off)
{
	ssize_t (*fn)(void *self, const char *buf);
	void *d;
	int idx;

	idx = fts_get_input_idx(dev);
	if (idx < 0 || idx >= FTS_FUNC_TEST_MAX)
		return -EINVAL;

	d = ts_func_test_device[idx];
	fn = d ? *(ssize_t (**)(void *, const char *))((char *)d + off) : NULL;

	if (!fn)
		return 0;

	return fn(*(void **)d, buf);
}

/* ---- attribute implementations, one per .rodata slot ---- */

ssize_t rawdata_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x48);
}

static DEVICE_ATTR_RO(rawdata);

ssize_t rawdata_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x50);
}

static DEVICE_ATTR_RO(rawdata_info);

ssize_t diff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x58);
}

static DEVICE_ATTR_RO(diff);

ssize_t selftest_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x60);
}

static DEVICE_ATTR_RO(selftest);

ssize_t hibernate_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x68);
}

static DEVICE_ATTR_RO(hibernate_test);

ssize_t fw_ic_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x70);
}

static DEVICE_ATTR_RO(fw_ic_version);

ssize_t fw_fs_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x78);
}

static DEVICE_ATTR_RO(fw_fs_version);

ssize_t module_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x80);
}

static DEVICE_ATTR_RO(module_id);

ssize_t chip_detect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x88);
}

static DEVICE_ATTR_RO(chip_detect);

ssize_t calibration_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x90);
}

static DEVICE_ATTR_RO(calibration);

ssize_t open_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xc0);
}

static DEVICE_ATTR_RO(open);

ssize_t short_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xc8);
}

static DEVICE_ATTR_RO(short);

ssize_t need_test_config_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xd8);
}

static DEVICE_ATTR_RO(need_test_config);

ssize_t factory_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xe0);
}

static DEVICE_ATTR_RO(factory_info);

ssize_t factory_cfg_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xe8);
}

static DEVICE_ATTR_RO(factory_cfg_info);

ssize_t fw_fs_settinginfo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x140);
}

static DEVICE_ATTR_RO(fw_fs_settinginfo);

ssize_t gesture_pos_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xf0);
}

static DEVICE_ATTR_RO(gesture_pos);

/*
 * gesture_fingers is not a plain dispatcher: the callback returns the number
 * of fingers instead of a length, and -EINVAL means the controller is not in
 * gesture mode at all.
 */
ssize_t gesture_fingers_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int (*fn)(void *self, char *buf);
	void *d;
	int idx;
	int val;

	idx = fts_get_input_idx(dev);
	if (idx < 0 || idx >= FTS_FUNC_TEST_MAX)
		return -EINVAL;

	d = ts_func_test_device[idx];
	fn = d ? *(int (**)(void *, char *))((char *)d + 0xf8) : NULL;

	if (!fn)
		return snprintf(buf, FTS_SHOW_BUF_SIZE, "%s\n", "NOT SUPPORTED");

	val = fn(*(void **)d, buf);
	if (val == -EINVAL)
		return snprintf(buf, FTS_SHOW_BUF_SIZE, "%s\n", "MODE ERROR");

	return snprintf(buf, FTS_SHOW_BUF_SIZE, "0x%02X\n", val);
}

static DEVICE_ATTR_RO(gesture_fingers);

ssize_t fw_path_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x98);
}

ssize_t fw_path_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0xa0);
}

static DEVICE_ATTR_RW(fw_path);

ssize_t glove_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x118);
}

ssize_t glove_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x110);
}

static DEVICE_ATTR_RW(glove);

ssize_t hall_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x108);
}

ssize_t hall_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x100);
}

static DEVICE_ATTR_RW(hall);

ssize_t tptype_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x128);
}

ssize_t tptype_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x120);
}

static DEVICE_ATTR_RW(tptype);

ssize_t tp_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x138);
}

ssize_t tp_enable_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x130);
}

static DEVICE_ATTR_RW(tp_enable);

ssize_t irq_awake_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x150);
}

ssize_t irq_awake_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x158);
}

static DEVICE_ATTR_RW(irq_awake);

ssize_t tp_work_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x160);
}

ssize_t tp_work_mode_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x168);
}

static DEVICE_ATTR_RW(tp_work_mode);

ssize_t gesture_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0xb8);
}

ssize_t gesture_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0xb0);
}

static DEVICE_ATTR_RW(gesture);

ssize_t test_config_path_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0xd0);
}

static DEVICE_ATTR_WO(test_config_path);

/* ---- update group, these live on a second set of slots ---- */

ssize_t fw_update_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return fts_attr_show(dev, buf, 0x38);
}

ssize_t fw_update_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x40);
}

static DEVICE_ATTR_RW(fw_update);

ssize_t fw_state_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	return fts_attr_show(dev, buf, 0x30);
}

static DEVICE_ATTR_RO(fw_state);

ssize_t fw_update_with_appointed_file_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return fts_attr_show(dev, buf, 0x38);
}

ssize_t fw_update_with_appointed_file_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x40);
}

static DEVICE_ATTR_RW(fw_update_with_appointed_file);

ssize_t fw_set_erase_flash_test(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0xa8);
}

static DEVICE_ATTR_WO(fw_set_erase_flash_test);

/* ---- debug attributes of the driver half ---- */

ssize_t fts_input_name_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return fts_attr_show(dev, buf, 0x18);
}

static DEVICE_ATTR_RO(fts_input_name);

ssize_t fts_light_suspend_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return fts_attr_show(dev, buf, 0x18);
}

ssize_t fts_light_suspend_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x40);
}

static DEVICE_ATTR_RW(fts_light_suspend);

ssize_t ts_irq_eb_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return fts_attr_show(dev, buf, 0x18);
}

ssize_t ts_irq_eb_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	return fts_attr_store(dev, buf, count, 0x40);
}

static DEVICE_ATTR_RW(ts_irq_eb);

/*
 * Device level debug attributes.
 *
 * The handset registers these on the touch device itself (not below
 * /sys/kernel/ctp<N>/) and their names differ from the ones the V3.3 sources
 * use, so they are spelled out here: every entry simply reuses the dispatcher
 * of the matching factory test attribute.
 */
static struct device_attribute dev_attr_rawdatashow =
	__ATTR(rawdatashow, 0664, rawdata_show, NULL);
static struct device_attribute dev_attr_rawdatainfo =
	__ATTR(rawdatainfo, 0664, rawdata_info_show, NULL);
static struct device_attribute dev_attr_diffshow =
	__ATTR(diffshow, 0664, diff_show, NULL);
static struct device_attribute dev_attr_selftestshow =
	__ATTR(selftestshow, 0664, selftest_show, NULL);
static struct device_attribute dev_attr_resettest =
	__ATTR(resettest, 0664, hibernate_test_show, NULL);
static struct device_attribute dev_attr_opentest =
	__ATTR(opentest, 0664, open_show, NULL);
static struct device_attribute dev_attr_shorttest =
	__ATTR(shorttest, 0664, short_show, NULL);
static struct device_attribute dev_attr_chiptest =
	__ATTR(chiptest, 0664, chip_detect_show, NULL);
static struct device_attribute dev_attr_testconfig =
	__ATTR(testconfig, 0664, need_test_config_show, test_config_path_store);
static struct device_attribute dev_attr_factoryinfo =
	__ATTR(factoryinfo, 0664, factory_info_show, NULL);
static struct device_attribute dev_attr_firmware_version =
	__ATTR(firmware_version, 0664, fw_fs_version_show, NULL);
static struct device_attribute dev_attr_fwversion =
	__ATTR(fwversion, 0664, fw_ic_version_show, NULL);
static struct device_attribute dev_attr_fwhostversion =
	__ATTR(fwhostversion, 0664, fw_fs_version_show, NULL);
static struct device_attribute dev_attr_fwmoduleid =
	__ATTR(fwmoduleid, 0664, module_id_show, NULL);
static struct device_attribute dev_attr_fwupdate =
	__ATTR(fwupdate, 0664, fw_path_show, fw_path_store);
static struct device_attribute dev_attr_fwbinupdate =
	__ATTR(fwbinupdate, 0664, calibration_show, NULL);
static struct device_attribute dev_attr_eraseflash =
	__ATTR(eraseflash, 0664, fw_fs_settinginfo_show, NULL);
static struct device_attribute dev_attr_gesturepos =
	__ATTR(gesturepos, 0664, gesture_pos_show, NULL);
static struct device_attribute dev_attr_gesturefingers =
	__ATTR(gesturefingers, 0664, gesture_fingers_show, NULL);
static struct device_attribute dev_attr_tptypeswitch =
	__ATTR(tptypeswitch, 0664, tptype_show, tptype_store);
static struct device_attribute dev_attr_tpenable =
	__ATTR(tpenable, 0664, tp_enable_show, tp_enable_store);
static struct device_attribute dev_attr_irqawake =
	__ATTR(irqawake, 0664, irq_awake_show, irq_awake_store);
static struct device_attribute dev_attr_gesture =
	__ATTR(gesture, 0664, gesture_show, gesture_store);
static struct device_attribute dev_attr_glove =
	__ATTR(glove, 0664, glove_show, glove_store);
static struct device_attribute dev_attr_hall =
	__ATTR(hall, 0664, hall_show, hall_store);
static struct device_attribute dev_attr_tp_work_mode =
	__ATTR(tp_work_mode, 0664, tp_work_mode_show, tp_work_mode_store);
static struct device_attribute dev_attr_factory_cfg_info =
	__ATTR(factory_cfg_info, 0664, factory_cfg_info_show, NULL);
static struct device_attribute dev_attr_fw_fs_settinginfo =
	__ATTR(fw_fs_settinginfo, 0664, fw_fs_settinginfo_show, NULL);

static struct attribute *fts_esd_mode_attrs[] = {
	&dev_attr_rawdatashow.attr,
	&dev_attr_rawdatainfo.attr,
	&dev_attr_diffshow.attr,
	&dev_attr_selftestshow.attr,
	&dev_attr_resettest.attr,
	&dev_attr_opentest.attr,
	&dev_attr_shorttest.attr,
	&dev_attr_chiptest.attr,
	&dev_attr_testconfig.attr,
	&dev_attr_factoryinfo.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_fwversion.attr,
	&dev_attr_fwhostversion.attr,
	&dev_attr_fwmoduleid.attr,
	&dev_attr_fwupdate.attr,
	&dev_attr_fwbinupdate.attr,
	&dev_attr_eraseflash.attr,
	&dev_attr_gesturepos.attr,
	&dev_attr_gesturefingers.attr,
	&dev_attr_tptypeswitch.attr,
	&dev_attr_tpenable.attr,
	&dev_attr_irqawake.attr,
	&dev_attr_gesture.attr,
	&dev_attr_glove.attr,
	&dev_attr_hall.attr,
	&dev_attr_tp_work_mode.attr,
	&dev_attr_factory_cfg_info.attr,
	&dev_attr_fw_fs_settinginfo.attr,
	NULL,
};

/**
 * fts_create_esd_sysfs() - register the device level debug attributes
 * @dev: touch device
 *
 * Return: 0 on success, negative errno otherwise.
 */
int fts_create_esd_sysfs(struct device *dev)
{
	struct attribute_group grp = {
		.name = NULL,
		.attrs = fts_esd_mode_attrs,
	};
	int ret;

	ret = sysfs_create_group(&dev->kobj, &grp);
	if (ret) {
		pr_err("[FTS_TS/E]%s:%s create fail\n", __func__, __func__);
		sysfs_remove_group(&dev->kobj, &grp);
		return ret;
	}

	return 0;
}
