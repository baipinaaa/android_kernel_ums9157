// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm character device.
 *
 * The test tools open /dev/synaptics_tcm/tcm0 and speak the raw TouchComm
 * protocol through it: a write carries one command (first byte) plus its
 * payload, a read hands back the response, or a whole touch report when the
 * touch module asked for one.  Four ioctls let the caller nudge the link
 * (reset, interrupt enable) and switch between command mode and report mode.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#include "synaptics_tcm_core.h"

/* the command that carries a touch report config written by the client */
#define CMD_CAPTURE_TOUCH_REPORT_CONFIG		0x26

/* id the core logs this module with */
#define DEVICE_MODULE_ID			0x02

/* ioctls the tools use, all _IOW('s', nr, int) */
#define SYNA_TCM_IOCTL_RESET			0x40047300
#define SYNA_TCM_IOCTL_ENABLE_IRQ		0x40047301
#define SYNA_TCM_IOCTL_SET_TOUCH_REPORT		0x40047302
#define SYNA_TCM_IOCTL_SET_REPORT_TOUCH		0x40047303

struct syna_tcm_dev_hcd {
	dev_t dev;				/* +0x000 */
	u8 touch_report;			/* +0x004 read whole reports */
	u8 report_touch;			/* +0x005 feed the touch module */
	u8 pad0[0x02];				/* +0x006 */
	u32 open_count;				/* +0x008 */
	u8 pad0b[0x04];				/* +0x00c */
	struct cdev char_dev;			/* +0x010 */
	u8 pad1[0x20];				/* +0x078 rest of struct cdev */
	struct class *class;			/* +0x098 */
	struct device *device;			/* +0x0a0 */
	struct syna_tcm_buf out;		/* +0x0a8 */
	struct syna_tcm_buf resp;		/* +0x0e0 */
	struct syna_tcm_buf report;		/* +0x118 */
	struct syna_tcm_hcd *tcm_hcd;		/* +0x150 */
};

static struct syna_tcm_dev_hcd *device_hcd;

static struct completion device_remove_complete;

static int rmidev_major_num;

/*
 * State of the touch report reassembly: a report may arrive split over
 * several response packets, so the length announced in the first one is kept
 * until the buffer is full.
 */
static struct {
	u32 remaining_size;			/* +0x00 */
	u8 report;				/* +0x04 */
	u8 pad0[0x03];				/* +0x05 */
	u32 offset;				/* +0x08 */
} device_capture_touch_report;

/* the vendor inlines its own copy of both helpers in every translation unit */
static int secure_memcpy(unsigned char *dest, unsigned int dest_size,
			 const unsigned char *src, unsigned int src_size)
{
	if (dest_size < src_size) {
		pr_err("%s: src_size = %d, dest_size = %d, count = %d\n",
		       __func__, src_size, dest_size, src_size);
		return -EINVAL;
	}

	memcpy(dest, src, src_size);

	return 0;
}

static int syna_tcm_alloc_mem(struct syna_tcm_hcd *tcm_hcd,
			      struct syna_tcm_buf *buf, unsigned int size)
{
	if (buf->buf_size >= size) {
		memset(buf->buf, 0x00, buf->buf_size);
		return 0;
	}

	kfree(buf->buf);

	buf->buf = kmalloc(size, GFP_KERNEL);
	if (!buf->buf) {
		dev_err(&tcm_hcd->pdev->dev, "%s: Failed to allocate memory\n",
			__func__);
		dev_err(&tcm_hcd->pdev->dev, "%s: Allocation size = %d\n",
			__func__, size);
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;

	memset(buf->buf, 0x00, buf->buf_size);

	return 0;
}

/*
 * Every node lives in its own directory named after the module, and the
 * tools get to choose the permission bits themselves.
 */
static char *device_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;

	*mode = 0666;

	return kasprintf(GFP_KERNEL, "%s/%s", "synaptics_tcm", dev_name(dev));
}

static int device_create_class(struct syna_tcm_hcd *tcm_hcd)
{
	if (!device_hcd->class) {
		device_hcd->class = class_create(THIS_MODULE, "synaptics_tcm");
		if (IS_ERR(device_hcd->class)) {
			LOGE("%s error: (line %d) Failed to create class\n",
			     __func__, 0x204);
			return -ENODEV;
		}

		device_hcd->class->devnode = device_devnode;
	}

	return 0;
}

