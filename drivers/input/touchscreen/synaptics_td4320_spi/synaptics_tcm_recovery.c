// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchComm (TCM) driver for the TD4320 touch controller.
 *
 * Recovery support.  When the application firmware refuses to start, or when
 * the panel has to be brought back from the dead after a failed flash update,
 * the host can take over the part completely.  The factory tool uploads an
 * Intel HEX image through the "ihex" binary attribute and then triggers one of
 * the two recovery flows:
 *
 *   f35_recovery     - the F$35 function block erases and programs the flash
 *                      by itself, the host only feeds it 18 byte chunks
 *   romboot_recovery - the host drives a JEDEC compatible SPI NOR flash on its
 *                      own through the SPI master function of the ROM
 *                      bootloader, one page at a time
 *
 * Reconstructed from the vendor module synaptics_td4320_spi_ts.drv.
 */

#include "synaptics_tcm_core.h"

#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>

#define RECOVERY_MODULE_ID		0x04

/* JEDEC commands understood by the SPI NOR flash on the panel */
#define JEDEC_WRITE_ENABLE		0x06
#define JEDEC_PAGE_PROGRAM		0x02
#define JEDEC_READ_STATUS		0x05
#define JEDEC_CHIP_ERASE		0xc7

/* an Intel HEX record is 13 characters plus the terminating newline */
#define RECOVERY_IHEX_RECORD_SIZE	0x0e
#define RECOVERY_IHEX_RECORD_FORMAT	"%c%02x%02x%02x%02x%02x%02x"

/* the largest image the two buffers below can hold */
#define RECOVERY_IHEX_BUF_SIZE		0x200000
#define RECOVERY_DATA_BUF_SIZE		0x20000

/* recovered images carry a 16 bit header and are programmed in 256 byte pages */
#define RECOVERY_ROMBOOT_IMAGE_HEADER	0xaa55
#define RECOVERY_ROMBOOT_PAGE_SIZE	0x100

/* F$35 talks in 18 byte chunks: 16 data bytes, 2 size bytes and a command */
#define RECOVERY_F35_CHUNK_SIZE		0x12
#define RECOVERY_F35_CHUNK_PAYLOAD	(RECOVERY_F35_CHUNK_SIZE + 1)

/* flash commands of the F$35 command register */
#define RECOVERY_F35_CMD_WRITE_FLASH	0x02
#define RECOVERY_F35_CMD_ERASE_FLASH	0x03
#define RECOVERY_F35_CMD_RESET		0x10

/* offsets inside the F$35 control registers derived from the PDT entry */
#define RECOVERY_F35_COMMAND_OFFSET	0x12
#define RECOVERY_F35_STATUS_OFFSET	0x05

/* the ROM bootloader exposes the attached flash through F$35 */
#define RECOVERY_F35_PDT_ENTRY_ADDR	0xe9
#define RECOVERY_F35_ID			0x35

/* upper bound for the two polling loops of the erase and program sequences */
#define RECOVERY_POLL_TIMEOUT_MS	5000
#define RECOVERY_POLL_INTERVAL_MS	100
/* the erase command needs a full five seconds before the status is valid */
#define RECOVERY_ERASE_SETTLE_MS	5000

#define RECOVERY_F35_MODE_ROM		0
#define RECOVERY_F35_MODE_DIRECT	1

struct syna_tcm_recovery_hcd {
	u8 f35_mode;			/* +0x000 */
	u8 chunk[0x12];			/* +0x001 */
	u8 flash_command;		/* +0x013 */
	u16 reboot_cmd;			/* +0x014 */
	u8 pad0[0x02];			/* +0x016 */
	u8 *ihex_buf;			/* +0x018 */
	u8 *data_buf;			/* +0x020 */
	u32 ihex_size;			/* +0x028 */
	u32 record_count;		/* +0x02c */
	u32 data_offset;		/* +0x030 */
	u8 pad1[0x1c];			/* +0x034 */
	u8 *payload;			/* +0x050 */
	u32 payload_size;		/* +0x058 */
	u8 pad2[0x24];			/* +0x05c */
	struct kobject *kobj;		/* +0x080 */
	u16 pdt_entry[4];		/* +0x088 */
	struct syna_tcm_hcd *tcm_hcd;	/* +0x090 */
};

static struct syna_tcm_recovery_hcd *recovery_hcd;

static DECLARE_COMPLETION(recovery_remove_complete);

/* the three byte prefix every SPI master command of the ROM bootloader has */
static const u8 recovery_flash_prefix[3] = { 0x01, 0x19, 0x00 };

