// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm (TCM) driver for the TD4320 touch controller.
 *
 * Reflashing support.  The application firmware, the application config, the
 * display config, the production test firmware and the boot config all live
 * in the touch controller's own flash.  This module owns the sysfs interface
 * that is used to push a signed image into the part and the low level
 * read/erase/write helpers that talk to the flash through TouchComm.
 *
 * Reconstructed from the vendor module synaptics_td4320_spi_ts.ko.
 */

#include "synaptics_tcm_core.h"

#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/version.h>

#define REFLASH_MODULE_ID		0x01

/* flash commands, as sent through the message interface */
#define CMD_ERASE_FLASH			0x11
#define CMD_WRITE_FLASH			0x12
#define CMD_READ_FLASH			0x13

/* magic values of the image file and of every section inside it */
#define REFLASH_IMAGE_MAGIC		0x4818472b
#define REFLASH_SECTION_MAGIC		0x7c05e516

/* the image file starts with the magic, the section count, then the table */
#define REFLASH_IMAGE_HEADER_SIZE	0x08
#define REFLASH_SECTION_HEADER_SIZE	0x24
/* magic, up to 20 bytes of name, the 4 byte flash address, size and crc */
#define REFLASH_SECTION_NAME_SIZE	0x14

/* a boot config slot holds 8 bytes and the bootloader parses 15 of them */
#define REFLASH_BOOT_CONFIG_SLOT_SIZE	0x08
#define REFLASH_BOOT_CONFIG_SLOTS	15
#define REFLASH_BOOT_CONFIG_SIZE	(REFLASH_BOOT_CONFIG_SLOT_SIZE * \
					 REFLASH_BOOT_CONFIG_SLOTS)

/* the customer serialisation area is a fixed 128 byte block */
#define REFLASH_CS_SIZE			0x80

/* the smallest useful application config carries the firmware and config ids */
#define REFLASH_APP_CONFIG_MIN_SIZE	0x22

/* how much of the image is copied below the image buffer at once */
#define REFLASH_IMAGE_BUF_SIZE		0x80000

#define REFLASH_ERASE_TIMEOUT_MS	500
#define REFLASH_WRITE_TIMEOUT_MS	20

struct syna_tcm_reflash_hcd {
	u8 update_firmware;		/* +0x000 from the reflash store input */
	u8 disp_config_present;		/* +0x001 set by the image parser */
	u8 do_reflash;			/* +0x002 image was pushed by hand */
	u8 pad0[0x05];			/* +0x003 */
	u8 *image;			/* +0x008 */
	u8 *image_buf;			/* +0x010 */
	u32 image_size;			/* +0x018 */
	u32 page_size;			/* +0x01c */
	u32 write_block_size;		/* +0x020 */
	u32 max_write_payload_size;	/* +0x024 */
	struct firmware *fw;		/* +0x028 */
	struct mutex reflash_mutex;	/* +0x030 */
	struct kobject *kobj;		/* +0x050 */
	struct kobject *custom_kobj;	/* +0x058 */
	u8 pad4[0x38];			/* +0x060 */
	u8 *boot_config;		/* +0x098 */
	u32 boot_config_size;		/* +0x0a0 */
	u32 boot_config_addr;		/* +0x0a4 */
	u8 *app_firmware;		/* +0x0a8 */
	u32 app_firmware_size;		/* +0x0b0 */
	u32 app_firmware_addr;		/* +0x0b4 */
	u8 *prod_test_firmware;		/* +0x0b8 */
	u32 prod_test_firmware_size;	/* +0x0c0 */
	u32 prod_test_firmware_addr;	/* +0x0c4 */
	u8 *app_config;			/* +0x0c8 */
	u32 app_config_size;		/* +0x0d0 */
	u32 app_config_addr;		/* +0x0d4 */
	u8 *disp_config;		/* +0x0d8 */
	u32 disp_config_size;		/* +0x0e0 */
	u32 disp_config_addr;		/* +0x0e4 */
	struct syna_tcm_buf out;	/* +0x0e8 */
	struct syna_tcm_buf resp;	/* +0x120 */
	struct syna_tcm_buf read;	/* +0x158 */
	struct syna_tcm_hcd *tcm_hcd;	/* +0x190 */
};

static struct syna_tcm_reflash_hcd *reflash_hcd;

static DECLARE_COMPLETION(reflash_remove_complete);

/* the six sections the image parser knows about */
static const char *const reflash_section_boot_config = "BOOT_CONFIG";
static const char *const reflash_section_app_code = "APP_CODE";
static const char *const reflash_section_app_prod_test = "APP_PROD_TEST";
static const char *const reflash_section_app_config = "APP_CONFIG";
static const char *const reflash_section_display = "DISPLAY";

/*
 * A copy of the two core helpers.  The vendor builds every translation unit
 * with its own inline copy, so the strings they log show up several times in
 * the module; keep them here instead of calling into the core.
 */
static int reflash_secure_memcpy(unsigned char *dest, unsigned int dest_size,
				 const unsigned char *src, unsigned int src_size)
{
	if (dest == NULL)
		return -EINVAL;

	if (src == NULL)
		return -EINVAL;

	if (dest_size < src_size) {
		pr_err("%s: Destination size %u is smaller than source size %u\n",
		       "secure_memcpy", dest_size, src_size);
		return -EINVAL;
	}

	memcpy(dest, src, src_size);

	return 0;
}

static int reflash_alloc_mem(struct syna_tcm_buf *buf, unsigned int size)
{
	if (buf->buf_size >= size) {
		memset(buf->buf, 0, buf->buf_size);
		return 0;
	}

	kfree(buf->buf);

	buf->buf = kmalloc(size, GFP_KERNEL);
	if (!buf->buf) {
		buf->buf_size = 0;
		return -ENOMEM;
	}

	buf->buf_size = size;
	memset(buf->buf, 0, buf->buf_size);

	return 0;
}

static ssize_t reflash_store_error(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	pr_err("%s: Attribute not writable\n", __func__);

	return -1;
}

static ssize_t reflash_show_error(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	pr_err("%s: Attribute not readable\n", __func__);

	return -1;
}

/*
 * Tell the firmware where every config lives in its flash and cache the
 * geometry the read/erase/write helpers need.  Everything below works in
 * "flash address units", one unit is two bytes, hence the shifts.
 */
static int reflash_set_up_flash_access(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = tcm_identify(tcm_hcd, true);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do identification\n",
			__func__, 0x2a1);
		return retval;
	}

	if (tcm_hcd->identify_info[1] == MODE_LOAD_APPLICATION) {
		retval = tcm_switch_mode(tcm_hcd, MODE_LOAD_BOOTLOADER);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to enter bootloader mode\n",
				__func__, 0x2a9);
			return retval;
		}
	}

	reflash_hcd->write_block_size = get_unaligned_le16(
					&tcm_hcd->boot_info[0x04]) & 0xff;
	reflash_hcd->write_block_size <<= 1;
	reflash_hcd->page_size =
		((unsigned int)tcm_hcd->boot_info[0x05] << 1) |
		((unsigned int)tcm_hcd->boot_info[0x06] << 9);
	reflash_hcd->max_write_payload_size =
		get_unaligned_le16(&tcm_hcd->boot_info[0x07]);

	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Write block size = %d\n",
		__func__, reflash_hcd->write_block_size);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Page size = %d\n",
		__func__, reflash_hcd->page_size);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Max write payload size = %d\n",
		__func__, reflash_hcd->max_write_payload_size);

	if (reflash_hcd->write_block_size >
	    (tcm_hcd->payload_length - 5)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Write block size greater than available chunk space\n",
			__func__, 0x2c5);
		return -EINVAL;
	}

	return 0;
}

