// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm factory test module.
 *
 * The line brings the controller into its production test firmware, asks it
 * to run one test item and then reads whatever frames the firmware streams
 * back.  Every raw frame starts with the touch report configuration header
 * (eight bytes copied straight out of app_info) followed by the item data, so
 * the checks below only have to know the panel geometry and the pass window
 * of the item at hand.
 *
 * Reading any of the "pt*" files runs the item synchronously: it enables the
 * report code, waits until the expected amount of frames arrived, disables it
 * again and then walks the collected data against a limits table.  The "data"
 * binary file exposes the frame the last item assembled.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/wait.h>

#include "synaptics_tcm_core.h"

/* slot the vendor reserved for this module in the module pool */
#define TESTING_MODULE_ID		0x03

/*
 * The production test firmware is driven with the report enable/disable
 * commands of the core plus the production test command below.
 */
#define CMD_PRODUCTION_TEST		0x2a

/* the limits tables are square: 38 rows of 38 columns of u16 */
#define TESTING_LIMITS_ROWS		0x26
#define TESTING_LIMITS_COLS		0x26

/* how long a single frame is allowed to take */
#define TESTING_FRAME_TIMEOUT_MS	5000

struct syna_tcm_testing_hcd {
	u8 result;				/* +0x000 0 = pass, 1 = fail */
	u8 report_code;				/* +0x001 code being collected */
	u8 pad0[0x02];				/* +0x002 */
	u32 report_index;			/* +0x004 frames collected so far */
	u32 report_size;			/* +0x008 frames the item sends */
	u8 pad1[0x04];				/* +0x00c */
	struct kobject *kobj;			/* +0x010 */
	struct syna_tcm_buf out;		/* +0x018 */
	struct syna_tcm_buf resp;		/* +0x050 */
	struct syna_tcm_buf report;		/* +0x088 */
	struct syna_tcm_buf process;		/* +0x0c0 */
	struct syna_tcm_buf output;		/* +0x0f8 frame handed to user space */
	struct syna_tcm_hcd *tcm_hcd;		/* +0x130 */
	/* int (*)(u8 report_code, unsigned int report_size) */
	void *collect_reports;			/* +0x138 */
};

static struct syna_tcm_testing_hcd *testing_hcd;

static struct completion testing_remove_complete;

/* signalled once every frame of the current item has been collected */
static struct completion report_complete;

/*
 * The vendor ships every limits table as a full 38x38 array even though all
 * of the entries carry the same value, so the tables are filled in one go
 * rather than spelling out 1444 identical numbers each.
 */
static const u16 pt5_hi_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 4000 },
};

static const u16 pt7_lo_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 300 },
};

static const u16 pt7_hi_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 1700 },
};

static const u16 pt10_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 28 },
};

static const u16 pt11_lo_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 0xfff8 },
};

static const u16 pt11_hi_limits[TESTING_LIMITS_ROWS][TESTING_LIMITS_COLS] = {
	[0 ... TESTING_LIMITS_ROWS - 1] = { [0 ... TESTING_LIMITS_COLS - 1] = 8 },
};