static int recovery_init(struct syna_tcm_hcd *tcm_hcd);
static int recovery_remove(struct syna_tcm_hcd *tcm_hcd);
static int recovery_reinit(struct syna_tcm_hcd *tcm_hcd);

static ssize_t recovery_sysfs_ihex_store(struct file *filp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t offset, size_t count);

static ssize_t recovery_show_error(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	pr_err("%s: Attribute not readable\n", __func__);
	return -1;
}

static ssize_t recovery_sysfs_romboot_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t recovery_sysfs_f35_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static DEVICE_ATTR(f35_recovery, 0220, recovery_show_error,
		recovery_sysfs_f35_recovery_store);
static DEVICE_ATTR(romboot_recovery, 0220, recovery_show_error,
		recovery_sysfs_romboot_recovery_store);

static struct attribute *recovery_attrs[] = {
	&dev_attr_romboot_recovery.attr,
	&dev_attr_f35_recovery.attr,
	NULL,
};

static struct bin_attribute recovery_ihex_bin_attribute = {
	.attr = {
		.name = "ihex",
		.mode = 0220,
	},
	.size = 0,
	.write = recovery_sysfs_ihex_store,
};

static struct syna_tcm_module_desc recovery_module = {
	.id = RECOVERY_MODULE_ID,
	.init = recovery_init,
	.remove = recovery_remove,
	.reinit = recovery_reinit,
};

static int recovery_do_romboot_recovery(void);
static int recovery_do_f35_recovery(void);

static void recovery_memcpy_check(const void *dst, unsigned int dst_size,
		const void *src, unsigned int src_size)
{
	pr_err("%s: dst = %pK, dst_size = %u, src = %pK, src_size = %u\n",
			"secure_memcpy", dst, dst_size, src, src_size);
}



static ssize_t recovery_sysfs_ihex_store(struct file *filp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t offset, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	int retval;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (!buf || !(recovery_hcd->ihex_buf + offset) ||
			(RECOVERY_IHEX_BUF_SIZE - offset) < count) {
		recovery_memcpy_check(recovery_hcd->ihex_buf + offset,
				RECOVERY_IHEX_BUF_SIZE - offset, buf, count);
		goto error;
	}

	memcpy(recovery_hcd->ihex_buf + offset, buf, count);

	recovery_hcd->ihex_size = count + offset;

	mutex_unlock(&tcm_hcd->extif_mutex);

	return count;

error:
	LOGE("%s error: (line %d) Failed to copy ihex data\n",
			"recovery_sysfs_ihex_store", 0x25a);
	recovery_hcd->ihex_size = 0;
	mutex_unlock(&tcm_hcd->extif_mutex);
	return -EINVAL;
}

static int recovery_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;
	int count;

	recovery_hcd = kzalloc(sizeof(*recovery_hcd), GFP_KERNEL);
	if (!recovery_hcd) {
		LOGE("%s error: (line %d) Failed to allocate memory for recovery_hcd\n",
				"recovery_init", 0x499);
		return -ENOMEM;
	}

	recovery_hcd->ihex_buf = kmalloc_order_trace(RECOVERY_IHEX_BUF_SIZE,
			GFP_KERNEL, 9);
	if (!recovery_hcd->ihex_buf) {
		LOGE("%s error: (line %d) Failed to allocate memory for recovery_hcd->ihex_buf\n",
				"recovery_init", 0x4a0);
		goto err_free_hcd;
	}

	recovery_hcd->data_buf = kmalloc_order_trace(RECOVERY_DATA_BUF_SIZE,
			GFP_KERNEL, 7);
	if (!recovery_hcd->data_buf) {
		LOGE("%s error: (line %d) Failed to allocate memory for recovery_hcd->data_buf\n",
				"recovery_init", 0x4a7);
		goto err_free_hcd;
	}

	recovery_hcd->tcm_hcd = tcm_hcd;
	recovery_hcd->f35_mode = RECOVERY_F35_MODE_DIRECT;
	recovery_hcd->reboot_cmd = CMD_REBOOT_TO_ROM_BOOTLOADER;

	recovery_hcd->kobj = kobject_create_and_add("recovery",
			tcm_hcd->sysfs_root);
	if (!recovery_hcd->kobj) {
		LOGE("%s error: (line %d) Failed to create sysfs directory\n",
				"recovery_init", 0x4ba);
		retval = -EINVAL;
		goto err_free_hcd;
	}

	retval = sysfs_create_file(recovery_hcd->kobj,
			&dev_attr_f35_recovery.attr);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create sysfs file\n",
				"recovery_init", 0x4c4);
		goto err_remove_sysfs;
	}

	retval = sysfs_create_file(recovery_hcd->kobj,
			&dev_attr_romboot_recovery.attr);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create sysfs file\n",
				"recovery_init", 0x4c4);
		goto err_remove_sysfs;
	}

	retval = sysfs_create_bin_file(recovery_hcd->kobj,
			&recovery_ihex_bin_attribute);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to create sysfs bin file\n",
				"recovery_init", 0x4cc);
		goto err_remove_sysfs;
	}

	return 0;

