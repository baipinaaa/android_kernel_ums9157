// SPDX-License-Identifier: GPL-2.0
/*
 * sprd_apipe.c - Unisoc audio pipe (APIPE) character device driver
 *
 * Reconstructed from the vendor binary module `sprd_apipe.ko`
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50, .text 0x15c8 = 5576 bytes,
 *  14 functions, module name `sprd_apipe`).
 *
 * The vendor module was built from this source file; the full build path is
 * embedded in .rodata of the binary:
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd_audio/audio_pipe/sprd_apipe.c
 *
 * modinfo of the binary:
 *   author=SPRD
 *   description=apipe driver
 *   license=GPL v2
 *   depends=
 *
 * The driver is a misc character device ("sprd_apipe") exported by a platform
 * driver ("sprd-apipe", of_match "unisoc,apipe").  It implements a byte stream
 * pipe backed by one kfifo plus a small "point" table used by the audio HAL to
 * publish pre-allocated buffer addresses (used by the DSP audio pipes).
 *
 * ---------------------------------------------------------------------------
 * Binary layout of the private data (devm_kmalloc(&pdev->dev, 0x148, GFP_KERNEL))
 *   0x000 struct miscdevice misc;          (0x50 bytes)
 *   0x050 char              name[64];      (from pdev->name or dev_name())
 *   0x090 struct kfifo      kfifo;         (in/out/mask/esize/data)
 *   0x0a8 int               open_count;
 *   0x0ac bool              rdone;         (forced read wake up)
 *   0x0ad bool              wdone;         (forced write wake up)
 *   0x0b0 void             *buf;           (vmalloc'ed kfifo memory)
 *   0x0b8 u32               buf_size;
 *   0x0c0 struct mutex      mutex;         ("&apipe_dev->mutex")
 *   0x0e0 wait_queue_head_t wwait;         ("&apipe_dev->wwait")
 *   0x0f8 wait_queue_head_t rwait;         ("&apipe_dev->rwait")
 *   0x110 struct mutex      w_mutex;       ("&apipe_dev->w_mutex")
 *   0x130 struct apipe_point *points;      (count * 24 bytes, vmalloc'ed)
 *   0x138 u32               point_count;
 *   0x140 struct device    *dev;
 *   total 0x148
 *
 * ioctl command set (all with type 'A'), exactly as decoded from the dispatch
 * table in apipe_ioctl():
 *   0x40044100 _IOW('A', 0, u32)  APIPE_WAKE_READ   rdone = 1; wake rwait
 *   0x40044101 _IOW('A', 1, u32)  APIPE_WAKE_WRITE  wdone = 1; wake wwait
 *   0x40044102 _IOW('A', 2, u32)  APIPE_SET_BUFSZ   (re)alloc kfifo at size
 *   0x80044103 _IOR('A', 3, u32)  APIPE_GET_BUFSZ   put_user(buf_size)
 *   0x80044104 _IOR('A', 4, u32)  APIPE_GET_LEN     put_user(kfifo_len)
 *   0x80044105 _IOR('A', 5, u32)  APIPE_GET_MAGIC   put_user(0xffff)
 *   0x4106     _IO('A', 6)        APIPE_LOCK        mutex_lock(w_mutex)
 *   0x4107     _IO('A', 7)        APIPE_UNLOCK      mutex_unlock(w_mutex)
 *   0x40044108 _IOW('A', 8, u32)  APIPE_POINT_ALLOC point_count = count
 *   0xc0184109 _IOWR('A', 9, struct apipe_point)  APIPE_POINT_INFO_GET
 *   0x4018410a _IOW('A', 10, struct apipe_point)  APIPE_POINT_INFO_SET
 * ---------------------------------------------------------------------------
 */

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#define APIPE_MISC_NAME		"sprd_apipe"
#define APIPE_DRV_NAME		"sprd-apipe"
#define APIPE_MAX_NAME		64

