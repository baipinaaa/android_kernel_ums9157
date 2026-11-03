// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm zero flash module.
 *
 * The controller can be told to reboot into its host download firmware by a
 * plain hardware reset while the interrupt line is enabled.  What happens next
 * depends on the state the part wakes up in: the F$35 bootloader wants the
 * application firmware pushed through F$35 registers, the romboot loader wants
 * one big command, and the host download firmware asks for the configuration
 * blocks that live next to the firmware image.  All three transfers run from
 * this file, which is why it also owns the workqueue that pushes the config
 * blocks once the part reports that it is ready for them.
 */

#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/kobject.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include "synaptics_tcm_core.h"

/* id the core logs this module with */
#define ZEROFLASH_MODULE_ID			0x00

/* the image the bootloader side of the part expects */
#define ZEROFLASH_FIRMWARE_NAME			"synaptics/hdl_firmware.img"
#define ZEROFLASH_IMAGE_MAGIC			0x4818472b
#define ZEROFLASH_SECTION_MAGIC			0x7c05e516
#define ZEROFLASH_SECTION_NAME_SIZE		0x14
#define ZEROFLASH_IMAGE_HEADER_SIZE		0x08

/* F$35 based download */
#define ZEROFLASH_HDL_ADDR_FUNCTION		0xee
#define ZEROFLASH_HDL_ADDR_PDT			0xe9
#define ZEROFLASH_HDL_PDT_ENTRY_SIZE		6
#define ZEROFLASH_HDL_ADDR_VM			0x35
#define ZEROFLASH_HDL_FUNCTION_MASK		0x1f
#define ZEROFLASH_HDL_STATE_READY		0x0b
#define ZEROFLASH_HDL_STATE_RETRY		0x06
#define ZEROFLASH_HDL_STATE_DONE		0x0f
#define ZEROFLASH_HDL_UBOOT_QUERY_TRIES		4
#define ZEROFLASH_HDL_COMMAND_OFFSET		0x12
#define ZEROFLASH_HDL_DATA_OFFSET		0x16
#define ZEROFLASH_HDL_RETRY_LIMIT		0x0b
#define ZEROFLASH_HDL_RETRY_INTERVAL_MS		20
#define ZEROFLASH_F35_COMMAND_TRANSFER		0x04

/* romboot based download */
#define ZEROFLASH_ROMBOOT_HEADER_SIZE		0x0e
#define ZEROFLASH_ROMBOOT_TIMEOUT_MS		20

/* config block download */
#define ZEROFLASH_CONFIG_COMMAND		0x30
#define ZEROFLASH_CONFIG_APP			1
#define ZEROFLASH_CONFIG_DISPLAY		2
#define ZEROFLASH_CONFIG_OPEN_SHORT		3
#define ZEROFLASH_CONFIG_PADDING		2
#define ZEROFLASH_CONFIG_RETRY_LIMIT		10
#define ZEROFLASH_REPORT_STATUS			0x1b
#define ZEROFLASH_REPORT_F35			0xfe
#define ZEROFLASH_REPORT_ROMBOOT		0xfd

/* the part asks for a host download through the status report */
#define ZEROFLASH_STATUS_F35			BIT(1)
#define ZEROFLASH_STATUS_DISPLAY		BIT(1)
#define ZEROFLASH_STATUS_APP_CONFIG		BIT(2)
#define ZEROFLASH_STATUS_OPEN_SHORT		BIT(7)
#define ZEROFLASH_STATUS_CONFIGS		(ZEROFLASH_STATUS_APP_CONFIG | \
						 ZEROFLASH_STATUS_DISPLAY)

#define ZEROFLASH_HDL_WAIT_MS			5000

/* values of tcm_hcd->mode while a download is running */
#define ZEROFLASH_MODE_HDL			2
#define ZEROFLASH_MODE_ROMBOOT			3

/*
 * The host download state machine lives in one static pointer; the core keeps
 * calling into the module long after the driver probed, so the pointer is the
 * only way the report handler can find the buffers of the current session.
 */
struct syna_tcm_zeroflash_hcd {
	u8 host_download_supported;		/* +0x000 */
	u8 uboot_found;				/* +0x001 */
	u8 open_short_available;		/* +0x002 */
	u8 pad0[0x05];				/* +0x003 */
	u8 *image;				/* +0x008 */
	u8 pad1[0x08];				/* +0x010 */
	struct firmware *fw;			/* +0x018 */
	struct work_struct download_config_work;	/* +0x020 */
	u8 pad2[0x10];				/* +0x040 */
	struct workqueue_struct *workqueue;	/* +0x050 */
	struct kobject *kobj;			/* +0x058 */
	u16 pdt_entry[4];			/* +0x060 */
	u32 app_config_id;			/* +0x068 */
	u8 pad3[0x04];				/* +0x06c */
	u8 *boot_config;			/* +0x070 */
	u8 pad4[0x08];				/* +0x078 */
	u8 *hdl_firmware;			/* +0x080 */
	u32 hdl_firmware_size;			/* +0x088 */
	u8 pad5[0x04];				/* +0x08c */
	u8 *app_config;				/* +0x090 */
	u32 app_config_size;			/* +0x098 */
	u8 pad6[0x04];				/* +0x09c */
	u8 *display_config;			/* +0x0a0 */
	u32 display_config_size;		/* +0x0a8 */
	u8 pad7[0x04];				/* +0x0ac */
	u8 *open_short_config;			/* +0x0b0 */
	u32 open_short_config_size;		/* +0x0b8 */
	u8 pad8[0x04];				/* +0x0bc */
	u16 status;				/* +0x0c0 */
	u8 pad9[0x06];				/* +0x0c2 */
	struct syna_tcm_buf out;		/* +0x0c8 */
	struct syna_tcm_buf resp;		/* +0x100 */
	struct syna_tcm_hcd *tcm_hcd;		/* +0x138 */
};

