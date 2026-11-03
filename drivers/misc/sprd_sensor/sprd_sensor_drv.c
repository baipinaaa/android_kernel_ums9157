// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) camera sensor driver - i2c sensor glue.
 *
 * Reconstructed from sprd_sensor.ko (sprd_sensor_drv.c of the vendor tree).
 * This file holds the per sensor power / clock / gpio control, the i2c
 * register access helpers and the six i2c_driver instances.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "sprd_sensor.h"

/* 13 entry voltage table, uV; index >= 13 selects idx * 1000 uV */
const u32 c_sensor_voltage_tab[13] = {
	3800000, 3300000, 3000000, 2800000, 2500000, 2000000,
	1800000, 1500000, 1300000, 1200000, 1100000, 1000000, 0,
};

/* phy_id - 2 -> dcam_id mapping table */
const u32 c_sensor_dcam_id_tab[6] = { 2, 4, 2, 3, 4, 5 };

const char *const sprd_sensor_supply_names[SENSOR_REGULATOR_NUM] = {
	"vddio", "vddcama", "vddcamd", "vddcammot",
};

const char *const sprd_sensor_gpio_names[SENSOR_GPIO_NUM] = {
	"reset-gpios",
	"power-down-gpios",
	"flash-gpios",
	"mipi-switch-en-gpios",
	"mipi-switch-mode-gpios",
	"switch-mode-gpios",
	"cam-id-gpios",
	"iovdd-gpios",
	"avdd-gpios",
	"dvdd-gpios",
	"mot-gpios",
};

const struct sensor_mclk_tab c_sensor_mclk_tab[SENSOR_MCLK_TAB_NUM] = {
	{ 96, "clk_96m" },
	{ 64, "clk_64m" },
	{ 51, "clk_51m2" },
	{ 48, "clk_48m" },
	{ 26, "clk_26m" },
};

/* ------------------------------------------------------------------ */
/* power / gpio                                                        */
/* ------------------------------------------------------------------ */

int sprd_sensor_set_voltage_by_gpio(u32 sensor_id, int val, u32 gpio_idx)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	int gpio, level, ret;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	if (gpio_idx >= SENSOR_GPIO_NUM)
		return -EINVAL;

	gpio = p->gpio[gpio_idx];
	if (gpio > 0x1ff || gpio < 0)
		return -EINVAL;

	level = !!val;
	ret = gpiod_direction_output_raw(gpio_to_desc(gpio), level);
	if (ret)
		return ret;

	gpiod_set_raw_value(gpio_to_desc(gpio), level);

	pr_debug("SENSOR_DRV: %d: %s L %d: sensor %d vdd val %d\n",
		 current->pid, __func__, __LINE__, sensor_id, val);

	return 0;
}

int sprd_sensor_set_voltage(u32 sensor_id, int vol, u32 id)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	struct regulator *reg;
	int step, ret = 0;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	mutex_lock(&p->set_voltage_lock);

	if (id >= SENSOR_REGULATOR_NUM) {
		pr_err("SENSOR_DRV: %d: %s L %d: p_dev %d invalid sensor_id%d\n",
		       current->pid, __func__, __LINE__, id, sensor_id);
		pr_err("p_dev %d invalid sensor_id%d\n", id, sensor_id);
		pr_debug("SENSOR_DRV: %d: %s L %d: regulator %d invalid sensor_id%d\n",
			 current->pid, __func__, __LINE__, id, sensor_id);
		mutex_unlock(&p->set_voltage_lock);
		return -EINVAL;
	}

	reg = p->regulator[id];
	if (!reg) {
		reg = devm_regulator_get(&p->client->dev,
					 sprd_sensor_supply_names[id]);
		p->regulator[id] = reg;
		if (!reg) {
			mutex_unlock(&p->set_voltage_lock);
			return -EINVAL;
		}
	}

	if (vol) {
		step = regulator_get_linear_step(reg) / 2;

		ret = regulator_set_voltage(reg, vol - step, vol + step);
		if (ret) {
			pr_err("SENSOR_DRV: %d: %s L %d: regulator %s vol set %d fail ret%d\n",
			       current->pid, __func__, __LINE__,
			       sprd_sensor_supply_names[id], vol, ret);
			goto unlock;
		}

		ret = sprd_sensor_regulator_enable(reg);
		if (ret) {
			pr_err("SENSOR_DRV: %d: %s L %d: Error in regulator_enable: err %d",
			       current->pid, __func__, __LINE__, ret);
			devm_regulator_put(reg);
			p->regulator[id] = NULL;
			pr_err("SENSOR_DRV: %d: %s L %d: regulator %s enable fail ret%d\n",
			       current->pid, __func__, __LINE__,
			       sprd_sensor_supply_names[id], ret);
			goto unlock;
		}

		p->regulator_count[id]++;
		ret = 0;
		goto unlock;
	}

	while (p->regulator_count[id]) {
		ret = sprd_sensor_regulator_disable(reg);
		if (ret) {
			pr_err("SENSOR_DRV: %d: %s L %d: Error in regulator_disable: err %d",
			       current->pid, __func__, __LINE__, ret);
			break;
		}
		if (!(--p->regulator_count[id]))
			break;
	}

	if (ret)
		pr_err("SENSOR_DRV: %d: %s L %d: regulator disable fail ret%d\n",
		       current->pid, __func__, __LINE__, ret);

	devm_regulator_put(reg);
	p->regulator[id] = NULL;
	ret = 0;

unlock:
	mutex_unlock(&p->set_voltage_lock);

	return ret;
}

int sprd_sensor_set_pd_level(u32 sensor_id, int level)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	int gpio, ret;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	gpio = p->gpio[SENSOR_GPIO_POWER_DOWN];
	if (gpio > 0x1ff || gpio < 0)
		return 0;

	ret = gpiod_direction_output_raw(gpio_to_desc(gpio), !!level);

	pr_debug("SENSOR_DRV: %d: %s L %d: sensor %d set pd level %d\n",
		 current->pid, __func__, __LINE__, sensor_id, level);

	return ret;
}

int sprd_sensor_reset(u32 sensor_id, int level, u32 width)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	int gpio, ret;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	gpio = p->gpio[SENSOR_GPIO_RESET];
	if (gpio > 0x1ff || gpio < 0)
		return 0;

	ret = gpiod_direction_output_raw(gpio_to_desc(gpio), level);
	gpiod_set_raw_value(gpio_to_desc(gpio), level);

	while (width--)
		udelay(1000);

	gpiod_set_raw_value(gpio_to_desc(gpio), level == 0);

	pr_debug("SENSOR_DRV: %d: %s L %d: sensor %d reset%d width %d\n",
		 current->pid, __func__, __LINE__, sensor_id, level, width);

	return ret;
}

int sprd_sensor_set_rst_level(u32 sensor_id, int level)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	int gpio, ret;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	gpio = p->gpio[SENSOR_GPIO_RESET];
	if (gpio > 0x1ff || gpio < 0)
		return 0;

	ret = gpiod_direction_output_raw(gpio_to_desc(gpio), level);
	gpiod_set_raw_value(gpio_to_desc(gpio), level);

	pr_debug("SENSOR_DRV: %d: %s L %d: sensor %d rst level %d\n",
		 current->pid, __func__, __LINE__, sensor_id, level);

	return ret;
}