/*
 * Erase a page range.  When both numbers fit in one byte the command only
 * carries two bytes, otherwise the page count is appended as a little endian
 * half word.  The firmware only accepts this in bootloader mode.
 */
static int reflash_erase_flash(unsigned int page_start, unsigned int page_count)
{
	int retval;
	unsigned char payload[4] = { 0 };
	unsigned int payload_length;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	payload[0] = (unsigned char)(page_start & 0xff);
	payload_length = 2;

	if ((page_start | page_count) >= 0x100) {
		payload[1] = (unsigned char)((page_start >> 8) & 0xff);
		payload[2] = (unsigned char)(page_count & 0xff);
		payload[3] = (unsigned char)((page_count >> 8) & 0xff);
		payload_length = 4;
	}

	mutex_lock(&reflash_hcd->resp.buf_mutex);

	retval = tcm_write_message(tcm_hcd, CMD_ERASE_FLASH, payload,
				   payload_length, &reflash_hcd->resp.buf,
				   &reflash_hcd->resp.buf_size,
				   &reflash_hcd->resp.data_length, NULL,
				   REFLASH_ERASE_TIMEOUT_MS);

	mutex_unlock(&reflash_hcd->resp.buf_mutex);

	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0x617, "CMD_ERASE_FLASH");
		return retval;
	}

	return 0;
}

/*
 * The address the firmware takes is a byte offset, the erase command works on
 * whole pages instead, so round both ends outwards.
 */
static int reflash_erase(unsigned int addr, unsigned int length)
{
	int retval;
	unsigned int page_start;
	unsigned int page_count;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	page_start = addr / reflash_hcd->page_size;
	page_count = (length + reflash_hcd->page_size - 1) /
		     reflash_hcd->page_size;

	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Page start = %d (0x%04x)\n",
		__func__, page_start, page_start);
	dev_dbg(&tcm_hcd->pdev->dev, "%s info: Page count = %d (0x%04x)\n",
		__func__, page_count, page_count);

	retval = reflash_erase_flash(page_start, page_count);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase pages, addr = 0x%04x, count = %d\n",
			__func__, 0x639, addr, page_count);
		return retval;
	}

	return 0;
}

/*
 * Write a chunk of the image.  The first two payload bytes are the write block
 * the data goes to, the firmware then programs as much as fits into one
 * TouchComm chunk.  Both buffers are held for the whole loop so a single
 * reflash cannot interleave with another flash user.
 */
static int reflash_write_flash(unsigned int addr, const unsigned char *data,
			       unsigned int length)
{
	int retval = 0;
	unsigned int offset = 0;
	unsigned int chunk;
	unsigned int size;
	unsigned int total;
	unsigned int block;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	chunk = ((tcm_hcd->payload_length - 5) /
		 reflash_hcd->write_block_size) * reflash_hcd->write_block_size;

	if (chunk > reflash_hcd->max_write_payload_size)
		chunk = reflash_hcd->max_write_payload_size;

	mutex_lock(&reflash_hcd->out.buf_mutex);
	mutex_lock(&reflash_hcd->resp.buf_mutex);

	while (length) {
		size = MIN(length, chunk);
		total = size + 2;

		retval = reflash_alloc_mem(&reflash_hcd->out, total);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for reflash_hcd->out.buf\n",
				__func__, 0x561);
			goto exit;
		}

		memset(reflash_hcd->out.buf, 0, reflash_hcd->out.buf_size);
		reflash_hcd->out.data_length = 0;

		block = (offset + addr) / reflash_hcd->write_block_size;
		reflash_hcd->out.buf[0] = (unsigned char)(block & 0xff);
		reflash_hcd->out.buf[1] = (unsigned char)((block >> 8) & 0xff);

		if (reflash_secure_memcpy(reflash_hcd->out.buf + 2,
					  reflash_hcd->out.buf_size - 2,
					  data + offset, size)) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to copy write data\n",
				__func__, 0x573);
			retval = -EINVAL;
			goto exit;
		}

		retval = tcm_write_message(tcm_hcd, CMD_WRITE_FLASH,
					   reflash_hcd->out.buf, total,
					   &reflash_hcd->resp.buf,
					   &reflash_hcd->resp.buf_size,
					   &reflash_hcd->resp.data_length, NULL,
					   REFLASH_WRITE_TIMEOUT_MS);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write command %s\n",
				__func__, 0x585, "CMD_WRITE_FLASH");
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Flash address = 0x%08x\n",
				__func__, 0x588, offset + addr);
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Data length = %d\n",
				__func__, 0x58b, size);
			goto exit;
		}

		length -= size;
		offset += size;
	}

exit:
	mutex_unlock(&reflash_hcd->resp.buf_mutex);
	mutex_unlock(&reflash_hcd->out.buf_mutex);

	return retval;
}

/*
 * Read a range of flash.  The request carries the byte address in four bytes
 * and the length in two, both shifted right by one because the firmware counts
 * in sixteen bit words.
 */
static int reflash_read_flash(unsigned int addr, unsigned int length,
			      unsigned char *data)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&reflash_hcd->out.buf_mutex);

	retval = reflash_alloc_mem(&reflash_hcd->out, 6);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to allocate memory for reflash_hcd->out.buf\n",
			__func__, 0x3e9);
		mutex_unlock(&reflash_hcd->out.buf_mutex);
		return -ENOMEM;
	}

	memset(reflash_hcd->out.buf, 0, reflash_hcd->out.buf_size);
	reflash_hcd->out.data_length = 0;

	reflash_hcd->out.buf[0] = (unsigned char)((addr >> 1) & 0xff);
	reflash_hcd->out.buf[1] = (unsigned char)((addr >> 9) & 0xff);
	reflash_hcd->out.buf[2] = (unsigned char)((addr >> 17) & 0xff);
	reflash_hcd->out.buf[3] = (unsigned char)((addr >> 25) & 0xff);
	reflash_hcd->out.buf[4] = (unsigned char)((length >> 1) & 0x7fffffff);
	reflash_hcd->out.buf[5] = (unsigned char)((length >> 9) & 0x7fffff);

	mutex_lock(&reflash_hcd->resp.buf_mutex);

	retval = tcm_write_message(tcm_hcd, CMD_READ_FLASH,
				   reflash_hcd->out.buf, 6,
				   &reflash_hcd->resp.buf,
				   &reflash_hcd->resp.buf_size,
				   &reflash_hcd->resp.data_length, NULL, 0);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write command %s\n",
			__func__, 0x406, "CMD_READ_FLASH");
		goto exit_resp;
	}

	mutex_unlock(&reflash_hcd->out.buf_mutex);

	if (reflash_hcd->resp.data_length != length) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read requested length\n",
			__func__, 0x410);
		retval = -EIO;
		goto exit_resp;
	}

	if (reflash_secure_memcpy(data, reflash_hcd->resp.buf_size,
				  reflash_hcd->resp.buf, length)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy read data\n",
			__func__, 0x41c);
		retval = -EINVAL;
		goto exit_resp;
	}

	mutex_unlock(&reflash_hcd->resp.buf_mutex);

	return 0;

exit_resp:
	mutex_unlock(&reflash_hcd->resp.buf_mutex);

	return retval;
}

/*
 * Read one of the config areas out of the flash and hand it back to the
 * caller.  The address and length of the three custom areas are only known to
 * the firmware, so those are asked for first; everything else is described by
 * the boot and application info blocks.
 */
