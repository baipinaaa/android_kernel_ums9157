// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc PDM (DMIC) codec driver, r2p0.
 *
 * Reconstructed from the vendor binary shipped on the Sunelan Q968
 * (UMS9157 / T157, build 10CN_0_480, Android 13):
 *
 *     vendor_boot_a.bin -> vendor_ramdisk
 *         lib/modules/snd-soc-sprd-pdm-r2p0.ko
 *         (.text 0x191c, 29 functions, sha256 recorded in
 *          ums9157-recon/data/modinfo.json)
 *
 * The vendor compiled this file from
 *
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/codec/sprd/pdm_r2p0/sprd-pdm.c
 *
 * (that path is embedded in the .ko .rodata; it proves that the audio
 * codec drivers live in a source tree outside the GKI kernel tree).
 *
 * Rebuild notes
 * -------------
 *  * All register offsets, register values, register wait times and the
 *    exact printk() format strings are taken from the binary.
 *  * `struct pdm_priv` field offsets below are the ones observed in the
 *    binary (devm_kmalloc() of 0x90 bytes in pdm_probe()).
 *  * Some of the vendor oddities are reproduced on purpose (they are
 *    marked "vendor quirk"), because the goal is to describe the shipped
 *    driver, not to clean it up.
 *  * The declared module name must stay "snd-soc-sprd-pdm-r2p0" so that
 *    vendor_ramdisk/lib/modules/modules.load keeps working.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/soc-dai.h>
#include <sound/tlv.h>

#define PDM_TAG		"[ASoC: pdm] "

/* audio IPC channel used to talk to the AGDSP (AMSG_CH_MISC) */
#define AMSG_CH_MISC	0xf

/* number of bytes a "read_regs" dump covers (single 0x10 byte window) */
#define PDM_REG_DUMP_LEN	0x61

/*
 * The vendor tree keeps the PMU register helpers in snd-soc-sprd-card.ko
 * (also a tree-external module); only their prototypes are needed here.
 */
extern struct regmap *arch_audio_get_agcp_ahb_gpr(void);
extern struct regmap *arch_audio_get_aon_apb_gpr(void);
extern void arch_audio_set_agcp_ahb_gpr(struct regmap *map);
extern void arch_audio_set_aon_apb_gpr(struct regmap *map);
extern int get_agcp_ahb_set_offset(void);
extern int get_agcp_ahb_clr_offset(void);
extern int get_sp_audio_debug_flag(void);

/* audio_sipc.ko */
extern int aud_ipc_ch_open(u8 channel);
extern int aud_ipc_ch_close(u8 channel);
extern int aud_send_cmd_no_param(u8 channel, u32 command, u32 p0, u32 p1,
				 u32 p2, u32 p3, int timeout);
/* agdsp_access.ko */
extern int agdsp_access_enable(void);
extern int agdsp_access_disable(void);

/*
 * Board flavour table referenced through of_device_id.data.
 * The binary only looks at the second u32 (board_version):
 *   0 -> UMS9230 register layout (agcp-ahb syscon + AHB gpr writes)
 *   1 -> UMS9620 register layout (aon-apb syscon + aon apb gpr writes)
 */
struct sprd_asoc_data {
	u32 device_id;
	u32 board_version;
};

static const struct sprd_asoc_data sprd_asoc_data_ums9230 = {
	.device_id = 0,
	.board_version = 0,
};

static const struct sprd_asoc_data sprd_asoc_data_ums9620 = {
	.device_id = 0,
	.board_version = 1,
};

