/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synaptics TouchComm (TCM) driver for the TD4320 touch controller.
 *
 * Reconstructed from the vendor module synaptics_td4320_spi_ts.ko
 * (.text 98190 bytes, 170 functions), built by the vendor from
 * bsp/modules/kernel5.4/input/touchscreen/synaptics_td4320_spi/.
 */

#ifndef _SYNAPTICS_TCM_CORE_H_
#define _SYNAPTICS_TCM_CORE_H_

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

/* the vendor tree carries this in its own helper header */
#ifndef MIN
#define MIN(a, b)			((a) < (b) ? (a) : (b))
#endif

#define TCM_TOUCH_REPORT_CONFIG_SIZE	1024

/* the input device name the Android framework matches on */
#define SYNA_TCM_INPUT_NAME		"synaptics_tcm_touch"

/* TCM commands, as sent through the message interface */
enum tcm_command {
	CMD_NONE = 0x00,
	CMD_CONTINUE_WRITE = 0x01,
	CMD_IDENTIFY = 0x02,
	CMD_RESET = 0x04,
	CMD_ENABLE_REPORT = 0x05,
	CMD_DISABLE_REPORT = 0x06,
	CMD_SET_DYNAMIC_CONFIG = 0x24,
	CMD_GET_DYNAMIC_CONFIG = 0x23,
	CMD_SET_DATA_LOCATION = 0x09,
	CMD_GET_BOOT_INFO = 0x0a,
	CMD_ERASE_FLASH = 0x11,
	CMD_WRITE_FLASH = 0x12,
	CMD_READ_FLASH = 0x13,
	CMD_RUN_APPLICATION_FIRMWARE = 0x14,
	CMD_REBOOT_TO_ROM_BOOTLOADER = 0x16,
	CMD_SPI_MASTER_WRITE_THEN_READ_EXTENDED = 0x43,
	CMD_RUN_BOOTLOADER_FIRMWARE = 0x1f,
	CMD_SPI_ONLY = 0x1b,
	CMD_REZERO = 0x27,
	CMD_PRODUCTION_TEST = 0x2a,
	CMD_DOWNLOAD_CONFIG = 0x30,
	CMD_ROMBOOT_DOWNLOAD = 0x45,
	CMD_ENTER_DEEP_SLEEP = 0x2c,
	CMD_EXIT_DEEP_SLEEP = 0x2d,
	CMD_GET_DATA_LOCATION = 0x2f,
	CMD_ENTER_PRODUCTION_TEST_MODE = 0x31,
	CMD_GET_FEATURES = 0x32,
	CMD_GET_APPLICATION_INFO = 0x20,
	CMD_GET_ROMBOOT_INFO = 0x40,
	CMD_ROMBOOT_RUN_BOOTLOADER_FIRMWARE = 0x42,
	CMD_GET_STATIC_CONFIG = 0x21,
	CMD_SET_STATIC_CONFIG = 0x22,
};

enum dynamic_config_id {
	DC_UNKNOWN = 0x00,
	DC_NO_DOZE = 0x01,
	DC_DISABLE_NOISE_MITIGATION = 0x02,
	DC_INHIBIT_FREQUENCY_SHIFT = 0x03,
	DC_REQUESTED_FREQUENCY = 0x04,
	DC_DISABLE_HSYNC = 0x05,
	DC_REFLASH_ADDRESS = 0x06,
	DC_FAST_SCAN = 0x07,
	DC_SELECT_OPEN = 0x08,
	DC_READ_RAW_DATA = 0x09,
	DC_EEPROM_ERASE = 0x0a,
	DC_BOOTLOADER_SIZE = 0x0b,
	DC_APP_FIRMWARE_SIZE = 0x0c,
};

/*
 * Flash areas as the firmware numbers them in the data location replies and
 * in the read/write/erase flash commands.  Area 1 is a fixed 128 byte block
 * holding the customer serialisation string, 2 is not used by anything and
 * 8 is the boot config area the bootloader itself parses.  The three custom
 * areas (6, 7, 8) are located dynamically through CMD_GET_DATA_LOCATION.
 */
enum flash_area {
	FLASH_AREA_NONE = 0,
	FLASH_AREA_CUSTOMER_SERIALIZATION = 1,
	FLASH_AREA_UNUSED = 2,
	FLASH_AREA_APP_CONFIG = 3,
	FLASH_AREA_DISP_CONFIG = 4,
	FLASH_AREA_CUSTOM_OTP = 5,
	FLASH_AREA_CUSTOM_LCM = 6,
	FLASH_AREA_CUSTOM_OEM = 7,
	FLASH_AREA_BOOT_CONFIG = 8,
	FLASH_AREA_COUNT = 9,
};

