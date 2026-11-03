// SPDX-License-Identifier: GPL-2.0
/*
 * flash_ic_ocp8137.c - OCP8137 dual flash LED driver (I2C)
 *
 * Reconstructed from the vendor binary flash_ic_ocp8137.ko shipped in the
 * UMS9157 (T157 / Sunelan Q968) vendor_boot ramdisk:
 *
 *   description = "Sprd ocp8137 Flash Driver"
 *   license     = GPL
 *   depends     = sprd_flash_drv
 *   vermagic    = 5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt
 *                 mod_unload modversions aarch64
 *
 * It plugs into the flash framework rebuilt in drivers/misc/sprd_flash_drv.c
 * (sprd_flash_register()) and exposes the nine IC callbacks of struct
 * sprd_flash_ic_ops.  The IC is described in the device tree as
 *
 *	flash_ic: flash-ic@.. {
 *		compatible = "sprd,flash-ocp8137";
 *		reg = <0x63>;
 *		sprd,flash-ic = <8137>;
 *		sprd,torch-level   = <..>;	/* percent, 0..31   */
 *		sprd,preflash-level = <..>;
 *		sprd,highlight-level = <..>;
 *		sprd,lvfm-enable = <..>;
 *		sprd,torch = <1>;		/* not used by the code */
 *		sprd,preflash = <1>;		/* not used by the code */
 *		sprd,highlight = <0>;		/* not used by the code */
 *		torch-led-idx = <..>;		/* which LEDs the torch uses */
 *		flash-chip-en-gpios  = <&ap_gpio .. GPIO_ACTIVE_HIGH>;
 *		flash-torch-en-gpios = <&ap_gpio .. GPIO_ACTIVE_HIGH>;
 *		flash-en-gpios       = <&ap_gpio .. GPIO_ACTIVE_HIGH>;
 *		flash-sync-gpios     = <&ap_gpio .. GPIO_ACTIVE_HIGH>;
 *	};
 *
 * Register map used by this driver (all writes are smbus byte writes):
 *
 *	0x01	mode        0xb0 | (led_mask & 3)   torch/preflash/highlight on
 *	0x03	highlight LED0 current = 4 * level
 *	0x04	highlight LED1 current = 4 * level
 *	0x05	torch/preflash LED0 current = 4 * (level & 0x1f)
 *	0x06	torch/preflash LED1 current = 4 * level
 *	0x08	0x1f			written once at probe
 *	0x0a / 0x0b	read (and logged) before the mode write
 *
 * Layout of the driver private data (devm_kmalloc'ed, 96 bytes, offsets as in
 * the vendor binary):
 *
 *	0x00	struct i2c_client *client
 *	0x08	struct mutex i2c_lock		(32 bytes, never taken)
 *	0x28	u32 gpio[4]			chip_en, torch_en, flash_en, sync
 *	0x38	u32 reserved[2]
 *	0x40	u32 lvfm_enable
 *	0x44	u32 torch_level
 *	0x48	u32 preflash_level
 *	0x4c	u32 highlight_level
 *	0x50	u32 reserved1
 *	0x54	u32 torch_led_index
 *	0x58	struct sprd_flash_info *info	-> &flash_ic_info
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "sprd_flash_drv.h"

