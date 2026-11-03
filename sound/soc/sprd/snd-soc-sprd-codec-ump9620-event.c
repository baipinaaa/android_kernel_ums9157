// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc ump9620 audio codec - DAPM event handlers
 *
 * Rebuilt from snd-soc-sprd-codec-ump9620.ko.  The vendor keeps a single
 * power state machine per block (digital/analog) plus one small handler per
 * widget; the handlers only fire on the events registered in the widget
 * table, so anything else is reported as an invalid event.
 */
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "snd-soc-sprd-codec-ump9620.h"

#define CODEC_DBG(fmt, args...)						\
	pr_debug("[ASoC:ump9620] " fmt, ##args)

static struct sprd_codec_priv *comp_to_priv(struct snd_soc_component *comp)
{
	return snd_soc_component_get_drvdata(comp);
}

static struct sprd_codec_priv *widget_to_priv(struct snd_soc_dapm_widget *w)
{
	return comp_to_priv(snd_soc_dapm_to_component(w->dapm));
}

/*
 * The event names only exist for the four events the codec widgets register;
 * the other four slots are filled with PRE_PMU but masked out, which is how
 * the vendor keeps the table at a fixed size.
 */
static const char *get_event_name(int event)
{
	static const char * const event_name[] = {
		"PRE_PMU", "POST_PMU", "PRE_PMU", "PRE_PMD",
		"PRE_PMU", "PRE_PMU", "PRE_PMU", "POST_PMD",
	};
	unsigned int idx = event - 1;

	if (idx < 8 && (0x8b >> idx) & 1)
		return event_name[idx];

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);
	return NULL;
}

/*
 * Digital block power up: the AGC DSP access window has to be open before any
 * register of 0x1000..0x12f4 can be touched, and the DAC/ADC rate selectors
 * have to be restored once the DSP is back.
 */
/* DC calibration modes reported by the 0x18670 table */
static const char * const c_dccal_mode_name[] = {
	"HW_CAL_MODE",
	"SW_CAL_MODE",
	"NOT_CAL",
};

static void sprd_codec_digital_open(struct sprd_codec_priv *priv)
{
	struct snd_soc_component *comp = priv->comp;

	pr_info("[ASoC:ump9620] %s %d\n", __func__, __LINE__);

	snd_soc_component_update_bits(comp, 0x100c, 0x8000, 0);
	sprd_codec_sample_rate_setting(priv);
	snd_soc_component_update_bits(comp, 0x1010, 0xffff, 0);

	if (priv->dac_lrclk_sel)
		snd_soc_component_update_bits(comp, 0x1008, 0x2, 0x2);
	if (priv->adc_lrclk_sel)
		snd_soc_component_update_bits(comp, 0x1008, 0x4, 0x4);
	if (priv->adc1_lrclk_sel)
		snd_soc_component_update_bits(comp, 0x1034, 0x1, 0x1);

	snd_soc_component_update_bits(comp, 0x1084, 0xffff, 0x3303);
	snd_soc_component_update_bits(comp, 0x1088, 0x1, 0);
}

/* analog block power up: clamp the VB regulator and release the resets */
static void sprd_codec_power_enable(struct sprd_codec_priv *priv)
{
	struct snd_soc_component *comp = priv->comp;

	if (sprd_codec_clk26m_sinout(priv, 1))
		pr_err("[ASoC:ump9620] %s set clk26m_sinout_pmic fail\n",
		       __func__);

	pr_info("[ASoC:ump9620] %s %d\n", __func__, __LINE__);

	regmap_update_bits(priv->agcp_apb, 0x200c, 0x83, 0x83);
	regmap_update_bits(priv->agcp_apb, 0x2394, 0x2, 0x2);
	regmap_update_bits(priv->agcp_apb, 0x2378, 0x100, 0x100);

	snd_soc_component_update_bits(comp, 0x2100, 0x2, 0x2);
	snd_soc_component_update_bits(comp, 0x2104, 0x6, 0x6);
	udelay(10);
	snd_soc_component_update_bits(comp, 0x2104, 0x6, 0);
	snd_soc_component_update_bits(comp, 0x2140, 0x10, 0);
	regulator_set_mode(priv->vb, 2);
}

static int sprd_codec_power_disable(struct sprd_codec_priv *priv)
{
	struct snd_soc_component *comp = priv->comp;

	pr_info("[ASoC:ump9620] %s %d\n", __func__, __LINE__);

	if (!priv->agcp_apb) {
		pr_err("[ASoC:ump9620] ERR: %s codec_regmap isn't initialized!\n",
		       "arch_audio_codec_analog_disable");
		goto reset;
	}

	regmap_update_bits(priv->agcp_apb, 0x200c, 0x83, 0);
	regmap_update_bits(priv->agcp_apb, 0x2394, 0x2, 0);

reset:
	snd_soc_component_update_bits(comp, 0x2100, 0x2, 0);
	snd_soc_component_update_bits(comp, 0x2104, 0x6, 0x6);
	udelay(10);
	snd_soc_component_update_bits(comp, 0x2104, 0x6, 0);
	snd_soc_component_update_bits(comp, 0x2140, 0x10, 0);
	regulator_set_mode(priv->vb, 8);

	if (sprd_codec_clk26m_sinout(priv, 0))
		pr_err("[ASoC:ump9620] %s clean clk26m_sinout_pmic fail\n",
		       __func__);
}