static struct syna_tcm_zeroflash_hcd *zeroflash_hcd;
static DECLARE_COMPLETION(zeroflash_remove_complete);

/* per block retry counters, the part rejects a block until it is complete */
static unsigned int zeroflash_app_config_retry_count;
static unsigned int zeroflash_display_config_retry_count;
static unsigned int zeroflash_open_short_config_retry_count;

static int syna_tcm_secure_memcpy(u8 *dest, unsigned int dest_size,
		const u8 *src, unsigned int src_size)
{
	if (!dest || !src || dest_size < src_size) {
		pr_err("%s: src_size = %d, dest_size = %d, count = %d\n",
				"secure_memcpy", src_size, dest_size, src_size);
		return -EINVAL;
	}

	memcpy(dest, src, src_size);

	return 0;
}

static int zeroflash_alloc_mem(struct syna_tcm_buf *buf, unsigned int size)
{
	if (buf->buf_size >= size) {
		memset(buf->buf, 0, buf->buf_size);
		return 0;
	}

	kfree(buf->buf);
	buf->buf = kmalloc(size, GFP_KERNEL);

	if (!buf->buf) {
		pr_err("%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
		pr_err("%s: Allocation size = %d\n", "syna_tcm_alloc_mem", size);
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;

	memset(buf->buf, 0, buf->buf_size);

	return 0;
}

static ssize_t syna_tcm_store_error(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	pr_err("%s: Attribute not writable\n", __func__);

	return -1;
}

static ssize_t syna_tcm_show_error(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	pr_err("%s: Attribute not readable\n", __func__);

	return -1;
}

/*
 * The host download runs asynchronously: the reset below puts the part into its
 * bootloader and the report handler kicks the actual transfer off.  Waiting here
 * only keeps the factory tool from issuing the next command too early.
 */
static int zeroflash_wait_hdl(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;

	if (tcm_hcd->hdl_pending) {
		retval = wait_event_interruptible_timeout(tcm_hcd->hdl_wq,
				tcm_hcd->hdl_pending,
				msecs_to_jiffies(ZEROFLASH_HDL_WAIT_MS));

		if (!retval) {
			dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Timed out waiting for completion of host download\n",
					__func__, 229);
			tcm_hcd->hdl_pending = 0;
			retval = -EIO;
		}
	}

	return retval;
}

static ssize_t zeroflash_sysfs_hdl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input = 0;
	struct syna_tcm_hcd *tcm_hcd;
	int retval;

	tcm_hcd = zeroflash_hcd->tcm_hcd;

	retval = sscanf(buf, "%u", &input);

	if (retval != 1)
		return -EINVAL;

	if (!input || !tcm_hcd->irq_enabled) {
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Invalid HDL devices\n",
				__func__, 270);
		return count;
	}

	if (tcm_hcd->reset_fn && tcm_hcd->reset_fn(tcm_hcd) < 0)
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to trigger the host download by reset\n",
				__func__, 254);

	msleep(100);

	retval = zeroflash_wait_hdl(tcm_hcd);

	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to wait for completion of host download\n",
				__func__, 259);

	if (zeroflash_hcd->fw) {
		release_firmware(zeroflash_hcd->fw);
		zeroflash_hcd->fw = NULL;
	}

	zeroflash_hcd->image = NULL;

	return count;
}

static DEVICE_ATTR(hdl, 0220, syna_tcm_show_error, zeroflash_sysfs_hdl_store);

