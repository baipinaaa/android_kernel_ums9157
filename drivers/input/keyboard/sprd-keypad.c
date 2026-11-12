// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc/Spreadtrum keypad controller (built-in, CONFIG_KEYBOARD_SPRD=y).
 *
 * 这份源码是从 `recon/raw/Image` 反汇编重建的（设备上没有 `.ko` 可拆：这个驱动
 * 编在内核里，`recon/out/image_addrs.txt` 给出 6 个函数的地址与大小，用
 * `recon/iodump.py sprd_keypad_probe …` 反汇编）。没有交叉编译器，所以下面的
 * “等价”指的是逻辑与常量逐条对得上，而不是能编出同一个二进制。
 *
 * 函数表（文件偏移 == 虚拟地址 - 0xffffffc010080000）：
 *   sprd_keypad_probe    0xd735bc 1760 B
 *   sprd_keypad_remove   0xd73c9c  332 B
 *   sprd_keypad_suspend  0xd73de8  120 B
 *   sprd_keypad_resume   0xd73e60  260 B
 *   sprd_keypad_hw_init  0xd73f64  260 B
 *   sprd_keypad_handler  0xd74068  344 B
 * 数据：sprd_keypad_driver 0x238e648 / sprd_keypad_pm_ops 0x1e40130 /
 *       sprd_keypad_match 0x1e3ffa0（compatible = "sprd,sc9860-keypad"）
 *
 * 私有结构体（devm_kmalloc 0x40 字节，gfp 0xdc0 = GFP_KERNEL|__GFP_ZERO）：
 *   +0x00 u32 row_used    probe 里扫 keymap 得到的“哪些 row 有键”掩码
 *   +0x04 u32 col_used    同上，col 方向
 *   +0x08 u32 num_rows    来自 DT `keypad,num-rows`
 *   +0x0c u32 num_cols    来自 DT `keypad,num-columns`
 *   +0x10 u32 flags       BIT(0) wakeup-source / BIT(1) sprd,support_long_key /
 *                         BIT(2) linux,repeat
 *   +0x14 u32 debounce    DT `debounce-interval`，缺省 5（毫秒）
 *   +0x18 u32 row_shift   只算不存（反汇编里没有写这个偏移的指令），占位
 *   +0x1c u32 pad
 *   +0x20 void __iomem *base
 *   +0x28 struct input_dev *input
 *   +0x30 struct clk *clk_enable   DT 里叫 "enable"
 *   +0x38 struct clk *clk_rtc      DT 里叫 "rtc"
 *
 * 寄存器（相对 base，全部 32 位）：
 *   +0x0c  按键状态（bit i = 第 i 组按下，bit i+4 = 第 i 组抬起，i=0..3）
 *   +0x10  中断状态，写 0xfff 清
 *   +0x18  hw_init 写 0xffff
 *   +0x1c  去抖计数值
 *   +0x20  预分频
 *   +0x28  hw_init 写 1（使能）
 *   +0x2c  列状态（每组 8 位，低 3 位列号，次 3 位行号，见 handler）
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define SPRD_KEYPAD_MAX_ROWS	8
#define SPRD_KEYPAD_MAX_COLS	8
#define SPRD_KEYPAD_GROUPS	4

#define SPRD_KEYPAD_DEF_DEBOUNCE	5

/* priv->flags */
#define SPRD_KEYPAD_WAKEUP		BIT(0)
#define SPRD_KEYPAD_LONG_KEY		BIT(1)
#define SPRD_KEYPAD_REPEAT		BIT(2)

/* 寄存器偏移 */
#define SPRD_KEYPAD_REG_CFG		0x00
#define SPRD_KEYPAD_REG_CTRL2		0x04
#define SPRD_KEYPAD_REG_STATUS		0x0c
#define SPRD_KEYPAD_REG_INT_STS		0x10
#define SPRD_KEYPAD_REG_CTRL0		0x18
#define SPRD_KEYPAD_REG_DEBOUNCE	0x1c
#define SPRD_KEYPAD_REG_PRESCALE	0x20
#define SPRD_KEYPAD_REG_CTRL3		0x24
#define SPRD_KEYPAD_REG_CTRL1		0x28
#define SPRD_KEYPAD_REG_COL		0x2c

struct sprd_keypad {
	u32 row_used;
	u32 col_used;
	u32 num_rows;
	u32 num_cols;
	u32 flags;
	u32 debounce;
	u32 row_shift;
	u32 pad;
	void __iomem *base;
	struct input_dev *input;
	struct clk *clk_enable;
	struct clk *clk_rtc;
};

