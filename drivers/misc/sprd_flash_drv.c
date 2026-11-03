// SPDX-License-Identifier: GPL-2.0
/*
 * sprd_flash_drv.c - UNISOC camera flash LED framework
 *
 * Reconstructed from the vendor binary sprd_flash_drv.ko shipped in the
 * UMS9157 (T157 / Sunelan Q968) vendor_boot ramdisk
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt mod_unload
 *  modversions aarch64, "intree: N", depends="sprd_flash_drv").
 *
 * The framework owns a misc device ("/dev/sprd_flash"), a 0600 sysfs
 * attribute ("test") on that device and a set of exported helpers that the
 * flash IC drivers (flash_ic_ocp8137.ko, ...) use to drive the LED:
 *
 *   sprd_flash_register(ops, data, index)  IC registration, index 0..1
 *   sprd_flash_ctrl(&ctrl)                 on/off of torch/preflash/highlight
 *   sprd_flash_cfg(&cfg)                   per element current ("level")
 *   sprd_flash_get_info(idx, led, arg)     returns a 48 byte info blob
 *
 * Binary layout of the driver private data (devm_kmalloc'ed, 136 bytes,
 * see sprd_flash_probe()):
 *
 *   0x00  struct miscdevice misc
 *   0x50  struct sprd_flash_ic_ops *ops[2]   filled by sprd_flash_register()
 *   0x60  void *ic_data[2]                   the IC private data
 *   0x70  u32 state[2]                       1 == "torch already enabled"
 *   0x78  u16 flash_type                     last value written by "test"
 *   0x7a  u8  pad[6]
 *   0x80  void *flash_name                   last name returned by get_info
 *
 * and of the 48 byte info blob that ends up in the global @flash_capacity
 * (offset 0x28 of the blob is a pointer, see show_help()).
 *
 * Every log line of the vendor driver uses the same shape:
 *
 *   "FLASH_DRV:: <pid> <line> <function> : <message>"
 *
 * which is preserved here through the flash_log_*() macros.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#define FLASH_DRV_NAME		"sprd_flash"
#define FLASH_DRV_MAX		2

#define FLASH_LOG(_level, fmt, ...)					\
	printk(_level "FLASH_DRV:: %d %d %s : " fmt, current->pid,	\
	       __LINE__, __func__, ##__VA_ARGS__)

#define flash_log_err(fmt, ...)		FLASH_LOG(KERN_ERR, fmt, ##__VA_ARGS__)
#define flash_log_info(fmt, ...)	FLASH_LOG(KERN_INFO, fmt, ##__VA_ARGS__)

#include "sprd_flash_drv.h"

struct sprd_flash_dev {
	struct miscdevice misc;				/* 0x00 */
	struct sprd_flash_ic_ops *ops[FLASH_DRV_MAX];	/* 0x50 */
	void *ic_data[FLASH_DRV_MAX];			/* 0x60 */
	u32 state[FLASH_DRV_MAX];			/* 0x70 */
	u32 flash_type;					/* 0x78, u16 in the vendor */
	u32 reserved;					/* 0x7c */
	void *flash_name;				/* 0x80 */
};

static struct sprd_flash_dev *s_flash_dev;
static struct platform_device *pdev;

/* Cached result of the last "test" type 9 command, see show_help(). */
static struct sprd_flash_info flash_capacity;

/**
 * sprd_flash_register() - register a flash IC driver
 * @ops: the IC callbacks, kept by pointer
 * @data: IC private data, handed back to every callback
 * @index: 0 or 1, index into the two supported slots
 */
int sprd_flash_register(struct sprd_flash_ic_ops *ops, void *data, u8 index)
{
	if (!s_flash_dev)
		return -ENODEV;

	if (index >= FLASH_DRV_MAX)
		return -EINVAL;

	s_flash_dev->ops[index] = ops;
	s_flash_dev->ic_data[index] = data;
	return 0;
}
EXPORT_SYMBOL(sprd_flash_register);

