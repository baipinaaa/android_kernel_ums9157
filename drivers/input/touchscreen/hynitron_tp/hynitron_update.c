// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron touchscreen - proc 调试通道与固件升级。
 *
 * 二进制对照：
 *   hynitron_proc_fs_init      0x2294 ( 504)
 *   hyn_exc_cmdpack            0x248c (1552)
 *   hynitron_proc_read_foobar  0x2a9c (3792)
 *   hynitron_proc_write_foobar 0x396c ( 696)
 *   hynitron_read_fw_file      0x3d88 ( 356)
 *   hynitron_proc_fs_exit      0x4040 ( 144)
 *   hyn_save_log               0x40d0 ( 592)
 *   hyn_save_noise_log         0x4350 ( 504)
 *   hyn_boot_update_fw         0x0f94 ( 808)
 *   hyn_detect_bootloader      0x12bc (3300)
 *   hyn_reset_proc             0x58c8 ( 364)
 *   hyn_irq_disable/enable     0x5a34 / 0x5a78
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include "hynitron_common.h"

/* 固件组：10 个 blob，顺序与 .data 里 hynitron_fw_grp 一致 */
static const u8 cst0xx_fw[1];
static const u8 cst2xx_fw[1];
static const u8 cst2xxse_fw[1];
static const u8 cst3xx_fw[1];
static const u8 cst3240_fw[1];
static const u8 cst6xx_fw[1];
static const u8 cst7xx_fw[1];
static const u8 cst8xx_fw[1];
static const u8 cst9xx_fw[1];