/* one byte of expected pin state for every byte the trx/trx test returns */
static const u8 pt1_limits[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

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
 * Make sure a buffer can hold size bytes, reusing whatever was allocated
 * before.  The diagnostic is part of the helper because the vendor copies it
 * with the code.
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

/* release whatever the five buffers still hold */
static void testing_remove_buffers(struct syna_tcm_testing_hcd *testing)
{
	if (!testing->output.init) {
		kfree(testing->output.buf);
		testing->output.buf_size = 0;
	}

	if (!testing->process.init) {
		kfree(testing->process.buf);
		testing->process.buf_size = 0;
	}

	if (!testing->report.init) {
		kfree(testing->report.buf);
		testing->report.buf_size = 0;
	}

	if (!testing->resp.init) {
		kfree(testing->resp.buf);
		testing->resp.buf_size = 0;
	}

	if (!testing->out.init) {
		kfree(testing->out.buf);
		testing->out.buf_size = 0;
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
 * Run one production test item and wait for the report code the module is
 * configured for.  On success the response buffer holds the raw item data.
 */
static int testing_run_prod_test_item(unsigned int test_item)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int size;
	int retval;

	if (!(tcm_hcd->feature_description[2] & 0x01)) {
		if ((tcm_hcd->identify_info[1] - 1) > 1 || tcm_hcd->status)
			goto err_identify_mode;
	} else {
		if (tcm_hcd->identify_info[1] == 0x0e)
			goto err_identify_mode;

		retval = tcm_hcd->switch_mode_fn(tcm_hcd,
						  MODE_LOAD_PRODUCTION_TEST);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to run production test firmware\n",
				__func__, 267);
			return retval;
		}
	}

	mutex_lock(&testing_hcd->out.buf_mutex);

	if (!testing_hcd->out.buf_size) {
		kfree(testing_hcd->out.buf);

		size = 1;

		testing_hcd->out.buf = kmalloc(size, GFP_KERNEL);
		if (!testing_hcd->out.buf) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Allocation size = %d\n", "syna_tcm_alloc_mem",
				size);
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for testing_hcd->out.buf\n",
				__func__, 285);
			mutex_unlock(&testing_hcd->out.buf_mutex);
			return -ENOMEM;
		}

		testing_hcd->out.buf_size = size;
	} else {
		size = testing_hcd->out.buf_size;
	}

	memset(testing_hcd->out.buf, 0x00, size);

	testing_hcd->out.data_length = 0;

	testing_hcd->out.buf[0] = test_item;

	mutex_lock(&testing_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, CMD_PRODUCTION_TEST,
					   testing_hcd->out.buf, 1,
					   &testing_hcd->resp.buf,
					   &testing_hcd->resp.buf_size,
					   &testing_hcd->resp.data_length,
					   NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 306, "CMD_PRODUCTION_TEST");
		mutex_unlock(&testing_hcd->resp.buf_mutex);
		mutex_unlock(&testing_hcd->out.buf_mutex);
		return retval;
	}

	mutex_unlock(&testing_hcd->resp.buf_mutex);

	mutex_unlock(&testing_hcd->out.buf_mutex);

	return 0;

err_identify_mode:
	dev_err(&tcm_hcd->pdev->dev,
		"%s error: (line %d) Identifying mode = 0x%02x\n",
		__func__, 274, tcm_hcd->identify_info[1]);

	return -ENODEV;
}

/*
 * Prefix the raw item data with the touch report configuration header so that
 * a reader can tell the frame apart from a touch report, and keep it in the
 * buffer the "data" file reads.
 */
static int testing_standard_frame_output(unsigned int include_header_extra)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int rows = get_unaligned_le16(&tcm_hcd->app_info[0x28]);
	unsigned int cols = get_unaligned_le16(&tcm_hcd->app_info[0x2a]);
	unsigned int buttons = get_unaligned_le16(&tcm_hcd->app_info[0x26]);
	unsigned int has_profile = get_unaligned_le16(&tcm_hcd->app_info[0x2c]);
	unsigned int size;
	unsigned int length;
	int retval;

	size = rows * cols;

	if (!include_header_extra)
		size += buttons + (has_profile == 0 ? 0 : rows + cols);

	size = (size << 1) + 8;

	mutex_lock(&testing_hcd->output.buf_mutex);

	if (testing_hcd->output.buf_size < size) {
		kfree(testing_hcd->output.buf);

		testing_hcd->output.buf = kmalloc(size, GFP_KERNEL);
		if (!testing_hcd->output.buf) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Allocation size = %d\n", "syna_tcm_alloc_mem",
				size);
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for testing_hcd->output.buf\n",
				__func__, 477);
			mutex_unlock(&testing_hcd->output.buf_mutex);
			return -ENOMEM;
		}

		testing_hcd->output.buf_size = size;
	} else {
		size = testing_hcd->output.buf_size;
	}

	memset(testing_hcd->output.buf, 0x00, size);

	testing_hcd->output.data_length = 0;

	if (secure_memcpy(testing_hcd->output.buf, size,
			  &tcm_hcd->app_info[0x26], 8)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy header data\n",
			__func__, 489);
		mutex_unlock(&testing_hcd->output.buf_mutex);
		return -EINVAL;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	if (secure_memcpy(testing_hcd->output.buf + 8, size - 8,
			  testing_hcd->resp.buf,
			  testing_hcd->resp.data_length)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy test data\n",
			__func__, 505);
		retval = -EINVAL;
	} else {
		length = testing_hcd->resp.data_length + 8;

		testing_hcd->output.data_length = length;

		retval = 0;
	}

	mutex_unlock(&testing_hcd->resp.buf_mutex);

	mutex_unlock(&testing_hcd->output.buf_mutex);

	return retval;
}

/*
 * pt01: every pin is reported as one bit, so the collected bytes are compared
 * with the expected pin pattern bit by bit.
 */