static int digital_power_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	int ret;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("'%s' %s\n", w->name, get_event_name(event));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		codec_digital_reg_disable(priv);

		mutex_lock(&priv->dig_access_mutex);
		if (priv->digital_en) {
			priv->dig_access_en = 0;
			agdsp_access_disable();
			priv->digital_en = 0;
		}
		mutex_unlock(&priv->dig_access_mutex);
		return 0;

	case SND_SOC_DAPM_PRE_PMU:
		mutex_lock(&priv->dig_access_mutex);
		ret = agdsp_access_enable();
		if (ret) {
			pr_err("[ASoC:ump9620] %s agdsp_access_enable failed\n",
			       __func__);
			mutex_unlock(&priv->dig_access_mutex);
			return ret;
		}
		priv->digital_en = 1;
		mutex_unlock(&priv->dig_access_mutex);

		mutex_lock(&priv->digital_enable_mutex);
		if (!priv->dig_access_cnt || priv->dig_access_en)
			arch_audio_codec_digital_reg_enable_v2(priv);
		priv->dig_access_cnt++;
		mutex_unlock(&priv->digital_enable_mutex);

		sprd_codec_digital_open(priv);
		return 0;

	default:
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}
}

static int analog_power_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	int ret;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("'%s' %s\n", w->name, get_event_name(event));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		sprd_codec_power_enable(priv);
		return 0;

	case SND_SOC_DAPM_PRE_PMD:
		return sprd_codec_power_disable(priv);

	default:
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}
}

/* the 26 MHz input is only switched to the PLL when userspace asked for it */
static int clk_26m_in_sel_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s, clk_26m_in_sel %d\n",
			w->name, get_event_name(event), priv->clk_26m_in_sel);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (priv->clk_26m_in_sel != 1)
			return 0;
		snd_soc_component_update_bits(comp, 0x3068, 0x4000, 0x4000);
		return 0;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(comp, 0x3068, 0x4000, 0);
		return 0;

	default:
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}
}

/* DCL block: needs a 180us settle before the LR data select can be applied */
static int audio_dcl_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event != SND_SOC_DAPM_POST_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x30fc, 0x8601, 0x8601);
	snd_soc_component_update_bits(comp, 0x3100, 0x400, 0x400);
	usleep_range(180, 200);

	if (priv->lrdat_sel)
		snd_soc_component_update_bits(comp, 0x305c, 0x300, 0x300);

	return 0;
}

/* DALDO enable: bypass mode uses 0x100, normal path uses 0x200 */
static int daldo_en_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s, daldo_bypass %d\n", w->name,
			get_event_name(event), priv->daldo_bypass);

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_component_update_bits(comp, 0x3050, 0x300,
					      priv->daldo_bypass ? 0x100 : 0x200);
		usleep_range(600, 610);
	} else if (event == SND_SOC_DAPM_PRE_PMD ||
		   event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x3050, 0x300, 0);
	}

	return 0;
}

/* SDM DC offset: 0xcd9f is the default when userspace did not set SDM_L */
static int sdm_dc_os_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 sdm_l, sdm_h;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (!(event & 0x3))
		return 0;

	sdm_l = priv->sdm_l;
	sdm_h = priv->sdm_h;
	if (!sdm_l)
		sdm_l = 0xcd9f;

	snd_soc_component_update_bits(comp, 0x1038, 0xffff, sdm_l);
	snd_soc_component_update_bits(comp, 0x103c, 0xffff, sdm_h);
	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] ramp reg: SDM_L 0x%x, SDM_H 0x%x\n",
			sdm_l, sdm_h);
	snd_soc_component_update_bits(comp, 0x210c, 0x3fff, 0x306);

	return 0;
}

/* plain supply widget: only validates that the event it got makes sense */
static int codec_widget_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("'%s' %s\n", w->name, get_event_name(event));

	if (event > 8 || !((1 << event) & 0x116))
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);

	return 0;
}

/*
 * DAC/ADC/ADC1 widgets carry their identity in the negative "no register"
 * marker of the widget (reg = -1, -2, -3), so ~reg is the channel index.
 */
static int chan_event(struct snd_soc_dapm_widget *w,
		      struct snd_kcontrol *kcontrol, int event)
{
	static const char * const chan_name[] = { "DAC", "ADC", "ADC1" };
	struct sprd_codec_priv *priv = widget_to_priv(w);
	int idx = ~w->reg;

	if ((get_sp_audio_debug_flag() & 1) && idx < 3)
		pr_info("[ASoC:ump9620] %s %s\n", chan_name[idx],
			(event & 0x3) ? "On" : "Off");

	sprd_codec_sample_rate_setting(priv);
	return 0;
}

/* the IV sense source forces a 48 kHz capture rate when it powers up */
static int sprd_ivsense_src_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMD)
		return 0;

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	sprd_codec_set_ad_sample_rate(comp, 48000, 0);
	return 0;
}

/* every ADC power up is followed by a fixed settle delay */
static void sprd_codec_wait(int ms)
{
	CODEC_DBG("%s %d ms\n", __func__, ms);
	msleep(ms);
}