/* private data, offsets are the binary ones, size 0x90 */
struct pdm_priv {
	u32 reserved0;			/* 0x00, never touched		*/
	struct platform_device *pdev;	/* 0x08				*/
	struct device *dev;		/* 0x10				*/
	struct regmap *agcp_ahb;	/* 0x18, ums9230 flavour only	*/
	struct regmap *agcp_apb;	/* 0x20, ums9620 flavour only	*/
	u32 phys_base;			/* 0x28, 32-bit copy		*/
	u32 set_offset;			/* 0x2c, "aon-apb-set-offset"	*/
	u32 clr_offset;			/* 0x30, "aon-apb-clr-offset"	*/
	u32 reserved1;			/* 0x34				*/
	const struct sprd_asoc_data *data;	/* 0x38			*/
	u32 reserved2;			/* 0x3c				*/
	u8 module_en;			/* 0x40, PDM module on/off	*/
	u8 pad0[3];
	u64 base_addr;			/* 0x50, 64-bit copy of phys	*/
	u32 size;			/* 0x58				*/
	u32 pad1;
	void __iomem *base;		/* 0x60, pdm register window	*/
	u64 clk_phys;			/* 0x68, clock register window	*/
	u32 clk_size;			/* 0x70				*/
	u32 pad2;
	void __iomem *clk_base;		/* 0x78				*/
	u32 adc2_l_dg;			/* 0x80, default 0x10		*/
	u32 adc2_r_dg;			/* 0x84, default 0x01		*/
	u32 flag0x88;			/* 0x88, "DMIC2 already asked"	*/
	u32 pad3;
};

static int pdm_module_en_ums9230(struct pdm_priv *pdm, u32 on);
static int pdm_module_en_ums9620(struct pdm_priv *pdm, u32 on);
static void pdm_dmic2_on(struct pdm_priv *pdm, int on);
static const struct of_device_id pdm_of_match[];

/* ------------------------------------------------------------------ */
/* helpers								*/
/* ------------------------------------------------------------------ */

static const char *pdm_get_event_name(int event)
{
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return "PRE_PMU";
	case SND_SOC_DAPM_POST_PMU:
		return "POST_PMU";
	case SND_SOC_DAPM_POST_PMD:
		return "POST_PMD";
	default:
		printk(KERN_ERR "[ASoC: pdm] %s fail, invalid event 0x%x\n",
		       __func__, event);
		return NULL;
	}
}

/* ------------------------------------------------------------------ */
/* PMU module enable							*/
/* ------------------------------------------------------------------ */

/*
 * UMS9230 flavour: the PDM block is gated through the AGCP AHB GPR
 * registers, two writes (set and clear of bit 6) with a 10us delay in
 * between, then the "on" bit in the set register.
 */
static int pdm_module_en_ums9230(struct pdm_priv *pdm, u32 on)
{
	struct regmap *map;
	int ret;

	if (on) {
		ret = agdsp_access_enable();
		if (ret) {
			printk(KERN_ERR "[ASoC: pdm] %s agdsp_access_enable error %d",
			       __func__, ret);
			return ret;
		}

		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_set_offset() + 8, 0x40);
		udelay(10);
		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_clr_offset() + 8, 0x40);
		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_set_offset(), 8);

		pdm->module_en = 1;
	} else {
		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_set_offset() + 8, 0x40);
		udelay(10);
		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_clr_offset() + 8, 0x40);
		map = arch_audio_get_agcp_ahb_gpr();
		regmap_write(map, get_agcp_ahb_clr_offset(), 8);

		pdm->module_en = 0;
		agdsp_access_disable();
	}

	return 0;
}

/*
 * UMS9620 flavour: the PDM block is gated through the AON APB GPR
 * registers.  "on" clears the two "keep in reset" bits, "off" sets
 * them again; both wait 10us after each step.  On power on the clock
 * gate CGM_PDM_SEL_CFG bit 0 is also enabled.
 */