int reflash_read_data(enum flash_area area, bool run_application,
		      struct syna_tcm_buf *output)
{
	int retval;
	unsigned int offset = 0;
	unsigned int size = 0;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	if ((area >= FLASH_AREA_CUSTOM_LCM) && (area <= FLASH_AREA_BOOT_CONFIG)) {
		retval = tcm_get_data_location(tcm_hcd, area, &offset, &size);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get data location\n",
				__func__, 0x43b);
			goto exit;
		}
	}

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x446);
		goto exit;
	}

	switch (area) {
	case FLASH_AREA_CUSTOMER_SERIALIZATION:
		offset = get_unaligned_le16(&tcm_hcd->boot_info[0x0c]) *
			 reflash_hcd->write_block_size;
		size = REFLASH_CS_SIZE;
		break;
	case FLASH_AREA_APP_CONFIG:
		offset = get_unaligned_le16(&tcm_hcd->app_info[0x08]) *
			 reflash_hcd->write_block_size;
		size = get_unaligned_le16(&tcm_hcd->app_info[0x0a]) *
		       reflash_hcd->write_block_size;
		break;
	case FLASH_AREA_DISP_CONFIG:
		offset = get_unaligned_le32(&tcm_hcd->boot_info[0x10]) *
			 reflash_hcd->write_block_size;
		size = get_unaligned_le16(&tcm_hcd->boot_info[0x14]) *
		       reflash_hcd->write_block_size;
		break;
	case FLASH_AREA_CUSTOM_OTP:
		offset = get_unaligned_le16(&tcm_hcd->boot_info[0x1c]) *
			 reflash_hcd->write_block_size;
		size = get_unaligned_le16(&tcm_hcd->boot_info[0x1e]) *
		       reflash_hcd->write_block_size;
		break;
	case FLASH_AREA_CUSTOM_LCM:
	case FLASH_AREA_CUSTOM_OEM:
	case FLASH_AREA_BOOT_CONFIG:
		offset *= reflash_hcd->write_block_size;
		size *= reflash_hcd->write_block_size;
		break;
	default:
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data area\n",
			__func__, 0x46c);
		retval = -EINVAL;
		goto exit;
	}

	if (!offset || !size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Data area unavailable\n",
			__func__, 0x473);
		retval = -EINVAL;
		goto exit;
	}

	mutex_lock(&reflash_hcd->read.buf_mutex);

	retval = reflash_alloc_mem(&reflash_hcd->read, size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to allocate memory for reflash_hcd->read.buf\n",
			__func__, 0x47f);
		goto exit_read;
	}

	memset(reflash_hcd->read.buf, 0, reflash_hcd->read.buf_size);
	reflash_hcd->read.data_length = 0;

	retval = reflash_read_flash(offset, size, reflash_hcd->read.buf);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read from flash\n",
			__func__, 0x487);
		goto exit_read;
	}

	reflash_hcd->read.data_length = size;

	if (output) {
		retval = reflash_alloc_mem(output, size);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to allocate memory for output->buf\n",
				__func__, 0x494);
			goto exit_read;
		}

		memset(output->buf, 0, output->buf_size);
		output->data_length = 0;

		if (reflash_secure_memcpy(output->buf, output->buf_size,
					  reflash_hcd->read.buf,
					  reflash_hcd->read.data_length)) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to copy read data\n",
				__func__, 0x4a0);
			retval = -EINVAL;
			goto exit_read;
		}

		output->data_length = size;
	}

	mutex_unlock(&reflash_hcd->read.buf_mutex);

	retval = 0;

exit:
	if (run_application) {
		if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to run application firmware\n",
				__func__, 0x4b2);
	}

	return retval;

exit_read:
	mutex_unlock(&reflash_hcd->read.buf_mutex);

	goto exit;
}

/*
 * Walk the section table of the image file and record where every section
 * lives.  Each section carries its own crc32, computed over the payload with
 * the usual complemented convention, and a name that tells us which partition
 * the payload belongs to.
 */
static int reflash_parse_fw_image(void)
{
	int retval;
	unsigned int section_count;
	unsigned int index;
	unsigned int section_offset;
	unsigned int section_crc;
	unsigned int section_size;
	unsigned int section_addr;
	unsigned int crc;
	unsigned char *section;
	unsigned char *payload;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	reflash_hcd->disp_config_present = 0;

	if (get_unaligned_le32(reflash_hcd->image) != REFLASH_IMAGE_MAGIC) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid image file magic value\n",
			__func__, 0x2e6);
		return -EINVAL;
	}

	memset(&reflash_hcd->boot_config, 0,
	       (unsigned long)&reflash_hcd->out - (unsigned long)&reflash_hcd->boot_config);

	section_count = get_unaligned_le32(reflash_hcd->image + 4);
	section_offset = REFLASH_IMAGE_HEADER_SIZE;

	while (section_count) {
		section = reflash_hcd->image +
			  get_unaligned_le32(reflash_hcd->image + section_offset);

		section_offset += 4;
		section_count--;

		if (get_unaligned_le32(section) != REFLASH_SECTION_MAGIC)
			continue;

		section_addr = section[0x18] << 1;
		section_addr |= section[0x19] << 9;
		section_addr |= section[0x1a] << 17;
		section_addr |= (section[0x1b] & 0x7f) << 25;

		section_size = get_unaligned_le32(section + 0x1c);
		section_crc = get_unaligned_le32(section + 0x20);
		payload = section + REFLASH_SECTION_HEADER_SIZE;

		if (!strncmp((const char *)section + 4,
			     reflash_section_boot_config,
			     strlen(reflash_section_boot_config))) {
			crc = ~crc32_le(~0, payload, section_size);
			if (crc != section_crc) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Boot config checksum error\n",
					__func__, 0x302);
				return -EINVAL;
			}

			reflash_hcd->boot_config = payload;
			reflash_hcd->boot_config_size = section_size;
			reflash_hcd->boot_config_addr = section_addr;

			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Boot config size = %d\n",
				__func__, section_size);
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Boot config flash address = 0x%08x\n",
				__func__, section_addr);
		} else if (!strncmp((const char *)section + 4,
				    reflash_section_app_code,
				    strlen(reflash_section_app_code))) {
			crc = ~crc32_le(~0, payload, section_size);
			if (crc != section_crc) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Application firmware checksum error\n",
					__func__, 0x313);
				return -EINVAL;
			}

			reflash_hcd->app_firmware = payload;
			reflash_hcd->app_firmware_size = section_size;
			reflash_hcd->app_firmware_addr = section_addr;

			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Application firmware size = %d\n",
				__func__, section_size);
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Application firmware flash address = 0x%08x\n",
				__func__, section_addr);
		} else if (!strncmp((const char *)section + 4,
				    reflash_section_app_prod_test,
				    strlen(reflash_section_app_prod_test))) {
			crc = ~crc32_le(~0, payload, section_size);
			if (crc != section_crc) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Production test firmware checksum error\n",
					__func__, 0x324);
				return -EINVAL;
			}

			reflash_hcd->prod_test_firmware = payload;
			reflash_hcd->prod_test_firmware_size = section_size;
			reflash_hcd->prod_test_firmware_addr = section_addr;

			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Production test firmware size = %d\n",
				__func__, section_size);
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Production test firmware flash address = 0x%08x\n",
				__func__, section_addr);
		} else if (!strncmp((const char *)section + 4,
				    reflash_section_app_config,
				    strlen(reflash_section_app_config))) {
			crc = ~crc32_le(~0, payload, section_size);
			if (crc != section_crc) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Application config checksum error\n",
					__func__, 0x335);
				return -EINVAL;
			}

			reflash_hcd->app_config = payload;
			reflash_hcd->app_config_size = section_size;
			reflash_hcd->app_config_addr = section_addr;

			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Application config size = %d\n",
				__func__, section_size);
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Application config flash address = 0x%08x\n",
				__func__, section_addr);
		} else if (!strncmp((const char *)section + 4,
				    reflash_section_display,
				    strlen(reflash_section_display))) {
			crc = ~crc32_le(~0, payload, section_size);
			if (crc != section_crc) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Display config checksum error\n",
					__func__, 0x346);
				return -EINVAL;
			}

			reflash_hcd->disp_config = payload;
			reflash_hcd->disp_config_size = section_size;
			reflash_hcd->disp_config_addr = section_addr;
			reflash_hcd->disp_config_present = 1;

			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Display config size = %d\n",
				__func__, section_size);
			dev_dbg(&tcm_hcd->pdev->dev,
				"%s info: Display config flash address = 0x%08x\n",
				__func__, section_addr);
		}
	}

	return 0;
}

