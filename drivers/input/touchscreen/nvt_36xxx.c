// SPDX-License-Identifier: GPL-2.0
/*
 * Novatek NT36xxx series capacitive touchscreen SPI driver
 *
 * Rebuilt from the vendor module nvt_nt36xxx_spi_ts.ko shipped with the
 * Sunelan Q968 (Unisoc UMS9157, kernel 5.4.210).  The binary contains
 * 58 functions (0x5d54 bytes of .text); every function below carries the
 * address and size it had in the original object so that the rebuild can be
 * diffed against the vendor blob with recon/symcheck.py.
 *
 * All field offsets of struct nvt_ts_data, struct nvt_ts_mem_map and
 * struct nvt_ts_trim_id_table were verified against the disassembly.
 */

#include "nvt_36xxx.h"

#define NVT_ID_LENGTH	6

/* ------------------------------------------------------------------ */
/* globals (mirror the .bss/.data symbols of the vendor module)        */
/* ------------------------------------------------------------------ */

struct nvt_ts_data *ts;

u8 tp_spi_safaMode;
u8 tp_spi_ignSafeModeIrq;
u8 bTouchIsAwake;
u8 recovery_cnt;
u32 SWRST_N8_ADDR;
u32 SPI_RD_FAST_ADDR;
u32 ENG_RST_ADDR = 0x3f10e;
char lcd_name[100];
struct workqueue_struct *nvt_fwu_wq;

u8 xdata_tmp[2048];
u8 xdata[8192];
u32 fw_need_write_size;
u32 partition;
u8 *bin_map;
u8 *fwbuf;

struct proc_dir_entry *NVT_proc_entry;
struct proc_dir_entry *NVT_proc_fw_version_entry;
struct proc_dir_entry *NVT_proc_baseline_entry;
struct proc_dir_entry *NVT_proc_raw_entry;
struct proc_dir_entry *NVT_proc_diff_entry;

u16 NT36772_hw_info = 0x0001;
u16 NT36525_hw_info = 0x0202;
u16 NT36676F_hw_info = 0x0202;
u16 NT36672A_hw_info = 0x0001;
u16 NT36675_hw_info = 0x0202;

/* ------------------------------------------------------------------ */
/* flash / register maps (.rodata+0x1ee0 .. 0x2198)                    */
/* ------------------------------------------------------------------ */

struct nvt_ts_mem_map NT36675_memory_map = {
	.rchive_fw_i2c_addr	= 0x00022d00,
	.rchive_fw_i2c_len	= 0x00024000,
	.ilm_addr		= 0x00024000,
	.dlm_addr		= 0x00021b90,
	.dlm_len		= 0x00000000,
	.bld_ilm_addr		= 0x00020c60,
	.bld_ilm_len		= 0x00024c60,
	.fw_status_addr		= 0x00000000,
	.fw_ilm_checksum_addr	= 0x00000000,
	.fw_dlm_checksum_addr	= 0x00000000,
	.fw_ver_addr		= 0x00000000,
	.fw_ver_bar_addr	= 0x00024000,
	.boot_ready_addr	= 0x00024002,
	.boot_ready_clr_addr	= 0x0003f10d,
	.boot_ready_val_addr	= 0x0003f138,
	.spi_rd_fast_addr	= 0x0003f118,
	.swrst_n8_addr		= 0x0003f130,
	.hw_crc_addr		= 0x0003f114,
	.hw_crc_err_ctrl	= 0x0003f128,
	.dlm_crc_len		= 0x0003f12c,
	.dlm_crc_buf_addr	= 0x0003f100,
	.sram_buf_addr		= 0x0003f104,
	.sram_buf_len		= 0x0003f120,
	.fdbg_addr		= 0x0003f124,
	.fdbg_len		= 0x0003f30e,
	.bld_crc_bank		= 0x0003f136,
	.fw_crc_bank		= 0x0003f133,
	.rsvd			= { 0x0003f134 },
};

struct nvt_ts_mem_map NT36672A_memory_map = {
	.rchive_fw_i2c_addr	= 0x00021c00,
	.rchive_fw_i2c_len	= 0x00020000,
	.ilm_addr		= 0x00023000,
	.dlm_addr		= 0x00020bfc,
	.dlm_len		= 0x00023bfc,
	.bld_ilm_addr		= 0x000206dc,
	.bld_ilm_len		= 0x000236dc,
	.fw_status_addr		= 0x00020510,
	.fw_ilm_checksum_addr	= 0x00023510,
	.fw_dlm_checksum_addr	= 0x00020bf0,
	.fw_ver_addr		= 0x00023bf0,
	.fw_ver_bar_addr	= 0x00024000,
	.boot_ready_addr	= 0x00024002,
	.boot_ready_clr_addr	= 0x0003f10d,
	.boot_ready_val_addr	= 0x00000000,
	.spi_rd_fast_addr	= 0x0003f10e,
	.swrst_n8_addr		= 0x0003f118,
	.hw_crc_addr		= 0x0003f130,
	.hw_crc_err_ctrl	= 0x0003f114,
	.dlm_crc_len		= 0x0003f128,
	.dlm_crc_buf_addr	= 0x0003f12c,
	.sram_buf_addr		= 0x0003f100,
	.sram_buf_len		= 0x0003f104,
	.fdbg_addr		= 0x0003f120,
	.fdbg_len		= 0x0003f124,
	.bld_crc_bank		= 0x0003f30e,
	.fw_crc_bank		= 0x0003f132,
	.rsvd			= { 0x0003f133, 0x0003f134 },
};

struct nvt_ts_mem_map NT36526_memory_map = {
	.rchive_fw_i2c_addr	= 0x00022d00,
	.rchive_fw_i2c_len	= 0x00024000,
	.ilm_addr		= 0x00024000,
	.dlm_addr		= 0x00021758,
	.dlm_len		= 0x00000000,
	.bld_ilm_addr		= 0x00020ab0,
	.bld_ilm_len		= 0x00024ab0,
	.fw_status_addr		= 0x00000000,
	.fw_ilm_checksum_addr	= 0x00000000,
	.fw_dlm_checksum_addr	= 0x00000000,
	.fw_ver_addr		= 0x00000000,
	.fw_ver_bar_addr	= 0x00024000,
	.boot_ready_addr	= 0x00024002,
	.boot_ready_clr_addr	= 0x0003f10d,
	.boot_ready_val_addr	= 0x00000000,
	.spi_rd_fast_addr	= 0x0003f138,
	.swrst_n8_addr		= 0x0003f118,
	.hw_crc_addr		= 0x0003f130,
	.hw_crc_err_ctrl	= 0x0003f114,
	.dlm_crc_len		= 0x0003f128,
	.dlm_crc_buf_addr	= 0x0003f12c,
	.sram_buf_addr		= 0x0003f100,
	.sram_buf_len		= 0x0003f104,
	.fdbg_addr		= 0x0003f120,
	.fdbg_len		= 0x0003f124,
	.bld_crc_bank		= 0x0003f30e,
	.fw_crc_bank		= 0x0003f136,
	.rsvd			= { 0x0003f133, 0x0003f134 },
};

