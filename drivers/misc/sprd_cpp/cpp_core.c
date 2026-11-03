// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- misc device / 平台驱动 (cpp_core.c).
 *
 * 重建自 vendor `cpp_lite/src/cpp_core.c`。
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>
#include <linux/sched.h>

#include "cpp_lite.h"

#define CORE_ERR(fmt, ...) \
	pr_err("CPP_CORE: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define CORE_DBG(fmt, ...) \
	pr_debug("CPP_CORE: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

struct cpp_device *g_cpp_dev;

int cppcore_module_enable(struct cpp_device *dev)
{
	int ret;

	if (!dev) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (dev->module_enabled)
		return 0;

	ret = sprd_glb_mm_pw_on_cfg();
	if (ret) {
		CORE_ERR("fail to enable cpp module\n");
		return ret;
	}

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0) {
		CORE_ERR("fail to resume cpp device %d\n", ret);
		sprd_glb_mm_pw_off_cfg();
		return ret;
	}

	dev->module_enabled = true;

	return 0;
}

int cppcore_module_disable(struct cpp_device *dev)
{
	if (!dev) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	mutex_lock(&dev->lock);
	sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_sync(dev->dev);
	__pm_relax(dev->ws);
	dev->module_enabled = false;
	mutex_unlock(&dev->lock);

	return 0;
}

int cppcore_open(struct inode *inode, struct file *file)
{
	struct cpp_device *dev;
	int ret;

	if (!file) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	dev = container_of(file->private_data, struct cpp_device, misc);
	if (!dev) {
		dev = g_cpp_dev;
		if (!dev) {
			CORE_ERR("fail to get valid cpp hw info\n");
			return -ENODEV;
		}
	}

	if (atomic_read(&dev->open_cnt) >= 1) {
		CORE_DBG("cpp device node has been opened %d\n",
			 atomic_read(&dev->open_cnt));
		CORE_ERR("cpp device node has been opened %d\n",
			 atomic_read(&dev->open_cnt));
		return -EBUSY;
	}

	ret = cppcore_module_enable(dev);
	if (ret) {
		CORE_ERR("fail to enable cpp module\n");
		return ret;
	}

	__pm_stay_awake(dev->ws);

	ret = mutex_lock_interruptible(&dev->lock);
	if (ret) {
		CORE_ERR("fail to get md device\n");
		goto err_relax;
	}

	ret = cpp_int_irq_request(dev);
	if (ret) {
		CORE_ERR("fail to install IRQ %d\n", ret);
		mutex_unlock(&dev->lock);
		goto err_relax;
	}

	atomic_inc(&dev->open_cnt);
	mutex_unlock(&dev->lock);

	CORE_DBG("open sprd_cpp success\n");

	return 0;

err_relax:
	__pm_relax(dev->ws);
	sprd_glb_mm_pw_off_cfg();
	return ret;
}

int cppcore_release(struct inode *inode, struct file *file)
{
	struct cpp_device *dev = g_cpp_dev;

	if (!file) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	if (!dev) {
		CORE_ERR("fail to get valid input ptr cpp_handle %p\n", dev);
		return -EINVAL;
	}

	if (atomic_read(&dev->open_cnt) == 0) {
		CORE_ERR("fail to close cpp device\n");
		return -EINVAL;
	}

	cpp_int_irq_free(dev);

	atomic_dec(&dev->open_cnt);

	CORE_ERR("cpp mdbg info:%d\n", atomic_read(&dev->open_cnt));

	cppcore_module_disable(dev);

	return 0;
}

long cppcore_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cpp_device *dev;
	long ret;

	if (!file) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	dev = g_cpp_dev;
	if (!dev) {
		CORE_ERR("fail to get cpp device\n");
		return -EFAULT;
	}

	switch (cmd) {
	case SPRD_CPP_IO_OPEN_ROT:
		ret = cppcore_ioctl_open_rot(dev, arg);
		break;
	case SPRD_CPP_IO_CLOSE_ROT:
		ret = cppcore_ioctl_open_rot(dev, arg);
		break;
	case SPRD_CPP_IO_START_ROT:
		ret = cppcore_ioctl_start_rot(dev, arg);
		break;
	case SPRD_CPP_IO_OPEN_SCALE:
		ret = cppcore_ioctl_open_scale(dev, arg);
		break;
	case SPRD_CPP_IO_START_SCALE:
		ret = cppcore_ioctl_start_scale(dev, arg);
		break;
	case SPRD_CPP_IO_STOP_SCALE:
		ret = cppdrv_scale_sl_stop(&dev->scale);
		break;
	case SPRD_CPP_IO_OPEN_DMA:
		ret = cppcore_ioctl_open_dma(dev, arg);
		break;
	case SPRD_CPP_IO_START_DMA:
		ret = cppcore_ioctl_start_dma(dev, arg);
		break;
	case SPRD_CPP_IO_SCALE_CAPABILITY:
		ret = cppcore_ioctl_get_scale_cap(dev, arg);
		break;
	default:
		CORE_ERR("fail to cmd %d\n", cmd & 0xff);
		ret = -EINVAL;
		break;
	}

	if (ret) {
		CORE_ERR("fail to cmd %d\n", cmd & 0xff);
		return ret;
	}

	return 0;
}