/*
 * Load the image.  A plain reflash pulls the update the framework pushed to
 * the device, the manual variant is used by the factory tool.
 */
static int reflash_get_fw_image(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	if (!reflash_hcd->image) {
		if (reflash_hcd->do_reflash) {
			retval = request_firmware(&reflash_hcd->fw,
						  "synaptics/reflash_firmware_manual.img",
						  &tcm_hcd->pdev->dev);
			if (retval < 0) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to request %s\n",
					__func__, 0x371,
					"synaptics/reflash_firmware_manual.img");
				return retval;
			}
		} else {
			retval = request_firmware(&reflash_hcd->fw,
						  "synaptics/reflash_firmware.img",
						  &tcm_hcd->pdev->dev);
			if (retval < 0) {
				dev_err(&tcm_hcd->pdev->dev,
					"%s error: (line %d) Failed to request %s\n",
					__func__, 0x366,
					"synaptics/reflash_firmware.img");
				return retval;
			}
		}

		reflash_hcd->image = (u8 *)reflash_hcd->fw->data;
		reflash_hcd->image_size = reflash_hcd->fw->size;

		dev_dbg(&tcm_hcd->pdev->dev,
			"%s info: Firmware image size = %d\n",
			__func__, reflash_hcd->image_size);
	}

	retval = reflash_parse_fw_image();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to parse firmware image\n",
			__func__, 0x381);
		return retval;
	}

	return 0;
}

/*
 * Every partition follows the same three steps: make sure the image carries
 * that partition and that it was built for the same flash address the part
 * reports, erase the old copy and write the new one.  The checks and the
 * operations each log with their own name, the vendor keeps them apart.
 */
static int reflash_check_boot_config(void)
{
	unsigned int offset;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	size = reflash_hcd->boot_config_size;

	if (size <= REFLASH_BOOT_CONFIG_SLOT_SIZE) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) No valid boot config in image file\n",
			__func__, 0x4c2);
		return -EINVAL;
	}

	offset = get_unaligned_le16(&tcm_hcd->boot_info[0x0c]) *
		 reflash_hcd->write_block_size;

	if (reflash_hcd->boot_config_addr != offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Flash address mismatch\n",
			__func__, 0x4cd);
		return -EINVAL;
	}

	return 0;
}

static int reflash_erase_boot_config(void)
{
	int retval;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le16(&tcm_hcd->boot_info[0x0c]) *
		 reflash_hcd->write_block_size;

	retval = reflash_erase(offset, reflash_hcd->boot_config_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase boot_config\n",
			__func__, 0x65b);
		return retval;
	}

	return 0;
}

/*
 * The boot config is made of 15 slots of 8 bytes.  The bootloader reads the
 * highest bit of the second byte of a slot to decide whether the slot may
 * still be changed, so the image is pushed slot by slot.
 */
static int reflash_write_boot_config(void)
{
	int retval;
	unsigned int index;
	unsigned int offset;
	unsigned int slot;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le16(&tcm_hcd->boot_info[0x0c]) *
		 reflash_hcd->write_block_size;

	for (index = 0; index < REFLASH_BOOT_CONFIG_SLOTS; index++) {
		slot = index * REFLASH_BOOT_CONFIG_SLOT_SIZE;

		retval = reflash_write_flash(offset + slot,
					     reflash_hcd->boot_config + slot,
					     REFLASH_BOOT_CONFIG_SLOT_SIZE);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write to flash\n",
				__func__, 0x7cb);
			return retval;
		}

		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: Slot %d updated with new boot config\n",
			   __func__, index);
	}

	return 0;
}

/*
 * Locking the boot config down means burning the one time bits the
 * bootloader checks; once every slot is marked the part refuses an update
 * for good, so bail out instead of erasing it again.
 */
int reflash_update_boot_config(bool update)
{
	int retval;
	unsigned int index;
	unsigned int locked;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x78d);
		return retval;
	}

	retval = reflash_check_boot_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed boot_config partition check\n",
			__func__, 0x798);
		goto exit;
	}

	offset = get_unaligned_le16(&tcm_hcd->boot_info[0x0c]) *
		 reflash_hcd->write_block_size;

	retval = reflash_read_data(FLASH_AREA_CUSTOMER_SERIALIZATION, false,
				   NULL);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read boot config\n",
			__func__, 0x79f);
		goto exit;
	}

	if (!update) {
		locked = 0;

		for (index = 0; index < REFLASH_BOOT_CONFIG_SLOTS; index++)
			locked += (reflash_hcd->read.buf[index *
				 REFLASH_BOOT_CONFIG_SLOT_SIZE + 1] >> 7) & 1;

		if (locked == REFLASH_BOOT_CONFIG_SLOTS) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Boot config already locked down\n",
				__func__, 0x7ab);
			retval = -EINVAL;
			goto exit;
		}
	}

	if (update) {
		retval = reflash_erase_boot_config();
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to erase boot_config partition\n",
				__func__, 0x7b4);
			goto exit;
		}

		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: boot_config partition erased\n", __func__);

		retval = reflash_write_boot_config();
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to write boot_config partition\n",
				__func__, 0x7c2);
			goto exit;
		}

		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: boot_config partition written\n", __func__);
	}

	retval = 0;

exit:
	if (tcm_reset(tcm_hcd) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do reset\n",
			__func__, 0x7d8);

	return retval;
}

static int reflash_check_disp_config(void)
{
	unsigned int offset;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	size = reflash_hcd->disp_config_size;

	if (!size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) No display config in image file\n",
			__func__, 0x507);
		return -EINVAL;
	}

	offset = get_unaligned_le32(&tcm_hcd->boot_info[0x10]) *
		 reflash_hcd->write_block_size;

	if (reflash_hcd->disp_config_addr != offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Flash address mismatch\n",
			__func__, 0x516);
		return -EINVAL;
	}

	offset = get_unaligned_le16(&tcm_hcd->boot_info[0x14]) *
		 reflash_hcd->write_block_size;

	if (size != offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Config size mismatch\n",
			__func__, 0x51c);
		return -EINVAL;
	}

	return 0;
}

static int reflash_erase_disp_config(void)
{
	int retval;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le32(&tcm_hcd->boot_info[0x10]) *
		 reflash_hcd->write_block_size;

	retval = reflash_erase(offset, reflash_hcd->disp_config_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase disp_config\n",
			__func__, 0x663);
		return retval;
	}

	return 0;
}