int sprd_sensor_set_mipi_level(u32 sensor_id, int level)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	int en_gpio, mode_gpio, ret;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	en_gpio = p->gpio[SENSOR_GPIO_MIPI_SWITCH_EN];
	if (en_gpio > 0x1ff || en_gpio < 0)
		return 0;

	mode_gpio = p->gpio[SENSOR_GPIO_MIPI_SWITCH_MODE];

	gpiod_direction_output_raw(gpio_to_desc(en_gpio), 0);
	gpiod_set_raw_value(gpio_to_desc(en_gpio), 0);

	ret = gpiod_direction_output_raw(gpio_to_desc(mode_gpio), level);
	gpiod_set_raw_value(gpio_to_desc(mode_gpio), level);

	pr_info("SENSOR_DRV: %d: %s L %d: %s mipi_switch_en %d mipi_gpio_id %d\n",
		current->pid, __func__, __LINE__, __func__, en_gpio, mode_gpio);

	return ret;
}

int sprd_sensor_set_i2c_addr(u32 sensor_id, u16 addr)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);

	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	p->client->addr = addr;

	return 0;
}

int sprd_sensor_set_i2c_clk(u32 sensor_id, u32 clk)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	p->i2c_clk = clk;

	pr_debug("SENSOR_DRV: %d: %s L %d: set i2c %d clk %d\n", current->pid,
		 __func__, __LINE__, sensor_id, clk);

	return 0;
}

int sprd_sensor_set_i2c_burst(u32 sensor_id, u32 mode)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	p->i2c_burst_mode = mode;

	pr_debug("SENSOR_DRV: %d: %s L %d: set sensor_id:%d, i2c_burst_mode:%d\n",
		 current->pid, __func__, __LINE__, sensor_id, mode);

	return 0;
}

/* ------------------------------------------------------------------ */
/* mclk                                                                */
/* ------------------------------------------------------------------ */

static int select_sensor_mclk(u32 mclk, char **clk_src_name, u8 *clk_div)
{
	int div, i, src, best, best_diff = 0x7fffffff, sel_div = 0, sel_src = 0;
	int v;
	u32 f = mclk & 0xff;

	if (f > 0x60)
		return -1;
	if (!clk_src_name || !clk_div)
		return -1;

	div = 0x30;
	for (i = 0; i < 8; i++) {
		div -= f;
		best = 0x7fffffff;
		src = 0;

		v = abs(div + 0x30);
		if (v < best) {
			best = v;
			src = 0;
		}
		v = abs(div + 0x10);
		if (v < best) {
			best = v;
			src = 1;
		}
		v = abs(div + 3);
		if (v < best) {
			best = v;
			src = 2;
		}
		v = abs(div);
		if (v < best) {
			best = v;
			src = 3;
		}
		v = abs(div - 0x16);
		if (v < best) {
			best = v;
			src = 4;
		}

		if (best < best_diff) {
			best_diff = best;
			sel_div = i;
			sel_src = src;
		}
	}

	if (sel_src >= SENSOR_MCLK_TAB_NUM)
		return -1;

	*clk_src_name = (char *)c_sensor_mclk_tab[sel_src].name;
	*clk_div = sel_div + 1;

	pr_debug("SENSOR_DRV: %d: %s L %d: src %d, div=%d .\n", current->pid,
		 __func__, __LINE__, sel_src, sel_div);

	return 0;
}

int sprd_sensor_set_mclk(int mclk, u32 sensor_id)
{
	struct sensor_dev_data *p = sprd_sensor_get_dev_context(sensor_id);
	char *src_name = NULL;
	struct clk *clk;
	u8 div = 0;
	int ret = 0;

	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	pr_info("SENSOR_DRV: %d: %s L %d: set_mclk %d sensor_id %d\n",
		current->pid, __func__, __LINE__, mclk, sensor_id);

	if (!mclk) {
		if (!p->mclk_ref_cnt)
			goto out_zero;
		if (!p->cur_mclk)
			goto out_zero;

		if (p->sensor_clk) {
			clk_set_parent(p->sensor_clk, p->clk_parent);
			clk_set_rate(p->sensor_clk, p->clk_rate);
			clk_disable_unprepare(p->sensor_clk);
		}

		clk = p->sensor_eb ? p->sensor_eb : p->sensor_clk2;
		if (clk) {
			clk_disable(clk);
			clk_unprepare(clk);
		}

		p->mclk_ref_cnt--;
		p->cur_mclk = 0;

out_zero:
		pr_debug("SENSOR_DRV: %d: %s L %d: sensor mclk %d ret= %d\n",
			 current->pid, __func__, __LINE__, mclk, ret);
		return 0;
	}

	if (!p->mclk_ref_cnt) {
		if (mclk > 0x60)
			mclk = 0x60;

		if (p->sensor_eb && p->sensor_clk) {
			ret = select_sensor_mclk(mclk, &src_name, &div);
			if (ret) {
				pr_err("SENSOR_DRV: %d: %s L %d: select sensor mclk error\n",
				       current->pid, __func__, __LINE__);
				return ret;
			}

			clk = of_clk_get_by_name(p->of_node, src_name);
			if (!clk) {
				pr_err("SENSOR_DRV: %d: %s L %d: sensor clock no device %d\n",
				       current->pid, __func__, __LINE__,
				       sensor_id);
				return -EINVAL;
			}

			pr_debug("SENSOR_DRV: %d: %s L %d: clk_src_name %s\n",
				 current->pid, __func__, __LINE__, src_name);

			clk_prepare_enable(p->sensor_eb);

			ret = clk_set_parent(p->sensor_clk, clk);
			if (ret) {
				pr_err("SENSOR_DRV: %d: %s L %d: set parent failed\n",
				       current->pid, __func__, __LINE__);
				return ret;
			}

			ret = clk_set_rate(p->sensor_clk, mclk * 1000000);
			if (ret) {
				pr_err("SENSOR_DRV: %d: %s L %d: set rate failed\n",
				       current->pid, __func__, __LINE__);
				return ret;
			}
		} else {
			if (p->sensor_clk2 && p->sensor_clk) {
				if (!clk_prepare(p->sensor_clk2)) {
					if (clk_enable(p->sensor_clk2))
						clk_unprepare(p->sensor_clk2);
				}
			}

			if (!clk_prepare(p->sensor_clk)) {
				if (clk_enable(p->sensor_clk))
					clk_unprepare(p->sensor_clk);
			}
		}

		p->mclk_ref_cnt++;
		p->cur_mclk = mclk;

		return 0;
	}

	if (p->cur_mclk == mclk)
		goto out;

	ret = select_sensor_mclk(mclk, &src_name, &div);
	if (ret) {
		pr_err("SENSOR_DRV: %d: %s L %d: select sensor mclk error\n",
		       current->pid, __func__, __LINE__);
		return ret;
	}

	clk = of_clk_get_by_name(p->of_node, src_name);
	if (!clk) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor clock no device %d\n",
		       current->pid, __func__, __LINE__, sensor_id);
		return -EINVAL;
	}

	pr_debug("SENSOR_DRV: %d: %s L %d: clk_src_name %s\n", current->pid,
		 __func__, __LINE__, src_name);

	ret = clk_set_parent(p->sensor_clk, clk);
	if (ret) {
		pr_err("SENSOR_DRV: %d: %s L %d: set_parent failed\n",
		       current->pid, __func__, __LINE__);
		pr_err("SENSOR_DRV: %d: %s L %d: set parent failed\n",
		       current->pid, __func__, __LINE__);
		return ret;
	}

	ret = clk_set_rate(p->sensor_clk, mclk * 1000000);
	if (ret) {
		pr_err("SENSOR_DRV: %d: %s L %d: set rate failed\n", current->pid,
		       __func__, __LINE__);
		return ret;
	}