/* default kfifo allocation, see apipe_open() */
#define APIPE_DEFAULT_BUFSZ	0x4000
/* apipe_ioctl(): (size - 1) > 0x9ffffe => -ENOMEM */
#define APIPE_MAX_BUFSZ		0x9fffff
/* apipe_ioctl(): point table is count * 24 bytes, count in [1, 0x1f3] */
#define APIPE_MAX_POINTS	0x1f3
/* apipe_ioctl(): value stored by APIPE_GET_MAGIC */
#define APIPE_MAGIC_VALUE	0xffff

/* one entry of the point table, 24 bytes in the binary */
struct apipe_point {
	u32 index;
	u32 reserved;
	u64 addr;
	u64 len;
};

struct apipe_dev {
	struct miscdevice misc;
	char name[APIPE_MAX_NAME];
	struct kfifo kfifo;
	int open_count;
	bool rdone;
	bool wdone;
	void *buf;
	u32 buf_size;
	struct mutex mutex;
	wait_queue_head_t wwait;
	wait_queue_head_t rwait;
	struct mutex w_mutex;
	struct apipe_point *points;
	u32 point_count;
	struct device *dev;
};

/* ------------------------------------------------------------------------- */
/* ioctl numbers - the binary compares the raw command words, "nr" is fixed  */
/* ------------------------------------------------------------------------- */
#define APIPE_IOC_WAKE_READ	_IOW('A', 0, int)
#define APIPE_IOC_WAKE_WRITE	_IOW('A', 1, int)
#define APIPE_IOC_SET_BUFSZ	_IOW('A', 2, int)
#define APIPE_IOC_GET_BUFSZ	_IOR('A', 3, int)
#define APIPE_IOC_GET_LEN	_IOR('A', 4, int)
#define APIPE_IOC_GET_MAGIC	_IOR('A', 5, int)
#define APIPE_IOC_LOCK		_IO('A', 6)
#define APIPE_IOC_UNLOCK	_IO('A', 7)
#define APIPE_IOC_POINT_ALLOC	_IOW('A', 8, int)
#define APIPE_IOC_POINT_GET	_IOWR('A', 9, struct apipe_point)
#define APIPE_IOC_POINT_SET	_IOW('A', 10, struct apipe_point)

static int apipe_open(struct inode *inode, struct file *file)
{
	struct apipe_dev *apipe_dev = file->private_data;
	int ret;

	if (!apipe_dev)
		return -EINVAL;

	mutex_lock(&apipe_dev->mutex);

	if (apipe_dev->open_count) {
		apipe_dev->open_count++;
		goto out;
	}

	apipe_dev->buf = vmalloc(APIPE_DEFAULT_BUFSZ);
	if (!apipe_dev->buf) {
		mutex_unlock(&apipe_dev->mutex);
		dev_err(apipe_dev->dev, "failed: no mem\n");
		return -ENOMEM;
	}

	/*
	 * The binary still carries the string "apipe_open failed: %d", it is
	 * no longer referenced by any code path of this revision.
	 */

	apipe_dev->buf_size = APIPE_DEFAULT_BUFSZ;
	apipe_dev->open_count = 1;

	ret = kfifo_init(&apipe_dev->kfifo, apipe_dev->buf,
			 APIPE_DEFAULT_BUFSZ);
	if (ret < 0) {
		vfree(apipe_dev->buf);
		apipe_dev->buf = NULL;
		mutex_unlock(&apipe_dev->mutex);
		dev_err(apipe_dev->dev, "failed: %d\n", ret);
		return ret;
	}

	init_waitqueue_head(&apipe_dev->wwait);
	init_waitqueue_head(&apipe_dev->rwait);

out:
	mutex_unlock(&apipe_dev->mutex);
	return 0;
}

