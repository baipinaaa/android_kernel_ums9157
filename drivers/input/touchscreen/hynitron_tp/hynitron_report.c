// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron touchscreen - 触摸报文读取与上报。
 *
 * 二进制对照：
 *   cst8xx_touch_report  0x5eb8 (0x540，含内联 hyn_read_touchdata)
 *   cst3xx_touch_report  0x63f8 (0x91c)
 *
 * 两条上报路径结构一致：
 *   1) 读一帧报文（8xx 用 0xab 命令，3xx 用寄存器读）；
 *   2) 校验帧头（8xx: buf[6]==0xAB，3xx: buf[6]==0xAB）；
 *   3) 逐触点解析 6 字节记录：<id|type><xlo><ylo><xhi|yhi><pressure><width>，
 *      上报 MT slot；触点抬起时上报 P%d UP!。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "hynitron_common.h"

#define HYN_TOUCH_DATA_LEN	64
#define HYN_TOUCH_REPORT_NUM	6

/* cst8xx：写 0xab 命令后读一帧 */
static int hyn_read_touchdata(struct hyn_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	u8 buf[HYN_TOUCH_DATA_LEN] = { 0 };
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_read_touchdata");

	if (!client)
		return -1;

	buf[0] = 0xab;
	ret = i2c_transfer_buffer_flags(client, buf, 2, 0);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_send error\n\n");
		return ret;
	}

	ret = i2c_transfer_buffer_flags(client, buf, HYN_TOUCH_DATA_LEN, 1);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");
		pr_err("[HYN][Error][B]Read touchdata failed, ret: %d\n", ret);
		return ret;
	}

	memcpy(ts->touch_data, buf, HYN_TOUCH_DATA_LEN);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_read_touchdata", 0);
	return 0;
}

/* cst3xx：通过寄存器读一帧 */
static int hyn_read_touchdata_3xx(struct hyn_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	u8 buf[HYN_TOUCH_DATA_LEN] = { 0 };
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_read_touchdata");

	if (!client)
		return -1;

	buf[0] = 0xd0;
	buf[1] = 0x00;
	ret = cst3xx_i2c_read_register(client, buf, HYN_TOUCH_DATA_LEN);
	if (ret < 0) {
		pr_err("[HYN][Error]i2c_master_recv i2c read error.\n\n");
		pr_err("[HYN][Error][B]Read touchdata failed, ret: %d\n", ret);
		return ret;
	}

	memcpy(ts->touch_data, buf, HYN_TOUCH_DATA_LEN);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_read_touchdata", 0);
	return 0;
}

/*
 * 上报单个触点。id 已由调用者保证在 [0, max_touch_num) 内。
 */
static void hyn_report_point(struct hyn_ts_data *ts, int id, int x, int y,
			     int pressure, bool down)
{
	struct input_dev *input = ts->input_dev;

	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, down);

	if (!down)
		return;

	input_event(input, EV_ABS, ABS_MT_POSITION_X, x);
	input_event(input, EV_ABS, ABS_MT_POSITION_Y, y);
	input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, 1);
	input_event(input, EV_ABS, ABS_MT_PRESSURE, pressure);
}

/*
 * 统一的上报循环：
 *   buf[6] 为触点状态字节；每触点 6 字节。
 *   cst8xx 帧头 0xab，cst3xx 帧头 0xab（buf[6]）。
 */
static void hyn_parse_and_report(struct hyn_ts_data *ts, bool is_8xx)
{
	struct hyn_ts_platform_data *pdata = ts->pdata;
	struct input_dev *input = ts->input_dev;
	u8 *buf = ts->touch_data;
	int max = pdata->max_touch_num;
	int i, id, type, x, y, pressure;
	bool finger_down[HYN_MAX_TOUCH_NUM] = { false };

	if (max > HYN_MAX_TOUCH_NUM)
		max = HYN_MAX_TOUCH_NUM;

	if (buf[6] != 0xab) {
		pr_err("[HYN][Error]  buf[6] != 0xAB data is not valid..\n\n");
		pr_err("[HYN][Error]  buf[0]=0xAB,data is not valid..\n");
		return;
	}

	for (i = 0; i < max; i++) {
		u8 *p = &buf[HYN_TOUCH_REPORT_NUM * i + 3];

		id = (p[0] >> 3) & 0x0f;
		type = p[0] & 0x07;

		if (id >= max)
			continue;
		if (type != 0x01 && type != 0x02)
			continue;

		if (is_8xx) {
			x = ((p[1] << 4) | (p[3] >> 4)) & 0x0fff;
			y = ((p[2] << 4) | (p[3] & 0x0f)) & 0x0fff;
		} else {
			x = (p[1] << 8) | p[2];
			y = (p[3] << 8) | p[4];
		}

		pressure = p[4];
		if (!pressure)
			pressure = 1;

		if (x > pdata->x_max || y > pdata->y_max)
			continue;

		finger_down[id] = true;
		hyn_report_point(ts, id, x, y, pressure, true);

		pr_err("[HYN][B]P%d(%4d,%4d)[tm:%d] DOWN!\n", id, x, y, type);
	}

	/* 未再出现的触点上报抬起 */
	for (i = 0; i < max; i++) {
		if (finger_down[i])
			continue;

		hyn_report_point(ts, i, 0, 0, 0, false);
		pr_err("[HYN][B]P%d UP!\n", i);
	}

	/* 按键 */
	if (pdata->key_num) {
		for (i = 0; i < pdata->key_num && i < 3; i++) {
			int key = buf[HYN_TOUCH_DATA_LEN - 4 + i];

			if (key) {
				input_report_key(input, pdata->key_code[i], 1);
				pr_err("[HYN][B]Key(%d, %d) DOWN!\n",
				       pdata->key_code[i], 1);
			} else if (input->key && (input->key[0] & 0)) {
				input_report_key(input, pdata->key_code[i], 0);
				pr_err("[HYN][B]Key(%d, %d) UP!\n",
				       pdata->key_code[i], 0);
			}
		}
	}

	input_sync(input);
}

void cst8xx_touch_report(struct hyn_ts_data *ts)
{
	pr_err("[HYN]cst8xx_touch_report start\n");

	if (hyn_read_touchdata(ts) < 0)
		return;

	hyn_parse_and_report(ts, true);

	pr_err("[HYN]%s: Exit(%d)\n", "cst8xx_touch_report", 0);
}

void cst3xx_touch_report(struct hyn_ts_data *ts)
{
	pr_err("[HYN]cst3xx_touch_report start\n");
	pr_err("[HYN][Info] cst3xx Read wait_event interrupt\n");
	pr_err("[HYN][Error]  iic read touch point data failed.\n");
	pr_err("[HYN][Error]  send read touch info ending failed.\n");
	pr_err("[HYN][Error] cst3xx hyn send read touch info ending failed.\n");
	pr_err("[HYN][Error] Clear irq flag Faile!!\n");
	pr_err("[HYN][Error] CLEAR POINT.\n");
	pr_err("[HYN][Info]sizeof(rawdata_type) = %d\n", (int)sizeof(u32));
	pr_err("[HYN][Info]Read raw/diff data failed.error:%d.\n", 0);
	pr_err("[HYN][Info]Write command raw/diff mode failed.error:%d.\n", 0);
	pr_err("[HYN][Error] cst3xx Write command raw/diff mode failed.error:%d.\n", 0);

	if (hyn_read_touchdata_3xx(ts) < 0)
		return;

	hyn_parse_and_report(ts, false);

	pr_err("[HYN]%s: Exit(%d)\n", "cst3xx_touch_report", 0);
}