enum tcm_report_type {
	REPORT_IDENTIFY = 0x10,
	REPORT_TOUCH = 0x11,
	REPORT_DELTA = 0x12,
	REPORT_RAW = 0x13,
	REPORT_STATUS = 0x1b,
	REPORT_DEVICE = 0x30,
};

enum tcm_mode {
	MODE_LOAD_BOOTLOADER = 0,
	MODE_LOAD_APPLICATION = 1,
	MODE_LOAD_PRODUCTION_TEST = 2,
};

enum tcm_response {
	RESP_NONE = 0x00,
	RESP_OK = 0x01,
	RESP_IDENTIFY_OK = 0x11,
};

#define MESSAGE_HEADER_SIZE		3
#define MESSAGE_MARKER			0xa5
#define MESSAGE_PADDING			0x5a

/* the vendor logs every message with the source line of its own file */
#define LOGD(...)	pr_debug(__VA_ARGS__)
#define LOGI(...)	pr_info(__VA_ARGS__)
#define LOGW(...)	pr_warn(__VA_ARGS__)
#define LOGE(...)	pr_err(__VA_ARGS__)

/* board data handed over by the SPI glue (devm_kzalloc, 0x60 bytes) */
struct syna_tcm_board_data {
	bool x_flip;			/* +0x00 */
	bool y_flip;			/* +0x01 */
	bool swap_axes;			/* +0x02 */
	u8 pad0[0x01];			/* +0x03 */
	int irq_gpio;			/* +0x04 */
	u32 irq_on_state;		/* +0x08 */
	int power_gpio;			/* +0x0c */
	u32 power_on_state;		/* +0x10 */
	int reset_gpio;			/* +0x14 */
	u32 reset_on_state;		/* +0x18 */
	int tpio_reset_gpio;		/* +0x1c */
	u32 spi_mode;			/* +0x20 */
	u32 power_delay_ms;		/* +0x24 */
	u32 reset_delay_ms;		/* +0x28 */
	u32 reset_active_ms;		/* +0x2c */
	u32 byte_delay_us;		/* +0x30 */
	u32 block_delay_us;		/* +0x34 */
	u8 pad1[0x04];			/* +0x38 */
	u32 ubl_max_freq;		/* +0x3c */
	u32 ubl_byte_delay_us;		/* +0x40 */
	u8 pad2[0x04];			/* +0x44 */
	int irq_gpio_flags;		/* +0x48 */
	u8 pad3[0x04];			/* +0x4c */
	const char *pwr_reg_name;	/* +0x50 */
	const char *bus_reg_name;	/* +0x58 */
};

/* buffer helpers kept by hcd, one instance per direction */
struct romboot_info {
	u8 version;
	u8 status;
	u8 version_0;
	u8 version_1;
	u8 write_block_size_words;
	u8 max_write_payload_size;
	u8 last_reset_reason;
	u8 pad[3];
};

struct syna_tcm_buf {
	bool init;			/* +0x00 */
	u8 pad0[0x07];			/* +0x01 */
	u8 *buf;			/* +0x08 */
	u32 buf_size;			/* +0x10 */
	u32 data_length;		/* +0x14 */
	struct mutex buf_mutex;		/* +0x18 */
};

struct syna_tcm_hcd;
struct kobject;

/* the transport callbacks, filled in by the SPI glue */
struct syna_tcm_bus_io {
	u8 type;			/* +0x00 vendor writes 0x1c, meaning unknown */
	u8 pad0[0x07];			/* +0x01 */
	int (*rmi_read)(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			unsigned char *data, unsigned int size);	/* +0x08 */
	int (*rmi_write)(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			 unsigned char *data, unsigned int size);	/* +0x10 */
	int (*read)(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		    unsigned int size);					/* +0x18 */
	int (*write)(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		     unsigned int size);				/* +0x20 */
};

struct syna_tcm_hw_interface {
	struct syna_tcm_board_data *bdata;	/* +0x00 */
	const struct syna_tcm_bus_io *io;	/* +0x08 */
};

/*
 * Every functional module (diag, device, testing, ...) hands one of these to
 * syna_tcm_add_module(); the core reaches the module through the callbacks and
 * the modules live in their own translation units.
 */
