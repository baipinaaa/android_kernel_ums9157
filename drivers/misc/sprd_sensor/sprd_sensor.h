/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unisoc (Spreadtrum) camera sensor driver - shared definitions.
 *
 * Reconstructed from sprd_sensor.ko of the Sunelan Q968 (UMS9157/T157,
 * sprd/sharkle class SoC) vendor_boot ramdisk.  The original module was
 * built from four source files:
 *
 *   bsp/modules/common/camera/sensor/sprd_sensor_core.c
 *   bsp/modules/common/camera/sensor/sprd_sensor_drv.c
 *   bsp/modules/common/camera/sensor/csi2/sprd/receiver_r3p1v2/csi_driver.c
 *   bsp/modules/common/camera/sensor/csi2/sprd/receiver_r3p1v2/csi_api.c
 *
 * The split above is kept here:  sprd_sensor_core.c holds the misc device
 * and the file operations, sprd_sensor_drv.c the i2c sensor glue (power,
 * clock, gpio, i2c register access) and csi_driver.c / csi_api.c the CSI2
 * receiver (r3p1v2) half.
 */

#ifndef _SPRD_SENSOR_H_
#define _SPRD_SENSOR_H_

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define SENSOR_MAX_NUM			6
#define SENSOR_GPIO_NUM			11
#define SENSOR_REGULATOR_NUM		4
#define SENSOR_MCLK_TAB_NUM		5
#define SENSOR_I2C_BURST_MAX		0x40

/* sensor_id of the six i2c_driver instances, in registration order */
enum {
	SENSOR_MAIN = 0,
	SENSOR_SUB,
	SENSOR_MAIN2,
	SENSOR_SUB2,
	SENSOR_MAIN3,
	SENSOR_SUB3,
};

/* index into sensor_dev_data.gpio[] */
enum {
	SENSOR_GPIO_RESET = 0,
	SENSOR_GPIO_POWER_DOWN,
	SENSOR_GPIO_FLASH,
	SENSOR_GPIO_MIPI_SWITCH_EN,
	SENSOR_GPIO_MIPI_SWITCH_MODE,
	SENSOR_GPIO_SWITCH_MODE,
	SENSOR_GPIO_CAM_ID,
	SENSOR_GPIO_IOVDD,
	SENSOR_GPIO_AVDD,
	SENSOR_GPIO_DVDD,
	SENSOR_GPIO_MOT,
};

/* index into sensor_dev_data.regulator[] */
enum {
	SENSOR_REGULATOR_VDDIO = 0,
	SENSOR_REGULATOR_VDDCAMA,
	SENSOR_REGULATOR_VDDCAMD,
	SENSOR_REGULATOR_VDDCAMMOT,
};

/* index into sensor_dev_data.regulator[] used by the avdd/dvdd/iovdd ioctls */
#define SENSOR_REG_AVDD			1
#define SENSOR_REG_DVDD			2
#define SENSOR_REG_IOVDD		0
/* index used by the "set voltage" ioctl */
#define SENSOR_REG_MOT			3
#define SENSOR_GPIO_MOT_IDX		10

/* ioctl magic + numbers (type 'R') */
#define SENSOR_IOC_MAGIC		'R'

