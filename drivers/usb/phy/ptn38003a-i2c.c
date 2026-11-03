#include "ptn38003a-i2c.h"

struct redriver_ptn38003a *ptn38003a_redriver;

static unsigned int temp_reg, temp_value;

const struct regmap_config ptn38003a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ptn38003a_write(unsigned int reg, unsigned int val)
{
	return regmap_write(ptn38003a_redriver->regmap, reg, val);
}

static int ptn38003a_read(unsigned int reg, unsigned int *reg_value)
{
	return regmap_read(ptn38003a_redriver->regmap, reg, reg_value);
}

static int ptn38003a_reg_update_bits(unsigned int reg,
			unsigned int mask, unsigned int val)
{
	return regmap_update_bits(ptn38003a_redriver->regmap, reg, mask, val);
}

int ptn38003a_mode_usb32_set(unsigned int enable)
{
	int ret;
	unsigned int reg_value = 0;
	unsigned int mask;

	if (!ptn38003a_redriver) {
		printk(KERN_ERR "ptn38003a hadn't been probe.");
		return -ENODEV;
	}

	if (enable) {
		/* usb plug in enable operation mode usb3.2*/
		mask = PTN38003A_AUTO_ORIENT_EN_MASK |
					PTN38003A_OPERATION_MODE_32_MASK;
		ret = ptn38003a_reg_update_bits(PTN38003A_MODE_CONTROL, mask, 0x81);
		if (ret < 0) {
			dev_err(ptn38003a_redriver->dev,
						"enable PTN38003A_MODE_CONTROL failed: %d.\n", ret);
			goto err;
		}
	} else {
		/* usb plug out disable operation mode usb3.2*/
		ret = ptn38003a_read(PTN38003A_MODE_CONTROL, &reg_value);
		if (ret < 0) {
			dev_err(ptn38003a_redriver->dev,
						"read PTN38003A_MODE_CONTROL failed!\n");
			goto err;
		}

		reg_value &= PTN38003A_OPERATION_MODE_32;
		if (reg_value) {
			ret = ptn38003a_reg_update_bits(PTN38003A_MODE_CONTROL,
				PTN38003A_OPERATION_MODE_32_MASK | PTN38003A_AUTO_ORIENT_EN_MASK, 0x00);
			if (ret < 0) {
				dev_err(ptn38003a_redriver->dev,
							"disable PTN38003A_MODE_CONTROL failed: %d.\n", ret);
				goto err;
			}
		}
	}

	return 0;
err:
	return ret;
}
EXPORT_SYMBOL(ptn38003a_mode_usb32_set);

/*
 * The three DP helpers below are what sprd-drm's redriver glue calls.  Only
 * PTN38003A_MODE_CONTROL (0x04) and register 0x06 carry the settings:
 *   0x04 bits 2:0 -> mode, 0x02 = 32- and 4-lane DP
 *   0x06 bits 3:2 -> lane count,  0x0c = 4 lanes (0x04 = 1 lane, 0x08 = 2)
 *   0x06 bits 1:0 -> link rate,   0x01 = 0x0a, 0x02 = 0x14, 0x03 = 0x1e
 * Anything else is left alone; the device still reads the register back for
 * its log line.
 */