static int apipe_release(struct inode *inode, struct file *file)
{
	struct apipe_dev *apipe_dev = file->private_data;

	if (!apipe_dev)
		return -EINVAL;

	mutex_lock(&apipe_dev->mutex);

	if (apipe_dev->open_count) {
		if (--apipe_dev->open_count == 0) {
			wake_up(&apipe_dev->wwait);
			wake_up(&apipe_dev->rwait);
			vfree(apipe_dev->buf);
			if (apipe_dev->points) {
				vfree(apipe_dev->points);
				apipe_dev->points = NULL;
			}
			file->private_data = NULL;
		}
	}

	mutex_unlock(&apipe_dev->mutex);
	return 0;
}

static ssize_t apipe_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct apipe_dev *apipe_dev = file->private_data;
	unsigned int copied = 0;
	int ret;

	if (!apipe_dev)
		return -EINVAL;

	/* O_NONBLOCK: single shot, never waits */
	if (file->f_flags & O_NONBLOCK) {
		mutex_lock(&apipe_dev->mutex);
		ret = kfifo_to_user(&apipe_dev->kfifo, buf,
				    (unsigned int)count, &copied);
		mutex_unlock(&apipe_dev->mutex);
		if (ret < 0)
			return ret;

		wake_up(&apipe_dev->wwait);
		return copied;
	}

	do {
		mutex_lock(&apipe_dev->mutex);
		ret = kfifo_to_user(&apipe_dev->kfifo, buf,
				    (unsigned int)count, &copied);
		mutex_unlock(&apipe_dev->mutex);

		if (!ret && copied)
			wake_up(&apipe_dev->wwait);

		if (apipe_dev->rdone || (!ret && copied)) {
			apipe_dev->rdone = false;
			return copied;
		}

		if (ret < 0)
			dev_dbg(apipe_dev->dev, "copy to user err:%d!\n", ret);

		ret = wait_event_interruptible(apipe_dev->rwait,
				!kfifo_is_empty(&apipe_dev->kfifo) ||
				apipe_dev->rdone);
		if (ret < 0) {
			dev_err(apipe_dev->dev, "wait interrupted!\n");
			return ret;
		}
	} while (1);
}

static ssize_t apipe_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct apipe_dev *apipe_dev = file->private_data;
	unsigned int copied = 0;
	int ret;

	if (!apipe_dev)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK) {
		mutex_lock(&apipe_dev->mutex);
		ret = kfifo_from_user(&apipe_dev->kfifo, buf,
				      (unsigned int)count, &copied);
		mutex_unlock(&apipe_dev->mutex);
		if (ret < 0)
			return ret;

		wake_up(&apipe_dev->rwait);
		return copied;
	}

	/* a pending APIPE_IOC_WAKE_WRITE request aborts the blocking loop */
	if (apipe_dev->wdone) {
		apipe_dev->wdone = false;
		return 0;
	}

	do {
		mutex_lock(&apipe_dev->mutex);
		ret = kfifo_from_user(&apipe_dev->kfifo, buf,
				      (unsigned int)count, &copied);
		mutex_unlock(&apipe_dev->mutex);

		if (!ret && copied) {
			wake_up(&apipe_dev->rwait);
			return copied;
		}

		if (ret < 0)
			dev_dbg(apipe_dev->dev, "copy from user err:%d!\n",
				ret);

		if (!kfifo_is_full(&apipe_dev->kfifo)) {
			if (!apipe_dev->wdone)
				continue;
			apipe_dev->wdone = false;
			return 0;
		}

		ret = wait_event_interruptible(apipe_dev->wwait,
				!kfifo_is_full(&apipe_dev->kfifo) ||
				apipe_dev->wdone);
		if (ret < 0) {
			dev_err(apipe_dev->dev, "wait interrupted!\n");
			return ret;
		}
	} while (1);
}

