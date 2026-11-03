// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc UMP9620 audio codec power (regulator) driver.
 *
 * Rebuilt from the vendor binary that ships on the Sunelan Q968
 * (Unisoc UMS9157 / T157, board qogirn6pro, build 10CN_0_480):
 *
 *	modules/snd-soc-sprd-codec-ump9620-power.ko
 *	vermagic 5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt mod_unload
 *		 modversions aarch64
 *	modinfo  alias=platform:sprd-audio-power
 *		 author=Jian Chen <jian.chen@spreadtrum.com>
 *		 description=SPRD audio power regulator driver
 *		 license=GPL  depends=snd-soc-sprd-card
 *
 * The vendor built it out of tree; the build machine path embedded in the
 * module .rodata is:
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/codec/sprd/ump9620/power/sprd-audio-power.c
 *
 * This driver registers 13 "regulators" which are really individual analog
 * blocks of the UMP9620 codec (VREG, VB, BG, BIAS, MICBIAS1..3, HEADMICBIAS,
 * DCL, DIG_CLK_INTC, DIG_CLK_HID, CLK_DCL_32K, DAHPL_CHN).  The platform
 * devices are created by its companion module
 * snd-soc-sprd-codec-ump9620-power-dev.ko (see
 * snd-soc-sprd-codec-ump9620-power-dev.c).
 *
 * Layout of the per-regulator descriptor (vendor struct, 0x170 bytes) as
 * measured on the binary:
 *	0x00 u32 id (== desc.id)
 *	0x04 u32 reg		(0 means "use the callback")
 *	0x08 u32 mask		(also abused as 0/1 state for callback types)
 *	0x10 int (*enable)(void *)
 *	0x18 int (*disable)(void *)
 *	0x20 int (*sleep)(void *, unsigned int on)
 *	0x28 u32 vsel_reg
 *	0x2c u32 vsel_mask
 *	0x30 u32 vsel_shift
 *	0x34 u32 num_voltages
 *	0x38 const u16 *v_table
 *	0x40 u32 min_uV
 *	0x44 u32 max_uV
 *	0x48 u32 enable delay (udelay after enable)
 *	0x4c u32 disable delay (udelay after disable)
 *	0x50 struct regulator_desc desc (0x120 bytes in this kernel)
 *
 * On this particular board all 13 descriptors carry only id/reg/mask: there
 * is no voltage table, so the whole list_voltage()/set_voltage()/
 * get_voltage()/audio_power_set_voltage() path is dead code here.  It is
 * rebuilt anyway because it is part of the vendor driver.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>

/* arch_audio_codec_*() live in snd-soc-sprd-codec-ump9620.ko */
extern int get_sp_audio_debug_flag(void);

#define SPRD_AUDIO_POWER_ANALOG_NODE	"unisoc,ump9620-audio-codec"
#define SPRD_AUDIO_POWER_BASE		0x3000
#define SPRD_AUDIO_POWER_DEBUG_ADDR	0xd000

struct sprd_audio_power_info {
	u32 id;
	u32 reg;
	u32 mask;
	u32 reserved;
	int (*enable)(void *data);
	int (*disable)(void *data);
	int (*sleep)(void *data, unsigned int on);
	u32 vsel_reg;
	u32 vsel_mask;
	u32 vsel_shift;
	u32 num_voltages;
	const u16 *v_table;
	u32 min_uV;
	u32 max_uV;
	u32 enable_delay;
	u32 disable_delay;
	struct regulator_desc desc;
};

/* codec register window, resolved once from the analog codec device */
static struct regmap *codec_regmap;
static u64 codec_reg_offset = (u64)-1;