static void sprd_keypad_hw_init(struct sprd_keypad *priv)
{
	u32 val;

	writel(0xfff, priv->base + SPRD_KEYPAD_REG_INT_STS);
	writel(0xffff, priv->base + SPRD_KEYPAD_REG_CTRL0);
	writel(1, priv->base + SPRD_KEYPAD_REG_CTRL1);

	/* 32.768 MHz / (rows*cols*2000) - 1，超过 32.768 MHz 就是 0 */
	val = priv->num_rows * priv->num_cols * 2000;
	if (val > 0x1f40000)
		writel(0, priv->base + SPRD_KEYPAD_REG_PRESCALE);
	else
		writel(0x1f40000 / val - 1, priv->base + SPRD_KEYPAD_REG_PRESCALE);

	/* 去抖：debounce(ms) * 32768 / (rows*cols*2000) - 1 */
	val = priv->num_rows * priv->num_cols * 2000;
	if (val > (priv->debounce << 15))
		writel(0, priv->base + SPRD_KEYPAD_REG_DEBOUNCE);
	else
		writel((priv->debounce << 15) / val - 1,
		       priv->base + SPRD_KEYPAD_REG_DEBOUNCE);

	/*
	 * 原厂 sprd_keypad_hw_init（Image 0xffffffc010df3f64，65 条指令）尾部还有三笔
	 * 寄存器写，缺了它们控制器不会开始扫描，只有 gpio-keys 的 POWER/音量键有反应：
	 *   [base+0x04] = flags & SPRD_KEYPAD_LONG_KEY ? 0xfff : 0xff
	 *   [base+0x24] = 0x7fff
	 *   [base+0x00] = (row_used << 16 | col_used << 8) & 0xffff00
	 *                 | (flags & SPRD_KEYPAD_LONG_KEY ? 7 : 3)
	 * 这里 flags 取的是 struct sprd_keypad 的 +0x10，LONG_KEY 是 BIT(1)，
	 * 与原厂 tst w8,#2 / csel 完全对应。
	 */
	writel((priv->flags & SPRD_KEYPAD_LONG_KEY) ? 0xfff : 0xff,
	       priv->base + SPRD_KEYPAD_REG_CTRL2);
	writel(0x7fff, priv->base + SPRD_KEYPAD_REG_CTRL3);
	writel((((priv->row_used << 16) | (priv->col_used << 8)) & 0xffff00) |
	       ((priv->flags & SPRD_KEYPAD_LONG_KEY) ? 7 : 3),
	       priv->base + SPRD_KEYPAD_REG_CFG);
}

static irqreturn_t sprd_keypad_handler(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct sprd_keypad *priv = platform_get_drvdata(pdev);
	struct input_dev *input = priv->input;
	u32 row_status, col_status;
	int row_shift;
	int i;

	row_status = readl(priv->base + SPRD_KEYPAD_REG_STATUS);
	col_status = readl(priv->base + SPRD_KEYPAD_REG_COL);

	/* row_shift = fls(num_cols) - (num_cols 是 2 的幂 ? 1 : 0) */
	row_shift = 32 - __builtin_clz(priv->num_cols);
	if (priv->num_cols && !(priv->num_cols & (priv->num_cols - 1)))
		row_shift -= 1;

	writel(0xfff, priv->base + SPRD_KEYPAD_REG_INT_STS);

	for (i = 0; i < SPRD_KEYPAD_GROUPS; i++) {
		u32 bits = row_status >> i;
		u32 cols = col_status >> (i * 8);
		u32 idx = ((((col_status >> (i * 8 + 4)) & 0x7) << row_shift) +
			   (cols & 0x7));

		if (bits & BIT(0)) {
			input_event(input, EV_KEY, ((unsigned short *)input->keycode)[idx], 1);
			input_event(input, EV_SYN, 0, 0);
		}

		printk("[key-test]input_event_up press %d \n", bits & 1);

		if (bits & BIT(4)) {
			input_event(input, EV_KEY, ((unsigned short *)input->keycode)[idx], 0);
			input_event(input, EV_SYN, 0, 0);
		}
	}

	return IRQ_HANDLED;
}

static int sprd_keypad_suspend(struct device *dev)
{
	struct sprd_keypad *priv = dev_get_drvdata(dev);

	/* 开了唤醒就别把时钟关掉，否则按键wakeup 收不到 */
	if (dev->power.can_wakeup && dev->power.wakeup)
		return 0;

	clk_disable_unprepare(priv->clk_enable);
	clk_disable_unprepare(priv->clk_rtc);

	return 0;
}

