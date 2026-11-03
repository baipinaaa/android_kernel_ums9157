// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc/Spreadtrum UMP9620 audio power regulator "device only" module.
 *
 * Reconstruction status: PARTIAL.
 *   - control flow recovered completely from "init_module" (492 bytes of
 *     .init.text, the module has no other code);
 *   - the 13 regulator platform devices and their 13 private data blocks are
 *     reproduced below with the names and ids read out of .data and the
 *     relocation tables, but the internal layout of the 216-byte private
 *     block was not fully mapped (only three of its slots are non-zero:
 *     +0x08 -> "SRG_xxx", +0xb8 == 1, +0xc0 -> a 16-byte {int id; char *name}
 *     object).  Fill in the struct once the consumer driver
 *     (snd-soc-sprd-codec-ump9620.ko) has been analysed.
 *
 * The module is a "device declarator": it looks the UMP9620 audio codec node
 * up in the device tree and, only when that node is enabled, registers one
 * platform device per internal audio regulator so that the codec driver can
 * bind to them later.  On the Sunelan Q968 (UMS9157) the codec node is
 * status = "disabled" in the DT overlay, so this module only prints the
 * "is disabled" line and returns 0.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define AUDIO_CODEC_OF_COMPAT	"unisoc,ump9620-audio-codec"
#define AUDIO_POWER_DEV_NAME	"sprd-audio-power"

#define AP_ERR(fmt, ...)	printk(KERN_ERR "[ASoC:POWER] " fmt, ##__VA_ARGS__)
#define AP_INFO(fmt, ...)	printk(KERN_INFO "[ASoC:POWER] " fmt, ##__VA_ARGS__)

/* One internal audio regulator of the UMP9620 codec. */
struct sprd_audio_regulator {
	u32 id;
	char *name;
};

/*
 * The 216-byte private block.  Only the slots observed in the binary are
 * named here; everything else is padding that the codec driver may use.
 */
struct sprd_audio_regulator_pdata {
	u8		reserved0[8];
	const char	*srg_name;	/* +0x08  "SRG_VREG", ... */
	u8		reserved1[0xb0];
	u32		enable_is_bool;	/* +0xb8  always 1 */
	u32		pad;
	struct sprd_audio_regulator *reg;	/* +0xc0 */
};

#define SPRD_AUDIO_REG(_name, _desc)					\
	{								\
		.id = 0,						\
		.name = (_name),					\
	}

static struct sprd_audio_regulator srg_vreg = SPRD_AUDIO_REG("VREG", 0);
static struct sprd_audio_regulator srg_vb = SPRD_AUDIO_REG("VB", 0);
static struct sprd_audio_regulator srg_micbias3 = SPRD_AUDIO_REG("MICBIAS3", 0);
static struct sprd_audio_regulator srg_bias = SPRD_AUDIO_REG("BIAS", 0);
static struct sprd_audio_regulator srg_micbias1 = SPRD_AUDIO_REG("MICBIAS1", 0);
static struct sprd_audio_regulator srg_micbias2 = SPRD_AUDIO_REG("MICBIAS2", 0);
static struct sprd_audio_regulator srg_headmicbias = SPRD_AUDIO_REG("HEADMICBIAS", 0);
static struct sprd_audio_regulator srg_clk_dcl_32k = SPRD_AUDIO_REG("CLK_DCL_32K", 0);
static struct sprd_audio_regulator srg_dig_clk_intc = SPRD_AUDIO_REG("DIG_CLK_INTC", 0);
static struct sprd_audio_regulator srg_dahpl_chn = SPRD_AUDIO_REG("DAHPL_CHN", 0);
static struct sprd_audio_regulator srg_dig_clk_hid = SPRD_AUDIO_REG("DIG_CLK_HID", 0);
static struct sprd_audio_regulator srg_dcl = SPRD_AUDIO_REG("DCL", 0);
static struct sprd_audio_regulator srg_bg = SPRD_AUDIO_REG("BG", 0);

#define SPRD_AUDIO_PDATA(_srg_name, _reg)				\
	{								\
		.srg_name = (_srg_name),				\
		.enable_is_bool = 1,					\
		.reg = (_reg),						\
	}

