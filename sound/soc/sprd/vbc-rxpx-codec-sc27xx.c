// SPDX-License-Identifier: GPL-2.0
/*
 * vbc-rxpx-codec-sc27xx.c  --  Unisoc VBC RxPx codec machine driver
 *
 * Reconstructed from snd-soc-sprd-card.ko (vendor kernel 5.4.210, T157).
 * Original file: .../audio_driver/sprd/machine/sprd_card/vbc-rxpx-codec-sc27xx.c
 *
 * Function map (binary .text offsets):
 *   0x0430   516 vbc_rxpx_codec_sc27xx_probe
 *   0x0634    96 vbc_rxpx_codec_sc27xx_late_probe
 *   0x06b4   936? (not in this file) ...
 *   0x07a4   116 dfm_params
 *   0x0818    48 dfm_priv_get
 *
 * Data:
 *   .rodata+0x1ed8 vbc_rxpx_codec_sc27xx_of_match[6]   (0x4b0 bytes)
 *   .rodata+0x2440 _sprd_asoc_card_widgets[12]
 *   .rodata+0x3340 _sprd_asoc_card_controls[17]
 *   .data+0x000    vbc_rxpx_codec_sc27xx_driver
 */

#define pr_fmt(fmt) "[ASoC:BOARD] " fmt

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "sprd-asoc-card-utils.h"

/* vendor .bss+0x000 */
static void *priv_dfm;

void *dfm_priv_get(void)
{
	return priv_dfm;
}
EXPORT_SYMBOL_GPL(dfm_priv_get);

static int dfm_params(struct snd_soc_card *card, int sample_rate,
		      int *hw_rate)
{
	int rate;

	pr_debug("%s\n", "dfm_params");
	pr_debug("%s\n", "dfm_rate");

	if (!hw_rate) {
		pr_err("ERR: dfm sample rate refine failed!\n");
		return -EINVAL;
	}

	rate = sample_rate;
	pr_debug("%s: hw_rate:%d,  sample_rate:%d\n", "dfm_params", *hw_rate,
		 sample_rate);

	*hw_rate = rate;
	return 0;
}

/* ---------------------------------------------------------------------- */
/* smartamp boost control (vendor .rodata+0x3890 / 0x3900)                */
/* ---------------------------------------------------------------------- */
static const char * const smartamp_boost_texts[] = {
	"Off", "Bypass", "Unmute",
};

static const struct soc_enum smartamp_boost_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(smartamp_boost_texts),
			smartamp_boost_texts);

/* ---------------------------------------------------------------------- */
/* DAPM widgets (vendor _sprd_asoc_card_widgets, .rodata+0x2440)          */
/* ---------------------------------------------------------------------- */
extern int board_speaker_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event);

static const struct snd_soc_dapm_widget _sprd_asoc_card_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Ext Spk"),
	SND_SOC_DAPM_OUTPUT("Ext Spk1"),
	SND_SOC_DAPM_OUTPUT("Ext Ear"),
	SND_SOC_DAPM_OUTPUT("HeadPhone Jack"),
	SND_SOC_DAPM_INPUT("Line Jack"),
	SND_SOC_DAPM_INPUT("Mic Jack"),
	SND_SOC_DAPM_INPUT("Aux Mic Jack"),
	SND_SOC_DAPM_INPUT("Aux Mic2 Jack"),
	SND_SOC_DAPM_INPUT("HP Mic Jack"),
	SND_SOC_DAPM_INPUT("DMic Jack"),
	SND_SOC_DAPM_INPUT("DMic1 Jack"),
	SND_SOC_DAPM_INPUT("Dig FM Jack"),
};

const struct snd_soc_dapm_widget sprd_asoc_card_widgets[] =
	_sprd_asoc_card_widgets;
EXPORT_SYMBOL_GPL(sprd_asoc_card_widgets);