static int zeroflash_parse_fw_image(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	u8 *image = zeroflash_hcd->image;
	u8 *section;
	u8 *name;
	u8 *payload;
	unsigned int count;
	unsigned int index = ZEROFLASH_IMAGE_HEADER_SIZE;
	u32 address;
	u32 size;
	u32 crc;

	if (get_unaligned_le32(image) != ZEROFLASH_IMAGE_MAGIC) {
		dev_err(dev, "%s error: (line %d) Invalid image file magic value\n",
				__func__, 374);
		return -EINVAL;
	}

	memset(&zeroflash_hcd->app_config_id, 0, 0x50);

	count = get_unaligned_le32(image + 4);

	while (count) {
		section = image + get_unaligned_le32(image + index);
		index += 4;

		if (get_unaligned_le32(section) != ZEROFLASH_SECTION_MAGIC) {
			if (!--count)
				break;
			continue;
		}

		address = ((u32)section[0x18] << 1) |
				((u32)section[0x19] << 9) |
				((u32)section[0x1a] << 17) |
				((u32)(section[0x1b] & 0x7f) << 25);
		size = get_unaligned_le32(section + 0x1c);
		crc = get_unaligned_le32(section + 0x20);
		name = section + 4;
		payload = section + 0x24;

		if (!strncmp(name, "BOOT_CONFIG", 11)) {
			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) Boot config checksum error\n",
						__func__, 402);
				return -EINVAL;
			}

			zeroflash_hcd->boot_config = payload;

			dev_dbg(dev, "%s info: Boot config size = %d\n",
					__func__, size);
			dev_dbg(dev, "%s info: Boot config flash address = 0x%08x\n",
					__func__, address);
		} else if (!strncmp(name, "F35_APP_CODE", 12)) {
			if (tcm_hcd->mode != ZEROFLASH_MODE_HDL) {
				dev_err(dev, "%s error: (line %d) Improper descriptor, F35_APP_CODE_ID\n",
						__func__, 419);
				return -EINVAL;
			}

			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) HDL_F35 firmware checksum error\n",
						__func__, 425);
				return -EINVAL;
			}

			zeroflash_hcd->hdl_firmware = payload;
			zeroflash_hcd->hdl_firmware_size = size;

			dev_dbg(dev, "%s info: HDL_F35 firmware size = %d\n",
					__func__, size);
			dev_dbg(dev, "%s info: HDL_F35 firmware flash address = 0x%08x\n",
					__func__, address);
		} else if (!strncmp(name, "ROMBOOT_APP_CODE", 16)) {
			if (tcm_hcd->mode != ZEROFLASH_MODE_ROMBOOT) {
				dev_err(dev, "%s error: (line %d) Improper descriptor, ROMBOOT_APP_CODE_ID\n",
						__func__, 444);
				return -EINVAL;
			}

			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) HDL_ROMBoot firmware checksum error\n",
						__func__, 450);
				return -EINVAL;
			}

			zeroflash_hcd->hdl_firmware = payload;
			zeroflash_hcd->hdl_firmware_size = size;

			dev_dbg(dev, "%s info: HDL_ROMBoot firmware size = %d\n",
					__func__, size);
			dev_dbg(dev, "%s info: HDL_ROMBoot firmware flash address = 0x%08x\n",
					__func__, address);
		} else if (!strncmp(name, "APP_CONFIG", 10)) {
			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) Application config checksum error\n",
						__func__, 468);
				return -EINVAL;
			}

			zeroflash_hcd->app_config = payload;
			zeroflash_hcd->app_config_size = size;
			zeroflash_hcd->app_config_id =
					get_unaligned_le32(payload + 0x0e);

			dev_dbg(dev, "%s info: Application config size = %d\n",
					__func__, size);
			dev_dbg(dev, "%s info: Application config flash address = 0x%08x\n",
					__func__, address);
		} else if (!strncmp(name, "DISPLAY", 7)) {
			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) Display config checksum error\n",
						__func__, 486);
				return -EINVAL;
			}

			zeroflash_hcd->display_config = payload;
			zeroflash_hcd->display_config_size = size;

			dev_dbg(dev, "%s info: Display config size = %d\n",
					__func__, size);
			dev_dbg(dev, "%s info: Display config flash address = 0x%08x\n",
					__func__, address);
		} else if (!strncmp(name, "OPENSHORT", 9)) {
			if (~crc32_le(~0, payload, size) != crc) {
				dev_err(dev, "%s error: (line %d) open_short config checksum error\n",
						__func__, 503);
				return -EINVAL;
			}

			zeroflash_hcd->open_short_available = 1;
			zeroflash_hcd->open_short_config = payload;
			zeroflash_hcd->open_short_config_size = size;

			dev_notice(dev, "%s info: open_short config size = %d\n",
					__func__, size);
			dev_notice(dev, "%s info: open_short config flash address = 0x%08x\n",
					__func__, address);
		}

		if (!--count)
			break;
	}

	return 0;
}

static int zeroflash_get_fw_image(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	int retval;

	if (zeroflash_hcd->fw)
		return 0;

	if (!zeroflash_hcd->image) {
		retval = request_firmware(&zeroflash_hcd->fw,
				ZEROFLASH_FIRMWARE_NAME, dev);

		if (retval < 0) {
			dev_err(dev, "%s error: (line %d) Failed to request %s\n",
					__func__, 537, ZEROFLASH_FIRMWARE_NAME);
			return retval;
		}

		zeroflash_hcd->image = zeroflash_hcd->fw->data;

		dev_dbg(dev, "%s info: Firmware image size = %d\n",
				__func__, (int)zeroflash_hcd->fw->size);
	}

	retval = zeroflash_parse_fw_image();

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to parse firmware image\n",
				__func__, 551);

		release_firmware(zeroflash_hcd->fw);
		zeroflash_hcd->fw = NULL;
		zeroflash_hcd->image = NULL;
	}

	return retval;
}

static int zeroflash_check_uboot(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	u8 status = 0;
	u8 entry[ZEROFLASH_HDL_PDT_ENTRY_SIZE] = { 0 };
	u8 query[3] = { 0 };
	unsigned int n;
	int retval;

	for (n = 0; n < ZEROFLASH_HDL_UBOOT_QUERY_TRIES; n++) {
		retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
				ZEROFLASH_HDL_ADDR_FUNCTION, &status, 1);

		if (retval < 0) {
			dev_err(dev, "%s error: (line %d) Failed to read RMI function number\n",
					__func__, 291);
			return retval;
		}

		dev_dbg(dev, "%s info: Found F$%02x\n", __func__, status);

		if (status == ZEROFLASH_HDL_ADDR_VM)
			break;
	}

	if (n == ZEROFLASH_HDL_UBOOT_QUERY_TRIES) {
		dev_err(dev, "%s error: (line %d) Failed to find F$35\n",
				__func__, 304);
		return -ENODEV;
	}

	if (zeroflash_hcd->uboot_found)
		return 0;

	retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd, ZEROFLASH_HDL_ADDR_PDT,
			entry, ZEROFLASH_HDL_PDT_ENTRY_SIZE);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to read PDT entry\n",
				__func__, 317);
		return retval;
	}

	zeroflash_hcd->pdt_entry[0] = entry[0];
	zeroflash_hcd->pdt_entry[1] = entry[1];
	zeroflash_hcd->pdt_entry[2] = entry[2];
	zeroflash_hcd->pdt_entry[3] = entry[3];

	retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd, entry[0], query, 3);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to read F$35 query\n",
				__func__, 332);
		return retval;
	}

	zeroflash_hcd->uboot_found = 1;

	if (((s8)query[0] < 0) && ((~query[2] & 0x03) == 0)) {
		zeroflash_hcd->host_download_supported = 1;
		return 0;
	}

	dev_err(dev, "%s error: (line %d) Host download not supported\n",
			__func__, 342);

	zeroflash_hcd->host_download_supported = 0;

	return -ENODEV;
}