/**
 * flash_get_flash_info() - ask the IC for its 48 byte information blob
 */
static struct sprd_flash_info flash_get_flash_info(struct sprd_flash_dev *dev,
						   u8 index, u32 arg1, void *arg2)
{
	struct sprd_flash_info info;
	struct sprd_flash_ic_ops *ops;

	memset(&info, 0, sizeof(info));

	if (!dev)
		return info;

	if (index >= FLASH_DRV_MAX)
		return info;

	ops = dev->ops[index];
	if (!ops || !ops->get_flash_info)
		return info;

	return ops->get_flash_info(dev->ic_data[index], arg1, arg2);
}

/**
 * sprd_flash_get_info() - exported wrapper around the IC information callback
 * @index: IC index (0..1)
 * @led_index: LED index forwarded to the IC
 * @arg: extra argument forwarded to the IC
 *
 * Returns the 48 byte blob by value, zeroed when the slot is empty.
 */
struct sprd_flash_info sprd_flash_get_info(u8 index, u32 led_index, void *arg)
{
	struct sprd_flash_info info;

	info = flash_get_flash_info(s_flash_dev, index, led_index, arg);
	flash_log_info("flash_idx 0x%x led_idx %d\n", index, led_index);
	return info;
}
EXPORT_SYMBOL(sprd_flash_get_info);

/*
 * Helper used by sprd_flash_ctrl() and sprd_flash_release(): run one IC
 * callback when it is implemented.  A missing callback reports -1, exactly
 * like the vendor build (the results of the three "off" callbacks are OR'ed
 * together).
 */
#define FLASH_IC_CALL(_idx, _op, _data, _mask)				\
	({								\
		struct sprd_flash_ic_ops *__ops = s_flash_dev->ops[_idx]; \
		int __ret = -1;						\
		if (!__ops)						\
			return -1;					\
		if (__ops->_op)						\
			__ret = __ops->_op(_data, _mask);		\
		__ret;							\
	})

/**
 * sprd_flash_ctrl() - switch the torch/preflash/highlight LEDs on or off
 * @ctrl: command, see struct sprd_flash_ctrl
 *
 * Returns 0 (mode already off), -1 on a missing IC/callback, or whatever the
 * IC callbacks returned.
 */
int sprd_flash_ctrl(struct sprd_flash_ctrl *ctrl)
{
	u32 led_mask;
	int mode, ret, tmp;
	u32 index;

	if (ctrl->enable[0]) {
		mode = ctrl->mode[0];
		led_mask = 1;
	} else {
		led_mask = 0;
		mode = FLASH_MODE_INVALID;
	}

	if (ctrl->enable[1]) {
		mode = ctrl->mode[1];
		led_mask |= 2;
	}

	flash_log_info("ctrl: led_index %d status 0x%x\n", led_mask, mode);

	switch (mode) {
	case FLASH_MODE_OFF:
	case FLASH_MODE_OFF_PREFLASH:
	case FLASH_MODE_OFF_HIGHLIGHT:
		index = ctrl->index;
		if (index >= FLASH_DRV_MAX)
			return -1;

		if (s_flash_dev->state[index])
			return 0;

		if (!s_flash_dev->ops[index])
			return -1;

		ret = FLASH_IC_CALL(index, close_torch,
				    s_flash_dev->ic_data[index], led_mask);
		tmp = FLASH_IC_CALL(index, close_preflash,
				    s_flash_dev->ic_data[index], led_mask);
		ret |= tmp;
		tmp = FLASH_IC_CALL(index, close_highlight,
				    s_flash_dev->ic_data[index], led_mask);
		return ret | tmp;

	case FLASH_MODE_OPEN_PREFLASH:
		index = ctrl->index;
		if (index >= FLASH_DRV_MAX)
			return -1;
		if (!s_flash_dev->ops[index] ||
		    !s_flash_dev->ops[index]->open_preflash)
			return -1;
		return s_flash_dev->ops[index]->open_preflash(
				s_flash_dev->ic_data[index], led_mask);

	case FLASH_MODE_OPEN_TORCH:
		index = ctrl->index;
		if (index >= FLASH_DRV_MAX)
			return -1;
		if (!s_flash_dev->ops[index] ||
		    !s_flash_dev->ops[index]->open_torch)
			return -1;
		return s_flash_dev->ops[index]->open_torch(
				s_flash_dev->ic_data[index], led_mask);

	case FLASH_MODE_OPEN_HIGHLIGHT:
		index = ctrl->index;
		if (index >= FLASH_DRV_MAX)
			return -1;
		if (!s_flash_dev->ops[index] ||
		    !s_flash_dev->ops[index]->open_highlight)
			return -1;
		return s_flash_dev->ops[index]->open_highlight(
				s_flash_dev->ic_data[index], led_mask);

	default:
		flash_log_info("invalid mode: 0x%x led_index %d\n",
			       mode, led_mask);
		return 0;
	}
}
EXPORT_SYMBOL(sprd_flash_ctrl);