static int pdm_module_en_ums9620(struct pdm_priv *pdm, u32 on)
{
	struct regmap *map;

	if (on) {
		int ret = agdsp_access_enable();

		if (ret) {
			printk(KERN_ERR "[ASoC: pdm] %s agdsp_access_enable error %d",
			       __func__, ret);
			return ret;
		}

		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x4, 0x4);
		udelay(10);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x4, 0x0);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x0, 0x10, 0x10);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x2, 0x2);
		udelay(10);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x2, 0x0);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x4, 0x2, 0x2);

		writel(readl(pdm->clk_base + 0x28) | 0x1,
		       pdm->clk_base + 0x28);
		pr_debug("[ASoC: pdm] %s CGM_PDM_SEL_CFG %0x", __func__,
			 readl(pdm->clk_base + 0x28));
		pdm->module_en = 1;
	} else {
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x2, 0x2);
		udelay(10);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x2, 0x0);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x4, 0x2, 0x0);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x4, 0x4);
		udelay(10);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x8, 0x4, 0x0);
		map = arch_audio_get_aon_apb_gpr();
		regmap_update_bits(map, 0x0, 0x10, 0x0);

		pdm->module_en = 0;
		agdsp_access_disable();
	}

	/* vendor quirk: the success paths return whatever the last regmap
	 * call returned, which is always 0 here.
	 */
	return 0;
}

static void pdm_module_en(struct pdm_priv *pdm, u32 on)
{
	int ret = agdsp_access_enable();

	if (ret) {
		printk(KERN_ERR "[ASoC: pdm] %s agdsp_access_enable error %d",
		       __func__, ret);
		return;
	}

	switch (pdm->data->board_version) {
	case 0:
		pdm_module_en_ums9230(pdm, on & 1);
		break;
	case 1:
		pdm_module_en_ums9620(pdm, on & 1);
		break;
	default:
		dev_err(pdm->dev, " %s wrong board version\n", __func__);
		break;
	}

	agdsp_access_disable();
}

/*
 * DMIC2 power sequence.  The first time DMIC2 is powered on the AGDSP is
 * asked (through the "no parameter" IPC command) to prepare the DMIC
 * path; the answer is latched in flag0x88 and the request is taken back
 * when DMIC2 is powered down.
 */
static void pdm_dmic2_on(struct pdm_priv *pdm, int on)
{
	printk(KERN_INFO "[ASoC: pdm] %s on_off %d\n", __func__, on & 1);

	if (!(on & 1)) {
		pdm_module_en(pdm, 0);

		if (pdm->flag0x88 == 1) {
			aud_send_cmd_no_param(AMSG_CH_MISC, 0, 0, 0, 0, 0, -1);
			pdm->flag0x88 = 0;
		}
		return;
	}

	if (!pdm->flag0x88) {
		if (aud_send_cmd_no_param(AMSG_CH_MISC, 1, 0, 0, 0, 0, -1))
			return;
		pdm->flag0x88 = 1;
	}

	pdm_module_en(pdm, 1);

	/* left channel gain, then right channel gain */
	writel(readl(pdm->base + 0x4c) | 0x200, pdm->base + 0x4c);
	writel((readl(pdm->base + 0x64) & ~0xff) | (pdm->adc2_l_dg & 0xff),
	       pdm->base + 0x64);
	writel(readl(pdm->base + 0x4c) | 0x400, pdm->base + 0x4c);
	writel((readl(pdm->base + 0x64) & ~0xff00) |
	       ((pdm->adc2_r_dg & 0xff) << 8), pdm->base + 0x64);

	if (pdm->data->board_version == 1) {
		writel((readl(pdm->base + 0x6c) & 0xffff0000) | 0x7,
		       pdm->base + 0x6c);
		writel((readl(pdm->base + 0x6c) & 0xffff) | 0x1ff0000,
		       pdm->base + 0x6c);
	} else {
		writel((readl(pdm->base + 0x6c) & 0xffff0000) | 0x3,
		       pdm->base + 0x6c);
		writel((readl(pdm->base + 0x6c) & 0xffff) | 0xff0000,
		       pdm->base + 0x6c);
	}

	writel((readl(pdm->base + 0x54) & ~0x7) | 0x5, pdm->base + 0x54);
	writel((readl(pdm->base + 0x54) & ~0x70) | 0x30, pdm->base + 0x54);
	writel((readl(pdm->base + 0x50) & ~0xff) | 0xc, pdm->base + 0x50);
	writel(readl(pdm->base + 0x1c) | 0x1, pdm->base + 0x1c);
	writel(0x148, pdm->base + 0x68);
	writel(readl(pdm->base) | 0x18000, pdm->base);
	writel(readl(pdm->base) | 0x4000, pdm->base);
	writel(readl(pdm->base + 0x4c) | 0x7e, pdm->base + 0x4c);
}