static int reflash_write_disp_config(void)
{
	int retval;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le32(&tcm_hcd->boot_info[0x10]) *
		 reflash_hcd->write_block_size;

	retval = reflash_write_flash(offset, reflash_hcd->disp_config,
				     reflash_hcd->disp_config_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write disp_config to flash\n",
			__func__, 0x675);
		return retval;
	}

	return 0;
}

int reflash_update_disp_config(bool update)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x82b);
		return retval;
	}

	retval = reflash_check_disp_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to check disp_config partition\n",
			__func__, 0x836);
		goto exit;
	}

	retval = reflash_erase_disp_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase disp_config partition\n",
			__func__, 0x83e);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: disp_config partition erased\n", __func__);

	retval = reflash_write_disp_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write disp_config partition\n",
			__func__, 0x846);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: disp_config partition written\n", __func__);

	retval = 0;

exit:
	if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 0x850);

	return retval;
}

static int reflash_check_app_config(void)
{
	unsigned int offset;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	size = reflash_hcd->app_config_size;

	if (!size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) No application config in image file\n",
			__func__, 0x4df);
		return -EINVAL;
	}

	offset = get_unaligned_le16(&tcm_hcd->app_info[0x08]) *
		 reflash_hcd->write_block_size;

	if (reflash_hcd->app_config_addr != offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Flash address mismatch\n",
			__func__, 0x4ef);
		return -EINVAL;
	}

	offset = get_unaligned_le16(&tcm_hcd->app_info[0x0a]) *
		 reflash_hcd->write_block_size;

	if (size != offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Config size mismatch\n",
			__func__, 0x4f5);
		return -EINVAL;
	}

	return 0;
}

static int reflash_erase_app_config(void)
{
	int retval;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le16(&tcm_hcd->app_info[0x08]) *
		 reflash_hcd->write_block_size;

	retval = reflash_erase(offset, reflash_hcd->app_config_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase app_config\n",
			__func__, 0x64e);
		return retval;
	}

	return 0;
}

static int reflash_write_app_config(void)
{
	int retval;
	unsigned int offset;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	offset = get_unaligned_le16(&tcm_hcd->app_info[0x08]) *
		 reflash_hcd->write_block_size;

	retval = reflash_write_flash(offset, reflash_hcd->app_config,
				     reflash_hcd->app_config_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write app_config to flash\n",
			__func__, 0x5aa);
		return retval;
	}

	return 0;
}

int reflash_update_app_config(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x7ea);
		return retval;
	}

	retval = reflash_check_app_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to check app_config partition\n",
			__func__, 0x7f5);
		goto exit;
	}

	retval = reflash_erase_app_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase app_config partition\n",
			__func__, 0x7fd);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: app_config partition erased\n", __func__);

	retval = reflash_write_app_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write app_config partition\n",
			__func__, 0x808);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: app_config partition written\n", __func__);

	retval = 0;

exit:
	if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 0x812);

	return retval;
}

static int reflash_check_app_firmware(void)
{
	if (!reflash_hcd->app_firmware_size) {
		dev_err(&reflash_hcd->tcm_hcd->pdev->dev,
			"%s error: (line %d) No application firmware in image file\n",
			__func__, 0x536);
		return -EINVAL;
	}

	return 0;
}

static int reflash_erase_app_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_erase(reflash_hcd->app_firmware_addr,
			       reflash_hcd->app_firmware_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase app_firmware\n",
			__func__, 0x68d);
		return retval;
	}

	return 0;
}

static int reflash_write_app_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_write_flash(reflash_hcd->app_firmware_addr,
				     reflash_hcd->app_firmware,
				     reflash_hcd->app_firmware_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write app_firmware to flash\n",
			__func__, 0x5ec);
		return retval;
	}

	return 0;
}

int reflash_update_app_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x8a6);
		return retval;
	}

	retval = reflash_check_app_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to check app_firmware partition\n",
			__func__, 0x8b1);
		goto exit;
	}

	retval = reflash_erase_app_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase app_firmware partition\n",
			__func__, 0x8b8);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: app_firmware partition erased\n", __func__);

	retval = reflash_write_app_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write app_firmware partition\n",
			__func__, 0x8c2);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: app_firmware partition written\n", __func__);

	retval = 0;

exit:
	if (tcm_reset_and_reinit(tcm_hcd, false, true) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do reset\n",
			__func__, 0x8ce);

	return retval;
}

static int reflash_check_prod_test_firmware(void)
{
	if (!reflash_hcd->prod_test_firmware_size) {
		dev_err(&reflash_hcd->tcm_hcd->pdev->dev,
			"%s error: (line %d) No production test firmware in image file\n",
			__func__, 0x529);
		return -EINVAL;
	}

	return 0;
}

static int reflash_erase_prod_test_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_erase(reflash_hcd->prod_test_firmware_addr,
			       reflash_hcd->prod_test_firmware_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase prod_test_firmware\n",
			__func__, 0x678);
		return retval;
	}

	return 0;
}

static int reflash_write_prod_test_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_write_flash(reflash_hcd->prod_test_firmware_addr,
				     reflash_hcd->prod_test_firmware,
				     reflash_hcd->prod_test_firmware_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write prod_test_firmware to flash\n",
			__func__, 0x5db);
		return retval;
	}

	return 0;
}

int reflash_update_prod_test_firmware(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x86c);
		return retval;
	}

	retval = reflash_check_prod_test_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to check prod_test_firmware partition\n",
			__func__, 0x877);
		goto exit;
	}

	retval = reflash_erase_prod_test_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to erase prod_test_firmware partition\n",
			__func__, 0x87e);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: prod_test_firmware partition erased\n", __func__);

	retval = reflash_write_prod_test_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write prod_test_firmware partition\n",
			__func__, 0x886);
		goto exit;
	}

	dev_notice(&tcm_hcd->pdev->dev,
		   "%s info: prod_test_firmware partition written\n", __func__);

	retval = 0;

exit:
	if (tcm_reset_and_reinit(tcm_hcd, false, true) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do reset\n",
			__func__, 0x890);

	return retval;
}

/*
 * Decide whether the image is worth pushing.  The application config starts
 * with the firmware id the image was built for followed by the 16 byte config
 * id; when the part already runs the same or a newer build there is nothing
 * to do.
 */
static int reflash_compare_id_info(void)
{
	unsigned int index;
	unsigned int image_id;
	unsigned int device_id;
	int newer = 0;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	if (reflash_hcd->app_config_size <= REFLASH_APP_CONFIG_MIN_SIZE) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid application config in image file\n",
			__func__, 0x398);
		dev_notice(&tcm_hcd->pdev->dev,
			   "%s info: No need to do reflash\n", __func__);
		return 0;
	}

	if (!reflash_hcd->update_firmware) {
		if ((tcm_hcd->identify_info[1] - 1) > 1)
			goto update;

		image_id = get_unaligned_le32(reflash_hcd->app_config + 0x0e);
		device_id = tcm_hcd->build_id;

		if (image_id > device_id) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Image firmware ID newer than device firmware ID\n",
				   __func__);
			goto update;
		}

		if (image_id < device_id) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Image firmware ID older than device firmware ID\n",
				   __func__);
			goto no_need;
		}

		for (index = 0; index < 16; index++) {
			if (reflash_hcd->app_config[0x12 + index] >
			    tcm_hcd->app_info[0x10 + index]) {
				newer = 1;
				break;
			}

			if (reflash_hcd->app_config[0x12 + index] <
			    tcm_hcd->app_info[0x10 + index]) {
				newer = -1;
				break;
			}
		}

		if (newer > 0) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Image config ID newer than device config ID\n",
				   __func__);
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Updating %s\n", __func__,
				   "config only");
			return 1;
		}

		if (newer < 0) {
			dev_notice(&tcm_hcd->pdev->dev,
				   "%s info: Image config ID older than device config ID\n",
				   __func__);
			goto no_need;
		}
	}