out:
	p->cur_mclk = mclk;
	pr_debug("SENSOR_DRV: %d: %s L %d: set rate mclk count %d \n",
		 current->pid, __func__, __LINE__, p->mclk_ref_cnt);


	pr_debug("SENSOR_DRV: %d: %s L %d: sensor mclk %d ret= %d\n", current->pid,
		 __func__, __LINE__, mclk, ret);

	return 0;
}

/* ------------------------------------------------------------------ */
/* i2c driver instances                                                */
/* ------------------------------------------------------------------ */

static int sprd_sensor_probe(struct i2c_client *client,
			     const struct i2c_device_id *id);
static int sprd_sensor_remove(struct i2c_client *client);

static const struct of_device_id sprd_main_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-main" },
	{ },
};

static const struct of_device_id sprd_sub_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-sub" },
	{ },
};

static const struct of_device_id sprd_main2_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-main2" },
	{ },
};

static const struct of_device_id sprd_sub2_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-sub2" },
	{ },
};

static const struct of_device_id sprd_main3_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-main3" },
	{ },
};

static const struct of_device_id sprd_sub3_sensor_of_match[] = {
	{ .compatible = "sprd,sensor-sub3" },
	{ },
};

static const struct i2c_device_id sprd_main_sensor_id[] = {
	{ },
};

static const struct i2c_device_id sprd_sub_sensor_id[] = {
	{ },
};

static const struct i2c_device_id sprd_main2_sensor_id[] = {
	{ },
};

static const struct i2c_device_id sprd_sub2_sensor_id[] = {
	{ },
};

static const struct i2c_device_id sprd_main3_sensor_id[] = {
	{ },
};

static const struct i2c_device_id sprd_sub3_sensor_id[] = {
	{ },
};

static struct i2c_driver sprd_main_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_main",
		.of_match_table = of_match_ptr(sprd_main_sensor_of_match),
	},
	.id_table = sprd_main_sensor_id,
};

static struct i2c_driver sprd_sub_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_sub",
		.of_match_table = of_match_ptr(sprd_sub_sensor_of_match),
	},
	.id_table = sprd_sub_sensor_id,
};

static struct i2c_driver sprd_main2_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_main2",
		.of_match_table = of_match_ptr(sprd_main2_sensor_of_match),
	},
	.id_table = sprd_main2_sensor_id,
};

static struct i2c_driver sprd_sub2_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_sub2",
		.of_match_table = of_match_ptr(sprd_sub2_sensor_of_match),
	},
	.id_table = sprd_sub2_sensor_id,
};

static struct i2c_driver sprd_main3_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_main3",
		.of_match_table = of_match_ptr(sprd_main3_sensor_of_match),
	},
	.id_table = sprd_main3_sensor_id,
};

static struct i2c_driver sprd_sub3_sensor_driver = {
	.probe = sprd_sensor_probe,
	.remove = sprd_sensor_remove,
	.driver = {
		.name = "sensor_sub3",
		.of_match_table = of_match_ptr(sprd_sub3_sensor_of_match),
	},
	.id_table = sprd_sub3_sensor_id,
};
/* ------------------------------------------------------------------ */
/* i2c register access                                                 */
/* ------------------------------------------------------------------ */

int sprd_sensor_read_reg(u32 sensor_id, struct sensor_i2c_reg *reg)
{
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	u8 data[2] = { 0 };
	u8 addr_buf[4] = { 0 };
	u16 addr = reg->addr;
	u32 setting = reg->setting;
	u32 addr_len, data_len;
	int ret = -1;

	if (sensor_id >= SENSOR_MAX_NUM)
		pr_err("SENSOR_DRV: %d: %s L %d: sensor_id %d error!\n",
		       current->pid, "sprd_sensor_get_dev_context", 0x134,
		       sensor_id);

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	client = p->client;

	if (setting & SENSOR_I2C_SETTING_ADDR16) {
		*(u16 *)addr_buf = swab16(addr);
		addr_len = 2;
	} else {
		addr_buf[0] = addr & 0xff;
		addr_len = 1;
	}

	data_len = (setting & SENSOR_I2C_SETTING_DATA16) ? 2 : 1;

	msg[0].addr = client->addr;
	msg[0].flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
	msg[0].len = addr_len;
	msg[0].buf = addr_buf;

	msg[1].addr = client->addr;
	msg[1].flags = (p->i2c_clk == 1000000) ? 0x81 : 0x41;
	msg[1].len = data_len;
	msg[1].buf = data;

	if (i2c_transfer(client->adapter, msg, 2) == 2) {
		if (setting & SENSOR_I2C_SETTING_DATA16)
			reg->val = (data[0] << 8) | data[1];
		else
			reg->val = data[0];
		return 0;
	}

	pr_err("SENSOR_DRV: %d: %s L %d: %s fail, ret %d, addr 0x%x, reg_addr 0x%x\n",
	       current->pid, __func__, __LINE__, __func__, -1, client->addr,
	       addr);
	usleep_range(1000, 1500);

	if (i2c_transfer(client->adapter, msg, 2) == 2) {
		if (setting & SENSOR_I2C_SETTING_DATA16)
			reg->val = (data[0] << 8) | data[1];
		else
			reg->val = data[0];
		return 0;
	}

	pr_err("SENSOR_DRV: %d: %s L %d: %s fail, ret %d, addr 0x%x, reg_addr 0x%x\n",
	       current->pid, __func__, __LINE__, __func__, -1, client->addr,
	       addr);
	usleep_range(1000, 1500);

	if (i2c_transfer(client->adapter, msg, 2) == 2) {
		if (setting & SENSOR_I2C_SETTING_DATA16)
			reg->val = (data[0] << 8) | data[1];
		else
			reg->val = data[0];
		return 0;
	}

	pr_err("SENSOR_DRV: %d: %s L %d: SENSOR:read reg fail, ret %d, addr 0x%x\n",
	       current->pid, __func__, __LINE__, -1, client->addr);
	pr_err("SENSOR_DRV: %d: %s L %d: %s fail, ret %d, addr 0x%x, reg_addr 0x%x\n",
	       current->pid, __func__, __LINE__, __func__, -1, client->addr,
	       addr);

	return ret;
}

int sprd_sensor_write_reg(u32 sensor_id, struct sensor_i2c_reg *reg)
{
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 buf[4];
	u16 addr = reg->addr;
	u16 val = reg->val;
	u32 setting = reg->setting;
	u32 len = 0;
	int i;

	if (sensor_id >= SENSOR_MAX_NUM)
		pr_err("SENSOR_DRV: %d: %s L %d: sensor_id %d error!\n",
		       current->pid, "sprd_sensor_get_dev_context", 0x134,
		       sensor_id);

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	client = p->client;

	if (setting & SENSOR_I2C_SETTING_ADDR16) {
		*(u16 *)buf = swab16(addr);
		len = 2;
	} else {
		buf[len++] = addr & 0xff;
	}

	if (setting & SENSOR_I2C_SETTING_DATA16)
		buf[len++] = (val >> 8) & 0xff;
	buf[len++] = val & 0xff;

	if (addr == SENSOR_I2C_REG_DELAY) {
		if (val >= 0x14) {
			msleep(val);
			return 0;
		}
		while (val--)
			udelay(1000);
		return 0;
	}

	for (i = 0; i < 3; i++) {
		msg.addr = client->addr;
		msg.flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
		msg.len = len;
		msg.buf = buf;

		if (i2c_transfer(client->adapter, &msg, 1) == 1)
			return 0;

		pr_err("SENSOR_DRV: %d: %s L %d: %s fail to:\ni2cAddr=%x,\naddr=%x, value=%x, bit=%d\n",
		       current->pid, __func__, __LINE__, __func__, client->addr,
		       addr, val, setting);
	}

	return -1;
}