/* ------------------------------------------------------------------ */
/* DAPM									*/
/* ------------------------------------------------------------------ */

static int pdm_widget_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	printk(KERN_INFO "[ASoC: pdm] wname %s %s\n", w->name,
	       pdm_get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMD)
		return 0;

	printk(KERN_ERR "[ASoC: pdm] %s fail, invalid event 0x%x\n",
	       __func__, event);

	return -EINVAL;
}

/*
 * DMIC1 has no dedicated power sequence in the vendor driver: the
 * handler only traces the DAPM event.
 */
static int pdm_dmic1_power_on(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct pdm_priv *pdm = dev_get_drvdata(w->dapm->dev);

	dev_info(pdm->dev, "%s wname %s, %s\n", __func__, w->name,
		 pdm_get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU)
		return 0;
	if (event == SND_SOC_DAPM_POST_PMU)
		return 0;
	if (event == SND_SOC_DAPM_PRE_PMD)
		return 0;
	if (event == SND_SOC_DAPM_POST_PMD)
		return 0;

	printk(KERN_ERR "[ASoC: pdm] %s fail, invalid event 0x%x\n",
	       __func__, event);
	dev_err(pdm->dev, "unknown widget event type %d\n", event);

	return 0;
}

static int pdm_dmic2_power_on(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct pdm_priv *pdm = dev_get_drvdata(w->dapm->dev);

	dev_info(pdm->dev, "%s wname %s, %s\n", __func__, w->name,
		 pdm_get_event_name(event));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
	case SND_SOC_DAPM_POST_PMU:
		pdm_module_en(pdm, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		pdm_dmic2_on(pdm, 0);
		break;
	default:
		dev_err(pdm->dev, "unknown widget event type %d\n", event);
		break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* kcontrols								*/
/* ------------------------------------------------------------------ */

static int pdm_module_en_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pdm->module_en;

	return 0;
}

static int pdm_module_en_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);
	bool on = !!ucontrol->value.integer.value[0];

	pdm->module_en = on;
	pdm_module_en(pdm, on);
	dev_info(pdm->dev, "set module_en %d\n", pdm->module_en);

	return 0;
}

static int pdm_adc2_l_dg_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pdm->adc2_l_dg;

	return 0;
}

static int pdm_adc2_l_dg_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);
	int ret;

	ret = agdsp_access_enable();
	if (ret) {
		printk(KERN_ERR "[ASoC: pdm] %s agdsp_access_enable error %d",
		       __func__, ret);
		return ret;
	}

	pdm->adc2_l_dg = ucontrol->value.integer.value[0];

	if (pdm->module_en) {
		writel(readl(pdm->base + 0x4c) | 0x200, pdm->base + 0x4c);
		writel((readl(pdm->base + 0x64) & ~0xff) |
		       (pdm->adc2_l_dg & 0xff), pdm->base + 0x64);
	}

	agdsp_access_disable();
	dev_info(pdm->dev, "module_en %d, set adc2_l_dg %d\n",
		 pdm->module_en, pdm->adc2_l_dg);

	return ret;
}

static int pdm_adc2_r_dg_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = pdm->adc2_r_dg;

	return 0;
}

static int pdm_adc2_r_dg_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);
	int ret;

	ret = agdsp_access_enable();
	if (ret) {
		printk(KERN_ERR "[ASoC: pdm] %s agdsp_access_enable error %d",
		       __func__, ret);
		return ret;
	}

	pdm->adc2_r_dg = ucontrol->value.integer.value[0];

	if (pdm->module_en) {
		writel(readl(pdm->base + 0x4c) | 0x400, pdm->base + 0x4c);
		writel((readl(pdm->base + 0x64) & ~0xff00) |
		       ((pdm->adc2_r_dg & 0xff) << 8), pdm->base + 0x64);
	}

	agdsp_access_disable();
	dev_info(pdm->dev, "module_en %d, set adc2_r_dg %d\n",
		 pdm->module_en, pdm->adc2_r_dg);

	return ret;
}

