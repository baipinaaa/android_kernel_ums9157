/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Novatek NT36xxx series capacitive touchscreen SPI driver - shared definitions
 *
 * Rebuilt from the vendor module nvt_nt36xxx_spi_ts.ko (Sunelan Q968 /
 * Unisoc UMS9157, kernel 5.4.210).  Text size 0x5d54, 58 functions.
 *
 * Layout of the vendor private data matches the binary exactly: the struct is
 * 0x148 bytes and every field offset below was verified against the
 * disassembly of nvt_ts_probe().
 */

#ifndef _NVT_36XXX_H_
#define _NVT_36XXX_H_

/* The vendor binary prefixes every message with "[%s] %s %d: " where the
 * three arguments are the driver tag, __func__ and __LINE__.
 */
#define pr_fmt(fmt)	"[%s] %s %d: " fmt

#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/string.h>

#define NVT_LOG_TAG	"NVT-ts"
#define NVT_TS_NAME	"NVT-ts"
#define NVT_TS_INPUT_NAME	"NVTCapacitiveTouchScreen"
#define NVT_FW_FILE	"novatek_ts_fw.bin"

/* printk helpers - the vendor binary uses a single format string
 * "[%s] %s %d: <msg>" with (NVT-ts, __func__, __LINE__) as the first three
 * arguments; the leading digit of the format string is the printk level.
 */
#define NVT_LOG(fmt, args...)	pr_info("[%s] %s %d: " fmt "\n", NVT_LOG_TAG, __func__, __LINE__, ##args)
#define NVT_ERR(fmt, args...)	pr_err("[%s] %s %d: " fmt "\n", NVT_LOG_TAG, __func__, __LINE__, ##args)

#define NVT_FLASH_DEV_SIZE	(64 * 1024 - 1023)

/* SPI command bytes */
#define NVT_CMD_READ		0x03
#define NVT_CMD_WRITE		0x00

/* FW state values used by nvt_check_fw_status() */
#define NVT_FW_STATE_STOP	0xa0

/* nvt_change_mode() modes */
#define NVT_MODE_NORMAL		0x00
#define NVT_MODE_SLEEP		0x01

/* reset states of nvt_check_fw_reset_state() */
#define RESET_STATE_INIT	0x00
#define RESET_STATE_REK		0x01
#define RESET_STATE_REK_FINISH	0x05
#define RESET_STATE_NORMAL_RUN	0xa0

struct nvt_ts_mem_map {
	u32 rchive_fw_i2c_addr;		/* +0x00 */
	u32 rchive_fw_i2c_len;		/* +0x04 */
	u32 ilm_addr;			/* +0x08 */
	u32 dlm_addr;			/* +0x0c */
	u32 dlm_len;			/* +0x10 */
	u32 bld_ilm_addr;		/* +0x14 */
	u32 bld_ilm_len;		/* +0x18 */
	u32 fw_status_addr;		/* +0x1c */
	u32 fw_ilm_checksum_addr;	/* +0x20 */
	u32 fw_dlm_checksum_addr;	/* +0x24 */
	u32 fw_ver_addr;		/* +0x28 */
	u32 fw_ver_bar_addr;		/* +0x2c */
	u32 boot_ready_addr;		/* +0x30 */
	u32 boot_ready_clr_addr;	/* +0x34 */
	u32 boot_ready_val_addr;	/* +0x38 */
	u32 spi_rd_fast_addr;		/* +0x3c */
	u32 swrst_n8_addr;		/* +0x40 */
	u32 hw_crc_addr;		/* +0x44 */
	u32 hw_crc_err_ctrl;		/* +0x48 */
	u32 dlm_crc_len;		/* +0x4c */
	u32 dlm_crc_buf_addr;		/* +0x50 */
	u32 sram_buf_addr;		/* +0x54 */
	u32 sram_buf_len;		/* +0x58 */
	u32 fdbg_addr;			/* +0x5c */
	u32 fdbg_len;			/* +0x60 */
	u32 bld_crc_bank;		/* +0x64 */
	u32 fw_crc_bank;		/* +0x68 */
	u32 rsvd[2];			/* +0x6c */
};

/* The vendor struct is 0x148 bytes; keep the exact field offsets. */
struct nvt_ts_data {
	struct spi_device *client;		/* 0x000 */
	struct input_dev *input_dev;		/* 0x008 */
	struct delayed_work fw_update_work;	/* 0x010 */
	u8 pad0[0x1a];				/* 0x080 */
	char phys[10];				/* 0x09a */
	u8 pad1[0x16];				/* 0x0a4 */
	u8 fw_ver;				/* 0x0ba */
	u8 x_num;				/* 0x0bb */
	u8 y_num;				/* 0x0bc */
	u8 pad2[3];				/* 0x0bd */
	u32 abs_x_max;				/* 0x0c0 */
	u32 abs_y_max;				/* 0x0c4 */
	u8 max_button_num;			/* 0x0c8 */
	u8 pad3[3];				/* 0x0c9 */
	u32 int_trigger_type;			/* 0x0cc */
	int irq_gpio;				/* 0x0d0 */
	u32 irq_flags;				/* 0x0d4 */
	u8 pad4[8];				/* 0x0d8 */
	struct mutex lock;			/* 0x0e0 */
	struct nvt_ts_mem_map *mmap;		/* 0x100 */
	u8 hw_crc;				/* 0x108 */
	u8 hw_crc_flag;				/* 0x109 */
	u16 pid;				/* 0x10a */
	u8 pad6[4];				/* 0x10c */
	u8 *rbuf;				/* 0x110 */
	u8 *xbuf;				/* 0x118 */
	struct mutex xbuf_lock;			/* 0x120 */
	u8 irq_enabled;				/* 0x140 */
	u8 suspended;				/* 0x141 */
};