static int sprd_sensor_burst_write_samsung(u16 *tab, u32 sensor_id, u32 count,
					   u32 setting)
{
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct sensor_i2c_reg reg;
	u8 *buf;
	u32 i = 0;

	if (sensor_id >= SENSOR_MAX_NUM)
		pr_err("SENSOR_DRV: %d: %s L %d: sensor_id %d error!\n",
		       current->pid, "sprd_sensor_get_dev_context", 0x134,
		       sensor_id);

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	client = p->client;

	buf = kmalloc(count * 4, GFP_KERNEL);
	if (!buf) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -ENOMEM;
	}

	if (!count) {
		kfree(buf);
		return 0;
	}

	while (i < count) {
		u16 addr = tab[i * 2];
		u16 val = tab[i * 2 + 1];

		if (addr != 0x6004) {
			reg.addr = addr;
			reg.val = val;
			reg.setting = setting;

			if (sprd_sensor_write_reg(sensor_id, &reg)) {
				pr_err("SENSOR_DRV: %d: %s L %d: sprd_sensor_burst_write_samsung failed!\n",
				       current->pid, __func__, __LINE__);
				kfree(buf);
				return -EINVAL;
			}
			i++;
			continue;
		}

		/* the samsung 0x6004 group: write it and the two next
		 * registers one by one, the remaining ones are bursted
		 */
		reg.addr = addr;
		reg.val = val;
		reg.setting = setting;
		if (sprd_sensor_write_reg(sensor_id, &reg)) {
			pr_err("SENSOR_DRV: %d: %s L %d: sprd_sensor_burst_write_samsung failed!\n",
			       current->pid, __func__, __LINE__);
			kfree(buf);
			return -EINVAL;
		}
		i++;

		while (i < count) {
			struct i2c_msg msg;
			u32 len = 0;
			int j;

			addr = tab[i * 2];
			val = tab[i * 2 + 1];

			if (len == 0) {
				buf[len++] = addr >> 8;
				buf[len++] = addr & 0xff;
				buf[len++] = (val >> 8) & 0xff;
				buf[len++] = val & 0xff;
			} else {
				buf[len++] = (val >> 8) & 0xff;
				buf[len++] = val & 0xff;
			}
			i++;

			if (i < count && tab[i * 2] == 0x6004)
				break;

			if (len + 4 > count * 4)
				break;
		}

		msg.addr = client->addr;
		msg.flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
		msg.len = len;
		msg.buf = buf;

		for (j = 0; j < 3; j++) {
			if (i2c_transfer(client->adapter, &msg, 1) == 1)
				break;
		}

		if (j == 3) {
			pr_err("SENSOR_DRV: %d: %s L %d: sprd_sensor_burst_write_samsung failed!\n",
			       current->pid, __func__, __LINE__);
			kfree(buf);
			return -EINVAL;
		}
	}

	kfree(buf);

	return 0;
}