static const struct snd_kcontrol_new pdm_snd_controls[] = {
	SOC_SINGLE_EXT("PDM MODULE EN", SND_SOC_NOPM, 0, 1, 0,
		       pdm_module_en_get, pdm_module_en_set),
	SOC_SINGLE_EXT("PDM ADC2L DG", SND_SOC_NOPM, 0, 255, 0,
		       pdm_adc2_l_dg_get, pdm_adc2_l_dg_set),
	SOC_SINGLE_EXT("PDM ADC2R DG", SND_SOC_NOPM, 0, 255, 0,
		       pdm_adc2_r_dg_get, pdm_adc2_r_dg_set),
};

/*
 * The binary carries three identical DAPM single switches ("Switch",
 * SND_SOC_NOPM, 0, 1); only entries [1] and [2] are referenced, entry
 * [0] is dead weight in the vendor object.
 */
static const struct snd_kcontrol_new pdm_dmic_switch[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_soc_dapm_widget pdm_dapm_widgets[] = {
	SND_SOC_DAPM_ADC_E("ADC PDM DMIC", "PDM-DMIC", SND_SOC_NOPM, 0, 0,
			   pdm_widget_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("PDM DMIC1 ON", SND_SOC_NOPM, 0, 0, NULL, 0,
			   pdm_dmic1_power_on,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH("PDM_DMIC1", SND_SOC_NOPM, 0, 1,
			    &pdm_dmic_switch[1]),
	/*
	 * vendor quirk: the binary has .subseq = 0xc9 on this widget where
	 * every other widget leaves it 0; kept as is.
	 */
	SND_SOC_DAPM_PGA_E("PDM DMIC2 ON", SND_SOC_NOPM, 0, 0, NULL, 0,
			   pdm_dmic2_power_on,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH("PDM_DMIC2", SND_SOC_NOPM, 0, 1,
			    &pdm_dmic_switch[2]),
	SND_SOC_DAPM_INPUT("PDM DMIC Pin"),
};

static const struct snd_soc_dapm_route pdm_intercon[] = {
	{ "PDM_DMIC1", "Switch", "PDM DMIC1 ON" },
	{ "PDM_DMIC2", "Switch", "PDM DMIC2 ON" },
};

/* ------------------------------------------------------------------ */
/* DAI									*/
/* ------------------------------------------------------------------ */

/*
 * Every DAI op below only traces the call: the real PDM programming is
 * done from the DAPM event handlers and from the sysfs/kcontrol
 * register writers.  This is exactly what the binary does, the five
 * functions are 64 bytes each.
 */
static int pdm_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
			      unsigned int freq, int dir)
{
	pr_debug("[ASoC: pdm] %s\n", __func__);

	return 0;
}

static int pdm_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	pr_debug("[ASoC: pdm] %s\n", __func__);

	return 0;
}

static int pdm_set_dai_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	pr_debug("[ASoC: pdm] %s\n", __func__);

	return 0;
}

static int pdm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	pr_debug("[ASoC: pdm] %s\n", __func__);

	return 0;
}

static int pdm_trigger(struct snd_pcm_substream *substream, int cmd,
		       struct snd_soc_dai *dai)
{
	pr_debug("[ASoC: pdm] %s\n", __func__);

	return 0;
}

static const struct snd_soc_dai_ops pdm_dai_ops = {
	.set_sysclk	= pdm_set_dai_sysclk,
	.set_fmt	= pdm_set_dai_fmt,
	.digital_mute	= pdm_set_dai_mute,
	.hw_params	= pdm_hw_params,
	.trigger	= pdm_trigger,
};

