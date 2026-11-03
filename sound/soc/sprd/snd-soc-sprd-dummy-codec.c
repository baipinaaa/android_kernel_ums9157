// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc/Spreadtrum dummy playback ASoC codec.
 *
 * Source-level reconstruction of the vendor module
 * "snd-soc-sprd-dummy-codec.ko" from the vendor_boot ramdisk of the Sunelan
 * Q968 (UMS9157 / T157).
 *
 * The module is tiny: module_init() registers a platform device named
 * "sprd-dummy-playback" (platform_device_info with id = -1) and a platform
 * driver with the same name, whose probe() calls
 * snd_soc_register_component() with a single, mostly unpopulated DAI.
 *
 * Recovered from the binary:
 *   - the component driver (248 bytes, .bss) is zero-initialised, i.e. the
 *     vendor really did register a component driver without a name or ops;
 *   - the DAI driver (256 bytes, .data) has name "sprd-dummy-playback-dai",
 *     playback.stream_name "Playback", formats 0x41557, rates 0x1ffe,
 *     rate_min = 0, rate_max = 0, channels_min = 1, channels_max = 0x180;
 *   - error path of module_init() unregisters the device again.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>

#define SPRD_DUMMY_DEV_NAME		"sprd-dummy-playback"
#define SPRD_DUMMY_DAI_NAME		"sprd-dummy-playback-dai"

static struct snd_soc_dai_driver dummy_playback_dai = {
	.name = SPRD_DUMMY_DAI_NAME,
	.playback = {
		.stream_name = "Playback",
		.formats = 0x41557,
		.rates = 0x1ffe,
		.rate_min = 0,
		.rate_max = 0,
		.channels_min = 1,
		.channels_max = 0x180,
	},
};

/* Zero-initialised in the vendor module, kept verbatim. */
static struct snd_soc_component_driver dummy_playback_codec;

static int sprd_dummy_playback_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_component(&pdev->dev, &dummy_playback_codec,
					  &dummy_playback_dai, 1);
}

static int sprd_dummy_playback_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static struct platform_driver sprd_dummy_driver = {
	.probe = sprd_dummy_playback_codec_probe,
	.remove = sprd_dummy_playback_codec_remove,
	.driver = {
		.name = SPRD_DUMMY_DEV_NAME,
	},
};

static struct platform_device *sprd_dummy_dev;

static int __init snd_soc_sprd_dummy_codec_init(void)
{
	struct platform_device_info pdevinfo;
	int ret;

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = SPRD_DUMMY_DEV_NAME;
	pdevinfo.id = -1;

	sprd_dummy_dev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(sprd_dummy_dev))
		return PTR_ERR(sprd_dummy_dev);

	ret = platform_driver_register(&sprd_dummy_driver);
	if (ret) {
		platform_device_unregister(sprd_dummy_dev);
		return ret;
	}

	return 0;
}

static void __exit snd_soc_sprd_dummy_codec_exit(void)
{
	platform_device_unregister(sprd_dummy_dev);
	platform_driver_unregister(&sprd_dummy_driver);
}

module_init(snd_soc_sprd_dummy_codec_init);
module_exit(snd_soc_sprd_dummy_codec_exit);

MODULE_DESCRIPTION("Dummy Playback ALSA SoC Codec Driver");
MODULE_AUTHOR("Peng Lee <peng.lee@spreadtrum.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("codec:dummy-playback");