/*
 * Copy the touch report config the client wrote into the buffer the touch
 * module reads, either behind the framing the tools use or raw.
 */
static int device_capture_touch_report_config(struct syna_tcm_hcd *tcm_hcd,
					      unsigned char *buf,
					      unsigned int count)
{
	unsigned int length;
	unsigned int offset;
	unsigned int size;
	int retval;

	if (device_hcd->touch_report) {
		if (count <= 2) {
			LOGE("%s error: (line %d) Invalid write data\n",
			     __func__, 0xb7);
			return -EINVAL;
		}

		length = get_unaligned_le16(buf + 1);

		if (count - 3 < length) {
			LOGE("%s error: (line %d) Incomplete write data\n",
			     __func__, 0xbf);
			return -EINVAL;
		}

		offset = 3;
	} else {
		length = count - 1;
		offset = 1;
	}

	if (!length)
		return 0;

	size = device_hcd->out.buf_size;

	mutex_lock(&tcm_hcd->config.buf_mutex);

	if (tcm_hcd->config.buf_size < length) {
		kfree(tcm_hcd->config.buf);

		tcm_hcd->config.buf = kmalloc(length, GFP_KERNEL);
		if (!tcm_hcd->config.buf) {
			LOGE("%s error: (line %d) Failed to allocate memory for tcm_hcd->config.buf\n",
			     __func__, 0xd9);
			tcm_hcd->config.buf_size = 0;
			mutex_unlock(&tcm_hcd->config.buf_mutex);
			return -ENOMEM;
		}

		tcm_hcd->config.buf_size = length;
	}

	memset(tcm_hcd->config.buf, 0x00, tcm_hcd->config.buf_size);

	tcm_hcd->config.data_length = 0;

	retval = secure_memcpy(tcm_hcd->config.buf, size - offset,
			       buf + offset, length);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to copy touch report config data\n",
		     __func__, 0xe5);
		goto exit;
	}

	tcm_hcd->config.data_length = length;

	retval = 0;

exit:
	mutex_unlock(&tcm_hcd->config.buf_mutex);

	return retval;
}

/*
 * Freeze a touch report the firmware sent in response to the read side of
 * the device, so the touch module can be handed the whole thing at once.
 * A report may arrive split over several responses, hence the carried state.
 */
static void device_capture_touch_report(struct syna_tcm_hcd *tcm_hcd,
					unsigned char *buf, unsigned int count)
{
	unsigned int offset;
	unsigned int size;
	unsigned int length;
	int retval;

	offset = 0;

	if (buf[1] == MESSAGE_HEADER_SIZE) {
		if (device_capture_touch_report.report != 1)
			return;

		offset = 2;
	} else if (buf[1] == REPORT_TOUCH) {
		if (count < 4) {
			device_capture_touch_report.report = 0;
			return;
		}

		length = get_unaligned_le16(&buf[2]);

		device_capture_touch_report.remaining_size = length;

		if (device_hcd->report.buf_size < length) {
			kfree(device_hcd->report.buf);

			device_hcd->report.buf = kmalloc(length, GFP_KERNEL);
			if (!device_hcd->report.buf) {
				LOGE("%s error: (line %d) Failed to allocate memory for device_hcd->report.buf\n",
				     __func__, 0x6f);
				device_hcd->report.buf_size = 0;
				device_capture_touch_report.report = 0;
				return;
			}

			device_hcd->report.buf_size = length;

			size = length;
		} else {
			size = device_hcd->report.buf_size;
		}

		memset(device_hcd->report.buf, 0x00, size);

		device_hcd->report.data_length = 0;
		device_capture_touch_report.offset = 0;
		device_capture_touch_report.report = 1;

		offset = 4;
	} else {
		return;
	}

	if (count > offset) {
		size = device_capture_touch_report.remaining_size;
		if (count - offset < size)
			size = count - offset;

		retval = secure_memcpy(device_hcd->report.buf +
				       device_capture_touch_report.offset,
				       count - offset, buf + offset, size);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy touch report data\n",
			     __func__, 0x8d);
			device_capture_touch_report.report = 0;
			return;
		}

		device_capture_touch_report.offset += size;
		device_capture_touch_report.remaining_size -= size;
		device_hcd->report.data_length += size;
	}

	if (device_capture_touch_report.remaining_size)
		return;

	mutex_lock(&tcm_hcd->report_buffer.buf_mutex);

	tcm_hcd->report_buffer.buf = device_hcd->report.buf;
	tcm_hcd->report_buffer.buf_size = device_hcd->report.buf_size;
	tcm_hcd->report_buffer.data_length = device_hcd->report.data_length;

	tcm_hcd->touch_report_fn();

	mutex_unlock(&tcm_hcd->report_buffer.buf_mutex);

	device_capture_touch_report.report = 0;
}