struct nvt_ts_trim_id_table {
	u8 id[6];				/* 0x00 */
	u8 mask[6];				/* 0x06 */
	u8 pad[4];				/* 0x0c */
	struct nvt_ts_mem_map *mmap_id;		/* 0x10 */
	u16 *crc;				/* 0x18 */
	u8 pad2[6];				/* 0x1a */
};

/* one entry of the firmware binary map, 0x1c bytes */
struct nvt_bin_map {
	char name[12];		/* +0x00 : "ILM" / "DLM" / "Info-N" / "Overlay-N" */
	u32 addr;		/* +0x0c : offset inside the bin file */
	u32 sram;		/* +0x10 : target sram address */
	u32 size;		/* +0x14 : section length */
	u32 checksum;		/* +0x18 : expected checksum */
};

/* bin file header, parsed by nvt_bin_header_parser() */
struct nvt_bin_header {
	u8 bin_ver[0x10];
	u8 bin_chksum[0x40];
	u8 bin_ilm_sec_num;
	u8 bin_dlm_sec_num;
	u8 bin_rsvd[0x1e];
};

/* one ILM or DLM section inside the bin file */
struct nvt_ilm_dlm_info {
	u32 start_addr;
	u32 len;
};

struct nvt_info_sec {
	u32 info_type;
	u32 info_len;
	u32 info_tab[16];
};

struct nvt_ilm_dlm_sec {
	u32 ilm_dlm_start_num;
	u32 ilm_dlm_num;
	u8 ilm_dlm_chksum_flag;
	struct nvt_ilm_dlm_info ilm_dlm_info[16];
} __packed;

/* ---- external data (defined once per module) ---- */
extern struct nvt_ts_data *ts;

extern u8 tp_spi_safaMode;
extern u8 tp_spi_ignSafeModeIrq;
extern u8 bTouchIsAwake;
extern u8 recovery_cnt;
extern u32 SWRST_N8_ADDR;
extern u32 SPI_RD_FAST_ADDR;
extern u32 ENG_RST_ADDR;
extern char lcd_name[100];
extern struct workqueue_struct *nvt_fwu_wq;

extern u8 xdata_tmp[2048];
extern u8 xdata[8192];

extern struct proc_dir_entry *NVT_proc_entry;
extern struct proc_dir_entry *NVT_proc_fw_version_entry;
extern struct proc_dir_entry *NVT_proc_baseline_entry;
extern struct proc_dir_entry *NVT_proc_raw_entry;
extern struct proc_dir_entry *NVT_proc_diff_entry;

extern struct nvt_ts_mem_map NT36772_memory_map;
extern struct nvt_ts_mem_map NT36525_memory_map;
extern struct nvt_ts_mem_map NT36676F_memory_map;
extern u16 NT36772_hw_info;
extern u16 NT36525_hw_info;
extern u16 NT36676F_hw_info;
extern u16 NT36672A_hw_info;
extern u16 NT36675_hw_info;


/* ---- spi primitives ---- */
int CTP_SPI_READ(struct spi_device *client, u8 *buf, u16 len);
int CTP_SPI_WRITE(struct spi_device *client, u8 *buf, u16 len);
int nvt_set_page(u32 addr);
int nvt_write_addr(u32 addr, u8 data);

/* ---- chip control ---- */
int nvt_bld_crc_enable(void);
int nvt_fw_crc_enable(void);
int nvt_boot_ready(void);
int nvt_eng_reset(void);
int nvt_sw_reset(void);
int nvt_sw_reset_idle(void);
int nvt_bootloader_reset(void);
int nvt_clear_fw_status(void);
int nvt_check_fw_status(void);
int nvt_check_fw_reset_state(int reset_state);
int nvt_read_pid(void);
int nvt_get_fw_info(void);
int nvt_change_mode(u8 mode);
int nvt_get_fw_pipe(void);
int nvt_read_mdata(u32 kaddr, u8 *data, u16 size);
int nvt_get_mdata(u8 *data, u32 size);

/* ---- update ---- */
int nvt_update_firmware(const char *fw_name);
int nvt_write_firmware(const u8 *fwbuf, u32 fwlen);
int nvt_set_bld_crc_bank(u32 crc, u32 len);
int Boot_Update_Firmware(struct device *dev);

/* ---- proc ---- */
int nvt_flash_proc_init(void);
void nvt_flash_proc_deinit(void);
int nvt_extra_proc_init(void);
void nvt_extra_proc_deinit(void);

/* ---- main driver ---- */
int nvt_ts_work_func_schedule(struct nvt_ts_data *ts);
int nvt_ts_suspend(struct device *dev);
int nvt_ts_resume(struct device *dev);

#endif /* _NVT_36XXX_H_ */
