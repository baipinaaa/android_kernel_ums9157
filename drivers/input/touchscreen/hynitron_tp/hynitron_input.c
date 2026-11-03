// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron touchscreen - input 设备、中断与 workqueue。
 *
 * 二进制对照：
 *   hyn_update_firmware_init  0x5ab8 (1408)
 *   hyn_input_dev_int         0x5c18 (640)
 *   hyn_ts_irq_handler        0x6e60 ( 96)
 *   hyn_irq_init              0x6d10 (1288，内联 hyn_irq_registration)
 *   hyn_work_func             0x6f?
 *   hyn_enter_deep_sleep      0x6ef0 (336)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "hynitron_common.h"

/*
 * hyn_update_firmware_init() —— 重新读一次芯片信息，按 chip_ic_type2 在
 * hynitron_chip_type_grp 里查出上报类型与 scan_id，写入 ts。
 */
int hyn_update_firmware_init(struct hyn_ts_data *ts)
{
	const struct hyn_chip_type *ct;
	int ret;
	int i;

	pr_err("[HYN]%s: Enter\n", "hyn_update_firmware_init");

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

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_firmware_info", 0xb78);

	if (ret < 0) {
		pr_err("[HYN][Error]hyn_firmware_info failed.\n\n");
		return ret;
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_firmware_info", 0xb7e);

	for (i = 0; i < HYN_CHIP_TYPE_NUM; i++) {
		ct = &hynitron_chip_type_grp[i];
		if (ct->chip_type == ts->chip_ic_type2)
			break;
	}

	if (i == HYN_CHIP_TYPE_NUM) {
		pr_err("[HYN][Error]chip type error,please check firmware.\n\n");
		return -1;
	}

	pr_err("[HYN]config [hyn] hyn_ts_data->config_chip_type = %d\n\n",
	       ct->config_chip_type);
	ts->chip_ic_type3 = ct->config_chip_type;
	ts->chip_type_idx = ct->config_chip_type;
	ts->scan_id = ct->scan_id;
	pr_err("[HYN]config [hyn] scan_id = %d\n\n", ts->scan_id);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_update_firmware_init", 0xb96);
	return 0;
}

/*
 * hyn_input_dev_int() —— 申请 input 设备、按平台数据设置坐标范围与按键，
 * 注册设备，创建上报 workqueue。
 */
int hyn_input_dev_int(struct hyn_ts_data *ts)
{
	struct hyn_ts_platform_data *pdata = ts->pdata;
	struct input_dev *input;
	int ret = 0;

	pr_err("[HYN]%s: Enter\n", "hyn_input_dev_int");

	input = input_allocate_device();
	if (!input) {
		pr_err("[HYN][Error]Failed to allocate memory for input device\n");
		return -1;
	}

	input->name = "hyn_ts";
	input->id.version = 1;
	input->dev.parent = ts->dev;
	input->id.bustype = pdata->bustype;
	input_set_drvdata(input, ts);
	input->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	if (pdata->key_num) {
		pr_err("[HYN][Info]set key capabilities\n");
		input_set_capability(input, EV_KEY, pdata->key_code[0]);
		input_set_capability(input, EV_KEY, pdata->key_code[1]);
		input_set_capability(input, EV_KEY, pdata->key_code[2]);
	}

	input_mt_init_slots(input, pdata->max_touch_num, 0);

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 0xff, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 0xff, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 0xff, 0, 0);

	ret = input_register_device(input);
	if (ret < 0) {
		pr_err("[HYN][Error]Input device registration failed\n");
		input_set_drvdata(input, NULL);
		input_free_device(input);
		return ret;
	}

	ts->input_dev = input;

	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST1XX:
		ts->report_fn = cst3xx_touch_report;
		break;
	case HYN_CHIP_CST8XX:
		ts->report_fn = cst8xx_touch_report;
		break;
	default:
		break;
	}

	ts->hyn_wq = alloc_workqueue("hyn_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!ts->hyn_wq) {
		pr_err("[HYN][Error]create hyn workqueue fail\n");
	} else {
		flush_workqueue(ts->hyn_wq);
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_input_dev_int", 0x2d4);
	return 0;
}