err_remove_sysfs:
	for (count = ARRAY_SIZE(recovery_attrs) - 2; count >= 0; count--)
		sysfs_remove_file(recovery_hcd->kobj, recovery_attrs[count]);

	kobject_put(recovery_hcd->kobj);

err_free_hcd:
	kfree(recovery_hcd->data_buf);
	kfree(recovery_hcd->ihex_buf);
	kfree(recovery_hcd);
	recovery_hcd = NULL;

	return retval;
}

static int recovery_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (recovery_hcd) {
		sysfs_remove_bin_file(recovery_hcd->kobj,
				&recovery_ihex_bin_attribute);
		sysfs_remove_file(recovery_hcd->kobj,
				&dev_attr_f35_recovery.attr);
		sysfs_remove_file(recovery_hcd->kobj,
				&dev_attr_romboot_recovery.attr);
		kobject_put(recovery_hcd->kobj);

		kfree(recovery_hcd->data_buf);
		kfree(recovery_hcd->ihex_buf);
		kfree(recovery_hcd);
		recovery_hcd = NULL;
	}

	complete(&recovery_remove_complete);

	return 0;
}

static int recovery_reinit(struct syna_tcm_hcd *tcm_hcd)
{
	if (recovery_hcd)
		return 0;

	return recovery_init(tcm_hcd);
}

static int recovery_module_init(void)
{
	return syna_tcm_add_module(&recovery_module, true);
}

static void recovery_module_exit(void)
{
	syna_tcm_add_module(&recovery_module, false);

	wait_for_completion(&recovery_remove_complete);
}

/*
 * The ROM bootloader exposes a tiny SPI master: every command is a three byte
 * prefix, a 16 bit read/write selector, the JEDEC op code and then the payload
 * the op code expects.
 */
static int recovery_flash_romboot_command(struct syna_tcm_hcd *tcm_hcd, u8 cmd,
		u8 *data, unsigned int length, u8 *status, bool read)
{
	u8 *payload = NULL;
	u8 *resp_buf = NULL;
	unsigned int resp_length = 0;
	unsigned char resp_code = 0;
	int retval;

	payload = kmalloc(length + 6, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;

	memcpy(payload, recovery_flash_prefix, sizeof(recovery_flash_prefix));
	*(u16 *)(payload + 3) = read ? 1 : 0;
	payload[5] = cmd;
	memcpy(payload + 6, data, length);

	retval = tcm_write_message(tcm_hcd,
			CMD_SPI_MASTER_WRITE_THEN_READ_EXTENDED, payload,
			length + 6, &resp_buf, &resp_length, &resp_code, NULL, 20);
	if (retval < 0)
		LOGE("%s error: (line %d) Failed to write command CMD_SPI_MASTER_WRITE_THEN_READ_EXTENDED\n",
				"recovery_flash_romboot_command", 0x13e);

	if (read && resp_length && resp_buf)
		*status = resp_buf[0];

	kfree(payload);
	kfree(resp_buf);

	return retval;
}

/*
 * Wait for the flash to finish the previous command.  The status register is
 * polled for at most five seconds, the vendor driver counts down one hundred
 * milliseconds at a time.
 */
static int recovery_flash_romboot_status(struct syna_tcm_hcd *tcm_hcd)
{
	u8 status = 0;
	int timeout = RECOVERY_POLL_TIMEOUT_MS;
	int retval;

	while (timeout) {
		retval = recovery_flash_romboot_command(tcm_hcd,
				JEDEC_READ_STATUS, NULL, 0, &status, true);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write JEDEC_READ_STATUS\n",
					"recovery_flash_romboot_status", 0x157);
			return retval;
		}

		usleep_range(RECOVERY_POLL_TIMEOUT_MS,
			2 * RECOVERY_POLL_TIMEOUT_MS);

		if (!status)
			return 0;

		timeout -= RECOVERY_POLL_INTERVAL_MS;
	}

	return -EIO;
}