static int testing_pt01_trx_trx_short(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int i;
	unsigned int j;
	unsigned int pin;
	u8 value;
	u8 limit;
	u8 *response;
	unsigned int length;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	retval = testing_run_prod_test_item(1);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run test\n",
			__func__, 875);
		return -EIO;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	length = testing_hcd->resp.data_length;

	if (length >= 0x11) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Mismatching limits data\n",
			__func__, 886);
		retval = -EINVAL;
	} else {
		response = testing_hcd->resp.buf;

		testing_hcd->result = 1;

		retval = 0;

		pin = 0;

		for (i = 0; i < length; i++) {
			value = response[i];

			dev_dbg(&tcm_hcd->pdev->dev, "%s info: [%d]: 0x%02x\n",
				__func__, i, value);

			for (j = 0; j < 8; j++) {
				limit = pt1_limits[i];

				if ((value & (1 << j)) != (limit & (1 << j))) {
					dev_err(&tcm_hcd->pdev->dev,
						"%s error: (line %d) pin-%2d : fail\n",
						__func__, 914, pin + j);
					testing_hcd->result = 0;
				} else {
					dev_dbg(&tcm_hcd->pdev->dev,
						"%s info: pin-%2d : pass\n",
						__func__, pin + j);
				}
			}

			pin += 8;
		}
	}

	mutex_unlock(&testing_hcd->resp.buf_mutex);

	if (tcm_hcd->feature_description[2] & 0x01) {
		if (tcm_hcd->reset_fn(tcm_hcd) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 934);
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

/*
 * pt05 full raw: the whole panel frame has to stay inside one fixed window,
 * otherwise the panel is not bonded the way the firmware expects.
 */
static int testing_pt05_full_raw(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int rows = get_unaligned_le16(&tcm_hcd->app_info[0x28]);
	unsigned int cols = get_unaligned_le16(&tcm_hcd->app_info[0x2a]);
	unsigned int i;
	unsigned int j;
	unsigned int offset;
	const u8 *response;
	u16 data;
	u16 lo;
	u16 hi;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	retval = testing_run_prod_test_item(5);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run test\n",
			__func__, 973);
		goto do_reset;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	if ((rows * cols) != (testing_hcd->resp.data_length >> 1)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Frame size mismatch\n",
			__func__, 981);
		retval = -EINVAL;
		goto unlock_resp;
	}

	if (rows > TESTING_LIMITS_ROWS || cols >= TESTING_LIMITS_COLS) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Mismatching limits data\n",
			__func__, 994);
		retval = -EINVAL;
		goto unlock_resp;
	}

	testing_hcd->result = 1;

	response = testing_hcd->resp.buf;

	for (i = 0; i < rows; i++) {
		if (!cols)
			continue;

		for (j = 0; j < cols; j++) {
			offset = (i * cols + j) * 2;

			data = get_unaligned_le16(&response[offset]);

			lo = pt7_lo_limits[i][j];
			hi = pt5_hi_limits[i][j];

			if (data > hi || data < lo) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					__func__, 1029, i, j, data, lo, hi);
				testing_hcd->result = 0;
			}
		}
	}

unlock_resp:
	mutex_unlock(&testing_hcd->resp.buf_mutex);

do_reset:
	if (tcm_hcd->feature_description[2] & 0x01) {
		if (tcm_hcd->reset_fn(tcm_hcd) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 1046);
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

/*
 * pt07 dynamic range: the difference between a touched and an untouched cell
 * has to land inside the narrow window that tells the firmware the panel
 * responds linearly.
 */
static int testing_pt07_dynamic_range(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int rows = get_unaligned_le16(&tcm_hcd->app_info[0x28]);
	unsigned int cols = get_unaligned_le16(&tcm_hcd->app_info[0x2a]);
	unsigned int i;
	unsigned int j;
	unsigned int offset;
	const u8 *response;
	u16 data;
	u16 lo;
	u16 hi;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	retval = testing_run_prod_test_item(7);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run test\n",
			__func__, 1086);
		goto do_reset;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	if ((rows * cols) != (testing_hcd->resp.data_length >> 1)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Frame size mismatch\n",
			__func__, 1094);
		retval = -EINVAL;
		goto unlock_resp;
	}

	if (rows > TESTING_LIMITS_ROWS || cols >= TESTING_LIMITS_COLS) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Mismatching limits data\n",
			__func__, 1107);
		retval = -EINVAL;
		goto unlock_resp;
	}

	testing_hcd->result = 1;

	response = testing_hcd->resp.buf;

	for (i = 0; i < rows; i++) {
		if (!cols)
			continue;

		for (j = 0; j < cols; j++) {
			offset = (i * cols + j) * 2;

			data = get_unaligned_le16(&response[offset]);

			lo = pt7_lo_limits[i][j];
			hi = pt7_hi_limits[i][j];

			if (data > hi || data < lo) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					__func__, 1139, i, j, data, lo, hi);
				testing_hcd->result = 0;
			}
		}
	}