#define OCP8137_LOG(_level, fmt, ...)					\
	printk(_level "FLASH_OCP8137: %d %d %s : " fmt, current->pid,	\
	       __LINE__, __func__, ##__VA_ARGS__)

#define ocp_log_err(fmt, ...)		OCP8137_LOG(KERN_ERR, fmt, ##__VA_ARGS__)
#define ocp_log_info(fmt, ...)		OCP8137_LOG(KERN_INFO, fmt, ##__VA_ARGS__)

#define OCP8137_CHIP_ID		8137

#define OCP8137_REG_MODE	0x01
#define OCP8137_REG_HL_LED0	0x03
#define OCP8137_REG_HL_LED1	0x04
#define OCP8137_REG_TORCH_LED0	0x05
#define OCP8137_REG_TORCH_LED1	0x06
#define OCP8137_REG_INIT	0x08
#define OCP8137_REG_READ0	0x0a
#define OCP8137_REG_READ1	0x0b

#define OCP8137_MODE_ON		0xb0
#define OCP8137_INIT_VAL	0x1f
#define OCP8137_LEVEL_SHIFT	2	/* level -> register value: 4 * level */
#define OCP8137_LEVEL_MASK	0x1f

#define GPIO_INVALID_MAX	0x1ff	/* of_get_named_gpio_flags() failure range */

enum {
	FLASH_GPIO_CHIP_EN = 0,
	FLASH_GPIO_TORCH_EN,
	FLASH_GPIO_FLASH_EN,
	FLASH_GPIO_SYNC,
	FLASH_GPIO_MAX
};

static const char * const flash_ic_gpio_names[FLASH_GPIO_MAX] = {
	[FLASH_GPIO_CHIP_EN]	= "flash-chip-en-gpios",
	[FLASH_GPIO_TORCH_EN]	= "flash-torch-en-gpios",
	[FLASH_GPIO_FLASH_EN]	= "flash-en-gpios",
	[FLASH_GPIO_SYNC]	= "flash-sync-gpios",
};

/**
 * struct ocp8137_data - driver private data, see the offset table above
 */
struct ocp8137_data {
	struct i2c_client *client;		/* 0x00 */
	struct mutex i2c_lock;			/* 0x08 */
	u32 gpio[FLASH_GPIO_MAX];		/* 0x28 */
	u32 reserved[2];			/* 0x38 */
	u32 lvfm_enable;			/* 0x40 */
	u32 torch_level;			/* 0x44 */
	u32 preflash_level;			/* 0x48 */
	u32 highlight_level;			/* 0x4c */
	u32 reserved1;				/* 0x50 */
	u32 torch_led_index;			/* 0x54 */
	struct sprd_flash_info *info;		/* 0x58 */
};

/*
 * Information blob handed to userspace.  The vendor build never writes it, it
 * is only pointed to by the private data and returned by get_flash_info(), so
 * it stays all zero in practice.
 */
static struct sprd_flash_info flash_ic_info;

/* ------------------------------------------------------------------ */
/* tiny register helpers (they are separate functions in the binary)   */
/* ------------------------------------------------------------------ */

static void flash_ic_driver_reg_write(struct i2c_client *client, u8 reg, u8 val)
{
	ocp_log_info("flash ic reg write %x %x\n", reg, val);
	i2c_smbus_write_byte_data(client, reg, val);
}

static int flash_ic_driver_reg_read(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		ocp_log_err("read reg %x failed %d\n", reg, ret);
	return ret;
}

/**
 * flash_ic_driver_reg_init() - read both status registers and set the mode
 */
static void flash_ic_driver_reg_init(struct ocp8137_data *pdata, u32 led_mask)
{
	flash_ic_driver_reg_read(pdata->client, OCP8137_REG_READ0);
	flash_ic_driver_reg_read(pdata->client, OCP8137_REG_READ1);
	flash_ic_driver_reg_write(pdata->client, OCP8137_REG_MODE,
				  OCP8137_MODE_ON | (led_mask & 3));
}

/*
 * Set one of the enable lines.  Both LED mask bits of the torch/preflash
 * callbacks drive the *same* pin (flash-torch-en) in the vendor build, the
 * highlight callbacks drive flash-en instead.  An out of range (i.e. not
 * present in the device tree) GPIO number is silently ignored.
 */
static int flash_ic_set_gpio(struct ocp8137_data *pdata, u32 index, int value)
{
	u32 gpio = pdata->gpio[index];

	if (gpio > GPIO_INVALID_MAX)
		return 0;

	return gpiod_direction_output_raw(gpio_to_desc(gpio), value);
}

/* ------------------------------------------------------------------ */
/* sprd_flash_ic_ops                            (dapm like ABI, 10 slots) */
/* ------------------------------------------------------------------ */

static int sprd_flash_ic_open_torch(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (pdata->client) {
		flash_ic_driver_reg_init(pdata, pdata->torch_led_index);
		if (!(pdata->torch_led_index & 1))
			goto out_led1;
	}

	ret = flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 1);
	if (ret)
		return ret;

out_led1:
	if (pdata->torch_led_index & 2)
		flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 1);

	return 0;
}