static int sprd_sensor_burst_write_reg16_val8(u16 *tab, u32 sensor_id,
					      u32 count)
{
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 *buf;
	u32 i = 0;
	int ret = 0;

	if (sensor_id >= SENSOR_MAX_NUM)
		pr_err("SENSOR_DRV: %d: %s L %d: sensor_id %d error!\n",
		       current->pid, "sprd_sensor_get_dev_context", 0x134,
		       sensor_id);

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	client = p->client;

	buf = kmalloc(count * 3 + 4, GFP_KERNEL);
	if (!buf) {
		pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: wr reg tab alloc fail %d\n",
		       current->pid, __func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	while (i < count) {
		u16 addr = tab[i * 2];
		u32 len = 0;
		int j;

		buf[len++] = addr >> 8;
		buf[len++] = addr & 0xff;
		buf[len++] = tab[i * 2 + 1] & 0xff;
		i++;

		while (i < count && tab[i * 2] == addr + 1) {
			addr = tab[i * 2];
			buf[len++] = tab[i * 2 + 1] & 0xff;
			i++;
		}

		msg.addr = client->addr;
		msg.flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
		msg.len = len;
		msg.buf = buf;

		for (j = 0; j < 3; j++) {
			if (i2c_transfer(client->adapter, &msg, 1) == 1)
				break;
		}

		if (j == 3) {
			pr_err("SENSOR_DRV: %d: %s L %d: sprd_sensor_burst_write_common failed!\n",
			       current->pid, __func__, __LINE__);
			ret = -EINVAL;
			break;
		}
	}

	kfree(buf);

	return ret;
}

static int sprd_sensor_burst_write_reg16_val16(u16 *tab, u32 sensor_id,
					       u32 count)
{
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 *buf;
	u32 i = 0;
	int ret = 0;

	if (sensor_id >= SENSOR_MAX_NUM)
		pr_err("SENSOR_DRV: %d: %s L %d: sensor_id %d error!\n",
		       current->pid, "sprd_sensor_get_dev_context", 0x134,
		       sensor_id);

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p || !p->client) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	client = p->client;

	buf = kmalloc(count * 4 + 4, GFP_KERNEL);
	if (!buf) {
		pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: wr reg tab alloc fail %d\n",
		       current->pid, __func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	while (i < count) {
		u16 addr = tab[i * 2];
		u32 len = 0;
		int j;

		buf[len++] = addr >> 8;
		buf[len++] = addr & 0xff;
		buf[len++] = (tab[i * 2 + 1] >> 8) & 0xff;
		buf[len++] = tab[i * 2 + 1] & 0xff;
		i++;

		while (i < count && tab[i * 2] == addr + 2) {
			addr = tab[i * 2];
			buf[len++] = (tab[i * 2 + 1] >> 8) & 0xff;
			buf[len++] = tab[i * 2 + 1] & 0xff;
			i++;
		}

		msg.addr = client->addr;
		msg.flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
		msg.len = len;
		msg.buf = buf;

		for (j = 0; j < 3; j++) {
			if (i2c_transfer(client->adapter, &msg, 1) == 1)
				break;
		}

		if (j == 3) {
			pr_err("SENSOR_DRV: %d: %s L %d: sprd_sensor_burst_write_common failed!\n",
			       current->pid, __func__, __LINE__);
			ret = -EINVAL;
			break;
		}
	}

	kfree(buf);

	return ret;
}
/* write a table of {u16 addr, u16 val} pairs taken from user space */
static int sprd_sensor_write_regtab(struct sensor_reg_tab *tab, u32 sensor_id)
{
	struct sensor_i2c_reg reg;
	u32 count = tab->reg_count;
	u32 size = count * sizeof(u16) * 2;
	u16 *buf;
	u32 i;
	int ret = 0;

	if (count && size / count != sizeof(u16) * 2) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor w err: interger overflow occurs\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: wr reg tab alloc fail %d\n",
		       current->pid, __func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	if (copy_from_user(buf, (void __user *)tab->reg_tab, size)) {
		kfree(buf);
		return -EFAULT;
	}

	switch (tab->burst_mode) {
	case SENSOR_I2C_BURST_SAMSUNG:
		ret = sprd_sensor_burst_write_samsung(buf, sensor_id, count,
						      tab->setting);
		break;
	case SENSOR_I2C_BURST_REG16_VAL8:
		ret = sprd_sensor_burst_write_reg16_val8(buf, sensor_id, count);
		break;
	case SENSOR_I2C_BURST_REG16_VAL16:
		ret = sprd_sensor_burst_write_reg16_val16(buf, sensor_id, count);
		break;
	default:
		if (tab->burst_mode > SENSOR_I2C_BURST_REG16_VAL16)
			pr_err("SENSOR_DRV: %d: %s L %d: invalid burst mode, turn to single write mode\n",
			       current->pid, __func__, __LINE__);

		for (i = 0; i < count; i++) {
			reg.addr = buf[i * 2];
			reg.val = buf[i * 2 + 1];
			reg.setting = tab->setting;

			if (sprd_sensor_write_reg(sensor_id, &reg)) {
				pr_err("SENSOR_DRV: %d: %s L %d: SENSOR WRITE REG TAB write reg fail\n",
				       current->pid, __func__, __LINE__);
				ret = -EINVAL;
				break;
			}
		}
		break;
	}

	kfree(buf);

	return ret;
}

static int sprd_sensor_k_write_regtab(struct sensor_reg_tab *tab, u32 sensor_id)
{
	struct sensor_i2c_reg reg;
	u32 count = tab->reg_count;
	u32 size = count * sizeof(u16) * 2;
	u16 *buf;
	u32 i;
	int ret = 0;

	if (count && size / count != sizeof(u16) * 2) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor w err: interger overflow occurs\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: wr reg tab alloc fail %d\n",
		       current->pid, __func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	memcpy(buf, (void *)tab->reg_tab, size);

	switch (tab->burst_mode) {
	case SENSOR_I2C_BURST_SAMSUNG:
		ret = sprd_sensor_burst_write_samsung(buf, sensor_id, count,
						      tab->setting);
		break;
	case SENSOR_I2C_BURST_REG16_VAL8:
		ret = sprd_sensor_burst_write_reg16_val8(buf, sensor_id, count);
		break;
	case SENSOR_I2C_BURST_REG16_VAL16:
		ret = sprd_sensor_burst_write_reg16_val16(buf, sensor_id, count);
		break;
	default:
		if (tab->burst_mode > SENSOR_I2C_BURST_REG16_VAL16)
			pr_err("SENSOR_DRV: %d: %s L %d: invalid burst mode, turn to single write mode\n",
			       current->pid, __func__, __LINE__);

		for (i = 0; i < count; i++) {
			reg.addr = buf[i * 2];
			reg.val = buf[i * 2 + 1];
			reg.setting = tab->setting;

			if (sprd_sensor_write_reg(sensor_id, &reg)) {
				pr_err("SENSOR_DRV: %d: %s L %d: SENSOR WRITE REG TAB write reg fail\n",
				       current->pid, __func__, __LINE__);
				ret = -EINVAL;
				break;
			}
		}
		break;
	}

	kfree(buf);

	return ret;
}

int sprd_sensor_write_muti_i2c(void *data)
{
	struct sensor_muti_i2c_tab *tab = data;
	struct sensor_dev_data *p;
	u16 sensor_id[3] = { 0 };
	u16 slave_addr[3] = { 0 };
	u16 addr_bits[3] = { 0 };
	u16 data_bits[3] = { 0 };
	u16 m_tab[SENSOR_I2C_BURST_MAX * 2];
	u16 s_tab[SENSOR_I2C_BURST_MAX * 2];
	u16 s2_tab[SENSOR_I2C_BURST_MAX * 2];
	struct sensor_i2c_reg reg;
	u32 i;
	int ret;

	memset(m_tab, 0, sizeof(m_tab));
	memset(s_tab, 0, sizeof(s_tab));
	memset(s2_tab, 0, sizeof(s2_tab));

	if (copy_from_user(sensor_id,
			   (void __user *)(unsigned long)tab->sensor_id_tab,
			   sizeof(sensor_id))) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read sensor id\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(slave_addr,
			   (void __user *)(unsigned long)tab->slave_addr_tab,
			   sizeof(slave_addr))) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read slave addr\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(addr_bits,
			   (void __user *)(unsigned long)tab->addr_bits_tab,
			   sizeof(addr_bits))) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read addr bits\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(data_bits,
			   (void __user *)(unsigned long)tab->data_bits_tab,
			   sizeof(data_bits))) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read data bits\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(m_tab,
			   (void __user *)(unsigned long)tab->m_tab,
			   tab->m_count * sizeof(u16) * 2)) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read msetting\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(s_tab,
			   (void __user *)(unsigned long)tab->s_tab,
			   tab->s_count * sizeof(u16) * 2)) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read ssetting\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	if (copy_from_user(s2_tab,
			   (void __user *)(unsigned long)tab->s2_tab,
			   tab->s2_count * sizeof(u16) * 2)) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to read ssetting\n",
		       current->pid, __func__, __LINE__);
		return -ENOMEM;
	}

	/* master block */
	if (tab->m_count) {
		p = sprd_sensor_get_dev_context(sensor_id[0]);
		if (!p) {
			pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n",
			       current->pid, __func__, __LINE__, __func__);
			return -EINVAL;
		}

		if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL16) {
			if (sprd_sensor_burst_write_reg16_val16(m_tab,
								sensor_id[0],
								tab->m_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write m reg, burst_reg16_val16\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL8) {
			if (sprd_sensor_burst_write_reg16_val8(m_tab,
							       sensor_id[0],
							       tab->m_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write m reg, burst_reg16_val8\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else {
			for (i = 0; i < tab->m_count; i++) {
				if (i >= SENSOR_I2C_BURST_MAX) {
					pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR: i2c_count is out of boundary, i2c_count: %d",
					       current->pid, __func__,
					       __LINE__, i);
					return -EINVAL;
				}

				reg.addr = m_tab[i * 2];
				reg.val = m_tab[i * 2 + 1];
				reg.setting = addr_bits[0] | data_bits[0];

				if (sprd_sensor_write_reg(sensor_id[0], &reg)) {
					pr_err(p->i2c_burst_mode == SENSOR_I2C_BURST_SAMSUNG ?
					       "SENSOR_DRV: %d: %s L %d: fail to write m reg, single\n" :
					       "SENSOR_DRV: %d: %s L %d: fail to write m reg, default\n",
					       current->pid, __func__, __LINE__);
					return -EINVAL;
				}
			}
		}

		tab->m_time = ktime_get_with_offset(1);
	}

	/* slave block */
	if (tab->s_count) {
		p = sprd_sensor_get_dev_context(sensor_id[1]);
		if (!p) {
			pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n",
			       current->pid, __func__, __LINE__, __func__);
			return -EINVAL;
		}

		if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL16) {
			if (sprd_sensor_burst_write_reg16_val16(s_tab,
								sensor_id[1],
								tab->s_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write s reg, burst_reg16_val16\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL8) {
			if (sprd_sensor_burst_write_reg16_val8(s_tab,
							       sensor_id[1],
							       tab->s_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write s reg, burst_reg16_val8\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else {
			for (i = 0; i < tab->s_count; i++) {
				if (i >= SENSOR_I2C_BURST_MAX) {
					pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR: i2c_count is out of boundary, i2c_count: %d",
					       current->pid, __func__,
					       __LINE__, i);
					return -EINVAL;
				}

				reg.addr = s_tab[i * 2];
				reg.val = s_tab[i * 2 + 1];
				reg.setting = addr_bits[1] | data_bits[1];

				if (sprd_sensor_write_reg(sensor_id[1], &reg)) {
					pr_err(p->i2c_burst_mode == SENSOR_I2C_BURST_SAMSUNG ?
					       "SENSOR_DRV: %d: %s L %d: fail to write s reg, single\n" :
					       "SENSOR_DRV: %d: %s L %d: fail to write s reg, default\n",
					       current->pid, __func__, __LINE__);
					return -EINVAL;
				}
			}
		}

		tab->s_time = ktime_get_with_offset(1);
	}

	/* slave2 block */
	if (tab->s2_count) {
		p = sprd_sensor_get_dev_context(sensor_id[2]);
		if (!p) {
			pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n",
			       current->pid, __func__, __LINE__, __func__);
			return -EINVAL;
		}

		if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL16) {
			if (sprd_sensor_burst_write_reg16_val16(s2_tab,
								sensor_id[2],
								tab->s2_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write s_2 reg, burst_reg16_val16\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else if (p->i2c_burst_mode == SENSOR_I2C_BURST_REG16_VAL8) {
			if (sprd_sensor_burst_write_reg16_val8(s2_tab,
							       sensor_id[2],
							       tab->s2_count)) {
				pr_err("SENSOR_DRV: %d: %s L %d: fail to write s_2 reg, burst_reg16_val8\n",
				       current->pid, __func__, __LINE__);
				return -EINVAL;
			}
		} else {
			for (i = 0; i < tab->s2_count; i++) {
				if (i >= SENSOR_I2C_BURST_MAX) {
					pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR: i2c_count is out of boundary, i2c_count: %d",
					       current->pid, __func__,
					       __LINE__, i);
					return -EINVAL;
				}

				reg.addr = s2_tab[i * 2];
				reg.val = s2_tab[i * 2 + 1];
				reg.setting = addr_bits[2] | data_bits[2];

				if (sprd_sensor_write_reg(sensor_id[2], &reg)) {
					pr_err(p->i2c_burst_mode == SENSOR_I2C_BURST_SAMSUNG ?
					       "SENSOR_DRV: %d: %s L %d: fail to write s_2 reg, single\n" :
					       "SENSOR_DRV: %d: %s L %d: fail to write s_2 reg, default\n",
					       current->pid, __func__, __LINE__);
					return -EINVAL;
				}
			}
		}

		tab->s2_time = ktime_get_with_offset(1);
	}

	return 0;
}

int sprd_sensor_write_i2c(void *data, u32 sensor_id)
{
	struct sensor_i2c_data *info = data;
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 buf[0x40];
	u32 len;
	int ret = -EINVAL;

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		goto out;
	}

	len = info->len;
	if (len >= sizeof(buf)) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR: i2c_count is out of boundary, i2c_count: %d",
		       current->pid, __func__, __LINE__, len);
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(buf, (void __user *)(unsigned long)info->buf, len)) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR:\ncopy user fail, size %d\n",
		       current->pid, __func__, __LINE__, len);
		goto out;
	}

	client = p->client2 ? p->client2 : p->client;
	if (!client || IS_ERR(client)) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to get valid phy ptr\n",
		       current->pid, __func__, __LINE__);
		ret = -1;
		goto out;
	}

	msg.addr = info->addr;
	msg.flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
	msg.len = len;
	msg.buf = buf;

	if (i2c_transfer(client->adapter, &msg, 1) != 1) {
		pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: w reg fail, i2c_cnt: %d, addr: 0x%x\n",
		       current->pid, __func__, __LINE__, ret, msg.addr);
		ret = -1;
		goto out;
	}

	ret = 0;