update:
	dev_notice(&tcm_hcd->pdev->dev, "%s info: Updating %s\n", __func__,
		   "firmware and config");

	return 1;

no_need:
	dev_notice(&tcm_hcd->pdev->dev, "%s info: No need to do reflash\n",
		   __func__);

	return 0;
}

/*
 * The full reflash: load the image, decide whether it supersedes what the part
 * runs, then rewrite the firmware and the application config.  The touch
 * driver is parked while the flash is being touched, the reflash work queue
 * wakes up the polling that was suspended for the duration.
 */
static int reflash_do_reflash(void)
{
	int retval;
	bool update_config;
	unsigned int input;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = reflash_get_fw_image();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to get firmware image\n",
			__func__, 0x8e2);
		return retval;
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start of reflash\n",
		   __func__);

	tcm_hcd->mode = 1;

	update_config = reflash_compare_id_info();
	if (!update_config) {
		dev_notice(&tcm_hcd->pdev->dev, "%s info: End of reflash\n",
			   __func__);
		return 0;
	}

	retval = reflash_update_app_firmware();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to reflash application firmware\n",
			__func__, 0x8f2);
		return retval;
	}

	if (reflash_hcd->disp_config_present) {
		retval = reflash_update_disp_config(false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to reflash display config\n",
				__func__, 0x903);
			return retval;
		}
	}

	retval = reflash_update_app_config();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to reflash application config\n",
			__func__, 0x90a);
		return retval;
	}

	if (reflash_hcd->prod_test_firmware_size) {
		retval = reflash_update_prod_test_firmware();
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to reflash production test firmware\n",
				__func__, 0x911);
			return retval;
		}
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: End of reflash\n", __func__);

	return 0;
}

/*
 * The three custom areas are writable from user space.  The OTP area is
 * written as is, the LCM and OEM areas are erased first when the whole area
 * is replaced (offset 0) and always followed by a trip through the application
 * firmware so the part comes back up.
 */
static int reflash_update_custom_otp(struct syna_tcm_hcd *tcm_hcd,
				     unsigned char *buf, unsigned int offset,
				     unsigned int count)
{
	int retval;
	unsigned int area_offset;
	unsigned int area_size;

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x6a1);
		return retval;
	}

	area_offset = get_unaligned_le16(&tcm_hcd->boot_info[0x1c]) *
		      reflash_hcd->write_block_size;
	area_size = get_unaligned_le16(&tcm_hcd->boot_info[0x1e]) *
		    reflash_hcd->write_block_size;

	if (!area_offset || !area_size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Data area unavailable\n",
			__func__, 0x6b1);
		retval = -EINVAL;
		goto exit;
	}

	if ((count + offset) > area_size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data length\n",
			__func__, 0x6b8);
		retval = -EINVAL;
		goto exit;
	}

	retval = reflash_write_flash(area_offset + offset, buf, count);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write to flash\n",
			__func__, 0x6c2);
		goto exit;
	}

	retval = 0;

exit:
	if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 0x6cb);

	return retval;
}

static int reflash_update_custom_lcm(struct syna_tcm_hcd *tcm_hcd,
				     unsigned char *buf, unsigned int offset,
				     unsigned int count)
{
	int retval;
	unsigned int area_offset;
	unsigned int area_size;

	retval = tcm_get_data_location(tcm_hcd, FLASH_AREA_CUSTOM_LCM,
				       &area_offset, &area_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to get data location\n",
			__func__, 0x6e5);
		return retval;
	}

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x6ec);
		return retval;
	}

	area_size *= reflash_hcd->write_block_size;
	area_offset *= reflash_hcd->write_block_size;

	if (!area_size || !area_offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Data area unavailable\n",
			__func__, 0x6f9);
		retval = -EINVAL;
		goto exit;
	}

	if ((count + offset) > area_size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data length\n",
			__func__, 0x700);
		retval = -EINVAL;
		goto exit;
	}

	if (!offset) {
		retval = reflash_erase_flash(area_offset /
					     reflash_hcd->page_size,
					     (area_size +
					      reflash_hcd->page_size - 1) /
					     reflash_hcd->page_size);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to erase flash pages\n",
				__func__, 0x70d);
			goto exit;
		}
	}

	retval = reflash_write_flash(area_offset + offset, buf, count);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write to flash\n",
			__func__, 0x717);
		goto exit;
	}

	retval = 0;

exit:
	if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 0x720);

	return retval;
}

static int reflash_update_custom_oem(struct syna_tcm_hcd *tcm_hcd,
				     unsigned char *buf, unsigned int offset,
				     unsigned int count)
{
	int retval;
	unsigned int area_offset;
	unsigned int area_size;

	retval = tcm_get_data_location(tcm_hcd, FLASH_AREA_CUSTOM_OEM,
				       &area_offset, &area_size);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to get data location\n",
			__func__, 0x73a);
		return retval;
	}

	retval = reflash_set_up_flash_access();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to set up flash access\n",
			__func__, 0x741);
		return retval;
	}

	area_size *= reflash_hcd->write_block_size;
	area_offset *= reflash_hcd->write_block_size;

	if (!area_size || !area_offset) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Data area unavailable\n",
			__func__, 0x74e);
		retval = -EINVAL;
		goto exit;
	}

	if ((count + offset) > area_size) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Invalid data length\n",
			__func__, 0x755);
		retval = -EINVAL;
		goto exit;
	}

	if (!offset) {
		retval = reflash_erase_flash(area_offset /
					     reflash_hcd->page_size,
					     (area_size +
					      reflash_hcd->page_size - 1) /
					     reflash_hcd->page_size);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to erase flash pages\n",
				__func__, 0x762);
			goto exit;
		}
	}

	retval = reflash_write_flash(area_offset + offset, buf, count);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to write to flash\n",
			__func__, 0x76c);
		goto exit;
	}

	retval = 0;

exit:
	if (tcm_switch_mode(tcm_hcd, MODE_LOAD_APPLICATION) < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to run application firmware\n",
			__func__, 0x775);

	return retval;
}

/*
 * The image file is pushed through a binary attribute.  The framework writes
 * it in one go, so all this has to do is to remember how much of the staging
 * buffer is valid.
 */
static ssize_t reflash_sysfs_image_store(struct file *filp,
					 struct kobject *kobj,
					 struct bin_attribute *attr,
					 char *buf, loff_t offset,
					 size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (reflash_secure_memcpy(reflash_hcd->image_buf + offset,
				  reflash_hcd->image_size ?
				  reflash_hcd->image_size : REFLASH_IMAGE_BUF_SIZE,
				  (const unsigned char *)buf, count)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy firmware image data\n",
			__func__, 0x1b5);
		reflash_hcd->image_size = 0;
		mutex_unlock(&tcm_hcd->extif_mutex);
		return -EINVAL;
	}

	reflash_hcd->image_size = count + offset;

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;
}

/*
 * Every custom area is accessed through a pair of binary attributes that read
 * and write the area as a whole, the caller uses lseek to address the middle
 * of it.
 */
