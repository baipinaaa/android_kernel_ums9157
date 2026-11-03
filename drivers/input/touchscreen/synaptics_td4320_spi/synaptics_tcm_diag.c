// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm diagnostics module.
 *
 * The client picks a report code through "type" and the module mirrors every
 * report carrying that code into a ping/pong pair of buffers, so that a reader
 * can always work on the frame the next report is not overwriting yet.  "size"
 * and the "data" binary file expose that frozen frame, and the task named
 * through "pid" is signalled with SIGIO once a new one is complete.
 */

#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#include "synaptics_tcm_core.h"

/* slot the vendor reserved for this module in the module pool */
#define DIAG_MODULE_ID		0x05

/*
 * The payload handed to send_sig_info(); only si_signo is filled in and the
 * rest of the 0x30 bytes is left zeroed.
 */
struct syna_tcm_diag_siginfo {
	int si_signo;				/* +0x00 */
	int si_errno;				/* +0x04 */
	int si_code;				/* +0x08 */
	u8 pad0[0x24];				/* +0x0c */
};

struct syna_tcm_diag_hcd {
	u32 pid;				/* +0x000 task to signal */
	u8 report_code;				/* +0x004 */
	u8 pad0[0x03];				/* +0x005 */
	u32 frame_index;			/* +0x008 1 means pong is frozen */
	u8 pad0b[0x04];				/* +0x00c */
	struct kobject *kobj;			/* +0x010 */
	struct syna_tcm_diag_siginfo siginfo;	/* +0x018 */
	struct task_struct *task;		/* +0x048 */
	struct syna_tcm_buf ping;		/* +0x050 */
	struct syna_tcm_buf pong;		/* +0x088 */
	struct syna_tcm_hcd *tcm_hcd;		/* +0x0c0 */
};

static struct syna_tcm_diag_hcd *diag_hcd;

static struct completion diag_remove_complete;

/* which half of the pair the next report is allowed to overwrite */
static struct {
	u8 state;
} diag_report_state;

/*
 * Copy that refuses to overrun the destination; the vendor inlines its own
 * copy into every translation unit, so this file carries one as well.
 */
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

/*
 * Make sure a report buffer can hold size bytes, reusing whatever was
 * allocated before.  The caller holds the buffer mutex.
 */
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
		LOGE("%s: Failed to allocate memory\n", __func__);
		LOGE("%s: Allocation size = %d\n", __func__, size);
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;

	memset(buf->buf, 0x00, buf->buf_size);

	return 0;
}

/*
 * The pair is only ever released together, and the vendor guards each half
 * with the (always false) init flag before handing the memory back.
 */
static void diag_remove_buffers(struct syna_tcm_diag_hcd *diag)
{
	if (!diag->pong.init) {
		kfree(diag->pong.buf);
		diag->pong.buf_size = 0;
	}

	if (!diag->ping.init) {
		kfree(diag->ping.buf);
		diag->ping.buf_size = 0;
	}
}

static ssize_t syna_tcm_show_error(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	pr_err("%s: Attribute not readable\n", __func__);

	return -1;
}

static ssize_t syna_tcm_store_error(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	pr_err("%s: Attribute not writable\n", __func__);

	return -1;
}

/*
 * The four geometry readers share the layout information the sensor reported
 * in its application info block; outside application mode it is unknown.
 */