/* retry counter of the F$35 download, the bootloader needs a few attempts */
static unsigned int zeroflash_hdl_retry_count;

static int zeroflash_download_app_fw(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	unsigned int size = zeroflash_hcd->hdl_firmware_size;
	u8 code = ZEROFLASH_F35_COMMAND_TRANSFER;
	int retval;

	dev_notice(dev, "%s info: Downloading application firmware\n", __func__);

	if (!size) {
		dev_err(dev, "%s error: (line %d) No application firmware in image file\n",
				__func__, 1005);
		return -ENOENT;
	}

	mutex_lock(&zeroflash_hcd->out.buf_mutex);

	if (zeroflash_hcd->out.buf_size < size) {
		kfree(zeroflash_hcd->out.buf);
		zeroflash_hcd->out.buf = kmalloc(size, GFP_KERNEL);

		if (!zeroflash_hcd->out.buf) {
			dev_err(dev, "%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			dev_err(dev, "%s: Allocation size = %d\n", "syna_tcm_alloc_mem", size);
			zeroflash_hcd->out.buf_size = 0;
			dev_err(dev, "%s error: (line %d) Failed to allocate memory for application firmware\n",
					__func__, 1016);
			mutex_unlock(&zeroflash_hcd->out.buf_mutex);
			return -ENOMEM;
		}

		zeroflash_hcd->out.buf_size = size;
	} else {
		size = zeroflash_hcd->out.buf_size;
	}

	memset(zeroflash_hcd->out.buf, 0, size);

	zeroflash_hcd->out.data_length = 0;

	if (syna_tcm_secure_memcpy(zeroflash_hcd->out.buf,
				zeroflash_hcd->out.buf_size,
				zeroflash_hcd->hdl_firmware, size)) {
		dev_err(dev, "%s error: (line %d) Failed to copy application firmware data\n",
				__func__, 1028);
		mutex_unlock(&zeroflash_hcd->out.buf_mutex);
		return -EINVAL;
	}

	zeroflash_hcd->out.data_length = size;

	retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
			zeroflash_hcd->pdt_entry[2] + ZEROFLASH_HDL_COMMAND_OFFSET,
			&code, 1);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write F$35 command\n",
				__func__, 1052);
		mutex_unlock(&zeroflash_hcd->out.buf_mutex);
		return retval;
	}

	retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
			zeroflash_hcd->pdt_entry[2] + ZEROFLASH_HDL_DATA_OFFSET,
			zeroflash_hcd->out.buf, zeroflash_hcd->out.data_length);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write application firmware data\n",
				__func__, 1063);
		mutex_unlock(&zeroflash_hcd->out.buf_mutex);
		return retval;
	}

	mutex_unlock(&zeroflash_hcd->out.buf_mutex);

	dev_notice(dev, "%s info: Application firmware downloaded\n", __func__);

	return 0;
}

static int zeroflash_do_f35_firmware_download(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	u8 data = 0;
	int retval = 0;

	if (tcm_hcd->irq_registered) {
		retval = tcm_hcd->enable_irq_fn(tcm_hcd, false, true);

		if (retval < 0)
			dev_err(dev, "%s error: (line %d) Failed to disable interrupt\n",
					__func__, 1089);
	}

	dev_notice(dev, "%s info: Prepare F35 firmware download\n", __func__);

	if (tcm_hcd->identify_info[1] == 4) {
		dev_err(dev, "%s error: (line %d) Incorrect uboot type, exit\n",
				__func__, 1098);
		goto reset;
	}

	retval = zeroflash_check_uboot();

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to find valid uboot\n",
				__func__, 1104);
		goto reset;
	}

	tcm_hcd->hdl_pending = 1;

	retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
			zeroflash_hcd->pdt_entry[3], &data, 1);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to read F$35 data\n",
				__func__, 1116);
		goto reset;
	}

	switch (data & ZEROFLASH_HDL_FUNCTION_MASK) {
	case ZEROFLASH_HDL_STATE_READY:
		zeroflash_hdl_retry_count = 0;
		break;
	case ZEROFLASH_HDL_STATE_RETRY:
		zeroflash_hdl_retry_count++;
		break;
	default:
		dev_err(dev, "%s error: (line %d) Microbootloader error code = 0x%02x\n",
				__func__, 1123, data & ZEROFLASH_HDL_FUNCTION_MASK);
		goto reset;
	}

	retval = zeroflash_get_fw_image();

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to get firmware image\n",
				__func__, 1137);
		goto reset;
	}

	dev_notice(dev, "%s info: Start of firmware download\n", __func__);

	retval = zeroflash_download_app_fw();

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to download application firmware, so reset tp \n",
				__func__, 1150);
		goto reset;
	}

	dev_notice(dev, "%s info: End of firmware download\n", __func__);

	goto enable_irq;

reset:
	gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio), 0);
	msleep(5);
	gpiod_set_raw_value(gpio_to_desc(bdata->reset_gpio), 1);
	msleep(5);

	zeroflash_hdl_retry_count++;

enable_irq:
	if (zeroflash_hdl_retry_count < ZEROFLASH_HDL_RETRY_LIMIT) {
		retval = tcm_hcd->enable_irq_fn(tcm_hcd, true, false);

		if (retval < 0)
			dev_err(dev, "%s error: (line %d) Failed to disable interrupt\n",
					__func__, 1171);
	} else {
		retval = tcm_hcd->enable_irq_fn(tcm_hcd, true, true);

		if (retval < 0)
			dev_err(dev, "%s error: (line %d) Failed to enable interrupt\n",
					__func__, 1184);
	}

	dev_dbg(dev, "%s info: Interrupt is disabled\n", __func__);

	return retval;
}