static ssize_t reflash_sysfs_lockdown_show(struct file *filp,
					   struct kobject *kobj,
					   struct bin_attribute *attr,
					   char *buf, loff_t offset,
					   size_t count)
{
	int retval;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_read_data(FLASH_AREA_CUSTOM_OTP, true, NULL);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read lockdown data\n",
			__func__, 0x1d3);
		goto exit;
	}

	mutex_lock(&reflash_hcd->read.buf_mutex);

	size = MIN(count, reflash_hcd->read.data_length - offset);

	if (reflash_secure_memcpy((unsigned char *)buf, count,
				  reflash_hcd->read.buf + offset, size)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy read data\n",
			__func__, 0x1d7);
		size = -EINVAL;
	}

	mutex_unlock(&reflash_hcd->read.buf_mutex);

exit:
	mutex_unlock(&reflash_hcd->reflash_mutex);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return size;
}

static ssize_t reflash_sysfs_lockdown_store(struct file *filp,
					    struct kobject *kobj,
					    struct bin_attribute *attr,
					    char *buf, loff_t offset,
					    size_t count)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	pm_stay_awake(&tcm_hcd->pdev->dev);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_update_custom_otp(tcm_hcd, (unsigned char *)buf,
					   offset, count);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to update custom OTP data\n",
			__func__, 0x1f1);

	mutex_unlock(&reflash_hcd->reflash_mutex);
	pm_relax(&tcm_hcd->pdev->dev);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval < 0 ? retval : count;
}

static ssize_t reflash_sysfs_lcm_show(struct file *filp, struct kobject *kobj,
				      struct bin_attribute *attr, char *buf,
				      loff_t offset, size_t count)
{
	int retval;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_read_data(FLASH_AREA_CUSTOM_LCM, true, NULL);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read LCM data\n",
			__func__, 0x210);
		goto exit;
	}

	mutex_lock(&reflash_hcd->read.buf_mutex);

	size = MIN(count, reflash_hcd->read.data_length - offset);

	if (reflash_secure_memcpy((unsigned char *)buf, count,
				  reflash_hcd->read.buf + offset, size)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy read data\n",
			__func__, 0x214);
		size = -EINVAL;
	}

	mutex_unlock(&reflash_hcd->read.buf_mutex);

exit:
	mutex_unlock(&reflash_hcd->reflash_mutex);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return size;
}

static ssize_t reflash_sysfs_lcm_store(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *attr, char *buf,
				       loff_t offset, size_t count)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	pm_stay_awake(&tcm_hcd->pdev->dev);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_update_custom_lcm(tcm_hcd, (unsigned char *)buf,
					   offset, count);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to update custom LCM data\n",
			__func__, 0x22e);

	mutex_unlock(&reflash_hcd->reflash_mutex);
	pm_relax(&tcm_hcd->pdev->dev);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval < 0 ? retval : count;
}

static ssize_t reflash_sysfs_oem_show(struct file *filp, struct kobject *kobj,
				      struct bin_attribute *attr, char *buf,
				      loff_t offset, size_t count)
{
	int retval;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_read_data(FLASH_AREA_CUSTOM_OEM, true, NULL);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read OEM data\n",
			__func__, 0x24d);
		goto exit;
	}

	mutex_lock(&reflash_hcd->read.buf_mutex);

	size = MIN(count, reflash_hcd->read.data_length - offset);

	if (reflash_secure_memcpy((unsigned char *)buf, count,
				  reflash_hcd->read.buf + offset, size)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy read data\n",
			__func__, 0x251);
		size = -EINVAL;
	}

	mutex_unlock(&reflash_hcd->read.buf_mutex);

exit:
	mutex_unlock(&reflash_hcd->reflash_mutex);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return size;
}

static ssize_t reflash_sysfs_oem_store(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *attr, char *buf,
				       loff_t offset, size_t count)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	pm_stay_awake(&tcm_hcd->pdev->dev);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_update_custom_oem(tcm_hcd, (unsigned char *)buf,
					   offset, count);
	if (retval < 0)
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to update custom OEM data\n",
			__func__, 0x26b);

	mutex_unlock(&reflash_hcd->reflash_mutex);
	pm_relax(&tcm_hcd->pdev->dev);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval < 0 ? retval : count;
}

static ssize_t reflash_sysfs_cs_show(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t offset, size_t count)
{
	int retval;
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);
	mutex_lock(&reflash_hcd->reflash_mutex);

	retval = reflash_read_data(FLASH_AREA_CUSTOMER_SERIALIZATION, true,
				   NULL);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to read OEM data\n",
			__func__, 0x28a);
		goto exit;
	}

	mutex_lock(&reflash_hcd->read.buf_mutex);

	size = MIN(count, reflash_hcd->read.data_length - offset);

	if (reflash_secure_memcpy((unsigned char *)buf, count,
				  reflash_hcd->read.buf + offset, size)) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to copy read data\n",
			__func__, 0x28e);
		size = -EINVAL;
	}

	mutex_unlock(&reflash_hcd->read.buf_mutex);

exit:
	mutex_unlock(&reflash_hcd->reflash_mutex);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return size;
}

/*
 * The reflash trigger.  The value the caller writes is a bit mask:
 *
 *   0x01/0x02  rewrite the application firmware (0x02 keeps it even when the
 *              part reports the same build), the configs follow
 *   0x04       application config only
 *   0x08       display config only, 0x04 selects the "keep" direction
 *   0x10       verify the boot config lockdown
 *   0x20       update the boot config
 */
static ssize_t reflash_sysfs_reflash_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int retval;
	unsigned int input = 0;
	struct syna_tcm_hcd *tcm_hcd = reflash_hcd->tcm_hcd;

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	mutex_lock(&tcm_hcd->extif_mutex);
	pm_stay_awake(&tcm_hcd->pdev->dev);
	mutex_lock(&reflash_hcd->reflash_mutex);

	if (reflash_hcd->image_size)
		reflash_hcd->image = reflash_hcd->image_buf;

	reflash_hcd->do_reflash = 1;
	reflash_hcd->update_firmware = (input >> 1) & 0x01;

	if ((input & 0x03) || (input > 3))
		goto do_reflash;

	retval = reflash_get_fw_image();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to get firmware image\n",
			__func__, 0x161);
		goto exit;
	}

	if (input & 0x20) {
		retval = reflash_update_boot_config(true);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to update boot config\n",
				__func__, 0x170);
			goto exit;
		}
	} else if (input & 0x10) {
		retval = reflash_update_boot_config(false);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to lockdown boot config\n",
				__func__, 0x169);
			goto exit;
		}
	}

	if (input & 0x08) {
		retval = reflash_update_disp_config((input & 0x04) ? false :
						    true);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to reflash display config\n",
				__func__, 0x181);
			goto exit;
		}
	} else if (input & 0x04) {
		retval = reflash_update_app_config();
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to reflash application config\n",
				__func__, 0x18a);
			goto exit;
		}
	}

	retval = count;
	goto exit;

do_reflash:
	retval = reflash_do_reflash();
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to do reflash\n",
			__func__, 0x154);
		goto exit;
	}

	if (input > 3) {
		retval = reflash_get_fw_image();
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to get firmware image\n",
				__func__, 0x161);
			goto exit;
		}

		if (input & 0x20) {
			retval = reflash_update_boot_config(true);
			if (retval < 0)
				goto exit;
		} else if (input & 0x10) {
			retval = reflash_update_boot_config(false);
			if (retval < 0)
				goto exit;
		}
	}

	retval = count;