/* hyn_irq_registration() —— 内联在 hyn_irq_init 中 */
static int hyn_irq_registration(struct hyn_ts_data *ts)
{
	struct hyn_ts_platform_data *pdata = ts->pdata;
	struct device_node *node;
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_irq_registration");

	node = of_find_matching_node_and_match(NULL, NULL, NULL);
	if (!node)
		pr_err("[HYN][Error]Can not find touch eint device node!\n");

	ts->irq = gpiod_to_irq(gpio_to_desc(pdata->irq_gpio));

	ret = request_threaded_irq(ts->irq, NULL, hyn_ts_irq_handler,
				   pdata->int_trigger_type | IRQF_ONESHOT,
				   "Hynitron Touch Int", ts);
	if (!ret) {
		pr_err("[HYN][Info]IRQ request succussfully, irq=%d ,trigger_rising:%d .\n",
		       ts->irq, pdata->int_trigger_type);
		ts->client->irq = ts->irq;
		pr_err("[HYN]%s: Exit(%d)\n", "hyn_irq_registration", 0x1f0);
		return 0;
	}

	pr_err("[HYN][Error]request_irq IRQ LINE NOT AVAILABLE!.\n");
	return ret;
}

int hyn_irq_init(struct hyn_ts_data *ts)
{
	pr_err("[HYN]%s: Enter\n", "hyn_irq_init");

	ts->irq_flag = 1;
	ts->pdata->int_trigger_type = ts->pdata->int_trigger_type;

	ts->irq = gpiod_to_irq(gpio_to_desc(ts->pdata->irq_gpio));
	pr_err("[HYN][Info]irq in ts_data:%d irq in client:%d\n",
	       ts->irq, ts->client->irq);

	return hyn_irq_registration(ts);
}

irqreturn_t hyn_ts_irq_handler(int irq, void *dev_id)
{
	return hyn_eint_interrupt_handler(irq, dev_id);
}

/*
 * hyn_eint_interrupt_handler() —— 二进制 0x6e64 的原始中断处理体：
 * config_chip_type 为 0 时把工作队列起来，然后唤醒 debug 等待队列。
 */
irqreturn_t hyn_eint_interrupt_handler(int irq, void *dev_id)
{
	struct hyn_ts_data *ts = dev_id;

	if (!ts)
		return IRQ_HANDLED;

	if (!ts->config_chip_type) {
		if (ts->hyn_wq)
			queue_work(ts->hyn_wq, &ts->work);
	}

	hynitron_wake_up_wait();

	return IRQ_HANDLED;
}

void hyn_work_func(struct work_struct *work)
{
	struct hyn_ts_data *ts = container_of(work, struct hyn_ts_data, work);

	if (ts->report_fn)
		ts->report_fn(ts);
}

/* hyn_enter_deep_sleep() —— 按芯片类型下发休眠命令 */
void hyn_enter_deep_sleep(struct hyn_ts_data *ts)
{
	u8 buf[4] = { 0 };
	int ret;

	pr_err("[HYN][Error]hyn_enter_deep_sleep\n");

	switch (ts->chip_type_idx) {
	case HYN_CHIP_CST8XX:
		buf[0] = 0xd1;
		buf[1] = 0x0a;
		break;
	case HYN_CHIP_CST1XX:
		buf[0] = 0xd1;
		buf[1] = 0x19;
		break;
	default:
		break;
	}

	if (!ts->client)
		return;

	ret = i2c_transfer_buffer_flags(ts->client, buf, 2, 0);
	if (ret < 0)
		pr_err("[HYN][Error]Set TP to sleep mode selfcap\n");
		pr_err("[HYN][Error][HYN]hyn_enter_deep_sleep read value fail\n");
		pr_err("[HYN][Error]clear irq flag Faile!!\n");
}