/* ADC1: power bit in 0x3074, reset pulse on the same register */
static int adc_1_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_component_update_bits(comp, 0x3074, 0x8, 0);
		sprd_codec_wait(60);
		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x3074, 0x2, 0x2);
	snd_soc_component_update_bits(comp, 0x3074, 0x2, 0);
	snd_soc_component_update_bits(comp, 0x3074, 0x8, 0);
	udelay(4);
	snd_soc_component_update_bits(comp, 0x3074, 0x8, 0x8);
	udelay(4);

	return 0;
}

/* ADC2: same sequence, bits 0 and 2 of 0x3074 */
static int adc_2_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_component_update_bits(comp, 0x3074, 0x4, 0);
		sprd_codec_wait(60);
		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x3074, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x3074, 0x1, 0);
	snd_soc_component_update_bits(comp, 0x3074, 0x4, 0);
	udelay(4);
	snd_soc_component_update_bits(comp, 0x3074, 0x4, 0x4);
	udelay(4);

	return 0;
}

/* ADC3: the enable bit lives in 0x3078, the reset pulse in 0x3074 */
static int adc_3_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_component_update_bits(comp, 0x3074, 0x8000, 0);
		sprd_codec_wait(60);
		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x3078, 0x8000, 0x8000);
	snd_soc_component_update_bits(comp, 0x3078, 0x8000, 0);
	snd_soc_component_update_bits(comp, 0x3074, 0x8000, 0);
	udelay(4);
	snd_soc_component_update_bits(comp, 0x3074, 0x8000, 0x8000);
	udelay(4);

	return 0;
}

/* VAD ADC uses 0x30cc bit 1 and the 0x30c8 bit 4 reset */
static int vad_ad_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_component_update_bits(comp, 0x30c8, 0x10, 0);
		sprd_codec_wait(20);
		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x30cc, 0x2, 0x2);
	snd_soc_component_update_bits(comp, 0x30cc, 0x2, 0);
	snd_soc_component_update_bits(comp, 0x30c8, 0x10, 0);
	udelay(10);
	snd_soc_component_update_bits(comp, 0x30c8, 0x10, 0x10);
	udelay(10);

	return 0;
}

/*
 * Shared DAC path switch: bit N of priv->dac_switch mirrors the DAC mux of
 * this path, and the same bit doubles as the mask of register 0x3098.
 */
static u32 update_switch(struct snd_soc_component *comp, u32 path, u32 on,
			 u32 dac_switch, u32 val)
{
	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s path 0x%x, on %d, dac_switch 0x%x, val 0x%x\n",
			__func__, path, on, dac_switch, val);

	return snd_soc_component_update_bits(comp, 0x3098, path, val);
}

/* poll a codec register for up to 4ms, in 126..500us steps */
static int sprd_codec_reg_read_poll_timeout(struct snd_soc_component *comp,
					    unsigned int reg,
					    unsigned int mask,
					    unsigned int val)
{
	ktime_t timeout = ktime_add_ns(ktime_get(), 4000000);
	unsigned int v;

	for (;;) {
		v = snd_soc_component_read32(comp, reg);
		if ((int)v < 0)
			return v;

		if ((v & mask) && (v & val))
			return 0;

		if (ktime_after(ktime_get(), timeout))
			break;

		usleep_range(126, 500);
	}

	/* one last look, the block may have settled while we gave up */
	v = snd_soc_component_read32(comp, reg);
	if ((int)v < 0)
		return v;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s val %d 0x%x, check1 0x%x, check2 0x%x, cond 0x%x\n",
			__func__, v, v, mask, val,
			(v & mask) && (v & val));

	return ((v & mask) && (v & val)) ? 0 : -ETIMEDOUT;
}

/* DRV_SOFT_T only takes effect when powered up */
static int drv_soft_t_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s, drv_soft_t %d\n", w->name,
			get_event_name(event), priv->drv_soft_t);

	if (event == SND_SOC_DAPM_PRE_PMD)
		return 0;

	if (event != SND_SOC_DAPM_POST_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	if (priv->drv_soft_t)
		snd_soc_component_update_bits(comp, 0x30a4, 0x7,
					      priv->drv_soft_t & 0x7);

	return 0;
}

/* DAAO offset: 0x306 is the vendor's "no offset" calibration value */
static int daao_os_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMD) {
		snd_soc_component_update_bits(comp, 0x3058, 0x10, 0);
		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x210c, 0x3fff, 0x306);
	snd_soc_component_update_bits(comp, 0x3058, 0x100, 0x100);
	snd_soc_component_update_bits(comp, 0x3058, 0x10, 0x10);
	snd_soc_component_update_bits(comp, 0x3058, 0x8, 0);
	snd_soc_component_update_bits(comp, 0x3058, 0x7, 0x5);

	return 0;
}

/* AO enable is a single bit which is set for both PMU events */
static int ao_en_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u32 val;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		val = 1;
	} else if (event == SND_SOC_DAPM_PRE_PMD ||
		   event == SND_SOC_DAPM_POST_PMD) {
		val = 0;
	} else {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x309c, 0x1, val);
	return 0;
}

/* SDAAOL -> AO path, DAC switch bit 5 */
static int sdaaol_ao_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	update_switch(comp, 0x20, on, priv->dac_switch,
		      on ? (priv->dac_switch & 0x20) : 0);
	return 0;
}

/* SDAAOR -> AO path, same register, DAC switch bit 5 */
static int sdaaor_ao_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	update_switch(comp, 0x20, on, priv->dac_switch,
		      on ? (priv->dac_switch & 0x20) : 0);
	return 0;
}