/**
 * sprd_flash_cfg() - set the "level" of one torch/preflash/highlight element
 * @cfg: command, see struct sprd_flash_cfg
 *
 * Returns -1 when the framework or the IC slot is missing, otherwise the
 * value returned by the IC callback.
 */
int sprd_flash_cfg(struct sprd_flash_cfg *cfg)
{
	struct sprd_flash_element element;
	struct sprd_flash_ic_ops *ops;
	u8 index;

	if (!cfg || !s_flash_dev) {
		flash_log_info("flash_cfg NULL parm %p flash_dev %p\n",
			       cfg, s_flash_dev);
		return -1;
	}

	memset(&element, 0, sizeof(element));
	element.value = cfg->element_a;
	element.index = cfg->element_b;

	index = cfg->index;
	flash_log_info("flash value: %d index %d status %d\n",
		       element.value, index, cfg->mode);

	if (index >= FLASH_DRV_MAX)
		return -1;

	ops = s_flash_dev->ops[index];
	if (!ops)
		return -1;

	switch (cfg->mode) {
	case 0:		/* torch */
		if (!ops->cfg_value_torch)
			return -1;
		return ops->cfg_value_torch(s_flash_dev->ic_data[index],
					    cfg->element, &element);

	case 1:		/* preflash */
		if (!ops->cfg_value_preflash)
			return -1;
		return ops->cfg_value_preflash(s_flash_dev->ic_data[index],
					       cfg->element, &element);

	case 2:		/* highlight */
		if (!ops->cfg_value_highlight)
			return -1;
		return ops->cfg_value_highlight(s_flash_dev->ic_data[index],
						cfg->element, &element);

	default:
		flash_log_info("invalid mode: 0x%x index %d\n",
			       cfg->mode, index);
		return 0;
	}
}
EXPORT_SYMBOL(sprd_flash_cfg);

/* "test" attribute: hex word, bit 15 = IC index, [7:4] = element, [3:0] = cmd */
static ssize_t show_help(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct sprd_flash_dev *flash_dev = dev_get_drvdata(dev);

	if (!flash_dev) {
		flash_log_err("flash device is null\n");
		return 0;
	}

	if ((flash_dev->flash_type & 0xf) == FLASH_TYPE_NAMED)
		return sprintf(buf, "%s\n", (char *)flash_capacity.name);

	return sprintf(buf, "%x\n", flash_dev->flash_type);
}

/*
 * Every write to "test" is a hexadecimal word:
 *
 *   [3:0]   command
 *   [7:4]   element / index
 *   [12:8]  level (only for the "cfg value" commands)
 *   [15]    IC index
 *
 * Commands:
 *   0  open preflash        1  close preflash
 *   2  open torch           3  close torch
 *   4  open highlight       5  close highlight
 *   6  cfg torch            7  cfg preflash        8  cfg highlight
 *   9  read the info blob into @flash_capacity
 */
