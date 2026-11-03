// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron capacitive touchscreen - i2c 原语、芯片信息、模式切换、proc 初始化。
 *
 * 重建自 vendor hynitron_ts.ko（.text 0x8094 = 32916 字节，68 个函数）。
 * 函数地址/大小对照：
 *   hyn_i2c_read           0x02d8 (160)
 *   hyn_i2c_write          0x0378 (100)
 *   hyn_i2c_write_byte     0x03dc (160)
 *   hyn_i2c_read_byte      0x047c (204)
 *   hyn_i2c_write_bytes    0x0548 (296)
 *   hyn_i2c_read_bytes     0x0670 (280)
 *   cst8xx_firmware_info   0x0d30 (516)
 *   cst3xx_firmware_info   0x08c4 (1132)
 *   hyn_firmware_info      0x0f08 (140, 分派)
 *   hynitron_touch_setmode 0x1fa0 (400)
 *   hynitron_wake_up_wait  0x2130 (104)
 *   hynitron_wait_event_wait 0x2198 (252)
 *   hynitron_proc_fs_init  0x2294 (504)
 *   hynitron_proc_fs_exit  0x4040 (144)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include "hynitron_common.h"

struct hyn_ts_data *hyn_ts_data;
struct hyn_ts_data *gExClient;
struct proc_dir_entry *g_proc_dir;
struct proc_dir_entry *g_update_file;
struct task_struct *gExchandle;
u8 g_cmdret;
u8 g_cmdpack[2048];
u32 g_cmdlen;
u8 g_exc_flag;
u8 CMDIndex;
wait_queue_head_t debug_waiter;
wait_queue_head_t run_cmd_waiter;
struct mutex g_procrw_mutex;

static struct lock_class_key hynitron_proc_fs_init_key;

/*
 * hyn_reset_proc() —— 拉低 reset 保持 ms 个 1ms 单位，再拉高。
 * 二进制 0x58c8：gpiod_direction_output_raw(desc, 0); __const_udelay x ms;
 * gpiod_direction_output_raw(desc, 1);
 */
void hyn_reset_proc(int ms)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	int i;

	pr_err("[HYN]%s: Enter\n", "hyn_reset_proc");

	if (!ts || !ts->pdata)
		return;

	gpiod_direction_output_raw(gpio_to_desc(ts->pdata->reset_gpio), 0);

	for (i = 0; i < ms; i++)
		__const_udelay(0x418958);

	gpiod_direction_output_raw(gpio_to_desc(ts->pdata->reset_gpio), 1);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_reset_proc", 0);
}

/*
 * hyn_i2c_read() —— 二进制里只使用 x0(client)、x3(buf)、w4(len)：先发 0 字节
 * 写传输（写缓冲即 buf），再读 len 字节。其余形参在原厂代码中未被使用。
 */
int hyn_i2c_read(struct i2c_client *client, u8 *buf, int len, int addr, int mode)
{
	int ret;

	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_read");
		return -1;
	}

	ret = i2c_transfer_buffer_flags(client, buf, len, 0);
	if (ret < 0)
		pr_err("[HYN][Error]i2c_master_send error\n\n");

	ret = i2c_transfer_buffer_flags(client, buf, len, 1);
	if (ret < 0)
		pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_read);

int hyn_i2c_write(struct i2c_client *client, u8 *buf, int len)
{
	int ret;

	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_write");
		return -1;
	}

	ret = i2c_transfer_buffer_flags(client, buf, len, 0);
	if (ret < 0)
		pr_err("[HYN][Error]i2c_master_send error\n\n");

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_write);

int hyn_i2c_write_byte(struct i2c_client *client, u8 b0, u8 b1)
{
	u8 buf[2];
	int ret;

	buf[0] = b0;
	buf[1] = b1;

	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_write");
		return -1;
	}

	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
		ret = -1;
	}

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_write_byte);

int hyn_i2c_read_byte(struct i2c_client *client, u8 reg, u8 *out)
{
	u8 reg_buf = reg;
	int ret;

	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_read");
		return -1;
	}

	ret = i2c_transfer_buffer_flags(client, &reg_buf, 1, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
	} else {
		ret = i2c_transfer_buffer_flags(client, out, 1, 1);
		if (ret < 0)
			pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");
	}

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_read_byte);