struct syna_tcm_module_desc {
	u32 id;						/* +0x00 */
	u8 pad0[0x04];
	int (*init)(struct syna_tcm_hcd *tcm_hcd);	/* +0x08 */
	int (*remove)(struct syna_tcm_hcd *tcm_hcd);	/* +0x10 */
	int (*handler)(struct syna_tcm_hcd *tcm_hcd);	/* +0x18 */
	int (*reinit)(struct syna_tcm_hcd *tcm_hcd);	/* +0x20 */
	int (*suspend)(struct syna_tcm_hcd *tcm_hcd);	/* +0x28 */
	int (*resume)(struct syna_tcm_hcd *tcm_hcd);	/* +0x30 */
};

/*
 * The main per-device object.  Every buffer sits in its own slot so that the
 * message layer can hand a response back to the caller without copying.
 *
 * The tail of the object holds the core service table.  The vendor assigns
 * them in probe() straight from the CFI jump table, so each slot has the
 * exact signature the compiler stamped on the function:
 *
 *   +0x4d8 int (*)(hcd)
 *   +0x4e0 int (*)(hcd, bool hw_reset, bool reinit)
 *   +0x4e8 int (*)(hcd, bool en)
 *   +0x4f0 int (*)(hcd, bool rescan)
 *   +0x4f8 int (*)(hcd, bool en, bool no_disable)
 *   +0x500 int (*)(hcd, enum tcm_mode mode)
 *   +0x508 int (*)(hcd, u8 *data, u32 size)
 *   +0x510 int (*)(hcd, u8 code, u8 *payload, u32 length, u8 **resp,
 *                  u32 *resp_length, u8 *response_code, u32 *delay,
 *                  u32 ms)
 *   +0x518 int (*)(hcd, enum dynamic_config_id id, u16 *value)
 *   +0x520 int (*)(hcd, enum dynamic_config_id id, u16 value)
 *   +0x528 int (*)(hcd, enum flash_area area, u32 *addr, u32 *len)
 *
 * And one slot is not a core service at all: +0x538 is installed by the
 * touch module in touch_init() and cleared in touch_remove(), so that the
 * message layer can tell it a touch report is ready.
 *
 *   +0x538 void (*)(void)
 */
struct syna_tcm_hcd {
	u32 context_pid;			/* +0x000 owner of the current command */
	u32 response_ready;			/* +0x004 */
	u32 hdl_pending;				/* +0x008 host download in flight */
	u8 pad0[0x04];				/* +0x00c */
	wait_queue_head_t hdl_wq;		/* +0x010 host download waiters */
	wait_queue_head_t reflash_wq;		/* +0x028 */
	int irq;				/* +0x040 */
	u8 in_suspend;				/* +0x044 */
	u8 polling_disabled;			/* +0x045 the client suspended us */
	u8 irq_registered;			/* +0x046 */
	u8 irq_enabled;				/* +0x047 */
	u8 helper_thread;			/* +0x048 */
	u8 irq_no_disable;			/* +0x049 */
	u8 shut_down;				/* +0x04a */
	u8 mode;				/* +0x04b 1=app 2=f35 3=romboot */
	u8 pad5[0x01];				/* +0x04c */
	u8 command;				/* +0x04d */
	u8 report_code_last;			/* +0x04e */
	u8 report_code;				/* +0x04f */
	u8 status_code;				/* +0x050 */
	u8 pad10[0x03];				/* +0x051 */
	u32 command_count;			/* +0x054 starts at 9 */
	u32 payload_length;			/* +0x058 */
	u32 pad12;				/* +0x05c */
	u32 max_response_size;			/* +0x060 0x100 */
	u32 read_message_count;			/* +0x064 */
	u32 status;				/* +0x068 */
	u32 pad13;				/* +0x06c */
	struct platform_device *pdev;		/* +0x070 */
	void *bus_reg;				/* +0x078 */
	void *pwr_reg;				/* +0x080 */
	/*
	 * kobject the functional modules hang their directories under.  The
	 * vendor never stores anything here, so kobject_create_and_add() gets a
	 * NULL parent and the directories end up directly below /sys.
	 */
	struct kobject *sysfs_root;		/* +0x088 */
	u8 pad14[0x08];				/* +0x090 */
	struct mutex extif_mutex;		/* +0x098 */
	struct mutex reset_mutex;		/* +0x0b8 */
	struct mutex irq_en_mutex;		/* +0x0d8 */
	struct mutex io_ctrl_mutex;		/* +0x0f8 */
	struct mutex rw_ctrl_mutex;		/* +0x118 */
	struct mutex command_mutex;		/* +0x138 */
	struct mutex identify_mutex;		/* +0x158 */
	struct delayed_work polling_work;	/* +0x178 */
	u8 pad15[0x40];				/* +0x1c0 rest of the delayed_work */
	struct workqueue_struct *polling_wq;	/* +0x200 */
	u8 pad15b[0x08];			/* +0x208 */
	struct syna_tcm_buf in;			/* +0x210 */
	struct syna_tcm_buf out;		/* +0x248 */
	struct syna_tcm_buf resp;		/* +0x280 */
	struct syna_tcm_buf temp;		/* +0x2b8 */
	struct syna_tcm_buf config;		/* +0x2f0 */
	u8 dispatch_report_code;		/* +0x328 */
	u8 pad16[0x07];				/* +0x329 */
	struct syna_tcm_buf report_buffer;	/* +0x330 */
	u8 app_info[0x30];			/* +0x368 */
	u8 boot_info[0x20];			/* +0x398 */
	u8 romboot_info[0x1a];			/* +0x3b8 */
	/*
	 * Raw identify payload.  +0x01 is the firmware mode, +0x12 the build
	 * id and +0x16 the read message count the sensor suggests.
	 */
	u8 identify_info[0x18];			/* +0x3d2 */
	u8 pad17a[0x06];			/* +0x3ea */
	u32 dispatch_state;			/* +0x3f0 */
	u8 pad17b[0x04];			/* +0x3f4 */
	struct work_struct helper_work;		/* +0x3f8 */
	u8 pad17c[0x10];			/* +0x418 */
	struct workqueue_struct *helper_wq;	/* +0x428 */
	u8 pad17d[0x98];			/* +0x430 */
	u8 feature_description[3];		/* +0x4c8 */
	u8 pad17e[0x05];			/* +0x4cb */
	struct syna_tcm_hw_interface *hw_if;	/* +0x4d0 */
	void *reset_fn;				/* +0x4d8 */
	void *reset_and_reinit_fn;		/* +0x4e0 */
	void *sleep_fn;				/* +0x4e8 */
	void *identify_fn;			/* +0x4f0 */
	void *enable_irq_fn;			/* +0x4f8 */
	void *switch_mode_fn;			/* +0x500 */
	void *read_message_fn;			/* +0x508 */
	void *write_message_fn;			/* +0x510 */
	void *get_dynamic_config_fn;		/* +0x518 */
	void *set_dynamic_config_fn;		/* +0x520 */
	void *get_data_location_fn;		/* +0x528 */
	void *reflash_data_fn;			/* +0x530 */
	void *touch_report_fn;			/* +0x538 */
	u8 pad19[0x880];			/* +0x540 */
};