static ssize_t flash_sysfs_test(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sprd_flash_dev *flash_dev = dev_get_drvdata(dev);
	struct sprd_flash_element element;
	struct sprd_flash_info info;
	struct sprd_flash_ic_ops *ops;
	u32 value, index, cmd;
	int ret;

	if (!flash_dev) {
		flash_log_err("flash device is null\n");
		return 0;
	}

	ret = kstrtouint(buf, 16, &value);
	if (ret)
		return ret;

	flash_dev->flash_type = value & 0xffff;
	cmd = value & 0xf;
	index = (value >> 4) & 0xf;

	memset(&element, 0, sizeof(element));
	element.value = (value >> 8) & 0x1f;

	flash_log_info("cmd:%d flash:%d element.index %d flash_idx %d\n",
		       cmd, (value >> 15) & 1, index, element.value);

	ops = flash_dev->ops[(value >> 15) & 1];
	if (!ops)
		return count;

	switch (cmd) {
	case 0:
		if (ops->open_preflash)
			ops->open_preflash(flash_dev->ic_data[(value >> 15) & 1],
					   index);
		break;
	case 1:
		if (ops->close_preflash)
			ops->close_preflash(flash_dev->ic_data[(value >> 15) & 1],
					    index);
		break;
	case 2:
		flash_dev->state[0] = 1;
		if (ops->open_torch)
			ops->open_torch(flash_dev->ic_data[(value >> 15) & 1],
					index);
		break;
	case 3:
		flash_dev->state[0] = 0;
		if (ops->close_torch)
			ops->close_torch(flash_dev->ic_data[(value >> 15) & 1],
					 index);
		break;
	case 4:
		if (ops->open_highlight)
			ops->open_highlight(flash_dev->ic_data[(value >> 15) & 1],
					    index);
		break;
	case 5:
		if (ops->close_highlight)
			ops->close_highlight(flash_dev->ic_data[(value >> 15) & 1],
					     index);
		break;
	case 6:
		if (ops->cfg_value_preflash)
			ops->cfg_value_preflash(
				flash_dev->ic_data[(value >> 15) & 1],
				index, &element);
		break;
	case 7:
		if (ops->cfg_value_torch)
			ops->cfg_value_torch(
				flash_dev->ic_data[(value >> 15) & 1],
				index, &element);
		break;
	case 8:
		if (ops->cfg_value_highlight)
			ops->cfg_value_highlight(
				flash_dev->ic_data[(value >> 15) & 1],
				index, &element);
		break;
	case 9:
		memset(&info, 0, sizeof(info));
		if (ops->get_flash_info)
			info = ops->get_flash_info(
				flash_dev->ic_data[(value >> 15) & 1],
				index, &flash_capacity);
		flash_capacity = info;
		flash_dev->flash_name = info.name;
		flash_log_info("%s %s", (char *)info.name, __func__);
		break;
	default:
		break;
	}

	return count;
}

static DEVICE_ATTR(test, 0600, show_help, flash_sysfs_test);

/*
 * The misc device is only usable once an IC registered itself: the open hook
 * seeds file->private_data with the driver data of the misc device, which is
 * what ->ioctl()/->release() validate.
 */
static int sprd_flash_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *misc = filp->private_data;

	if (!misc)
		return -EFAULT;

	filp->private_data = dev_get_drvdata(misc->this_device);
	return 0;
}

static int sprd_flash_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	return filp->private_data ? 0 : -EFAULT;
}

/*
 * Make sure no LED is left on when userspace closes the device: every IC slot
 * gets all three close callbacks twice, once with LED mask 1 and once with
 * mask 2 (the vendor build hard codes both masks, they are not loop derived).
 */