static int sprd_flash_ic_close_torch(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (pdata->torch_led_index & 1) {
		ret = flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 0);
		if (ret)
			return ret;
	}

	if (pdata->torch_led_index & 2)
		flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 0);

	return 0;
}

static int sprd_flash_ic_open_preflash(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (pdata->client) {
		flash_ic_driver_reg_init(pdata, led_mask);
		if (!(led_mask & 1))
			goto out_led1;
	}

	ret = flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 1);
	if (ret)
		return ret;

out_led1:
	if (led_mask & 2)
		flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 1);

	return 0;
}

static int sprd_flash_ic_close_preflash(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (led_mask & 1) {
		ret = flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 0);
		if (ret)
			return ret;
	}

	if (led_mask & 2)
		flash_ic_set_gpio(pdata, FLASH_GPIO_TORCH_EN, 0);

	return 0;
}

static int sprd_flash_ic_open_highlight(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (pdata->client) {
		flash_ic_driver_reg_init(pdata, led_mask);
		if (!(led_mask & 1))
			goto out_led1;
	}

	ret = flash_ic_set_gpio(pdata, FLASH_GPIO_FLASH_EN, 1);
	if (ret)
		return ret;

out_led1:
	if (led_mask & 2)
		return flash_ic_set_gpio(pdata, FLASH_GPIO_FLASH_EN, 1);

	return 0;
}

static int sprd_flash_ic_close_highlight(void *data, u32 led_mask)
{
	struct ocp8137_data *pdata = data;
	int ret;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, idx:%d\n",
		     pdata->torch_led_index, led_mask & 0xff);

	if (led_mask & 1) {
		ret = flash_ic_set_gpio(pdata, FLASH_GPIO_FLASH_EN, 0);
		if (ret)
			return ret;
	}

	if (led_mask & 2)
		return flash_ic_set_gpio(pdata, FLASH_GPIO_FLASH_EN, 0);

	return 0;
}

/*
 * "level" programming.  The framework hands us the element value in
 * element->value (0..31, i.e. 1/32 steps of the maximum current); the register
 * value is 4 * level.  Which LED is programmed depends on the LED mask: the
 * torch variant uses the DTS torch-led-idx, the preflash/highlight variants use
 * the mask passed by the framework.
 */
static int sprd_flash_ic_cfg_value_torch(void *data, u32 index,
					 struct sprd_flash_element *element)
{
	struct ocp8137_data *pdata = data;
	u32 level;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("torch_led_index:%d, element->index %d, idx:%d\n",
		     pdata->torch_led_index, element->index & 0xff, index);

	if (!pdata->client)
		return 0;

	level = element->value & OCP8137_LEVEL_MASK;
	if (pdata->torch_led_index & 1)
		flash_ic_driver_reg_write(pdata->client,
					  OCP8137_REG_TORCH_LED0,
					  level << OCP8137_LEVEL_SHIFT);

	if (!(pdata->torch_led_index & 2))
		return 0;

	flash_ic_driver_reg_write(pdata->client, OCP8137_REG_TORCH_LED1,
				  (element->value & 0xff) << OCP8137_LEVEL_SHIFT);
	return 0;
}

static int sprd_flash_ic_cfg_value_preflash(void *data, u32 led_mask,
					    struct sprd_flash_element *element)
{
	struct ocp8137_data *pdata = data;
	u32 level;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("element->index %d, idx:%d\n", element->index & 0xff,
		     led_mask & 0xff);

	if (!pdata->client)
		return 0;

	level = element->value & OCP8137_LEVEL_MASK;
	if (led_mask & 1)
		flash_ic_driver_reg_write(pdata->client,
					  OCP8137_REG_TORCH_LED0,
					  level << OCP8137_LEVEL_SHIFT);

