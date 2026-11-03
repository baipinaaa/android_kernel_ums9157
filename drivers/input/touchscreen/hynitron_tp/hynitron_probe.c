// SPDX-License-Identifier: GPL-2.0
/*
 * Hynitron touchscreen - 平台数据解析、探测与移除。
 *
 * 二进制对照：
 *   hyn_ts_data_init       0x70bc ( 348)
 *   hyn_platform_data_init 0x7218 ( 992，内联 hyn_parse_dt)
 *   hyn_gpio_configure     (内联在 hyn_probe 0x75f8)
 *   hyn_detect             0x7d3c (  32)
 *   hyn_probe              0x75f8 (1860)
 *   hyn_remove             0x0000 (.exit.text, 104)
 *   hyn_pm_suspend/resume  0x7d5c / 0x7d64（均直接返回 0）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/version.h>

#include "hynitron_common.h"

/* DT 属性名（.rodata 实测） */
#define HYN_DT_DISPLAY_COORDS	"hynitron,display-coords"
#define HYN_DT_HAVE_KEY		"hynitron,have-key"
#define HYN_DT_KEY_NUMBER	"hynitron,key-number"
#define HYN_DT_KEY_CODE		"hynitron,key-code"
#define HYN_DT_KEY_X_COORD	"hynitron,key-x-coord"
#define HYN_DT_KEY_Y_COORD	"hynitron,key-y-coord"
#define HYN_DT_RESET_GPIO	"hynitron,reset-gpio"
#define HYN_DT_IRQ_GPIO		"hynitron,irq-gpio"
#define HYN_DT_MAX_TOUCH	"hynitron,max-touch-number"

/*
 * hyn_ts_data_init() —— 用 client 补齐 ts 的芯片类型信息。
 */
int hyn_ts_data_init(struct i2c_client *client)
{
	struct hyn_ts_data *ts = hyn_ts_data;
	struct hyn_ts_platform_data *pdata;
	const struct hyn_chip_type *ct;
	int i;
	u16 type;

	pr_err("[HYN]%s: Enter\n", "hyn_ts_data_init");

	ts->client = client;

	type = ts->chip_ic_type2;

	for (i = 0; i < HYN_CHIP_TYPE_NUM; i++) {
		ct = &hynitron_chip_type_grp[i];
		if (ct->chip_type == type)
			break;
	}

	if (i == HYN_CHIP_TYPE_NUM) {
		pr_err("[HYN][Error]chip type error,please check firmware.\n\n");
		pr_err("[HYN]%s: Exit(%d)\n", "hyn_ts_data_init", 0);
		return -1;
	}

	pr_err("[HYN]config [hyn] scan_id = %d,config_chip_type:0x%x.\n\n",
	       ct->scan_id, ct->config_chip_type);

	ts->chip_ic_type3 = ct->config_chip_type;
	ts->chip_type_idx = ct->config_chip_type;
	ts->scan_id = ct->scan_id;

	if (client->addr != ts->main_addr) {
		pr_err("[HYN]client addr is different,now use default addr,please confirm. client->addr=0x%x, default iic addr=0x%x.\n\n",
		       client->addr, ts->main_addr);
		ts->main_addr = client->addr;
		client->addr = client->addr;
	}

	pdata = ts->pdata;
	if (pdata)
		pr_err("[HYN]config_chip_type:0x%x,main_addr:0x%x,x_overturn=%d,y_overturn=%d,xy_exchange=%d,.\n\n",
		       ts->chip_ic_type2, ts->main_addr,
		       pdata->x_overturn, pdata->y_overturn, pdata->xy_exchange);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_ts_data_init", 0);
	return 0;
}

/*
 * hyn_parse_dt() —— 解析触摸屏 DT 属性（内联在 hyn_platform_data_init 中）。
 */