static struct snd_soc_dai_driver pdm_dai = {
	.name = "SPRD-PDM-DMIC",
	.id = 0,
	.capture = {
		.stream_name	= "PDM-DMIC",
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE,
		.rates		= SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				  SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				  SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				  SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &pdm_dai_ops,
};

/* ------------------------------------------------------------------ */
/* sysfs attributes							*/
/* ------------------------------------------------------------------ */

static ssize_t pdm_get_read_regs(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pdm_priv *pdm = dev_get_drvdata(dev);
	int len = 0;
	u32 i;

	if (!pdm->module_en) {
		/* fixed 0x25 byte answer copied straight from .rodata */
		static const char *const hint =
			"Error! Please add module_en to true!\n";

		memcpy(buf, hint, 0x25);
		return 0x25;
	}

	for (i = 0; i < 8; i++) {
		/* the binary compares the byte offset against 0x61, which lets
		 * eight 0x10 byte windows (0x00 .. 0x70) through
		 */
		len += sprintf(buf + len,
			       "0x%04x | 0x%04x 0x%04x 0x%04x 0x%04x\n",
			       pdm->phys_base + i * 0x10,
			       readl(pdm->base + i * 0x10),
			       readl(pdm->base + i * 0x10 + 4),
			       readl(pdm->base + i * 0x10 + 8),
			       readl(pdm->base + i * 0x10 + 0xc));
	}

	return len;
}

static ssize_t pdm_set_read_regs(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct pdm_priv *pdm = dev_get_drvdata(dev);
	u32 reg, val;

	if (!pdm->module_en) {
		printk(KERN_ERR "[ASoC: pdm] %s Error! Please add module_en to true!\n",
		       __func__);
		return count;
	}

	if (sscanf(buf, "%x %x", &reg, &val) != 2) {
		printk(KERN_ERR "[ASoC: pdm] %s set reg, parse data fail\n", __func__);
		return -EINVAL;
	}

	printk(KERN_INFO "[ASoC: pdm] %s set reg, reg 0x%x --> val 0x%x\n",
	       __func__, reg, val);
	writel(val, pdm->base + reg);

	return count;
}

static ssize_t pdm_get_module_en(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pdm_priv *pdm = dev_get_drvdata(dev);
	int len;

	len = sprintf(buf, "get_io %d\n", pdm->module_en);
	printk(KERN_INFO "[ASoC: pdm] %s module_en %d\n", __func__, pdm->module_en);

	return len;
}

static ssize_t pdm_set_module_en(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct pdm_priv *pdm = dev_get_drvdata(dev);
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		printk(KERN_ERR "[ASoC: pdm] %s fail %d, buf %s\n", __func__, ret,
		       buf);
		return ret;
	}

	pdm->module_en = val != 0;
	pdm_module_en(pdm, val != 0);
	printk(KERN_INFO "[ASoC: pdm] %s module_en %d\n", __func__, pdm->module_en);

	return count;
}

static DEVICE_ATTR(pdm_modu_en, 0660, pdm_get_module_en, pdm_set_module_en);
static DEVICE_ATTR(pdm_read_regs, 0660, pdm_get_read_regs, pdm_set_read_regs);

static struct attribute *pdm_attributes[] = {
	&dev_attr_pdm_modu_en.attr,
	&dev_attr_pdm_read_regs.attr,
	NULL,
};

static struct attribute_group pdm_attribute_group = {
	.attrs = pdm_attributes,
};

/* ------------------------------------------------------------------ */
/* component / platform driver						*/
/* ------------------------------------------------------------------ */

static int pdm_soc_probe(struct snd_soc_component *component)
{
	struct pdm_priv *pdm = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = &component->dapm;

	if (!dapm) {
		dev_err(pdm->dev, "spk dapm %p, sprd_pdm %p, NULL error\n",
			dapm, pdm);
		return -EINVAL;
	}

	dapm->idle_bias_off = 1;
	snd_soc_dapm_ignore_suspend(dapm, "PDM-DMIC");
	snd_soc_dapm_ignore_suspend(dapm, "PDM DMIC Pin");
	dev_info(pdm->dev, "%s\n", __func__);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_pdm = {
	.probe			= pdm_soc_probe,
	.controls		= pdm_snd_controls,
	.num_controls		= ARRAY_SIZE(pdm_snd_controls),
	.dapm_widgets		= pdm_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pdm_dapm_widgets),
	.dapm_routes		= pdm_intercon,
	.num_dapm_routes	= ARRAY_SIZE(pdm_intercon),
};