static ssize_t diag_sysfs_buttons_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		count = -ENODEV;
		goto exit;
	}

	count = snprintf(buf, PAGE_SIZE, "%u\n",
			 get_unaligned_le16(&tcm_hcd->app_info[0x26]));

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t diag_sysfs_hybrid_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		count = -ENODEV;
		goto exit;
	}

	count = snprintf(buf, PAGE_SIZE, "%u\n",
			 get_unaligned_le16(&tcm_hcd->app_info[0x2c]));

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t diag_sysfs_cols_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		count = -ENODEV;
		goto exit;
	}

	count = snprintf(buf, PAGE_SIZE, "%u\n",
			 get_unaligned_le16(&tcm_hcd->app_info[0x2a]));

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t diag_sysfs_rows_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status) {
		count = -ENODEV;
		goto exit;
	}

	count = snprintf(buf, PAGE_SIZE, "%u\n",
			 get_unaligned_le16(&tcm_hcd->app_info[0x28]));

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/* "size" reports how many bytes the frozen frame holds */
static ssize_t diag_sysfs_size_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (diag_hcd->frame_index) {
		mutex_lock(&diag_hcd->pong.buf_mutex);
		count = snprintf(buf, PAGE_SIZE, "%u\n",
				 diag_hcd->pong.data_length);
		mutex_unlock(&diag_hcd->pong.buf_mutex);
	} else {
		mutex_lock(&diag_hcd->ping.buf_mutex);
		count = snprintf(buf, PAGE_SIZE, "%u\n",
				 diag_hcd->ping.data_length);
		mutex_unlock(&diag_hcd->ping.buf_mutex);
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/* "type" selects which report code the module mirrors */
static ssize_t diag_sysfs_type_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	unsigned int input;
	int retval;

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	mutex_lock(&tcm_hcd->extif_mutex);

	diag_hcd->report_code = input;

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/* "pid" names the task that gets signalled for every frozen frame */
static ssize_t diag_sysfs_pid_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	unsigned int input;
	int retval;

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	mutex_lock(&tcm_hcd->extif_mutex);

	diag_hcd->pid = input;

	if (input) {
		diag_hcd->task = pid_task(find_vpid(input), PIDTYPE_PID);
		if (!diag_hcd->task) {
			LOGE("%s error: (line %d) Failed to locate task\n",
			     __func__, 0x72);
			mutex_unlock(&tcm_hcd->extif_mutex);
			return -EINVAL;
		}
	}

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/* "data" hands out the frozen frame starting at the requested offset */
static ssize_t diag_sysfs_data_show(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
				    loff_t offset, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = diag_hcd->tcm_hcd;
	unsigned int size;
	int retval = 0;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (diag_hcd->frame_index) {
		mutex_lock(&diag_hcd->pong.buf_mutex);

		if (!diag_hcd->pong.data_length) {
			count = 0;
			goto exit;
		}

		size = min_t(unsigned int, diag_hcd->pong.data_length - offset,
			     count);

		retval = -EINVAL;

		if (buf && diag_hcd->pong.buf + offset &&
		    size <= count && size <= diag_hcd->pong.buf_size - offset) {
			memcpy(buf, diag_hcd->pong.buf + offset, size);
			retval = 0;
		}

		mutex_unlock(&diag_hcd->pong.buf_mutex);

		count = size;
	} else {
		mutex_lock(&diag_hcd->ping.buf_mutex);

		if (!diag_hcd->ping.data_length) {
			count = 0;
			goto exit;
		}

		size = min_t(unsigned int, diag_hcd->ping.data_length - offset,
			     count);

		retval = -EINVAL;

		if (buf && diag_hcd->ping.buf + offset &&
		    size <= count && size <= diag_hcd->ping.buf_size - offset) {
			memcpy(buf, diag_hcd->ping.buf + offset, size);
			retval = 0;
		}

		mutex_unlock(&diag_hcd->ping.buf_mutex);

		count = size;
	}

	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to copy report data\n",
		     __func__, 0x154);
		count = retval;
	}

exit:
	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static struct bin_attribute diag_data_bin_attr = {
	.attr = {
		.name = "data",
		.mode = 0444,
	},
	.read = diag_sysfs_data_show,
};

static DEVICE_ATTR(pid, 0220, syna_tcm_show_error, diag_sysfs_pid_store);
static DEVICE_ATTR(size, 0444, diag_sysfs_size_show, syna_tcm_store_error);
static DEVICE_ATTR(type, 0220, syna_tcm_show_error, diag_sysfs_type_store);
static DEVICE_ATTR(rows, 0444, diag_sysfs_rows_show, syna_tcm_store_error);
static DEVICE_ATTR(cols, 0444, diag_sysfs_cols_show, syna_tcm_store_error);
static DEVICE_ATTR(hybrid, 0444, diag_sysfs_hybrid_show, syna_tcm_store_error);
static DEVICE_ATTR(buttons, 0444, diag_sysfs_buttons_show,
		   syna_tcm_store_error);

static struct attribute *diag_attrs[] = {
	&dev_attr_pid.attr,
	&dev_attr_size.attr,
	&dev_attr_type.attr,
	&dev_attr_rows.attr,
	&dev_attr_cols.attr,
	&dev_attr_hybrid.attr,
	&dev_attr_buttons.attr,
	NULL,
};

/*
 * Freeze the report that just arrived into the half nobody is reading and
 * wake the client.  The report code has already been matched by the caller.
 */
static int diag_report(struct syna_tcm_hcd *tcm_hcd)
{
	struct syna_tcm_diag_hcd *diag = diag_hcd;
	u8 state;
	int retval;

	state = diag_report_state.state;

	if (state & 0x01) {
		mutex_lock(&diag->pong.buf_mutex);

		retval = syna_tcm_alloc_mem(tcm_hcd, &diag->pong,
					    tcm_hcd->report_buffer.data_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for diag_hcd->pong.buf\n",
			     __func__, 0x18b);
			mutex_unlock(&diag->pong.buf_mutex);
			return 0;
		}

		diag->pong.data_length = 0;

		retval = secure_memcpy(diag->pong.buf, diag->pong.buf_size,
				       tcm_hcd->report_buffer.buf,
				       tcm_hcd->report_buffer.data_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy report data\n",
			     __func__, 0x197);
			mutex_unlock(&diag->pong.buf_mutex);
			return 0;
		}

		diag->pong.data_length = tcm_hcd->report_buffer.data_length;

		mutex_unlock(&diag->pong.buf_mutex);
	} else {
		mutex_lock(&diag->ping.buf_mutex);

		retval = syna_tcm_alloc_mem(tcm_hcd, &diag->ping,
					    tcm_hcd->report_buffer.data_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to allocate memory for diag_hcd->ping.buf\n",
			     __func__, 0x16c);
			mutex_unlock(&diag->ping.buf_mutex);
			return 0;
		}

		diag->ping.data_length = 0;

		retval = secure_memcpy(diag->ping.buf, diag->ping.buf_size,
				       tcm_hcd->report_buffer.buf,
				       tcm_hcd->report_buffer.data_length);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to copy report data\n",
			     __func__, 0x178);
			mutex_unlock(&diag->ping.buf_mutex);
			return 0;
		}

		diag->ping.data_length = tcm_hcd->report_buffer.data_length;

		mutex_unlock(&diag->ping.buf_mutex);
	}

	diag->frame_index = state;
	diag_report_state.state = state ^ 0x01;

	if (diag->pid)
		send_sig_info(SIGIO,
			      (struct kernel_siginfo *)&diag->siginfo,
			      diag->task);

	return 0;
}