int ptn38003a_dp_lane_set(unsigned int lane)
{
	int val = 0, ret = 0;

	pr_info("dp_lane_set");
	if (!ptn38003a_redriver) {
		printk(KERN_ERR "ptn38003a hadn't been probe.");
		return -ENODEV;
	}

	ptn38003a_read(0x06, &val);
	pr_info("ptn38003a_dp_lane_count_start:0x%x,0x%x", val, lane);

	switch (lane & ~PTN38003A_AUTO_ORIENT_EN_MASK) {
	case 1:
		val = 0x04;
		break;
	case 2:
		val = 0x08;
		break;
	case 4:
		val = 0x0c;
		break;
	default:
		val = 0;
		if (lane)
			goto readback;
	}

	ret = ptn38003a_reg_update_bits(0x06, 0x0c, val);

readback:
	ptn38003a_read(0x06, &val);
	pr_info("ptn38003a_dp_lane_count_end:0x%x", val);
	if (ret < 0) {
		dev_err(ptn38003a_redriver->dev,
			"set PTN38003A_DP_LANE failed: %d.", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ptn38003a_dp_lane_set);

int ptn38003a_dp_link_rate_set(unsigned int rate)
{
	int val = 0, ret = 0;

	pr_info("dp_link_rate_set");
	if (!ptn38003a_redriver) {
		printk(KERN_ERR "ptn38003a hadn't been probe.");
		return -ENODEV;
	}

	ptn38003a_read(0x06, &val);
	pr_info("ptn38003a_dp_link_rate_start:0x%x,0x%x", val, rate);

	switch (rate) {
	case 0x0a:
		val = 1;
		break;
	case 0x14:
		val = 2;
		break;
	case 0x1e:
		val = 3;
		break;
	default:
		goto readback;
	}

	ret = ptn38003a_reg_update_bits(0x06, 0x03, val);

readback:
	ptn38003a_read(0x06, &val);
	pr_info("ptn38003a_dp_link_rate_end:0x%x", val);
	if (ret < 0) {
		dev_err(ptn38003a_redriver->dev,
			"set PTN38003A_DP_LINK_RATE failed: %d.", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ptn38003a_dp_link_rate_set);

int ptn38003a_dp_mode_set(unsigned int on)
{
	int ret;

	if (!ptn38003a_redriver) {
		printk(KERN_ERR "ptn38003a hadn't been probe.");
		return -ENODEV;
	}

	ret = ptn38003a_reg_update_bits(PTN38003A_MODE_CONTROL,
					PTN38003A_OPERATION_MODE_MASK, on ? 0x02 : 0);
	if (ret < 0) {
		dev_err(ptn38003a_redriver->dev,
			"set PTN38003A_CONTROL_MODE failed: %d.", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ptn38003a_dp_mode_set);

static ssize_t ptn38003a_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret = 0;
	unsigned int reg_val = 0;

	ret = ptn38003a_read(temp_reg, &reg_val);
	if (ret < 0)
		return sprintf(buf, "Read 0x%x failed!\n", temp_reg);

	return sprintf(buf, "0x%x\n", reg_val);
}

static ssize_t ptn38003a_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int ret;

	ret = sscanf(buf, "%x %x", &temp_reg, &temp_value);
	if (ret == 2)
		ptn38003a_write(temp_reg, temp_value);

	return size;
}

static DEVICE_ATTR_RW(ptn38003a);

static struct attribute *ptn38003a_attrs[] = {
	&dev_attr_ptn38003a.attr,
	NULL
};

ATTRIBUTE_GROUPS(ptn38003a);

static int ptn38003a_detect(void)
{
	unsigned int reg_val = 0;
	return ptn38003a_read(PTN38003A_CHIP_ID, &reg_val);
}

static int ptn38003a_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "device doesn't suport I2C\n");
		return -ENODEV;
	}

	ptn38003a_redriver = devm_kzalloc(&client->dev, sizeof(*ptn38003a_redriver), GFP_KERNEL);
	if (!ptn38003a_redriver)
		return -ENOMEM;

	ptn38003a_redriver->dev = &client->dev;
	ptn38003a_redriver->client = client;

	ptn38003a_redriver->regmap = devm_regmap_init_i2c(client, &ptn38003a_regmap_config);
	if (IS_ERR(ptn38003a_redriver->regmap)) {
		ret = PTR_ERR(ptn38003a_redriver->regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		goto err_regmap;
	}

	i2c_set_clientdata(client, ptn38003a_redriver);

	ret = ptn38003a_detect();
	if (ret < 0) {
		dev_err(&client->dev, "detect ptn38003a failed.\n");
		goto err_regmap;
	} else {
		dev_info(&client->dev, "detect ptn38003a success.\n");
	}

	ret = sysfs_create_groups(&client->dev.kobj, ptn38003a_groups);
	if (ret)
		dev_err(&client->dev, "failed to create ptn38003a  attributes\n");

	return 0;

err_regmap:
	devm_kfree(&client->dev, ptn38003a_redriver);
	ptn38003a_redriver = NULL;

	return ret;
}

static int ptn38003a_i2c_remove(struct i2c_client *client)
{
	devm_kfree(&client->dev, ptn38003a_redriver);
	sysfs_remove_file(&client->dev.kobj, ptn38003a_attrs[0]);
	ptn38003a_redriver = NULL;

	return 0;
}

static const struct of_device_id ptn38003a_of_match[] = {
	{
		.compatible = "sprd,ptn38003a redriver",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ptn38003a_of_match);

static const struct i2c_device_id ptn38003a_i2c_ids[] = {
	{"ptn38003a redriver", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ptn38003a_i2c_ids);

static struct i2c_driver redriver_i2c = {
	.probe = ptn38003a_i2c_probe,
	.remove = ptn38003a_i2c_remove,
	.driver = {
		.name = "ptn38003a",
		.of_match_table = of_match_ptr(ptn38003a_of_match),
	},
	.id_table = ptn38003a_i2c_ids,
};

module_i2c_driver(redriver_i2c);

MODULE_DESCRIPTION("usb30 phy redrive driver");
MODULE_LICENSE("GPL v2");