static int pdm_parse_dt_ums9230(struct pdm_priv *pdm)
{
	struct regmap *map;

	map = syscon_regmap_lookup_by_phandle(pdm->pdev->dev.of_node,
					      "sprd,syscon-agcp-ahb");
	if (IS_ERR(map))
		printk(KERN_ERR "[ASoC: pdm] %s error, get the codec ahb syscon failed %ld\n",
		       __func__, PTR_ERR(map));
	else {
		pdm->agcp_ahb = map;
		arch_audio_set_agcp_ahb_gpr(map);
	}

	return 0;
}

static int pdm_parse_dt_ums9620(struct pdm_priv *pdm)
{
	struct device_node *np = pdm->pdev->dev.of_node;
	struct resource *res;
	struct regmap *map;
	int ret;

	map = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-agcp-apb");
	if (IS_ERR(map)) {
		printk(KERN_ERR "[ASoC: pdm] %s error, get the codec aon apb syscon failed %ld\n",
		       __func__, PTR_ERR(map));
		return -EPROBE_DEFER;
	}
	pdm->agcp_apb = map;
	arch_audio_set_aon_apb_gpr(map);

	ret = of_property_read_variable_u32_array(np, "aon-apb-set-offset",
						  &pdm->set_offset, 1, 0);
	if (ret < 0) {
		if (get_sp_audio_debug_flag() & 1)
			printk(KERN_INFO "[ASoC: pdm] %s error, no set-offset attribute\n", __func__);
		pdm->set_offset = 0;
	}

	ret = of_property_read_variable_u32_array(np, "aon-apb-clr-offset",
						  &pdm->clr_offset, 1, 0);
	if (ret < 0) {
		if (get_sp_audio_debug_flag() & 1)
			printk(KERN_INFO "[ASoC: pdm] %s error, no clr-offset attribute\n", __func__);
		pdm->clr_offset = 0;
	}

	if (get_sp_audio_debug_flag() & 1)
		printk(KERN_INFO "[ASoC: pdm] %s set_offset 0x%x, clr_offset 0x%x\n", __func__,
		       pdm->set_offset, pdm->clr_offset);

	res = platform_get_resource(pdm->pdev, IORESOURCE_MEM, 1);
	if (!res) {
		printk(KERN_ERR "[ASoC: pdm] %s pdm clk reg parse error!\n",
		       __func__);
		return -EINVAL;
	}

	pdm->clk_phys = res->start;
	pdm->clk_size = resource_size(res);
	pdm->clk_base = devm_ioremap_resource(&pdm->pdev->dev, res);
	if (IS_ERR(pdm->clk_base)) {
		printk(KERN_ERR "[ASoC: pdm] %s ERR: pdm clk reg address ioremap_nocache error!\n",
		       __func__);
		return -ENOMEM;
	}

	printk(KERN_INFO "[ASoC: pdm] %s clk_membase 0x%lx, clk_memphys 0x%lx, end 0x%lx, reg_size 0x%x\n",
	       __func__, (unsigned long)pdm->clk_base,
	       (unsigned long)pdm->clk_phys, (unsigned long)res->end,
	       pdm->clk_size);

	return 0;
}

