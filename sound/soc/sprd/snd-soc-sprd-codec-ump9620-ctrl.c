// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc ump9620 audio codec - kcontrol callbacks and power sequencing
 *
 * Rebuilt from snd-soc-sprd-codec-ump9620.ko.  Every callback here is
 * referenced from the tables in snd-soc-sprd-codec-ump9620-tables.c, so the
 * function names are part of the ABI of the module (recon/symcheck.py
 * matches them literally).
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "snd-soc-sprd-codec-ump9620.h"

#define CODEC_DBG(fmt, args...)						\
	pr_debug("[ASoC:ump9620] " fmt, ##args)

/* decode a kcontrol back to the driver data */
static struct sprd_codec_priv *kc_to_priv(struct snd_kcontrol *kc)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kc);
	struct snd_soc_component *comp = snd_soc_dapm_to_component(dapm);

	return snd_soc_component_get_drvdata(comp);
}

static struct snd_soc_component *kc_to_comp(struct snd_kcontrol *kc)
{
	return snd_soc_dapm_to_component(snd_soc_dapm_kcontrol_dapm(kc));
}

/* bit width helpers, the codec derives masks from soc_mixer_control.max */
static u32 mc_mask(struct soc_mixer_control *mc)
{
	u32 width = mc->max ? 32 - __builtin_clz((u32)mc->max) : 0;

	return (u32)~0U >> (32 - width);
}

/*
 * Analog block power up order: the ADC bias, the reference buffer and the
 * ADC itself live in three different registers of the analog window.
 */
int sprd_codec_ap_adc_power_on(struct snd_soc_component *comp, bool on)
{
	snd_soc_component_update_bits(comp, 0x3080, 0xf000, on ? 0xc000 : 0);
	snd_soc_component_update_bits(comp, 0x3080, 0xc00, on ? 0xc00 : 0);
	snd_soc_component_update_bits(comp, 0x3080, 0x300, on ? 0x400 : 0x300);
	snd_soc_component_update_bits(comp, 0x30c8, 0x100, on ? 0x100 : 0);
	snd_soc_component_update_bits(comp, 0x3100, 0x4000, on ? 0x4000 : 0);
	snd_soc_component_update_bits(comp, 0x3100, 0x8000, on ? 0x8000 : 0);
	snd_soc_component_update_bits(comp, 0x3100, 0x2000, on ? 0x2000 : 0);
	return 0;
}

/*
 * The 4 MHz oscillator needs a full calibration run on power up; the done bit
 * is bit 13 of 0x31a0 and the vendor polls it 500 times with a 5 ms delay.
 */
void osc_4m_clk_cal(struct snd_soc_component *comp, bool on)
{
	unsigned int v = 0;
	int i;

	if (on) {
		snd_soc_component_update_bits(comp, 0x211c, 4, 0);
		snd_soc_component_update_bits(comp, 0x3108, 0xff, 8);
		snd_soc_component_update_bits(comp, 0x3108, 0xff00, 0x800);
		snd_soc_component_update_bits(comp, 0x310c, 0x1f, 1);
		snd_soc_component_update_bits(comp, 0x310c, 0x3fe0, 0xf80);
		snd_soc_component_update_bits(comp, 0x310c, 0xc000, 0x4000);
		snd_soc_component_update_bits(comp, 0x313c, 0xfff, 0x3e7);
		snd_soc_component_update_bits(comp, 0x313c, 0x8000, 0x8000);
		snd_soc_component_update_bits(comp, 0x3140, 0xfff, 0x3e7);
		snd_soc_component_update_bits(comp, 0x3104, 0xc000, 0);
		snd_soc_component_update_bits(comp, 0x3104, 0xfff, 0x552);
		snd_soc_component_update_bits(comp, 0x3104, 0x3000, 0x3000);

		for (i = 0; i < 500; i++) {
			v = snd_soc_component_read32(comp, 0x31a0);
			if (v & 0x2000)
				break;
			if (get_sp_audio_debug_flag() & 1)
				pr_info("[ASoC:ump9620] %s [%d] 0x%x\n",
					__func__, i, v);
			udelay(5000);
		}
		if (get_sp_audio_debug_flag() & 1)
			pr_info("[ASoC:ump9620] %s osc 4m cal %s, [%d] 0x%x\n",
				__func__, i > 499 ? "failed" : "successful", i, v);
		return;
	}

	snd_soc_component_update_bits(comp, 0x3108, 0xff, 0);
	snd_soc_component_update_bits(comp, 0x3108, 0xff00, 0);
	snd_soc_component_update_bits(comp, 0x310c, 0x1f, 0);
	snd_soc_component_update_bits(comp, 0x310c, 0x3fe0, 0);
	snd_soc_component_update_bits(comp, 0x310c, 0xc000, 0);
	snd_soc_component_update_bits(comp, 0x313c, 0xfff, 0);
	snd_soc_component_update_bits(comp, 0x313c, 0x8000, 0);
	snd_soc_component_update_bits(comp, 0x3140, 0xfff, 0);
	snd_soc_component_update_bits(comp, 0x3104, 0xc000, 0);
	snd_soc_component_update_bits(comp, 0x3104, 0xfff, 0);
	snd_soc_component_update_bits(comp, 0x3104, 0x3000, 0);
}