static int audio_codec_write_mask(u32 reg, u32 mask, u32 val)
{
	if (!codec_regmap || codec_reg_offset == (u64)-1) {
		printk(KERN_ERR "[ASoC:POWER] ERR: %s reg map or offset isn't initialized!",
		       "arch_audio_codec_write_mask");
		return -1;
	}

	return regmap_update_bits_base(codec_regmap,
				       reg + codec_reg_offset - SPRD_AUDIO_POWER_BASE,
				       mask, val, NULL, 0, 0);
}

static int audio_codec_read(u32 reg, u32 *val)
{
	if (!codec_regmap || codec_reg_offset == (u64)-1) {
		printk(KERN_ERR "[ASoC:POWER] ERR: %s reg map or offset isn't initialized!",
		       "arch_audio_codec_read");
		return -1;
	}

	return regmap_read(codec_regmap,
			   reg + codec_reg_offset - SPRD_AUDIO_POWER_BASE, val);
}

/* arch_audio_codec_read() + the vendor's second error message */
static int audio_codec_read_checked(u32 reg, u32 *val)
{
	int ret = audio_codec_read(reg, val);

	if (ret)
		printk(KERN_ERR "[ASoC:POWER] %s: sci_adi_read failed!",
		       "arch_audio_codec_read");

	return ret;
}

/*
 * arch_audio_codec_analog_*() are macros in the vendor's header, hence the
 * literal function names below.
 */
static int arch_audio_codec_analog_reg_enable(void)
{
	if (!codec_regmap) {
		printk(KERN_ERR "[ASoC:POWER] ERR: %s codec_regmap isn't initialized!",
		       "arch_audio_codec_analog_reg_enable");
		return -1;
	}

	return regmap_update_bits_base(codec_regmap, 0x2008, 0x10, 0x10,
				       NULL, 0, 0);
}

static int arch_audio_codec_analog_reg_disable(void)
{
	if (!codec_regmap) {
		printk(KERN_ERR "[ASoC:POWER] ERR: %s codec_regmap isn't initialized!",
		       "arch_audio_codec_analog_reg_disable");
		return -1;
	}

	return regmap_update_bits_base(codec_regmap, 0x2008, 0x10, 0,
				       NULL, 0, 0);
}

/* pulse the analog block out of reset, then release the self-clear bit */
static int arch_audio_codec_analog_reset(void)
{
	int ret;

	if (!codec_regmap) {
		printk(KERN_ERR "[ASoC:POWER] ERR: %s codec_regmap isn't initialized!",
		       "arch_audio_codec_analog_reset");
		return -1;
	}

	ret = regmap_update_bits_base(codec_regmap, 0x2014, 0x3100, 0x3100,
				      NULL, 0, 0);
	udelay(10);
	if (ret >= 0)
		regmap_update_bits_base(codec_regmap, 0x2014, 0x3100, 0,
					NULL, 0, 0);

	return ret;
}

/*
 * One register poke with the vendor's optional read-back dump.
 * Returns the raw error code coming from the codec accessors.
 */
static int sprd_audio_power_poke(struct sprd_audio_power_info *info, u32 off,
				 u32 mask, u32 val)
{
	int ret;

	ret = audio_codec_write_mask(codec_reg_offset + off, mask, val);

	if (get_sp_audio_debug_flag() & 4) {
		u32 readback = 0;

		audio_codec_read_checked(codec_reg_offset + off, &readback);
		printk(KERN_INFO "[ASoC:POWER] [0x%04x] U:[0x%08x] R:[0x%08x] MASK:0x%x NAME:%s",
		       (u32)((codec_reg_offset + off + SPRD_AUDIO_POWER_DEBUG_ADDR) & 0xffff),
		       val, readback, mask, info->desc.name);
	}

	return ret;
}

static int vreg_enable(void *data);
static int vreg_disable(void *data);
static int power_enable(void *data);
static int power_disable(void *data);
static int power_sleep(void *data, unsigned int on);