int hyn_i2c_write_bytes(u8 addr, const u8 *data, u16 len, u8 addr_mode)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 tbuf[0x258];
	struct i2c_client *client;
	int total;
	int ret;

	memset(tbuf, 0, sizeof(tbuf));

	if (addr_mode == 1) {
		tbuf[0] = addr;
		memcpy(tbuf + 1, data, len);
		total = len + 1;
	} else {
		*(u16 *)tbuf = bswap16(addr);
		memcpy(tbuf + 2, data, len);
		total = len + 2;
	}

	client = ts->client;
	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_write");
		ret = -1;
	} else {
		ret = i2c_transfer_buffer_flags(client, tbuf, total, 0);
		if (ret < 0)
			pr_err("[HYN][Error]i2c_master_send error\n\n");
	}

	if (ret < 0)
		pr_err("[HYN][Error]%s i2c write error.\n\n", "hyn_i2c_write_bytes");

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_write_bytes);

int hyn_i2c_read_bytes(u8 addr, u8 *data, u16 len, u8 addr_mode)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 tbuf[8] = { 0 };
	struct i2c_client *client;
	int ret;

	if (addr_mode == 1)
		tbuf[0] = addr;
	else
		*(u16 *)tbuf = bswap16(addr);

	client = ts->client;
	if (!client) {
		pr_err("[HYN][Error][IIC][%s]i2c_client==NULL!\n", "hyn_i2c_read");
		return -1;
	}

	ret = i2c_transfer_buffer_flags(client, tbuf, addr_mode, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
	} else {
		ret = i2c_transfer_buffer_flags(client, data, len, 1);
		if (ret < 0)
			pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");
	}

	if (ret < 0)
		pr_err("[HYN][Error]f%s: i2c read error.\n\n", "hyn_i2c_read_bytes");

	return ret;
}
EXPORT_SYMBOL(hyn_i2c_read_bytes);

int cst3xx_i2c_read(struct i2c_client *client, u8 *buf, int len)
{
	int ret = i2c_transfer_buffer_flags(client, buf, len, 1);

	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, len, 1);

	return ret;
}

int cst3xx_i2c_write(struct i2c_client *client, u8 *buf, int len)
{
	int ret = i2c_transfer_buffer_flags(client, buf, len, 0);

	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, len, 0);

	return ret;
}

int cst3xx_i2c_read_register(struct i2c_client *client, u8 *buf, int len)
{
	int ret = i2c_transfer_buffer_flags(client, buf, 2, 0);

	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);

	ret = i2c_transfer_buffer_flags(client, buf, len, 1);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, len, 1);

	return ret;
}

/*
 * cst3xx_firmware_info() —— 通过固定命令序列读取 checkcode / chip type /
 * project id / fw version / checksum / module id。
 */
int cst3xx_firmware_info(struct i2c_client *client)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 buf[8] = { 0 };
	int ret;

	pr_err("[HYN]%s: Enter\n", "cst3xx_firmware_info");

	/* 1. 读 chip checkcode */
	*(u16 *)buf = 0x01d1;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	mdelay(10);

	*(u16 *)buf = 0xfcd1;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	ret = i2c_transfer_buffer_flags(client, buf, 4, 1);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 4, 1);
	if (ret < 0)
		goto err;

	ts->chip_ic_checkcode = *(u32 *)buf;
	pr_err("[HYN][Info]the chip chip_ic_checkcode:0x%x.\n\n",
	       ts->chip_ic_checkcode);

	if ((u16)(ts->chip_ic_checkcode >> 16) != 0xcaca)
		goto read_error;

	/* 2. 读 chip type / project id */
	mdelay(10);

	*(u16 *)buf = 0x04d2;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	ret = i2c_transfer_buffer_flags(client, buf, 4, 1);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 4, 1);
	if (ret < 0)
		goto err;

	ts->chip_ic_type = *(u16 *)(buf + 2);
	ts->chip_ic_type2 = *(u16 *)(buf + 2);
	ts->chip_ic_project_id = *(u16 *)buf;
	pr_err("[HYN][Info]chip_ic_type :0x%x, chip_ic_project_id:0x%x\n\n",
	       ts->chip_ic_type, ts->chip_ic_project_id);

	/* 3. 读 fw version / checksum */
	__const_udelay(0x8312b0);

	*(u16 *)buf = 0x08d2;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	ret = i2c_transfer_buffer_flags(client, buf, 8, 1);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 8, 1);
	if (ret < 0)
		goto err;

	ts->chip_ic_fw_version = *(u16 *)buf;
	ts->chip_ic_checksum = *(u32 *)(buf + 4);
	pr_err("[HYN][Info]chip_ic_fw_version:0x%x, chip_ic_checksum:0x%x\n\n",
	       ts->chip_ic_fw_version, ts->chip_ic_checksum);

	/* 4. 读 module id */
	*(u16 *)buf = 0x47d0;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	ret = i2c_transfer_buffer_flags(client, buf, 1, 1);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 1, 1);
	if (ret < 0)
		goto err;

	ts->chip_ic_module_id = buf[0];
	pr_err("[HYN][Info]chip_ic_module_id:0x%x.\n\n", ts->chip_ic_module_id);

	/* 5. 结束命令 */
	*(u16 *)buf = 0x09d1;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret <= 0)
		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0)
		goto err;

	__const_udelay(0x147aeb8);
	pr_err("[HYN]%s: Exit(%d)\n", "cst3xx_firmware_info", 0xb1c);
	return 0;