static int hyn_parse_dt(struct hyn_ts_data *ts)
{
	struct device *dev = ts->dev;
	struct hyn_ts_platform_data *pdata = ts->pdata;
	struct device_node *np = dev->of_node;
	u32 coords[2] = { 0 };
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_parse_dt");

	if (!of_find_property(np, HYN_DT_DISPLAY_COORDS, NULL) ||
	    !of_find_property(np, HYN_DT_DISPLAY_COORDS, NULL)) {
		pr_err("[HYN][Error]DTS Unable to get display-coords\n");
		pr_err("[HYN][Error]device-tree parse fail.\n");
		return -EINVAL;
	}

	ret = of_property_read_variable_u32_array(np, HYN_DT_DISPLAY_COORDS,
						  coords, 2, 2);
	if (ret < 0) {
		pr_err("[HYN][Error]Unable to read %s, please check dts\n",
		       HYN_DT_DISPLAY_COORDS);
		pr_err("[HYN][Error]device-tree parse fail.\n");
		return ret;
	}

	pdata->x_max = coords[0];
	pdata->y_max = coords[1];
	pr_err("[HYN][Info]display x(%d ) y(%d )\n", pdata->x_max, pdata->y_max);

	if (of_find_property(np, HYN_DT_HAVE_KEY, NULL)) {
		u32 key_number = 0;
		u32 key_code[4] = { 0 };
		u32 key_x[4] = { 0 };
		u32 key_y[4] = { 0 };
		int i;

		ret = of_property_read_variable_u32_array(np, HYN_DT_KEY_NUMBER,
							  &key_number, 1, 1);
		if (ret < 0) {
			pr_err("[HYN][Error]Key number undefined!\n");
			return ret;
		}

		if (key_number > 3)
			key_number = 3;

		ret = of_property_read_variable_u32_array(np, HYN_DT_KEY_CODE,
							  key_code, 1, 4);
		if (ret < 0) {
			pr_err("[HYN][Error]Key code undefined!\n");
			return ret;
		}

		ret = of_property_read_variable_u32_array(np, HYN_DT_KEY_X_COORD,
							  key_x, 1, 4);
		if (ret < 0) {
			pr_err("[HYN][Error]Key X Coords undefined!\n");
			return ret;
		}

		ret = of_property_read_variable_u32_array(np, HYN_DT_KEY_Y_COORD,
							  key_y, 1, 4);
		if (ret < 0) {
			pr_err("[HYN][Error]Key Y Coords undefined!\n");
			return ret;
		}

		if (key_number) {
			for (i = 0; i < key_number && i < 3; i++) {
				pdata->key_code[i] = key_code[i];
				pr_err("[HYN]KEY Number:%d, key_x_coords:%d, key_y_coords:%d, key_code:%d.\n\n",
				       key_number, key_x[i], key_y[i], key_code[i]);
			}
			pdata->key_num = key_number;
		}
	}

	ret = of_get_named_gpio_flags(np, HYN_DT_RESET_GPIO, 0, NULL);
	if (ret < 0) {
		pr_err("[HYN][Error]DTS Unable to get reset_gpio\n");
		return ret;
	}
	pdata->reset_gpio = ret;

	ret = of_get_named_gpio_flags(np, HYN_DT_IRQ_GPIO, 0, NULL);
	if (ret < 0) {
		pr_err("[HYN][Error]DTS Unable to get irq_gpio\n");
		return ret;
	}
	pdata->irq_gpio = ret;

	ret = of_property_read_variable_u32_array(np, HYN_DT_MAX_TOUCH,
						  &pdata->max_touch_num, 1, 1);
	if (ret < 0) {
		pr_err("[HYN][Error]DTS Unable to get max-touch-number, please check dts\n");
		return ret;
	}

	if (pdata->max_touch_num < 1 || pdata->max_touch_num > 6)
		pdata->max_touch_num = HYN_MAX_TOUCH_NUM;

	pdata->int_trigger_type = IRQF_TRIGGER_FALLING;

	pr_err("[HYN]DTS max touch number:%d, irq gpio:%d, reset gpio:%d\n",
	       pdata->max_touch_num, pdata->irq_gpio, pdata->reset_gpio);

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_parse_dt", 0);
	return 0;
}

/*
 * hyn_platform_data_init() —— 分配并解析平台数据。
 */
int hyn_platform_data_init(struct hyn_ts_data *ts)
{
	struct hyn_ts_platform_data *pdata;
	int ret;

	pr_err("[HYN]%s: Enter\n", "hyn_platform_data_init");

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("[HYN][Error]allocate memory for platform_data fail\n");
		return -ENOMEM;
	}

	ts->pdata = pdata;

	if (!ts->dev->of_node) {
		pr_err("[HYN][Error]device-tree parse fail.\n");
		return -EINVAL;
	}

	ret = hyn_parse_dt(ts);
	if (ret < 0)
		return ret;

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_platform_data_init", 0);
	return 0;
}