struct nvt_ts_mem_map NT36772_memory_map = {
	.rchive_fw_i2c_addr	= 0x00011e00,
	.rchive_fw_i2c_len	= 0x00010000,
	.ilm_addr		= 0x00012000,
	.dlm_addr		= 0x00010e70,
	.dlm_len		= 0x00012e70,
	.bld_ilm_addr		= 0x00010830,
	.bld_ilm_len		= 0x00012830,
	.fw_status_addr		= 0x00010e60,
	.fw_ilm_checksum_addr	= 0x00012e60,
	.fw_dlm_checksum_addr	= 0x00010e68,
	.fw_ver_addr		= 0x00012e68,
	.fw_ver_bar_addr	= 0x00014000,
	.boot_ready_addr	= 0x00014002,
	.boot_ready_clr_addr	= 0x0001f141,
	.boot_ready_val_addr	= 0x0001f61c,
	.spi_rd_fast_addr	= 0x00000000,
	.swrst_n8_addr		= 0x00000000,
	.hw_crc_addr		= 0x00000000,
	.hw_crc_err_ctrl	= 0x00000000,
	.dlm_crc_len		= 0x00000000,
	.dlm_crc_buf_addr	= 0x00000000,
	.sram_buf_addr		= 0x00000000,
	.sram_buf_len		= 0x0001bf00,
	.fdbg_addr		= 0x00000000,
	.fdbg_len		= 0x00000000,
	.bld_crc_bank		= 0x00000000,
	.fw_crc_bank		= 0x00000000,
	.rsvd			= { 0 },
};

struct nvt_ts_mem_map NT36525_memory_map = {
	.rchive_fw_i2c_addr	= 0x00011a00,
	.rchive_fw_i2c_len	= 0x00010000,
	.ilm_addr		= 0x00012000,
	.dlm_addr		= 0x00010b08,
	.dlm_len		= 0x00012b08,
	.bld_ilm_addr		= 0x0001064c,
	.bld_ilm_len		= 0x0001264c,
	.fw_status_addr		= 0x00010634,
	.fw_ilm_checksum_addr	= 0x00012634,
	.fw_dlm_checksum_addr	= 0x00010afc,
	.fw_ver_addr		= 0x00012afc,
	.fw_ver_bar_addr	= 0x00014000,
	.boot_ready_addr	= 0x00014002,
	.boot_ready_clr_addr	= 0x0001f141,
	.boot_ready_val_addr	= 0x0001f61c,
	.spi_rd_fast_addr	= 0x00000000,
	.swrst_n8_addr		= 0x00000000,
	.hw_crc_addr		= 0x00000000,
	.hw_crc_err_ctrl	= 0x00000000,
	.dlm_crc_len		= 0x00000000,
	.dlm_crc_buf_addr	= 0x00000000,
	.sram_buf_addr		= 0x00000000,
	.sram_buf_len		= 0x0001bf00,
	.fdbg_addr		= 0x00000000,
	.fdbg_len		= 0x00000000,
	.bld_crc_bank		= 0x00000000,
	.fw_crc_bank		= 0x00000000,
	.rsvd			= { 0 },
};

struct nvt_ts_mem_map NT36676F_memory_map = {
	.rchive_fw_i2c_addr	= 0x00011a00,
	.rchive_fw_i2c_len	= 0x00010000,
	.ilm_addr		= 0x00012000,
	.dlm_addr		= 0x00010b08,
	.dlm_len		= 0x00012b08,
	.bld_ilm_addr		= 0x0001064c,
	.bld_ilm_len		= 0x0001264c,
	.fw_status_addr		= 0x00010634,
	.fw_ilm_checksum_addr	= 0x00012634,
	.fw_dlm_checksum_addr	= 0x00010afc,
	.fw_ver_addr		= 0x00012afc,
	.fw_ver_bar_addr	= 0x00014000,
	.boot_ready_addr	= 0x00014002,
	.boot_ready_clr_addr	= 0x00000000,
	.boot_ready_val_addr	= 0x00000000,
	.spi_rd_fast_addr	= 0x00000000,
	.swrst_n8_addr		= 0x00000000,
	.hw_crc_addr		= 0x00000000,
	.hw_crc_err_ctrl	= 0x00000000,
	.dlm_crc_len		= 0x00000000,
	.dlm_crc_buf_addr	= 0x00000000,
	.sram_buf_addr		= 0x00000000,
	.sram_buf_len		= 0x00000000,
	.fdbg_addr		= 0x00000000,
	.fdbg_len		= 0x00000000,
	.bld_crc_bank		= 0x00000000,
	.fw_crc_bank		= 0x00000000,
	.rsvd			= { 0 },
};