static int sprd_keypad_resume(struct device *dev)
{
	struct sprd_keypad *priv = dev_get_drvdata(dev);
	int ret;

	if (dev->power.can_wakeup && dev->power.wakeup)
		return 0;

	ret = clk_prepare_enable(priv->clk_rtc);
	if (ret) {
		dev_err(&priv->input->dev, "enable rtc failed.\n");
		clk_disable_unprepare(priv->clk_enable);
		return ret;
	}

	ret = clk_prepare_enable(priv->clk_enable);
	if (ret) {
		dev_err(&priv->input->dev, "enable keypad failed.\n");
		clk_disable_unprepare(priv->clk_rtc);
		return ret;
	}

	sprd_keypad_hw_init(priv);

	return 0;
}

static int sprd_keypad_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_keypad *priv;
	struct resource *res;
	int row_shift;
	int ret, irq;
	int i, j;

	pr_info("sprd_keypad_probe enter\r\n");

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL | __GFP_ZERO);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out_print;
	}

	if (fwnode_property_read_u32_array(dev_fwnode(dev), "keypad,num-rows",
					   &priv->num_rows, 1) ||
	    fwnode_property_read_u32_array(dev_fwnode(dev), "keypad,num-columns",
					   &priv->num_cols, 1) ||
	    !priv->num_rows || !priv->num_cols) {
		dev_err(dev, "number of keypad rows/columns not specified\n");
		ret = -EINVAL;
		goto out_print;
	}

	if (priv->num_rows > SPRD_KEYPAD_MAX_ROWS ||
	    priv->num_cols > SPRD_KEYPAD_MAX_COLS) {
		dev_err(dev, "invalid num_rows or num_cols\n");
		ret = -EINVAL;
		goto out_print;
	}

	if (of_property_read_u32(dev->of_node, "debounce-interval",
				 &priv->debounce)) {
		priv->debounce = SPRD_KEYPAD_DEF_DEBOUNCE;
		dev_warn(dev, "parse debounce-interval failed.\n");
	}

	if (of_get_property(dev->of_node, "linux,repeat", NULL))
		priv->flags |= SPRD_KEYPAD_REPEAT;
	if (of_get_property(dev->of_node, "sprd,support_long_key", NULL))
		priv->flags |= SPRD_KEYPAD_LONG_KEY;
	if (of_get_property(dev->of_node, "wakeup-source", NULL))
		priv->flags |= SPRD_KEYPAD_WAKEUP;

	priv->clk_enable = devm_clk_get(dev, "enable");
	if (IS_ERR(priv->clk_enable)) {
		if (PTR_ERR(priv->clk_enable) != -EPROBE_DEFER)
			dev_err(dev, "get enable clk failed.\n");
		ret = PTR_ERR(priv->clk_enable);
		goto out_print;
	}

	priv->clk_rtc = devm_clk_get(dev, "rtc");
	if (IS_ERR(priv->clk_rtc)) {
		/*
		 * vendor 这里判的是另一只时钟的错误码（此时 clk_enable 已经成功，
		 * 所以条件恒真，等于总是打印），照抄不改。
		 */
		if (PTR_ERR(priv->clk_enable) != -EPROBE_DEFER)
			dev_err(dev, "get rtc clk failed.\n");
		ret = PTR_ERR(priv->clk_rtc);
		goto out_print;
	}

	priv->input = devm_input_allocate_device(dev);
	if (IS_ERR(priv->input)) {
		dev_err(dev, "alloc input dev failed.\n");
		pr_info("sprd_keypad_probe alloc input dev failed.\r\n");
		ret = PTR_ERR(priv->input);
		goto out_print;
	}

	priv->input->name = "sprd-keypad";
	priv->input->phys = "sprd-key/input0";

	ret = matrix_keypad_build_keymap(NULL, NULL, priv->num_rows,
					 priv->num_cols, NULL, priv->input);
	if (ret) {
		pr_info("sprd_keypad_probe matrix_keypad_build_keymap failed. ret=%d\r\n",
			ret);
		goto out_print;
	}

	row_shift = 32 - __builtin_clz(priv->num_cols);
	if (priv->num_cols && !(priv->num_cols & (priv->num_cols - 1)))
		row_shift -= 1;

	pr_info("sprd_keypad_probe before rows=%d cols=%d row_shift=%d\r\n",
		priv->num_rows, priv->num_cols, row_shift);

	/*
	 * 扫一遍 keymap，记下“哪些 row / 哪些 col 上真的按键”，硬件据此少扫
	 * 空线；掩码存在 priv->row_used / col_used。
	 */
	if (priv->num_rows) {
		u32 row_map = 0;
		u32 col_map = 0;

		for (i = 0; i < priv->num_rows; i++) {
			for (j = 0; j < priv->num_cols; j++) {
				if (!((unsigned short *)priv->input->keycode)[(i << row_shift) + j])
					continue;
				row_map |= BIT(i);
				col_map |= BIT(j);
			}
		}

		priv->row_used = row_map;
		priv->col_used = col_map;
	}

	if (priv->flags & SPRD_KEYPAD_REPEAT)
		set_bit(EV_REP, priv->input->evbit);

	input_set_drvdata(priv->input, priv);

	ret = clk_prepare_enable(priv->clk_rtc);
	if (ret) {
		dev_err(&priv->input->dev, "enable rtc failed.\n");
		goto out_print;
	}

	ret = clk_prepare_enable(priv->clk_enable);
	if (ret) {
		dev_err(&priv->input->dev, "enable keypad failed.\n");
		clk_disable_unprepare(priv->clk_rtc);
		goto out_print;
	}

	sprd_keypad_hw_init(priv);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0 && irq != -EPROBE_DEFER)
		dev_err(dev, "IRQ index %u not found\n", 0);

	if (irq < 0) {
		dev_err(dev, "get irq failed.\n");
		ret = irq;
		goto out_disable_clk;
	}

	ret = devm_request_threaded_irq(dev, irq, sprd_keypad_handler, NULL,
					IRQF_NO_SUSPEND, dev_name(dev), pdev);
	if (ret) {
		dev_err(dev, "request irq failed.\n");
		goto out_disable_clk;
	}

	ret = input_register_device(priv->input);
	if (ret) {
		dev_err(dev, "register input dev failed\n");
		goto out_disable_clk;
	}

	if ((priv->flags & SPRD_KEYPAD_WAKEUP) && dev)
		device_init_wakeup(dev, true);

	pr_info("sprd_keypad_probe ok\r\n");

	return 0;