/*
 * hyn_gpio_configure() —— 申请 irq/reset GPIO 并给出复位时序。
 */
int hyn_gpio_configure(struct hyn_ts_data *ts)
{
	struct hyn_ts_platform_data *pdata = ts->pdata;
	int ret;
	int i;

	pr_err("[HYN]%s: Enter\n", "hyn_gpio_configure");

	if (pdata->irq_gpio >= 0 && pdata->irq_gpio <= 0x1ff) {
		ret = gpio_request(pdata->irq_gpio, "hyn_irq_gpio");
		if (ret < 0) {
			pr_err("[HYN][Error][GPIO]irq gpio request failed\n");
			return ret;
		}

		ret = gpiod_direction_input(gpio_to_desc(pdata->irq_gpio));
		if (ret < 0) {
			pr_err("[HYN][Error][GPIO]set_direction for irq gpio failed\n");
			goto free_irq_gpio;
		}
	}

	if (pdata->reset_gpio >= 0 && pdata->reset_gpio <= 0x1ff) {
		ret = gpio_request(pdata->reset_gpio, "hyn_reset_gpio");
		if (ret < 0) {
			pr_err("[HYN][Error][GPIO]reset gpio request failed\n");
			goto free_irq_gpio;
		}

		ret = gpiod_direction_output_raw(gpio_to_desc(pdata->reset_gpio), 1);
		if (ret < 0) {
			pr_err("[HYN][Error][GPIO]set_direction for reset gpio failed\n");
			goto free_reset_gpio;
		}
	}

	pr_err("[HYN]%s: Exit(%d)\n", "hyn_gpio_configure", 0x591);

	/* 复位时序：拉低 -> 20 ms -> 拉高 */
	gpiod_direction_output_raw(gpio_to_desc(pdata->reset_gpio), 0);
	for (i = 0; i < 20; i++)
		__const_udelay(0x418958);
	gpiod_direction_output_raw(gpio_to_desc(pdata->reset_gpio), 1);
	pr_err("[HYN]%s: Exit(%d)\n", "hyn_gpio_configure", 0x586);
	return 0;

free_reset_gpio:
	gpio_free(pdata->reset_gpio);
free_irq_gpio:
	gpio_free(pdata->irq_gpio);
	pr_err("[HYN][Error]hyn_gpio_configure fail\n");
	return -1;
}

int hyn_detect(struct hyn_ts_data *ts)
{
	return hyn_detect_bootloader(ts->client);
}