read_error:
	pr_err("[HYN][Error]cst3xx_firmware_info read error .\n\n");
	ts->chip_ic_checkcode = -1;
	return -1;

err:
	return -1;
}

int cst8xx_firmware_info(struct i2c_client *client)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 cmd;
	u8 val = 0;
	u16 chip_ver = 0;
	int ret;

	pr_err("[HYN]%s: Enter\n", "cst8xx_firmware_info");

	msleep(100);

	/* 读 chip version（命令 0xa6，随后读 2 字节） */
	cmd = 0xa6;
	ret = i2c_transfer_buffer_flags(client, &cmd, 1, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
		goto read_error;
	}

	ret = i2c_transfer_buffer_flags(client, (u8 *)&chip_ver, 2, 1);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");
		goto read_error;
	}

	/* 读 0xa8 单字节状态 */
	cmd = 0xa8;
	ret = i2c_transfer_buffer_flags(client, &cmd, 1, 0);
	if (ret < 0)
		pr_err("[HYN][Error]i2c_master_send error\n\n");

	ret = i2c_transfer_buffer_flags(client, &val, 1, 1);
	if (ret < 0)
		pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");

	pr_err("[HYN][Info]cst8xx_firmware_info hyn_i2c_read_byte 0xA8=0x%02X.\n", val);
	pr_err("[HYN][Info]\nhyn chipversion: %x\n\n", chip_ver);
	pr_err("[HYN][Info]\nhyn update_fw_flag: %x\n\n", 1);

	pr_err("[HYN]%s: Exit(%d)\n", "cst8xx_firmware_info", 0xb4f);
	return ret;

read_error:
	dev_err(&ts->client->dev, "f%s: i2c read error.\n", "cst8xx_firmware_info");
	return -1;
}

int hyn_firmware_info(struct hyn_ts_data *ts)
{
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_firmware_info");

	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST1XX:
		ret = cst3xx_firmware_info(ts->client);
		break;
	case HYN_CHIP_CST8XX:
		ret = cst8xx_firmware_info(ts->client);
		break;
	default:
		ret = -1;
		break;
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_firmware_info", 0xb86);

	if (ret < 0)
		return ret;

	pr_err("[HYN][Info]hyn_firmware_info success.\n\n");
	return 0;
}

/*
 * hynitron_touch_setmode() —— 切换工作模式（mode 1..3 为不同扫描模式，
 * 其它值为默认模式），sleep 为真时进入低功耗。
 */
int hynitron_touch_setmode(struct i2c_client *client, u8 mode, bool sleep)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 buf[2];
	int ret;

	pr_err("[HYN]%s: Enter\n", "hynitron_touch_setmode");

	if (!client) {
		pr_err("[HYN][Info]client is null.\n");
		return -1;
	}

	if (!mode || mode > 4)
		pr_err("[HYN][Info]cmd[0] error:%d.\n", mode);

	pr_err("[HYN][Info]set work_mode(0x%02x).\n", mode);

	ts->config_chip_type = mode;
	ts->hyn_irq_flag = 0;

	buf[0] = sleep ? 0xd1 : 0x00;
	switch (mode) {
	case 1:
		buf[1] = sleep ? 0x19 : 0x04;
		break;
	case 2:
		buf[1] = sleep ? 0x0a : 0x06;
		break;
	case 3:
		buf[1] = sleep ? 0x0d : 0x07;
		break;
	default:
		buf[1] = sleep ? 0x00 : 0x09;
		break;
	}

	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
		pr_err("[HYN][Error] cst3xx Write command normal mode failed.error:%d.\n\n",
		       ret);
		pr_err("[HYN][Info]hynitron_touch_setmode fail.\n");
		if (sleep)
			pr_err("[HYN][Error]Set TP to sleep mode fail, ret=%d!\n", ret);
	}

	pr_err("[HYN][Error] cst3xx Write command fail\n");
	pr_err("[HYN][Error] cst3xx Write command1 fail\n");
	pr_err("[HYN][Error] cst3xx Write command2 fail\n");
	pr_err("[HYN][Error] cst3xx Write command3 fail\n");
	pr_err("[HYN]%s: Exit(%d)\n", "hynitron_touch_setmode", 0x1ff);
	return ret;
}