/* SDAAOL -> RCV path, DAC switch bit 1 */
static int sdaaol_rcv_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	update_switch(comp, 0x2, on, priv->dac_switch,
		      on ? (priv->dac_switch & 0x2) : 0);
	fdin_dvld_check_event(w, event);
	return 0;
}

/* SDAHPL -> RCV path, DAC switch bit 0 */
static int sdahpl_rcv_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	update_switch(comp, 0x1, on, priv->dac_switch,
		      on ? (priv->dac_switch & 0x1) : 0);
	fdin_dvld_check_event(w, event);
	return 0;
}

/* analog die loopback keeps bit 0 of 0x2100/0x2108 in sync */
static int adie_loop_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u32 val;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMU)
		val = 1;
	else if (event == SND_SOC_DAPM_PRE_PMD)
		val = 0;
	else {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x2100, 0x1, val);
	snd_soc_component_update_bits(comp, 0x2108, 0x1, val);
	return 0;
}

/* digital loopback runs at 48 kHz on both directions */
static int digital_loop_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	sprd_codec_set_ad_sample_rate(comp, 48000, 0);
	sprd_codec_set_sample_rate(comp, 48000, 0);

	if (event == SND_SOC_DAPM_PRE_PMD) {
		snd_soc_component_update_bits(comp, 0x101c, 0x1, 0);
		return 0;
	}

	if (event != SND_SOC_DAPM_POST_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x101c, 0x200, 0x200);
	snd_soc_component_update_bits(comp, 0x101c, 0x1, 0x1);
	return 0;
}

/* earphone switch, waits for the RCV data valid before enabling the ramp */
static int ear_switch_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	int ret = 0;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD)
		return 0;

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x30a8, 0x1, 0);
	snd_soc_component_update_bits(comp, 0x309c, 0x2, 0x2);

	ret = sprd_codec_reg_read_poll_timeout(comp, 0x3188, 0x400, 0x2);
	if (ret)
		pr_err("[ASoC:ump9620] %s check rcv dvld failed, %d\n",
		       __func__, ret);

	if (priv->rcv_pop_ramp)
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0x500);

	return ret;
}

static void hp_dpop_dvld_check(struct snd_soc_component *comp, u32 on,
			       u32 path);

/* DAHP DC offset: 5 is the vendor's default when nothing was programmed */
static int dahp_os_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 os_d;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s, dahp_os_inv %d, dahp_os_d %d\n",
			w->name, get_event_name(event), priv->dahp_os_inv,
			priv->dahp_os_d);

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD)
		return 0;

	if (event != SND_SOC_DAPM_POST_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x3054, 0x10, 0x10);
	snd_soc_component_update_bits(comp, 0x3054, 0x100, 0x100);
	snd_soc_component_update_bits(comp, 0x3050, 0xfc, 0xfc);

	if (priv->dahp_os_inv == 1)
		snd_soc_component_update_bits(comp, 0x3054, 0x8, 0x8);

	os_d = priv->dahp_os_d;
	snd_soc_component_update_bits(comp, 0x3054, 0x7, os_d ? (os_d & 0x7) : 5);

	return 0;
}

/* headphone depop on power up only */
static int hp_depop_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	u32 on = !!(event & 0x3);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (!on)
		return 0;

	snd_soc_component_update_bits(comp, 0x3110, 0x60, 0x60);
	snd_soc_component_update_bits(comp, 0x3110, 0x7, 0);
	snd_soc_component_update_bits(comp, 0x3118, 0xf, 0x1);
	snd_soc_component_update_bits(comp, 0x311c, 0x3c00, 0x1800);
	snd_soc_component_update_bits(comp, 0x3110, 0x18, 0x18);

	return 0;
}

/* charge pump comparator calibration runs once per power up */
static int cp_ad_cmp_cali_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMD)
		return 0;

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x3124, 0xc000, 0x4000);
	snd_soc_component_update_bits(comp, 0x3124, 0x1000, 0x1000);
	snd_soc_component_update_bits(comp, 0x3128, 0x8000, 0);
	snd_soc_component_update_bits(comp, 0x3000, 0x4, 0x4);
	snd_soc_component_update_bits(comp, 0x3124, 0x2000, 0x2000);
	usleep_range(3000, 3200);

	return 0;
}

/*
 * Headphone channel enable: the mask pair depends on the type of headphone
 * the detection settled on (LR / R / L), and the DAC switch bit decides
 * whether the path is really enabled.
 */
static int hp_chn_en_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	static const u32 dvld_path[3] = { 0x18, 0x8, 0x10 };
	static const u32 sw_mask[3] = { 0xc, 0x4, 0x8 };
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);
	u32 type = priv->hp_chn_type;
	u32 path, mask;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s, type: %s\n", w->name,
			get_event_name(event), c_hp_chn_type_name[type]);

	if (type >= 3) {
		pr_err("[ASoC:ump9620] %s hp chn type error, using HP_LR_TYPE\n",
		       __func__);
		path = 0x18;
		mask = 0xc;
	} else {
		path = dvld_path[type];
		mask = sw_mask[type];
	}

	if (event == SND_SOC_DAPM_PRE_PMD) {
		update_switch(comp, mask, on, priv->dac_switch, 0);
		hp_dpop_dvld_check(comp, on, path);
		snd_soc_component_update_bits(comp, 0x309c, mask, 0);
		return 0;
	}

	if (event != SND_SOC_DAPM_POST_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	update_switch(comp, mask, on, priv->dac_switch, mask & priv->dac_switch);
	snd_soc_component_update_bits(comp, 0x309c, mask, mask);
	hp_dpop_dvld_check(comp, on, path);

	return 0;
}