unlock_resp:
	mutex_unlock(&testing_hcd->resp.buf_mutex);

	retval = testing_standard_frame_output(0) ? : retval;

do_reset:
	if (tcm_hcd->feature_description[2] & 0x01) {
		if (tcm_hcd->reset_fn(tcm_hcd) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 1157);
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

/*
 * pt10 noise: every cell of the untouched panel has to stay below the noise
 * floor of the item.
 */
static int testing_pt10_noise(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int rows = get_unaligned_le16(&tcm_hcd->app_info[0x28]);
	unsigned int cols = get_unaligned_le16(&tcm_hcd->app_info[0x2a]);
	unsigned int i;
	unsigned int j;
	unsigned int offset;
	const u8 *response;
	u16 data;
	u16 limit;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	retval = testing_run_prod_test_item(10);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run test\n",
			__func__, 1196);
		goto do_reset;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	if ((rows * cols) != (testing_hcd->resp.data_length >> 1)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Frame size mismatch\n",
			__func__, 1204);
		retval = -EINVAL;
		goto unlock_resp;
	}

	if (rows > TESTING_LIMITS_ROWS || cols >= TESTING_LIMITS_COLS) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Mismatching limits data\n",
			__func__, 1217);
		retval = -EINVAL;
		goto unlock_resp;
	}

	testing_hcd->result = 1;

	response = testing_hcd->resp.buf;

	for (i = 0; i < rows; i++) {
		if (!cols)
			continue;

		for (j = 0; j < cols; j++) {
			offset = (i * cols + j) * 2;

			data = get_unaligned_le16(&response[offset]);

			limit = pt10_limits[i][j];

			if (data > limit) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) fail at (%2d, %2d) data = %5d, limit = %4d\n",
					__func__, 1234, i, j, data, limit);
				testing_hcd->result = 0;
			}
		}
	}

unlock_resp:
	mutex_unlock(&testing_hcd->resp.buf_mutex);

	retval = testing_standard_frame_output(0) ? : retval;

do_reset:
	if (tcm_hcd->feature_description[2] & 0x01) {
		if (tcm_hcd->reset_fn(tcm_hcd) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 1252);
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

/*
 * pt11 open detection: every cell has to stay inside a symmetric window
 * around zero, an open trace pulls a cell out of it.
 */
static int testing_pt11_open_detection(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int rows = get_unaligned_le16(&tcm_hcd->app_info[0x28]);
	unsigned int cols = get_unaligned_le16(&tcm_hcd->app_info[0x2a]);
	unsigned int i;
	unsigned int j;
	unsigned int offset;
	const u8 *response;
	u16 data;
	u16 lo;
	u16 hi;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	retval = testing_run_prod_test_item(0x0b);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run test\n",
			__func__, 1292);
		goto do_reset;
	}

	mutex_lock(&testing_hcd->resp.buf_mutex);

	if ((rows * cols) != (testing_hcd->resp.data_length >> 1)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Image size mismatch\n",
			__func__, 1300);
		retval = -EINVAL;
		goto unlock_resp;
	}

	if (rows > TESTING_LIMITS_ROWS || cols >= TESTING_LIMITS_COLS) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Mismatching limits data\n",
			__func__, 1313);
		retval = -EINVAL;
		goto unlock_resp;
	}

	testing_hcd->result = 1;

	response = testing_hcd->resp.buf;

	for (i = 0; i < rows; i++) {
		if (!cols)
			continue;

		for (j = 0; j < cols; j++) {
			offset = (i * cols + j) * 2;

			data = get_unaligned_le16(&response[offset]);

			lo = pt11_lo_limits[i][j];
			hi = pt11_hi_limits[i][j];

			if (data > hi || data < lo) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) fail at (%2d, %2d) data = %5d, limit = (%4d, %4d)\n",
					__func__, 1346, i, j, data, lo, hi);
				testing_hcd->result = 0;
			}
		}
	}