/* 60 MHz RC clock, only enabled on demand */
void rc_60m_clk(struct snd_soc_component *comp, bool on)
{
	struct sprd_codec_priv *priv;

	if (!on)
		return;

	priv = snd_soc_component_get_drvdata(comp);
	regmap_update_bits(priv->aon_apb2, 0xc, 0x1000, 0x1000);
	udelay(1000);
	regmap_update_bits(priv->aon_apb2, 0xc, 0x1000, 0);
	regmap_update_bits(priv->aon_apb2, 4, 0x2000000, 0x2000000);
	regmap_update_bits(priv->aon_apb2, 0xc90, 1, 1);
	regmap_update_bits(priv->aon_apb2, 0xc90, 0x20, 0x20);
}

/* playback side sample rate, coded in the low nibble of 0x100c */
int sprd_codec_set_sample_rate(struct snd_soc_component *comp, u32 rate,
			       u32 dummy)
{
	u32 sel;

	if (agdsp_access_enable())
		return -1;

	switch (rate) {
	case 8000:
		sel = 0xa;
		break;
	case 11025:
		sel = 8;
		break;
	case 16000:
		sel = 6;
		break;
	case 22050:
		sel = 5;
		break;
	case 24000:
		sel = 3;
		break;
	case 32000:
		sel = 2;
		break;
	case 44100:
		sel = 1;
		break;
	case 48000:
		sel = 0;
		break;
	default:
		pr_err("[ASoC:ump9620] error, SPRD-CODEC not support rate %d\n", rate);
		agdsp_access_disable();
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x100c, 0xf, sel);
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("Set Playback rate 0x%x\n",
			  snd_soc_component_read32(comp, 0x100c));

	agdsp_access_disable();
	return 0;
}

/*
 * Capture side rate: the ADCs use their own divider in 0x1018, and the
 * vendor substitutes the value when "ad-rate-fixup" is set in DT.
 */
int sprd_codec_set_ad_sample_rate(struct snd_soc_component *comp, u32 rate,
				  u32 shift)
{
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	u32 div;

	if (agdsp_access_enable())
		return -1;

	if (priv->ad_rate_fixup) {
		if (snd_pcm_rate_to_rate_bit(rate) & BIT(6))
			CODEC_DBG("Replace %uHz with %uHz for record\n",
				  rate, priv->ad_rate_fixup);
		else
			rate = priv->ad_rate_fixup;
	}

	/* 0x10624dd3 / 2^40 is 1/1000, so this is rate / 1000 */
	div = (u32)(((u64)rate * 0x10624dd3ULL) >> 40);
	if (rate / 1000 >= 0x36b)
		pr_err("[ASoC:ump9620] error, SPRD-CODEC not support ad rate %d\n",
		       rate);

	snd_soc_component_update_bits(comp, 0x1018, 0xf << shift, div << shift);
	agdsp_access_disable();
	return 0;
}