/*
 * Wait until the headphone data-valid bit matches the requested state; the
 * analog block needs up to 20 * 10ms to settle after a headphone switch.
 */
static void hp_dpop_dvld_check(struct snd_soc_component *comp, u32 on,
			       u32 path)
{
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	char type_name[12];
	unsigned int val = 0;
	int i;

	strncpy(type_name, c_hp_chn_type_name[priv->hp_chn_type], 0xb);

	for (i = 1; i < 0x15; i++) {
		val = snd_soc_component_read32(comp, 0x3188);
		if (on ? ((val & path) == path) : ((val & path) == 0))
			break;

		CODEC_DBG("%s %d ms\n", "sprd_codec_wait", 10);
		usleep_range(10000, 10200);
	}

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s %s DPOP %s, i %d, ANA_STS1 0x%x, dvld_type 0x%x\n",
			__func__, type_name, i > 0x13 ? "failed" : "sucessed",
			i, val, path);
}

/*
 * Re-calibrate the headphone DAC reference: the vendor keeps the two
 * reference codes in 0x30c0/0x30c4 and shifts them by one step whenever the
 * measured value is outside the middle window.
 */
static void hp_dac_re_cali(int id, struct snd_soc_component *comp)
{
	u32 vrp, vrn, vrp_new;
	u16 reg1, reg2;

	snd_soc_component_update_bits(comp, 0x308c, 0x3, id != 2);

	vrp = sprd_codec_read(comp, 0x30c0) & 0x1ff;
	vrn = sprd_codec_read(comp, 0x30c4) & 0x1ff;

	if (vrp > 0x1ee)
		vrp_new = 0x1ff;
	else if (vrp >= 0x1c0)
		vrp_new = vrp + 0x10;
	else if (vrp >= 0x190)
		vrp_new = vrp - 0x10;
	else
		vrp_new = (vrp > 0x17f) ? (0x350 - vrp) : 0x1c0;

	pr_info("[ASoC:ump9620] %s %s vrp 0x%x, vrp_new 0x%x, vrn 0x%x\n",
		__func__, id == 2 ? "left " : "right ", vrp, vrp_new, vrn);

	if (id == 2) {
		snd_soc_component_update_bits(comp, 0x3088, 0x800, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x400, 0x400);
		reg1 = 0x30b0;
		reg2 = 0x30b4;
	} else {
		snd_soc_component_update_bits(comp, 0x3088, 0x100, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x80, 0x80);
		reg1 = 0x30b8;
		reg2 = 0x30bc;
	}

	snd_soc_component_update_bits(comp, reg1, 0x1ff, vrp_new & 0x1ff);
	snd_soc_component_update_bits(comp, reg2, 0x1ff, vrn);
}

/*
 * FDIN data-valid check: wait for 0x3188 bit 0 to follow the stream state and
 * then walk the RCV pop ramp back down when the path is disabled.
 */
static void fdin_dvld_check_event(struct snd_soc_dapm_widget *w, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 on = !!(event & 0x3);
	u32 bit;
	int i = 0;

	while (i != 10) {
		usleep_range(10000, 10200);
		i++;
		bit = snd_soc_component_read32(comp, 0x3188) & 0x1;
		if (on && bit)
			break;
		if (bit | on)
			continue;
		break;
	}
	if (i == 10)
		i = 11;

	if (on && priv->rcv_pop_ramp) {
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0x400);
		usleep_range(100, 110);
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0x300);
		usleep_range(100, 110);
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0x200);
		usleep_range(100, 110);
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0x100);
		usleep_range(100, 110);
		snd_soc_component_update_bits(comp, 0x3094, 0xf00, 0);
		usleep_range(100, 110);
	}

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] %s on %d\n", __func__, on);

	if (!on) {
		snd_soc_component_update_bits(comp, 0x309c, 0x2, 0);
		if (!(get_sp_audio_debug_flag() & 1))
			return;
	} else if (!(get_sp_audio_debug_flag() & 1)) {
		return;
	}

	pr_info("[ASoC:ump9620] '%s' %s, check %s i %d\n", w->name,
		get_event_name(event), i > 9 ? "failed" : "sucessful", i);
}

/*
 * The left/right headphone channel regulators are shared between the analog
 * widgets, so the vendor keeps a module wide enable counter for them.
 */
static void dahpl_chn_en(struct sprd_codec_priv *priv, int on)
{
	static int count_en;
	int ret;

	if (!on) {
		regulator_disable(priv->dahpl_chn);
		count_en--;
		pr_info("[ASoC:ump9620] %s on_off %d, count_en %d", __func__, on,
			count_en);
		return;
	}

	ret = regulator_enable(priv->dahpl_chn);
	if (ret) {
		pr_err("[ASoC:ump9620] %s dahpl_chn regulator enable failed",
		       __func__);
		return;
	}

	count_en++;
	pr_info("[ASoC:ump9620] %s on_off %d, count_en %d", __func__, on,
		count_en);
}