unlock_resp:
	mutex_unlock(&testing_hcd->resp.buf_mutex);

	retval = testing_standard_frame_output(1) ? : retval;

do_reset:
	if (tcm_hcd->feature_description[2] & 0x01) {
		if (tcm_hcd->reset_fn(tcm_hcd) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do reset\n",
				__func__, 1364);
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

static ssize_t testing_sysfs_size_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	mutex_lock(&testing_hcd->output.buf_mutex);

	count = snprintf(buf, PAGE_SIZE, "%u\n",
			 testing_hcd->output.data_length);

	mutex_unlock(&testing_hcd->output.buf_mutex);

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/* the firmware reports the part number the production line programmed */
static int testing_device_id(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	if (!strnstr(&tcm_hcd->identify_info[2], "3618", 16)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Device ID is mismatching, FW: %s (%s)\n",
			__func__, 542, &tcm_hcd->identify_info[2], "3618");
	} else {
		testing_hcd->result = 1;
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return 0;
}

static ssize_t testing_sysfs_device_id_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	testing_device_id();

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/*
 * The config id is the sixteen bytes the factory tool wrote into app_info;
 * the vendor compares them against a literal that ends in "G", keep it.
 */
static int testing_config_id(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	const char *expected = "0123456789ABCDEG";
	unsigned int i;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 1;

	for (i = 0; i < 16; i++) {
		if (expected[i] != tcm_hcd->app_info[0x10 + i]) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Config ID is mismatching at byte %d\n",
				__func__, 567, i);
			testing_hcd->result = 0;
		}
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return 0;
}

static ssize_t testing_sysfs_config_id_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;

	mutex_lock(&tcm_hcd->extif_mutex);

	testing_config_id();

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t testing_sysfs_pt01_trx_trx_short_show(struct device *dev,
						     struct device_attribute *attr,
						     char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_pt01_trx_trx_short();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do pt01_trx_trx_short test\n",
			__func__, 190);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t testing_sysfs_pt05_full_raw_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_pt05_full_raw();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do pt05_full_raw test\n",
			__func__, 192);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t testing_sysfs_pt07_dynamic_range_show(struct device *dev,
						     struct device_attribute *attr,
						     char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_pt07_dynamic_range();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do pt07_dynamic_range test\n",
			__func__, 194);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t testing_sysfs_pt10_noise_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_pt10_noise();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do pt10_noise test\n",
			__func__, 196);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

static ssize_t testing_sysfs_pt11_open_detection_show(struct device *dev,
						      struct device_attribute *attr,
						      char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_pt11_open_detection();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do pt11_open_detection test\n",
			__func__, 198);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/*
 * Drive the reset line by hand, check that the controller comes back in the
 * bootloader and then bring the application firmware back.  Everything
 * happens under the reset mutex so that nothing else talks to the controller
 * while the line is toggled.
 */
static int testing_reset_open(void)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	int retval;

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start testing\n", __func__);

	testing_hcd->result = 0;

	if (bdata->reset_gpio < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Hardware reset unavailable\n",
			__func__, 1386);
		return -EINVAL;
	}

	mutex_lock(&tcm_hcd->reset_mutex);

	gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
			    bdata->reset_on_state);

	msleep(bdata->reset_active_ms);

	gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio),
			    (bdata->reset_on_state == 0));

	msleep(bdata->reset_delay_ms);

	mutex_unlock(&tcm_hcd->reset_mutex);

	if (tcm_hcd->identify_info[1] == 0x01) {
		retval = tcm_hcd->switch_mode_fn(tcm_hcd, MODE_LOAD_BOOTLOADER);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to enter bootloader mode\n",
				__func__, 1411);
			return retval;
		}
	} else {
		retval = tcm_hcd->identify_fn(tcm_hcd, false);
		if (retval < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to do identification\n",
				__func__, 1418);
	}

	if (retval >= 0) {
		testing_hcd->result = (tcm_hcd->boot_info[9] == 0x13);

		retval = 0;
	}

	if (tcm_hcd->switch_mode_fn(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 1433);

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Result = %s\n", __func__,
		   testing_hcd->result ? "pass" : "fail");

	return retval;
}