static struct sprd_audio_regulator_pdata vreg_data = SPRD_AUDIO_PDATA("SRG_VREG", &srg_vreg);
static struct sprd_audio_regulator_pdata vb_data = SPRD_AUDIO_PDATA("SRG_VB", &srg_vb);
static struct sprd_audio_regulator_pdata micbias3_data = SPRD_AUDIO_PDATA("SRG_MICBIAS3", &srg_micbias3);
static struct sprd_audio_regulator_pdata bias_data = SPRD_AUDIO_PDATA("SRG_BIAS", &srg_bias);
static struct sprd_audio_regulator_pdata micbias1_data = SPRD_AUDIO_PDATA("SRG_MICBIAS1", &srg_micbias1);
static struct sprd_audio_regulator_pdata micbias2_data = SPRD_AUDIO_PDATA("SRG_MICBIAS2", &srg_micbias2);
static struct sprd_audio_regulator_pdata headmicbias_data = SPRD_AUDIO_PDATA("SRG_HEADMICBIAS", &srg_headmicbias);
static struct sprd_audio_regulator_pdata clk_dcl_32k_data = SPRD_AUDIO_PDATA("SRG_CLK_DCL_32K", &srg_clk_dcl_32k);
static struct sprd_audio_regulator_pdata dig_clk_intc_data = SPRD_AUDIO_PDATA("SRG_DIG_CLK_INTC", &srg_dig_clk_intc);
static struct sprd_audio_regulator_pdata dahpl_chn_data = SPRD_AUDIO_PDATA("SRG_DAHPL_CHN", &srg_dahpl_chn);
static struct sprd_audio_regulator_pdata dig_clk_hid_data = SPRD_AUDIO_PDATA("SRG_DIG_CLK_HID", &srg_dig_clk_hid);
static struct sprd_audio_regulator_pdata dcl_data = SPRD_AUDIO_PDATA("SRG_DCL", &srg_dcl);
static struct sprd_audio_regulator_pdata bg_data = SPRD_AUDIO_PDATA("SRG_BG", &srg_bg);

/*
 * Registration order as laid out in .data (13 entries, ids 1..13).
 */
static struct platform_device sprd_audio_regulator_devices[] = {
	{ .name = AUDIO_POWER_DEV_NAME, .id = 1,  .dev.platform_data = &vreg_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 2,  .dev.platform_data = &vb_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 3,  .dev.platform_data = &micbias3_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 4,  .dev.platform_data = &bias_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 5,  .dev.platform_data = &micbias1_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 6,  .dev.platform_data = &micbias2_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 7,  .dev.platform_data = &headmicbias_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 8,  .dev.platform_data = &clk_dcl_32k_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 9,  .dev.platform_data = &dig_clk_intc_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 10, .dev.platform_data = &dahpl_chn_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 11, .dev.platform_data = &dig_clk_hid_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 12, .dev.platform_data = &dcl_data },
	{ .name = AUDIO_POWER_DEV_NAME, .id = 13, .dev.platform_data = &bg_data },
};

static int __init sprd_audio_power_dev_init(void)
{
	struct device_node *np;
	int i, ret = 0;

	np = of_find_compatible_node(NULL, NULL, AUDIO_CODEC_OF_COMPAT);
	if (!np) {
		AP_INFO("there is no analog node!\n");
		return 0;
	}

	if (!of_device_is_available(np)) {
		AP_INFO(" node '" AUDIO_CODEC_OF_COMPAT "' is disabled.\n");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(sprd_audio_regulator_devices); i++) {
		ret = platform_device_register(&sprd_audio_regulator_devices[i]);
		if (ret < 0) {
			AP_ERR("ERR:Add Regulator %d Failed %d\n",
			       sprd_audio_regulator_devices[i].id, ret);
			break;
		}
	}

	return ret;
}

module_init(sprd_audio_power_dev_init);

MODULE_DESCRIPTION("SPRD audio power regulator device");
MODULE_AUTHOR("Jian Chen <jian.chen@spreadtrum.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" AUDIO_POWER_DEV_NAME);