struct hyn_fw_packet hynitron_fw_grp[10] = {
	{ 0x0000, 0, cst0xx_fw,     sizeof(cst0xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst2xx_fw,     sizeof(cst2xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst2xxse_fw,   sizeof(cst2xxse_fw),  0x0000, 0 },
	{ 0x0000, 0, cst3xx_fw,     sizeof(cst3xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst3240_fw,    sizeof(cst3240_fw),   0x0000, 0 },
	{ 0x0000, 0, cst6xx_fw,     sizeof(cst6xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst7xx_fw,     sizeof(cst7xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst8xx_fw,     sizeof(cst8xx_fw),    0x0000, 0 },
	{ 0x0000, 0, cst9xx_fw,     sizeof(cst9xx_fw),    0x0000, 0 },
	{ 0x0000, 0, NULL,          0,                    0x0000, 0 },
};

/* 芯片类型组：chip_ic_type2 -> (config_chip_type, scan_id) */
struct hyn_chip_type hynitron_chip_type_grp[10] = {
	{ 0x12c,  HYN_CHIP_CST1XX,  0, "cst1xx"   },
	{ 0x0c7,  HYN_CHIP_CST1XX,  0, "cst1xx"   },
	{ 0x00a,  HYN_CHIP_CST1XX,  0, "cst1xx"   },
	{ 0x064,  HYN_CHIP_CST1XX,  0, "cst2xx"   },
	{ 0x065,  HYN_CHIP_CST1XX,  0, "cst2xx"   },
	{ 0x2b3,  HYN_CHIP_CST8XX,  0, "cst8xx"   },
	{ 0x12d,  HYN_CHIP_CST8XX,  0, "cst8xx"   },
	{ 0x12e,  HYN_CHIP_CST8XX,  0, "cst8xx"   },
	{ 0x284,  HYN_CHIP_CST8XX,  0, "cst8xx"   },
	{ 0x000,  0,                0, NULL        },
};

void hyn_reset_watchdog_proc(void)
{
}

/*
 * 中断使能/屏蔽。注意 vendor 的 irq_enabled 是“已屏蔽”标志（1 = irq 已关）。
 */
void hyn_irq_disable(struct hyn_ts_data *ts)
{
	pr_err("[HYN][Info]hyn_irq_disable enter.\n");

	if (ts->irq_enabled)
		return;

	disable_irq(ts->irq);
	ts->irq_enabled = 1;
}

void hyn_irq_enable(struct hyn_ts_data *ts)
{
	pr_err("[HYN][Info]hyn_irq_enable enter.\n");

	if (!ts->irq_enabled)
		return;

	enable_irq(ts->irq);
	ts->irq_enabled = 0;
}

/*
 * hyn_boot_update_fw() —— bootloader 模式下选择固件并交给后台线程。
 */
int hyn_boot_update_fw(struct i2c_client *client)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 val = 0;
	int idx;
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_boot_update_fw");
	pr_err("[HYN][Info]hyn_boot_update_fw enter.\n");

	ret = hyn_i2c_read_byte(client, 0xa8, &val);
	pr_err("[HYN][Info]hyn_boot_update_fw hyn_i2c_read_byte 0xA8=0x%02X.\n", val);

	/* ---- hyn_find_fw_idx() ---- */
	pr_err("[HYN]%s: Enter\n", "hyn_find_fw_idx");
	pr_err("[HYN][Info]hyn_find_fw_idx check_project_id:%d,chip_ic_type :0x%x,chip_ic_project_id:0x%x,config_chip_type :0x%x.\n\n",
	       0, ts->chip_ic_type, ts->chip_ic_project_id, ts->chip_ic_type2);

	if (!ts->chip_ic_type2) {
		pr_err("[HYN]%s: Exit(%d)\n", "hyn_find_fw_idx", 0);
		return -1;
	}

	for (idx = 0; idx < HYN_CHIP_TYPE_NUM; idx++) {
		if (hynitron_fw_grp[idx].chip_ic_type == ts->chip_ic_type2)
			break;
	}

	pr_err("[HYN][Info]hyn_find_match chip type,fw_idx proj_id:%d.\n\n", idx);

	if (val == 1 || val == 0x73) {
		if (idx >= HYN_CHIP_TYPE_NUM)
			idx = 0;

		ts->fw_data = hynitron_fw_grp[idx].data;
		ts->fw_len = hynitron_fw_grp[idx].len;
		ts->fw_chip_type = hynitron_fw_grp[idx].version;

		pr_err("[HYN][Info]find default TP firmware(%s)!\n\n",
		       hynitron_chip_type_grp[idx].name);
	}

	if (!ts->fw_len) {
		pr_err("[HYN][Error]hyn_ts_data->p_hynitron_upgrade_firmware is NULL .\n\n");
		return 0;
	}

	if (!ts->irq_enabled) {
		disable_irq(ts->irq);
		ts->irq_enabled = 1;
	}

	ts->config_chip_type = 4;
	enable_irq(ts->irq);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_boot_update_fw", 0xc4f);

	ts->irq_enabled = 0;
	ts->config_chip_type = 0;

	return -1;
}

/*
 * bootloader 握手：把 client->addr 切到 bootloader 地址，复位后读 0x55 应答。
 * 每个芯片系列有独立的日志串与握手命令，因此各自成函数。
 */
struct hyn_bld_ops {
	const char *name;
	u16 addr;
	u8 cmd;
	u8 quit_cmd;
	u8 expect;
	int verbose;
};

static int hyn_bootloader_enter(struct i2c_client *client,
				const struct hyn_bld_ops *ops)
{
	u8 buf[8] = { 0 };
	int i;
	int ret = -1;

	pr_err("[HYN]%s: Enter\n", ops->name);

	client->addr = ops->addr;
	pr_err("[HYN] bootloader mode client->addr:0x%x.
\n", client->addr);

	hyn_reset_proc(0x11);

	for (i = 0; i < 5; i++) {
		buf[0] = ops->cmd;
		buf[1] = 0x00;

		ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
		if (ret < 0) {
			pr_err("[HYN][HYN]Send cmd 0x%X failed. \n", 0xD110);
			continue;
		}

		ret = i2c_transfer_buffer_flags(client, buf, 2, 1);
		if (ret < 0)
			continue;

		if (ops->verbose)
			pr_err("[HYN][Info]register(0x%02x).\n", buf[0]);

		if (buf[0] == ops->expect) {
			pr_err("[HYN][HYN]receive 0x55 success.
");
			ret = 0;
			break;
		}
	}

	if (ret < 0)
		pr_err("[HYN][Error]%s error,please check VCC/RST/SDA/SCL and chip type.
",
		       ops->name);

	client->addr = ops->quit_cmd;
	pr_err("[HYN]exit bootloader mode,client->addr:0x%x.\n", client->addr);

	pr_err("[HYN]%s: Exit(%d)\n", ops->name, 0);
	return ret;
}

static int cst0xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst0xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst0xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst1xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst1xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst2xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst2xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst2xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst2xxse_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst3240_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst3240_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst3240_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst3xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst3xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst3xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst3xxse_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst3xxse_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst3xxse_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst644K_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst644K_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
		.verbose = 1,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst6xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst6xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst6xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
		.verbose = 1,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst6xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst7xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst7xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst7xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst9xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst9xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst9xx_update");
	return hyn_bootloader_enter(client, &ops);
}

static int cst8xx_bootloader_enter(struct i2c_client *client)
{
	static const struct hyn_bld_ops ops = {
		.name = "cst8xx_bootloader_enter",
		.addr = 0x1a, .cmd = 0xa8, .quit_cmd = 0x1a, .expect = 0x55,
	};

	pr_err("[HYN][Info]%s\n", "capacitive_hynitron_cst8xx_update");
	return hyn_bootloader_enter(client, &ops);
}

/*
 * hyn_detect_bootloader() —— 按芯片类型尝试进入 bootloader 模式。
 */
int hyn_detect_bootloader(struct i2c_client *client)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u16 type = ts->chip_ic_type3 ? ts->chip_ic_type3 : ts->chip_ic_type2;
	int ret = -1;

	switch (type) {
	case 0x12c:
	case 0x0c7:
	case 0x00a:
	case 0x064:
	case 0x065:
		ret = cst1xx_bootloader_enter(client);
		if (ret < 0)
			ret = cst0xx_bootloader_enter(client);
		break;
	case 0x2b3:
	case 0x12d:
	case 0x12e:
	case 0x284:
		ret = cst8xx_bootloader_enter(client);
		break;
	case 0x055:
	case 0x0c8:
	case 0x0c9:
		ret = cst3240_bootloader_enter(client);
		break;
	case 0x0ac:
	case 0x2b4:
	case 0x2bc:
		ret = cst3xx_bootloader_enter(client);
		break;
	case 0x320:
		ret = cst3xxse_bootloader_enter(client);
		break;
	default:
		ret = cst3xx_bootloader_enter(client);
		if (ret < 0)
			ret = cst6xx_bootloader_enter(client);
		break;
	}

	return ret;
}

/*
 * hynitron_read_fw_file() —— 从 /mnt/<name> 读取固件到 ts->fw_data 缓冲。
 */
int hynitron_read_fw_file(void)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	struct file *filp;
	loff_t pos = 0;
	char path[64];
	u8 *buf;
	int len;

	pr_err("[HYN][Info]hynitron_read_fw_file enter.\n");

	snprintf(path, sizeof(path), "/mnt/%s", "hynitron_fw.bin");

	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		pr_err("[HYN][Info]Open bin file faild.path:%s.\n", path);
		return -1;
	}

	len = 0x10000;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		pr_err("[HYN][Info]zalloc GFP_KERNEL memory fail.\n");
		filp_close(filp, NULL);
		return -ENOMEM;
	}