static ssize_t testing_sysfs_reset_open_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	int count;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	retval = testing_reset_open();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do reset_open test\n",
			__func__, 200);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return retval;
	}

	count = snprintf(buf, PAGE_SIZE, "%s\n",
			 testing_hcd->result ? "Passed" : "Failed");

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}
/* the "data" binary file hands out the frame of the last finished item */
static ssize_t testing_sysfs_data_show(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *attr, char *buf,
				       loff_t offset, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int size;

	mutex_lock(&tcm_hcd->extif_mutex);

	mutex_lock(&testing_hcd->output.buf_mutex);

	size = min_t(unsigned int, count,
		     testing_hcd->output.data_length - offset);

	if (buf && testing_hcd->output.buf &&
	    !secure_memcpy(buf, count, testing_hcd->output.buf + offset, size)) {
		mutex_unlock(&testing_hcd->output.buf_mutex);
		mutex_unlock(&tcm_hcd->extif_mutex);
		return size;
	}

	dev_err(&tcm_hcd->pdev->dev,
		"%s error: (line %d) Failed to copy report data\n",
		__func__, 245);

	mutex_unlock(&testing_hcd->output.buf_mutex);

	mutex_unlock(&tcm_hcd->extif_mutex);

	return -EINVAL;
}

static struct bin_attribute testing_data_bin_attr = {
	.attr = {
		.name = "data",
		.mode = 0444,
	},
	.size = 0,
	.read = testing_sysfs_data_show,
};

static DEVICE_ATTR(size, 0444, testing_sysfs_size_show, syna_tcm_store_error);
static DEVICE_ATTR(device_id, 0444, testing_sysfs_device_id_show,
		   syna_tcm_store_error);
static DEVICE_ATTR(config_id, 0444, testing_sysfs_config_id_show,
		   syna_tcm_store_error);
static DEVICE_ATTR(pt01_trx_trx_short, 0444,
		   testing_sysfs_pt01_trx_trx_short_show, syna_tcm_store_error);
static DEVICE_ATTR(pt05_full_raw, 0444, testing_sysfs_pt05_full_raw_show,
		   syna_tcm_store_error);
static DEVICE_ATTR(pt07_dynamic_range, 0444,
		   testing_sysfs_pt07_dynamic_range_show, syna_tcm_store_error);
static DEVICE_ATTR(pt10_noise, 0444, testing_sysfs_pt10_noise_show,
		   syna_tcm_store_error);
static DEVICE_ATTR(pt11_open_detection, 0444,
		   testing_sysfs_pt11_open_detection_show, syna_tcm_store_error);
static DEVICE_ATTR(reset_open, 0444, testing_sysfs_reset_open_show,
		   syna_tcm_store_error);

static struct attribute *testing_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_device_id.attr,
	&dev_attr_config_id.attr,
	&dev_attr_pt01_trx_trx_short.attr,
	&dev_attr_pt05_full_raw.attr,
	&dev_attr_pt07_dynamic_range.attr,
	&dev_attr_pt10_noise.attr,
	&dev_attr_pt11_open_detection.attr,
	&dev_attr_reset_open.attr,
	NULL,
};

/*
 * Ask the production test firmware for report_size frames of report_code and
 * wait until the interrupt thread has gathered all of them.
 */
static int testing_collect_reports(u8 report_code, unsigned int report_size)
{
	struct syna_tcm_hcd *tcm_hcd = testing_hcd->tcm_hcd;
	unsigned int timeout;
	int retval;

	testing_hcd->report_code = report_code;

	testing_hcd->report_index = 0;

	testing_hcd->report_size = report_size;

	init_completion(&report_complete);

	mutex_lock(&testing_hcd->out.buf_mutex);

	if (!testing_hcd->out.buf_size) {
		kfree(testing_hcd->out.buf);

		testing_hcd->out.buf = kmalloc(1, GFP_KERNEL);
		if (!testing_hcd->out.buf) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			dev_err(&tcm_hcd->pdev->dev,
				"%s: Allocation size = %d\n", "syna_tcm_alloc_mem",
				1);
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for testing_hcd->out.buf\n",
				__func__, 343);
			mutex_unlock(&testing_hcd->out.buf_mutex);
			return -ENOMEM;
		}

		testing_hcd->out.buf_size = 1;
	}

	memset(testing_hcd->out.buf, 0x00, 1);

	testing_hcd->out.data_length = 0;

	testing_hcd->out.buf[0] = testing_hcd->report_code;

	mutex_lock(&testing_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, CMD_ENABLE_REPORT,
					   testing_hcd->out.buf, 1,
					   &testing_hcd->resp.buf,
					   &testing_hcd->resp.buf_size,
					   &testing_hcd->resp.data_length,
					   NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 364, "CMD_ENABLE_REPORT");
		mutex_unlock(&testing_hcd->resp.buf_mutex);
		mutex_unlock(&testing_hcd->out.buf_mutex);
		return -EIO;
	}

	mutex_unlock(&testing_hcd->resp.buf_mutex);

	mutex_unlock(&testing_hcd->out.buf_mutex);

	timeout = report_size * TESTING_FRAME_TIMEOUT_MS;

	timeout = wait_for_completion_timeout(&report_complete,
					     msecs_to_jiffies(timeout));
	if (!timeout)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Timed out waiting for report collection\n",
			__func__, 380);

	mutex_lock(&testing_hcd->out.buf_mutex);

	testing_hcd->out.buf[0] = testing_hcd->report_code;

	mutex_lock(&testing_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, CMD_DISABLE_REPORT,
					   testing_hcd->out.buf, 1,
					   &testing_hcd->resp.buf,
					   &testing_hcd->resp.buf_size,
					   &testing_hcd->resp.data_length,
					   NULL, 0);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 403, "CMD_DISABLE_REPORT");

	mutex_unlock(&testing_hcd->resp.buf_mutex);

	mutex_unlock(&testing_hcd->out.buf_mutex);

	testing_hcd->report_code = 0;

	return timeout ? 0 : -EIO;
}