static int recovery_flash_romboot_erase(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	int retval;

	LOGI("%s info: %s", "recovery_flash_romboot_erase",
			"recovery_flash_romboot_erase");

	retval = recovery_flash_romboot_command(tcm_hcd, JEDEC_WRITE_ENABLE,
			NULL, 0, NULL, false);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write JEDEC_WRITE_ENABLE\n",
				"recovery_flash_romboot_erase", 0x174);
		return retval;
	}

	retval = recovery_flash_romboot_command(tcm_hcd, JEDEC_CHIP_ERASE,
			NULL, 0, NULL, false);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write JEDEC_CHIP_ERASE\n",
				"recovery_flash_romboot_erase", 0x17c);
		return retval;
	}

	retval = recovery_flash_romboot_status(tcm_hcd);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to get correct status: %d\n",
				"recovery_flash_romboot_erase", 0x183, retval);
		return retval;
	}

	return 0;
}

/*
 * A recovered image is 256 byte pages behind a 0xaa55 header.  Every page is
 * programmed by writing the page number followed by the page itself, and the
 * status register is polled after each of them.
 */
static int recovery_flash_romboot_reprogram(u8 *payload, unsigned int size)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	u8 page[3 + RECOVERY_ROMBOOT_PAGE_SIZE];
	u16 header;
	unsigned int pages;
	unsigned int i;
	int retval;

	LOGI("%s info: Do recovery_flash_romboot_reprogram\n",
			"recovery_do_romboot_recovery");

	header = get_unaligned_le16(payload);
	if ((size & 0xff) || header != RECOVERY_ROMBOOT_IMAGE_HEADER) {
		LOGE("%s error: (line %d) Wrong image file\n",
				"recovery_flash_romboot_reprogram", 0x195);
		LOGE("%s error: (line %d) image_size = %d, img_header = 0x%04x\n",
				"recovery_flash_romboot_reprogram", 0x198,
				size, header);
		return -EINVAL;
	}

	pages = size >> 8;

	/* the vendor driver logs this through dev_err() on purpose */
	LOGE("%s error: (line %d) image_size = %d, img_header = 0x%04x, pages = %d\n",
			"recovery_flash_romboot_reprogram", 0x1a0, size,
			header, pages);

	if (!pages)
		return 0;

	for (i = 0; i < pages; i++) {
		retval = recovery_flash_romboot_command(tcm_hcd,
				JEDEC_WRITE_ENABLE, NULL, 0, NULL, false);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write JEDEC_WRITE_ENABLE\n",
					"recovery_flash_romboot_reprogram", 0x1a7);
			return retval;
		}

		page[0] = (u8)(i >> 8);
		page[1] = (u8)(i & 0xff);
		page[2] = 0;
		memcpy(page + 3, payload + i * RECOVERY_ROMBOOT_PAGE_SIZE,
				RECOVERY_ROMBOOT_PAGE_SIZE);

		retval = recovery_flash_romboot_command(tcm_hcd,
				JEDEC_PAGE_PROGRAM, page, sizeof(page), NULL,
				false);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write JEDEC_READ_STATUS\n",
					"recovery_flash_romboot_reprogram", 0x1b6);
			return retval;
		}

		retval = recovery_flash_romboot_status(tcm_hcd);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to get correct status: %d\n",
					"recovery_flash_romboot_reprogram",
					0x1be, retval);
			return retval;
		}
	}

	return 0;
}

/*
 * Parse the Intel HEX records into a flat image.  Records with type 2 move the
 * address base, records with type 0 carry two data bytes each.
 */
static int recovery_add_romboot_data_entry(unsigned int offset, u8 value)
{
	if (offset >= RECOVERY_DATA_BUF_SIZE) {
		LOGE("%s error: (line %d) Reached data buffer size limit\n",
				"recovery_add_romboot_data_entry", 0x296);
		return -EINVAL;
	}

	recovery_hcd->data_buf[offset] = value;

	return 0;
}