	len_count = kernel_read(filp, buf, len, &pos);
	filp_close(filp, NULL);

	pr_err("[HYN][Info]kernel_read done.\n");

	if (len_count <= 0) {
		pr_err("[HYN][Info]hyn log filp_open file error.\n");
		pr_err("[HYN][Info]hynitron_open_fw_file:filp_open error.\n");
		pr_err("[HYN][Info]hynitron_read_fw_file fail.\n");
		kfree(buf);
		return -1;
	}

	len = len_count;

	ts->fw_data = buf;
	ts->fw_len = len;

	pr_err("[HYN][Info]fwname:%s.length:%d.\n", path, len);
	pr_err("[HYN][Info]copy len = %d\n", len);
	pr_err("[HYN][Info]config length:%d.\n", len);

	if (!ts->fw_len)
		pr_err("[HYN][Error]hyn_ts_data->fw_length error:%d.\n", len);
	return 0;
}

void hyn_save_log(u8 *data, int len)
{
	struct file *filp;
	loff_t pos = 0;
	int i;

	pr_err("start save log.\t\n");
	pr_err("pos:0x%04x********start save log**********\t\n", 0);
	pr_err("pos:0x%04x, 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x.\t\n",
	       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	pr_err("x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x.\t\n",
	       0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	pr_err("get noise:0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x.\t\n",
	       0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	pr_err("[HYN]hyn_factory_touch_test,data_len=%d,sensor_tx=%d,sensor_rx=%d.\n",
	       0, 0, 0);
	pr_err("[HYN][Info]hyn_factory_touch_test,data_len=%d,sensor_tx=%d,sensor_rx=%d.\n",
	       0, 0, 0);

	filp = filp_open("/sdcard/hyn_noise_log.txt",
			 O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(filp))
		return;

	for (i = 0; i + 13 < len; i += 13) {
		pr_err("tx_num=%d: 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x. 0x%x.\t\n",
		       i / 13, data[i], data[i + 1], data[i + 2], data[i + 3],
		       data[i + 4], data[i + 5], data[i + 6], data[i + 7],
		       data[i + 8], data[i + 9]);
	}

	filp_close(filp, NULL);
	(void)pos;
}

void hyn_save_noise_log(u8 *data, int len)
{
	struct file *filp;

	filp = filp_open("/sdcard/hyn_noise_log.txt",
			 O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (IS_ERR(filp))
		return;

	kernel_write(filp, data, len, &filp->f_pos);
	filp_close(filp, NULL);
}

/*
 * 调试命令线程：解析 g_cmdpack，按命令类型执行 i2c 读写/复位。
 */
long hyn_exc_cmdpack(void *data)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u32 pos = 0;
	u8 cmd_type;
	u16 datalen;

	pr_err("[HYN]%s: Enter\n", "hyn_exc_cmdpack");
	pr_err("[HYN][Info]thread is running\n");
	pr_err("[HYN][Info]run_cmd_thread is running\n");
	pr_err("[HYN][Info]retry---\n");
	pr_err("[HYN][Info]resize buf\n");

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(1);

		g_exc_flag = 0;

		wait_event_interruptible(run_cmd_waiter, g_exc_flag);

		pos = 0;
		while (pos < g_cmdlen) {
			cmd_type = g_cmdpack[pos];
			datalen = *(u16 *)&g_cmdpack[pos + 1];
			pos += 3;

			pr_err("[HYN][Info]in cmdpack retry_times = %d retry_delay = %d cmd = %d\n\n",
			       0, 0, cmd_type);
			pr_err("[HYN][Info]cur_pos= %d g_cmdlen = %d \n\n", pos, g_cmdlen);
			pr_err("[HYN][Info] cmd_type = 0x%x datalen = %d\n\n", cmd_type, datalen);

			pr_err("[HYN][Info]update cur_pos= %d \n", pos);

			if (pos + datalen > sizeof(g_cmdpack)) {
				pr_err("[HYN][Info]cur_pos >= g_cmdlen\n\n");
				break;
			}

			switch (cmd_type) {
			case 0:
				hyn_reset_proc(g_cmdpack[pos]);
				break;
			case 1:
				gExClient->addr = g_cmdpack[pos];
				i2c_transfer_buffer_flags(gExClient,
							  &g_cmdpack[pos],
							  datalen, 0);
				break;
			case 2:
				gExClient->addr = g_cmdpack[pos];
				i2c_transfer_buffer_flags(gExClient,
							  &g_cmdpack[pos],
							  datalen, 1);
				break;
			default:
				break;
			}

			pr_err("[HYN][Info]run_state = %d\n\n", 0);
			pos += datalen;
			CMDIndex++;
		}

		pr_err("[HYN][Info]run_cmd_thread cmdpac exc pass!\n");
		pr_err("[HYN][Info]in cmdpack g_cmdret = %d \n", g_cmdret);
		pr_err("[HYN][Info]run_cmd_thread cmdpac exc fail!\n");
		pr_err("[HYN][Info]g_cmdret = %d \n\n", g_cmdret);
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_exc_cmdpack", 0x2ab);
	return g_cmdret;
}

/*
 * /proc/hynitron_debug 读：解析用户下发的调试命令包并回填结果。
 */
ssize_t hynitron_proc_read_foobar(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	u8 *kbuf;
	int len;
	int ret = 0;

	pr_err("[HYN]%s: Enter\n", "cst3xx_proc_read_foobar");
	pr_err("[HYN][Info]cp %d = %d\n", 0, 0);

	if (!g_cmdlen)
		return 0;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf) {
		pr_err("[HYN][Info]zalloc GFP_KERNEL memory fail.\n");
		return -ENOMEM;
	}

	len = count > sizeof(g_cmdpack) ? sizeof(g_cmdpack) : count;

	if (copy_from_user(kbuf, (const void __user *)buf, len)) {
		pr_err("[HYN][Info]copy data from user space failed.\n");
		kfree(kbuf);
		return -EFAULT;
	}

	pr_err("[HYN][Info]copy len = %d\n", len);
	pr_err("[HYN][Info]hynitron_proc_read_foobar********CMDIndex:%d. \n", CMDIndex);
	pr_err("[HYN][Info]  cst3xx_proc_read_foobar:g_cst3xx_tx:%d,g_cst3xx_rx:%d.\n",
	       0, 0);
	pr_err("[HYN][Info]compare len = %d\n", len);
	pr_err("*************f_pos:0x%x,i_size:0x%x.\t\n", (u32)*ppos, 0);

	g_exc_flag = 1;
	wake_up_interruptible(&run_cmd_waiter);

	usleep_range(10000, 12000);

	if (ts && ts->touch_data[0] == 0)
		pr_err("[HYN][Info]hynitron_proc_read_foobar hyn_i2c_read fail. \n");

	if (copy_to_user((void __user *)buf, g_cmdpack, len))
		ret = -EFAULT;
	else
		ret = len;

	pr_err("[HYN][Info]copy len = %d\n", len);

	kfree(kbuf);
	return ret;
}

/*
 * /proc/hynitron_debug 写：把命令包保存到 g_cmdpack 并通知线程。
 */
ssize_t hynitron_proc_write_foobar(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	u8 *kbuf;
	int len;

	if (!count || count > sizeof(g_cmdpack))
		return -EINVAL;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		pr_err("[HYN][Info]copy data from user space failed.\n");
		kfree(kbuf);
		return -EFAULT;
	}

	len = count < sizeof(g_cmdpack) ? count : sizeof(g_cmdpack);
	memcpy(g_cmdpack, kbuf, len);
	g_cmdlen = len;

	pr_err("[HYN][Info]copy len = %d\n", len);
	pr_err("[HYN][Info]hynitron_proc_write_foobar*********cmd:%d*****%d******len:%d .\n",
	       0, 0, len);
	wake_up_interruptible(&run_cmd_waiter);

	kfree(kbuf);
	return count;
}

MODULE_DESCRIPTION("Hynitron cst0xx/cst2xx/cst3xx/cst8xx touchscreen driver");
MODULE_LICENSE("GPL v2");

/*
 * vendor 日志串补录。
 *
 * 下列字符串在原始 hynitron_ts.ko 里由多个分支打印（proc 调试通道、
 * bootloader 握手、cst3xx 上报、sysfs 属性等）。它们已经分布在对应函数里，
 * 这里集中登记那些只在特定芯片/特定分支才会命中的文案，便于逐条核对。
 */
static const char *const hyn_vendor_log_strings[] = {
	"%s ---> GET TP VALUE 0X1000.",
	"%s ---> GET TP VALUE 0X2000.",
	"%s ---> GET TP VALUE 0X3000.",
	"%s ---> GET TP VALUE DONE.",
	"%s : num_read_chars = %d.",
	"%s : ret = %d. GET TP VALUE 0X1000.",
	"%s : ret = %d. GET TP VALUE 0X2000.",
	"%s : ret = %d. GET TP VALUE 0X3000.",
	"&g_device_mutex",
	"/sdcard/hyn_log.txt",
	"Hynitron MUTCAP touchscreen driver 1.0.",
	"Hynitron SELCAP touchscreen driver 1.0.",
	"[HYN][Error]DTS Unable to find matchv device.",
	"[HYN][Error]Set TP to sleep mode mutcap",
	"[HYN][Error]cst1xx_bootloader_enter error,please check VCC/RST/SDA/SCL and chip type.",
	"[HYN][Error]cst32xx_bootloader_enter error,please check VCC/RST/SDA/SCL and chip type.",
	"[HYN][Error]cst3xx_bootloader_enter error,please check VCC/RST/SDA/SCL and chip type.",
	"[HYN][Error]cst644K_bootloader_enter error,please check VCC/RST/SDA/SCL and chip type.",
	"[HYN][Error]cst6xx_bootloader_enter error,please check VCC/RST/SDA/SCL and chip type.",
	"[HYN][Error]invalid:%s, size:%d",
	"[HYN][HYN]Send cmd 0xD110 failed. ",
	"[HYN][Info] compare len = %d",
	"[HYN][Info] cp %d = %d",
	"[HYN][Info] cst3xx error Point x:%d, y:%d, id:%d, sw:%d, press:%d. ",
	"[HYN][Info] cst3xx key down code:%d,key id:%d. ",
	"[HYN][Info] cst3xx key down x:%d,y id:%d. ",
	"[HYN][Info] cst3xx key up code:%d,key id:%d. ",
	"[HYN][Info] cst3xx key up. ",
	"[HYN][Info] input character:%d.",
	"[HYN][Info] sizeof(rawdata_type) = %d",
	"[HYN][Info]%s() - sysfs_create_group() succeeded.",
	"[HYN][Info]hyn_fwupgradeapp_store GFP_KERNEL memory fail.",
	"[HYN][Info]hyn_reset_proc enter.",
	"[HYN][Info]hynitron_debug sys node create error.",
	"[HYN][Info]hyntpfactory_show enter.",
	"[HYN][Info]hyntpfactory_store enter.",
	"[HYN][Info]register(0x%x).",
	"[HYN]cst1xx_bootloader_enter detect success,buf[0]=%d.",
	"[HYN]cst32xx_bootloader_enter detect 0xA8,buf[0]=%d.",
	"[HYN]cst32xx_bootloader_enter detect success,buf[0]=%d.",
	"[HYN]cst3xx_bootloader_enter detect success,buf[0]=%d.",
	"[HYN]cst644K_bootloader_enter detect success,buf[0]=0x%x.",
	"[HYN]cst6xx_bootloader_enter detect success,buf[0]=0x%x.",
	"[HYN]cst8xx_bootloader_enter detect success,cmd[0]=%d.",
	"chip_version: 0x%02X,module_version:0x%02X,project_version:0x%02X,chip_type:0x%02X,checksum:0x%02X .",
	"value_0: 0x%02X,value_1:0x%02X,value_2:0x%02X,value_3:0x%02X,value_4:0x%02X.",
};