static __poll_t apipe_poll(struct file *file, struct poll_table_struct *wait)
{
	struct apipe_dev *apipe_dev = file->private_data;
	__poll_t mask;

	if (!apipe_dev)
		return -EINVAL;

	if (wait) {
		poll_wait(file, &apipe_dev->wwait, wait);
		poll_wait(file, &apipe_dev->rwait, wait);
	}

	mask = kfifo_len(&apipe_dev->kfifo) ?
		(EPOLLIN | EPOLLRDNORM) : 0;
	mask |= (EPOLLOUT | EPOLLWRNORM);

	return mask;
}

static long apipe_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct apipe_dev *apipe_dev = file->private_data;
	void __user *uarg = (void __user *)arg;
	struct apipe_point point;
	unsigned int value = 0;
	unsigned int size;
	void *new_buf;
	int ret;

	if (!apipe_dev)
		return -EFAULT;

	switch (cmd) {
	case APIPE_IOC_WAKE_READ:
		apipe_dev->rdone = true;
		wake_up_interruptible(&apipe_dev->rwait);
		return 0;

	case APIPE_IOC_WAKE_WRITE:
		apipe_dev->wdone = true;
		wake_up_interruptible(&apipe_dev->wwait);
		return 0;

	case APIPE_IOC_SET_BUFSZ:
		if (get_user(value, (unsigned int __user *)uarg))
			return -EFAULT;

		size = roundup_pow_of_two(value);

		mutex_lock(&apipe_dev->mutex);

		if (size - 1 > APIPE_MAX_BUFSZ - 1) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		new_buf = vmalloc(size);
		if (!new_buf) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		if (apipe_dev->buf)
			vfree(apipe_dev->buf);

		ret = kfifo_init(&apipe_dev->kfifo, new_buf, size);
		if (ret < 0) {
			/*
			 * vendor bug kept as is: apipe_dev->buf is still the
			 * already freed old buffer, so this frees it twice.
			 */
			vfree(apipe_dev->buf);
			apipe_dev->buf = NULL;
			mutex_unlock(&apipe_dev->mutex);
			dev_err(apipe_dev->dev, "failed: %d\n", ret);
			return ret;
		}

		apipe_dev->buf = new_buf;
		apipe_dev->buf_size = size;
		mutex_unlock(&apipe_dev->mutex);
		return 0;

	case APIPE_IOC_GET_BUFSZ:
		if (put_user(apipe_dev->buf_size, (unsigned int __user *)uarg))
			return -EFAULT;
		return 0;

	case APIPE_IOC_GET_LEN:
		if (put_user((unsigned int)kfifo_len(&apipe_dev->kfifo),
			     (unsigned int __user *)uarg))
			return -EFAULT;
		return 0;

	case APIPE_IOC_GET_MAGIC:
		/* vendor constant, the binary stores 0xffff unconditionally */
		if (put_user(APIPE_MAGIC_VALUE, (unsigned int __user *)uarg))
			return -EFAULT;
		return 0;

	case APIPE_IOC_LOCK:
		mutex_lock(&apipe_dev->w_mutex);
		return 0;

	case APIPE_IOC_UNLOCK:
		mutex_unlock(&apipe_dev->w_mutex);
		return 0;

	case APIPE_IOC_POINT_ALLOC:
		if (get_user(value, (unsigned int __user *)uarg))
			return -EFAULT;

		mutex_lock(&apipe_dev->mutex);

		if (apipe_dev->points) {
			vfree(apipe_dev->points);
			apipe_dev->points = NULL;
		}

		if (value - 1 > APIPE_MAX_POINTS - 1) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		apipe_dev->points = vmalloc(value * sizeof(struct apipe_point));
		if (!apipe_dev->points) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		apipe_dev->point_count = value;
		mutex_unlock(&apipe_dev->mutex);

		dev_info(apipe_dev->dev, "POINT_ALLOC count:%d\n", value);
		return 0;

	case APIPE_IOC_POINT_GET:
		if (copy_from_user(&point, uarg, sizeof(point)))
			return -EFAULT;

		mutex_lock(&apipe_dev->mutex);
		if (!apipe_dev->points) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		if (point.index >= apipe_dev->point_count) {
			mutex_unlock(&apipe_dev->mutex);
			dev_err(apipe_dev->dev,
				"POINTINFO_GET: error:point index:%d\n",
				point.index);
			return -EINVAL;
		}

		ret = copy_to_user(uarg, &apipe_dev->points[point.index],
				   sizeof(point));
		mutex_unlock(&apipe_dev->mutex);
		if (ret)
			return -EFAULT;
		return 0;

	case APIPE_IOC_POINT_SET:
		if (copy_from_user(&point, uarg, sizeof(point)))
			return -EFAULT;

		mutex_lock(&apipe_dev->mutex);
		if (!apipe_dev->points) {
			mutex_unlock(&apipe_dev->mutex);
			return -ENOMEM;
		}

		if (point.index >= apipe_dev->point_count) {
			mutex_unlock(&apipe_dev->mutex);
			dev_err(apipe_dev->dev,
				"POINTINFO_SET: error:point index:%d\n",
				point.index);
			return -EINVAL;
		}

		apipe_dev->points[point.index] = point;
		mutex_unlock(&apipe_dev->mutex);
		return 0;

	default:
		dev_err(apipe_dev->dev, "default command\n");
		return -EINVAL;
	}
}