static int sprd_audio_power_list_voltage(struct regulator_dev *rdev,
					 unsigned int selector)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	u16 value;

	pr_debug("[ASoC:POWER] %s List Voltage", "sprd_audio_power_list_voltage");

	if (!info->v_table)
		return 0;

	if (info->num_voltages <= selector)
		return 0;

	value = info->v_table[selector];
	if (value & 0x8000)
		return 0;

	return (value & 0x7fff) * 1000;
}

/*
 * Map min_uV..max_uV onto the v_table and program the selector field.
 * Returns the selector index, or -EDOM when the window is not covered.
 */
static int audio_power_set_voltage(struct regulator_dev *rdev, int min_uV,
				   int max_uV)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	unsigned int selector;
	u32 mask, val;

	if (!info->v_table) {
		printk(KERN_ERR "[ASoC:POWER] %s info->v_table is NULL!",
		       "audio_power_set_voltage");
		return -EINVAL;
	}

	for (selector = 0; selector < info->num_voltages; selector++) {
		u16 value = info->v_table[selector];
		int uV;

		if (value & 0x8000)
			continue;

		uV = (value & 0x7fff) * 1000;
		if (uV < min_uV)
			continue;
		if (uV > max_uV)
			continue;

		pr_debug("[ASoC:POWER] %s: %dus", "audio_power_set_voltage", uV);

		mask = info->vsel_mask << info->vsel_shift;
		val = selector << info->vsel_shift;
		audio_codec_write_mask(info->vsel_reg, mask, val);

		if (get_sp_audio_debug_flag() & 4) {
			u32 readback = 0;

			audio_codec_read_checked(info->vsel_reg, &readback);
			printk(KERN_INFO "[ASoC:POWER] [0x%04x] U:[0x%08x] R:[0x%08x] MASK:0x%x NAME:%s",
			       (u32)((info->vsel_reg + SPRD_AUDIO_POWER_DEBUG_ADDR) & 0xffff),
			       val, readback, mask, info->desc.name);
		}

		return selector;
	}

	return -EDOM;
}

static int sprd_audio_power_set_voltage(struct regulator_dev *rdev,
					int min_uV, int max_uV,
					unsigned int *selector)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int ret;

	pr_debug("[ASoC:POWER] %s Set Voltage %d--%d",
		 "sprd_audio_power_set_voltage", min_uV, max_uV);

	if (!info->v_table)
		return 0;

	info->min_uV = min_uV;
	info->max_uV = max_uV;

	ret = audio_power_set_voltage(rdev, min_uV, max_uV);
	if (ret == -EDOM)
		return ret;

	*selector = ret;

	return ret;
}

static int sprd_audio_power_get_voltage(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	u32 value = 0;
	unsigned int selector;
	int ret;

	pr_debug("[ASoC:POWER] %s Get Voltage", "sprd_audio_power_get_voltage");

	if (!info->v_table)
		return 0;

	ret = audio_codec_read(info->vsel_reg, &value);
	if (ret) {
		/* the vendor keeps going with 0xffffffff as the value */
		value = (u32)-1;
		printk(KERN_ERR "[ASoC:POWER] %s: sci_adi_read failed!",
		       "sprd_audio_power_get_voltage");
	}

	selector = (value & info->vsel_mask) >> info->vsel_shift;

	return (info->v_table[selector] & 0x7fff) * 1000;
}

/*
 * Enable a single analog block.
 * Register type (info->reg != 0): set the mask bits.
 * Callback type: call the block specific helper and mark mask as state.
 */
static int sprd_audio_power_enable(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int ret;

	pr_debug("[ASoC:POWER] %s Enable", "sprd_audio_power_enable");

	if (info->reg) {
		ret = audio_codec_write_mask(info->reg, info->mask, info->mask);

		if (get_sp_audio_debug_flag() & 4) {
			u32 readback = 0;

			audio_codec_read_checked(info->reg, &readback);
			printk(KERN_INFO "[ASoC:POWER] [0x%04x] U:[0x%08x] R:[0x%08x] MASK:0x%x NAME:%s",
			       (u32)((info->reg + SPRD_AUDIO_POWER_DEBUG_ADDR) & 0xffff),
			       info->mask, readback, info->mask, info->desc.name);
		}
	} else if (info->enable) {
		ret = info->enable(info);
		info->mask = 1;
	}

	udelay(info->enable_delay);

	if (info->v_table && (info->min_uV | info->max_uV))
		audio_power_set_voltage(rdev, info->min_uV, info->max_uV);

	return ret;
}