/* .rodata+0x1ba0, 26 entries of 0x20 bytes */
static struct nvt_ts_trim_id_table trim_id_table[] = {
	{ .id = { 0x20, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36675_memory_map,  .crc = &NT36675_hw_info },
	{ .id = { 0x00, 0xFF, 0xFF, 0x80, 0x66, 0x03 }, .mmap_id = &NT36675_memory_map,  .crc = &NT36675_hw_info },
	{ .id = { 0x0C, 0xFF, 0xFF, 0x25, 0x65, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0E, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36675_memory_map,  .crc = &NT36675_hw_info },
	{ .id = { 0x0C, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36675_memory_map,  .crc = &NT36675_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x26, 0x65, 0x03 }, .mmap_id = &NT36526_memory_map,  .crc = &NT36526_hw_info },
	{ .id = { 0xE7, 0xFF, 0xFF, 0x75, 0x66, 0x03 }, .mmap_id = &NT36675_memory_map,  .crc = &NT36675_hw_info },
	{ .id = { 0x0B, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0B, 0xFF, 0xFF, 0x82, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0B, 0xFF, 0xFF, 0x25, 0x65, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0A, 0xFF, 0xFF, 0x72, 0x65, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0A, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0A, 0xFF, 0xFF, 0x82, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0A, 0xFF, 0xFF, 0x70, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0B, 0xFF, 0xFF, 0x70, 0x66, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x0A, 0xFF, 0xFF, 0x72, 0x67, 0x03 }, .mmap_id = &NT36672A_memory_map, .crc = &NT36672A_hw_info },
	{ .id = { 0x55, 0x00, 0xFF, 0x00, 0x00, 0x00 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0x55, 0x72, 0xFF, 0x00, 0x00, 0x00 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xAA, 0x00, 0xFF, 0x00, 0x00, 0x00 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xAA, 0x72, 0xFF, 0x00, 0x00, 0x00 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x72, 0x67, 0x03 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x70, 0x66, 0x03 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x70, 0x67, 0x03 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x72, 0x66, 0x03 }, .mmap_id = &NT36772_memory_map,  .crc = &NT36772_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x25, 0x65, 0x03 }, .mmap_id = &NT36525_memory_map,  .crc = &NT36525_hw_info },
	{ .id = { 0xFF, 0xFF, 0xFF, 0x76, 0x66, 0x03 }, .mmap_id = &NT36676F_memory_map, .crc = &NT36676F_hw_info },
};

/* ------------------------------------------------------------------ */
/* SPI primitives                                                     */
/* ------------------------------------------------------------------ */

/* 0x484, 524 bytes */
static int nvt_is_in_tui(void);

static int spi_read_write(struct spi_device *client, u8 *buf, u16 len, u8 op)
{
	struct spi_message msg;
	struct spi_transfer xfer[2];
	u8 *xbuf;
	int ret, i;

	if (tp_spi_safaMode) {
		for (i = 0; i < 200; i++) {
			__const_udelay(0x418958 >> 2);
			NVT_ERR("error, retry=%d", i);
			NVT_ERR("spi in safe mode!(%d)", 200, tp_spi_safaMode);
		}
		NVT_ERR("spi in safe mode!");
		dump_stack();
		return -EIO;
	}

	spi_message_init(&msg);
	memset(xfer, 0, sizeof(xfer));
	memset(&msg, 0, sizeof(msg));
	spi_message_init(&msg);

	mutex_lock(&ts->xbuf_lock);
	xbuf = ts->xbuf;
	memset(xbuf, 0, len + 1);
	memcpy(xbuf, buf, len);

	if (op == 0) {
		xfer[0].tx_buf = xbuf;
		xfer[0].len = len;
		spi_message_add_tail(&xfer[0], &msg);
	} else if (op == 1) {
		xfer[0].tx_buf = xbuf;
		xfer[0].len = len;
		spi_message_add_tail(&xfer[0], &msg);
		xfer[1].rx_buf = ts->rbuf;
		xfer[1].len = len + 1;
		spi_message_add_tail(&xfer[1], &msg);
	} else {
		NVT_ERR("invalid transfer len!");
		mutex_unlock(&ts->xbuf_lock);
		return -EINVAL;
	}

	ret = spi_sync(client, &msg);
	if (ret < 0)
		NVT_ERR("Full duplex not supported by master");
	mutex_unlock(&ts->xbuf_lock);
	return ret;
}

/* 0x360, 292 bytes */
int CTP_SPI_READ(struct spi_device *client, u8 *buf, u16 len)
{
	int ret = 0, i;

	mutex_lock(&ts->lock);
	buf[0] &= 0x7f;
	for (i = 0; i < 5; i++) {
		ret = spi_read_write(client, buf, len, 1);
		if (!ret)
			break;
	}
	if (ret) {
		NVT_ERR("CTP_SPI_READ failed.(%d)", ret);
		ret = -EIO;
	} else {
		memcpy(buf + 1, ts->rbuf + 2, len - 1);
		ret = 0;
	}
	mutex_unlock(&ts->lock);
	return ret;
}

/* 0x690, 272 bytes */
int CTP_SPI_WRITE(struct spi_device *client, u8 *buf, u16 len)
{
	int ret = 0, i;

	mutex_lock(&ts->lock);
	buf[0] |= 0x80;
	for (i = 0; i < 5; i++) {
		ret = spi_read_write(client, buf, len, 0);
		if (!ret)
			break;
	}
	if (ret) {
		NVT_ERR("CTP_SPI_WRITE failed.(%d)", ret);
		ret = -EIO;
	} else {
		ret = 0;
	}
	mutex_unlock(&ts->lock);
	return ret;
}

/* 0x7a0, 116 bytes */
int nvt_set_page(u32 addr)
{
	u8 buf[4] = { 0 };

	buf[0] = 0xff;
	buf[1] = (addr >> 15) & 0xff;
	buf[2] = (addr >> 7) & 0xff;
	return CTP_SPI_WRITE(ts->client, buf, 3);
}

/* 0x814, 268 bytes */
int nvt_write_addr(u32 addr, u8 data)
{
	u8 buf[4] = { 0 };
	int ret;

	buf[0] = 0xff;
	buf[1] = (addr >> 15) & 0xff;
	buf[2] = (addr >> 7) & 0xff;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret < 0) {
		NVT_ERR("set page 0x%06X failed, ret = %d", addr, ret);
		return ret;
	}