static int recovery_parse_romboot_ihex(void)
{
	unsigned int records;
	unsigned int i;
	unsigned int offset = 0;
	unsigned int base = 0;
	unsigned int length;
	unsigned int addr_hi;
	unsigned int addr_lo;
	unsigned int record_type;
	unsigned int data0;
	unsigned int data1;
	char type;
	int retval;

	if (!recovery_hcd->ihex_buf) {
		LOGE("%s error: (line %d) No ihex data\n",
				"recovery_parse_romboot_ihex", 0xda);
		return -ENODATA;
	}

	records = recovery_hcd->ihex_size / RECOVERY_IHEX_RECORD_SIZE;

	memset(recovery_hcd->data_buf, 0xff, RECOVERY_DATA_BUF_SIZE);

	recovery_hcd->record_count = records;
	recovery_hcd->data_offset = 0;

	for (i = 0; i < records; i++) {
		char *line = (char *)recovery_hcd->ihex_buf +
			i * RECOVERY_IHEX_RECORD_SIZE;

		line[RECOVERY_IHEX_RECORD_SIZE - 1] = 0;

		retval = sscanf(line, RECOVERY_IHEX_RECORD_FORMAT, &type,
				&length, &addr_hi, &addr_lo, &record_type,
				&data0, &data1);
		if (retval != 7) {
			LOGE("%s error: (line %d) Failed to read ihex record\n",
					"recovery_parse_romboot_ihex", 0xf2);
			return -EINVAL;
		}

		if (record_type == 2) {
			base = (data1 + (data0 << 8)) << 4;
			continue;
		}

		if (record_type != 0)
			continue;

		offset = base + addr_lo + (addr_hi << 8);

		retval = recovery_add_romboot_data_entry(offset, (u8)data0);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to add data entry for data0\n",
					"recovery_parse_romboot_ihex", 0xff);
			return retval;
		}

		offset++;

		retval = recovery_add_romboot_data_entry(offset, (u8)data1);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to add data entry for data1\n",
					"recovery_parse_romboot_ihex", 0x106);
			return retval;
		}

		offset++;

		recovery_hcd->data_offset = offset;
	}

	return 0;
}

static int recovery_do_romboot_recovery(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	u8 *payload = NULL;
	unsigned int size;
	int retval;

	if (tcm_hcd->identify_info[1] != 4) {
		LOGE("%s error: (line %d) Not in romboot\n",
				"recovery_do_romboot_recovery", 0x1d6);
		return -EINVAL;
	}

	retval = recovery_parse_romboot_ihex();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to parse ihex data\n",
				"recovery_do_romboot_recovery", 0x1dd);
		return retval;
	}

	size = RECOVERY_DATA_BUF_SIZE;

	recovery_hcd->payload = recovery_hcd->data_buf;
	recovery_hcd->payload_size = size;

	payload = kmalloc(size, GFP_KERNEL);
	if (!payload || !recovery_hcd->payload) {
		LOGE("%s error: (line %d) Failed to copy payload\n",
				"recovery_do_romboot_recovery", 0x1eb);
		kfree(payload);
		return -ENOMEM;
	}

	memcpy(payload, recovery_hcd->payload, size);

	LOGI("%s info: image_size = %d\n", "recovery_do_romboot_recovery", size);

	retval = recovery_flash_romboot_erase();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to erase chip\n",
				"recovery_do_romboot_recovery", 0x1f6);
		goto out;
	}

	retval = recovery_flash_romboot_reprogram(payload, size);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to program chip\n",
				"recovery_do_romboot_recovery", 0x1ff);
		goto out;
	}

	LOGI("%s info: Do reset and re-init\n", "recovery_do_romboot_recovery");

	if (tcm_hcd->reset_and_reinit_fn(tcm_hcd, true, true) < 0)
		LOGE("%s error: (line %d) Failed to reset\n",
				"recovery_do_romboot_recovery", 0x208);

out:
	kfree(payload);

	return retval;
}

static ssize_t recovery_sysfs_romboot_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	unsigned int input = 0;
	int retval;

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	if (!input)
		return 0;

	dev_notice(dev, "%s info: %s\n", "recovery_do_romboot_recovery",
			"recovery_do_romboot_recovery");

	mutex_lock(&tcm_hcd->extif_mutex);

	pm_stay_awake(&tcm_hcd->pdev->dev);

	retval = recovery_do_romboot_recovery();

	pm_relax(&tcm_hcd->pdev->dev);

	mutex_unlock(&tcm_hcd->extif_mutex);

	return 0;
}

/*
 * The F$35 function block keeps the flash for itself, so the host only asks it
 * to erase or to program an 18 byte chunk at a time.  Both flows below are
 * rebuilt from the vendor driver; every helper is a static function whose name
 * ends up in the error messages, which is how their boundaries were found.
 */
static int recovery_add_data_entry(unsigned int offset, u8 value)
{
	if (offset >= RECOVERY_DATA_BUF_SIZE) {
		LOGE("%s error: (line %d) Reached data buffer size limit\n",
				"recovery_add_data_entry", 0x287);
		return -EINVAL;
	}

	recovery_hcd->data_buf[offset] = value;

	return 0;
}