int syna_tcm_bus_init(void);
void syna_tcm_bus_exit(void);
int syna_tcm_add_module(void *module, bool flag);

int syna_tcm_show_error(void);
int syna_tcm_read_message(struct syna_tcm_hcd *tcm_hcd, unsigned char *buf,
			  unsigned int len);
int syna_tcm_write_message(struct syna_tcm_hcd *tcm_hcd, unsigned char command,
			   unsigned char *payload, unsigned int length,
			   unsigned char **resp_buf, unsigned int *resp_length,
			   unsigned char *response_code,
			   unsigned int polling_delay_ms);

extern struct spi_driver syna_tcm_spi_driver;
int syna_tcm_spi_remove(struct spi_device *spi);
int syna_tcm_spi_alloc_mem(struct syna_tcm_hcd *tcm_hcd, unsigned int count,
			   unsigned int size);
int syna_tcm_spi_read(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		      unsigned int size);
int syna_tcm_spi_write(struct syna_tcm_hcd *tcm_hcd, unsigned char *data,
		       unsigned int size);
int syna_tcm_spi_rmi_read(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			  unsigned char *data, unsigned int size);
int syna_tcm_spi_rmi_write(struct syna_tcm_hcd *tcm_hcd, unsigned short addr,
			   unsigned char *data, unsigned int size);
int syna_tcm_spi_parse_dt(struct device *dev,
			  struct syna_tcm_board_data *bdata,
			  struct device_node *np);

int syna_tcm_read_message(struct syna_tcm_hcd *tcm_hcd, unsigned char *buf,
			  unsigned int length);

int syna_tcm_get_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
				enum dynamic_config_id id, u16 *value);
int syna_tcm_set_dynamic_config(struct syna_tcm_hcd *tcm_hcd,
				enum dynamic_config_id id, u16 value);
int syna_tcm_get_data_location(struct syna_tcm_hcd *tcm_hcd,
			       enum flash_area area, u32 *addr, u32 *length);

#endif /* _SYNAPTICS_TCM_CORE_H_ */