/* debug helper shared by the four DAC enable events */
static void dccal_dbg_show(u32 val, u32 old, const char *const *name)
{
	pr_info("[ASoC:ump9620] dccal select %s %s %s\n", name[val],
		val == old ? "" : "from", val == old ? "" : name[old]);
}

static int daaol_en_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 val;
	int ret;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		val = priv->ana_chip_ver == 0xa000 ? 2 : priv->daaol_buf_dccal;

		if (get_sp_audio_debug_flag() & 1)
			dccal_dbg_show(val, priv->daaol_buf_dccal,
				       c_dccal_mode_name);

		if (val - 1 < 2) {
			snd_soc_component_update_bits(comp, 0x3088, 0x20, 0);
			snd_soc_component_update_bits(comp, 0x3088, 0x10, 0x10);
			snd_soc_component_update_bits(comp, 0x3058, 0x40, 0x40);
			return 0;
		}

		if (val) {
			pr_err("[ASoC:ump9620] daaol_buf_dccal error %d\n",
			       priv->daaol_buf_dccal);
			snd_soc_component_update_bits(comp, 0x3058, 0x40, 0x40);
			return -EINVAL;
		}

		dahpl_chn_en(priv, 1);
		snd_soc_component_update_bits(comp, 0x3088, 0x400, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x800, 0x800);
		snd_soc_component_update_bits(comp, 0x3088, 0x10, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x20, 0x20);
		snd_soc_component_update_bits(comp, 0x3058, 0x40, 0x40);

		ret = sprd_codec_reg_read_poll_timeout(comp, 0x3090, 0x8, 0x4);
		if (ret)
			pr_err("[ASoC:ump9620] %s check cal_done failed %d\n",
			       __func__, ret);

		dahpl_chn_en(priv, 0);
		return ret;
	}

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x3058, 0x40, 0);
		return 0;
	}

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);

	return -EINVAL;
}

static int daaor_en_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 val;
	int ret;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		val = priv->ana_chip_ver == 0xa000 ? 2 : priv->daaor_buf_dccal;

		if (get_sp_audio_debug_flag() & 1)
			dccal_dbg_show(val, priv->daaor_buf_dccal,
				       c_dccal_mode_name);

		if (val - 1 < 2) {
			snd_soc_component_update_bits(comp, 0x3088, 0x4, 0);
			snd_soc_component_update_bits(comp, 0x3088, 0x2, 0x2);
			snd_soc_component_update_bits(comp, 0x3058, 0x20, 0x20);
			return 0;
		}

		if (val) {
			pr_err("[ASoC:ump9620] daaor_buf_dccal %d error\n",
			       priv->daaor_buf_dccal);
			snd_soc_component_update_bits(comp, 0x3058, 0x20, 0x20);
			return -EINVAL;
		}

		dahpl_chn_en(priv, 1);
		snd_soc_component_update_bits(comp, 0x3088, 0x400, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x800, 0x800);
		snd_soc_component_update_bits(comp, 0x3088, 0x2, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x4, 0x4);
		snd_soc_component_update_bits(comp, 0x3058, 0x20, 0x20);

		ret = sprd_codec_reg_read_poll_timeout(comp, 0x3090, 0x2, 0x1);
		if (ret)
			pr_err("[ASoC:ump9620] %s check cal_done failed %d\n",
			       __func__, ret);

		dahpl_chn_en(priv, 0);
		return ret;
	}

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x3058, 0x20, 0);
		return 0;
	}

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);

	return -EINVAL;
}

static int dahpl_en_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 val;
	int ret, do_cali;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		val = priv->ana_chip_ver == 0xa000 ? 2 : priv->dahpl_buf_dccal;

		if (get_sp_audio_debug_flag() & 1)
			dccal_dbg_show(val, priv->dahpl_buf_dccal,
				       c_dccal_mode_name);

		ret = 0;
		do_cali = 0;

		if (val - 1 < 2) {
			snd_soc_component_update_bits(comp, 0x3088, 0x800, 0);
			snd_soc_component_update_bits(comp, 0x3088, 0x400, 0x400);
		} else if (!val) {
			snd_soc_component_update_bits(comp, 0x3088, 0x400, 0);
			snd_soc_component_update_bits(comp, 0x3088, 0x800, 0x800);
			do_cali = 1;
		} else {
			pr_err("[ASoC:ump9620] %s cal mode %d error\n", __func__,
			       priv->dahpl_buf_dccal);
			ret = -EINVAL;
		}

		dahpl_chn_en(priv, 1);

		if (!do_cali)
			return ret;

		ret = sprd_codec_reg_read_poll_timeout(comp, 0x3090, 0x80, 0x40);
		if (ret)
			pr_err("[ASoC:ump9620] %s check cal_done failed %d\n",
			       __func__, ret);

		hp_dac_re_cali(2, comp);

		return ret;
	}

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD) {
		dahpl_chn_en(priv, 0);
		return 0;
	}

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);

	return -EINVAL;
}