static int zeroflash_do_romboot_firmware_download(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	unsigned int size;
	u8 *payload = NULL;
	u8 *resp_buf = NULL;
	u32 resp_length = 0;
	u8 resp_code = 0;
	int retval = 0;

	dev_notice(dev, "%s info: Prepare ROMBOOT firmware download\n", __func__);

	tcm_hcd->hdl_pending = 1;

	if (!tcm_hcd->irq_registered) {
		retval = tcm_hcd->enable_irq_fn(tcm_hcd, true, false);

		if (retval < 0)
			dev_err(dev, "%s error: (line %d) Failed to enable interrupt\n",
					__func__, 1209);
	}

	pm_stay_awake(&tcm_hcd->pdev->dev);

	if (tcm_hcd->identify_info[1] != 4) {
		dev_err(dev, "%s error: (line %d) Not in romboot mode\n",
				__func__, 1217);
		tcm_hcd->hdl_pending = 0;
		retval = 0;
		goto out;
	}

	retval = zeroflash_get_fw_image();

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to request romboot.img\n",
				__func__, 1225);
		goto out;
	}

	size = zeroflash_hcd->hdl_firmware_size;

	dev_dbg(dev, "%s info: image_size = %d\n", __func__, size);

	payload = kmalloc(size + ZEROFLASH_ROMBOOT_HEADER_SIZE, GFP_KERNEL);

	memset(payload, 0, ZEROFLASH_ROMBOOT_HEADER_SIZE);

	payload[0] = (u8)(size >> 16);

	if (!zeroflash_hcd->hdl_firmware) {
		dev_err(dev, "%s error: (line %d) Failed to copy payload\n",
				__func__, 1251);
		goto out;
	}

	memcpy(payload + ZEROFLASH_ROMBOOT_HEADER_SIZE,
			zeroflash_hcd->hdl_firmware, size);

	dev_dbg(dev, "%s info: data_size_blocks: %d\n", __func__, size >> 4);

	retval = tcm_hcd->write_message_fn(tcm_hcd, CMD_ROMBOOT_DOWNLOAD,
			payload, size + ZEROFLASH_ROMBOOT_HEADER_SIZE,
			&resp_buf, &resp_length, &resp_code, NULL,
			ZEROFLASH_ROMBOOT_TIMEOUT_MS);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write command ROMBOOT DOWNLOAD\n",
				__func__, 1270);
	} else if (tcm_hcd->switch_mode_fn &&
			tcm_hcd->switch_mode_fn(tcm_hcd, MODE_LOAD_BOOTLOADER) < 0) {
		dev_err(dev, "%s error: (line %d) Failed to switch to bootloader",
				__func__, 1277);
	}

out:
	pm_relax(&tcm_hcd->pdev->dev);

	kfree(payload);
	kfree(resp_buf);

	return retval;
}