/*
 * The interrupt thread hands every report with the code the test asked for to
 * this hook; the frames of one item all carry the same length, so the buffer
 * is sized once and the frames are appended in order.
 */
static int testing_syncbox(struct syna_tcm_hcd *tcm_hcd)
{
	unsigned int frame_size;
	unsigned int size;
	unsigned int offset;
	int retval;

	if (!testing_hcd)
		return 0;

	if (tcm_hcd->dispatch_report_code != testing_hcd->report_code)
		return 0;

	frame_size = tcm_hcd->report_buffer.data_length;

	mutex_lock(&testing_hcd->report.buf_mutex);

	if (!testing_hcd->report_index) {
		size = testing_hcd->report_size * frame_size;

		if (testing_hcd->report.buf_size < size) {
			kfree(testing_hcd->report.buf);

			testing_hcd->report.buf = kmalloc(size, GFP_KERNEL);
			if (!testing_hcd->report.buf) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s: Failed to allocate memory\n",
					"syna_tcm_alloc_mem");
				dev_err(&tcm_hcd->pdev->dev,
					"%s: Allocation size = %d\n",
					"syna_tcm_alloc_mem", size);
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to allocate memory for testing_hcd->report.buf\n",
					"testing_report", 1459);
				testing_hcd->report.buf_size = 0;
				mutex_unlock(&testing_hcd->report.buf_mutex);
				return 0;
			}

			testing_hcd->report.buf_size = size;
		} else {
			size = testing_hcd->report.buf_size;
		}

		memset(testing_hcd->report.buf, 0x00, size);

		testing_hcd->report.data_length = 0;
	}

	if (testing_hcd->report_index < testing_hcd->report_size) {
		offset = testing_hcd->report_index * frame_size;

		if (testing_hcd->report.buf &&
		    tcm_hcd->report_buffer.buf &&
		    !secure_memcpy(testing_hcd->report.buf + offset,
				   testing_hcd->report.buf_size - offset,
				   tcm_hcd->report_buffer.buf, frame_size)) {
			testing_hcd->report_index++;

			testing_hcd->report.data_length += frame_size;
		} else {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to copy report data\n",
				"testing_report", 1475);
			mutex_unlock(&testing_hcd->report.buf_mutex);
			return 0;
		}
	}

	mutex_unlock(&testing_hcd->report.buf_mutex);

	if (testing_hcd->report_index == testing_hcd->report_size)
		complete(&report_complete);

	return 0;
}