/* ---------------------------------------------------------------------- */
/* kcontrols (vendor _sprd_asoc_card_controls, .rodata+0x3340)            */
/*                                                                        */
/* 13 "<path> Function" switches handled by board_func_get/set and         */
/*  4 "<path> Mute" switches handled by board_mute_get/set.                */
/*  The private_value encodes the path index as ~reg.                      */
/* ---------------------------------------------------------------------- */
#define SPRD_FUNC_CTL(xname, idx)					\
	{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,			\
		.name = xname,						\
		.info = snd_soc_info_volsw,				\
		.get = board_func_get,					\
		.put = board_func_set,					\
		.private_value = SOC_SINGLE_VALUE(~(idx), 0, 1, 0) }

#define SPRD_MUTE_CTL(xname, idx)					\
	{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,			\
		.name = xname,						\
		.info = snd_soc_info_volsw,				\
		.get = board_mute_get,					\
		.put = board_mute_set,					\
		.private_value = SOC_SINGLE_VALUE(~(idx), 0, 1, 0) }

static const struct snd_kcontrol_new _sprd_asoc_card_controls[] = {
	SPRD_FUNC_CTL("Speaker Function", 0),
	SPRD_FUNC_CTL("Speaker1 Function", 1),
	SPRD_FUNC_CTL("Earpiece Function", 2),
	SPRD_FUNC_CTL("HeadPhone Function", 3),
	SPRD_FUNC_CTL("Line Function", 4),
	SPRD_FUNC_CTL("Mic Function", 5),
	SPRD_FUNC_CTL("Aux Mic Function", 6),
	SPRD_FUNC_CTL("Aux Mic2 Function", 7),
	SPRD_FUNC_CTL("HP Mic Function", 8),
	SPRD_FUNC_CTL("DMic Function", 9),
	SPRD_FUNC_CTL("DMic1 Function", 10),
	SPRD_FUNC_CTL("Digital FM Function", 11),
	SPRD_MUTE_CTL("Speaker Mute", 0),
	SPRD_MUTE_CTL("Speaker2 Mute", 1),
	SPRD_MUTE_CTL("Earpiece Mute", 2),
	SPRD_MUTE_CTL("HeadPhone Mute", 3),
	{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "SmartAmp Boost",
		.info = snd_soc_info_enum_double,
		.get = smartamp_boost_get,
		.put = smartamp_boost_set,
		.private_value = (unsigned long)&smartamp_boost_enum },
};

const struct snd_kcontrol_new sprd_asoc_card_controls[] =
	_sprd_asoc_card_controls;
EXPORT_SYMBOL_GPL(sprd_asoc_card_controls);

/* ---------------------------------------------------------------------- */
/* of match table (vendor .rodata+0x1ed8, 6 entries)                      */
/* ---------------------------------------------------------------------- */
static const struct sprd_asoc_data sprd_asoc_data_r3p0_2731 = {
	.type = 1, .codec = 1,
};
static const struct sprd_asoc_data sprd_asoc_data_r1p0v3_2721 = {
	.type = 2, .codec = 2,
};
static const struct sprd_asoc_data sprd_asoc_data_r1p0v3_2731 = {
	.type = 2, .codec = 1,
};
static const struct sprd_asoc_data sprd_asoc_data_r2p0_2723 = {
	.type = 0, .codec = 0,
};
static const struct sprd_asoc_data sprd_asoc_data_v4_2730 = {
	.type = 3, .codec = 3,
};

static const struct of_device_id vbc_rxpx_codec_sc27xx_of_match[] = {
	{ .compatible = "vbc-rxpx-codec-sc27xx", },
	{ .compatible = "unisoc,vbc-r3p0-codec-sc2731",
	  .data = &sprd_asoc_data_r3p0_2731 },
	{ .compatible = "unisoc,vbc-r1p0v3-codec-sc2721",
	  .data = &sprd_asoc_data_r1p0v3_2721 },
	{ .compatible = "unisoc,vbc-r1p0v3-codec-sc2731",
	  .data = &sprd_asoc_data_r1p0v3_2731 },
	{ .compatible = "unisoc,vbc-r2p0-codec-sc2723",
	  .data = &sprd_asoc_data_r2p0_2723 },
	{ .compatible = "unisoc,vbc-v4-codec-sc2730",
	  .data = &sprd_asoc_data_v4_2730 },
	{ }
};
MODULE_DEVICE_TABLE(of, vbc_rxpx_codec_sc27xx_of_match);

