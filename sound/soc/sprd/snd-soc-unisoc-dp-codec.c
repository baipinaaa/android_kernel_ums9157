// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc/Spreadtrum ASoC DP (I2S) codec.
 *
 * Source-level reconstruction of the vendor module
 * "snd-soc-unisoc-dp-codec.ko" from the vendor_boot ramdisk of the Sunelan
 * Q968 (UMS9157 / T157).
 *
 * The codec is a thin shim: it registers a single playback DAI ("DP I2S")
 * and forwards startup / shutdown / hw_params / digital_mute to an ops table
 * that is handed over through the platform device's platform_data.  The
 * same table also exposes one ALSA control, "DP Data Width", which reads and
 * writes the negotiated sample width kept in the private data.
 *
 * Recovered layout of the private data (40 bytes, devm_kmalloc):
 *   0x00  three pointers copied verbatim from platform_data (the first one
 *         is the ops table used below)
 *   0x18  u32 channels   (default 2)
 *   0x1c  u32 rate       (default 48000)
 *   0x20  u32 bit_width  (default 24)
 *   0x24  u32 aif_type   (default 0)
 *
 * The ops table is indexed as:
 *   0x00 startup(dev)  0x08 shutdown(dev)
 *   0x10 hw_params(dev, &channels, channels, rate, bit_width, aif_type)
 *   0x18 digital_mute(dev, mute)
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>

#define SPRD_DP_DAI_NAME	"DP I2S"
#define SPRD_DP_MAX_WIDTH	24
#define SPRD_DP_MIN_WIDTH	16

struct device;

struct sprd_dp_ops {
	int (*startup)(struct device *dev);
	void (*shutdown)(struct device *dev);
	int (*hw_params)(struct device *dev, u32 *params, u32 channels,
			 u32 rate, u32 bit_width, u32 aif_type);
	int (*digital_mute)(struct device *dev, bool mute);
};

struct sprd_dp_platform_data {
	const struct sprd_dp_ops *ops;
	void *priv;
	void *data;
};

struct sprd_dp_data {
	const struct sprd_dp_ops *ops;
	void *priv;
	void *data;
	u32 channels;
	u32 rate;
	u32 bit_width;
	u32 aif_type;
};

static int sprd_dp_codec_probe(struct platform_device *pdev)
{
	struct sprd_dp_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct sprd_dp_data *dp;

	if (!pdata)
		return -EINVAL;

	dp = devm_kmalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->ops = pdata->ops;
	dp->priv = pdata->priv;
	dp->data = pdata->data;
	dp->channels = 2;
	dp->rate = 48000;
	dp->bit_width = SPRD_DP_MAX_WIDTH;
	dp->aif_type = 0;

	dev_set_drvdata(&pdev->dev, dp);
	dev_info(&pdev->dev, "%s: success\n", __func__);

	return snd_soc_register_component(&pdev->dev, &sprd_dp_codec,
					  &sprd_dp_dai, 1);
}

static int sprd_dp_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static int sprd_dp_dai_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sprd_dp_data *dp = dev_get_drvdata(dai->dev);
	int ret;

	if (!dp->ops->startup)
		return 0;

	ret = dp->ops->startup(dai->dev);
	if (ret)
		return ret;

	return 0;
}

static void sprd_dp_dai_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct sprd_dp_data *dp = dev_get_drvdata(dai->dev);

	if (!dp->ops->shutdown)
		return;

	dp->ops->shutdown(dai->dev);
}

static int sprd_dp_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct sprd_dp_data *dp = dev_get_drvdata(dai->dev);

	dp->channels = params_channels(params);
	dp->rate = params_rate(params);

	dev_info(dai->dev, "%s: channel (%u) rate (%u) width (%u) aif_type (%u)\n",
		 __func__, dp->channels, dp->rate, dp->bit_width,
		 dp->aif_type);

	if (!dp->ops->hw_params)
		return 0;

	return dp->ops->hw_params(dai->dev, &dp->channels, dp->channels,
				  dp->rate, dp->bit_width, dp->aif_type);
}

static int sprd_dp_dai_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct sprd_dp_data *dp = dev_get_drvdata(dai->dev);

	if (!dp->ops->digital_mute)
		return 0;

	return dp->ops->digital_mute(dai->dev, mute != 0);
}

static int sprd_dp_get_bit_width(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sprd_dp_data *dp = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = dp->bit_width;

	return 0;
}

static int sprd_dp_set_bit_width(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sprd_dp_data *dp = snd_soc_component_get_drvdata(component);
	unsigned int width = ucontrol->value.integer.value[0];

	if (width < SPRD_DP_MIN_WIDTH)
		width = SPRD_DP_MIN_WIDTH;

	dp->bit_width = width;

	return 0;
}

static const struct snd_kcontrol_new sprd_dp_snd_controls[] = {
	SOC_SINGLE_EXT("DP Data Width", 0, 0, SPRD_DP_MAX_WIDTH, 0,
		       sprd_dp_get_bit_width, sprd_dp_set_bit_width),
};

static const struct snd_soc_dai_ops sprd_dp_dai_ops = {
	.digital_mute	= sprd_dp_dai_digital_mute,
	.startup	= sprd_dp_dai_startup,
	.shutdown	= sprd_dp_dai_shutdown,
	.hw_params	= sprd_dp_dai_hw_params,
};

static struct snd_soc_dai_driver sprd_dp_dai = {
	.name = SPRD_DP_DAI_NAME,
	.playback = {
		.stream_name	= "DP I2S Playback",
		.formats	= SNDRV_PCM_FMTBIT_S32_LE,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.channels_min	= 1,
		.channels_max	= 8,
	},
	.ops = &sprd_dp_dai_ops,
};

static const struct snd_soc_component_driver sprd_dp_codec = {
	.controls	= sprd_dp_snd_controls,
	.num_controls	= ARRAY_SIZE(sprd_dp_snd_controls),
};

static struct platform_driver sprd_dp_codec_driver = {
	.probe	= sprd_dp_codec_probe,
	.remove	= sprd_dp_codec_remove,
	.driver	= {
		.name = "sprd-dp-codec",
	},
};
module_platform_driver(sprd_dp_codec_driver);

MODULE_DESCRIPTION("SPRD ASoC DP Codec Driver");
MODULE_AUTHOR("Bin Pang <bin.pang@unisoc.com>");
MODULE_LICENSE("GPL");