static int recovery_add_padding(unsigned int *offset, unsigned int count,
		unsigned int line)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (*offset >= RECOVERY_DATA_BUF_SIZE) {
			LOGE("%s error: (line %d) Reached data buffer size limit\n",
					"recovery_add_padding", 0x287);
			LOGE("%s error: (line %d) Failed to add data entry\n",
					"recovery_add_padding", line);
			return -EINVAL;
		}

		recovery_hcd->data_buf[(*offset)++] = 0xff;
	}

	return 0;
}

/*
 * The F$35 image is not a flat one.  Every eight data bytes are preceded by a
 * 16 bit marker that holds the address of the block shifted right by four, and
 * every address record pads the current block to eight bytes first.
 */
static int recovery_parse_f35_ihex(void)
{
	unsigned int records = recovery_hcd->record_count;
	unsigned int offset = 0;
	unsigned int base = 0;
	unsigned int padding = 0;
	unsigned int length;
	unsigned int addr_hi;
	unsigned int addr_lo;
	unsigned int record_type;
	unsigned int data0;
	unsigned int data1;
	unsigned int addr;
	unsigned int i;
	char type;
	int retval;

	if (records < RECOVERY_IHEX_RECORD_SIZE) {
		recovery_hcd->data_offset = offset;
		return 0;
	}

	for (i = 0; i < records; i++) {
		char *line = (char *)recovery_hcd->ihex_buf +
			i * RECOVERY_IHEX_RECORD_SIZE;

		line[RECOVERY_IHEX_RECORD_SIZE - 1] = 0;

		retval = sscanf(line, RECOVERY_IHEX_RECORD_FORMAT, &type,
				&length, &addr_hi, &addr_lo, &record_type,
				&data0, &data1);
		if (retval != 7) {
			LOGE("%s error: (line %d) Failed to read ihex record\n",
					"recovery_parse_f35_ihex", 0x2e4);
			return -EINVAL;
		}

		if (record_type == 2) {
			unsigned int pad = (-padding) & 0x07;

			if (pad) {
				retval = recovery_add_padding(&offset, pad,
						(padding & 0x07) ? 0x2ac : 0x2b3);
				if (retval < 0) {
					LOGE("%s error: (line %d) Failed to add padding\n",
						"recovery_parse_f35_ihex", 0x31c);
					return retval;
				}

				padding += pad;
			}

			base = (data1 + (data0 << 8)) << 4;
			continue;
		}

		if (record_type != 0)
			continue;

		if (!(padding & 0x07)) {
			addr = base + addr_lo + (addr_hi << 8);

			retval = recovery_add_data_entry(offset, (u8)(addr >> 4));
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to add data entry\n",
						"recovery_parse_f35_ihex", 0x2f1);
				return retval;
			}

			offset++;

			retval = recovery_add_data_entry(offset, (u8)(addr >> 12));
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to add data entry\n",
						"recovery_parse_f35_ihex", 0x2f8);
				return retval;
			}

			offset++;
		}

		retval = recovery_add_data_entry(offset, (u8)data0);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to add data entry\n",
					"recovery_parse_f35_ihex", 0x300);
			return retval;
		}

		offset++;

		retval = recovery_add_data_entry(offset, (u8)data1);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to add data entry\n",
					"recovery_parse_f35_ihex", 0x307);
			return retval;
		}

		offset++;

		padding++;
	}

	recovery_hcd->data_offset = offset;

	return 0;
}

static int recovery_check_status(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	u8 status = 0;
	int retval;

	retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
			recovery_hcd->pdt_entry[3], &status, 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read status\n",
				"recovery_check_status", 0x32f);
		return retval;
	}

	status &= 0x1f;

	if (status) {
		LOGE("%s error: (line %d) Recovery mode status = 0x%02x\n",
				"recovery_check_status", 0x338, status);
		return -EINVAL;
	}

	return 0;
}

/*
 * Kick the command register and then wait for it to fall back to zero, the
 * flash status register is polled afterwards for the busy bit.
 */
static int recovery_poll_erase_completion(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	u8 status = 0;
	u8 value = 1;
	int timeout = RECOVERY_POLL_TIMEOUT_MS;
	int retval;

	while (timeout >= 0) {
		retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
				recovery_hcd->pdt_entry[1], &value, 1);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write F$35 command\n",
					"recovery_poll_erase_completion", 0x38f);
			return retval;
		}

		retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
				recovery_hcd->pdt_entry[1], &status, 1);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read command status\n",
					"recovery_poll_erase_completion", 0x39a);
			return retval;
		}

		timeout -= RECOVERY_POLL_INTERVAL_MS;

		if (!status)
			break;

		msleep(RECOVERY_POLL_INTERVAL_MS);
	}

	if (status) {
		LOGE("%s error: (line %d) Failed to get erase completion\n",
				"recovery_poll_erase_completion", 0x3c8);
		return -EINVAL;
	}

	while (1) {
		retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
				recovery_hcd->pdt_entry[3] +
				RECOVERY_F35_STATUS_OFFSET, &status, 1);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to read flash status\n",
					"recovery_poll_erase_completion", 0x3b3);
			return retval;
		}

		if (!(status & 0x01))
			break;

		msleep(RECOVERY_POLL_INTERVAL_MS);
	}

	return 0;
}