static ssize_t device_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *f_pos)
{
	struct syna_tcm_hcd *tcm_hcd = device_hcd->tcm_hcd;
	unsigned int size;
	int retval;

	if (!count)
		return 0;

	mutex_lock(&tcm_hcd->extif_mutex);

	mutex_lock(&device_hcd->resp.buf_mutex);

	if (device_hcd->touch_report) {
		size = device_hcd->resp.buf_size;

		retval = syna_tcm_alloc_mem(tcm_hcd, &device_hcd->resp, count);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for device_hcd->resp.buf\n",
			     __func__, 0x13e);
			mutex_unlock(&device_hcd->resp.buf_mutex);
			retval = -ENOMEM;
			goto exit;
		}

		memset(device_hcd->resp.buf, 0x00, size);

		device_hcd->resp.data_length = 0;

		retval = (int)tcm_hcd->read_message_fn(tcm_hcd,
						       device_hcd->resp.buf,
						       count);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read message\n",
			     __func__, 0x148);
			mutex_unlock(&device_hcd->resp.buf_mutex);
			goto exit;
		}
	} else if (device_hcd->resp.data_length != count) {
		LOGE("%s error: (line %d) Invalid length information\n",
		     __func__, 0x14f);
		mutex_unlock(&device_hcd->resp.buf_mutex);
		retval = -EINVAL;
		goto exit;
	}

	if (copy_to_user(buf, device_hcd->resp.buf, count)) {
		LOGE("%s error: (line %d) Failed to copy data to user space\n",
		     __func__, 0x158);
		mutex_unlock(&device_hcd->resp.buf_mutex);
		retval = -EINVAL;
		goto exit;
	}

	if (device_hcd->report_touch) {
		if (!tcm_hcd->touch_report_fn) {
			LOGE("%s error: (line %d) Unable to report touch\n",
			     __func__, 0x163);
			device_hcd->report_touch = 0;
		}

		if (device_hcd->report_touch && device_hcd->touch_report &&
		    count >= 2 && device_hcd->resp.buf[0] == MESSAGE_MARKER) {
			mutex_lock(&device_hcd->report.buf_mutex);

			device_capture_touch_report(tcm_hcd, device_hcd->resp.buf,
						    count);

			mutex_unlock(&device_hcd->report.buf_mutex);
		}
	}

	mutex_unlock(&device_hcd->resp.buf_mutex);

	retval = count;

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}