out:
	pr_debug("SENSOR_DRV: %d: %s L %d: sensor w done, ret %d\n",
		 current->pid, __func__, __LINE__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(sprd_sensor_write_i2c);

int sprd_sensor_read_i2c(void *data, u32 sensor_id)
{
	struct sensor_i2c_data *info = data;
	struct sensor_dev_data *p;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	u8 addr_buf[4] = { 0 };
	u8 stack_buf[0x40];
	u8 *buf = stack_buf;
	u32 len, size;
	int ret = -EINVAL, i;

	p = sprd_sensor_get_dev_context(sensor_id);
	if (!p) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s, error\n", current->pid,
		       __func__, __LINE__, __func__);
		return -EINVAL;
	}

	len = info->len;
	size = info->size;

	if (copy_from_user(addr_buf, (void __user *)(unsigned long)info->buf,
			   len)) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor w err:copy user fail, size %d\n",
		       current->pid, __func__, __LINE__, len);
		pr_err("SENSOR_DRV: %d: %s L %d: sensor W I2C ERR: copy user fail, size %d\n",
		       current->pid, __func__, __LINE__, len);
		return -ENOMEM;
	}

	if (size >= sizeof(stack_buf)) {
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf) {
			pr_err("SENSOR_DRV: %d: %s L %d: SENSOR: read block data alloc fail %d\n",
			       current->pid, __func__, __LINE__, -ENOMEM);
			return -ENOMEM;
		}
	}

	client = p->client2 ? p->client2 : p->client;
	if (!client || IS_ERR(client)) {
		pr_err("SENSOR_DRV: %d: %s L %d: fail to get valid phy ptr\n",
		       current->pid, __func__, __LINE__);
		ret = -1;
		goto out;
	}

	msg[0].addr = info->addr;
	msg[0].flags = (p->i2c_clk == 1000000) ? 0x80 : 0x40;
	msg[0].len = len;
	msg[0].buf = addr_buf;

	msg[1].addr = info->addr;
	msg[1].flags = (p->i2c_clk == 1000000) ? 0x81 : 0x41;
	msg[1].len = size;
	msg[1].buf = buf;

	for (i = 0; i < 3; i++) {
		if (i2c_transfer(client->adapter, msg, 2) != 2) {
			pr_err("SENSOR_DRV: %d: %s L %d: %s fail, ret %d, addr 0x%x, reg_addr 0x%x\n",
			       current->pid, __func__, __LINE__, __func__, -1,
			       client->addr, info->addr);
			msleep(20);
			ret = 0;
			goto out;
		}
	}

	ret = copy_to_user((void __user *)(unsigned long)info->buf, buf, size);

out:
	if (buf != stack_buf)
		kfree(buf);

	pr_debug("SENSOR_DRV: %d: %s L %d: %s ret %d\n", current->pid, __func__,
		 __LINE__, __func__, ret);

	return ret;
}
/* ------------------------------------------------------------------ */
/* ioctl helpers                                                       */
/* ------------------------------------------------------------------ */

static int sprd_sensor_io_set_avdd(struct sensor_file_ctx *ctx,
				   unsigned long arg)
{
	u32 idx = 0;
	int val, ret;

	if (_copy_from_user(&idx, (void __user *)arg, sizeof(idx)))
		return 0;

	if ((int)idx >= 13)
		val = idx * 1000;
	else
		val = c_sensor_voltage_tab[idx];

	pr_debug("SENSOR_CORE: %d: %s line=%d: set avdd %d\n", current->pid,
		 __func__, __LINE__, val);

	ret = sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, val,
					      SENSOR_GPIO_AVDD);
	ret &= sprd_sensor_set_voltage(ctx->sensor_id, val, SENSOR_REG_AVDD);

	return ret;
}