static int recovery_erase_flash(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	u8 command = RECOVERY_F35_CMD_ERASE_FLASH;
	int retval;

	LOGI("%s info: %s\n", "recovery_erase_flash",
			"recovery_erase_flash");

	retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
			recovery_hcd->pdt_entry[2] +
			RECOVERY_F35_COMMAND_OFFSET, &command, 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write F$35 command\n",
				"recovery_erase_flash", 0x3dc);
		return retval;
	}

	if (recovery_hcd->pdt_entry[1]) {
		retval = recovery_poll_erase_completion();
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to wait for erase completion\n",
					"recovery_erase_flash", 0x3e4);
			return retval;
		}
	} else {
		/* older parts have no command register, the erase is blind */
		msleep(RECOVERY_ERASE_SETTLE_MS);
	}

	retval = recovery_check_status();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to get no error recovery mode status\n",
				"recovery_erase_flash", 0x3ee);
		return retval;
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Flash erased\n",
			"recovery_do_f35_recovery");

	return 0;
}

static int recovery_device_reset(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	u8 command = RECOVERY_F35_CMD_RESET;
	int retval;

	retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
			recovery_hcd->pdt_entry[2] +
			RECOVERY_F35_COMMAND_OFFSET, &command, 1);
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write F$35 command\n",
				"recovery_device_reset", 0x278);
		return retval;
	}

	msleep(bdata->reset_delay_ms);

	dev_notice(&tcm_hcd->pdev->dev, "%s info: End of recovery\n",
			"recovery_do_f35_recovery");

	return 0;
}

static int recovery_write_flash(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	unsigned int offset = recovery_hcd->data_offset;
	unsigned int written = 0;
	int retval;

	/*
	 * The command byte follows the chunk in memory, which is why the write
	 * below sends one byte more than the chunk itself.
	 */
	recovery_hcd->flash_command = RECOVERY_F35_CMD_WRITE_FLASH;

	while (written < offset) {
		if (written + RECOVERY_F35_CHUNK_SIZE > offset) {
			LOGE("%s error: (line %d) Failed to copy chunk data\n",
					"recovery_write_flash", 0x35d);
			return -EINVAL;
		}

		memcpy(recovery_hcd->chunk,
				recovery_hcd->data_buf + written,
				RECOVERY_F35_CHUNK_SIZE);

		retval = tcm_hcd->hw_if->io->rmi_write(tcm_hcd,
				recovery_hcd->pdt_entry[2] +
				RECOVERY_F35_COMMAND_OFFSET,
				recovery_hcd->chunk,
				RECOVERY_F35_CHUNK_PAYLOAD);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write chunk data\n",
					"recovery_write_flash", 0x367);
			return retval;
		}

		written += RECOVERY_F35_CHUNK_SIZE;
	}

	retval = recovery_check_status();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to get no error recovery mode status\n",
				"recovery_write_flash", 0x372);
		return retval;
	}

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Flash written\n",
			"recovery_do_f35_recovery");

	return recovery_device_reset();
}