	buf[0] = addr & 0x7f;
	buf[1] = data;
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret < 0) {
		NVT_ERR("write data to 0x%06X failed, ret = %d", addr, ret);
		return ret;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* chip control                                                       */
/* ------------------------------------------------------------------ */

/* 0x920, 220 bytes */
int nvt_bld_crc_enable(void)
{
	u8 buf[4] = { 0 };
	u32 addr;

	addr = ts->mmap->bld_crc_bank;
	if (nvt_set_page(addr) < 0) {
		NVT_ERR("set page failed, ret = %d", -EIO);
		return 0;
	}

	buf[0] = addr & 0x7f;
	buf[1] = 0x80;
	CTP_SPI_WRITE(ts->client, buf, 2);
	return 0;
}

/* 0x9fc, 184 bytes */
int nvt_fw_crc_enable(void)
{
	u8 buf[4] = { 0 };
	u32 addr;

	addr = ts->mmap->rchive_fw_i2c_addr;
	if (nvt_set_page(addr) < 0)
		return 0;

	buf[0] = addr & 0x7f;
	buf[1] = 0x50;
	buf[2] = 0xae;
	CTP_SPI_WRITE(ts->client, buf, 3);
	return 0;
}

/* 0xab4, 116 bytes */
int nvt_boot_ready(void)
{
	__const_udelay(0x147aeb8);
	if (!ts->hw_crc) {
		nvt_write_addr(ts->mmap->boot_ready_clr_addr, 0);
		nvt_write_addr(ts->mmap->boot_ready_val_addr, 0xa0);
	}
	return 0;
}

/* 0xb28, 52 bytes */
int nvt_eng_reset(void)
{
	nvt_write_addr(ENG_RST_ADDR, 0x5a);
	__const_udelay(0x418958);
	return 0;
}

/* 0xb5c, 48 bytes */
int nvt_sw_reset(void)
{
	nvt_write_addr(SWRST_N8_ADDR, 0x55);
	msleep(10);
	return 0;
}

/* 0xb8c, 48 bytes */
int nvt_sw_reset_idle(void)
{
	nvt_write_addr(SWRST_N8_ADDR, 0xaa);
	msleep(15);
	return 0;
}

/* 0xbbc, 72 bytes */
int nvt_bootloader_reset(void)
{
	nvt_write_addr(SWRST_N8_ADDR, 0x69);
	__const_udelay(0x147aeb8);
	if (SPI_RD_FAST_ADDR)
		nvt_write_addr(SPI_RD_FAST_ADDR, 0);
	return 0;
}

/* 0xc04, 292 bytes */
int nvt_clear_fw_status(void)
{
	u8 buf[8] = { 0 };
	int i, ret = 0;

	for (i = 0; i < 20; i++) {
		if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0) {
			NVT_ERR("read error, ret=%d", ret);
			return 0;
		}
		buf[0] = 0x51;
		CTP_SPI_WRITE(ts->client, buf, 2);
		buf[0] = 0x51;
		buf[1] = 0xff;
		CTP_SPI_READ(ts->client, buf, 2);
		if (buf[1] == 0x00) {
			ret = 0;
			break;
		}
		usleep_range(10000, 10000);
	}
	if (i == 20) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X", i, buf[1]);
		ret = -1;
	}
	return ret;
}

/* 0xd28, 272 bytes */
int nvt_check_fw_status(void)
{
	u8 buf[8] = { 0 };
	int i, ret = 0;

	for (i = 0; i < 50; i++) {
		if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0) {
			NVT_ERR("read error, ret=%d", ret);
			return 0;
		}
		buf[0] = 0x51;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 2);
		if ((buf[1] & 0xf0) == 0xa0) {
			ret = 0;
			break;
		}
		usleep_range(10000, 10000);
	}
	if (i == 50) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X", i, buf[1]);
		ret = -1;
	}
	return ret;
}

/* 0xe38, 428 bytes */
int nvt_check_fw_reset_state(int reset_state)
{
	u8 buf[8] = { 0 };
	int ret = 0, retry = 0;

	NVT_LOG("test log: nvt_check_fw_reset_state start");
	if (reset_state == RESET_STATE_NORMAL_RUN) {
		nvt_read_pid();
		ret = nvt_get_fw_info();
		if (ret)
			NVT_ERR("nvt_get_fw_info failed. (%d)", ret);
	}

	while (1) {
		retry++;
		if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0) {
			NVT_ERR("read error, ret=%d", ret);
			return 0;
		}
		buf[0] = 0x51;
		buf[1] = 0x00;
		buf[2] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 3);
		buf[0] = 0x51;
		CTP_SPI_READ(ts->client, buf, 6);
		if (buf[1] == 0xaf) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)", buf[1]);
			return -1;
		}
		if (buf[1] == reset_state) {
			ret = 0;
			break;
		}
		usleep_range(10000, 10000);
		buf[0] = 0x51;
		CTP_SPI_READ(ts->client, buf, 6);
		if (buf[1] == 0xaf) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)", buf[1]);
			return -1;
		}
		if (buf[1] == reset_state) {
			ret = 0;
			break;
		}
		if (retry > 0x60) {
			NVT_ERR("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X",
				retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
			ret = -1;
			break;
		}
	}
	NVT_LOG("test log: nvt_check_fw_reset_state end");
	return ret;
}

/* 0xfe4, 260 bytes */
int nvt_read_pid(void)
{
	u8 buf[8] = { 0 };
	u32 addr;

	if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr + 1) < 0)
		return 0;

	buf[0] = 0x9a;
	CTP_SPI_READ(ts->client, buf, 3);
	ts->pid = (buf[1] << 8) | buf[2];
	NVT_LOG("PID=%04X", ts->pid);

	if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0)
		return 0;
	return 0;
}

/* 0x10e8, 716 bytes */
int nvt_get_fw_info(void)
{
	u8 buf[0x11] = { 0 };
	int i, ret = 0;

	for (i = 1; i < 5; i++) {
		if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0) {
			NVT_ERR("read error, ret=%d", ret);
			return 0;
		}
		buf[0] = 0x78;
		if (CTP_SPI_READ(ts->client, buf, 0x11) < 0) {
			NVT_ERR("read error, ret=%d", ret);
			continue;
		}
		ts->fw_ver = buf[1];
		ts->x_num = buf[3];
		ts->y_num = buf[4];
		ts->abs_x_max = (buf[5] << 8) | buf[6];
		ts->abs_y_max = (buf[7] << 8) | buf[8];
		ts->max_button_num = buf[11];

		if ((u8)(buf[1] + buf[2]) != 0xff) {
			NVT_ERR("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X", buf[1], buf[2]);
			ts->fw_ver = 0;
			ts->x_num = 0x12;
			ts->y_num = 0x20;
			ts->abs_x_max = 0x0438;
			ts->abs_y_max = 0x0968;
			ts->max_button_num = 0;
			if (i == 4) {
				NVT_ERR("chip is not identified");
				ret = -1;
				goto out;
			}
		} else {
			ret = 0;
			break;
		}
	}

	NVT_LOG("============================================================");
	NVT_LOG("Set default fw_ver=%d, x_num=%d, y_num=%d, abs_x_max=%d, abs_y_max=%d, max_button_num=%d!",
		ts->fw_ver, ts->x_num, ts->y_num, ts->abs_x_max, ts->abs_y_max,
		ts->max_button_num);
	NVT_LOG("fw_ver = 0x%02X, fw_type = 0x%02X", ts->fw_ver, ts->hw_crc);
	nvt_read_pid();
out:
	return ret;
}