static ssize_t device_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	struct syna_tcm_hcd *tcm_hcd = device_hcd->tcm_hcd;
	unsigned int size;
	int retval;

	if (!count)
		return 0;

	mutex_lock(&tcm_hcd->extif_mutex);

	mutex_lock(&device_hcd->out.buf_mutex);

	size = (count == 1) ? 2 : count;

	if (device_hcd->out.buf_size < size) {
		kfree(device_hcd->out.buf);

		device_hcd->out.buf = kmalloc(size, GFP_KERNEL);
		if (!device_hcd->out.buf) {
			LOGE("%s error: (line %d) Failed to allocate memory for device_hcd->out.buf\n",
			     __func__, 0x187);
			mutex_unlock(&device_hcd->out.buf_mutex);
			retval = -ENOMEM;
			goto exit;
		}

		device_hcd->out.buf_size = size;
	}

	memset(device_hcd->out.buf, 0x00, size);

	device_hcd->out.data_length = 0;

	if (copy_from_user(device_hcd->out.buf, buf, count)) {
		LOGE("%s error: (line %d) Failed to copy data from user space\n",
		     __func__, 0x18e);
		mutex_unlock(&device_hcd->out.buf_mutex);
		retval = -EINVAL;
		goto exit;
	}

	mutex_lock(&device_hcd->resp.buf_mutex);

	if (device_hcd->touch_report) {
		retval = (int)tcm_hcd->write_message_fn(tcm_hcd,
							device_hcd->out.buf[0],
							device_hcd->out.buf + 1,
							count - 1, NULL, NULL,
							NULL, NULL, 0);
	} else {
		mutex_lock(&tcm_hcd->rw_ctrl_mutex);

		retval = (int)tcm_hcd->write_message_fn(tcm_hcd,
							device_hcd->out.buf[0],
							device_hcd->out.buf + 1,
							count - 1,
							&device_hcd->resp.buf,
							&device_hcd->resp.buf_size,
							&device_hcd->resp.data_length,
							NULL, 0);

		mutex_unlock(&tcm_hcd->rw_ctrl_mutex);
	}

	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write command 0x%02x\n",
		     __func__, 0x1b0, device_hcd->out.buf[0]);
		mutex_unlock(&device_hcd->resp.buf_mutex);
		mutex_unlock(&device_hcd->out.buf_mutex);
		goto exit;
	}

	if (device_hcd->out.buf[0] == CMD_CAPTURE_TOUCH_REPORT_CONFIG) {
		retval = device_capture_touch_report_config(tcm_hcd,
							    device_hcd->out.buf,
							    count);
		if (retval < 0)
			LOGE("%s error: (line %d) Failed to capture touch report config\n",
			     __func__, 0x1ba);
	}

	mutex_unlock(&device_hcd->out.buf_mutex);

	if (!device_hcd->touch_report)
		count = device_hcd->resp.data_length;

	mutex_unlock(&device_hcd->resp.buf_mutex);

	retval = count;

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}

static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct syna_tcm_hcd *tcm_hcd = device_hcd->tcm_hcd;
	int retval;

	if (!device_hcd)
		return -ENODEV;

	mutex_lock(&tcm_hcd->extif_mutex);

	switch (cmd) {
	case SYNA_TCM_IOCTL_RESET:
		retval = (int)tcm_hcd->reset_and_reinit_fn(tcm_hcd, false, true);
		break;
	case SYNA_TCM_IOCTL_ENABLE_IRQ:
		if (arg == 0)
			retval = (int)tcm_hcd->enable_irq_fn(tcm_hcd, false, false);
		else if (arg == 1)
			retval = (int)tcm_hcd->enable_irq_fn(tcm_hcd, true, false);
		else
			retval = 0;
		break;
	case SYNA_TCM_IOCTL_SET_TOUCH_REPORT:
		retval = 0;
		if (arg == 0)
			device_hcd->touch_report = 0;
		else if (arg == 1)
			device_hcd->touch_report = 1;
		break;
	case SYNA_TCM_IOCTL_SET_REPORT_TOUCH:
		retval = 0;
		if (arg == 0)
			device_hcd->report_touch = 0;
		else if (arg == 1)
			device_hcd->report_touch = 1;
		break;
	default:
		retval = -ENOTTY;
		break;
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}

static int device_open(struct inode *inode, struct file *filp)
{
	struct syna_tcm_hcd *tcm_hcd = device_hcd->tcm_hcd;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (device_hcd->open_count) {
		retval = -EBUSY;
	} else {
		retval = 0;
		device_hcd->open_count = 1;
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}

static int device_release(struct inode *inode, struct file *filp)
{
	struct syna_tcm_hcd *tcm_hcd = device_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (device_hcd->open_count)
		device_hcd->open_count--;

	mutex_unlock(&tcm_hcd->extif_mutex);

	return 0;
}

static loff_t device_llseek(struct file *filp, loff_t off, int whence)
{
	return -EINVAL;
}

static const struct file_operations device_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.llseek = device_llseek,
	.read = device_read,
	.write = device_write,
	.unlocked_ioctl = device_ioctl,
};

