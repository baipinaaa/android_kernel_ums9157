/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hynitron cst0xx/cst2xx/cst3xx/cst8xx capacitive touchscreen driver.
 *
 * Rebuilt from vendor binary drivers/input/touchscreen/hynitron_ts.ko
 * (module hynitron_ts, .text 0x8094 = 32916 bytes, 68 functions).
 *
 * 字段注释里的偏移是 vendor 结构体里的字节偏移，供逐函数对照使用；
 * 重建版按语义排布，字段一一对应（不强行 pad，避免人为错位）。
 *
 * 二进制里的日志串都以 "\001" + "3"（KERN_ERR）+ "[HYN]" 开头，
 * 因此源码统一用 pr_err("[HYN]" fmt, ...) 形态，不定义 pr_fmt。
 */

#ifndef _HYNITRON_TP_H_
#define _HYNITRON_TP_H_

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define HYN_TP_NAME			"hynitron_ts"
#define HYN_INPUT_NAME			"hyn_ts"

#define HYN_ERR(fmt, ...)		pr_err("[HYN]" fmt, ##__VA_ARGS__)

#define HYN_MAX_TOUCH_NUM		10
#define HYN_TOUCH_DATA_SIZE		64
#define HYN_CHIP_TYPE_NUM		10

/* 芯片类型索引（hyn_ts_data->chip_type_idx，二进制 +0x196） */
#define HYN_CHIP_CST1XX			1
#define HYN_CHIP_CST8XX			2

/* 固件组（.data hynitron_fw_grp，240 B = 10 项 × 0x18） */
struct hyn_fw_packet {
	u16 chip_ic_type;		/* +0x00 */
	u16 pad;
	const u8 *data;			/* +0x08 */
	u32 len;			/* +0x10 */
	u16 version;			/* +0x16 */
	u16 pad2;
};

/* 芯片类型组（.data hynitron_chip_type_grp） */
struct hyn_chip_type {
	u16 chip_type;			/* +0x00 */
	u8 config_chip_type;		/* +0x02 */
	u8 scan_id;			/* +0x03 */
	const char *name;
};

struct hyn_ts_platform_data {
	int irq_gpio;			/* +0x00 */
	u32 int_trigger_type;		/* +0x04 */
	int reset_gpio;			/* +0x08 */
	u32 x_max;			/* +0x10 */
	u32 y_max;			/* +0x14 */
	u32 max_touch_num;		/* +0x1c */
	u32 key_code[3];		/* +0x60 */
	u8 xy_exchange;			/* +0x80 */
	u8 x_overturn;			/* +0x81 */
	u8 y_overturn;			/* +0x82 */
	u8 key_num;			/* +0x83 */
	u32 bustype;
};

struct hyn_ts_data;

typedef void (*hyn_report_fn_t)(struct hyn_ts_data *ts);

struct hyn_ts_data {
	struct hyn_ts_platform_data *pdata;	/* +0x000 */
	struct i2c_client *client;		/* +0x008 */
	struct device *dev;			/* +0x010 */
	struct input_dev *input_dev;		/* +0x018 */

	hyn_report_fn_t report_fn;		/* +0x070 */
	struct work_struct work;		/* +0x078 */
	struct workqueue_struct *hyn_wq;	/* +0x0a0 */

	u8 irq_enabled;				/* +0x0a9 */
	int irq;				/* +0x0ac */
	u8 irq_num_flag;			/* +0x0b0 */
	u8 hyn_irq_flag;			/* +0x0b3 */
	u8 config_chip_type;			/* +0x0b6 */

	u16 chip_ic_type;			/* +0x178 */
	u32 chip_ic_checksum;			/* +0x180 */
	u32 chip_ic_checkcode;			/* +0x184 */
	u16 chip_ic_fw_version;			/* +0x188 */
	u16 chip_ic_project_id;			/* +0x18a */
	u16 chip_ic_module_id;			/* +0x18c */
	u8 scan_id;				/* +0x190 */
	u8 main_addr;				/* +0x191 */
	u16 chip_ic_type2;			/* +0x192 */
	u16 chip_ic_type3;			/* +0x194 */
	u8 chip_type_idx;			/* +0x196 */

	const u8 *fw_data;			/* +0x198 */
	u32 fw_chip_type;			/* +0x1a0 */
	u64 fw_len;				/* +0x1a8 */

	void *attr_group;			/* +0x1b8 */