#define SENSOR_IO_SET_PD_LEVEL		_IOW('R', 0x00, u8)
#define SENSOR_IO_SET_AVDD		_IOW('R', 0x01, u32)
#define SENSOR_IO_SET_DVDD		_IOW('R', 0x02, u32)
#define SENSOR_IO_SET_IOVDD		_IOW('R', 0x03, u32)
#define SENSOR_IO_SET_MCLK		_IOW('R', 0x04, u32)
#define SENSOR_IO_RESET			_IOW('R', 0x05, u32)
#define SENSOR_IO_SET_SENSOR_ID		_IOW('R', 0x08, u32)
#define SENSOR_IO_SET_RST_LEVEL		_IOW('R', 0x09, u32)
#define SENSOR_IO_SET_I2C_ADDR		_IOW('R', 0x0a, u16)
#define SENSOR_IO_READ_I2C_REG		_IOW('R', 0x0b, u64)
#define SENSOR_IO_READ_I2C_REG64	_IOWR('R', 0x0b, u64)
#define SENSOR_IO_WRITE_I2C_REG		_IOW('R', 0x0c, u64)
#define SENSOR_IO_WRITE_I2C_REGS	_IOW('R', 0x0e, struct sensor_reg_tab)
#define SENSOR_IO_SET_VOLTAGE		_IOW('R', 0x0f, u32)
#define SENSOR_IO_SET_I2C_CLK		_IOW('R', 0x10, u32)
#define SENSOR_IO_IF_CFG		_IOW('R', 0x12, struct sensor_if_cfg)
#define SENSOR_IO_GRC_WRITE_I2C		_IOW('R', 0x13, struct sensor_grc_i2c)
#define SENSOR_IO_GRC_READ_I2C		_IOWR('R', 0x14, struct sensor_grc_i2c)
#define SENSOR_IO_MIPI_IF		_IOWR('R', 0x15, struct sensor_if_cfg)
#define SENSOR_IO_SET_MIPI_LEVEL	_IOW('R', 0x16, u32)
#define SENSOR_IO_WRITE_MUTI_I2C	_IOWR('R', 0x17, struct sensor_muti_i2c_tab)
#define SENSOR_IO_IF_CFG2		_IOW('R', 0x18, struct sensor_if_cfg)
#define SENSOR_IO_IF_DUMP		_IOW('R', 0x19, u32)
#define SENSOR_IO_SET_PRIVATE_KEY	_IOW('R', 0x1a, u32)
#define SENSOR_IO_SET_I2C_BURST		_IOW('R', 0x1b, u32)

/* private key handed over by the camera HAL before any other ioctl works */
#define SENSOR_PRIVATE_KEY		0x789a
/* magic slave address which switches the driver into CSI test pattern mode */
#define SENSOR_PATTERN_ADDR		0xee

/* burst modes stored in sensor_dev_data.i2c_burst_mode */
enum {
	SENSOR_I2C_BURST_NONE = 0,
	SENSOR_I2C_BURST_SAMSUNG,
	SENSOR_I2C_BURST_REG16_VAL8,
	SENSOR_I2C_BURST_REG16_VAL16,
};

/* bits of struct sensor_i2c_reg.setting */
#define SENSOR_I2C_SETTING_DATA16	BIT(0)
#define SENSOR_I2C_SETTING_ADDR16	BIT(1)
/* pseudo register address which makes write_reg delay instead of writing */
#define SENSOR_I2C_REG_DELAY		0xffff

/* module wide data, one instance, reached through sensor_dev.this_device */
struct sensor_glb {
	atomic_t open_count;		/* +0x00 */
	struct mutex sensor_id_lock;	/* +0x08 */
	u32 reserved;			/* +0x24 */
	struct wakeup_source *ws;	/* +0x28 */
};

/* per sensor (one i2c_driver instance) data */
struct sensor_dev_data {
	struct mutex sync_lock;			/* +0x00 */
	struct mutex set_voltage_lock;		/* +0x20 */
	u32 reserved0;				/* +0x40 */
	struct clk *sensor_clk;			/* +0x48 */
	struct clk *clk_parent;			/* +0x50 */
	u64 clk_rate;				/* +0x58 */
	struct clk *sensor_clk2;		/* +0x60 */
	struct clk *sensor_eb;			/* +0x68 */
	struct regulator *regulator[SENSOR_REGULATOR_NUM];	/* +0x70 */
	struct i2c_client *client;		/* +0x90 */
	struct i2c_client *client2;		/* +0x98 */
	int gpio[SENSOR_GPIO_NUM];		/* +0xa0 */
	u32 regulator_count[SENSOR_REGULATOR_NUM];	/* +0xcc */
	u32 sensor_id;				/* +0xdc */
	u32 dcam_id;				/* +0xe0 */
	u32 cur_mclk;				/* +0xe4 */
	u32 mclk_ref_cnt;			/* +0xe8 */
	struct device_node *of_node;		/* +0xf0 */
	u32 i2c_clk;				/* +0xf8 */
	u32 i2c_burst_mode;			/* +0xfc */
};