static int dahpr_en_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	u32 val;
	int ret;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) {
		val = priv->ana_chip_ver == 0xa000 ? 2 : priv->dahpr_buf_dccal;

		if (get_sp_audio_debug_flag() & 1)
			dccal_dbg_show(val, priv->dahpr_buf_dccal,
				       c_dccal_mode_name);

		if (val - 1 < 2) {
			snd_soc_component_update_bits(comp, 0x3088, 0x100, 0);
			snd_soc_component_update_bits(comp, 0x3088, 0x80, 0x80);
			snd_soc_component_update_bits(comp, 0x3054, 0x20, 0x20);
			usleep_range(20, 30);
			return 0;
		}

		if (val) {
			pr_err("[ASoC:ump9620] %s cal mode %d error\n", __func__,
			       priv->dahpr_buf_dccal);
			snd_soc_component_update_bits(comp, 0x3054, 0x20, 0x20);
			usleep_range(20, 30);
			return -EINVAL;
		}

		dahpl_chn_en(priv, 1);
		snd_soc_component_update_bits(comp, 0x3088, 0x400, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x800, 0x800);
		snd_soc_component_update_bits(comp, 0x3088, 0x80, 0);
		snd_soc_component_update_bits(comp, 0x3088, 0x100, 0x100);
		snd_soc_component_update_bits(comp, 0x3054, 0x20, 0x20);

		ret = sprd_codec_reg_read_poll_timeout(comp, 0x3090, 0x20, 0x10);
		if (ret)
			pr_err("[ASoC:ump9620] %s check cal_done failed %d\n",
			       __func__, ret);

		hp_dac_re_cali(1, comp);
		dahpl_chn_en(priv, 0);

		snd_soc_component_update_bits(comp, 0x3088, 0x400, 0x400);
		snd_soc_component_update_bits(comp, 0x3088, 0x800, 0);
		snd_soc_component_update_bits(comp, 0x3054, 0x20, 0x20);
		usleep_range(20, 30);

		return ret;
	}

	if (event == SND_SOC_DAPM_PRE_PMD || event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x3054, 0x20, 0);
		return 0;
	}

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);

	return -EINVAL;
}

/*
 * VAD clock/power sequencing.  Only PRE_PMU and POST_PMD do real work; every
 * other event either returns immediately or is rejected.
 */
static int vad_clk_power_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct sprd_codec_priv *priv = comp_to_priv(comp);
	int on = !!(event & 0x3);
	int ret = 0;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_PRE_PMU) {
		ret = agdsp_access_enable();
		if (ret) {
			pr_err("[ASoC:ump9620] %s, agdsp_access_enable failed\n",
			       __func__);
			goto done;
		}

		regmap_update_bits_base(priv->agcp_apb, 0x0, 0x8, on ? 0x8 : 0,
					NULL, false, false);
		regmap_update_bits_base(priv->agcp_apb, 0x4, 0x1, 0x1,
					NULL, false, false);
		regmap_update_bits_base(priv->aon_apb2, 0x4, 0x1000, 0x1000,
					NULL, false, false);
		regmap_update_bits_base(priv->aon_apb2, 0x4, 0x800, 0x800,
					NULL, false, false);

		snd_soc_component_update_bits(comp, 0x3018, 0x1000, 0x1000);
		snd_soc_component_update_bits(comp, 0x3018, 0xe0, 0xe0);
		snd_soc_component_update_bits(comp, 0x2140, 0x10, 0x10);

		sprd_codec_ap_adc_power_on(comp, on);

		snd_soc_component_update_bits(comp, 0x211c, 0x1, 0x1);
		osc_4m_clk_cal(comp, on);
		snd_soc_component_update_bits(comp, 0x307c, 0x1000, 0x1000);
		snd_soc_component_update_bits(comp, 0x3010, 0x1, 0x1);
		rc_60m_clk(comp, on);

		regmap_update_bits_base(arch_audio_get_pmu_apb_gpr(), 0x9ec,
					0x200000, 0x200000, NULL, false, false);
		regulator_set_mode(priv->vb, 8);
		goto done;
	}

	if (event == SND_SOC_DAPM_POST_PMU || event == SND_SOC_DAPM_PRE_PMD)
		return 0;

	if (event == SND_SOC_DAPM_POST_PMD) {
		ret = agdsp_access_enable();
		if (ret) {
			pr_err("[ASoC:ump9620] %s, agdsp_access_enable failed\n",
			       __func__);
			goto done;
		}

		regmap_update_bits_base(arch_audio_get_pmu_apb_gpr(), 0x9ec,
					0x200000, 0, NULL, false, false);

		rc_60m_clk(comp, on);
		snd_soc_component_update_bits(comp, 0x3010, 0x1, 0);
		snd_soc_component_update_bits(comp, 0x307c, 0x1000, 0);
		osc_4m_clk_cal(comp, on);
		snd_soc_component_update_bits(comp, 0x211c, 0x1, 0);
		sprd_codec_ap_adc_power_on(comp, on);
		snd_soc_component_update_bits(comp, 0x2140, 0x10, 0);
		snd_soc_component_update_bits(comp, 0x3018, 0xe0, 0);
		snd_soc_component_update_bits(comp, 0x3018, 0x1000, 0);

		regmap_update_bits_base(priv->agcp_apb, 0x0, 0x8, 0,
					NULL, false, false);
		regmap_update_bits_base(priv->agcp_apb, 0x4, 0x1, 0,
					NULL, false, false);

		goto done;
	}

	pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n", __func__, event);

	return -EINVAL;

done:
	agdsp_access_disable();

	return ret;
}

