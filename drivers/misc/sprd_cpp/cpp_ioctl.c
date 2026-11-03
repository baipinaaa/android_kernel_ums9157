// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc CPP lite r6p0 -- ioctl 实现 (cpp_ioctl.c).
 *
 * 重建自 vendor `cpp_lite/src/cpp_ioctl.c`。
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "cpp_lite.h"

#define CORE_ERR(fmt, ...) \
	pr_err("CPP_CORE: %d %d %s : " fmt, current->pid, __LINE__, \
	       __func__, ##__VA_ARGS__)

#define CORE_DBG(fmt, ...) \
	pr_debug("CPP_CORE: %d %d %s : " fmt, current->pid, __LINE__, \
		 __func__, ##__VA_ARGS__)

long cppcore_ioctl_open_rot(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_rot_dev *rot;
	int ret;

	CORE_DBG("cppcore_ioctl_open_rot arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	rot = &dev->rot;
	rot->hw = dev->hw;

	ret = cppdrv_rot_format_get(rot);
	if (ret) {
		CORE_ERR("fail to open rot drv\n");
		return ret;
	}

	return 0;
}

long cppcore_ioctl_start_rot(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_rot_dev *rot;
	struct cpp_rot_param param;
	u32 *cfg;
	int ret;

	CORE_DBG("cppcore_ioctl_start_rot arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	rot = &dev->rot;

	if (copy_from_user(&param, (void __user *)arg, sizeof(param))) {
		CORE_ERR("fail to check rot parm\n");
		return -EFAULT;
	}

	ret = cppdrv_rot_parm_check(&param);
	if (ret) {
		CORE_ERR("fail to check rot parm\n");
		return ret;
	}

	cfg = cpp_drv_get_cpp_res(dev, sizeof(*cfg) * 8);
	if (!cfg) {
		CORE_ERR("fail to open rot drv\n");
		return -ENOMEM;
	}

	mutex_lock(&rot->lock);

	ret = cppdrv_rot_stop(rot);
	if (ret) {
		CORE_ERR("fail to stop rot\n");
		mutex_unlock(&rot->lock);
		return ret;
	}

	ret = cppdrv_rot_y_parm_set(rot, &param, cfg);
	if (ret) {
		CORE_ERR("fail to  set rot v parm\n");
		mutex_unlock(&rot->lock);
		return ret;
	}

	ret = cppdrv_rot_uv_parm_set(rot, &param, cfg);
	if (ret) {
		CORE_ERR("fail to  set rot v parm\n");
		mutex_unlock(&rot->lock);
		return ret;
	}

	reinit_completion(&rot->done);
	ret = cppdrv_rot_start(rot);
	if (ret) {
		CORE_ERR("fail to stop rot\n");
		mutex_unlock(&rot->lock);
		return ret;
	}

	ret = wait_for_completion_timeout(&rot->done,
					  msecs_to_jiffies(CPP_TIMEOUT_MS));
	if (!ret) {
		CORE_ERR("cpp rotation ret %d\n", ret);
		cppdrv_rot_stop(rot);
		mutex_unlock(&rot->lock);
		return -ETIMEDOUT;
	}

	mutex_unlock(&rot->lock);

	CORE_DBG("cpp rotation ret %d\n", ret);

	return 0;
}

long cppcore_ioctl_open_scale(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_scale_dev *sc;
	int ret;

	CORE_DBG("cppcore_ioctl_open_scale arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	sc = &dev->scale;
	sc->hw = dev->hw;

	ret = cppdrv_scale_max_size_get(sc, NULL);
	if (ret) {
		CORE_ERR("fail to get max size form user\n");
		return ret;
	}

	return 0;
}

long cppcore_ioctl_start_scale(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_scale_dev *sc;
	struct cpp_scale_param param;
	struct cpp_scale_cfg *cfg;
	int ret;

	CORE_DBG("cppcore_ioctl_start_scale arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	sc = &dev->scale;
	if (!sc->ctrl) {
		CORE_ERR("fail to get valid scif!\n");
		return -EINVAL;
	}

	if (copy_from_user(&param, (void __user *)arg, sizeof(param))) {
		CORE_ERR("fail to get parm form user\n");
		return -EFAULT;
	}

	cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg) {
		CORE_ERR("fail to get parm form user\n");
		return -ENOMEM;
	}
	memset(cfg, 0, sizeof(*cfg));

	ret = cppdrv_scale_param_check(&param);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		kfree(cfg);
		return ret;
	}

	mutex_lock(&sc->lock);

	ret = cppdrv_scale_stop(sc);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	ret = cppdrv_scale_slice_param_check(cfg);
	if (ret) {
		CORE_ERR("fail to get slice support info\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	ret = cppdrv_scale_cfg_param_set(cfg);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	ret = cppdrv_scale_slice_param_set(cfg);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	ret = cppdrv_scale_set_regs(cfg);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	reinit_completion(&sc->done);

	ret = cppdrv_scale_start(sc);
	if (ret) {
		CORE_ERR("fail to set slice param\n");
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return ret;
	}

	ret = wait_for_completion_timeout(&sc->done,
					  msecs_to_jiffies(CPP_TIMEOUT_MS));
	if (!ret) {
		CORE_ERR("fail to get scaling done com\n");
		cppdrv_scale_stop(sc);
		mutex_unlock(&sc->lock);
		kfree(cfg);
		return -ETIMEDOUT;
	}

	cppdrv_scale_sl_stop(sc);
	mutex_unlock(&sc->lock);
	kfree(cfg);

	CORE_DBG("cpp scaling done\n");

	return 0;
}

long cppcore_ioctl_open_dma(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_dma_dev *dm;
	int ret;

	CORE_DBG("cppcore_ioctl_open_dma arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	dm = &dev->dma;
	dm->hw = dev->hw;

	ret = cppdrv_dma_param_set(NULL);
	if (ret) {
		CORE_ERR("fail to open dma drv\n");
		return ret;
	}

	return 0;
}

long cppcore_ioctl_start_dma(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_dma_dev *dm;
	struct cpp_dma_param param;
	int ret;

	CORE_DBG("cppcore_ioctl_start_dma arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	dm = &dev->dma;

	if (copy_from_user(&param, (void __user *)arg, sizeof(param))) {
		CORE_ERR("fail to get parm form user\n");
		return -EFAULT;
	}

	mutex_lock(&dm->lock);

	ret = cppdrv_dma_stop(dm);
	if (ret) {
		CORE_ERR("fail to stop dma\n");
		mutex_unlock(&dm->lock);
		return ret;
	}

	ret = cppdrv_dma_param_set(&param);
	if (ret) {
		CORE_ERR("fail to set parm\n");
		mutex_unlock(&dm->lock);
		return ret;
	}

	ret = cppdrv_dma_enable(dm);
	if (ret) {
		CORE_ERR("fail to enable dma\n");
		mutex_unlock(&dm->lock);
		return ret;
	}

	reinit_completion(&dm->done);

	ret = cppdrv_dma_start(dm);
	if (ret) {
		CORE_ERR("fail to start dma\n");
		mutex_unlock(&dm->lock);
		return ret;
	}

	ret = wait_for_completion_timeout(&dm->done,
					  msecs_to_jiffies(CPP_TIMEOUT_MS));
	if (!ret) {
		CORE_ERR("fail to get dma done com\n");
		cppdrv_dma_stop(dm);
		mutex_unlock(&dm->lock);
		return -ETIMEDOUT;
	}

	mutex_unlock(&dm->lock);

	CORE_DBG("cpp dma done\n");

	return 0;
}

long cppcore_ioctl_get_scale_cap(struct cpp_device *dev, unsigned long arg)
{
	struct cpp_scale_capability cap;
	int ret;

	CORE_DBG("cppcore_ioctl_get_scale_cap arg 0x%lx\n", arg);

	if (!dev || !dev->hw) {
		CORE_ERR("fail to get invalid cppif!\n");
		return -EINVAL;
	}

	ret = cppdrv_scale_capability_get(&cap);
	if (ret) {
		CORE_ERR("fail to get slice support info\n");
		return ret;
	}

	if (copy_to_user((void __user *)arg, &cap, sizeof(cap))) {
		CORE_ERR("fail to get parm form user\n");
		return -EFAULT;
	}

	return 0;
}