static int sprd_audio_power_disable(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int ret = 0;

	pr_debug("[ASoC:POWER] %s Disable", "sprd_audio_power_disable");

	if (info->reg) {
		ret = audio_codec_write_mask(info->reg, info->mask, 0);

		if (get_sp_audio_debug_flag() & 4) {
			u32 readback = 0;

			audio_codec_read_checked(info->reg, &readback);
			printk(KERN_INFO "[ASoC:POWER] [0x%04x] U:[0x%08x] R:[0x%08x] MASK:0x%x NAME:%s",
			       (u32)((info->reg + SPRD_AUDIO_POWER_DEBUG_ADDR) & 0xffff),
			       0, readback, info->mask, info->desc.name);
		}
	} else if (info->disable) {
		ret = info->disable(info);
	}

	info->mask = 0;
	udelay(info->disable_delay);

	return ret;
}

static int sprd_audio_power_is_enabled(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	u32 value;

	pr_debug("[ASoC:POWER] %s Is Enabled", "sprd_audio_power_is_enabled");

	if (info->reg) {
		value = 0;
		audio_codec_read(info->reg, &value);
		value &= info->mask;
	} else {
		value = info->mask;
	}

	return value != 0;
}

static int sprd_audio_power_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int (*sleep)(void *data, unsigned int on) = info->sleep;

	pr_debug("[ASoC:POWER] %s Set Mode 0x%x",
		 "sprd_audio_power_set_mode", mode);

	if (!sleep)
		return 0;

	if (mode == REGULATOR_MODE_STANDBY)
		return sleep(info, 1);

	if (mode != REGULATOR_MODE_NORMAL)
		return -EINVAL;

	return sleep(info, 0);
}

static int sprd_audio_power_get_status(struct regulator_dev *rdev)
{
	pr_debug("[ASoC:POWER] %s Get Status", "sprd_audio_power_get_status");

	/* the vendor reports ON as REGULATOR_STATUS_NORMAL (== 4) */
	return (sprd_audio_power_is_enabled(rdev) != 0) << 2;
}

/*
 * VREG is disabled in the device tree on this board (the audio codec analog
 * node is status="disabled"), but the code is rebuilt as shipped.
 */
static int vreg_enable(void *data)
{
	struct sprd_audio_power_info *info = data;
	static u8 vreg_enabled;

	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC:POWER] audio power  %d  enable, %s\n",
		       info->desc.id, "vreg_enable");

	arch_audio_codec_analog_reg_enable();

	if (!vreg_enabled) {
		arch_audio_codec_analog_reset();
		vreg_enabled = 1;
	}

	return 0;
}

static int vreg_disable(void *data)
{
	/* the vendor left this unconditional line in */
	printk(KERN_ERR "[ASoC:POWER] %s %d\n", "vreg_disable", 546);

	arch_audio_codec_analog_reg_disable();

	return 0;
}