/* charge pump sequencing: settle delays come straight from the vendor blob */
static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	int ret = 0;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x3000, 0xc, 0);
		snd_soc_component_update_bits(comp, 0x301c, 0x700, 0x700);
	} else if (event == SND_SOC_DAPM_PRE_PMU) {
		snd_soc_component_update_bits(comp, 0x3124, 0x800, 0x800);
		snd_soc_component_update_bits(comp, 0x3134, 0xff00, 0xe100);
		snd_soc_component_update_bits(comp, 0x3134, 0xff, 0x9e);

		if (get_sp_audio_debug_flag() & 1)
			CODEC_DBG("%s ANA_DCL14 0x%x\n", __func__,
				  sprd_codec_read(comp, 0x3134));

		snd_soc_component_update_bits(comp, 0x3124, 0x20, 0x20);
		usleep_range(100, 110);
		snd_soc_component_update_bits(comp, 0x301c, 0x700, 0);
		snd_soc_component_update_bits(comp, 0x3000, 0x4, 0x4);
		usleep_range(50, 80);
		snd_soc_component_update_bits(comp, 0x3000, 0x8, 0x8);
		CODEC_DBG("%s %d ms\n", "sprd_codec_wait", 1);
		usleep_range(1000, 1200);
	} else {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		ret = -EINVAL;
	}

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s ret %d\n", __func__, ret);

	return ret;
}

/*
 * DNS (digital noise suppression) coefficients.  The vendor programs the same
 * block on every PRE_PMU and only resets the enables on POST_PMD.
 */
static int dns_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	static const u16 eq_val[] = {
		0x00fa, 0x0030, 0x0001, 0x0001, 0x7fff, 0x00fa, 0x00fa,
		0x00fa, 0x00fa, 0x00fa, 0x00fa, 0x00b1, 0x007d, 0x0058,
		0x003e, 0x002b, 0x001e, 0x0200, 0x02d4, 0x0400, 0x05a8,
		0x0800, 0x0b50, 0x1000, 0x16a0, 0x2000, 0x2d41, 0x4000,
		0x5a82, 0x8000,
	};
	int i;

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] '%s' %s\n", w->name,
			get_event_name(event));

	if (event == SND_SOC_DAPM_POST_PMD) {
		snd_soc_component_update_bits(comp, 0x30a0, 0x10, 0);
		snd_soc_component_update_bits(comp, 0x30a0, 0x20, 0);
		snd_soc_component_update_bits(comp, 0x1084, 0xf000, 0x3000);
		snd_soc_component_update_bits(comp, 0x1084, 0xf00, 0x300);
		snd_soc_component_update_bits(comp, 0x1084, 0x2, 0x2);
		snd_soc_component_update_bits(comp, 0x1088, 0x1, 0x1);
		snd_soc_component_update_bits(comp, 0x1088, 0x1, 0);

		return 0;
	}

	if (event != SND_SOC_DAPM_PRE_PMU) {
		pr_err("[ASoC:ump9620] %s invalid event error 0x%x\n",
		       __func__, event);
		return -EINVAL;
	}

	snd_soc_component_update_bits(comp, 0x30a0, 0x10, 0x10);
	snd_soc_component_update_bits(comp, 0x30a0, 0x20, 0x20);
	snd_soc_component_update_bits(comp, 0x1084, 0xf000, 0x1000);
	snd_soc_component_update_bits(comp, 0x1084, 0xf00, 0x100);
	snd_soc_component_update_bits(comp, 0x1084, 0x2, 0);
	snd_soc_component_update_bits(comp, 0x1088, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x108c, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x1090, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x1094, 0xf00, 0xa00);
	snd_soc_component_update_bits(comp, 0x1094, 0xf0, 0xa0);
	snd_soc_component_update_bits(comp, 0x1094, 0x3, 0x3);
	snd_soc_component_update_bits(comp, 0x10a4, 0x1, 0x1);

	for (i = 0; i < ARRAY_SIZE(eq_val); i++)
		snd_soc_component_update_bits(comp, 0x10a8 + i * 4, 0xffff,
					      eq_val[i]);

	snd_soc_component_update_bits(comp, 0x1120, 0xf, 0x9);
	snd_soc_component_update_bits(comp, 0x1124, 0xffff, 0x20);
	snd_soc_component_update_bits(comp, 0x1128, 0xf, 0x1);
	snd_soc_component_update_bits(comp, 0x112c, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x1130, 0x1, 0x1);
	snd_soc_component_update_bits(comp, 0x1134, 0xf, 0);
	snd_soc_component_update_bits(comp, 0x1138, 0x1f, 0x6);
	snd_soc_component_update_bits(comp, 0x113c, 0x3f, 0x9);

	for (i = 0; i < 13; i++)
		snd_soc_component_update_bits(comp, 0x1140 + i * 4, 0x3fff,
					      0x1000);

	for (i = 0; i < 13; i++)
		snd_soc_component_update_bits(comp, 0x1174 + i * 4, 0x1f, 0x5);

	for (i = 0; i < 13; i++)
		snd_soc_component_update_bits(comp, 0x11ac + i * 4, 0x3fff,
					      0x1000);

	for (i = 0; i < 12; i++)
		snd_soc_component_update_bits(comp, 0x11e0 + i * 4, 0x1f, 0x5);

	snd_soc_component_update_bits(comp, 0x1218, 0x1, 0);
	snd_soc_component_update_bits(comp, 0x1218, 0x1, 0x1);

	return 0;
}