	if (!(led_mask & 2))
		return 0;

	flash_ic_driver_reg_write(pdata->client, OCP8137_REG_TORCH_LED1,
				  (element->value & 0xff) << OCP8137_LEVEL_SHIFT);
	return 0;
}

static int sprd_flash_ic_cfg_value_highlight(void *data, u32 led_mask,
					     struct sprd_flash_element *element)
{
	struct ocp8137_data *pdata = data;
	u32 level;

	if (!pdata)
		return -EFAULT;

	ocp_log_info("element->index %d, idx:%d\n", element->index & 0xff,
		     led_mask & 0xff);

	if (!pdata->client)
		return 0;

	level = element->value & OCP8137_LEVEL_MASK;
	if (led_mask & 1)
		flash_ic_driver_reg_write(pdata->client,
					  OCP8137_REG_HL_LED0,
					  level << OCP8137_LEVEL_SHIFT);

	if (!(led_mask & 2))
		return 0;

	flash_ic_driver_reg_write(pdata->client, OCP8137_REG_HL_LED1,
				  (element->value & 0xff) << OCP8137_LEVEL_SHIFT);
	return 0;
}

/*
 * The framework calls this through a pointer to the IC data, the vendor build
 * ignores both arguments and returns the (zeroed) global blob.
 */
static struct sprd_flash_info sprd_flash_ic_get_flash_info(void *data, u32 index,
							   void *arg)
{
	ocp_log_info("sprd_flash_ic_get_flash_info flash_ic_name %s %s\n",
		     "ocp8137", "");

	return flash_ic_info;
}

static struct sprd_flash_ic_ops flash_ic_ops = {
	.open_torch = sprd_flash_ic_open_torch,
	.close_torch = sprd_flash_ic_close_torch,
	.open_preflash = sprd_flash_ic_open_preflash,
	.close_preflash = sprd_flash_ic_close_preflash,
	.open_highlight = sprd_flash_ic_open_highlight,
	.close_highlight = sprd_flash_ic_close_highlight,
	.cfg_value_torch = sprd_flash_ic_cfg_value_torch,
	.cfg_value_preflash = sprd_flash_ic_cfg_value_preflash,
	.cfg_value_highlight = sprd_flash_ic_cfg_value_highlight,
	.get_flash_info = sprd_flash_ic_get_flash_info,
};

/* ------------------------------------------------------------------ */
/* probe / remove                                                      */
/* ------------------------------------------------------------------ */

/**
 * sprd_flash_ic_init() - enable the chip and initialise it over I2C
 */
static void sprd_flash_ic_init(struct ocp8137_data *pdata)
{
	u32 gpio = pdata->gpio[FLASH_GPIO_CHIP_EN];
	int ret;

	if (gpio > GPIO_INVALID_MAX) {
		ocp_log_err("flash chip enable gpio %d not available\n", gpio);
		goto out;
	}

	ocp_log_info("flash ic init gpio id %d\n", gpio);
	ret = gpiod_direction_output_raw(gpio_to_desc(gpio), 1);
	if (ret) {
		ocp_log_err("flash chip enable gpio %d failed %d\n", gpio, ret);
		goto out;
	}

	msleep(2);

	if (pdata->client) {
		flash_ic_driver_reg_write(pdata->client, OCP8137_REG_MODE,
					  OCP8137_MODE_ON);
		flash_ic_driver_reg_write(pdata->client, OCP8137_REG_INIT,
					  OCP8137_INIT_VAL);
		ocp_log_info("flash ic init ret %d %d\n", OCP8137_REG_MODE,
			     OCP8137_REG_INIT);
	}

out:
	ocp_log_info("flash ic init exit\n");
}