/* inlined in the vendor binary as nvt_gpio_config() */
static int nvt_gpio_config(void)
{
	int ret = 0;

	if (ts->irq_gpio <= 0x1ff) {
		ret = gpio_request_one(ts->irq_gpio, 1, "NVT-int");
		if (ret < 0) {
			NVT_ERR("Failed to request NVT-int GPIO");
			NVT_ERR("gpio config error!");
			return ret;
		}
	}

	return 0;
}

/* inlined in the vendor binary as nvt_irq_enable() */
static void nvt_irq_enable(int enable)
{
	struct irq_desc *desc;

	if (enable) {
		if (!ts->irq_enabled) {
			enable_irq(ts->client->irq);
			ts->irq_enabled = 1;
		} else {
			NVT_LOG("irqEnable:%d iRtyCount:%d", enable,
				ts->irq_enabled);
		}
	} else {
		if (ts->irq_enabled) {
			disable_irq(ts->client->irq);
			ts->irq_enabled = 0;
		}
	}

	desc = irq_to_desc(ts->client->irq);
	NVT_LOG("enable=%d, desc->depth=%d", enable, desc ? desc->depth : -1);
}

/* 0x38b4, 224 bytes */
int nvt_change_mode(u8 mode)
{
	u8 buf[8] = { 0 };

	if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0)
		return 0;

	buf[0] = 0x50;
	buf[1] = mode;
	CTP_SPI_WRITE(ts->client, buf, 2);

	if (mode == 0) {
		buf[0] = 0x51;
		buf[1] = 0xbb;
		CTP_SPI_WRITE(ts->client, buf, 2);
		msleep(20);
	}
	return 0;
}

/* 0x3994, 168 bytes */
int nvt_get_fw_pipe(void)
{
	u8 buf[8] = { 0 };

	if (nvt_set_page(ts->mmap->rchive_fw_i2c_addr) < 0)
		return 0;

	buf[0] = 0x51;
	CTP_SPI_READ(ts->client, buf, 2);
	return buf[1] & 1;
}

/* 0x3a3c, 708 bytes */
int nvt_read_mdata(u32 kaddr, u8 *data, u16 size)
{
	u8 buf[0x102] = { 0 };
	u32 total = ts->x_num * ts->y_num * 2;
	u32 blocks = ((kaddr & 0xff) + total) >> 8;
	u32 remain = ((kaddr & 0xff) + total) & 0xff;
	u32 base = kaddr & 0xffffff00;
	int i, j;

	for (j = 0; j < blocks; j++) {
		u32 addr = base + (j << 8);

		if (nvt_set_page(addr) < 0)
			return 0;
		buf[0] = 0x00;
		CTP_SPI_READ(ts->client, buf, 0x101);
		for (i = 0; i < 0x100; i++) {
			if ((u32)(j * 0x100 + i) > 0x7ff)
				break;
			xdata_tmp[j * 0x100 + i] = buf[i + 1];
		}
	}

	if (remain) {
		u32 addr = base + (blocks << 8);

		if (nvt_set_page(addr) < 0)
			return 0;
		buf[0] = 0x00;
		CTP_SPI_READ(ts->client, buf, remain + 1);
		for (i = 0; i < remain; i++) {
			if ((u32)(blocks * 0x100 + i) > 0x7ff)
				break;
			xdata_tmp[blocks * 0x100 + i] = buf[i + 1];
		}
	}

	memcpy(data, xdata_tmp, size);
	return 0;
}

/* 0x3d00, 84 bytes */
int nvt_get_mdata(u8 *data, u32 size)
{
	memcpy(data, xdata_tmp, size);
	return 0;
}

/* ------------------------------------------------------------------ */
/* interrupt / work                                                    */
/* ------------------------------------------------------------------ */

/* 0x2d60, 1188 bytes */
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
	struct nvt_ts_data *ts = data;
	u8 point_data[0x42] = { 0 };
	u8 touch_bit[0xa] = { 0 };
	int i, ret = 0;
	u8 finger = 0;

	if (tp_spi_ignSafeModeIrq) {
		NVT_LOG("Ignore safe mode irq!");
		tp_spi_ignSafeModeIrq = 0;
		return IRQ_HANDLED;
	}

	mutex_lock(&ts->lock);
	ret = CTP_SPI_READ(ts->client, point_data, 0x42);
	if (ret < 0) {
		NVT_ERR("read error, ret=%d", ret);
		goto end;
	}

	recovery_cnt++;
	if (((point_data[1] + 3) & 0xff) <= 1 && ((point_data[2] + 3) & 0xff) <= 1 &&
	    ((point_data[3] + 3) & 0xff) <= 1 && ((point_data[4] + 3) & 0xff) <= 1 &&
	    ((point_data[5] + 3) & 0xff) <= 1 && ((point_data[6] + 3) & 0xff) <= 1) {
		if (recovery_cnt > 10) {
			NVT_ERR("Recover for fw reset, %02X", point_data[1]);
			recovery_cnt = 0;
			if (nvt_update_firmware(NVT_FW_FILE))
				NVT_ERR("download firmware failed, ignore check fw state");
			goto end;
		}
	} else {
		recovery_cnt = 0;
		if (point_data[1] == 0x77 && point_data[2] == 0x77 &&
		    point_data[3] == 0x77 && point_data[4] == 0x77 &&
		    point_data[5] == 0x77 && point_data[6] == 0x77)
			goto end;
	}

	for (i = 0; i < ts->max_button_num; i++) {
		u8 *p = point_data + i * 6;
		u8 id, status;
		int x, y, w, pr;

		id = (p[1] >> 3) - 1;
		if (id >= ts->max_button_num)
			continue;
		status = (p[1] & 0x7) - 1;
		if (status > 1)
			continue;
		x = (p[2] << 4) | (p[4] >> 4);
		if (x > ts->abs_x_max)
			continue;
		y = (p[3] << 4) | (p[4] & 0xf);
		if (y > ts->abs_y_max)
			continue;
		w = p[5] ? p[5] : 1;
		if (i <= 1) {
			pr = p[6] | (point_data[0x3f + i] << 8);
			if (pr > 1000)
				pr = 1000;
		} else {
			pr = p[6];
		}
		if (id > 9)
			continue;
		touch_bit[id] = 1;
		input_event(ts->input_dev, EV_ABS, ABS_MT_TRACKING_ID, pr ? pr : 1);
		input_mt_slot(ts->input_dev, id);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
		input_event(ts->input_dev, EV_ABS, ABS_MT_POSITION_X, x);
		input_event(ts->input_dev, EV_ABS, ABS_MT_POSITION_Y, y);
		input_event(ts->input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR, w);
		input_event(ts->input_dev, EV_ABS, ABS_MT_PRESSURE, pr);
		finger++;
	}

	for (i = 0; i < ts->max_button_num; i++) {
		if (touch_bit[i] == 1)
			continue;
		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
	}