	u8 touch_data[HYN_TOUCH_DATA_SIZE];
};

/* 全局（二进制 .bss） */
extern struct hyn_ts_data *hyn_ts_data;
extern struct hyn_ts_data *gExClient;
extern struct proc_dir_entry *g_proc_dir;
extern struct proc_dir_entry *g_update_file;
extern const struct file_operations proc_tool_debug_fops;
extern u8 g_cmdret;
extern u8 g_cmdpack[2048];
extern u32 g_cmdlen;
extern u8 g_exc_flag;
extern struct task_struct *gExchandle;
extern u8 CMDIndex;
extern wait_queue_head_t debug_waiter;
extern wait_queue_head_t run_cmd_waiter;
extern struct mutex g_procrw_mutex;
extern struct hyn_fw_packet hynitron_fw_grp[10];
extern struct hyn_chip_type hynitron_chip_type_grp[10];
extern struct attribute_group hyn_attribute_group;
extern struct attribute_group tp_attr_group;

/* i2c 原语 */
int hyn_i2c_read(struct i2c_client *client, u8 *buf, int len, int addr, int mode);
int hyn_i2c_write(struct i2c_client *client, u8 *buf, int len);
int hyn_i2c_write_byte(struct i2c_client *client, u8 b0, u8 b1);
int hyn_i2c_read_byte(struct i2c_client *client, u8 reg, u8 *out);
int hyn_i2c_write_bytes(u8 addr, const u8 *data, u16 len, u8 addr_mode);
int hyn_i2c_read_bytes(u8 addr, u8 *data, u16 len, u8 addr_mode);

/* cst3xx 时序化的 i2c 访问 */
int cst3xx_i2c_read(struct i2c_client *client, u8 *buf, int len);
int cst3xx_i2c_write(struct i2c_client *client, u8 *buf, int len);
int cst3xx_i2c_read_register(struct i2c_client *client, u8 *buf, int len);

/* 芯片探测/固件 */
int cst3xx_firmware_info(struct i2c_client *client);
int cst8xx_firmware_info(struct i2c_client *client);
int hyn_firmware_info(struct hyn_ts_data *ts);
int hyn_detect_bootloader(struct i2c_client *client);
int hyn_detect(struct hyn_ts_data *ts);
int hyn_boot_update_fw(struct i2c_client *client);

/* 工作模式 */
int hynitron_touch_setmode(struct i2c_client *client, u8 mode, bool sleep);
void hynitron_wake_up_wait(void);
void hynitron_wait_event_wait(void);

/* GPIO / 上下电 */
int hyn_gpio_configure(struct hyn_ts_data *ts);
void hyn_reset_proc(int ms);
void hyn_enter_deep_sleep(struct hyn_ts_data *ts);

/* 输入设备 / 中断 */
int hyn_input_dev_int(struct hyn_ts_data *ts);
int hyn_irq_init(struct hyn_ts_data *ts);
void hyn_irq_disable(struct hyn_ts_data *ts);
void hyn_irq_enable(struct hyn_ts_data *ts);
void hyn_reset_watchdog_proc(void);
irqreturn_t hyn_ts_irq_handler(int irq, void *dev_id);
irqreturn_t hyn_eint_interrupt_handler(int irq, void *dev_id);
void hyn_work_func(struct work_struct *work);
void cst3xx_touch_report(struct hyn_ts_data *ts);
void cst8xx_touch_report(struct hyn_ts_data *ts);

/* 固件升级 */
int hyn_update_firmware_init(struct hyn_ts_data *ts);
int hynitron_read_fw_file(void);
void hyn_save_log(u8 *data, int len);
void hyn_save_noise_log(u8 *data, int len);

/* proc / sysfs */
int hynitron_proc_fs_init(void);
void hynitron_proc_fs_exit(void);
int hyn_create_sysfs(struct hyn_ts_data *ts);
void hyn_release_sysfs(struct hyn_ts_data *ts);
ssize_t hynitron_proc_read_foobar(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos);
ssize_t hynitron_proc_write_foobar(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos);
void hyn_check_diff(u8 *buf, int len);
long hyn_exc_cmdpack(void *data);

/* 初始化 */
int hyn_platform_data_init(struct hyn_ts_data *ts);
int hyn_ts_data_init(struct hyn_ts_data *ts);

#endif /* _HYNITRON_TP_H_ */