static int device_init(struct syna_tcm_hcd *tcm_hcd)
{
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	unsigned int minor;
	dev_t dev;
	int retval;

	device_hcd = kzalloc(sizeof(*device_hcd), GFP_KERNEL);
	if (!device_hcd) {
		LOGE("%s error: (line %d) Failed to allocate memory for device_hcd\n",
		     __func__, 0x227);
		return -ENOMEM;
	}

	device_hcd->tcm_hcd = tcm_hcd;
	device_hcd->report_touch = 1;

	mutex_init(&device_hcd->out.buf_mutex);
	device_hcd->out.init = 0;

	mutex_init(&device_hcd->resp.buf_mutex);
	device_hcd->resp.init = 0;

	mutex_init(&device_hcd->report.buf_mutex);
	device_hcd->report.init = 0;

	if (rmidev_major_num) {
		dev = MKDEV(rmidev_major_num, 0);

		retval = register_chrdev_region(dev, 1, "synaptics_tcm");
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to register char device\n",
			     __func__, 0x239);
			goto err_free_hcd;
		}
	} else {
		retval = alloc_chrdev_region(&dev, 0, 1, "synaptics_tcm");
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate char device\n",
			     __func__, 0x241);
			goto err_free_hcd;
		}

		rmidev_major_num = MAJOR(dev);
	}

	cdev_init(&device_hcd->char_dev, &device_fops);

	device_hcd->dev = dev;

	retval = cdev_add(&device_hcd->char_dev, dev, 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to add char device\n",
		     __func__, 0x24f);
		goto err_unregister_region;
	}

	retval = device_create_class(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create class\n",
		     __func__, 0x256);
		goto err_del_cdev;
	}

	minor = MINOR(dev);

	device_hcd->device = device_create(device_hcd->class, NULL, dev, NULL,
					   "tcm%d", minor);
	if (IS_ERR(device_hcd->device)) {
		LOGE("%s error: (line %d) Failed to create device\n",
		     __func__, 0x25f);
		class_destroy(device_hcd->class);
		goto err_del_cdev;
	}

	/*
	 * The line the panel raises its interrupt on is resolved here; the
	 * vendor has the failure message unconditionally after the lookup.
	 */
	if (bdata->irq_gpio >= 0) {
		gpio_to_desc(bdata->irq_gpio);

		LOGE("%s error: (line %d) Failed to export GPIO\n",
		     __func__, 0x268);
	}

	return 0;

err_del_cdev:
	cdev_del(&device_hcd->char_dev);

	retval = -ENODEV;

err_unregister_region:
	unregister_chrdev_region(dev, 1);
	goto err_free_hcd;

err_free_hcd:
	if (!device_hcd->report.init) {
		kfree(device_hcd->report.buf);
		device_hcd->report.buf_size = 0;
	}

	if (!device_hcd->resp.init) {
		kfree(device_hcd->resp.buf);
		device_hcd->resp.buf_size = 0;
	}

	if (!device_hcd->out.init) {
		kfree(device_hcd->out.buf);
		device_hcd->out.buf_size = 0;
	}

	kfree(device_hcd);
	device_hcd = NULL;

	return retval;
}

static int device_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (device_hcd) {
		device_destroy(device_hcd->class, device_hcd->dev);
		class_destroy(device_hcd->class);
		cdev_del(&device_hcd->char_dev);
		unregister_chrdev_region(device_hcd->dev, 1);

		if (!device_hcd->report.init) {
			kfree(device_hcd->report.buf);
			device_hcd->report.buf_size = 0;
		}

		if (!device_hcd->resp.init) {
			kfree(device_hcd->resp.buf);
			device_hcd->resp.buf_size = 0;
		}

		if (!device_hcd->out.init) {
			kfree(device_hcd->out.buf);
			device_hcd->out.buf_size = 0;
		}

		kfree(device_hcd);
		device_hcd = NULL;
	}

	complete(&device_remove_complete);

	return 0;
}

static int device_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (device_hcd)
		return 0;

	return device_init(tcm_hcd);
}

static struct syna_tcm_module_desc device_module = {
	.id = DEVICE_MODULE_ID,
	.init = device_init,
	.remove = device_remove,
	.reinit = device_reinit,
};

int device_module_init(void)
{
	return syna_tcm_add_module(&device_module, true);
}

void device_module_exit(void)
{
	syna_tcm_add_module(&device_module, false);

	wait_for_completion(&device_remove_complete);
}