static int zeroflash_download_app_config(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	unsigned int size = zeroflash_hcd->app_config_size;
	unsigned int pad;
	unsigned int total;
	unsigned int version;
	u8 code = 0;
	int retval;

	if (!size) {
		dev_err(dev, "%s error: (line %d) No application config in image file\n",
				__func__, 826);
		goto abort;
	}

	pad = ((size & 7) == 0) ? ZEROFLASH_CONFIG_PADDING :
			10 - (size & 7);

retry:
	mutex_lock(&zeroflash_hcd->out.buf_mutex);

	total = size + pad;

	if (zeroflash_hcd->out.buf_size < total) {
		kfree(zeroflash_hcd->out.buf);
		zeroflash_hcd->out.buf = kmalloc(total, GFP_KERNEL);

		if (!zeroflash_hcd->out.buf) {
			pr_err("%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			pr_err("%s: Allocation size = %d\n", "syna_tcm_alloc_mem", total);
			zeroflash_hcd->out.buf_size = 0;
			dev_err(dev, "%s error: (line %d) Failed to allocate memory for application config\n",
					__func__, 841);
			retval = -ENOMEM;
			goto err_unlock;
		}

		zeroflash_hcd->out.buf_size = total;
	} else {
		total = zeroflash_hcd->out.buf_size;
	}

	memset(zeroflash_hcd->out.buf, 0, total);

	zeroflash_hcd->out.data_length = 0;

	version = (zeroflash_hcd->status >> 3) & 0x0f;

	if (version >= 3) {
		dev_err(dev, "%s error: (line %d) Invalid HDL version (%d)\n",
				__func__, 857, version);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.buf[0] = (u8)(0x101 >> (version * 8));
	zeroflash_hcd->out.buf[1] = ZEROFLASH_CONFIG_APP;

	if (syna_tcm_secure_memcpy(zeroflash_hcd->out.buf + 2,
				zeroflash_hcd->out.buf_size - 2,
				zeroflash_hcd->app_config, size)) {
		dev_err(dev, "%s error: (line %d) Failed to copy application config data\n",
				__func__, 870);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.data_length = size + pad;

	mutex_lock(&zeroflash_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, ZEROFLASH_CONFIG_COMMAND,
			zeroflash_hcd->out.buf, zeroflash_hcd->out.data_length,
			NULL, NULL, NULL, &code, 0);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write command %s\n",
				__func__, 891, "CMD_DOWNLOAD_CONFIG");
	}

	if (code != ZEROFLASH_HDL_STATE_DONE) {
		zeroflash_app_config_retry_count++;

		if (zeroflash_app_config_retry_count <= ZEROFLASH_CONFIG_RETRY_LIMIT) {
			mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
			mutex_unlock(&zeroflash_hcd->out.buf_mutex);
			goto retry;
		}

		goto err_unlock;
	}

	zeroflash_app_config_retry_count = 0;

	if (zeroflash_hcd->resp.buf && zeroflash_hcd->resp.buf_size > 1) {
		zeroflash_hcd->status = get_unaligned_le16(zeroflash_hcd->resp.buf);
		dev_notice(dev, "%s info: Application config downloaded\n", __func__);
		retval = 0;
	} else {
		dev_err(dev, "%s error: (line %d) Failed to copy firmware status\n",
				__func__, 908);
		retval = -EINVAL;
	}

err_unlock:
	mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
	mutex_unlock(&zeroflash_hcd->out.buf_mutex);

	return retval;

abort:
	tcm_hcd->hdl_pending = 0;
	dev_err(dev, "%s error: (line %d) Failed to download application config, abort\n",
			__func__, 946);

	return -EINVAL;
}

static int zeroflash_download_disp_config(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	unsigned int size = zeroflash_hcd->display_config_size;
	unsigned int pad = ZEROFLASH_CONFIG_PADDING;
	unsigned int total;
	unsigned int version;
	u8 code = 0;
	int retval;

	if (!size) {
		dev_err(dev, "%s error: (line %d) No display config in image file\n",
				__func__, 715);
		goto abort;
	}

retry:
	mutex_lock(&zeroflash_hcd->out.buf_mutex);

	total = size + pad;

	if (zeroflash_hcd->out.buf_size < total) {
		kfree(zeroflash_hcd->out.buf);
		zeroflash_hcd->out.buf = kmalloc(total, GFP_KERNEL);

		if (!zeroflash_hcd->out.buf) {
			pr_err("%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			pr_err("%s: Allocation size = %d\n", "syna_tcm_alloc_mem", total);
			zeroflash_hcd->out.buf_size = 0;
			dev_err(dev, "%s error: (line %d) Failed to allocate memory for display config\n",
					__func__, 726);
			retval = -ENOMEM;
			goto err_unlock;
		}

		zeroflash_hcd->out.buf_size = total;
	} else {
		total = zeroflash_hcd->out.buf_size;
	}

	memset(zeroflash_hcd->out.buf, 0, total);

	zeroflash_hcd->out.data_length = 0;

	version = (zeroflash_hcd->status >> 3) & 0x0f;

	if (version >= 3) {
		dev_err(dev, "%s error: (line %d) Invalid HDL version (%d)\n",
				__func__, 742, version);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.buf[0] = (u8)(0x101 >> (version * 8));
	zeroflash_hcd->out.buf[1] = ZEROFLASH_CONFIG_DISPLAY;

	if (syna_tcm_secure_memcpy(zeroflash_hcd->out.buf + 2,
				zeroflash_hcd->out.buf_size - 2,
				zeroflash_hcd->display_config, size)) {
		dev_err(dev, "%s error: (line %d) Failed to copy display config data\n",
				__func__, 755);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.data_length = size + pad;

	mutex_lock(&zeroflash_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, ZEROFLASH_CONFIG_COMMAND,
			zeroflash_hcd->out.buf, zeroflash_hcd->out.data_length,
			NULL, NULL, NULL, &code, 0);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write command %s\n",
				__func__, 775, "CMD_DOWNLOAD_CONFIG");
	}

	if (code != ZEROFLASH_HDL_STATE_DONE) {
		zeroflash_display_config_retry_count++;

		if (zeroflash_display_config_retry_count <= ZEROFLASH_CONFIG_RETRY_LIMIT) {
			mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
			mutex_unlock(&zeroflash_hcd->out.buf_mutex);
			goto retry;
		}

		goto err_unlock;
	}

	zeroflash_display_config_retry_count = 0;

	if (zeroflash_hcd->resp.buf && zeroflash_hcd->resp.buf_size > 1) {
		zeroflash_hcd->status = get_unaligned_le16(zeroflash_hcd->resp.buf);
		dev_notice(dev, "%s info: Display config downloaded\n", __func__);
		retval = 0;
	} else {
		dev_err(dev, "%s error: (line %d) Failed to copy firmware status\n",
				__func__, 792);
		retval = -EINVAL;
	}

err_unlock:
	mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
	mutex_unlock(&zeroflash_hcd->out.buf_mutex);

	return retval;

abort:
	tcm_hcd->hdl_pending = 0;
	dev_err(dev, "%s error: (line %d) Failed to download display config, abort\n",
			__func__, 957);

	return -EINVAL;
}

static int zeroflash_download_open_short_config(void)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	unsigned int size = zeroflash_hcd->open_short_config_size;
	unsigned int pad = ZEROFLASH_CONFIG_PADDING;
	unsigned int total;
	unsigned int version;
	u8 code = 0;
	int retval;

	if (!size) {
		dev_err(dev, "%s error: (line %d) No open_short config in image file\n",
				__func__, 605);
		goto abort;
	}

retry:
	mutex_lock(&zeroflash_hcd->out.buf_mutex);

	total = size + pad;

	if (zeroflash_hcd->out.buf_size < total) {
		kfree(zeroflash_hcd->out.buf);
		zeroflash_hcd->out.buf = kmalloc(total, GFP_KERNEL);

		if (!zeroflash_hcd->out.buf) {
			pr_err("%s: Failed to allocate memory\n", "syna_tcm_alloc_mem");
			pr_err("%s: Allocation size = %d\n", "syna_tcm_alloc_mem", total);
			zeroflash_hcd->out.buf_size = 0;
			dev_err(dev, "%s error: (line %d) Failed to allocate memory for open_short config\n",
					__func__, 616);
			retval = -ENOMEM;
			goto err_unlock;
		}

		zeroflash_hcd->out.buf_size = total;
	} else {
		total = zeroflash_hcd->out.buf_size;
	}

	memset(zeroflash_hcd->out.buf, 0, total);

	zeroflash_hcd->out.data_length = 0;

	version = (zeroflash_hcd->status >> 3) & 0x0f;

	if (version >= 3) {
		dev_err(dev, "%s error: (line %d) Invalid HDL version (%d)\n",
				__func__, 632, version);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.buf[0] = (u8)(0x101 >> (version * 8));
	zeroflash_hcd->out.buf[1] = ZEROFLASH_CONFIG_OPEN_SHORT;

	if (syna_tcm_secure_memcpy(zeroflash_hcd->out.buf + 2,
				zeroflash_hcd->out.buf_size - 2,
				zeroflash_hcd->open_short_config, size)) {
		dev_err(dev, "%s error: (line %d) Failed to copy open_short config data\n",
				__func__, 645);
		retval = -EINVAL;
		goto err_unlock;
	}

	zeroflash_hcd->out.data_length = size + pad;

	mutex_lock(&zeroflash_hcd->resp.buf_mutex);

	retval = tcm_hcd->write_message_fn(tcm_hcd, ZEROFLASH_CONFIG_COMMAND,
			zeroflash_hcd->out.buf, zeroflash_hcd->out.data_length,
			NULL, NULL, NULL, &code, 0);

	if (retval < 0) {
		dev_err(dev, "%s error: (line %d) Failed to write command %s\n",
				__func__, 665, "CMD_DOWNLOAD_CONFIG");
	}

	if (code != ZEROFLASH_HDL_STATE_DONE) {
		zeroflash_open_short_config_retry_count++;

		if (zeroflash_open_short_config_retry_count <= ZEROFLASH_CONFIG_RETRY_LIMIT) {
			mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
			mutex_unlock(&zeroflash_hcd->out.buf_mutex);
			goto retry;
		}

		goto err_unlock;
	}

	zeroflash_open_short_config_retry_count = 0;

	if (zeroflash_hcd->resp.buf && zeroflash_hcd->resp.buf_size > 1) {
		zeroflash_hcd->status = get_unaligned_le16(zeroflash_hcd->resp.buf);
		dev_notice(dev, "%s info: open_short config downloaded\n", __func__);
		retval = 0;
	} else {
		dev_err(dev, "%s error: (line %d) Failed to copy firmware status\n",
				__func__, 682);
		retval = -EINVAL;
	}

err_unlock:
	mutex_unlock(&zeroflash_hcd->resp.buf_mutex);
	mutex_unlock(&zeroflash_hcd->out.buf_mutex);

	return retval;

abort:
	tcm_hcd->hdl_pending = 0;
	dev_err(dev, "%s error: (line %d) Failed to download open_short config, abort\n",
			__func__, 970);

	return -EINVAL;
}

static void zeroflash_download_config_work(struct work_struct *work)
{
	struct syna_tcm_hcd *tcm_hcd = zeroflash_hcd->tcm_hcd;
	struct device *dev = &tcm_hcd->pdev->dev;
	u16 status;
	int retval = 0;

	if (zeroflash_get_fw_image() < 0) {
		dev_err(dev, "%s error: (line %d) Failed to get firmware image\n",
				__func__, 934);
		return;
	}

	dev_notice(dev, "%s info: Start of config download\n", __func__);

	status = zeroflash_hcd->status;

	if (status & ZEROFLASH_STATUS_APP_CONFIG) {
		dev_notice(dev, "%s info: Downloading application config\n", __func__);
		retval = zeroflash_download_app_config();
	} else if (status & ZEROFLASH_STATUS_DISPLAY) {
		dev_notice(dev, "%s info: Downloading display config\n", __func__);
		retval = zeroflash_download_disp_config();
	} else if ((status & ZEROFLASH_STATUS_OPEN_SHORT) &&
			zeroflash_hcd->open_short_available) {
		dev_notice(dev, "%s info: Downloading open_short config\n", __func__);
		retval = zeroflash_download_open_short_config();
	} else {
		goto done;
	}

	if (retval < 0)
		return;

done:
	dev_notice(dev, "%s info: End of config download\n", __func__);

	if (tcm_hcd->shut_down)
		return;

	status = zeroflash_hcd->status;

	if (status & ZEROFLASH_STATUS_CONFIGS) {
		if (!tcm_hcd->hdl_pending || tcm_hcd->shut_down)
			return;

		queue_work(zeroflash_hcd->workqueue,
				&zeroflash_hcd->download_config_work);

		return;
	} else if ((status & ZEROFLASH_STATUS_OPEN_SHORT) &&
			zeroflash_hcd->open_short_available) {
		if (!tcm_hcd->hdl_pending || tcm_hcd->shut_down)
			return;

		queue_work(zeroflash_hcd->workqueue,
				&zeroflash_hcd->download_config_work);

		return;
	}

	if (!tcm_hcd->hdl_pending)
		return;

	if (!tcm_hcd->dispatch_state) {
		tcm_hcd->dispatch_state = 2;
		queue_work(tcm_hcd->helper_wq, &tcm_hcd->helper_work);
	}

	tcm_hcd->hdl_pending = 0;
}

static int zeroflash_syncbox(struct syna_tcm_hcd *tcm_hcd)
{
	unsigned int code;
	u16 status;

	if (!zeroflash_hcd)
		return 0;

	code = tcm_hcd->dispatch_report_code;

	if (code == ZEROFLASH_REPORT_F35) {
		zeroflash_do_f35_firmware_download();
		return 0;
	}

	if (code == ZEROFLASH_REPORT_ROMBOOT) {
		zeroflash_do_romboot_firmware_download();
		return 0;
	}

	if (code != ZEROFLASH_REPORT_STATUS)
		return 0;

	if (!tcm_hcd->report_buffer.buf ||
			tcm_hcd->report_buffer.buf_size <= 1) {
		pr_err("%s: src_size = %d, dest_size = %d, count = %d\n",
				"secure_memcpy", 2, 2, 2);
		goto error;
	}

	status = get_unaligned_le16(tcm_hcd->report_buffer.buf);

	zeroflash_hcd->status = status;

	if (status & ZEROFLASH_STATUS_CONFIGS) {
		if (!tcm_hcd->hdl_pending)
			return 0;

		if (tcm_hcd->shut_down)
			return 0;

		queue_work(zeroflash_hcd->workqueue,
				&zeroflash_hcd->download_config_work);

		return 0;
	}

	if (status & ZEROFLASH_STATUS_OPEN_SHORT) {
		if (zeroflash_hcd->open_short_available) {
			if (!tcm_hcd->hdl_pending)
				return 0;

			if (tcm_hcd->shut_down)
				return 0;

			queue_work(zeroflash_hcd->workqueue,
					&zeroflash_hcd->download_config_work);

			return 0;
		}
	}

	if (!tcm_hcd->hdl_pending)
		return 0;

	if (!tcm_hcd->dispatch_state) {
		tcm_hcd->dispatch_state = 2;
		queue_work(tcm_hcd->helper_wq, &tcm_hcd->helper_work);
	}

	tcm_hcd->hdl_pending = 0;

	return 0;

error:
	dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy firmware status\n",
			__func__, 1421);

	return -EINVAL;
}

static void zeroflash_remove_buffers(void)
{
	if (!zeroflash_hcd->resp.init) {
		kfree(zeroflash_hcd->resp.buf);
		zeroflash_hcd->resp.buf_size = 0;
	}

	if (!zeroflash_hcd->out.init) {
		kfree(zeroflash_hcd->out.buf);
		zeroflash_hcd->out.buf_size = 0;
	}
}

static int zeroflash_init(struct syna_tcm_hcd *tcm_hcd)
{
	struct device *dev = &tcm_hcd->pdev->dev;
	int retval = 0;

	zeroflash_hcd = NULL;

	if (!tcm_hcd->irq_enabled)
		return 0;

	zeroflash_hcd = kzalloc(sizeof(*zeroflash_hcd), GFP_KERNEL);

	if (!zeroflash_hcd) {
		dev_err(dev, "%s error: (line %d) Failed to allocate memory for zeroflash_hcd\n",
				__func__, 1304);
		return -ENOMEM;
	}

	zeroflash_hcd->tcm_hcd = tcm_hcd;
	zeroflash_hcd->image = NULL;
	zeroflash_hcd->host_download_supported = 0;
	zeroflash_hcd->uboot_found = 0;
	zeroflash_hcd->open_short_available = 0;

	mutex_init(&zeroflash_hcd->out.buf_mutex);
	zeroflash_hcd->out.init = 0;

	mutex_init(&zeroflash_hcd->resp.buf_mutex);
	zeroflash_hcd->resp.init = 0;

	zeroflash_hcd->workqueue = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM, "syna_tcm_zeroflash");

	INIT_WORK(&zeroflash_hcd->download_config_work,
			zeroflash_download_config_work);

	zeroflash_hcd->kobj = kobject_create_and_add("zeroflash",
			tcm_hcd->sysfs_root);

	if (!zeroflash_hcd->kobj) {
		dev_err(dev, "%s error: (line %d) Failed to create sysfs directory\n",
				__func__, 1329);
		return -EINVAL;
	}

	retval = sysfs_create_file(zeroflash_hcd->kobj, &dev_attr_hdl.attr);

	if (retval < 0)
		dev_err(dev, "%s error: (line %d) Failed to create sysfs file\n",
				__func__, 1338);

	if (!tcm_hcd->irq_enabled)
		return retval;

	switch (tcm_hcd->mode) {
	case ZEROFLASH_MODE_ROMBOOT:
		zeroflash_do_romboot_firmware_download();
		break;
	case ZEROFLASH_MODE_HDL:
		zeroflash_do_f35_firmware_download();
		break;
	default:
		dev_err(dev, "%s error: (line %d) Failed to find valid HDL state (%d)\n",
				__func__, 1355, tcm_hcd->mode);
		break;
	}

	return retval;
}