static int sprd_flash_ic_driver_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct ocp8137_data *pdata;
	u32 val = 0;
	u32 torch = 0, preflash = 0, highlight = 0;
	int i, ret;

	if (!np) {
		ocp_log_err("no device node %s\n", dev_name(&client->dev));
		return -ENODEV;
	}

	ocp_log_info("flash-ic-driver probe\n");

	ret = of_property_read_u32(np, "sprd,flash-ic", &val);
	if (ret < 0 || val != OCP8137_CHIP_ID)
		return -ENODEV;

	/* the vendor build uses devm_kmalloc() and never clears the buffer;
	 * kzalloc() keeps the observable behaviour and is deterministic
	 */
	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	i2c_set_clientdata(client, pdata);
	pdata->client = client;
	mutex_init(&pdata->i2c_lock);	/* name in the binary: "&pdata->i2c_lock" */

	/*
	 * The three "mode" properties are read but not used by the vendor code,
	 * only the two of them being present is enforced.
	 */
	ret = of_property_read_u32(np, "sprd,torch", &torch);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(np, "sprd,preflash", &preflash);
	if (ret < 0)
		return ret;

	of_property_read_u32(np, "sprd,highlight", &highlight);

	if (of_property_read_u32(np, "sprd,torch-level", &pdata->torch_level))
		ocp_log_err("torch-level no cfg\n");

	if (of_property_read_u32(np, "sprd,preflash-level",
				 &pdata->preflash_level))
		ocp_log_err("preflash-level no cfg\n");

	if (of_property_read_u32(np, "sprd,highlight-level",
				 &pdata->highlight_level))
		ocp_log_err("highlight-level no cfg\n");

	if (of_property_read_u32(np, "sprd,lvfm-enable", &pdata->lvfm_enable))
		ocp_log_err("lvfm-enable no cfg\n");

	if (of_property_read_u32(np, "torch-led-idx", &pdata->torch_led_index))
		pdata->torch_led_index = 1;

	for (i = 0; i < FLASH_GPIO_MAX; i++) {
		pdata->gpio[i] = of_get_named_gpio_flags(np,
							 flash_ic_gpio_names[i],
							 0, NULL);
		if (pdata->gpio[i] <= GPIO_INVALID_MAX &&
		    devm_gpio_request(&client->dev, pdata->gpio[i],
				      flash_ic_gpio_names[i]))
			ocp_log_err("flash gpio err\n");
	}

	pdata->info = &flash_ic_info;
	ocp_log_info("flash_ic_info %s %s\n", "ocp8137", "");
	ocp_log_info("flash ic init gpio id %d\n",
		     pdata->gpio[FLASH_GPIO_CHIP_EN]);

	sprd_flash_ic_init(pdata);

	return sprd_flash_register(&flash_ic_ops, pdata, 0);
}

static int sprd_flash_ic_driver_remove(struct i2c_client *client)
{
	struct ocp8137_data *pdata = i2c_get_clientdata(client);

	if (pdata)
		devm_kfree(&client->dev, pdata);

	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct of_device_id sprd_flash_ic_of_match_table[] = {
	{ .compatible = "sprd,flash-ocp8137" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_flash_ic_of_match_table);

static const struct i2c_device_id sprd_flash_ic_ids[] = {
	{ "ocp8137", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sprd_flash_ic_ids);

static struct i2c_driver sprd_flash_ic_driver = {
	.probe = sprd_flash_ic_driver_probe,
	.remove = sprd_flash_ic_driver_remove,
	.driver = {
		.name = "sprd_ocp8137",
		.of_match_table = sprd_flash_ic_of_match_table,
	},
	.id_table = sprd_flash_ic_ids,
};

static int __init sprd_flash_ic_init_module(void)
{
	int ret = i2c_register_driver(THIS_MODULE, &sprd_flash_ic_driver);

	ocp_log_info("register sprd_flash_ic_driver:%d\n", ret);
	return ret;
}

static void __exit sprd_flash_ic_exit_module(void)
{
	i2c_del_driver(&sprd_flash_ic_driver);
}

module_init(sprd_flash_ic_init_module);
module_exit(sprd_flash_ic_exit_module);

MODULE_DESCRIPTION("Sprd ocp8137 Flash Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:ocp8137");