/* re-apply the three sample rates after a power transition */
void sprd_codec_sample_rate_setting(struct sprd_codec_priv *priv)
{
	struct snd_soc_component *comp = priv->comp;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s AD %u DA %u AD1 %u\n", __func__,
			priv->da_rate, priv->ad_rate, priv->ad1_rate);

	if (agdsp_access_enable()) {
		pr_err("[ASoC:ump9620] %s agdsp_access_enable error %d",
		       __func__, -EINVAL);
		return;
	}

	if (priv->ad_rate)
		sprd_codec_set_ad_sample_rate(comp, priv->ad_rate, 0);
	if (priv->ad1_rate)
		sprd_codec_set_ad_sample_rate(comp, priv->ad1_rate, 4);
	if (priv->da_rate)
		sprd_codec_set_sample_rate(comp, priv->da_rate, 0);

	agdsp_access_disable();
}

/* ---- plain mixer callbacks ------------------------------------------- */

u32 sprd_codec_spk_pga_get(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->spk_pga;
	return 0;
}

u32 sprd_codec_spk_pga_put(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	priv->spk_pga = val;
	if (priv->hp_chn_num >= 2)
		ucontrol->value.integer.value[0] = priv->spk_dg_fall;

	snd_soc_component_update_bits(comp, mc->reg,
				      mc_mask(mc) << mc->shift,
				      (u32)ucontrol->value.integer.value[0] << mc->shift);
	return 0;
}

u32 sprd_codec_info_get(struct snd_kcontrol *kc,
			struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] chip_id 0x%x, ver_id 0x%x, hw_index %d\n",
			priv->ana_chip_id, priv->ana_chip_ver, 8);

	ucontrol->value.integer.value[0] = 8;
	return 0;
}

u32 sprd_codec_inter_pa_get(struct snd_kcontrol *kc,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	unsigned long val = priv->ana_state;

	if (mc->invert)
		val = (u32)mc->max - (u32)(priv->ana_state);

	ucontrol->value.integer.value[0] = val;
	return 0;
}

u32 sprd_codec_inter_pa_put(struct snd_kcontrol *kc,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	val &= mc_mask(mc);
	if (mc->invert)
		val = (u32)mc->max - val;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d\n", __func__, val);

	priv->ana_state = (priv->ana_state & 0xf8) | (val & 7);
	return 0;
}

/* ---- sample rate selectors (explicit, symcheck needs the names) ----- */

u32 sprd_codec_dac_lrclk_sel_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = !!priv->dac_lrclk_sel;

	ucontrol->value.integer.value[0] = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %u\n", __func__, val);
	return 0;
}

u32 sprd_codec_dac_lrclk_sel_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];
	int ret;

	if ((int)val >= (int)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->dac_lrclk_sel = !!val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d\n", __func__, val);

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("[ASoC:ump9620] %s agdsp_access_enable error %d",
		       __func__, ret);
		return ret;
	}

	snd_soc_component_update_bits(comp, 0x1008, 0x2, val ? 0x2 : 0);
	agdsp_access_disable();
	return 0;
}

u32 sprd_codec_adc_lrclk_sel_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = !!priv->adc_lrclk_sel;

	ucontrol->value.integer.value[0] = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %u\n", __func__, val);
	return 0;
}

u32 sprd_codec_adc_lrclk_sel_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];
	int ret;

	if ((int)val >= (int)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->adc_lrclk_sel = !!val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d\n", __func__, val);

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("[ASoC:ump9620] %s agdsp_access_enable error %d",
		       __func__, ret);
		return ret;
	}

	snd_soc_component_update_bits(comp, 0x1008, 0x4, val ? 0x4 : 0);
	agdsp_access_disable();
	return 0;
}

u32 sprd_codec_adc1_lrclk_sel_get(struct snd_kcontrol *kc,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = !!priv->adc1_lrclk_sel;

	ucontrol->value.integer.value[0] = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %u\n", __func__, val);
	return 0;
}

u32 sprd_codec_adc1_lrclk_sel_put(struct snd_kcontrol *kc,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];
	int ret;

	if ((int)val >= (int)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->adc1_lrclk_sel = !!val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d\n", __func__, val);

	ret = agdsp_access_enable();
	if (ret) {
		pr_err("[ASoC:ump9620] %s agdsp_access_enable error %d",
		       __func__, ret);
		return ret;
	}

	snd_soc_component_update_bits(comp, 0x1034, 0x1, val ? 0x1 : 0);
	agdsp_access_disable();
	return 0;
}

