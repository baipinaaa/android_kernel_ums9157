// SPDX-License-Identifier: GPL-2.0
/*
 * ASoC tables of the Unisoc ump9620 audio codec, rebuilt from the
 * snd-soc-sprd-codec-ump9620.ko shipped in vendor_boot.
 *
 * The whole DAPM description lives in .rodata as snd_kcontrol_new[],
 * snd_soc_dapm_widget[] and snd_soc_dapm_route[] arrays, so it was lifted
 * back mechanically with recon/dapm.py instead of being transcribed by hand:
 * 27 controls, 144 widgets and 170 routes, with every callback, TLV, private
 * mixer/enum descriptor and string in its original position.
 *
 * Mixer descriptors with .max = 0x7fffffff belong to the controls that carry
 * their own get/put handlers and never look at the limits.
 */
#include "snd-soc-sprd-codec-ump9620.h"


static const unsigned int c_tlv_18638[] = {0x1, 0x8, 0xfffff6a0, 0x1012c};
static const unsigned int c_tlv_18660[] = {0x1, 0x8, 0xfffff6a0, 0x1012c};
static const unsigned int c_tlv_18708[] = {0x1, 0x8, 0x0, 0x96};
static const unsigned int c_tlv_18718[] = {0x1, 0x8, 0xfffffa24, 0x4b};
static const unsigned int c_tlv_18740[] = {0x1, 0x8, 0x0, 0x12c};