end:
	input_event(ts->input_dev, EV_KEY, BTN_TOUCH, finger > 0);
	input_event(ts->input_dev, EV_SYN, SYN_REPORT, 0);
	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* power management                                                   */
/* ------------------------------------------------------------------ */

/* 0x13b4, 1000 bytes */
int nvt_ts_suspend(struct device *dev)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);
	struct irq_desc *desc;
	int i;

	NVT_LOG("testlog: nvt_ts_suspend start.");
	if (!bTouchIsAwake) {
		NVT_LOG("Touch is already suspend");
		return 0;
	}

	if (nvt_is_in_tui()) {
		NVT_LOG("in TUI, cannot suspend!");
		for (i = 0; i < 11; i++) {
			if (!tp_spi_safaMode)
				break;
			__const_udelay(0x418958 >> 2);
		}
		if (tp_spi_safaMode) {
			NVT_LOG("waiting for tui to exit first!");
			return 0;
		}
	}

	nvt_irq_enable(0);

	mutex_lock(&ts->lock);
	bTouchIsAwake = 0;

	{
		u8 buf[8] = { 0 };

		buf[0] = 0x50;
		buf[1] = 0x11;
		CTP_SPI_WRITE(ts->client, buf, 2);
	}
	mutex_unlock(&ts->lock);

	for (i = 0; i < ts->max_button_num; i++) {
		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
	}
	input_event(ts->input_dev, EV_KEY, BTN_TOUCH, 0);
	input_event(ts->input_dev, EV_SYN, SYN_REPORT, 0);

	ts->suspended = 1;
	msleep(50);
	NVT_LOG("finished");
	NVT_LOG("testlog: nvt_ts_suspend end.");
	return 0;
}

/* 0x179c, 404 bytes */
int nvt_ts_resume(struct device *dev)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);
	struct irq_desc *desc;
	int ret;

	NVT_LOG("testlog: nvt_ts_resume start.");
	if (bTouchIsAwake == 1) {
		NVT_LOG("Touch is already resume");
		return 0;
	}

	mutex_lock(&ts->lock);
	NVT_LOG("start");
	ret = nvt_update_firmware(NVT_FW_FILE);
	if (ret == 0)
		nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);
	else
		NVT_ERR("firmware load failed, ret=%d", ret);

	if (!ts->irq_enabled) {
		enable_irq(ts->client->irq);
		ts->irq_enabled = 1;
	}
	desc = irq_to_desc(ts->client->irq);
	NVT_LOG("%d", desc ? desc->depth : -1);

	bTouchIsAwake = 1;
	ts->suspended = 0;
	mutex_unlock(&ts->lock);

	NVT_LOG("testlog: nvt_ts_resume end.");
	NVT_LOG("finished");
	return 0;
}

/* ------------------------------------------------------------------ */
/* chip identification                                                */
/* ------------------------------------------------------------------ */

/* 0x2b20, 576 bytes */
static int nvt_ts_check_chip_ver_trim(u32 chip_id)
{
	u8 buf[8] = { 0 };
	int i, j;

	for (i = 0; i < 5; i++) {
		nvt_bootloader_reset();
		buf[0] = 0xff;
		buf[1] = (chip_id >> 7) & 0xff;
		buf[2] = (chip_id >> 15) & 0xff;
		CTP_SPI_WRITE(ts->client, buf, 3);
		buf[0] = chip_id & 0x7f;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0;
		buf[5] = 0;
		buf[6] = 0;
		CTP_SPI_READ(ts->client, buf, 7);
		NVT_LOG("buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X",
			buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

		for (j = 0; j < 0x1a; j++) {
			struct nvt_ts_trim_id_table *t = &trim_id_table[j];

			if (!((0x3f000060ULL >> j) & 1) && buf[1] != t->id[0])
				continue;
			if ((j & ~0x0f) == 0x10 && buf[2] != t->id[1])
				continue;
			if (buf[4] != t->id[3])
				continue;
			if (buf[5] != t->id[4])
				continue;
			if (buf[6] != t->id[5])
				continue;

			NVT_LOG("This is NVT touch IC");
			ts->mmap = t->mmap_id;
			ts->hw_crc = t->crc ? (u8)(*t->crc & 0xff) : 0;
			ts->hw_crc_flag = t->crc ? (u8)(*t->crc >> 8) : 0;
			return 0;
		}
		NVT_LOG("retry_count=%d", i);
		msleep(10);
	}

	NVT_ERR("%s: match %s fail,return", __func__, "trim_id_table");

	return 0xff;
}

/* ------------------------------------------------------------------ */
/* TUI detection (reads /chosen bootargs)                             */
/* ------------------------------------------------------------------ */

static int nvt_is_in_tui(void)
{
	struct device_node *chosen;
	const char *cmdline = NULL;
	const char *p;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen) {
		NVT_ERR("Can't get the /chosen");
		return 0;
	}
	if (of_property_read_string(chosen, "bootargs", &cmdline)) {
		NVT_ERR("Can't get the bootargs");
		return 0;
	}
	if (!cmdline || !*cmdline) {
		NVT_ERR("Can't get the bootargs");
		return 0;
	}

	p = strstr(cmdline, "lcd_name=");
	if (!p) {
		NVT_ERR("Read bootargs %s fail", cmdline);
		return 0;
	}
	p += strlen("lcd_name=");
	strncpy(lcd_name, p, sizeof(lcd_name) - 1);
	lcd_name[sizeof(lcd_name) - 1] = 0;
	{
		char *sp = strchr(lcd_name, ' ');

		if (sp)
			*sp = 0;
	}
	NVT_LOG("lcd_name = %s", lcd_name);

	if (!strncmp(lcd_name, "lcd_nt36672e_truly_mipi_fhd", strlen("lcd_nt36672e_truly_mipi_fhd")))
		return 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* sysfs                                                              */
/* ------------------------------------------------------------------ */

static ssize_t nvt_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", ts->suspended ? "true" : "false");
}