/* MICBIAS1/2/3 and HEADMICBIAS specific bring-up sequences */
static int power_enable(void *data)
{
	struct sprd_audio_power_info *info = data;
	int ret;

	switch (info->desc.id) {
	case 5: /* MICBIAS1 */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0x0010);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0001, 0x0001);
		if (ret < 0)
			goto err;
		break;
	case 6: /* MICBIAS2 */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0x0010);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0004, 0x0004);
		if (ret < 0)
			goto err;
		break;
	case 7: /* MICBIAS3 */
		ret = sprd_audio_power_poke(info, 0x04, 0x3008, 0x3008);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x14, 0xf000, 0x9000);
		if (ret < 0)
			goto err;
		break;
	case 8: /* HEADMICBIAS */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0x0010);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0002, 0x0002);
		if (ret < 0)
			goto err;
		break;
	default:
		ret = -EINVAL;
		WARN_ON_ONCE(1);
		goto err;
	}

	if (get_sp_audio_debug_flag() & 1) {
		printk("[ASoC:POWER] audio power %d enable ret:%d\n",
		       info->desc.id, ret);
		pr_debug("audio power %d enable ret:%d\n", info->desc.id, ret);
	}

	return ret;

err:
	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC:POWER] audio power %d enable errer:%d,\n",
		       info->desc.id, ret);

	return ret;
}

static int power_disable(void *data)
{
	struct sprd_audio_power_info *info = data;
	int ret;

	switch (info->desc.id) {
	case 5: /* MICBIAS1 */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0001, 0);
		if (ret < 0)
			goto err;
		break;
	case 6: /* MICBIAS2 */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0004, 0);
		if (ret < 0)
			goto err;
		break;
	case 7: /* MICBIAS3: only the power bit is dropped */
		ret = sprd_audio_power_poke(info, 0x04, 0x3008, 0);
		if (ret < 0)
			goto err;
		break;
	case 8: /* HEADMICBIAS */
		ret = sprd_audio_power_poke(info, 0x00, 0x0010, 0);
		if (ret < 0)
			goto err;
		ret = sprd_audio_power_poke(info, 0x04, 0x0002, 0);
		if (ret < 0)
			goto err;
		break;
	default:
		ret = -EINVAL;
		WARN_ON_ONCE(1);
		goto err;
	}

	if (get_sp_audio_debug_flag() & 1) {
		printk("[ASoC:POWER] audio power %d disable ret:%d\n",
		       info->desc.id, ret);
		/* yes, the vendor logs "sleep" from the disable path */
		pr_debug("audio power %d sleep ret:%d\n", info->desc.id, ret);
	}

	return ret;

err:
	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC:POWER] audio power %d disable errer:%d,\n",
		       info->desc.id, ret);

	return ret;
}

/* low power entry/exit for the analog blocks that support it */
static int power_sleep(void *data, unsigned int on)
{
	struct sprd_audio_power_info *info = data;
	int ret;

	switch (info->desc.id) {
	case 2: /* VB */
		ret = sprd_audio_power_poke(info, 0x00, 0x0400, on << 10);
		break;
	case 5: /* MICBIAS1 */
		ret = sprd_audio_power_poke(info, 0x04, 0x0010, on << 4);
		break;
	case 6: /* MICBIAS2 */
		ret = sprd_audio_power_poke(info, 0x04, 0x0020, on << 5);
		break;
	case 7: /* MICBIAS3 */
		ret = sprd_audio_power_poke(info, 0x04, 0x0040, on << 6);
		break;
	case 8: /* HEADMICBIAS: 0x80 to sleep, 0x1000 to wake */
		ret = sprd_audio_power_poke(info, 0x04, 0x1080, on ? 0x0080 : 0x1000);
		break;
	default:
		ret = -EINVAL;
		WARN_ON_ONCE(1);
		return ret;
	}

	if (get_sp_audio_debug_flag() & 1)
		printk("[ASoC:POWER] audio power %d sleep ret:%d\n",
		       info->desc.id, ret);

	return ret;
}

static const struct regulator_ops sprd_audio_power_ops = {
	.list_voltage	= sprd_audio_power_list_voltage,
	.set_voltage	= sprd_audio_power_set_voltage,
	.get_voltage	= sprd_audio_power_get_voltage,
	.enable		= sprd_audio_power_enable,
	.disable	= sprd_audio_power_disable,
	.is_enabled	= sprd_audio_power_is_enabled,
	.set_mode	= sprd_audio_power_set_mode,
	.get_status	= sprd_audio_power_get_status,
};

