// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO torch LED driver for Unisoc/Spreadtrum Android phones.
 *
 * Source-level reconstruction of the vendor module "leds-torch.ko"
 * (description "GPIO LED driver for Android phone", license "GPL v2",
 * intree:Y) taken from the vendor_boot ramdisk of the Sunelan Q968
 * (UMS9157 / T157, Android 13, build 10CN_0_480).
 *
 * Every observable behaviour below is taken from the binary:
 *   - probe() devm_kzallocs a struct led_classdev, sets name "torch",
 *     max_brightness 1 and brightness_set, then calls
 *     devm_led_classdev_register_ext(dev, cdev, NULL);
 *   - the brightness callback drives hard-coded GPIO 176 through
 *     gpio_to_desc()/gpiod_set_raw_value().  GPIO 176 is AP GPIO 112
 *     (the EIC banks occupy the first 64 numbers), i.e. exactly the pin
 *     described by "torch-en-gpios" of the "torch-gpio-leds" node in the
 *     device tree;
 *   - the platform driver is named "gpio-leds" and matches the
 *     "torch-gpio-leds" compatible.
 *
 * 名字照 .ko 的符号表：函数叫 gpio_led_probe/gpio_led_set、驱动结构叫
 * gpio_led_driver、匹配表叫 torch_led_of_match；.modinfo 里**没有** of: 别名，
 * 说明 vendor 源码没有写 MODULE_DEVICE_TABLE(of, ...)，这里也不写。
 *
 * See ums9157-recon/ for the reconstruction notes and the binary hash.
 */

#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* AP GPIO 112; the global number once the EIC controllers are accounted for. */
#define TORCH_GPIO_NUM		176

static void gpio_led_set(struct led_classdev *cdev, enum led_brightness value)
{
	gpiod_set_raw_value(gpio_to_desc(TORCH_GPIO_NUM), value);
}

static int gpio_led_probe(struct platform_device *pdev)
{
	struct led_classdev *cdev;

	/*
	 * .ko 只 import devm_kmalloc（没有 devm_kzalloc），gfp 立即数 0xdc0 =
	 * GFP_KERNEL(0xcc0) | __GFP_ZERO(0x100) ⇒ 调的是 devm_kmalloc 但带
	 * __GFP_ZERO；只初始化下面三个字段，其余靠清零。
	 */
	cdev = devm_kmalloc(&pdev->dev, sizeof(*cdev), GFP_KERNEL | __GFP_ZERO);
	if (!cdev)
		return -ENOMEM;

	cdev->name = "torch";
	cdev->max_brightness = 1;
	cdev->brightness_set = gpio_led_set;

	return devm_led_classdev_register_ext(&pdev->dev, cdev, NULL);
}

static const struct of_device_id torch_led_of_match[] = {
	{ .compatible = "torch-gpio-leds" },
	{ }
};

static struct platform_driver gpio_led_driver = {
	.probe = gpio_led_probe,
	.driver = {
		.name = "gpio-leds",
		.of_match_table = torch_led_of_match,
	},
};

static int __init gpio_led_init(void)
{
	return platform_driver_register(&gpio_led_driver);
}

static void __exit gpio_led_exit(void)
{
	platform_driver_unregister(&gpio_led_driver);
}

module_init(gpio_led_init);
module_exit(gpio_led_exit);

MODULE_DESCRIPTION("GPIO LED driver for Android phone");
MODULE_LICENSE("GPL v2");