exit:
	if (reflash_hcd->fw) {
		release_firmware(reflash_hcd->fw);
		reflash_hcd->fw = NULL;
	}

	reflash_hcd->do_reflash = 0;
	reflash_hcd->image = NULL;
	reflash_hcd->image_size = 0;
	reflash_hcd->update_firmware = 0;

	mutex_unlock(&reflash_hcd->reflash_mutex);
	pm_relax(&tcm_hcd->pdev->dev);
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}

static struct bin_attribute bin_attrs[] = {
	{
		.attr = {
			.name = "image",
			.mode = 0220,
		},
		.size = 0,
		.write = reflash_sysfs_image_store,
	},
	{
		.attr = {
			.name = "lockdown",
			.mode = 0664,
		},
		.size = 0,
		.read = reflash_sysfs_lockdown_show,
		.write = reflash_sysfs_lockdown_store,
	},
	{
		.attr = {
			.name = "lcm",
			.mode = 0664,
		},
		.size = 0,
		.read = reflash_sysfs_lcm_show,
		.write = reflash_sysfs_lcm_store,
	},
	{
		.attr = {
			.name = "oem",
			.mode = 0664,
		},
		.size = 0,
		.read = reflash_sysfs_oem_show,
		.write = reflash_sysfs_oem_store,
	},
	{
		.attr = {
			.name = "customer_serialization",
			.mode = 0444,
		},
		.size = 0,
		.read = reflash_sysfs_cs_show,
	},
};

static DEVICE_ATTR(reflash, 0220, reflash_show_error,
		   reflash_sysfs_reflash_store);

static struct syna_tcm_module_desc reflash_module;

static void reflash_remove_buffers(void)
{
	if (!reflash_hcd->read.init) {
		kfree(reflash_hcd->read.buf);
		reflash_hcd->read.buf_size = 0;
	}

	if (!reflash_hcd->resp.init) {
		kfree(reflash_hcd->resp.buf);
		reflash_hcd->resp.buf_size = 0;
	}

	if (!reflash_hcd->out.init) {
		kfree(reflash_hcd->out.buf);
		reflash_hcd->out.buf_size = 0;
	}
}

int reflash_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	unsigned int index;

	reflash_hcd = NULL;

	if (tcm_hcd->irq_enabled)
		return 0;

	reflash_hcd = kzalloc(sizeof(*reflash_hcd), GFP_KERNEL);
	if (!reflash_hcd) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to allocate memory for reflash_hcd\n",
			__func__, 0x95a);
		return -ENOMEM;
	}

	reflash_hcd->image_buf = (u8 *)__get_free_pages(GFP_KERNEL,
							get_order(REFLASH_IMAGE_BUF_SIZE));
	if (!reflash_hcd->image_buf) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to allocate memory for reflash_hcd->image_buf\n",
			__func__, 0x961);
		retval = -ENOMEM;
		goto err_free_hcd;
	}

	reflash_hcd->tcm_hcd = tcm_hcd;

	mutex_init(&reflash_hcd->reflash_mutex);
	mutex_init(&reflash_hcd->out.buf_mutex);
	reflash_hcd->out.init = 0;
	mutex_init(&reflash_hcd->resp.buf_mutex);
	reflash_hcd->resp.init = 0;
	mutex_init(&reflash_hcd->read.buf_mutex);
	reflash_hcd->read.init = 0;

	reflash_hcd->kobj = kobject_create_and_add("reflash",
						   tcm_hcd->sysfs_root);
	if (!reflash_hcd->kobj) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to create sysfs directory\n",
			__func__, 0x97d);
		retval = -EINVAL;
		goto err_free_buf;
	}

	retval = sysfs_create_file(reflash_hcd->kobj, &dev_attr_reflash.attr);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to create sysfs file\n",
			__func__, 0x987);
		goto err_remove_kobj;
	}

	retval = sysfs_create_bin_file(reflash_hcd->kobj, &bin_attrs[0]);
	if (retval < 0) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to create sysfs bin file\n",
			__func__, 0x98f);
		goto err_remove_sysfs_file;
	}

	reflash_hcd->custom_kobj = kobject_create_and_add("custom",
							  reflash_hcd->kobj);
	if (!reflash_hcd->custom_kobj) {
		dev_err(&tcm_hcd->pdev->dev,
			"%s error: (line %d) Failed to create custom sysfs directory\n",
			__func__, 0x997);
		retval = -EINVAL;
		goto err_remove_bin_file;
	}

	for (index = 1; index < ARRAY_SIZE(bin_attrs); index++) {
		retval = sysfs_create_bin_file(reflash_hcd->custom_kobj,
					       &bin_attrs[index]);
		if (retval < 0) {
			dev_err(&tcm_hcd->pdev->dev,
				"%s error: (line %d) Failed to create custom sysfs bin file\n",
				__func__, 0x9a1);
			goto err_remove_custom_bin_file;
		}
	}

	tcm_hcd->reflash_data_fn = reflash_read_data;

	return 0;

err_remove_custom_bin_file:
	while (--index > 1)
		sysfs_remove_bin_file(reflash_hcd->custom_kobj,
				      &bin_attrs[index]);

	kobject_put(reflash_hcd->custom_kobj);

err_remove_bin_file:
	sysfs_remove_bin_file(reflash_hcd->kobj, &bin_attrs[0]);

err_remove_sysfs_file:
	sysfs_remove_file(reflash_hcd->kobj, &dev_attr_reflash.attr);

err_remove_kobj:
	kobject_put(reflash_hcd->kobj);

err_free_buf:
	if (reflash_hcd->image_buf) {
		free_pages((unsigned long)reflash_hcd->image_buf,
			   get_order(REFLASH_IMAGE_BUF_SIZE));
		reflash_hcd->image_buf = NULL;
	}

	reflash_remove_buffers();

err_free_hcd:
	kfree(reflash_hcd);
	reflash_hcd = NULL;

	return retval;
}

int reflash_remove(struct syna_tcm_hcd *tcm_hcd)
{
	unsigned int index;

	if (reflash_hcd) {
		tcm_hcd->reflash_data_fn = NULL;

		for (index = 1; index < ARRAY_SIZE(bin_attrs); index++)
			sysfs_remove_bin_file(reflash_hcd->custom_kobj,
					      &bin_attrs[index]);

		kobject_put(reflash_hcd->custom_kobj);

		sysfs_remove_bin_file(reflash_hcd->kobj, &bin_attrs[0]);
		sysfs_remove_file(reflash_hcd->kobj, &dev_attr_reflash.attr);
		kobject_put(reflash_hcd->kobj);

		if (reflash_hcd->image_buf) {
			free_pages((unsigned long)reflash_hcd->image_buf,
				   get_order(REFLASH_IMAGE_BUF_SIZE));
			reflash_hcd->image_buf = NULL;
		}

		reflash_remove_buffers();

		kfree(reflash_hcd);
		reflash_hcd = NULL;
	}

	complete(&reflash_remove_complete);

	return 0;
}

int reflash_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (reflash_hcd)
		return 0;

	if (tcm_hcd->irq_enabled)
		return 0;

	return reflash_init(tcm_hcd);
}

static struct syna_tcm_module_desc reflash_module = {
	.id = REFLASH_MODULE_ID,
	.init = reflash_init,
	.remove = reflash_remove,
	.reinit = reflash_reinit,
};

int reflash_module_init(void)
{
	return syna_tcm_add_module(&reflash_module, true);
}

void reflash_module_exit(void)
{
	syna_tcm_add_module(&reflash_module, false);

	wait_for_completion(&reflash_remove_complete);
}