static const struct soc_mixer_control c_mix_0 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_1 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_2 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_3 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_4 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_5 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_6 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_7 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_8 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_9 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_10 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_11 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 1, .rshift = 1,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_12 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 3, .rshift = 3,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_13 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_14 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 2, .rshift = 2,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_15 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 4, .rshift = 4,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_16 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = 12408, .rreg = 12408, .shift = 5, .rshift = 5,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_17 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_18 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_19 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_20 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_21 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_22 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_23 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_24 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = 12412, .rreg = 12412, .shift = 3, .rshift = 3,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_25 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = 12412, .rreg = 12412, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_26 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = 12412, .rreg = 12412, .shift = 6, .rshift = 6,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_27 = {
	.min = 0, .max = 7, .platform_max = 7,
	.reg = 12492, .rreg = 12492, .shift = 10, .rshift = 10,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_28 = {
	.min = 0, .max = 2, .platform_max = 2,
	.reg = 12368, .rreg = 12368, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 1, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_29 = {
	.min = 0, .max = 15, .platform_max = 15,
	.reg = 12436, .rreg = 12436, .shift = 12, .rshift = 12,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_30 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 5, .rshift = 5,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_31 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 4, .rshift = 4,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_32 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_33 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_34 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_35 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_36 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_37 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_38 = {
	.min = 0, .max = 15, .platform_max = 15,
	.reg = 12436, .rreg = 12436, .shift = 4, .rshift = 4,
	.sign_bit = 0, .invert = 1, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_39 = {
	.min = 0, .max = 15, .platform_max = 15,
	.reg = 12436, .rreg = 12436, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 1, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_40 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 3, .rshift = 3,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_41 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_42 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_43 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_44 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_45 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 2, .rshift = 2,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_46 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_47 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_48 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_49 = {
	.min = 0, .max = 15, .platform_max = 15,
	.reg = 12436, .rreg = 12436, .shift = 8, .rshift = 8,
	.sign_bit = 0, .invert = 1, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_50 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_51 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 1, .rshift = 1,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_52 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_53 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_54 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_55 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_56 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_57 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_58 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_59 = {
	.min = 0, .max = 1, .platform_max = 1,
	.reg = -1, .rreg = -1, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_60 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const struct soc_mixer_control c_mix_61 = {
	.min = 0, .max = 2147483647, .platform_max = 2147483647,
	.reg = 0, .rreg = 0, .shift = 0, .rshift = 0,
	.sign_bit = 0, .invert = 0, .autodisable = 0,
};

static const char * const c_enum_62_texts[] = {};
static const struct soc_enum c_enum_62 = {
	.reg = 4096, .shift_l = 14, .shift_r = 14, .items = 3, .mask = 3,
	.texts = c_enum_62_texts,
};

static const char * const c_enum_63_texts[] = {};
static const struct soc_enum c_enum_63 = {
	.reg = 4096, .shift_l = 8, .shift_r = 8, .items = 3, .mask = 3,
	.texts = c_enum_63_texts,
};

static const char * const c_enum_64_texts[] = {};
static const struct soc_enum c_enum_64 = {
	.reg = -1, .shift_l = 0, .shift_r = 0, .items = 2, .mask = 1,
	.texts = c_enum_64_texts,
};

static const char * const c_enum_65_texts[] = {};
static const struct soc_enum c_enum_65 = {
	.reg = -1, .shift_l = 0, .shift_r = 0, .items = 2, .mask = 1,
	.texts = c_enum_65_texts,
};

static const char * const c_enum_66_texts[] = {};
static const struct soc_enum c_enum_66 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 10, .mask = 0,
	.texts = c_enum_66_texts,
};

static const char * const c_enum_67_texts[] = {};
static const struct soc_enum c_enum_67 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 2, .mask = 0,
	.texts = c_enum_67_texts,
};

static const char * const c_enum_68_texts[] = {};
static const struct soc_enum c_enum_68 = {
	.reg = -1, .shift_l = 0, .shift_r = 0, .items = 4, .mask = 3,
	.texts = c_enum_68_texts,
};

static const char * const c_enum_69_texts[] = {};
static const struct soc_enum c_enum_69 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 4, .mask = 0,
	.texts = c_enum_69_texts,
};

static const char * const c_enum_70_texts[] = {};
static const struct soc_enum c_enum_70 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 2, .mask = 0,
	.texts = c_enum_70_texts,
};

static const char * const c_enum_71_texts[] = {};
static const struct soc_enum c_enum_71 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 3, .mask = 0,
	.texts = c_enum_71_texts,
};

static const char * const c_enum_72_texts[] = {};
static const struct soc_enum c_enum_72 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 3, .mask = 0,
	.texts = c_enum_72_texts,
};

static const char * const c_enum_73_texts[] = {};
static const struct soc_enum c_enum_73 = {
	.reg = 0, .shift_l = 0, .shift_r = 0, .items = 3, .mask = 0,
	.texts = c_enum_73_texts,
};

const struct snd_kcontrol_new sprd_codec_snd_controls[] = {
	{
		.iface = 2, .name = "Aud Codec Info",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_info_get, .put = NULL,
		.private_value = (unsigned long)&c_enum_66,
	},
	{
		.iface = 2, .name = "Inter PA Config",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_inter_pa_get, .put = sprd_codec_inter_pa_put,
		.private_value = (unsigned long)&c_mix_0,
	},
	{
		.iface = 2, .name = "DAC LRCLK Select",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_dac_lrclk_sel_get, .put = sprd_codec_dac_lrclk_sel_put,
		.private_value = (unsigned long)&c_enum_67,
	},
	{
		.iface = 2, .name = "ADC LRCLK Select",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_adc_lrclk_sel_get, .put = sprd_codec_adc_lrclk_sel_put,
		.private_value = (unsigned long)&c_enum_67,
	},
	{
		.iface = 2, .name = "ADC1 LRCLK Select",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_adc1_lrclk_sel_get, .put = sprd_codec_adc1_lrclk_sel_put,
		.private_value = (unsigned long)&c_enum_67,
	},
	{
		.iface = 2, .name = "HP mix mode",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_get_hp_mix_mode, .put = sprd_codec_set_hp_mix_mode,
		.private_value = (unsigned long)&c_mix_60,
	},
	{
		.iface = 2, .name = "SPK DG fall",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_spk_dg_fall_get, .put = sprd_codec_spk_dg_fall_set,
		.private_value = (unsigned long)&c_mix_61,
	},
	{
		.iface = 2, .name = "DAS Input Mux",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_das_input_mux_get, .put = sprd_codec_das_input_mux_put,
		.private_value = (unsigned long)&c_enum_68,
	},
	{
		.iface = 2, .name = "TEST_FIXED_RATE",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_fixed_rate_get, .put = sprd_codec_fixed_rate_put,
		.private_value = (unsigned long)&c_mix_1,
	},
	{
		.iface = 2, .name = "Codec Digital Access Disable",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = dig_access_disable_get, .put = dig_access_disable_put,
		.private_value = (unsigned long)&c_mix_2,
	},
	{
		.iface = 2, .name = "IVSENCE_DMIC_SEL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_ivsence_dmic_get, .put = sprd_codec_ivsence_dmic_put,
		.private_value = (unsigned long)&c_enum_69,
	},
	{
		.iface = 2, .name = "DALDO BYPASS",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_daldo_bypass_get, .put = sprd_codec_daldo_bypass_put,
		.private_value = (unsigned long)&c_mix_3,
	},
	{
		.iface = 2, .name = "CLK 26M IN SEL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_clk_26m_in_sel_get, .put = sprd_codec_clk_26m_in_sel_put,
		.private_value = (unsigned long)&c_enum_70,
	},
	{
		.iface = 2, .name = "DRV SOFT T",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_drv_soft_t_get, .put = sprd_codec_drv_soft_t_put,
		.private_value = (unsigned long)&c_mix_4,
	},
	{
		.iface = 2, .name = "DAAOL BUF DCCAL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_daaol_buf_dccal_get, .put = sprd_codec_daaol_buf_dccal_put,
		.private_value = (unsigned long)&c_enum_71,
	},
	{
		.iface = 2, .name = "DAAOR BUF DCCAL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_daaor_buf_dccal_get, .put = sprd_codec_daaor_buf_dccal_put,
		.private_value = (unsigned long)&c_enum_71,
	},
	{
		.iface = 2, .name = "DAHP OS INV",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_dahp_os_inv_get, .put = sprd_codec_dahp_os_inv_put,
		.private_value = (unsigned long)&c_enum_67,
	},
	{
		.iface = 2, .name = "DAHP OS D",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_dahp_os_d_get, .put = sprd_codec_dahp_os_d_put,
		.private_value = (unsigned long)&c_mix_5,
	},
	{
		.iface = 2, .name = "DAHPL BUF DCCAL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_dahpl_buf_dccal_get, .put = sprd_codec_dahpl_buf_dccal_put,
		.private_value = (unsigned long)&c_enum_71,
	},
	{
		.iface = 2, .name = "DAHPR BUF DCCAL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_dahpr_buf_dccal_get, .put = sprd_codec_dahpr_buf_dccal_put,
		.private_value = (unsigned long)&c_enum_71,
	},
	{
		.iface = 2, .name = "RCV POP RAMP",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_rcv_pop_ramp_get, .put = sprd_codec_rcv_pop_ramp_put,
		.private_value = (unsigned long)&c_mix_6,
	},
	{
		.iface = 2, .name = "SDM_L SET",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_get_sdm_l, .put = sprd_codec_set_sdm_l,
		.private_value = (unsigned long)&c_mix_7,
	},
	{
		.iface = 2, .name = "SDM_H SET",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_get_sdm_h, .put = sprd_codec_set_sdm_h,
		.private_value = (unsigned long)&c_mix_8,
	},
	{
		.iface = 2, .name = "HP CHN TYPE",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_hp_chn_type_get, .put = sprd_codec_hp_chn_type_put,
		.private_value = (unsigned long)&c_enum_72,
	},
	{
		.iface = 2, .name = "ADD0 DATA SEL",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_enum_double, .get = sprd_codec_add0_data_sel_get, .put = sprd_codec_add0_data_sel_put,
		.private_value = (unsigned long)&c_enum_73,
	},
	{
		.iface = 2, .name = "BIAS Power",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = bias_power_get, .put = bias_power_put,
		.private_value = (unsigned long)&c_mix_9,
	},
	{
		.iface = 2, .name = "Codec Chip ID",
		.index = 0, .access = 0x0, .count = 0,
		.info = snd_soc_info_volsw, .get = sprd_codec_get_ana_chip_id, .put = NULL,
		.private_value = (unsigned long)&c_mix_10,
	},
};

const struct snd_soc_dapm_widget sprd_codec_dapm_widgets[] = {
	{ /* 0 */
		.id = 19, .name = "Digital Power",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
		.event = digital_power_event, .event_flags = 0x9,
	},
	{ /* 1 */
		.id = 19, .name = "Analog Power",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = analog_power_event, .event_flags = 0x9,
	},
	{ /* 2 */
		.id = 21, .name = "BG",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 3 */
		.id = 21, .name = "BIAS",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 4 */
		.id = 21, .name = "MICBIAS1",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 5 */
		.id = 21, .name = "MICBIAS2",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 6 */
		.id = 21, .name = "MICBIAS3",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 7 */
		.id = 21, .name = "HEADMICBIAS",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 8 */
		.id = 19, .name = "CP_LDO",
		.reg = 12288, .shift = 1, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 2,
	},
	{ /* 9 */
		.id = 19, .name = "ANA_CLK",
		.reg = 12392, .shift = 15, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 10 */
		.id = 19, .name = "CLK 26M IN SEL",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
		.event = clk_26m_in_sel_event, .event_flags = 0x9,
	},
	{ /* 11 */
		.id = 19, .name = "CLK_DCL_6M5",
		.reg = 12392, .shift = 12, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 12 */
		.id = 21, .name = "CLK_DCL_32K",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 13 */
		.id = 19, .name = "CLK_CP",
		.reg = 12392, .shift = 7, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 14 */
		.id = 19, .name = "CLK_CP_32K",
		.reg = 12392, .shift = 6, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 15 */
		.id = 19, .name = "CLK_IMPD",
		.reg = 12392, .shift = 10, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 16 */
		.id = 19, .name = "CLK_DAC",
		.reg = 12392, .shift = 8, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 17 */
		.id = 19, .name = "CLK_ADC",
		.reg = 12392, .shift = 9, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 18 */
		.id = 19, .name = "CLK_DIG_6M5",
		.reg = 12392, .shift = 11, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 19 */
		.id = 19, .name = "CLK_ADC_Norm",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 1,
	},
	{ /* 20 */
		.id = 19, .name = "DIG_CLK_DAC_BUF",
		.reg = 12544, .shift = 12, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
	},
	{ /* 21 */
		.id = 21, .name = "DCL",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 22 */
		.id = 19, .name = "DIG_CLK_DRV_SOFT",
		.reg = 12544, .shift = 1, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
		.event = audio_dcl_event, .event_flags = 0x2,
	},
	{ /* 23 */
		.id = 21, .name = "DIG_CLK_INTC",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 24 */
		.id = 21, .name = "DIG_CLK_HID",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
		.event = dapm_regulator_event, .event_flags = 0x9,
	},
	{ /* 25 */
		.id = 19, .name = "DIG_CLK_CFGA_PRT",
		.reg = 12544, .shift = 6, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 26 */
		.id = 19, .name = "DIG_CLK_HPDPOP",
		.reg = 12544, .shift = 4, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 27 */
		.id = 19, .name = "DIG_CLK_IMPD",
		.reg = 12544, .shift = 3, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 28 */
		.id = 19, .name = "DIG_CLK_RCV",
		.reg = 12544, .shift = 11, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 29 */
		.id = 19, .name = "DALDO EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 2,
		.event = daldo_en_event, .event_flags = 0x9,
	},
	{ /* 30 */
		.id = 19, .name = "SDM DC OS",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
		.event = sdm_dc_os_event, .event_flags = 0x6,
	},
	{ /* 31 */
		.id = 19, .name = "ADC_1_CLK",
		.reg = 12404, .shift = 5, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 32 */
		.id = 19, .name = "ADC_2_CLK",
		.reg = 12404, .shift = 4, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 33 */
		.id = 19, .name = "ADC_3_CLK",
		.reg = 12404, .shift = 14, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
	},
	{ /* 34 */
		.id = 19, .name = "VAD_AD_CLK",
		.reg = 12492, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
		.event = vad_clk_power_event, .event_flags = 0xf,
	},
	{ /* 35 */
		.id = 19, .name = "ADC_IBIAS",
		.reg = 12404, .shift = 11, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
	},
	{ /* 36 */
		.id = 19, .name = "ADC_VREF_BUF",
		.reg = 12404, .shift = 10, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
	},
	{ /* 37 */
		.id = 19, .name = "IVSense SRC",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
		.event = sprd_ivsense_src_event, .event_flags = 0x9,
	},
	{ /* 38 */
		.id = 4, .name = "ADC_1 Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_38[] = {
	{ .iface = 2, .name = "MIC1PGA_1 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "HPMICPGA_1 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 2, .kcontrol_news = c_wctl_38,
	},
	{ /* 39 */
		.id = 4, .name = "ADC_2 Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_39[] = {
	{ .iface = 2, .name = "MIC2PGA_2 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "HPMICPGA_2 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 2, .kcontrol_news = c_wctl_39,
	},
	{ /* 40 */
		.id = 4, .name = "ADC_3 Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_40[] = {
	{ .iface = 2, .name = "MIC3PGA_3 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "HPMICPGA_3 Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 2, .kcontrol_news = c_wctl_40,
	},
	{ /* 41 */
		.id = 15, .name = "VAD_SMIC1",
		.reg = 12492, .shift = 13, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_41[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_41,
	},
	{ /* 42 */
		.id = 15, .name = "VAD_SMIC2",
		.reg = 12492, .shift = 14, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_42[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_42,
	},
	{ /* 43 */
		.id = 15, .name = "VAD_SMIC3",
		.reg = 12492, .shift = 15, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_43[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_43,
	},
	{ /* 44 */
		.id = 15, .name = "AUD ADC0L",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x0, .off_val = 0x1,
static const struct snd_kcontrol_new c_wctl_44[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_44,
	},
	{ /* 45 */
		.id = 15, .name = "AUD ADC0R",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x0, .off_val = 0x1,
static const struct snd_kcontrol_new c_wctl_45[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_45,
	},
	{ /* 46 */
		.id = 15, .name = "AUD ADC1L",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x0, .off_val = 0x1,
static const struct snd_kcontrol_new c_wctl_46[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_46,
	},
	{ /* 47 */
		.id = 15, .name = "AUD ADC1R",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x0, .off_val = 0x1,
static const struct snd_kcontrol_new c_wctl_47[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_47,
	},
	{ /* 48 */
		.id = 6, .name = "ADC_1 Gain",
		.reg = 12404, .shift = 9, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_48[] = {
	{ .iface = 2, .name = "ADC_1 Capture Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_48,
	},
	{ /* 49 */
		.id = 6, .name = "ADC_2 Gain",
		.reg = 12404, .shift = 8, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_49[] = {
	{ .iface = 2, .name = "ADC_2 Capture Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_49,
	},
	{ /* 50 */
		.id = 6, .name = "ADC_3 Gain",
		.reg = 12404, .shift = 12, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_50[] = {
	{ .iface = 2, .name = "ADC_3 Capture Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_50,
	},
	{ /* 51 */
		.id = 6, .name = "VAD_AD Gain",
		.reg = 12492, .shift = 9, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_51[] = {
	{ .iface = 2, .name = "VAD Capture Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_51,
	},
	{ /* 52 */
		.id = 6, .name = "VAD_Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 53 */
		.id = 6, .name = "ADC_1 Switch",
		.reg = 12404, .shift = 7, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = adc_1_event, .event_flags = 0x3,
	},
	{ /* 54 */
		.id = 6, .name = "ADC_2 Switch",
		.reg = 12404, .shift = 6, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = adc_2_event, .event_flags = 0x3,
	},
	{ /* 55 */
		.id = 6, .name = "ADC_3 Switch",
		.reg = 12404, .shift = 13, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = adc_3_event, .event_flags = 0x3,
	},
	{ /* 56 */
		.id = 6, .name = "VAD_AD Switch",
		.reg = 12488, .shift = 3, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = vad_ad_event, .event_flags = 0x3,
	},
	{ /* 57 */
		.id = 6, .name = "ADie Digital ADCL Switch",
		.reg = 8456, .shift = 3, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 2,
	},
	{ /* 58 */
		.id = 6, .name = "ADie Digital ADCR Switch",
		.reg = 8456, .shift = 5, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 2,
	},
	{ /* 59 */
		.id = 6, .name = "Digital ADCL Switch",
		.reg = 4096, .shift = 1, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
	},
	{ /* 60 */
		.id = 6, .name = "Digital ADCR Switch",
		.reg = 4096, .shift = 3, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
	},
	{ /* 61 */
		.id = 6, .name = "Digital ADC1L Switch",
		.reg = 4096, .shift = 10, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
	},
	{ /* 62 */
		.id = 6, .name = "Digital ADC1R Switch",
		.reg = 4096, .shift = 11, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
	},
	{ /* 63 */
		.id = 8, .name = "ADC",
		.sname = "Normal-Capture-AP01",
		.reg = 65534, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = chan_event, .event_flags = 0x9,
	},
	{ /* 64 */
		.id = 8, .name = "ADC1",
		.sname = "Normal-Capture-AP23",
		.reg = 65534, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = chan_event, .event_flags = 0x9,
	},
	{ /* 65 */
		.id = 9, .name = "DAC",
		.sname = "Normal-Playback",
		.reg = 65535, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = chan_event, .event_flags = 0x9,
	},
	{ /* 66 */
		.id = 9, .name = "DAC",
		.sname = "Normal-Playback-AP23",
		.reg = 65535, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = chan_event, .event_flags = 0x9,
	},
	{ /* 67 */
		.id = 6, .name = "Digital DACL Switch",
		.reg = 4096, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = -4,
	},
	{ /* 68 */
		.id = 6, .name = "Digital DACR Switch",
		.reg = 4096, .shift = 2, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = -4,
	},
	{ /* 69 */
		.id = 6, .name = "ADie Digital DACL Switch",
		.reg = 8456, .shift = 2, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = -3,
	},
	{ /* 70 */
		.id = 6, .name = "ADie Digital DACR Switch",
		.reg = 8456, .shift = 4, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = -3,
	},
	{ /* 71 */
		.id = 6, .name = "DAC Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 72 */
		.id = 6, .name = "DAC Gain",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_72[] = {
	{ .iface = 2, .name = "DAC Playback Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_72,
	},
	{ /* 73 */
		.id = 6, .name = "DRV SOFT EN",
		.reg = 12452, .shift = 3, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 100,
	},
	{ /* 74 */
		.id = 6, .name = "DRV SOFT T",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
		.event = drv_soft_t_event, .event_flags = 0x6,
	},
	{ /* 75 */
		.id = 6, .name = "DAAO OS",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 104,
		.event = daao_os_event, .event_flags = 0x9,
	},
	{ /* 76 */
		.id = 6, .name = "AO Gain",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_76[] = {
	{ .iface = 2, .name = "AO Playback Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_codec_spk_pga_get, .put = sprd_codec_spk_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_76,
	},
	{ /* 77 */
		.id = 6, .name = "AO Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 78 */
		.id = 6, .name = "AO Virt2",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 79 */
		.id = 6, .name = "AOL Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 80 */
		.id = 4, .name = "AO Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_80[] = {
	{ .iface = 2, .name = "AOL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "AOR Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCLAOL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCRAOL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 4, .kcontrol_news = c_wctl_80,
	},
	{ /* 81 */
		.id = 6, .name = "AO CUR",
		.reg = 12460, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 82 */
		.id = 6, .name = "SPK PA",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x9,
	},
	{ /* 83 */
		.id = 3, .name = "AOL EAR Sel",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_83[] = {
	{ .iface = 2, .name = "AOL EAR Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x18688,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_83,
	},
	{ /* 84 */
		.id = 3, .name = "AOL EAR Sel2",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_84[] = {
	{ .iface = 2, .name = "AOL EAR Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x18688,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_84,
	},
	{ /* 85 */
		.id = 6, .name = "DAAOL EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 101,
		.event = daaol_en_event, .event_flags = 0x9,
	},
	{ /* 86 */
		.id = 6, .name = "DAAOR EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 101,
		.event = daaor_en_event, .event_flags = 0x9,
	},
	{ /* 87 */
		.id = 15, .name = "DA AOL",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0xa,
static const struct snd_kcontrol_new c_wctl_87[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_87,
	},
	{ /* 88 */
		.id = 15, .name = "DA AOR",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0xa,
static const struct snd_kcontrol_new c_wctl_88[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_88,
	},
	{ /* 89 */
		.id = 15, .name = "DA HPL",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0xa,
static const struct snd_kcontrol_new c_wctl_89[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_89,
	},
	{ /* 90 */
		.id = 15, .name = "DA HPR",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0xa,
static const struct snd_kcontrol_new c_wctl_90[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_90,
	},
	{ /* 91 */
		.id = 6, .name = "AO EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 108,
		.event = ao_en_event, .event_flags = 0xa,
	},
	{ /* 92 */
		.id = 6, .name = "SDAAOL AO",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 102,
		.event = sdaaol_ao_event, .event_flags = 0xa,
	},
	{ /* 93 */
		.id = 6, .name = "SDAAOR AO",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 102,
		.event = sdaaor_ao_event, .event_flags = 0xa,
	},
	{ /* 94 */
		.id = 6, .name = "SDAAOL RCV",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 102,
		.event = sdaaol_rcv_event, .event_flags = 0xa,
	},
	{ /* 95 */
		.id = 6, .name = "SDAHPL RCV",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 102,
		.event = sdahpl_rcv_event, .event_flags = 0x5,
	},
	{ /* 96 */
		.id = 6, .name = "HPRCV COM Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 101,
	},
	{ /* 97 */
		.id = 6, .name = "DAHPL EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 101,
		.event = dahpl_en_event, .event_flags = 0x9,
	},
	{ /* 98 */
		.id = 6, .name = "DAHPR EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 101,
		.event = dahpr_en_event, .event_flags = 0x9,
	},
	{ /* 99 */
		.id = 6, .name = "DAHP OS",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 104,
		.event = dahp_os_event, .event_flags = 0x6,
	},
	{ /* 100 */
		.id = 6, .name = "DNS",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 104,
		.event = dns_event, .event_flags = 0x9,
	},
	{ /* 101 */
		.id = 6, .name = "HPL Gain",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_101[] = {
	{ .iface = 2, .name = "HPL Playback Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_101,
	},
	{ /* 102 */
		.id = 6, .name = "HPR Gain",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_102[] = {
	{ .iface = 2, .name = "HPR Playback Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_102,
	},
	{ /* 103 */
		.id = 4, .name = "HPL Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_103[] = {
	{ .iface = 2, .name = "DACLHPL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "DACRHPL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCLHPL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCRHPL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 4, .kcontrol_news = c_wctl_103,
	},
	{ /* 104 */
		.id = 4, .name = "HPR Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_104[] = {
	{ .iface = 2, .name = "DACLHPR Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "DACRHPR Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCLHPR Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
	{ .iface = 2, .name = "ADCRHPR Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 4, .kcontrol_news = c_wctl_104,
	},
	{ /* 105 */
		.id = 6, .name = "HP DEPOP",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 96,
		.event = hp_depop_event, .event_flags = 0x5,
	},
	{ /* 106 */
		.id = 15, .name = "HP Virt",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0xa,
static const struct snd_kcontrol_new c_wctl_106[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_106,
	},
	{ /* 107 */
		.id = 6, .name = "HP CHN EN",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 110,
		.event = hp_chn_en_event, .event_flags = 0x6,
	},
	{ /* 108 */
		.id = 19, .name = "CP AD Cali",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
		.event = cp_ad_cmp_cali_event, .event_flags = 0x9,
	},
	{ /* 109 */
		.id = 19, .name = "CP",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 4,
		.event = cp_event, .event_flags = 0x9,
	},
	{ /* 110 */
		.id = 6, .name = "RCV DEPOP",
		.reg = 12668, .shift = 8, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 5,
		.event = codec_widget_event, .event_flags = 0x5,
	},
	{ /* 111 */
		.id = 6, .name = "EAR Gain",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_111[] = {
	{ .iface = 2, .name = "EAR Playback Volume", .index = 0, .access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE|SNDRV_CTL_ELEM_ACCESS_TLV_READ, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_pga_get, .put = sprd_pga_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_111,
	},
	{ /* 112 */
		.id = 4, .name = "EAR_HPL Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_112[] = {
	{ .iface = 2, .name = "DACHPL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_112,
	},
	{ /* 113 */
		.id = 4, .name = "EAR_AOL Mixer",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_113[] = {
	{ .iface = 2, .name = "DACAOL Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = sprd_mixer_get, .put = sprd_mixer_put, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_113,
	},
	{ /* 114 */
		.id = 6, .name = "EAR Switch",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 109,
		.event = ear_switch_event, .event_flags = 0x5,
	},
	{ /* 115 */
		.id = 3, .name = "HPL EAR Sel",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_115[] = {
	{ .iface = 2, .name = "HPL EAR Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x185b8,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_115,
	},
	{ /* 116 */
		.id = 3, .name = "HPL EAR Sel2",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_116[] = {
	{ .iface = 2, .name = "HPL EAR Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x185b8,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_116,
	},
	{ /* 117 */
		.id = 6, .name = "HPL Path",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x1,
	},
	{ /* 118 */
		.id = 6, .name = "EAR_HPL Path",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x1,
	},
	{ /* 119 */
		.id = 6, .name = "EAR_AOL Path",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.event = codec_widget_event, .event_flags = 0x1,
	},
	{ /* 120 */
		.id = 1, .name = "EAR Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 121 */
		.id = 1, .name = "HP Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 122 */
		.id = 1, .name = "SPK Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 123 */
		.id = 1, .name = "Virt Output Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 124 */
		.id = 15, .name = "Virt Output",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_124[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_124,
	},
	{ /* 125 */
		.id = 0, .name = "MIC Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 126 */
		.id = 0, .name = "MIC2 Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 127 */
		.id = 0, .name = "MIC3 Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 128 */
		.id = 0, .name = "HPMIC Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 129 */
		.id = 0, .name = "DMIC Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 130 */
		.id = 0, .name = "DMIC1 Pin",
		.reg = -1, .shift = 0, .mask = 0x0, .on_val = 0x0, .off_val = 0x0,
	},
	{ /* 131 */
		.id = 6, .name = "DMIC Switch",
		.reg = 4144, .shift = 6, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
	},
	{ /* 132 */
		.id = 6, .name = "DMIC1 Switch",
		.reg = 4144, .shift = 7, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 3,
	},
	{ /* 133 */
		.id = 15, .name = "ADC-DAC Adie Loop",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_133[] = {
	{ .iface = 2, .name = "switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_133,
	},
	{ /* 134 */
		.id = 15, .name = "ADC1-DAC Adie Loop",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_134[] = {
	{ .iface = 2, .name = "switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_134,
	},
	{ /* 135 */
		.id = 15, .name = "ADC-DAC Digital Loop",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_135[] = {
	{ .iface = 2, .name = "switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_135,
	},
	{ /* 136 */
		.id = 15, .name = "ADC1-DAC Digital Loop",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_136[] = {
	{ .iface = 2, .name = "switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_136,
	},
	{ /* 137 */
		.id = 6, .name = "ADC-DAC Adie Loop post",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 93,
		.event = adie_loop_event, .event_flags = 0xa,
	},
	{ /* 138 */
		.id = 6, .name = "ADC-DAC Digital Loop post",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
		.subseq = 93,
		.event = digital_loop_event, .event_flags = 0xa,
	},
	{ /* 139 */
		.id = 15, .name = "DA mode",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x0, .off_val = 0x1,
static const struct snd_kcontrol_new c_wctl_139[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_139,
	},
	{ /* 140 */
		.id = 2, .name = "Digital ADC In Sel",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_140[] = {
	{ .iface = 2, .name = "Digital ADC In Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x18548,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_140,
	},
	{ /* 141 */
		.id = 2, .name = "Digital ADC1 In Sel",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_141[] = {
	{ .iface = 2, .name = "Digital ADC1 In Sel", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_enum_double, .get = snd_soc_dapm_get_enum_double, .put = snd_soc_dapm_put_enum_double, .tlv.p = (void *)0x184c0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_141,
	},
	{ /* 142 */
		.id = 15, .name = "VAD_VirtL",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_142[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_142,
	},
	{ /* 143 */
		.id = 15, .name = "VAD_VirtR",
		.reg = -1, .shift = 0, .mask = 0x1, .on_val = 0x1, .off_val = 0x0,
static const struct snd_kcontrol_new c_wctl_143[] = {
	{ .iface = 2, .name = "Switch", .index = 0, .access = 0x0, .count = 0,
	  .info = snd_soc_info_volsw, .get = snd_soc_dapm_get_volsw, .put = snd_soc_dapm_put_volsw, .tlv.p = (void *)0x0,
	  .private_value = 0x0 },
};

		.num_kcontrols = 1, .kcontrol_news = c_wctl_143,
	},
};

const struct snd_soc_dapm_route sprd_codec_intercon[] = {
	{ "ANA_CLK", NULL, "Analog Power" },
	{ "CLK 26M IN SEL", NULL, "ANA_CLK" },
	{ "CLK_DCL_32K", NULL, "CLK 26M IN SEL" },
	{ "CLK_DCL_6M5", NULL, "CLK_DCL_32K" },
	{ "CLK_DIG_6M5", NULL, "CLK_DCL_6M5" },
	{ "ADC_IBIAS", NULL, "ADC_VREF_BUF" },
	{ "CLK_DAC", NULL, "CLK_DIG_6M5" },
	{ "CLK_DAC", NULL, "DCL" },
	{ "CLK_DAC", NULL, "DIG_CLK_DRV_SOFT" },
	{ "CLK_DAC", NULL, "Digital Power" },
	{ "CLK_DAC", NULL, "BG" },
	{ "CLK_DAC", NULL, "DIG_CLK_DAC_BUF" },
	{ "CLK_CP", NULL, "DALDO EN" },
	{ "CP_LDO", NULL, "CLK_CP" },
	{ "CP", NULL, "CP_LDO" },
	{ "CLK_CP_32K", NULL, "CP" },
	{ "CP AD Cali", NULL, "CLK_CP_32K" },
	{ "CLK_DAC", NULL, "CP AD Cali" },
	{ "CLK_DAC", NULL, "BIAS" },
	{ "CLK_ADC", NULL, "CLK_DIG_6M5" },
	{ "CLK_ADC", NULL, "DCL" },
	{ "CLK_ADC", NULL, "DIG_CLK_DRV_SOFT" },
	{ "CLK_ADC", NULL, "Digital Power" },
	{ "CLK_ADC_Norm", NULL, "CLK_ADC" },
	{ "CLK_ADC_Norm", NULL, "ADC_IBIAS" },
	{ "DAC", NULL, "CLK_DAC" },
	{ "Digital DACL Switch", NULL, "DAC" },
	{ "Digital DACR Switch", NULL, "DAC" },
	{ "ADie Digital DACL Switch", NULL, "Digital DACL Switch" },
	{ "ADie Digital DACR Switch", NULL, "Digital DACR Switch" },
	{ "DAC Virt", NULL, "ADie Digital DACL Switch" },
	{ "DAC Virt", NULL, "ADie Digital DACR Switch" },
	{ "DRV SOFT EN", NULL, "DAC Virt" },
	{ "DAC Gain", NULL, "DRV SOFT EN" },
	{ "DRV SOFT T", NULL, "SDM DC OS" },
	{ "DRV SOFT T", NULL, "DAC Gain" },
	{ "DAAO OS", NULL, "DRV SOFT T" },
	{ "AO Virt", NULL, "DAAO OS" },
	{ "AO Mixer", "AOR Switch", "AO Virt" },
	{ "DAAOR EN", NULL, "AO Mixer" },
	{ "DA AOR", "Switch", "DAAOR EN" },
	{ "SDAAOR AO", NULL, "DA AOR" },
	{ "AO Virt2", NULL, "SDAAOR AO" },
	{ "AO Mixer", "AOL Switch", "AO Virt" },
	{ "DAAOL EN", NULL, "AO Mixer" },
	{ "AOL EAR Sel", NULL, "DAAOL EN" },
	{ "AOL Virt", "AOL", "AOL EAR Sel" },
	{ "AOL EAR Sel2", NULL, "AOL Virt" },
	{ "SDAAOL AO", "AOL", "AOL EAR Sel2" },
	{ "AO Virt2", NULL, "SDAAOL AO" },
	{ "AO CUR", NULL, "AO Virt2" },
	{ "AO EN", NULL, "AO CUR" },
	{ "SPK PA", NULL, "AO EN" },
	{ "SPK PA", NULL, "DIG_CLK_RCV" },
	{ "SPK Pin", NULL, "SPK PA" },
	{ "HP DEPOP", NULL, "DIG_CLK_HPDPOP" },
	{ "HP DEPOP", NULL, "DIG_CLK_INTC" },
	{ "HP DEPOP", NULL, "SDM DC OS" },
	{ "HPRCV COM Virt", NULL, "DAC Gain" },
	{ "DAHP OS", NULL, "HPRCV COM Virt" },
	{ "DAHPL EN", NULL, "DAHP OS" },
	{ "HPL EAR Sel", NULL, "DAHPL EN" },
	{ "HPL Path", "HPL", "HPL EAR Sel" },
	{ "HPL Mixer", "DACLHPL Switch", "HPL Path" },
	{ "HPL EAR Sel2", NULL, "HPL Mixer" },
	{ "HPL Gain", "HPL", "HPL EAR Sel2" },
	{ "HP DEPOP", NULL, "HPL Gain" },
	{ "DAHPR EN", NULL, "DAHP OS" },
	{ "HPR Mixer", "DACRHPR Switch", "DAHPR EN" },
	{ "HPR Gain", NULL, "HPR Mixer" },
	{ "HP DEPOP", NULL, "HPR Gain" },
	{ "HP Virt", "Switch", "HP DEPOP" },
	{ "HP CHN EN", NULL, "HP Virt" },
	{ "HP Pin", NULL, "HP CHN EN" },
	{ "RCV DEPOP", NULL, "DIG_CLK_INTC" },
	{ "RCV DEPOP", NULL, "DIG_CLK_RCV" },
	{ "HPL EAR Sel", NULL, "DAC Gain" },
	{ "AOL EAR Sel", NULL, "DAC Gain" },
	{ "EAR_HPL Path", "EAR", "HPL EAR Sel" },
	{ "EAR_HPL Mixer", "DACHPL Switch", "EAR_HPL Path" },
	{ "SDAHPL RCV", NULL, "EAR_HPL Mixer" },
	{ "HPL EAR Sel2", NULL, "SDAHPL RCV" },
	{ "EAR Switch", "EAR", "HPL EAR Sel2" },
	{ "EAR_AOL Path", "EAR", "AOL EAR Sel" },
	{ "EAR_AOL Mixer", "DACAOL Switch", "EAR_AOL Path" },
	{ "SDAAOL RCV", NULL, "EAR_AOL Mixer" },
	{ "AOL EAR Sel2", NULL, "SDAAOL RCV" },
	{ "EAR Switch", "EAR", "AOL EAR Sel2" },
	{ "RCV DEPOP", NULL, "EAR Switch" },
	{ "EAR Gain", NULL, "RCV DEPOP" },
	{ "EAR Pin", NULL, "EAR Gain" },
	{ "BIAS", NULL, "BG" },
	{ "ADC_1_CLK", NULL, "BIAS" },
	{ "ADC_2_CLK", NULL, "BIAS" },
	{ "ADC_3_CLK", NULL, "BIAS" },
	{ "ADC_1 Switch", NULL, "ADC_1_CLK" },
	{ "ADC_2 Switch", NULL, "ADC_2_CLK" },
	{ "ADC_3 Switch", NULL, "ADC_3_CLK" },
	{ "VAD_AD_CLK", NULL, "CLK_ADC" },
	{ "VAD_AD Switch", NULL, "VAD_AD_CLK" },
	{ "ADC_1 Gain", NULL, "ADC_1 Mixer" },
	{ "ADC_2 Gain", NULL, "ADC_2 Mixer" },
	{ "ADC_3 Gain", NULL, "ADC_3 Mixer" },
	{ "VAD_AD Gain", NULL, "VAD_Virt" },
	{ "ADC_1 Switch", NULL, "ADC_1 Gain" },
	{ "ADC_2 Switch", NULL, "ADC_2 Gain" },
	{ "ADC_3 Switch", NULL, "ADC_3 Gain" },
	{ "VAD_AD Switch", NULL, "VAD_AD Gain" },
	{ "ADie Digital ADCL Switch", NULL, "ADC_1 Switch" },
	{ "ADie Digital ADCR Switch", NULL, "ADC_2 Switch" },
	{ "ADie Digital ADCL Switch", NULL, "ADC_3 Switch" },
	{ "ADie Digital ADCL Switch", NULL, "VAD_AD Switch" },
	{ "ADie Digital ADCR Switch", NULL, "VAD_AD Switch" },
	{ "VAD_VirtL", "Switch", "ADie Digital ADCL Switch" },
	{ "VAD_VirtR", "Switch", "ADie Digital ADCR Switch" },
	{ "Digital ADC In Sel", "ADC", "ADie Digital ADCL Switch" },
	{ "Digital ADC In Sel", "ADC", "ADie Digital ADCR Switch" },
	{ "Digital ADC1 In Sel", "ADC", "ADie Digital ADCL Switch" },
	{ "Digital ADC1 In Sel", "ADC", "ADie Digital ADCR Switch" },
	{ "Digital ADCL Switch", NULL, "Digital ADC In Sel" },
	{ "Digital ADCR Switch", NULL, "Digital ADC In Sel" },
	{ "Digital ADC1L Switch", NULL, "Digital ADC1 In Sel" },
	{ "Digital ADC1R Switch", NULL, "Digital ADC1 In Sel" },
	{ "AUD ADC0L", "Switch", "Digital ADCL Switch" },
	{ "AUD ADC0R", "Switch", "Digital ADCR Switch" },
	{ "AUD ADC1L", "Switch", "Digital ADC1L Switch" },
	{ "AUD ADC1R", "Switch", "Digital ADC1R Switch" },
	{ "ADC", NULL, "AUD ADC0L" },
	{ "ADC", NULL, "AUD ADC0R" },
	{ "ADC1", NULL, "AUD ADC0L" },
	{ "ADC1", NULL, "AUD ADC0R" },
	{ "ADC", NULL, "AUD ADC1L" },
	{ "ADC", NULL, "AUD ADC1R" },
	{ "ADC1", NULL, "AUD ADC1L" },
	{ "ADC1", NULL, "AUD ADC1R" },
	{ "ADC", NULL, "CLK_ADC_Norm" },
	{ "ADC1", NULL, "CLK_ADC_Norm" },
	{ "MIC Pin", NULL, "MICBIAS1" },
	{ "MIC2 Pin", NULL, "MICBIAS2" },
	{ "MIC3 Pin", NULL, "MICBIAS3" },
	{ "HPMIC Pin", NULL, "HEADMICBIAS" },
	{ "ADC_1 Mixer", "MIC1PGA_1 Switch", "MIC Pin" },
	{ "ADC_2 Mixer", "MIC2PGA_2 Switch", "MIC2 Pin" },
	{ "ADC_3 Mixer", "MIC3PGA_3 Switch", "MIC3 Pin" },
	{ "ADC_1 Mixer", "HPMICPGA_1 Switch", "HPMIC Pin" },
	{ "ADC_2 Mixer", "HPMICPGA_2 Switch", "HPMIC Pin" },
	{ "ADC_3 Mixer", "HPMICPGA_3 Switch", "HPMIC Pin" },
	{ "VAD_SMIC1", "Switch", "MIC Pin" },
	{ "VAD_SMIC2", "Switch", "MIC2 Pin" },
	{ "VAD_SMIC3", "Switch", "MIC3 Pin" },
	{ "VAD_Virt", NULL, "VAD_SMIC1" },
	{ "VAD_Virt", NULL, "VAD_SMIC2" },
	{ "VAD_Virt", NULL, "VAD_SMIC3" },
	{ "ADC-DAC Adie Loop", "switch", "ADC" },
	{ "ADC1-DAC Adie Loop", "switch", "ADC1" },
	{ "ADC-DAC Adie Loop post", NULL, "ADC-DAC Adie Loop" },
	{ "DAC", NULL, "ADC-DAC Adie Loop post" },
	{ "ADC-DAC Digital Loop", "switch", "ADC" },
	{ "ADC1-DAC Digital Loop", "switch", "ADC1" },
	{ "ADC-DAC Digital Loop post", NULL, "ADC-DAC Digital Loop" },
	{ "DAC", NULL, "ADC-DAC Digital Loop post" },
	{ "DMIC Switch", NULL, "DMIC Pin" },
	{ "Digital ADC In Sel", "DMIC", "DMIC Switch" },
	{ "DMIC1 Switch", NULL, "DMIC1 Pin" },
	{ "Digital ADC1 In Sel", "DMIC", "DMIC1 Switch" },
	{ "Virt Output", "Switch", "Digital DACL Switch" },
	{ "Virt Output", "Switch", "Digital DACR Switch" },
	{ "Virt Output Pin", NULL, "Virt Output" },
	{ "Virt Output Pin", NULL, "VAD_VirtL" },
	{ "Virt Output Pin", NULL, "VAD_VirtR" },
};


/*
 * Names of the headset detect modes, indexed by sprd_headset.detect_mode
 * (.rodata+0x6d28, 7 pointers).  The headset state machine prints them in its
 * traces and the vendor bounds them with UBSan, so keep the order.
 */
const char * const eic_hw_state[] = {
	"HW_LDETL_PLUG_OUT",
	"HW_INSERT_ALL_PLUG_OUT",
	"HW_LDETL_PLUG_IN",
	"HW_INSERT_ALL_PLUG_IN",
	"HW_MDET",
	"HW_BTN_PRESS",
	"HW_BTN_RELEASE",
};

/*
 * The CP short detection walks the BG divider (0x301c bits 8..10) and the
 * BIAS current (0x3020 bits 0..3) through six steps, waiting for bit 4 of
 * 0x3198 to rise (.rodata+0x18bf0 and .rodata+0x18c08).
 */
const unsigned int cp_short_bg[] = { 0x500, 0x500, 0x600, 0x600, 0x300, 0x300 };
const unsigned int cp_short_bias[] = { 0x9, 0x8, 0x5, 0x4, 0x3, 0x2 };