/* per opened file data */
struct sensor_file_ctx {
	struct miscdevice *misc;	/* +0x00 */
	u32 reserved0;			/* +0x08 */
	u32 csi_param;			/* +0x0c bps_per_lane of the last open */
	u32 mipi_state;			/* +0x10 */
	u32 reserved2;			/* +0x14 */
	struct sensor_glb *p_data;	/* +0x18 */
	u32 sensor_id;			/* +0x20 */
	u32 mipi_on;			/* +0x24 */
	u32 private_key_ok;		/* +0x28 */
};

/* one i2c register access (setup.calculation result of the HAL) */
/* grow-only scratch buffer used by the ioctl paths */
struct sensor_mem {
	void *buf;		/* +0x00 */
	u32 size;		/* +0x08 */
};

struct sensor_i2c_reg {
	u16 addr;			/* +0x00 */
	u16 val;			/* +0x02 */
	u32 setting;			/* +0x04 */
};

/* register table written by SENSOR_IO_WRITE_I2C_REGS (size 0x18) */
struct sensor_reg_tab {
	unsigned long reg_tab;		/* +0x00 user pointer to u16 pairs */
	u32 reg_count;			/* +0x08 */
	u32 setting;			/* +0x0c */
	u32 burst_mode;			/* +0x10 */
};

/* i2c read/write helpers (SENSOR_IO_WRITE_I2C_REGS neighbours) */
struct sensor_i2c_data {
	u32 buf;			/* +0x00 user buffer address */
	u32 reserved0;			/* +0x04 */
	u16 len;			/* +0x08 number of address bytes / payload */
	u16 addr;			/* +0x0a i2c slave address */
	u16 size;			/* +0x0c number of bytes to read */
	u16 reserved1;			/* +0x0e */
};

/* mipi if open/close payload (SENSOR_IO_IF_CFG, size 0x18) */
struct sensor_if_cfg {
	u32 type;			/* +0x00 0 = open, anything else = close */
	u32 open;			/* +0x04 must be 1 or the request is ignored */
	u32 phy_id;			/* +0x08 */
	u32 lane_num;			/* +0x0c */
	u32 bps_per_lane;		/* +0x10 */
	u32 use_phy_id;			/* +0x14 */
	u64 priv;			/* +0x18 SENSOR_IF_PATTERN enables test mode */
};

/* priv magic which puts the receiver in test pattern mode */
#define SENSOR_IF_PATTERN		0xeeee

/* 0x10 byte grc (general register config) payload */
struct sensor_grc_i2c {
	u32 sensor_id;			/* +0x00 */
	u32 slave_addr;			/* +0x04 */
	u32 reg_addr;			/* +0x08 */
	u32 reg_val;			/* +0x0c */
};

/* three sensor, three block i2c write command (size 0x50) */
struct sensor_muti_i2c_tab {
	u32 sensor_id_tab;		/* +0x00 u16[3] sensor ids */
	u32 reserved0;			/* +0x04 */
	u32 slave_addr_tab;		/* +0x08 u16[3] slave addresses */
	u32 reserved1;			/* +0x0c */
	u32 addr_bits_tab;		/* +0x10 u16[3] address width settings */
	u32 reserved2;			/* +0x14 */
	u32 data_bits_tab;		/* +0x18 u16[3] data width settings */
	u32 reserved3;			/* +0x1c */
	u32 m_tab;			/* +0x20 master block register pairs */
	u16 m_count;			/* +0x24 */
	u16 reserved4;			/* +0x26 */
	u32 s_tab;			/* +0x28 slave block register pairs */
	u16 s_count;			/* +0x2c */
	u16 reserved5;			/* +0x2e */
	u32 s2_tab;			/* +0x30 slave2 block register pairs */
	u16 s2_count;			/* +0x34 */
	u16 reserved6;			/* +0x36 */
	ktime_t m_time;			/* +0x38 */
	ktime_t s_time;			/* +0x40 */
	ktime_t s2_time;		/* +0x48 */
};