void hynitron_wake_up_wait(void)
{
	if (!hyn_ts_data->config_chip_type)
		return;

	__wake_up(&debug_waiter, TASK_UNINTERRUPTIBLE, 1, NULL);
	hyn_ts_data->hyn_irq_flag = 1;
	pr_err("[HYN][Info]hynitron_wake_up_wait exit********hyn_ts_data->hyn_irq_flag:%d. \n\n",
	       hyn_ts_data->hyn_irq_flag);
}

void hynitron_wait_event_wait(void)
{
	DEFINE_WAIT(wq);
	long ret;

	pr_err("[HYN]%s: Enter\n", "hynitron_wait_event_wait");

	if (!hyn_ts_data->hyn_irq_flag) {
		for (;;) {
			init_wait_entry(&wq, 0);
			ret = prepare_to_wait_event(&debug_waiter, &wq,
						    TASK_UNINTERRUPTIBLE);
			if (hyn_ts_data->hyn_irq_flag)
				break;
			if (ret)
				break;
			schedule();
		}
		finish_wait(&debug_waiter, &wq);
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hynitron_wait_event_wait", 0x1da);
}

/*
 * hynitron_proc_fs_init() —— 按芯片类型建立 /proc/cst1xx_ts 或 /proc/cst8xx_ts，
 * update 文件与 debug kthread。
 */
int hynitron_proc_fs_init(void)
{
	struct hyn_ts_data *ts = hyn_ts_data;

	pr_err("[HYN]%s: Enter\n", "hynitron_proc_fs_init");

	__mutex_init(&g_procrw_mutex, "&g_procrw_mutex", &hynitron_proc_fs_init_key);

	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST1XX:
		g_proc_dir = proc_mkdir("cst1xx_ts", NULL);
		if (!g_proc_dir) {
			pr_err("[HYN][Info]proc_mkdir HYNITRON_PROC_DIR_NAME failed.\n\n");
			return -1;
		}
		break;
	case HYN_CHIP_CST8XX:
		g_proc_dir = proc_mkdir("cst8xx_ts", NULL);
		if (!g_proc_dir) {
			pr_err("[HYN][Info]proc_mkdir HYNITRON_PROC_DIR_NAME_2 failed.\n\n");
			return -1;
		}
		break;
	default:
		break;
	}

	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST1XX:
		g_update_file = proc_create_data("cst1xx-update", 0x81ff, g_proc_dir,
						 &proc_tool_debug_fops, ts->client);
		if (!g_update_file) {
			pr_err("[HYN][Info]proc_create_data HYNITRON_PROC_FILE_NAME failed.\n\n");
			goto remove_dir;
		}
		break;
	case HYN_CHIP_CST8XX:
		g_update_file = proc_create_data("cst8xx-update", 0x81ff, g_proc_dir,
						 &proc_tool_debug_fops, ts->client);
		if (!g_update_file) {
			pr_err("[HYN][Info]proc_create_data HYNITRON_PROC_FILE_NAME_2 failed.\n\n");
			goto remove_dir;
		}
		break;
	default:
		break;
	}

	if (!gExchandle) {
		gExchandle = kthread_create_on_node(hyn_exc_cmdpack, NULL, -1,
						    "hyn-debug-thread");
		if (!IS_ERR(gExchandle))
			wake_up_process(gExchandle);
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hynitron_proc_fs_init", 0x4d9);
	return 0;

remove_dir:
	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST1XX:
		proc_remove(g_proc_dir);
		g_proc_dir = NULL;
		break;
	case HYN_CHIP_CST8XX:
		proc_remove(g_proc_dir);
		g_proc_dir = NULL;
		break;
	default:
		break;
	}

	return -1;
}

void hynitron_proc_fs_exit(void)
{
	if (gExchandle) {
		kthread_stop(gExchandle);
		gExchandle = NULL;
	}

	if (g_update_file) {
		proc_remove(g_update_file);
		g_update_file = NULL;
	}

	if (g_proc_dir) {
		proc_remove(g_proc_dir);
		g_proc_dir = NULL;
	}
}