static int sprd_flash_release(struct inode *inode, struct file *filp)
{
	struct sprd_flash_dev *dev = filp->private_data;
	struct sprd_flash_ic_ops *ops;
	void *data;
	int i;

	if (!dev)
		return -EFAULT;

	for (i = 0; i < FLASH_DRV_MAX; i++) {
		ops = dev->ops[i];
		if (!ops)
			continue;

		data = dev->ic_data[i];
		if (ops->close_torch)
			ops->close_torch(data, 1);
		if (ops->close_preflash)
			ops->close_preflash(data, 1);
		if (ops->close_highlight)
			ops->close_highlight(data, 1);
		if (ops->close_torch)
			ops->close_torch(data, 2);
		if (ops->close_preflash)
			ops->close_preflash(data, 2);
		if (ops->close_highlight)
			ops->close_highlight(data, 2);
	}

	filp->private_data = NULL;
	return 0;
}

static const struct file_operations flash_fops = {
	.owner = THIS_MODULE,
	.open = sprd_flash_open,
	.unlocked_ioctl = sprd_flash_ioctl,
	.compat_ioctl = sprd_flash_ioctl,
	.release = sprd_flash_release,
};

static int sprd_flash_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_flash_dev *flash_dev;
	int ret;

	/* the vendor build uses devm_kmalloc() and relies on the page being
	 * zero filled; kzalloc() makes that explicit without changing behaviour
	 */
	flash_dev = devm_kzalloc(dev, sizeof(*flash_dev), GFP_KERNEL);
	if (!flash_dev)
		return -ENOMEM;

	flash_dev->misc.minor = MISC_DYNAMIC_MINOR;
	flash_dev->misc.name = FLASH_DRV_NAME;
	flash_dev->misc.fops = &flash_fops;
	flash_dev->misc.parent = NULL;

	ret = misc_register(&flash_dev->misc);
	if (ret) {
		flash_log_err("failed to register misc devices\n");
		goto err_probe;
	}

	dev_set_drvdata(flash_dev->misc.this_device, flash_dev);
	platform_set_drvdata(pdev, flash_dev);
	s_flash_dev = flash_dev;

	ret = device_create_file(flash_dev->misc.this_device, &dev_attr_test);
	if (ret) {
		flash_log_err("failed to create flash test file");
		misc_deregister(&flash_dev->misc);
		goto err_probe;
	}

	return 0;

err_probe:
	flash_log_err("failed to probe flash driver\n");
	return ret;
}

static int sprd_flash_remove(struct platform_device *pdev)
{
	struct sprd_flash_dev *flash_dev = platform_get_drvdata(pdev);

	device_remove_file(flash_dev->misc.this_device, &dev_attr_test);
	misc_deregister(&flash_dev->misc);
	platform_set_drvdata(pdev, NULL);
	s_flash_dev = NULL;
	return 0;
}

/* "drvier" is the vendor spelling, kept for readability of the mapping */
static struct platform_driver sprd_flash_drvier = {
	.probe = sprd_flash_probe,
	.remove = sprd_flash_remove,
	.driver = {
		.name = FLASH_DRV_NAME,
	},
};

static int __init flash_init(void)
{
	struct platform_device_info pdevinfo;
	int ret;

	ret = platform_driver_register(&sprd_flash_drvier);
	if (ret) {
		flash_log_err("failed to register flash driver\n");
		return ret;
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = FLASH_DRV_NAME;
	pdevinfo.id = PLATFORM_DEVID_NONE;
	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR_OR_NULL(pdev)) {
		platform_driver_unregister(&sprd_flash_drvier);
		return (int)PTR_ERR(pdev);
	}

	return 0;
}

static void __exit flash_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&sprd_flash_drvier);
}

module_init(flash_init);
module_exit(flash_exit);

MODULE_DESCRIPTION("Sprd Flash Driver");
MODULE_LICENSE("GPL");