/* mclk source table entry */
struct sensor_mclk_tab {
	int div;			/* +0x00 divider of the source clock */
	const char *name;		/* +0x08 clock name in the device tree */
};

extern struct sensor_dev_data *s_sensor_dev_data[SENSOR_MAX_NUM];
extern const char *const sprd_sensor_supply_names[SENSOR_REGULATOR_NUM];
extern const char *const sprd_sensor_gpio_names[SENSOR_GPIO_NUM];
extern const struct sensor_mclk_tab c_sensor_mclk_tab[SENSOR_MCLK_TAB_NUM];
extern const u32 c_sensor_voltage_tab[13];
extern const u32 c_sensor_dcam_id_tab[6];

/* csi_driver.c */
extern u8 csi_pattern;
extern u8 csi_reg_dump_flag;

/* sprd_sensor_core.c */
struct sensor_dev_data *sprd_sensor_get_dev_context(u32 sensor_id);
int sprd_sensor_find_dcam_id(u32 sensor_id);
long sprd_sensor_file_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg);
int compat_sensor_k_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg);
void sprd_sensor_sync_lock(u32 sensor_id);
void sprd_sensor_sync_unlock(u32 sensor_id);

/* sprd_sensor_drv.c */
int sprd_sensor_register_driver(void);
void sprd_sensor_unregister_driver(void);
int sprd_sensor_write_i2c(void *data, u32 sensor_id);
int sprd_sensor_read_i2c(void *data, u32 sensor_id);
int sprd_sensor_write_reg(u32 sensor_id, struct sensor_i2c_reg *reg);
int sprd_sensor_read_reg(u32 sensor_id, struct sensor_i2c_reg *reg);
int sprd_sensor_write_muti_i2c(void *data);
int sprd_sensor_set_mclk(int mclk, u32 sensor_id);
int sprd_sensor_set_voltage(u32 sensor_id, int vol, u32 id);
int sprd_sensor_set_voltage_by_gpio(u32 sensor_id, int val, u32 gpio_idx);
int sprd_sensor_set_pd_level(u32 sensor_id, int level);
int sprd_sensor_reset(u32 sensor_id, int level, u32 width);
int sprd_sensor_set_rst_level(u32 sensor_id, int level);
int sprd_sensor_set_mipi_level(u32 sensor_id, int level);
int sprd_sensor_set_i2c_addr(u32 sensor_id, u16 addr);
int sprd_sensor_set_i2c_clk(u32 sensor_id, u32 clk);
int sprd_sensor_set_i2c_burst(u32 sensor_id, u32 mode);
int sprd_sensor_io_set_avdd(struct sensor_file_ctx *ctx, unsigned long arg);
int sprd_sensor_io_set_dvdd(struct sensor_file_ctx *ctx, unsigned long arg);
int sprd_sensor_io_set_iovdd(struct sensor_file_ctx *ctx, unsigned long arg);
int sprd_sensor_io_write_i2c_regs(struct sensor_file_ctx *ctx, void __user *arg);
int sprd_sensor_io_grc_write_i2c(struct sensor_file_ctx *ctx, void __user *arg);
int sprd_sensor_io_grc_read_i2c(struct sensor_file_ctx *ctx, void __user *arg);

/* csi_api.c / csi_driver.c */
int csi_api_reg_trace(void);
int csi_api_get_dcam_id(u32 sensor_id);
int csi_api_mipi_phy_cfg_init(struct device_node *node, u32 sensor_id);
void csi_api_mipi_phy_cfg(void);
int csi_api_dt_node_init(struct device *dev, struct device_node *node,
			 u32 sensor_id, int phy_id);
int csi_api_open(u32 phy_freq, u32 on, u32 type, u32 sensor_id,
		 u32 pattern, u32 lane_num);
int csi_api_close(u32 dev, u32 sensor_id);
int csi_api_switch(u32 sensor_id);
int sprd_sensor_io_if_cfg(struct sensor_file_ctx *ctx, void __user *arg);

#endif /* _SPRD_SENSOR_H_ */