#define SPRD_AUDIO_POWER(_id, _name, _reg, _mask, _en, _dis, _slp)	\
	{								\
		.id	= (_id),					\
		.reg	= (_reg),					\
		.mask	= (_mask),					\
		.enable	= (_en),					\
		.disable = (_dis),					\
		.sleep	= (_slp),					\
		.desc	= {						\
			.name	= (_name),				\
			.id	= (_id),				\
			.ops	= &sprd_audio_power_ops,		\
			.owner	= THIS_MODULE,				\
		},							\
	}

/* index 0..12 == platform device id 1..13 (see the -dev module) */
static struct sprd_audio_power_info sprd_audio_power_regs[] = {
	SPRD_AUDIO_POWER(1,  "VREG",         0x0000, 0x0000, vreg_enable, vreg_disable, NULL),
	SPRD_AUDIO_POWER(2,  "VB",           0x3000, 0x2000, NULL, NULL, power_sleep),
	SPRD_AUDIO_POWER(3,  "BG",           0x3000, 0x0200, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(4,  "BIAS",         0x3000, 0x0010, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(5,  "MICBIAS1",     0x3004, 0x0001, NULL, NULL, power_sleep),
	SPRD_AUDIO_POWER(6,  "MICBIAS2",     0x3004, 0x0002, NULL, NULL, power_sleep),
	SPRD_AUDIO_POWER(7,  "MICBIAS3",     0x3004, 0x0004, NULL, NULL, power_sleep),
	SPRD_AUDIO_POWER(8,  "HEADMICBIAS",  0x0000, 0x0000, power_enable, power_disable, power_sleep),
	SPRD_AUDIO_POWER(9,  "DCL",          0x3100, 0x0200, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(10, "DIG_CLK_INTC", 0x3100, 0x0100, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(11, "DIG_CLK_HID",  0x3100, 0x0080, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(12, "CLK_DCL_32K",  0x3068, 0x2000, NULL, NULL, NULL),
	SPRD_AUDIO_POWER(13, "DAHPL_CHN",    0x3054, 0x0040, NULL, NULL, NULL),
};

/*
 * Resolve the codec regmap and the analog register window offset ("reg" of
 * the unisoc,ump9620-audio-codec node) once for all 13 regulators.
 */
static int sprd_audio_power_config_reg_vars(void)
{
	struct platform_device *adev;
	struct device_node *np;
	struct regmap *map;
	u32 offset;
	int ret;

	if (codec_regmap) {
		printk(KERN_INFO "[ASoC:POWER] %s regmap exist return 0",
		       "sprd_audio_power_config_reg_vars");
		return 0;
	}

	np = of_find_compatible_node(NULL, NULL, SPRD_AUDIO_POWER_ANALOG_NODE);
	if (!np) {
		printk(KERN_ERR "[ASoC:POWER] ERR: [%s] there must be a analog node!",
		       "sprd_audio_power_config_reg_vars");
		return -ENODEV;
	}

	adev = of_find_device_by_node(np);
	if (!adev) {
		printk(KERN_ERR "[ASoC:POWER] ERR: [%s] this node has no pdev?!",
		       "sprd_audio_power_config_reg_vars");
		return -EPROBE_DEFER;
	}

	/* the vendor takes the regmap from the codec device parent (offset 0x60) */
	map = dev_get_regmap(adev->dev.parent, NULL);
	if (!map) {
		printk(KERN_ERR "[ASoC:POWER] ERR: [%s] spi device is not ready yet!",
		       "sprd_audio_power_config_reg_vars");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32(np, "reg", &offset);
	if (ret) {
		dev_err(&adev->dev, "%s :no property of reg",
			"sprd_audio_power_config_reg_vars");
		return -ENXIO;
	}

	codec_regmap = map;
	codec_reg_offset = offset;

	return 0;
}

static int sprd_audio_power_probe(struct platform_device *pdev)
{
	struct sprd_audio_power_info *info = NULL;
	struct sprd_audio_power_info *copy;
	struct regulation_constraints *constraints;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	unsigned int min_sel = 0, max_sel = 0;
	int id = pdev->id;
	int ret = 0;
	int i;

	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC:POWER] Probe %d", id);

	ret = sprd_audio_power_config_reg_vars();
	if (ret)
		goto err_print;

	for (i = 0; i < ARRAY_SIZE(sprd_audio_power_regs); i++) {
		if (sprd_audio_power_regs[i].desc.id == id) {
			info = &sprd_audio_power_regs[i];
			break;
		}
	}

	if (!info) {
		ret = -EINVAL;
		goto err_print;
	}

	constraints = pdev->dev.platform_data;
	if (!constraints) {
		ret = -EINVAL;
		goto err_print;
	}

	copy = kmemdup(info, sizeof(*info), GFP_KERNEL);
	if (!copy) {
		ret = -ENOMEM;
		goto err_print;
	}

	constraints->valid_modes_mask |= REGULATOR_MODE_FAST | REGULATOR_MODE_STANDBY;
	constraints->valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE |
				       REGULATOR_CHANGE_MODE |
				       REGULATOR_CHANGE_STATUS;

	if (copy->v_table) {
		if (copy->num_voltages < 1) {
			min_sel = 0;
			max_sel = 0;
		} else {
			for (i = 0; i < copy->num_voltages; i++) {
				if (copy->v_table[i] < copy->v_table[min_sel])
					min_sel = i;
				if (copy->v_table[i] > copy->v_table[max_sel])
					max_sel = i;
			}
		}

		constraints->min_uV = (copy->v_table[min_sel] & 0x7fff) * 1000;
		constraints->max_uV = (copy->v_table[max_sel] & 0x7fff) * 1000;

		if (get_sp_audio_debug_flag() & 1)
			printk(KERN_INFO "[ASoC:POWER] min_uV:%d, max_uV:%d",
			       constraints->min_uV, constraints->max_uV);
	}

	/* the runtime window is tracked in the copy, not in the template */
	copy->min_uV = 0;
	copy->max_uV = 0;

	config.dev = &pdev->dev;
	config.init_data = constraints;
	config.driver_data = copy;
	config.of_node = pdev->dev.of_node;

	rdev = regulator_register(&copy->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "ERR:Can't Register %s, %ld",
			copy->desc.name, PTR_ERR(rdev));
		kfree(copy);
		return (int)PTR_ERR(rdev);
	}

	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC:POWER] Register %s Success!",
		       copy->desc.name);

	dev_set_drvdata(&pdev->dev, rdev);

	return 0;

err_print:
	printk(KERN_ERR "[ASoC:POWER] %s: config register variables failed!",
	       "sprd_audio_power_probe");

	return ret;
}

static int sprd_audio_power_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = dev_get_drvdata(&pdev->dev);
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);

	regulator_unregister(rdev);
	kfree(info);

	return 0;
}

static struct platform_driver sprd_audio_power_driver = {
	.probe	= sprd_audio_power_probe,
	.remove	= sprd_audio_power_remove,
	.driver	= {
		.name	= "sprd-audio-power",
	},
};

static int __init sprd_audio_power_init(void)
{
	return platform_driver_register(&sprd_audio_power_driver);
}
module_init(sprd_audio_power_init);

static void __exit sprd_audio_power_exit(void)
{
	platform_driver_unregister(&sprd_audio_power_driver);
}
module_exit(sprd_audio_power_exit);

MODULE_AUTHOR("Jian Chen <jian.chen@spreadtrum.com>");
MODULE_DESCRIPTION("SPRD audio power regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sprd-audio-power");