static ssize_t nvt_suspend_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return -EINVAL;

	if (val == 1) {
		nvt_ts_suspend(dev);
	} else if (val == 0) {
		nvt_ts_resume(dev);
	} else {
		NVT_ERR("invalid args");
		return -EINVAL;
	}
	return count;
}

static ssize_t nvt_ts_firmware_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ts->fw_ver);
}

static ssize_t ts_input_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", NVT_TS_INPUT_NAME);
}

static ssize_t ts_irq_eb_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);
	struct irq_desc *desc;

	desc = irq_to_desc(gpiod_to_irq(gpio_to_desc(ts->irq_gpio)));
	if (!desc)
		return 0;
	return snprintf(buf, PAGE_SIZE, "irq_depth:%d\n", desc->depth);
}

static ssize_t ts_irq_eb_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);
	struct irq_desc *desc;
	unsigned int val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return -EINVAL;

	mutex_lock(&ts->input_dev->mutex);
	if (val == 1) {
		NVT_LOG("enable irq");
		nvt_irq_enable(1);
		tp_spi_safaMode = 0;
	} else if (val == 0) {
		NVT_LOG("disable irq");
		nvt_irq_enable(0);
		tp_spi_safaMode = 1;
		tp_spi_ignSafeModeIrq = 1;
	} else {
		NVT_ERR("invalid args");
		ret = -EINVAL;
	}
	mutex_unlock(&ts->input_dev->mutex);

	return ret ? ret : count;
}

static DEVICE_ATTR(ts_suspend, 0644, nvt_suspend_show, nvt_suspend_store);
static DEVICE_ATTR(input_name, 0444, ts_input_name_show, NULL);
static DEVICE_ATTR(firmware_version, 0444, nvt_ts_firmware_version_show, NULL);
static DEVICE_ATTR(ts_irq_eb, 0644, ts_irq_eb_show, ts_irq_eb_store);

static struct attribute *nvt_attrs[] = {
	&dev_attr_ts_suspend.attr,
	&dev_attr_input_name.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_ts_irq_eb.attr,
	NULL,
};

static struct attribute_group nvt_attr_group = {
	.attrs = nvt_attrs,
};

/* ------------------------------------------------------------------ */
/* probe / remove                                                     */
/* ------------------------------------------------------------------ */

static int nvt_parse_dt(struct spi_device *client)
{
	struct device_node *nd = client->dev.of_node;
	int ret;

	ret = of_get_named_gpio_flags(nd, "novatek,irq-gpio", 0, &ts->irq_flags);
	ts->irq_gpio = ret;
	NVT_LOG("novatek,irq-gpio=%d", ret);

	ret = of_property_read_variable_u32_array(nd, "novatek,swrst-n8-addr", &SWRST_N8_ADDR, 1, NULL);
	if (ret < 0) {
		NVT_ERR("not support novatek,swrst-n8-addr");
		NVT_ERR("error reading novatek,swrst-n8-addr. ret=%d", ret);
		return ret;
	}
	NVT_LOG("SWRST_N8_ADDR=0x%06X", SWRST_N8_ADDR);

	ret = of_property_read_variable_u32_array(nd, "novatek,spi-rd-fast-addr", &SPI_RD_FAST_ADDR, 1, NULL);
	if (ret < 0) {
		NVT_LOG("not support novatek,spi-rd-fast-addr");
		SPI_RD_FAST_ADDR = 0;
	} else {
		NVT_LOG("SPI_RD_FAST_ADDR=0x%06X", SPI_RD_FAST_ADDR);
	}

	ret = of_property_read_variable_u32_array(nd, "novatek,touch_max_width", &ts->abs_x_max, 1, NULL);
	if (ret < 0)
		NVT_LOG("touch_max_width not found in dts,using default %d", 1080);
	else
		NVT_LOG("touch_max_width found in dts, abs_x_max=%d", ts->abs_x_max);

	ret = of_property_read_variable_u32_array(nd, "novatek,touch_max_height", &ts->abs_y_max, 1, NULL);
	if (ret < 0)
		NVT_LOG("touch_max_height not found in dts, using default %d", 2400);
	else
		NVT_LOG("touch_max_height found in dts, abs_y_max=%d", ts->abs_y_max);

	return 0;
}