static const struct file_operations apipe_fops = {
	.owner		= THIS_MODULE,
	.read		= apipe_read,
	.write		= apipe_write,
	.poll		= apipe_poll,
	.unlocked_ioctl	= apipe_ioctl,
	.compat_ioctl	= apipe_ioctl,
	.open		= apipe_open,
	.release	= apipe_release,
};

static int apipe_probe(struct platform_device *pdev)
{
	struct apipe_dev *apipe_dev;
	const char *name;
	size_t len;
	int ret;

	apipe_dev = devm_kmalloc(&pdev->dev, sizeof(*apipe_dev), GFP_KERNEL);
	if (!apipe_dev)
		return -ENOMEM;

	name = pdev->name ? pdev->name : dev_name(&pdev->dev);
	len = strlen(name);
	if (len >= APIPE_MAX_NAME) {
		dev_err(&pdev->dev, "apipe: name is too long:%d\n", (int)len);
		dev_err(&pdev->dev, "parse dt failed\n");
		return -EFAULT;
	}

	strncpy(apipe_dev->name, name, len);
	dev_info(&pdev->dev, "name=%s\n", apipe_dev->name);

	apipe_dev->misc.minor = MISC_DYNAMIC_MINOR;
	apipe_dev->misc.name = apipe_dev->name;
	apipe_dev->misc.fops = &apipe_fops;
	apipe_dev->misc.parent = NULL;
	apipe_dev->dev = &pdev->dev;

	ret = misc_register(&apipe_dev->misc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to alloc apipe chrdev\n");
		return ret;
	}

	mutex_init(&apipe_dev->mutex);
	mutex_init(&apipe_dev->w_mutex);

	platform_set_drvdata(pdev, apipe_dev);
	return 0;
}

static int apipe_remove(struct platform_device *pdev)
{
	struct apipe_dev *apipe_dev = platform_get_drvdata(pdev);

	if (apipe_dev)
		misc_deregister(&apipe_dev->misc);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id apipe_match_table[] = {
	{ .compatible = "unisoc,apipe" },
	{ }
};
MODULE_DEVICE_TABLE(of, apipe_match_table);

static struct platform_driver apipe_driver = {
	.probe = apipe_probe,
	.remove = apipe_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = APIPE_DRV_NAME,
		.of_match_table = apipe_match_table,
	},
};

static int __init apipe_init(void)
{
	return platform_driver_register(&apipe_driver);
}

static void __exit apipe_exit(void)
{
	platform_driver_unregister(&apipe_driver);
}

module_init(apipe_init);
module_exit(apipe_exit);

MODULE_DESCRIPTION("apipe driver");
MODULE_AUTHOR("SPRD");
MODULE_LICENSE("GPL v2");