/*
 * Handler registered with the core: only reports whose code the client asked
 * for are interesting, everything else belongs to the other modules.
 */
static int diag_syncbox(struct syna_tcm_hcd *tcm_hcd)
{
	if (!diag_hcd)
		return 0;

	if (tcm_hcd->dispatch_report_code != diag_hcd->report_code)
		return 0;

	diag_report(tcm_hcd);

	return 0;
}

static int diag_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	int index;
	int count;

	diag_hcd = kzalloc(sizeof(*diag_hcd), GFP_KERNEL);
	if (!diag_hcd) {
		LOGE("%s error: (line %d) Failed to allocate memory for diag_hcd\n",
		     __func__, 0x1b2);
		return -ENOMEM;
	}

	diag_hcd->tcm_hcd = tcm_hcd;
	diag_hcd->frame_index = 0;

	mutex_init(&diag_hcd->ping.buf_mutex);
	diag_hcd->ping.init = 0;

	mutex_init(&diag_hcd->pong.buf_mutex);
	diag_hcd->pong.init = 0;

	memset(&diag_hcd->siginfo, 0x00, sizeof(diag_hcd->siginfo));

	diag_hcd->siginfo.si_signo = SIGIO;
	diag_hcd->siginfo.si_code = 0;

	diag_hcd->kobj = kobject_create_and_add("diagnostics",
						tcm_hcd->sysfs_root);
	if (!diag_hcd->kobj) {
		LOGE("%s error: (line %d) Failed to create sysfs directory\n",
		     __func__, 0x1c4);
		retval = -EINVAL;
		goto err_free_buffers;
	}

	/* index of the last attribute that made it into the directory */
	index = -1;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_pid.attr);
	if (retval < 0)
		goto err_create_file;

	index = 0;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_size.attr);
	if (retval < 0)
		goto err_create_file;

	index = 1;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_type.attr);
	if (retval < 0)
		goto err_create_file;

	index = 2;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_rows.attr);
	if (retval < 0)
		goto err_create_file;

	index = 3;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_cols.attr);
	if (retval < 0)
		goto err_create_file;

	index = 4;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_hybrid.attr);
	if (retval < 0)
		goto err_create_file;

	index = 5;

	retval = sysfs_create_file(diag_hcd->kobj, &dev_attr_buttons.attr);
	if (retval < 0)
		goto err_create_file;

	index = 6;

	retval = sysfs_create_bin_file(diag_hcd->kobj, &diag_data_bin_attr);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create sysfs bin file\n",
		     __func__, 0x1d6);
		goto err_create_file;
	}

	return 0;

err_create_file:
	LOGE("%s error: (line %d) Failed to create sysfs file\n",
	     __func__, 0x1ce);

	for (count = index; count >= 0; count--)
		sysfs_remove_file(diag_hcd->kobj, diag_attrs[count]);

	kobject_put(diag_hcd->kobj);

err_free_buffers:
	diag_remove_buffers(diag_hcd);
	kfree(diag_hcd);
	diag_hcd = NULL;

	return retval;
}

static int diag_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (diag_hcd) {
		sysfs_remove_bin_file(diag_hcd->kobj, &diag_data_bin_attr);

		sysfs_remove_file(diag_hcd->kobj, &dev_attr_pid.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_size.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_type.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_rows.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_cols.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_hybrid.attr);
		sysfs_remove_file(diag_hcd->kobj, &dev_attr_buttons.attr);

		kobject_put(diag_hcd->kobj);

		diag_remove_buffers(diag_hcd);

		kfree(diag_hcd);
		diag_hcd = NULL;
	}

	complete(&diag_remove_complete);

	return 0;
}

static int diag_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (diag_hcd)
		return 0;

	return diag_init(tcm_hcd);
}

static struct syna_tcm_module_desc diag_module = {
	.id = DIAG_MODULE_ID,
	.init = diag_init,
	.remove = diag_remove,
	.handler = diag_syncbox,
	.reinit = diag_reinit,
};

int diag_module_init(void)
{
	return syna_tcm_add_module(&diag_module, true);
}

void diag_module_exit(void)
{
	syna_tcm_add_module(&diag_module, false);

	wait_for_completion(&diag_remove_complete);
}