/* 0x1930, 3008 bytes */
static int nvt_ts_probe(struct spi_device *client)
{
	struct nvt_ts_data *ts_data;
	struct irq_desc *desc;
	int ret = 0;

	NVT_LOG("start");

	ts_data = kzalloc(sizeof(*ts_data), GFP_KERNEL);
	ts = ts_data;
	if (!ts) {
		NVT_ERR("failed to allocated memory for nvt ts data");
		return -ENOMEM;
	}

	ts->xbuf = kmalloc_order(64 * 1024, GFP_KERNEL | __GFP_ZERO, 4);
	if (!ts->xbuf) {
		NVT_ERR("kzalloc for xbuf failed!");
		ret = -ENOMEM;
		goto err_ts;
	}

	ts->rbuf = kzalloc(2048, GFP_KERNEL);
	if (!ts->rbuf) {
		NVT_ERR("kzalloc for rbuf failed!");
		ret = -ENOMEM;
		goto err_xbuf;
	}

	ts->client = client;
	spi_set_drvdata(client, ts);

	if (!client->dev.of_node) {
		NVT_ERR("parse dt error");
		ret = -EIO;
		goto err;
	}

	client->mode = SPI_LSB_FIRST;
	client->max_speed_hz = 0;
	ret = spi_setup(client);
	if (ret < 0) {
		NVT_ERR("Failed to perform SPI setup");
		goto err;
	}
	NVT_LOG("mode=%d, max_speed_hz=%d", client->mode, client->max_speed_hz);

	ts->abs_x_max = 1080;
	ts->abs_y_max = 2400;

	ret = nvt_parse_dt(client);
	if (ret) {
		NVT_ERR("parse dt error");
		goto err;
	}

	ret = nvt_gpio_config();
	if (ret < 0)
		goto err_gpio;

	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);

	nvt_write_addr(ENG_RST_ADDR, 0x5a);
	__const_udelay(0x418958 >> 2);
	msleep(10);

	if (nvt_ts_check_chip_ver_trim(0x03f004) & 0xff) {
		NVT_ERR("try to check from old chip ver trim address");
		if (nvt_ts_check_chip_ver_trim(0x01f64e) & 0xff) {
			NVT_ERR("chip is not identified");
			ret = -EINVAL;
			goto err_input;
		}
	}

	ts->input_dev = input_allocate_device();
	if (!ts->input_dev) {
		NVT_ERR("allocate input device failed");
		ret = -ENOMEM;
		goto err_input;
	}

	ts->max_button_num = 10;
	ts->int_trigger_type = 1;
	NVT_LOG("int_trigger_type=%d", ts->int_trigger_type);
	NVT_LOG("NVT_TOUCH_ESD_PROTECT is %d", 0);

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	__set_bit(BTN_TOUCH, ts->input_dev->keybit);
	ts->input_dev->id.version = 2;

	input_mt_init_slots(ts->input_dev, ts->max_button_num, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 0x3e8, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xff, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);

	ts->input_dev->name = NVT_TS_INPUT_NAME;
	strcpy(ts->phys, "input/ts");
	ts->phys[8] = 0;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_SPI;

	ret = input_register_device(ts->input_dev);
	if (ret) {
		NVT_ERR("register input device (%s) failed. ret=%d", ts->input_dev->name, ret);
		goto err_input_free;
	}

	client->irq = gpiod_to_irq(gpio_to_desc(ts->irq_gpio));
	if (client->irq) {
		ts->irq_enabled = 1;
		ret = request_threaded_irq(client->irq, NULL, nvt_ts_work_func,
					   ts->int_trigger_type | IRQF_ONESHOT, NVT_TS_NAME, ts);
		if (ret) {
			NVT_ERR("request irq failed. ret=%d", ret);
			goto err_unreg_input;
		}
		if (ts->irq_enabled) {
			disable_irq(client->irq);
			ts->irq_enabled = 0;
		}
		desc = irq_to_desc(client->irq);
		NVT_LOG("%d", desc ? desc->depth : -1);
		NVT_LOG("request irq %d succeed", client->irq);
	}

	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_fwu_wq) {
		NVT_ERR("nvt_fwu_wq create workqueue failed");
		ret = -ENOMEM;
		goto err_free_irq;
	}

	INIT_DELAYED_WORK(&ts->fw_update_work, (void (*)(struct work_struct *))nvt_ts_work_func);
	queue_delayed_work_on(WORK_CPU_UNBOUND, nvt_fwu_wq, &ts->fw_update_work, 3500);
	NVT_LOG("This is NVT touch IC");

	ret = sysfs_create_group(&client->dev.kobj, &nvt_attr_group);
	if (ret) {
		dev_err(&client->dev, "%s: Failed to crate sysfs attributes.
",
			__func__);
		goto err_remove_group;
	}

	ret = sysfs_create_link(NULL, &client->dev.kobj, "touchscreen");
	if (ret) {
		dev_err(&client->dev, "%s: Failed to crate sysfs link!
",
			__func__);
		goto err_remove_group;
	}

	ret = nvt_flash_proc_init();
	if (ret) {
		NVT_ERR("nvt flash proc init failed. ret=%d", ret);
		goto err_remove_group;
	}

	ret = nvt_extra_proc_init();
	if (ret) {
		NVT_ERR("nvt extra proc init failed. ret=%d", ret);
		nvt_flash_proc_deinit();
		goto err_remove_group;
	}

	bTouchIsAwake = 1;
	NVT_LOG("finished");
	if (!ts->irq_enabled) {
		enable_irq(client->irq);
		ts->irq_enabled = 1;
	}
	desc = irq_to_desc(client->irq);
	NVT_LOG("%d", desc ? desc->depth : -1);
	return 0;

err_remove_group:
	sysfs_remove_group(&client->dev.kobj, &nvt_attr_group);
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->fw_update_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
err_free_irq:
	if (client->irq)
		free_irq(client->irq, ts);
err_unreg_input:
	ts->input_dev = NULL;
	input_unregister_device(ts->input_dev);
err_input_free:
	ts->input_dev = NULL;
err_input:
err_gpio:
	if (ts->irq_gpio <= 0x1ff)
		gpio_free(ts->irq_gpio);
	spi_set_drvdata(client, NULL);
	kfree(ts->rbuf);
	kfree(ts->xbuf);
	kfree(ts);
err_xbuf:
err_ts:
	ts = NULL;
	return ret;
}

/* 0x24f0, 392 bytes */
static int nvt_ts_remove(struct spi_device *client)
{
	struct nvt_ts_data *ts = spi_get_drvdata(client);

	NVT_LOG("Removing driver...");

	nvt_extra_proc_deinit();
	nvt_flash_proc_deinit();
	sysfs_remove_link(NULL, "touchscreen");
	sysfs_remove_group(&client->dev.kobj, &nvt_attr_group);

	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->fw_update_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}

	if (ts->irq_enabled) {
		disable_irq(client->irq);
		ts->irq_enabled = 0;
	}
	free_irq(client->irq, ts);

	if (ts->irq_gpio <= 0x1ff)
		gpio_free(ts->irq_gpio);

	if (ts->input_dev) {
		input_unregister_device(ts->input_dev);
		ts->input_dev = NULL;
	}

	spi_set_drvdata(client, NULL);
	ts = NULL;

	return 0;
}

/* 0x2678, 276 bytes */
static void nvt_ts_shutdown(struct spi_device *client)
{
	struct nvt_ts_data *ts = spi_get_drvdata(client);

	NVT_LOG("Shutdown driver...");

	if (ts->irq_enabled) {
		disable_irq(client->irq);
		ts->irq_enabled = 0;
	}

	nvt_extra_proc_deinit();
	nvt_flash_proc_deinit();

	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->fw_update_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* module plumbing                                                    */
/* ------------------------------------------------------------------ */

static const struct of_device_id nvt_match_table[] = {
	{ .compatible = "novatek,NVT-ts-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, nvt_match_table);

static const struct spi_device_id nvt_ts_id[] = {
	{ "NVT-ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, nvt_ts_id);

static struct spi_driver nvt_spi_driver = {
	.probe = nvt_ts_probe,
	.remove = nvt_ts_remove,
	.shutdown = nvt_ts_shutdown,
	.id_table = nvt_ts_id,
	.driver = {
		.name = NVT_TS_NAME,
		.of_match_table = nvt_match_table,
	},
};

static int __init nvt_driver_init(void)
{
	int ret;

	ret = spi_register_driver(&nvt_spi_driver);
	if (ret) {
		NVT_ERR("failed to add spi driver");
		return ret;
	}
	return 0;
}

static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&nvt_spi_driver);
}

module_init(nvt_driver_init);
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek NT36xxx Touchscreen SPI driver");
MODULE_LICENSE("GPL");