/* ---- table/HW index driven callbacks -------------------------------- */

u32 sprd_codec_spk_dg_fall_get(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->spk_dg_fall;
	return 0;
}

u32 sprd_codec_spk_dg_fall_set(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	kc_to_priv(kc)->spk_dg_fall = ucontrol->value.integer.value[0];
	return 0;
}

u32 sprd_codec_get_hp_mix_mode(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->hp_mix_mode;
	return 0;
}

u32 sprd_codec_set_hp_mix_mode(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	kc_to_priv(kc)->hp_mix_mode = ucontrol->value.integer.value[0];
	return 0;
}


u32 sprd_codec_das_input_mux_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->das_input_mux;
	return 0;
}

u32 sprd_codec_das_input_mux_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val >= (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->das_input_mux = val;
	snd_soc_component_update_bits(comp, 0x2110, 0x18, val << 3);
	return 0;
}

u32 sprd_codec_fixed_rate_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->fixed_rate[0];
	return 0;
}

u32 sprd_codec_fixed_rate_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d\n", __func__, val);

	priv->fixed_rate[0] = val;
	priv->fixed_rate[1] = val;
	priv->fixed_rate[2] = val;
	return 0;
}

/*
 * "dig access disable" is the userspace switch which keeps the AGC DSP
 * powered, so it may not be turned off while the access counter is not zero.
 */
u32 dig_access_disable_get(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->dig_access_en;
	return 0;
}

u32 dig_access_disable_put(struct snd_kcontrol *kc,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	mutex_lock(&priv->dig_access_mutex);
	if (val) {
		if (priv->dig_access_en) {
			mutex_unlock(&priv->dig_access_mutex);
			return 0;
		}
		priv->dig_access_en = 1;
		if (!priv->dig_access_cnt)
			agdsp_access_enable();
		CODEC_DBG("%s, enable agdsp access\n", __func__);
	} else {
		if (!priv->dig_access_en) {
			mutex_unlock(&priv->dig_access_mutex);
			return 0;
		}
		priv->dig_access_en = 0;
		if (!priv->dig_access_cnt)
			agdsp_access_disable();
		CODEC_DBG("%s, disable agdsp access\n", __func__);
	}
	mutex_unlock(&priv->dig_access_mutex);
	return 0;
}

u32 sprd_codec_ivsence_dmic_get(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->ivsence_dmic;
	return 0;
}

u32 sprd_codec_ivsence_dmic_put(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val < 0 || (int)val >= (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s using %s, val %d\n", __func__,
			mc->texts ? mc->texts[val] : "", val);

	priv->ivsence_dmic = val;
	return 0;
}

u32 sprd_codec_daldo_bypass_get(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->daldo_bypass;
	return 0;
}

u32 sprd_codec_daldo_bypass_put(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	priv->daldo_bypass = !!ucontrol->value.integer.value[0];
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s daldo_bypass %d\n", __func__, priv->daldo_bypass);
	return 0;
}

u32 sprd_codec_clk_26m_in_sel_get(struct snd_kcontrol *kc,
				  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->clk_26m_in_sel;
	return 0;
}

u32 sprd_codec_clk_26m_in_sel_put(struct snd_kcontrol *kc,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val >= (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->clk_26m_in_sel = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("clk 26m in sel %s\n",
			  priv->clk_26m_in_sel ? "AUD_PLL_IN" : "AUD_26M_IN");
	return 0;
}

u32 sprd_codec_drv_soft_t_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->drv_soft_t;
	return 0;
}

u32 sprd_codec_drv_soft_t_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val < 0 || (int)val > (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->drv_soft_t = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("drv_soft_t %d\n", priv->drv_soft_t);
	return 0;
}

/* cal-mode / channel-type string tables, indexed by the latched value */
static const char * const c_cal_mode_name[] = {
	"HW_CAL_MODE", "SW_CAL_MODE", "NOT_CAL",
};
const char * const c_hp_chn_type_name[] = {
	"HP_LR_TYPE", "HP_R_TYPE", "HP_L_TYPE",
};

u32 sprd_codec_daaol_buf_dccal_get(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->daaol_buf_dccal;
	return 0;
}