static int hyn_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct hyn_ts_data *ts;
	struct hyn_ts_platform_data *pdata;
	int ret;
	int i;

	pr_err("[HYN]%s: Enter\n", "hyn_probe");

	pr_err("[HYN]Touch Panel Device Probe %s!\n", dev_name(&client->dev));

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("[HYN][Error]I2C not supported\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts) {
		pr_err("[HYN][Error]allocate memory for ts_data fail\n");
		return -ENOMEM;
	}
	hyn_ts_data = ts;

	ts->client = client;
	ts->dev = &client->dev;
	i2c_set_clientdata(client, ts);

	ret = hyn_platform_data_init(ts);
	if (ret < 0) {
		pr_err("[HYN][Error]hyn_platform_data_init fail,please check DTS.\n");
		goto err_free_ts;
	}

	pdata = ts->pdata;

	ret = hyn_gpio_configure(ts);
	if (ret < 0)
		goto err_free_ts;

	ret = hyn_ts_data_init(client);
	if (ret < 0)
		goto err_free_ts;

	/* 上电后等待芯片启动 */
	for (i = 0; i < 60; i++)
		__const_udelay(0x418958);

	ret = hyn_firmware_info(ts);
	if (ret < 0) {
		pr_err("[HYN][Error]hyn_firmware_info failed,start check boot.\n\n");
		ret = hyn_detect(ts);
		if (ret < 0) {
			pr_err("[HYN][Error]Touch Probe : detect hynitron ic type fail...\n");
			goto err_free_ts;
		}
	}

	for (i = 0; i < 17; i++)
		__const_udelay(0x418958);

	pr_err("[HYN][Info]hyn_firmware_info success.\n\n");

	ret = hyn_input_dev_int(ts);
	if (ret < 0) {
		pr_err("[HYN][Error]Touch Probe : hyn_input_dev_int  fail...
");
		goto err_free_ts;
	}

	ret = hyn_irq_init(ts);
	if (ret < 0) {
		pr_err("[HYN][Error]Touch Probe : hyn_irq_init  fail...
");
		goto err_free_ts;
	}

	if (ts->irq_enabled) {
		disable_irq(ts->irq);
		ts->irq_enabled = 0;
	}

	ret = hyn_update_firmware_init(ts);
	if (ret < 0) {
		pr_err("[HYN][Error]Touch Probe : hyn_update_firmware_init  fail...
");
		goto err_free_irq;
	}

	ret = hynitron_proc_fs_init();
	if (ret < 0) {
		pr_err("[HYN][Error]create hyniyron proc fs node failed.
");
		pr_err("[HYN][Error][create hyniyron proc fs node failed.
");
		goto err_free_irq;
	}

	ret = hyn_create_sysfs(ts);
	if (ret < 0)
		goto err_proc_exit;

	ret = sysfs_create_group(&client->dev.kobj, &tp_attr_group);
	if (ret < 0)
		pr_err("hyn Add device attr failed!
");

	ret = sysfs_create_link(NULL, &client->dev.kobj, "touchscreen");
	if (ret < 0)
		pr_err("hyn Failed to create link!
");

	hyn_reset_proc(0x28);

	if (ts->irq_enabled)
		enable_irq(ts->irq);

	return 0;

err_proc_exit:
	hynitron_proc_fs_exit();
err_free_irq:
	free_irq(ts->irq, ts);
err_free_ts:
	kfree(ts);
	hyn_ts_data = NULL;
	return ret;
}

static int __exit hyn_remove(struct i2c_client *client)
{
	struct hyn_ts_data *ts = i2c_get_clientdata(client);

	if (gExchandle) {
		kthread_stop(gExchandle);
		gExchandle = NULL;
	}

	if (ts) {
		if (ts->irq)
			free_irq(ts->irq, ts);

		sysfs_remove_group(&client->dev.kobj, &hyn_attribute_group);
		hynitron_proc_fs_exit();
		hyn_release_sysfs(ts);
		kfree(ts->pdata);
		kfree(ts);
	}

	i2c_set_clientdata(client, NULL);
	hyn_ts_data = NULL;

	return 0;
}

static int hyn_pm_suspend(struct device *dev)
{
	return 0;
}

static int hyn_pm_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops hyn_ts_pm_ops = {
	.suspend = hyn_pm_suspend,
	.resume = hyn_pm_resume,
};

static const struct i2c_device_id hyn_tpd_id[] = {
	{ "hyn_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hyn_tpd_id);

static const struct of_device_id hyn_dt_match[] = {
	{ .compatible = "hynitron,hyn_ts" },
	{ }
};
MODULE_DEVICE_TABLE(of, hyn_dt_match);

static struct i2c_driver hynitron_i2c_driver = {
	.driver = {
		.name = "hyn_ts",
		.of_match_table = of_match_ptr(hyn_dt_match),
		.pm = &hyn_ts_pm_ops,
	},
	.probe = hyn_probe,
	.remove = __exit_p(hyn_remove),
	.id_table = hyn_tpd_id,
};

static int __init hynitron_driver_init(void)
{
	int ret;

	pr_err("[HYN][Info]Driver version: %s\n", "1.0");
	pr_err("Hynitron TouchScreen Spreadtrum Driver V2.2 20210129
");

	ret = i2c_add_driver(&hynitron_i2c_driver);
	if (ret < 0) {
		pr_err("[HYN][Error]Hynitron touch screen driver init failed!
");
		return ret;
	}

	return 0;
}

static void __exit hynitron_driver_exit(void)
{
	i2c_del_driver(&hynitron_i2c_driver);
}

module_init(hynitron_driver_init);
module_exit(hynitron_driver_exit);

MODULE_DESCRIPTION("Hynitron cst3xx/cst8xx touchscreen driver");
MODULE_LICENSE("GPL v2");