static int testing_init(struct syna_tcm_hcd *tcm_hcd)
{
	int index;
	int count;
	int retval;

	testing_hcd = kzalloc(sizeof(*testing_hcd), GFP_KERNEL);
	if (!testing_hcd) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to allocate memory for testing_hcd\n",
			__func__, 1501);
		return -ENOMEM;
	}

	testing_hcd->tcm_hcd = tcm_hcd;

	testing_hcd->collect_reports = testing_collect_reports;

	mutex_init(&testing_hcd->out.buf_mutex);
	testing_hcd->out.init = 0;

	mutex_init(&testing_hcd->resp.buf_mutex);
	testing_hcd->resp.init = 0;

	mutex_init(&testing_hcd->report.buf_mutex);
	testing_hcd->report.init = 0;

	mutex_init(&testing_hcd->process.buf_mutex);
	testing_hcd->process.init = 0;

	mutex_init(&testing_hcd->output.buf_mutex);
	testing_hcd->output.init = 0;

	testing_hcd->kobj = kobject_create_and_add("testing",
						   tcm_hcd->sysfs_root);
	if (!testing_hcd->kobj) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to create sysfs directory\n",
			__func__, 1519);
		retval = -EINVAL;
		goto err_free_hcd;
	}

	retval = sysfs_create_file(&testing_hcd->kobj, &dev_attr_size.attr);
	if (retval < 0) {
		index = -1;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj, &dev_attr_device_id.attr);
	if (retval < 0) {
		index = 0;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj, &dev_attr_config_id.attr);
	if (retval < 0) {
		index = 1;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj,
				   &dev_attr_pt01_trx_trx_short.attr);
	if (retval < 0) {
		index = 2;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj,
				   &dev_attr_pt05_full_raw.attr);
	if (retval < 0) {
		index = 3;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj,
				   &dev_attr_pt07_dynamic_range.attr);
	if (retval < 0) {
		index = 4;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj, &dev_attr_pt10_noise.attr);
	if (retval < 0) {
		index = 5;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj,
				   &dev_attr_pt11_open_detection.attr);
	if (retval < 0) {
		index = 6;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(&testing_hcd->kobj, &dev_attr_reset_open.attr);
	if (retval < 0) {
		index = 7;
		goto err_remove_sysfs;
	}

	retval = sysfs_create_bin_file(&testing_hcd->kobj,
				       &testing_data_bin_attr);
	if (retval < 0) {
		index = 7;
		goto err_remove_bin;
	}

	return 0;

err_remove_bin:
	dev_err(&tcm_hcd->pdev->dev,
		"%s error: (line %d) Failed to create sysfs bin file\n",
		__func__, 1562);

	for (count = index; count >= 0; count--)
		sysfs_remove_file(&testing_hcd->kobj, testing_attrs[count]);

	goto err_put_kobj;

err_remove_sysfs:
	dev_err(&tcm_hcd->pdev->dev,
		"%s error: (line %d) Failed to create sysfs file\n",
		__func__, 1529);

	for (count = index; count >= 0; count--)
		sysfs_remove_file(&testing_hcd->kobj, testing_attrs[count]);

err_put_kobj:
	kobject_put(testing_hcd->kobj);

err_free_hcd:
	testing_remove_buffers(testing_hcd);

	kfree(testing_hcd);

	testing_hcd = NULL;

	return retval;
}

static int testing_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (testing_hcd) {
		sysfs_remove_bin_file(testing_hcd->kobj, &testing_data_bin_attr);

		sysfs_remove_file(&testing_hcd->kobj, &dev_attr_size.attr);
		sysfs_remove_file(&testing_hcd->kobj, &dev_attr_device_id.attr);
		sysfs_remove_file(&testing_hcd->kobj, &dev_attr_config_id.attr);
		sysfs_remove_file(&testing_hcd->kobj,
				  &dev_attr_pt01_trx_trx_short.attr);
		sysfs_remove_file(&testing_hcd->kobj,
				  &dev_attr_pt05_full_raw.attr);
		sysfs_remove_file(&testing_hcd->kobj,
				  &dev_attr_pt07_dynamic_range.attr);
		sysfs_remove_file(&testing_hcd->kobj, &dev_attr_pt10_noise.attr);
		sysfs_remove_file(&testing_hcd->kobj,
				  &dev_attr_pt11_open_detection.attr);
		sysfs_remove_file(&testing_hcd->kobj, &dev_attr_reset_open.attr);

		kobject_put(testing_hcd->kobj);

		testing_remove_buffers(testing_hcd);

		kfree(testing_hcd);

		testing_hcd = NULL;
	}

	complete(&testing_remove_complete);

	return 0;
}

static int testing_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (testing_hcd)
		return 0;

	return testing_init(tcm_hcd);
}

static struct syna_tcm_module_desc testing_module = {
	.id = TESTING_MODULE_ID,
	.init = testing_init,
	.remove = testing_remove,
	.handler = testing_syncbox,
	.reinit = testing_reinit,
};

int testing_module_init(void)
{
	return syna_tcm_add_module(&testing_module, true);
}

void testing_module_exit(void)
{
	syna_tcm_add_module(&testing_module, false);

	wait_for_completion(&testing_remove_complete);
}