static int pdm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct pdm_priv *pdm;
	struct resource *res;
	int ret;

	printk(KERN_INFO "[ASoC: pdm] %s\n", __func__);

	ret = aud_ipc_ch_open(AMSG_CH_MISC);
	if (ret < 0) {
		printk(KERN_ERR "[ASoC: pdm] Failed to open AMSG_CH_MISC channel\n");
		return ret;
	}

	pdm = devm_kmalloc(&pdev->dev, sizeof(*pdm), GFP_KERNEL);
	if (!pdm) {
		ret = -ENOMEM;
		goto out;
	}

	match = of_match_node(pdm_of_match, np);
	if (!match) {
		printk(KERN_ERR "[ASoC: pdm] %s get board pdm of device id failed\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	pdm->flag0x88 = 0;
	pdm->pdev = pdev;
	pdm->dev = &pdev->dev;
	platform_set_drvdata(pdev, pdm);
	pdm->data = match->data;

	switch (pdm->data->board_version) {
	case 0:
		ret = pdm_parse_dt_ums9230(pdm);
		break;
	case 1:
		ret = pdm_parse_dt_ums9620(pdm);
		break;
	default:
		dev_err(&pdev->dev, " %s board version error\n", __func__);
		/* vendor quirk: positive value, not -ENODEV */
		ret = 0x13;
		goto out;
	}
	if (ret)
		goto out;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR "[ASoC: pdm] %s pdm reg parse error!\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	pdm->phys_base = res->start;
	pdm->base_addr = res->start;
	pdm->size = resource_size(res);
	pdm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdm->base)) {
		printk(KERN_ERR "[ASoC: pdm] %s ERR: pdm reg address ioremap_nocache error!\n",
		       __func__);
		ret = -ENOMEM;
		goto out;
	}

	printk(KERN_INFO "[ASoC: pdm] %s membase 0x%lx, memphys 0x%lx, end 0x%lx, reg_size 0x%x\n",
	       __func__, (unsigned long)pdm->base,
	       (unsigned long)pdm->base_addr, (unsigned long)res->end,
	       pdm->size);

	ret = sysfs_create_group(&pdev->dev, &pdm_attribute_group);
	if (ret)
		dev_err(&pdev->dev, "fail to create sysfs attr files\n");

	/* default ADC2 gains, the binary stores both in one 64-bit store */
	pdm->adc2_l_dg = 0x10;
	pdm->adc2_r_dg = 0x01;

	ret = snd_soc_register_component(&pdev->dev, &soc_codec_dev_pdm,
					 &pdm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, " %s register codec fail, %d\n", __func__,
			ret);
		goto out;
	}

	printk(KERN_INFO "[ASoC: pdm] %s successful\n", __func__);
out:
	return ret;
}

static int pdm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	sysfs_remove_group(&pdev->dev, &pdm_attribute_group);

	if (aud_ipc_ch_close(AMSG_CH_MISC) < 0)
		printk(KERN_ERR "[ASoC: pdm] Failed to close AMSG_CH_MISC channel\n");

	return 0;
}

static const struct of_device_id pdm_of_match[] = {
	{
		.compatible = "unisoc,pdm",
		.data = &sprd_asoc_data_ums9230,
	},
	{
		.compatible = "unisoc,pdm-ums9620",
		.data = &sprd_asoc_data_ums9620,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pdm_of_match);

static const struct platform_device_id pdm_id_table[] = {
	{ "pdm" },
	{ "pdm-ums9620" },
	{ }
};

static struct platform_driver pdm_driver = {
	.probe		= pdm_probe,
	.remove		= pdm_remove,
	.driver		= {
		.name		= "pdm",
		.of_match_table	= pdm_of_match,
	},
	.id_table	= pdm_id_table,
};

static int __init pdm_init(void)
{
	return platform_driver_register(&pdm_driver);
}

static void __exit pdm_exit(void)
{
	platform_driver_unregister(&pdm_driver);
}

module_init(pdm_init);
module_exit(pdm_exit);

MODULE_DESCRIPTION("SPRD PDM driver");
MODULE_AUTHOR("SPRD");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pdm");
MODULE_ALIAS("platform:pdm-ums9620");
MODULE_ALIAS("of:N*T*Cunisoc,pdm");
MODULE_ALIAS("of:N*T*Cunisoc,pdm-ums9620");