static int zeroflash_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (zeroflash_hcd) {
		if (zeroflash_hcd->fw)
			release_firmware(zeroflash_hcd->fw);

		sysfs_remove_file(zeroflash_hcd->kobj, &dev_attr_hdl.attr);
		kobject_put(zeroflash_hcd->kobj);

		cancel_work_sync(&zeroflash_hcd->download_config_work);
		flush_workqueue(zeroflash_hcd->workqueue);
		destroy_workqueue(zeroflash_hcd->workqueue);

		zeroflash_remove_buffers();

		kfree(zeroflash_hcd);
		zeroflash_hcd = NULL;
	}

	complete(&zeroflash_remove_complete);

	return 0;
}

static int zeroflash_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (zeroflash_hcd)
		return 0;

	if (!tcm_hcd->irq_enabled)
		return 0;

	return zeroflash_init(tcm_hcd);
}

static struct syna_tcm_module_desc zeroflash_module = {
	.id = ZEROFLASH_MODULE_ID,
	.init = zeroflash_init,
	.remove = zeroflash_remove,
	.handler = zeroflash_syncbox,
	.reinit = zeroflash_reinit,
};

int zeroflash_module_init(void)
{
	return syna_tcm_add_module(&zeroflash_module, true);
}

void zeroflash_module_exit(void)
{
	syna_tcm_add_module(&zeroflash_module, false);

	wait_for_completion(&zeroflash_remove_complete);
}