static int recovery_set_up_recovery_mode(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	u8 entry[6];
	u8 *payload = (u8 *)&recovery_hcd->reboot_cmd + 1;
	int retval;

	if (recovery_hcd->f35_mode) {
		retval = tcm_hcd->identify_fn(tcm_hcd, true);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to do identification\n",
					"recovery_set_up_recovery_mode", 0x3fe);
			return retval;
		}

		if (tcm_hcd->identify_info[1] == 1) {
			retval = tcm_hcd->switch_mode_fn(tcm_hcd,
					MODE_LOAD_BOOTLOADER);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to enter bootloader mode\n",
					"recovery_set_up_recovery_mode", 0x406);
				return retval;
			}
		}

		retval = tcm_write_message(tcm_hcd,
				recovery_hcd->reboot_cmd, payload, 2,
				NULL, NULL, NULL, NULL, 0);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to write command %s\n",
					"recovery_set_up_recovery_mode", 0x417,
					"CMD_REBOOT_TO_ROM_BOOTLOADER");
			return retval;
		}

		/* give the part the time it needs to come back in ROM mode */
		msleep(bdata->reset_delay_ms);
	}

	retval = tcm_hcd->hw_if->io->rmi_read(tcm_hcd,
			RECOVERY_F35_PDT_ENTRY_ADDR, entry, sizeof(entry));
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to read PDT entry\n",
				"recovery_do_f35_recovery", 0x440);
		return retval;
	}

	if (entry[5] != RECOVERY_F35_ID) {
		LOGE("%s error: (line %d) Failed to find F$35\n",
				"recovery_do_f35_recovery", 0x446);
		return -ENODEV;
	}

	recovery_hcd->pdt_entry[0] = entry[0];
	recovery_hcd->pdt_entry[1] = entry[1];
	recovery_hcd->pdt_entry[2] = entry[2];
	recovery_hcd->pdt_entry[3] = entry[3];

	dev_notice(&tcm_hcd->pdev->dev, "%s info: Start of recovery\n",
			"recovery_do_f35_recovery");

	return 0;
}

static int recovery_do_f35_recovery(void)
{
	struct syna_tcm_hcd *tcm_hcd = recovery_hcd->tcm_hcd;
	int retval;

	retval = recovery_parse_f35_ihex();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to parse ihex data\n",
				"recovery_do_f35_recovery", 0x429);
		return retval;
	}

	retval = recovery_set_up_recovery_mode();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to set up recovery mode\n",
				"recovery_do_f35_recovery", 0x431);
		return retval;
	}

	retval = recovery_erase_flash();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to erase flash\n",
				"recovery_do_f35_recovery", 0x455);
		return retval;
	}

	retval = recovery_write_flash();
	if (retval < 0) {
		LOGE("%s error: (line %d) Failed to write to flash\n",
				"recovery_do_f35_recovery", 0x45f);
		return retval;
	}

	/* in ROM mode the part has to be brought back to the application */
	if (!recovery_hcd->f35_mode) {
		retval = tcm_hcd->enable_irq_fn(tcm_hcd, true, false);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to enable interrupt\n",
					"recovery_do_f35_recovery", 0x47a);
			return retval;
		}

		retval = tcm_hcd->identify_fn(tcm_hcd, true);
		if (retval < 0) {
			LOGE("%s error: (line %d) Failed to do identification\n",
					"recovery_do_f35_recovery", 0x481);
			return retval;
		}

		if ((u8)(tcm_hcd->identify_info[1] - 1) >= 2) {
			retval = tcm_hcd->switch_mode_fn(tcm_hcd,
					MODE_LOAD_APPLICATION);
			if (retval < 0) {
				LOGE("%s error: (line %d) Failed to run application firmware\n",
					"recovery_do_f35_recovery", 0x489);
				return retval;
			}
		}
	}

	return 0;
}

static ssize_t recovery_sysfs_f35_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct syna_tcm_hcd *tcm_hcd;
	unsigned int input = 0;
	unsigned int records;
	int retval;

	retval = sscanf(buf, "%u", &input);
	if (retval != 1)
		return -EINVAL;

	if (input == 1)
		recovery_hcd->f35_mode = RECOVERY_F35_MODE_DIRECT;
	else if (input == 2)
		recovery_hcd->f35_mode = RECOVERY_F35_MODE_ROM;
	else
		return -EINVAL;

	tcm_hcd = recovery_hcd->tcm_hcd;

	mutex_lock(&tcm_hcd->extif_mutex);

	if (!recovery_hcd->ihex_size) {
		LOGE("%s error: (line %d) Failed to get ihex data\n",
				"recovery_sysfs_f35_recovery_store", 0x22b);
		retval = -EINVAL;
		goto out;
	}

	if (recovery_hcd->ihex_size % RECOVERY_IHEX_RECORD_SIZE) {
		LOGE("%s error: (line %d) Invalid ihex data\n",
				"recovery_sysfs_f35_recovery_store", 0x232);
		retval = -EINVAL;
		goto out;
	}

	records = recovery_hcd->ihex_size / RECOVERY_IHEX_RECORD_SIZE;

	recovery_hcd->record_count = records;
	recovery_hcd->data_offset = 0;

	retval = recovery_do_f35_recovery();
	if (retval < 0)
		LOGE("%s error: (line %d) Failed to do recovery\n",
				"recovery_sysfs_f35_recovery_store", 0x23c);

out:
	recovery_hcd->f35_mode = RECOVERY_F35_MODE_DIRECT;
	mutex_unlock(&tcm_hcd->extif_mutex);

	return retval;
}