static const struct file_operations cpp_fops = {
	.owner = THIS_MODULE,
	.open = cppcore_open,
	.release = cppcore_release,
	.unlocked_ioctl = cppcore_ioctl,
	.compat_ioctl = cppcore_ioctl,
};

static int cppcore_platform_probe(struct platform_device *pdev)
{
	struct cpp_device *dev;
	int ret;

	if (!pdev) {
		CORE_ERR("fail to get valid input ptr\n");
		return -EINVAL;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		CORE_ERR("fail to alloc cpp dev memory\n");
		return -ENOMEM;
	}

	dev->dev = &pdev->dev;
	dev->hw = devm_kzalloc(&pdev->dev, sizeof(*dev->hw), GFP_KERNEL);
	if (!dev->hw) {
		CORE_ERR("fail to alloc cpp dev memory\n");
		return -ENOMEM;
	}

	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.name = "sprd_cpp";
	dev->misc.fops = &cpp_fops;
	dev->misc.parent = &pdev->dev;

	ret = misc_register(&dev->misc);
	if (ret) {
		CORE_ERR("fail to register misc devices\n");
		return ret;
	}

	mutex_init(&dev->lock);
	spin_lock_init(&dev->scale.slock);
	spin_lock_init(&dev->rot.slock);
	spin_lock_init(&dev->dma.slock);
	atomic_set(&dev->open_cnt, 0);

	dev->ws = wakeup_source_create("cpp");
	if (dev->ws)
		wakeup_source_add(dev->ws);

	platform_set_drvdata(pdev, dev);
	g_cpp_dev = dev;

	ret = cpphw_probe(pdev, dev->hw);
	if (ret) {
		CORE_ERR("fai to probe cpp module\n");
		misc_deregister(&dev->misc);
		return ret;
	}

	dev->scale.ctrl = (u32 *)(g_cpp_base + CPP_REG_CTRL);
	dev->rot.ctrl = dev->scale.ctrl;
	dev->dma.ctrl = dev->scale.ctrl;
	dev->scale.hw = dev->hw;
	dev->rot.hw = dev->hw;
	dev->dma.hw = dev->hw;

	pm_runtime_enable(&pdev->dev);

	CORE_DBG("cppcore_probe\n");

	return 0;
}

int cppcore_probe(struct platform_device *pdev)
{
	return cppcore_platform_probe(pdev);
}

int cppcore_remove(struct platform_device *pdev)
{
	struct cpp_device *dev = platform_get_drvdata(pdev);

	if (!dev)
		return -EINVAL;

	if (dev->ws) {
		wakeup_source_remove(dev->ws);
		wakeup_source_destroy(dev->ws);
	}

	pm_runtime_disable(&pdev->dev);
	misc_deregister(&dev->misc);
	g_cpp_dev = NULL;

	return 0;
}

static int cpp_probe(struct platform_device *pdev)
{
	return cppcore_probe(pdev);
}

static int cpp_remove(struct platform_device *pdev)
{
	return cppcore_remove(pdev);
}

static const struct of_device_id cpp_of_match[] = {
	{ .compatible = "sprd,cpp" },
	{ }
};
MODULE_DEVICE_TABLE(of, cpp_of_match);

static struct platform_driver cpp_driver = {
	.probe = cpp_probe,
	.remove = cpp_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sprd_cpp",
		.of_match_table = cpp_of_match,
	},
};

static int __init cpp_init(void)
{
	return platform_driver_register(&cpp_driver);
}

static void __exit cpp_exit(void)
{
	platform_driver_unregister(&cpp_driver);
}

module_init(cpp_init);
module_exit(cpp_exit);

MODULE_AUTHOR("Multimedia_Camera@unisoc");
MODULE_DESCRIPTION("CPP Driver");
MODULE_LICENSE("GPL");