/* ---------------------------------------------------------------------- */
/* probe / late probe                                                     */
/* ---------------------------------------------------------------------- */
static int vbc_rxpx_codec_sc27xx_late_probe(struct snd_soc_card *card)
{
	sprd_asoc_board_comm_late_probe(card);

	snd_soc_dapm_ignore_suspend(&card->dapm, "inter HP PA");
	snd_soc_dapm_ignore_suspend(&card->dapm, "inter Spk PA");
	snd_soc_dapm_ignore_suspend(&card->dapm, "inter Ear PA");
	return 0;
}

static int vbc_rxpx_codec_sc27xx_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	struct sprd_asoc_card_priv *priv;
	const struct of_device_id *match;
	int ret, i;

	ret = asoc_sprd_card_probe(&pdev->dev, &card);
	if (ret) {
		pr_err("ERR: %s, asoc_sprd_card_probe failed!\n",
		       "vbc_rxpx_codec_sc27xx_probe");
		return ret;
	}

	for (i = 0; i < 4; i++) {
		if (sp_audio_debug_flag & 1)
			pr_debug("function id(%d), on=%d\n", i, 0);
		if (g_card_ext_hook && g_card_ext_hook->fn[i])
			g_card_ext_hook->fn[i](i, 0);
	}

	match = of_match_node(vbc_rxpx_codec_sc27xx_of_match,
			      pdev->dev.of_node);
	if (!match) {
		pr_err("Get the asoc board of device id failed!\n");
		return -ENODEV;
	}

	priv = sprd_card_priv(card);
	if (priv)
		priv->board_id = (unsigned long)match->data;

	card->controls = sprd_asoc_card_controls;
	card->num_controls = ARRAY_SIZE(_sprd_asoc_card_controls);
	card->dapm_widgets = sprd_asoc_card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(_sprd_asoc_card_widgets);
	card->late_probe = vbc_rxpx_codec_sc27xx_late_probe;

	ret = asoc_sprd_register_card(card, &pdev->dev);
	if (ret < 0) {
		pr_err("ERR: %s, asoc_sprd_register_card failed!\n",
		       "vbc_rxpx_codec_sc27xx_probe");
		sprd_asoc_card_ext_hook_free_gpio(&pdev->dev);
		return ret;
	}

	platform_set_drvdata(pdev, card);
	return 0;
}

static int vbc_rxpx_codec_sc27xx_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	sprd_asoc_card_ext_hook_free_gpio(&pdev->dev);
	return asoc_sprd_card_remove(card);
}

static struct platform_driver vbc_rxpx_codec_sc27xx_driver = {
	.driver = {
		.name = "snd_soc_sprd_card",
		.of_match_table = vbc_rxpx_codec_sc27xx_of_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = vbc_rxpx_codec_sc27xx_probe,
	.remove = vbc_rxpx_codec_sc27xx_remove,
};

static int __init vbc_rxpx_codec_sc27xx_init(void)
{
	int ret;

	ret = sprd_audio_debug_init();
	if (ret)
		return ret;

	return platform_driver_register(&vbc_rxpx_codec_sc27xx_driver);
}
module_init(vbc_rxpx_codec_sc27xx_init);

static void __exit vbc_rxpx_codec_sc27xx_exit(void)
{
	platform_driver_unregister(&vbc_rxpx_codec_sc27xx_driver);
}
module_exit(vbc_rxpx_codec_sc27xx_exit);

MODULE_DESCRIPTION("snd_soc_sprd_card");
MODULE_DESCRIPTION("Unisoc VBC RxPx codec machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:snd_soc_sprd_card");