static int sprd_sensor_io_set_dvdd(struct sensor_file_ctx *ctx,
				   unsigned long arg)
{
	u32 idx = 0;
	int val, ret;

	if (_copy_from_user(&idx, (void __user *)arg, sizeof(idx)))
		return 0;

	if ((int)idx >= 13)
		val = idx * 1000;
	else
		val = c_sensor_voltage_tab[idx];

	pr_debug("SENSOR_CORE: %d: %s line=%d: set dvdd %d\n", current->pid,
		 __func__, __LINE__, val);

	ret = sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, val,
					      SENSOR_GPIO_DVDD);
	ret &= sprd_sensor_set_voltage(ctx->sensor_id, val, SENSOR_REG_DVDD);

	return ret;
}

static int sprd_sensor_io_set_iovdd(struct sensor_file_ctx *ctx,
				    unsigned long arg)
{
	u32 idx = 0;
	int val, ret;

	if (_copy_from_user(&idx, (void __user *)arg, sizeof(idx)))
		return 0;

	if ((int)idx >= 13)
		val = idx * 1000;
	else
		val = c_sensor_voltage_tab[idx];

	pr_debug("SENSOR_CORE: %d: %s line=%d: set iovdd %d\n", current->pid,
		 __func__, __LINE__, val);

	ret = sprd_sensor_set_voltage_by_gpio(ctx->sensor_id, val,
					      SENSOR_GPIO_IOVDD);
	ret &= sprd_sensor_set_voltage(ctx->sensor_id, val, SENSOR_REG_IOVDD);

	return ret;
}

int sprd_sensor_io_write_i2c_regs(struct sensor_file_ctx *ctx, void __user *arg)
{
	struct sensor_reg_tab tab;

	memset(&tab, 0, sizeof(tab));

	if (__get_user(tab.reg_count, (u32 __user *)(arg + 4)) ||
	    __get_user(tab.setting, (u32 __user *)(arg + 8)) ||
	    __get_user(tab.burst_mode, (u32 __user *)(arg + 0xc)) ||
	    __get_user(tab.reg_tab, (u32 __user *)arg))
		return -EFAULT;

	if (csi_pattern)
		return 0;

	return sprd_sensor_write_regtab(&tab, ctx->sensor_id);
}

int sprd_sensor_io_grc_write_i2c(struct sensor_file_ctx *ctx, void __user *arg)
{
	struct sensor_i2c_data info;

	memset(&info, 0, sizeof(info));

	if (__get_user(info.len, (u16 __user *)(arg + 4)) ||
	    __get_user(info.addr, (u16 __user *)(arg + 6)) ||
	    __get_user(info.size, (u16 __user *)(arg + 8)) ||
	    __get_user(info.buf, (u32 __user *)arg))
		return -EFAULT;

	if (csi_pattern)
		return 0;

	return sprd_sensor_write_i2c(&info, ctx->sensor_id);
}

int sprd_sensor_io_grc_read_i2c(struct sensor_file_ctx *ctx, void __user *arg)
{
	struct sensor_i2c_data info;

	memset(&info, 0, sizeof(info));

	if (__get_user(info.len, (u16 __user *)(arg + 4)) ||
	    __get_user(info.addr, (u16 __user *)(arg + 6)) ||
	    __get_user(info.size, (u16 __user *)(arg + 8)) ||
	    __get_user(info.buf, (u32 __user *)arg))
		return -EFAULT;

	if (csi_pattern)
		return 0;

	return sprd_sensor_read_i2c(&info, ctx->sensor_id);
}

/* ------------------------------------------------------------------ */
/* device tree parsing                                                 */
/* ------------------------------------------------------------------ */

static int sprd_sensor_parse_clk_dt(struct device *dev,
				    struct sensor_dev_data *p)
{
	p->sensor_clk = of_clk_get_by_name(dev->of_node, "clk_src");

	pr_info("SENSOR_DRV: %d: %s L %d: sensor_info->sensor_clk: %p id %d\n",
		current->pid, __func__, __LINE__, p->sensor_clk, p->sensor_id);

	if (!p->sensor_clk || IS_ERR(p->sensor_clk)) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor clk config err\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	p->clk_parent = clk_get_parent(p->sensor_clk);
	if (!p->clk_parent || IS_ERR(p->clk_parent)) {
		pr_err("SENSOR_DRV: %d: %s L %d: get clk parent error\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	p->clk_rate = clk_get_rate(p->clk_parent);

	p->sensor_eb = of_clk_get_by_name(dev->of_node, "sensor_eb");
	p->cur_mclk = 0;
	p->mclk_ref_cnt = 0;

	if (!p->sensor_eb || IS_ERR(p->sensor_eb)) {
		pr_err("SENSOR_DRV: %d: %s L %d: sensor eb clk config err\n",
		       current->pid, __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

static int sprd_sensor_parse_gpio_dt(struct device *dev,
				     struct sensor_dev_data *p)
{
	int i, gpio;

	for (i = 0; i < SENSOR_GPIO_NUM; i++) {
		gpio = of_get_named_gpio_flags(dev->of_node,
					       sprd_sensor_gpio_names[i], 0,
					       NULL);
		p->gpio[i] = gpio;

		if (gpio > 0x1ff || gpio < 0) {
			pr_debug("SENSOR_DRV: %d: %s L %d: invalid gpio: i=%d, gpio=%d, name=%s\n",
				 current->pid, __func__, __LINE__, i, gpio,
				 sprd_sensor_gpio_names[i]);
			continue;
		}

		devm_gpio_request(dev, gpio, sprd_sensor_gpio_names[i]);
	}

	return 0;
}

static int sprd_sensor_parse_dt(struct device *dev,
				struct sensor_dev_data *p)
{
	if (sprd_sensor_parse_clk_dt(dev, p) ||
	    sprd_sensor_parse_gpio_dt(dev, p)) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s :clock parsing error\n",
		       current->pid, __func__, __LINE__, __func__);
		return -EINVAL;
	}

	return 0;
}

static struct device_node *get_csi_port_node(struct device_node *of_node)
{
	struct device_node *node;

	node = of_parse_phandle(of_node, "csi", 0);
	if (!node)
		pr_err("SENSOR_DRV: %d: %s L %d: %s :error getting csi_node\n",
		       current->pid, __func__, __LINE__, __func__);

	return node;
}

static int csi_api_get_dcam_id(u32 phy_id)
{
	u32 dcam_id;

	if (phy_id >= 0xa) {
		pr_err("csi_api: %d %d %s : %s:fail to parse dcam_id : dcam_id:%u\n",
		       current->pid, __LINE__, __func__, __func__, 0xff);
		return -1;
	}

	if ((phy_id & ~8) == 0 || (phy_id & ~8) == 1) {
		dcam_id = phy_id & ~8;
	} else if ((phy_id - 2) < 6) {
		dcam_id = c_sensor_dcam_id_tab[phy_id - 2];
	} else {
		dcam_id = 0xff;
	}

	pr_info("csi_api: %d %d %s : sensor_id %d dcam_id:%u\n", current->pid,
		__LINE__, __func__, phy_id, dcam_id);

	return dcam_id;
}

static int sprd_sensor_config(struct device *dev, struct sensor_dev_data *p)
{
	struct device_node *csi_node;
	int phy_id, dcam_id, ret;

	ret = sprd_sensor_parse_dt(dev, p);
	if (ret) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s :parse dt error\n",
		       current->pid, __func__, __LINE__, __func__);
		return ret;
	}

	p->of_node = dev->of_node;

	phy_id = csi_api_mipi_phy_cfg_init(dev->of_node, p->sensor_id);

	csi_node = get_csi_port_node(p->of_node);
	if (!csi_node) {
		pr_err("SENSOR_DRV: %d: %s L %d: %s; csi sensor connection error\n",
		       current->pid, __func__, __LINE__, __func__);
	} else {
		pr_info("SENSOR_DRV: %d: %s L %d: %lu\n", current->pid, __func__,
			__LINE__, (unsigned long)csi_node);

		csi_api_dt_node_init(dev, csi_node, p->sensor_id, phy_id);
	}

	dcam_id = csi_api_get_dcam_id(phy_id);
	p->dcam_id = dcam_id;

	return ret;
}

/* ------------------------------------------------------------------ */
/* i2c driver plumbing                                                 */
/* ------------------------------------------------------------------ */

static int sprd_sensor_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct sensor_dev_data *p;
	int ret = 0;