u32 sprd_codec_daaol_buf_dccal_put(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->daaol_buf_dccal = val;
	if ((get_sp_audio_debug_flag() & 1) && val <= 2)
		CODEC_DBG("daaol_buf_dccal %s\n", c_cal_mode_name[val]);
	return 0;
}

u32 sprd_codec_daaor_buf_dccal_get(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->daaor_buf_dccal;
	return 0;
}

u32 sprd_codec_daaor_buf_dccal_put(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->daaor_buf_dccal = val;
	if ((get_sp_audio_debug_flag() & 1) && val <= 2)
		CODEC_DBG("daaor_buf_dccal %s\n", c_cal_mode_name[val]);
	return 0;
}

u32 sprd_codec_dahpl_buf_dccal_get(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->dahpl_buf_dccal;
	return 0;
}

u32 sprd_codec_dahpl_buf_dccal_put(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->dahpl_buf_dccal = val;
	if ((get_sp_audio_debug_flag() & 1) && val <= 2)
		CODEC_DBG("dahpl_buf_dccal %s\n", c_cal_mode_name[val]);
	return 0;
}

u32 sprd_codec_dahpr_buf_dccal_get(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->dahpr_buf_dccal;
	return 0;
}

u32 sprd_codec_dahpr_buf_dccal_put(struct snd_kcontrol *kc,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->dahpr_buf_dccal = val;
	if ((get_sp_audio_debug_flag() & 1) && val <= 2)
		CODEC_DBG("dahpr_buf_dccal %s\n", c_cal_mode_name[val]);
	return 0;
}

u32 sprd_codec_dahp_os_inv_get(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->dahp_os_inv;
	return 0;
}

u32 sprd_codec_dahp_os_inv_put(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->dahp_os_inv = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("dahp_os_inv %s\n", val ? "invert" : "normal");
	return 0;
}

u32 sprd_codec_hp_chn_type_get(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->hp_chn_type;
	return 0;
}

u32 sprd_codec_hp_chn_type_put(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if (val >= (u32)mc->platform_max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->hp_chn_type = val;
	if ((get_sp_audio_debug_flag() & 1) && val <= 2)
		CODEC_DBG("hp type %s\n", c_hp_chn_type_name[val]);
	return 0;
}

u32 sprd_codec_dahp_os_d_get(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->dahp_os_d;
	return 0;
}

u32 sprd_codec_dahp_os_d_put(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val > (int)mc->max)
		return -EINVAL;

	priv->dahp_os_d = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("dahp_os_d %d\n", priv->dahp_os_d);
	return 0;
}

u32 sprd_codec_rcv_pop_ramp_get(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->rcv_pop_ramp;
	return 0;
}