out_disable_clk:
	clk_disable_unprepare(priv->clk_enable);
	clk_disable_unprepare(priv->clk_rtc);
out_print:
	pr_info("sprd_keypad_probe ret=%d\r\n", ret);
	return ret;
}

static int sprd_keypad_remove(struct platform_device *pdev)
{
	struct sprd_keypad *priv = platform_get_drvdata(pdev);
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0 && irq != -EPROBE_DEFER)
		dev_err(&pdev->dev, "IRQ index %u not found\n", 0);

	if (priv->flags & SPRD_KEYPAD_WAKEUP)
		device_init_wakeup(&pdev->dev, false);

	input_unregister_device(priv->input);

	devm_free_irq(&pdev->dev, irq, pdev);

	clk_disable_unprepare(priv->clk_enable);
	clk_disable_unprepare(priv->clk_rtc);

	return 0;
}

static const struct dev_pm_ops sprd_keypad_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sprd_keypad_suspend, sprd_keypad_resume)
};

static const struct of_device_id sprd_keypad_match[] = {
	{ .compatible = "sprd,sc9860-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_keypad_match);

static struct platform_driver sprd_keypad_driver = {
	.probe = sprd_keypad_probe,
	.remove = sprd_keypad_remove,
	.driver = {
		.name = "sprd-keypad",
		.of_match_table = sprd_keypad_match,
		.pm = &sprd_keypad_pm_ops,
	},
};

/*
 * 显式写出 init/exit：Image 里的符号名就是 sprd_keypad_driver_init(0x2115f7c)
 * 与 sprd_keypad_driver_exit(0x214a234)，且 __initcall_132_416_sprd_keypad_
 * driver_init6 的后缀 6 = device_initcall。用 module_platform_driver() 会由宏
 * 生成这两个函数名，校验工具就查不到定义行了。
 */
static int __init sprd_keypad_driver_init(void)
{
	return platform_driver_register(&sprd_keypad_driver);
}
module_init(sprd_keypad_driver_init);

static void __exit sprd_keypad_driver_exit(void)
{
	platform_driver_unregister(&sprd_keypad_driver);
}
module_exit(sprd_keypad_driver_exit);

MODULE_DESCRIPTION("Unisoc/Spreadtrum keypad driver");
MODULE_LICENSE("GPL v2");