	pr_info("SENSOR_DRV: %d: %s L %d: dcam sensor probe start:device name:%s\n",
		current->pid, __func__, __LINE__, id->name);

	if (!client->dev.of_node) {
		pr_err("SENSOR_DRV: %d: %s L %d: no device node %s", current->pid,
		       __func__, __LINE__, __func__);
		return -ENODEV;
	}

	p = devm_kmalloc(&client->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	i2c_set_clientdata(client, p);

	p->regulator[3] = NULL;
	p->client = client;
	p->regulator[0] = NULL;
	p->regulator[1] = NULL;
	p->regulator[2] = NULL;

	__pm_runtime_set_status(&client->dev, 0);
	pm_runtime_enable(&client->dev);

	{
		static struct lock_class_key sync_lock_key;
		static struct lock_class_key voltage_lock_key;

		__mutex_init(&p->sync_lock, "&pdata->sync_lock", &sync_lock_key);
		__mutex_init(&p->set_voltage_lock, "&pdata->set_voltage_lock",
			     &voltage_lock_key);
	}

	p->reserved0 = 0;
	p->sensor_id = SENSOR_MAX_NUM;

	if (of_device_is_compatible(client->dev.of_node, "sprd,sensor-main")) {
		p->sensor_id = SENSOR_MAIN;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_MAIN] = p;
	} else if (of_device_is_compatible(client->dev.of_node,
					   "sprd,sensor-sub")) {
		p->sensor_id = SENSOR_SUB;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_SUB] = p;
	} else if (of_device_is_compatible(client->dev.of_node,
					   "sprd,sensor-main2")) {
		p->sensor_id = SENSOR_MAIN2;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_MAIN2] = p;
	} else if (of_device_is_compatible(client->dev.of_node,
					   "sprd,sensor-sub2")) {
		p->sensor_id = SENSOR_SUB2;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_SUB2] = p;
	} else if (of_device_is_compatible(client->dev.of_node,
					   "sprd,sensor-main3")) {
		p->sensor_id = SENSOR_MAIN3;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_MAIN3] = p;
	} else if (of_device_is_compatible(client->dev.of_node,
					   "sprd,sensor-sub3")) {
		p->sensor_id = SENSOR_SUB3;
		ret = sprd_sensor_config(&client->dev, p);
		s_sensor_dev_data[SENSOR_SUB3] = p;
	}

	return ret;
}

static int sprd_sensor_remove(struct i2c_client *client)
{
	struct sensor_dev_data *p = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int i, gpio;

	for (i = 0; i < SENSOR_GPIO_NUM; i++) {
		gpio = of_get_named_gpio_flags(client->dev.of_node,
					       sprd_sensor_gpio_names[i], 0,
					       NULL);
		p->gpio[i] = gpio;

		if (gpio < 0x200)
			devm_gpio_free(dev, gpio);
	}

	if (p->sensor_id < SENSOR_MAX_NUM)
		s_sensor_dev_data[p->sensor_id] = NULL;

	devm_kfree(dev, p);
	client->dev.driver_data = NULL;

	return 0;
}

int sprd_sensor_register_driver(void)
{
	memset(s_sensor_dev_data, 0, sizeof(s_sensor_dev_data));

	i2c_register_driver(THIS_MODULE, &sprd_main_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register main sensor:%d\n",
		current->pid, __func__, __LINE__, 0);
	usleep_range(10000, 11000);

	i2c_register_driver(THIS_MODULE, &sprd_sub_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register sub sensor:%d\n",
		current->pid, __func__, __LINE__, 0);
	usleep_range(10000, 11000);

	i2c_register_driver(THIS_MODULE, &sprd_main2_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register main2 sensor:%d\n",
		current->pid, __func__, __LINE__, 0);
	usleep_range(10000, 11000);

	i2c_register_driver(THIS_MODULE, &sprd_sub2_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register  sub2 sensor:%d\n",
		current->pid, __func__, __LINE__, 0);
	usleep_range(10000, 11000);

	i2c_register_driver(THIS_MODULE, &sprd_main3_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register main3 sensor:%d\n",
		current->pid, __func__, __LINE__, 0);
	usleep_range(10000, 11000);

	i2c_register_driver(THIS_MODULE, &sprd_sub3_sensor_driver);
	pr_info("SENSOR_DRV: %d: %s L %d: register  sub3 sensor:%d\n",
		current->pid, __func__, __LINE__, 0);

	return 0;
}

void sprd_sensor_unregister_driver(void)
{
	i2c_del_driver(&sprd_main_sensor_driver);
	i2c_del_driver(&sprd_sub_sensor_driver);
	i2c_del_driver(&sprd_main2_sensor_driver);
	i2c_del_driver(&sprd_sub2_sensor_driver);
	i2c_del_driver(&sprd_main3_sensor_driver);
	i2c_del_driver(&sprd_sub3_sensor_driver);
}

MODULE_DESCRIPTION("Spreadtrum Camera Sensor Driver");
MODULE_LICENSE("GPL");

/* small grow-only scratch buffer shared by the ioctl paths */
int sprd_sensor_malloc(struct sensor_mem *mem, u32 size)
{
	if (mem->buf) {
		if (mem->size >= size)
			return 0;
		kfree(mem->buf);
		mem->buf = NULL;
		mem->size = 0;
	}

	mem->buf = kzalloc(size, GFP_KERNEL);
	if (!mem->buf)
		return -ENOMEM;

	mem->size = size;

	return 0;
}

void sprd_sensor_free(struct sensor_mem *mem)
{
	if (mem->buf)
		kfree(mem->buf);

	mem->buf = NULL;
	mem->size = 0;
}

/* thin wrappers around the regulator core, they exist for the log only */
int sprd_sensor_regulator_enable(struct regulator *reg)
{
	int ret = regulator_enable(reg);

	if (ret)
		pr_err("SENSOR_DRV: %d: %s L %d: %s error %d", current->pid,
		       __func__, __LINE__, __func__, ret);

	return ret;
}

int sprd_sensor_regulator_disable(struct regulator *reg)
{
	int ret = regulator_disable(reg);

	if (ret)
		pr_err("SENSOR_DRV: %d: %s L %d: %s error %d", current->pid,
		       __func__, __LINE__, __func__, ret);

	return ret;
}