u32 sprd_codec_rcv_pop_ramp_put(struct snd_kcontrol *kc,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 val = ucontrol->value.integer.value[0];

	if ((int)val > (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->rcv_pop_ramp = val;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("rcv_pop_ramp %d\n", priv->rcv_pop_ramp);
	return 0;
}

/* SDM modulator bias, read back from the driver copy of the register */
u32 sprd_codec_get_sdm_l(struct snd_kcontrol *kc,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	ucontrol->value.integer.value[0] = priv->sdm_l;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("ramp read SDM_L %d ( 0x%x)\n", priv->sdm_l, priv->sdm_l);
	return 0;
}

u32 sprd_codec_set_sdm_l(struct snd_kcontrol *kc,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	priv->sdm_l = ucontrol->value.integer.value[0];
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("ramp set SDM_L %d ( 0x%x)\n", priv->sdm_l, priv->sdm_l);
	return 0;
}

u32 sprd_codec_get_sdm_h(struct snd_kcontrol *kc,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	ucontrol->value.integer.value[0] = priv->sdm_h;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("ramp read SDM_H %d ( 0x%x)\n", priv->sdm_h, priv->sdm_h);
	return 0;
}

u32 sprd_codec_set_sdm_h(struct snd_kcontrol *kc,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	priv->sdm_h = ucontrol->value.integer.value[0];
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("ramp set SDM_H %d ( 0x%x)\n", priv->sdm_h, priv->sdm_h);
	return 0;
}

/* "add0_data_sel" also programs the AGC AHB bridge in register 0x234c */
u32 sprd_codec_add0_data_sel_get(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->add0_data_sel;
	return 0;
}

u32 sprd_codec_add0_data_sel_put(struct snd_kcontrol *kc,
				 struct snd_ctl_elem_value *ucontrol)
{
	static const u32 tab[4] = { 0, 0x100, 0x180, 0 };
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	struct soc_mixer_control *mc = (void *)kc->private_value;
	u32 val = ucontrol->value.integer.value[0];
	u32 sel = 0;

	if ((int)val >= (int)mc->max) {
		pr_err("[ASoC:ump9620] %s index outof bounds error\n", __func__);
		return -EINVAL;
	}

	priv->add0_data_sel = val;
	if (val < 3)
		sel = tab[val];
	else if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s %d out of range\n", __func__, val);

	regmap_update_bits(priv->agcp_apb, 0x234c, 0x180, sel);
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s add0_data_sel %d\n", __func__, priv->add0_data_sel);
	return 0;
}

/* ---- mixer / pga helpers -------------------------------------------- */

/* one bit of priv->dac_switch mirrors one DAC mux in the DSP */
u32 sprd_mixer_get(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 bit = 1 << mc->shift;

	ucontrol->value.integer.value[0] = !!(priv->dac_switch & bit);

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("dac switch 0x%x,shift %d, get %d\n", priv->dac_switch,
			  mc->shift, (u32)ucontrol->value.integer.value[0]);
	return 0;
}

u32 sprd_mixer_put(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	u32 bit = 1 << mc->shift;

	if (ucontrol->value.integer.value[0])
		priv->dac_switch |= bit;
	else
		priv->dac_switch &= ~bit;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("dac switch 0x%x,shift %d,set %d\n", priv->dac_switch,
			  mc->shift, (u32)ucontrol->value.integer.value[0]);

	snd_soc_dapm_put_volsw(kc, ucontrol);
	return 0;
}

u32 sprd_pga_get(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	u32 mask = mc_mask(mc);

	ucontrol->value.integer.value[0] =
		(snd_soc_component_read32(comp, mc->reg) >> mc->shift) & mask;
	return 0;
}

u32 sprd_pga_put(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (void *)kc->private_value;
	struct snd_soc_component *comp = kc_to_comp(kc);
	u32 val = ucontrol->value.integer.value[0];

	snd_soc_component_update_bits(comp, mc->reg, mc_mask(mc) << mc->shift,
				      val << mc->shift);
	return 0;
}

/* BIAS is the only supply toggled directly from userspace */
u32 bias_power_get(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kc_to_priv(kc)->bias_on_off;
	return 0;
}

u32 bias_power_put(struct snd_kcontrol *kc, struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);
	int on = !!ucontrol->value.integer.value[0];
	int ret = 0;

	if (on) {
		ret = regulator_enable(priv->bias);
		if (ret)
			pr_err("[ASoC:ump9620] bias regulator en failed");
	} else {
		regulator_disable(priv->bias);
	}

	priv->bias_on_off = on;
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("BIAS on_off %d\n", priv->bias_on_off);
	return ret;
}

/* the analog die revision, exported as a read-only control */
u32 sprd_codec_get_ana_chip_id(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_codec_priv *priv = kc_to_priv(kc);

	ucontrol->value.integer.value[0] = priv->ana_chip_ver;
	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s ana_chip_ver_id = %x\n",
			__func__, priv->ana_chip_ver);
	return 0;
}

/*
 * DAC mute IRQ: the handler completes the playback muting completion and
 * clears whichever status bit was raised (bit 5 stays as its own mask).
 */
irqreturn_t sprd_codec_dp_irq(int irq, void *data)
{
	struct snd_soc_component *comp = *(struct snd_soc_component **)data;
	u32 val = snd_soc_component_read32(comp, 0x1020);

	if (get_sp_audio_debug_flag() & 1)
		pr_debug("[ASoC:ump9620] dac mute irq mask = 0x%x\n", val);

	if (val & 0x10) {
		complete((struct completion *)((char *)data + 0x18));
		val = 1;
	}

	snd_soc_component_update_bits(comp, 0x1028,
				      (val & 0x20) ? val : 2, 0);
	return IRQ_HANDLED;
}
