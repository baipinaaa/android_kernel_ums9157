// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc UMP9620 headset detection driver.
 *
 * Reconstructed from the vendor module snd-soc-sprd-codec-ump9620.ko of the
 * Sunelan Q968 (kernel 5.4.210-android12-9).  The headset block lives in the
 * same module as the codec, so this file only contains the headset half.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/extcon.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/pm_wakeup.h>
#include <linux/string.h>
#include <linux/iio/consumer.h>
#include <linux/input.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include <linux/nvmem-consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/kobject.h>

#include "snd-soc-sprd-codec-ump9620.h"
#include "sprd-asoc-card-utils.h"

#define CODEC_DBG(fmt, args...)						\
	pr_debug("[ASoC:HDST9620] " fmt, ##args)

/* forward declarations, the EIC trigger is used by several helpers */
static void sprd_headset_eic_trig(int bit);
static void sprd_set_eic_trig_level(const char *name, unsigned int mask,
				    int level);
static void sprd_headset_reset(struct sprd_headset *hdst);
static void headset_ldetl_work_func(struct work_struct *work);
static int sprd_headset_type_plugged(void);
static int sprd_detect_type_through_mdet(struct sprd_headset *hdst);
static irqreturn_t sprd_headset_top_eic_handler(int irq, void *data);

/* global headset state, referenced by the codec half as well */
struct sprd_headset *sprd_hdst;
EXPORT_SYMBOL_GPL(sprd_hdst);

/* the ASoC machine driver subscribes here to get plug events */
static BLOCKING_NOTIFIER_HEAD(headset_notifier);

int headset_unregister_notifier(struct notifier_block *nb)
{
	if (!nb) {
		pr_err("[ASoC:HDST9620] %s nb NULL error\n", __func__);
		return -1;
	}

	return blocking_notifier_chain_unregister(&headset_notifier, nb);
}
EXPORT_SYMBOL_GPL(headset_unregister_notifier);

int headset_register_notifier(struct notifier_block *nb)
{
	if (!sprd_hdst) {
		pr_err("[ASoC:HDST9620] %s sprd_hdset is NULL!\n", __func__);
		return 0;
	}

	if (!sprd_hdst->extcon_notifier_registered)
		return 0;

	return blocking_notifier_chain_register(&headset_notifier, nb);
}
EXPORT_SYMBOL_GPL(headset_register_notifier);

int headset_get_plug_state(void)
{
	if (!sprd_hdst) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdset is NULL!\n", __func__);
		return 0;
	}

	return sprd_hdst->plug_state != 0;
}
EXPORT_SYMBOL_GPL(headset_get_plug_state);

/*
 * Called by the machine driver whenever the audio path is started or stopped:
 * while audio is running the mic bias polling has to be suspended.
 */
static void headset_set_audio_state(int on)
{
	struct sprd_headset *hdst = sprd_hdst;
	int polling = on & 0x1;

	mutex_lock(&hdst->audio_on_lock);
	hdst->audio_on = polling;
	mutex_unlock(&hdst->audio_on_lock);

	sprd_enable_hmicbias_polling((~on) & 0x1, 0);
}

/* the FM digital gain hook is not wired on this platform */
int vbc_close_fm_dggain(void)
{
	pr_err("[ASoC:HDST9620] ERR: vbc_close_fm_dggain is not defined!\n");

	return -1;
}
EXPORT_SYMBOL_GPL(vbc_close_fm_dggain);

int __vbc_close_fm_dggain(void) __alias(vbc_close_fm_dggain);
EXPORT_SYMBOL_GPL(__vbc_close_fm_dggain);

int dsp_fm_mute_by_set_dg(void)
{
	pr_err("[ASoC:HDST9620] ERR: dsp_fm_mute_by_set_dg is not defined!\n");

	return -1;
}
EXPORT_SYMBOL_GPL(dsp_fm_mute_by_set_dg);

int __dsp_fm_mute_by_set_dg(void) __alias(dsp_fm_mute_by_set_dg);
EXPORT_SYMBOL_GPL(__dsp_fm_mute_by_set_dg);

/* the codec register window is published by the machine driver */
void sprd_headset_set_global_variables(struct sprd_codec_regmap *info)
{
	sprd_codec_set_global_regmap(info->map, info->offset);
}

static ssize_t sprd_headset_debug_level_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct sprd_headset *hdst = sprd_hdst;

	if (!hdst) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdset is NULL!\n", __func__);
		return -1;
	}

	pr_info("[ASoC:HDST9620] %s debug_level %d\n", __func__,
		hdst->debug_level);

	return sprintf(buf, "%d\n", hdst->debug_level);
}

static ssize_t sprd_headset_debug_level_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct sprd_headset *hdst = sprd_hdst;
	int level;

	if (kstrtoint(buf, 0, &level))
		return -EINVAL;

	if (hdst)
		hdst->debug_level = level;

	return count;
}

static ssize_t sprd_headset_state_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int state;
	int plug;

	if (!hdst) {
		pr_err("[ASoC:HDST9620] sprd_hdset is NULL!\n");
		return -EINVAL;
	}

	state = hdst->state;
	plug = state == 1 ? 2 : 0;
	plug = state == 3 ? 0 : plug + 1;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s status: %#x, headset_state %d\n", __func__,
			  hdst->hw_status, plug);

	return sprintf(buf, "%d\n", plug);
}

static ssize_t sprd_headset_state_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	return count;
}

static void sprd_headset_power_deinit(struct sprd_headset *hdst);

static int sprd_headset_remove(struct platform_device *pdev)
{
	struct sprd_headset *hdst = sprd_hdst;

	if (!hdst)
		return 0;

	if (hdst->extcon_notifier_registered)
		extcon_unregister_notifier(hdst->extcon, 0x15, &hdst->extcon_nb);

	sprd_headset_power_deinit(hdst);

	return 0;
}

/* headset supplies, in the order the DT exposes them */
static const char * const regu_name_list[] = {
	"VREG", "VB", "BG", "BIAS", "MICBIAS1", "MICBIAS2",
	"HEADMICBIAS", "DCL", "DIG_CLK_INTC", "DIG_CLK_HID", "CLK_DCL_32K",
};

#define HDST_REGU_COUNT		ARRAY_SIZE(regu_name_list)

/*
 * Look up the supply table entry matching @name; returns the entry or NULL
 * when the name is not part of the list.
 */
static struct sprd_regulator *sprd_headset_search_power(
					struct sprd_regulator *regu,
					const char *name)
{
	int i;

	for (i = 0; i < HDST_REGU_COUNT; i++) {
		if (!strcmp(regu[i].name, name)) {
			CODEC_DBG("%s i %d\n", __func__, i);
			return &regu[i];
		}
	}

	pr_err("[ASoC:HDST9620] %s: wrong regu name %s\n", __func__, name);

	return NULL;
}

static void sprd_headset_power_deinit(struct sprd_headset *hdst)
{
	int i;

	for (i = 0; i < HDST_REGU_COUNT; i++) {
		if (hdst->regu[i].reg) {
			regulator_put(hdst->regu[i].reg);
			hdst->regu[i].reg = NULL;
			hdst->regu[i].name = NULL;
		}
	}
}

static int sprd_headset_power_init(struct sprd_regulator *regu,
				   struct platform_device *pdev)
{
	int i, ret = 0;

	if (!regu) {
		pr_err("[ASoC:HDST9620] %s power(%p) is null!\n", __func__,
		       regu);
		return -EINVAL;
	}

	for (i = 0; i < HDST_REGU_COUNT; i++) {
		if (!regu_name_list[i]) {
			if (regu[i].reg)
				regulator_put(regu[i].reg);
			regu[i].reg = NULL;
			regu[i].name = NULL;
			regu[i].index = i;
			pr_err("[ASoC:HDST9620] %s regu_name_list[%d] name NULL\n",
			       __func__, i);
			continue;
		}

		regu[i].name = regu_name_list[i];
		regu[i].index = i;
		regu[i].reg = regulator_get(&pdev->dev, regu_name_list[i]);
		if (IS_ERR(regu[i].reg)) {
			long err = PTR_ERR(regu[i].reg);

			regu[i].reg = NULL;
			pr_err("[ASoC:HDST9620] %s Failed to request %ld: %s\n",
			       __func__, err, regu_name_list[i]);
			sprd_headset_power_deinit(sprd_hdst);
			ret = (int)err;
			break;
		}
	}

	return ret;
}

/* EIC helpers: every EIC interrupt lives in the codec register window */
static void sprd_headset_eic_clear(unsigned int mask, const char *name)
{
	CODEC_DBG("%s clear %s\n", "sprd_headset_eic_clear", name);
	sprd_codec_regmap_update_bits(0x224, mask, mask);
}

static void sprd_headset_eic_enable(int enable, const char *name,
				    unsigned int mask)
{
	CODEC_DBG("%s %s %s\n", "sprd_headset_eic_enable",
		  enable ? "enable" : "disable", name);
	sprd_codec_regmap_update_bits(0x218, mask, enable ? mask : 0);
}

static void sprd_headset_eic_trig(int type);

static void sprd_headset_reinit_mdet_eic(void)
{
	sprd_headset_eic_clear(0x800, "HDST_MDET_EIC");
	sprd_headset_eic_enable(1, "HDST_MDET_EIC", 0x800);
	sprd_headset_eic_trig(0xb);
	sprd_codec_regmap_update_bits(0x284, 0x4000, 0);
}

static void sprd_headset_button_eic_reenable(void)
{
	sprd_headset_eic_clear(0x8000, "HDST_BDET_EIC");
	sprd_codec_regmap_update_bits(0x284, 0x4000, 0);
	sprd_headset_eic_trig(0xf);
}

/*
 * Enable/disable one supply of the headset table, tracking the state in the
 * entry itself so that repeated calls stay balanced.
 */
static int sprd_headset_power_set(struct sprd_regulator *regu, int on)
{
	if (!regu) {
		pr_err("%s  power NULL\n", __func__);
		return -EINVAL;
	}

	pr_info("[ASoC:HDST9620] %s name %s, power_on %d, en %d\n", __func__,
		regu->name, on, regu->en);

	if (on) {
		if (!regu->en && !regulator_enable(regu->reg))
			regu->en = 1;
	} else {
		if (regu->en && !regulator_disable(regu->reg))
			regu->en = 0;
	}

	return 0;
}

/*
 * Start/stop the head mic bias polling.  @force skips the "headset present"
 * precondition; the caller either passes it or lets the state machine decide.
 */
void sprd_enable_hmicbias_polling(int on, int force)
{
	struct sprd_headset *hdst = sprd_hdst;
	struct sprd_regulator *regu;
	const char *reason = "force";

	if (!force) {
		if (!hdst->plug_state) {
			pr_info("[ASoC:HDST9620] %s no headset insert!\n", __func__);
			return;
		}

		mutex_lock(&hdst->btn_detecting_lock);
		reason = "state";
		if (!(hdst->state & 2) || hdst->in_ldetl) {
			pr_info("[ASoC:HDST9620] %s no headset plugin or button pressing 0x%x\n",
				__func__, hdst->state);
			mutex_unlock(&hdst->btn_detecting_lock);
			return;
		}
		mutex_unlock(&hdst->btn_detecting_lock);
	}

	CODEC_DBG("%s %s set polling to %s, current_polling_state %d\n", __func__,
		  reason, on ? "enable" : "disable", hdst->hmicbias_on);

	mutex_lock(&hdst->hmicbias_polling_lock);

	if (on) {
		if (hdst->hmicbias_on)
			goto out_unlock;

		regu = sprd_headset_search_power(hdst->regu, "HEADMICBIAS");
		if (regulator_is_enabled(regu->reg) >= 1)
			regulator_set_mode(regu->reg, 8);

		regu = sprd_headset_search_power(hdst->regu, "DIG_CLK_HID");
		if (!regu) {
			pr_err("[ASoC:HDST9620] %s  power NULL\n",
			       "sprd_headset_power_set");
			goto on_done;
		}

		sprd_headset_power_set(regu, 1);
on_done:
		hdst->hmicbias_on = 1;
	} else {
		if (!hdst->hmicbias_on)
			goto out_unlock;

		sprd_codec_regmap_update_bits(0x144, 0x4000, 0);

		regu = sprd_headset_search_power(hdst->regu, "DIG_CLK_HID");
		if (!regu) {
			pr_err("[ASoC:HDST9620] %s  power NULL\n",
			       "sprd_headset_power_set");
			goto off_done;
		}

		sprd_headset_power_set(regu, 0);
off_done:
		regu = sprd_headset_search_power(hdst->regu, "HEADMICBIAS");
		if (regulator_is_enabled(regu->reg) >= 1)
			regulator_set_mode(regu->reg, 2);

		hdst->hmicbias_on = 0;
	}

out_unlock:
	mutex_unlock(&hdst->hmicbias_polling_lock);
}

static int sprd_headset_parse_dt(struct sprd_headset *hdst);

static int sprd_headset_probe(struct platform_device *pdev)
{
	struct sprd_headset *hdst;
	int ret;

	if (!pdev) {
		pr_err("[ASoC:HDST9620] %s: platform device is NULL!\n", __func__);
		return -EINVAL;
	}

	if (!(&pdev->dev)) {
		pr_err("[ASoC:HDST9620] %s: dev of platform_device is NULL!\n",
		       __func__);
		return -EINVAL;
	}

	hdst = devm_kmalloc(&pdev->dev, 0x898, GFP_KERNEL);
	if (!hdst)
		return -ENOMEM;

	hdst->pdev = pdev;

	ret = sprd_headset_parse_dt(hdst);
	if (ret < 0) {
		pr_err("[ASoC:HDST9620] Failed to parse dt for headset.\n");
		return ret;
	}

	if (hdst->eu_us_switch_gpio) {
		ret = devm_gpio_request(&pdev->dev, hdst->eu_us_switch_gpio,
					"headset_eu_us_switch");
		if (ret < 0) {
			pr_err("[ASoC:HDST9620] %s failed to request GPIO_%d(headset_switch)\n",
			       __func__, hdst->eu_us_switch_gpio);
			return ret;
		}
	} else {
		pr_info("[ASoC:HDST9620] automatic EU/US type switch is unsupported\n");
	}

	ret = devm_gpio_request(&pdev->dev, hdst->aud_int_gpio, "aud_int_all");
	if (ret < 0) {
		pr_err("[ASoC:HDST9620] %s failed to request GPIO_%d(%s)\n",
		       __func__, hdst->aud_int_gpio, "aud_int_all");
		return ret;
	}

	CODEC_DBG("%s name %s, gpio %d\n", __func__, "aud_int_all",
		  hdst->aud_int_gpio);

	sprd_hdst = hdst;

	pr_info("[ASoC:HDST9620] headset_detect_probe success\n");

	return 0;
}

/* Average a pair of headset ADC samples, ten times over */
static int sprd_get_adc_value(struct iio_channel *chan)
{
	int val = 0, val2 = 0, sum = 0, i, ret;

	for (i = 0; i < 10; i++) {
		sprd_codec_regmap_update_bits(0xdc, 0x10, 0);

		val = 0;
		ret = iio_read_channel_raw(chan, &val);
		if (ret < 0) {
			pr_err("[ASoC:HDST9620] %s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
			return ret;
		}
		if (val < 0)
			return sum / 10;

		CODEC_DBG("%s: adc: %d\n", "sprd_headset_adc_get", val);

		sprd_codec_regmap_update_bits(0xdc, 0x10, 0x10);

		val2 = 0;
		ret = iio_read_channel_raw(chan, &val2);
		if (ret < 0)
			pr_err("[ASoC:HDST9620] %s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
		else
			val = val2;

		sum += (short)((val + val2) >> 1);
	}

	return sum / 10;
}

/*
 * Convert a raw ADC reading into the "ideal" value with the efuse A/B/E
 * calibration points of this board.
 */
static int sprd_adc_to_ideal(int adc, int scale)
{
	s64 num, den, base, span;

	if (!adc_cal_headset_valid) {
		pr_info("[ASoC:HDST9620] %s efuse A,B,E hasn't been calculated!\n",
			__func__);
		return adc;
	}

	if (adc * 9 + adc_cal_headset[1] < adc_cal_headset[0] * 10)
		return adc;

	base = (s64)adc * 9 - 10 * adc_cal_headset[0] + adc_cal_headset[1];
	span = (s64)adc_cal_headset[2] - adc_cal_headset[3];

	num = base * scale * (span + 1200);
	den = (sprd_hdst->adc_cal_flag ? 3 : 1) * base * (span - 1200) * 100 +
	      (s64)(adc_cal_headset[1] - adc_cal_headset[0]) *
	      adc_cal_headset[2] * 2400;

	CODEC_DBG("exp1 %lld, exp2 %lld, exp3 %lld, exp4 %lld\n",
		  num, den, span, base);
	CODEC_DBG("denominator %lld, numerator %lld\n", den, num);

	return (int)(num / den);
}

/* efuse calibration points for the headset ADC, filled by parse_dt */
static u8 adc_cal_headset_valid;
static int adc_cal_headset[4];

/* ------------------------------------------------------------------ */
/* EIC helpers                                                         */
/* ------------------------------------------------------------------ */

/*
 * set the trigger level of an EIC source.  "name" is only used for the
 * debug trace; the register bit is shared (CODEC_EIC 0x214 bit 10).
 */
/*
 * Set the trigger level of one EIC source.  Bit 10 of 0x214 is shared by the
 * whole bank, so the caller has to pass the mask of its own source
 * (INSERT_ALL 0x400, MDET 0x800, LDETL 0x1000, BDET 0x8000).
 */
static void sprd_set_eic_trig_level(const char *name, unsigned int mask,
				    int level)
{
	CODEC_DBG("%s set %s trig level to %s\n", "sprd_set_eic_trig_level",
		  name, level ? "high" : "low");
	sprd_codec_regmap_update_bits(0x214, mask, level ? mask : 0);
}

static void sprd_headset_eic_trig(int bit)
{
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;

	/* pulse the trigger bit of the insert/mdet/bdet EIC block */
	sprd_codec_regmap_update_bits(0x228, 1U << bit, 1U << bit);

	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	CODEC_DBG("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x\n",
		  "sprd_headset_eic_trig", sts0, iev, ie, ris, mis, sts1,
		  intc_en, int34);
}

static void sprd_headset_disable_eic(void)
{
	sprd_headset_eic_enable(0, "HDST_INSERT_ALL_EIC", 0x400);
	sprd_headset_eic_enable(0, "HDST_MDET_EIC", 0x800);
	sprd_headset_eic_enable(0, "HDST_BDET_EIC", 0x8000);
	sprd_set_eic_trig_level("HDST_INSERT_ALL_EIC", 0x400, 1);
}

/*
 * Every internal EIC source shares the pending register 0x224: the low bits
 * are write-one-to-clear status latches, 0xfc00 keeps the configuration of
 * the block while dropping all latches at once.
 */
static void sprd_headset_clear_all_eic(void)
{
	sprd_codec_regmap_update_bits(0x224, 0xffff, 0xfc00);
	pr_info("%s clear all internal eic\n", __func__);
}

/* one hardware debounce timer per EIC source, 0x268..0x27c */
static void sprd_eic_hardware_debounce_set(unsigned int reg, int ms,
					   unsigned int val)
{
	sprd_codec_regmap_update_bits(reg, 0xffff, val);
	CODEC_DBG("%s reg 0x%x, ms %d, val %x\n", __func__, reg, ms, val);
}

/* 0x214 programs the trigger level of the whole bank in one write */
static void sprd_set_all_eic_trig_level(const char *level)
{
	sprd_codec_regmap_update_bits(0x214, 0xffff, 0xffff);
	pr_info("%s set all internal eic trig level to %s\n", __func__, level);
}

static void sprd_headset_all_eic_enable(int enable)
{
	sprd_codec_regmap_update_bits(0x218, 0xffff, enable ? 0xffff : 0);
	pr_info("%s %s all internal eic\n", __func__,
		enable ? "enable" : "disable");
}

/*
 * Program the headset block of the codec: unmask the analog detect path,
 * arm all EIC sources, give them their debounce time and leave the bank
 * disabled until a detection mode is selected.
 */
static void sprd_headset_eic_init(void)
{
	unsigned int int0, int1, int6, mis;
	unsigned int sts0, iev, ie, ris, sts1, intc_en, int34;
	unsigned int bits = 0xffff;

	/* analog detect path and the two clock gates of the block */
	sprd_codec_regmap_update_bits(0xd0, 0x8000, 0x8000);
	sprd_codec_regmap_update_bits(0xd8, 0x10, 0x10);
	sprd_codec_regmap_update_bits(0xd8, 0x200, 0x200);
	sprd_codec_regmap_update_bits(0xd0, 0x80, 0x80);
	sprd_codec_regmap_update_bits(0xd8, 0x8000, 0x8000);
	sprd_codec_regmap_update_bits(0x08, 0x1000, 0x1000);

	/* all three detect interrupts, the mask is cleared below */
	sprd_codec_regmap_update_bits(0x204, 0xffff, 0xffff);

	int0 = 0;
	int1 = 0;
	int6 = 0;
	sprd_codec_regmap_read(0x200, &int0);
	sprd_codec_regmap_read(0x204, &int1);
	sprd_codec_regmap_read(0x218, &int6);
	pr_info("%s INT0(0200) %x, INT1(0204) %x, INT6(0218) %x\n",
		__func__, int0, int1, int6);

	sprd_headset_clear_all_eic();

	/* insert all, insert bdet, insert all again, mdet, bdet, gdet */
	sprd_eic_hardware_debounce_set(0x268, 2, 0x4002);
	sprd_eic_hardware_debounce_set(0x26c, 0x32, 0x4032);
	sprd_eic_hardware_debounce_set(0x270, 2, 0x4002);
	sprd_eic_hardware_debounce_set(0x274, 2, 0x4002);
	sprd_eic_hardware_debounce_set(0x278, 2, 0x4002);
	sprd_eic_hardware_debounce_set(0x27c, 6, 0x4006);

	sprd_set_all_eic_trig_level("high");
	sprd_headset_all_eic_enable(0);

	/* force the interrupt controller to drop whatever is still latched */
	sprd_codec_regmap_update_bits_force(0x284, bits, bits);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	pr_info("intc force clear, INT8(220.MIS) %x, bits %x\n", mis, bits);
	sprd_codec_regmap_update_bits_force(0x284, 0xffff, 0);

	sts0 = 0;
	iev = 0;
	ie = 0;
	ris = 0;
	mis = 0;
	sts1 = 0;
	intc_en = 0;
	int34 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	pr_info("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		__func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);
}

/*
 * cut the analog part of the headset block when nothing is plugged in,
 * otherwise only the detect level of the type ADC has to be restored.
 */
int sprd_headset_reset_hw_status(struct sprd_headset *hdst)
{
	struct sprd_regulator *pwr;

	if (hdst->plug_state)
		return 0;

	sprd_codec_regmap_update_bits(0xdc, 0x20, 0);

	pwr = sprd_headset_search_power(hdst->regu, "HEADMICBIAS");
	if (!pwr)
		pr_err("%s  power NULL\n", "sprd_headset_power_set");
	else
		sprd_headset_power_set(pwr, 0);

	if (!hdst->hw_type) {
		pwr = sprd_headset_search_power(hdst->regu, "BIAS");
		if (!pwr)
			pr_err("%s  power NULL\n", "sprd_headset_power_set");
		else
			sprd_headset_power_set(pwr, 0);
	}

	sprd_codec_regmap_update_bits(0xd0, 0x700, 0x700);
	sprd_codec_regmap_update_bits(0xd0, 0x2000, 0);

	return 0;
}

/*
 * "status" carries the detect result of the charger/hs type: +0x04 is the
 * detected type, 0 = headset, 1 = headphone, anything else is unknown.
 */
void sprd_headset_set_hw_status(struct sprd_headset *hdst,
				struct sprd_headset_hw_status *status)
{
	struct sprd_regulator *pwr;
	unsigned int val = 0;

	sprd_codec_regmap_update_bits(0xd8, 8, 8);

	switch (status->type) {
	case 0:
		sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);
		break;
	case 1:
		sprd_codec_regmap_update_bits(0xd4, 0x1000, 0x1000);
		sprd_codec_regmap_update_bits(0xd0, 0x4000, 0);
		break;
	default:
		sprd_codec_regmap_read(0xd8, &val);
		CODEC_DBG("filter detect_l HDT2 0x%04x\n", val);
		break;
	}

	pwr = sprd_headset_search_power(hdst->regu, "BIAS");
	if (!pwr)
		pr_err("%s  power NULL\n", "sprd_headset_power_set");
	else
		sprd_headset_power_set(pwr, 1);
}

/*
 * Top level EIC handler: every detect source of the codec reports into the
 * same irq.  Snapshot both banks, force the interrupt controller clear, then
 * hand the pending sources over to their workqueues.
 */
static irqreturn_t sprd_headset_top_eic_handler(int irq, void *data)
{
	struct sprd_headset *hdst = data;
	unsigned int bits, mis, sts0, iev, ie, ris, sts1, intc_en, int34, val;
	int ret;

	pr_info("[ASoC:HDST9620] %s enter\n", __func__);

	if (!hdst) {
		pr_err("[ASoC:HDST9620] %s hdst NULL\n", __func__);
		return IRQ_HANDLED;
	}

	/*
	 * The vendor tests data against (void *)-0x430 here, a sentinel no
	 * caller ever passes; keep the check and its message.
	 */
	if (hdst == (struct sprd_headset *)-0x430) {
		pr_err("[ASoC:HDST9620] %s: power is NULL!\n", __func__);
		return IRQ_HANDLED;
	}

	bits = 0;
	sprd_codec_regmap_read(0x288, &bits);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	sts0 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	iev = 0;
	sprd_codec_regmap_read(0x214, &iev);
	ie = 0;
	sprd_codec_regmap_read(0x218, &ie);
	ris = 0;
	sprd_codec_regmap_read(0x21c, &ris);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	sts1 = 0;
	sprd_codec_regmap_read(0x22c, &sts1);
	intc_en = 0;
	sprd_codec_regmap_read(0x280, &intc_en);
	int34 = 0;
	sprd_codec_regmap_read(0x288, &int34);
	pr_info("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		__func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);

	/* 0x288 carries the mask of the sources that fired */
	sprd_codec_regmap_update_bits_force(0x284, bits, bits);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	pr_info("[ASoC:HDST9620] intc force clear, INT8(220.MIS) %x, bits %x\n",
		mis, bits);

	irq_set_irq_type(hdst->irq, IRQ_TYPE_LEVEL_HIGH);

	if (bits & 0x2) {
		/* left detect: let the worker do the slow analog work */
		pm_wakeup_ws_event(hdst->typec_ws, 500, false);
		hdst->int_shadow = bits;
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->ldetl_wq,
				      &hdst->ldetl_dwork, 0);
	}

	if (!(bits & 0x4000))
		return IRQ_HANDLED;

	if (!(iev & 0x9c00)) {
		/* no source we can handle, dump everything and start over */
		sts0 = 0;
		iev = 0;
		ie = 0;
		ris = 0;
		mis = 0;
		sts1 = 0;
		intc_en = 0;
		int34 = 0;
		sprd_codec_regmap_read(0x184, &sts0);
		sprd_codec_regmap_read(0x214, &iev);
		sprd_codec_regmap_read(0x218, &ie);
		sprd_codec_regmap_read(0x21c, &ris);
		sprd_codec_regmap_read(0x220, &mis);
		sprd_codec_regmap_read(0x22c, &sts1);
		sprd_codec_regmap_read(0x280, &intc_en);
		sprd_codec_regmap_read(0x288, &int34);
		pr_err("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		       __func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);
		sprd_headset_reset(hdst);
		return IRQ_HANDLED;
	}

	if (!(iev & 0x8000) && hdst->extcon_notifier_registered) {
		pr_err("[ASoC:HDST9620] top_eic_handler only support bdet in typec analog headset, eic_mis 0x%x\n",
		       bits);
		return IRQ_HANDLED;
	}

	pm_wakeup_ws_event(hdst->typec_ws, 500, false);

	if (iev & 0x400) {
		val = 0;
		sprd_codec_regmap_read(0x214, &val);
		if ((val & 0x400) && !hdst->hw_type)
			complete(&hdst->insert_all_completion);

		ret = cancel_delayed_work(&hdst->detect_all_dwork);
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->detect_all_wq,
				      &hdst->detect_all_dwork, 0);
		pr_info("[ASoC:HDST9620] %s insert_all irq active, exit, ret %d\n",
			__func__, ret & 1);
	}

	if (iev & 0x800) {
		ret = cancel_delayed_work(&hdst->mic_dwork);
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->mic_wq,
				      &hdst->mic_dwork, 2);
		pr_info("[ASoC:HDST9620] %s mdet irq active, ret %d\n",
			__func__, ret & 1);
	}

	if (iev & 0x1000) {
		if (hdst->hw_type == 1) {
			pr_err("[ASoC:HDST9620] %s: don't need ldetl_irq in JACK_TYPE_NC!\n",
			       __func__);
			return IRQ_HANDLED;
		}

		val = 0;
		sprd_codec_regmap_read(0x214, &val);
		if (!(val & 0x1000))
			complete(&hdst->detect_completion);

		ret = cancel_delayed_work(&hdst->ldetl_dwork);
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->ldetl_wq,
				      &hdst->ldetl_dwork, 0);
		pr_info("[ASoC:HDST9620] %s ldetl irq active, plug_state_last %d\n",
			__func__, hdst->plug_state);
	}

	if (iev & 0x8000) {
		ret = cancel_delayed_work(&hdst->button_dwork);
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->button_wq,
				      &hdst->button_dwork, 0);
		pr_info("[ASoC:HDST9620] %s bdet irq active, ret %d\n",
			__func__, ret & 1);
	}

	return IRQ_HANDLED;
}

/*
 * Arm the detect source that matches the current jack type.  The EIC block
 * keeps a single "insert all" bank, the left-detect bank is used for a
 * 4 pole plug so that the MIC/button ADC can be sampled afterwards.
 */
int sprd_headset_eic_plugin_enable(void)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;

	if (!hdst)
		return -EINVAL;

	if (hdst->extcon_notifier_registered) {
		gpiod_set_value_cansleep(hdst->mic_gpio, 0);
		return 0;
	}

	switch (hdst->hw_type) {
	case 0:
		sprd_headset_eic_enable(1, "HDST_LDETL_EIC", 0x1000);
		sprd_headset_eic_trig(0xc);
		break;
	case 1:
		sprd_headset_eic_enable(1, "HDST_INSERT_ALL_EIC", 0x400);
		sprd_headset_eic_trig(0xa);
		break;
	default:
		break;
	}

	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	pr_info("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		"sprd_headset_eic_plugin_enable", sts0, iev, ie, ris, mis, sts1,
		intc_en, int34);

	return 0;
}

/* kcontrol of the DAPM widget that routes the jack to the analog path */
static const struct snd_ctl_elem_id hp_jack_switch_id = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Virt HP Jack Switch",
};

static void dapm_jack_switch_control(struct sprd_headset *hdst, int on)
{
	struct snd_soc_component *comp = hdst->comp2;
	struct snd_soc_card *card = comp ? comp->card : NULL;
	struct snd_ctl_elem_value value;
	struct snd_kcontrol *kctl;

	if (!card) {
		pr_err("%s card is NULL!\n", "dapm_jack_switch_control");
		return;
	}

	pr_info("%s, %s\n", "dapm_jack_switch_control", on ? "on" : "off");

	memset(&value, 0, sizeof(value));
	kctl = snd_ctl_find_id(card->snd_card, &hp_jack_switch_id);
	if (!kctl) {
		pr_err("%s can't find kctrl '%s'\n", "dapm_jack_switch_control",
		       hp_jack_switch_id.name);
		return;
	}

	value.value.integer.value[0] = on;
	snd_soc_dapm_put_volsw(kctl, &value);
}

void sprd_headset_jack_report(struct sprd_headset *hdst,
			      struct snd_soc_jack *jack, int status, int mask)
{
	if (mask & 1)
		dapm_jack_switch_control(hdst, status != 0);

	snd_soc_jack_report(jack, status, mask);
}

/*
 * The type-c switch supply is toggled from a work context: the notifier
 * itself only records the new state and kicks the worker.
 */
static void __maybe_unused sprd_headset_typec_work(struct sprd_headset *hdst)
{
	int ret;

	CODEC_DBG("typec_work typec_attached %d\n", hdst->typec_attached);

	if (hdst->typec_attached)
		sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);

	if (!hdst->typec_supply) {
		pr_info("Typec switch supply is NULL.\n");
		goto out;
	}

	if (hdst->typec_attached) {
		ret = regulator_set_voltage(hdst->typec_supply,
					    hdst->typec_supply_vol,
					    hdst->typec_supply_vol);
		if (ret < 0) {
			pr_err("Failed(%d) to set typec switch supply voltage at %duV\n",
			       ret, hdst->typec_supply_vol);
			goto out;
		}

		ret = regulator_enable(hdst->typec_supply);
		if (ret) {
			pr_err("Power typec swich supply failed(%d)!\n", ret);
			goto out;
		}
	} else {
		ret = regulator_disable(hdst->typec_supply);
		if (ret) {
			pr_err("Power typec swich supply failed(%d)!\n", ret);
			goto out;
		}
	}

out:
	pm_wakeup_ws_event(hdst->typec_ws, 500, false);
	queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->detect_all_wq,
			      &hdst->detect_all_dwork, 0);
}

static int __maybe_unused sprd_headset_typec_notifier(struct notifier_block *nb,
				       unsigned long status, void *data)
{
	struct sprd_headset *hdst = container_of(nb, struct sprd_headset,
						 extcon_nb);

	CODEC_DBG("typec_notifier status %ld\n", status);

	if (!hdst->extcon_notifier_registered) {
		pr_err("Analog typec headset is not supported!\n");
		return 0;
	}

	hdst->typec_attached = (status != 0);
	sprd_headset_typec_work(hdst);

	return 1;
}

/*
 * MDET (microphone detect) EIC worker: the interrupt already cleared the
 * source, all that is left is to wait for the debounced STS0 bit.
 */
static void sprd_mdet_eic_work(struct work_struct *work)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int sts0 = 0;

	sprd_codec_regmap_read(0x184, &sts0);

	if (!(sts0 & 0x400)) {
		pr_err("%s check %s debounce failed\n", "sprd_mdet_eic_work",
		       "HDST_MDET_EIC");
		pr_err("%s invalid, mdet_insert %d\n", "sprd_mdet_eic_work", 0);
		sprd_headset_reinit_mdet_eic();
		return;
	}

	complete(&hdst->mdet_completion);
	sprd_headset_eic_enable(0, "HDST_MDET_EIC", 0x800);
	sprd_headset_eic_clear(0x800, "HDST_MDET_EIC");
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);

	sprd_codec_regmap_read(0x184, &sts0);
	pr_info("%s STS0 0x%x\n", "sprd_mdet_eic_work", sts0);
}

/*
 * Arm the insert-all EIC so that a plug-out of a 3-pole jack is seen.
 */
int sprd_headset_prepare_insert_all_plugout(struct sprd_headset *hdst)
{
	if (hdst->extcon_notifier_registered)
		return 0;

	sprd_headset_eic_clear(0x400, "HDST_INSERT_ALL_EIC");
	sprd_set_eic_trig_level("HDST_INSERT_ALL_EIC", 0x400, 0);
	sprd_headset_eic_enable(1, "HDST_INSERT_ALL_EIC", 0x400);
	sprd_headset_eic_trig(0xa);
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);

	return 0;
}

/*
 * Arm the LDETL EIC (used for 4-pole jacks) and give the analog block time
 * to settle before the interrupt may fire.
 */
void sprd_headset_prepare_ldetl(struct sprd_headset *hdst)
{
	sprd_set_eic_trig_level("HDST_LDETL_EIC", 0x1000, 1);
	sprd_headset_eic_clear(0x1000, "HDST_LDETL_EIC");
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);
	sprd_headset_eic_enable(1, "HDST_LDETL_EIC", 0x1000);
	sprd_headset_eic_trig(0xc);
	msleep(20);
}

/*
 * Fallback of every detect path: force the whole headset block back into a
 * known idle state.  The sequence is always the same - dump both interrupt
 * banks, release a button that may still be reported, power the mic bias
 * rails down, clear the analog state machine and re-arm the EIC block for the
 * next plugin.
 */
static void sprd_headset_reset(struct sprd_headset *hdst)
{
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;

	pr_info("[ASoC:HDST9620] %s hdst_hw_status %s, audio_on %d, plug_state_last %d\n",
		__func__, eic_hw_state[hdst->detect_mode], hdst->audio_on,
		hdst->plug_state);

	sts0 = 0;
	iev = 0;
	ie = 0;
	ris = 0;
	mis = 0;
	sts1 = 0;
	intc_en = 0;
	int34 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	pr_err("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
	       __func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);

	/* 0x7fff drops every source latch but keeps the ldetl level bit */
	sprd_codec_regmap_update_bits_force(0x284, 0x7fff, 0x7fff);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	pr_info("[ASoC:HDST9620] intc force clear, INT8(220.MIS) %x, bits %x\n",
		mis, 0x7fff);

	sprd_codec_regmap_update_bits(0xd8, 0x8, 0);
	sprd_codec_regmap_update_bits(0xd8, 0x7, 0x3);

	/*
	 * The vendor tests the global pointer here instead of the argument, the
	 * helper itself works on sprd_hdst as well.
	 */
	if (hdst->button_pressed == 1) {
		sprd_headset_button_release_verify();
		hdst->in_ldetl = 0;
	}

	if (hdst->state & 0x3) {
		hdst->state &= ~0x3;
		sprd_headset_jack_report(hdst, &hdst->jack, 0, 0x3);
		hdst->in_ldetl = 0;
	}

	if (!hdst->insert_all_active)
		sprd_codec_regmap_update_bits(0xd4, 0x2000,
					      hdst->extcon_notifier_registered ?
					      0 : 0x2000);

	sprd_enable_hmicbias_polling(0, 1);

	sprd_headset_power_set(sprd_headset_search_power(hdst->regu,
							 "HEADMICBIAS"), 0);
	sprd_headset_power_set(sprd_headset_search_power(hdst->regu,
							 "BIAS"), 0);

	msleep(0x32);

	/* mic bias ADC path off */
	sprd_codec_regmap_update_bits(0xdc, 0x20, 0);

	hdst->mic_bias_state = 0;
	hdst->button_status = 0;
	hdst->insert_all_active = 0;
	hdst->insert_started = 0;
	hdst->reset_marker = 0;
	hdst->plug_state = 0;
	hdst->headset_type = -1;
	hdst->in_ldetl = 0;
	hdst->detect_again = 0;
	hdst->detect_deadline = 0;

	sprd_codec_regmap_update_bits(0xd0, 0x700, 0x700);
	sprd_codec_regmap_update_bits(0xd0, 0x2000, 0);
	sprd_headset_all_eic_enable(0);
	sprd_headset_clear_all_eic();
	sprd_set_all_eic_trig_level("high");
	sprd_codec_regmap_update_bits_force(0x284, 0x7fff, 0);
	sprd_headset_eic_plugin_enable();
}

/*
 * Measure the voltage present on the typec HPL line: select the ADC input,
 * average it, convert it into millivolt through the calibration data.
 */
int sprd_headset_typec_get_hpl_voltage(struct sprd_headset *hdst)
{
	int raw, adc, ideal, mv;

	sprd_codec_regmap_update_bits(0xdc, 0x40, 0x40);
	sprd_codec_regmap_update_bits(0xdc, 0x20, 0);
	sprd_codec_regmap_update_bits(0xdc, 0xf, 5);

	raw = sprd_get_adc_value(hdst->headmic_chan);
	adc = raw;
	if (raw < 0) {
		dev_err(&hdst->pdev->dev, "adc error, adc_left_average %d\n", raw);
		adc = 0;
	}

	sprd_codec_regmap_update_bits(0xdc, 0xf, 4);

	ideal = sprd_adc_to_ideal(adc, hdst->adc_scale);
	mv = ideal * 1250 / 10000;

	pr_info("adc_left_average %d, adc_left_ideal %d, V_ideal %s %d mV\n",
		adc, ideal,
		(raw < 0 || ideal > 0xffe) ? "outrange!" : "", mv);

	return mv;
}

/*
 * Wait for the insert-all interrupt to be flagged and for the debounced
 * status bit to become stable.
 */
int sprd_headset_valid_insert_all(int msleep_ms)
{
	unsigned int mis, a, b, sts0, iev, ie, ris, mis8, sts1, intc_en, int34;

	sprd_codec_regmap_read(0x220, &mis);
	if (!(mis & 0x400)) {
		pr_err("fatal error, HDST_INSERT_ALL_EIC invalid, INT8(220.MIS) %x\n",
		       0);
		return -3;
	}

	sprd_codec_regmap_read(0x200, &a);
	msleep(msleep_ms);
	sprd_codec_regmap_read(0x200, &b);

	if (((a >> 10) & 1) != ((b & 0x400) >> 10)) {
		pr_err("insert all eic check debounce failed\n");
		return -2;
	}

	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis8);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);

	pr_debug("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		 "sprd_headset_valid_insert_all", sts0, iev, ie, ris, mis8, sts1,
		 intc_en, int34);

	return 0;
}

/*
 * A 4-pole jack was detected: report it and arm the button detection EIC.
 */
/*
 * Sample the two ladders of the jack and classify what is plugged in.
 *
 * The detect pin is first routed to the MIC side of the plug and its voltage
 * is measured, then the left/right side is selected (4 for a 4 pole jack,
 * 5 for the 3 pole one) and measured again.  The pair of values decides
 * between a headset, a headphone and "nothing usable".
 *
 * Returns -1 when nothing was detected, 0 or 2 for a 4 pole plug (2 means the
 * first ladder was clearly above ground, i.e. a mic is present) and 1 for a 3
 * pole plug, whose type is then reported by the caller.
 */
static int sprd_headset_type_plugged(void)
{
	struct sprd_headset *hdst = sprd_hdst;
	struct iio_channel *chan;
	int raw, adc, ideal, mv, raw2, ideal2, mv2, out2, ret;
	unsigned int sts0;
	unsigned int insert_status;

	if (!hdst) {
		CODEC_DBG("%s %d\n", __func__, __LINE__);
		return -1;
	}

	if (!hdst->aud_int_gpio)
		pr_info("[ASoC:HDST9620] automatic type switch is unsupported\n");
	else
		gpiod_direction_output_raw(gpio_to_desc(hdst->aud_int_gpio), 0);

	usleep_range(4000, 4100);

	CODEC_DBG("%s, get adc value of headmic in little scale\n", __func__);

	/* mic bias ADC path on, offset compensation off, mic input selected */
	sprd_codec_regmap_update_bits(0xdc, 0x40, 0x40);
	sprd_codec_regmap_update_bits(0xdc, 0x20, 0);
	sprd_codec_regmap_update_bits(0xdc, 0xf, 0);

	chan = hdst->headmic_chan;

	raw = sprd_get_adc_value(chan);
	adc = 0;
	out2 = raw < 0;
	if (raw < 0)
		pr_err("[ASoC:HDST9620] %s adc error, adc_mic_average %d\n",
		       __func__, raw);
	else
		adc = raw;

	ideal = sprd_adc_to_ideal(adc, hdst->adc_scale);
	mv = ideal * 1250 / 10000;
	pr_info("[ASoC:HDST9620] %s, adc_mic_average %d, adc_mic_ideal %d, V_ideal %s %d mV\n",
		__func__, adc, ideal,
		(adc > 0xffe || raw < 0) ? "outrange!" : "", mv);

	if (!hdst->hw_type)
		sprd_codec_regmap_update_bits(0xdc, 0xf, 4);
	else if (hdst->hw_type == 1)
		sprd_codec_regmap_update_bits(0xdc, 0xf, 5);

	raw2 = sprd_get_adc_value(chan);
	if (raw2 < 0) {
		pr_err("[ASoC:HDST9620] %s adc error, adc_left_average %d\n",
		       __func__, raw2);
		raw2 = 0;
		out2 = 1;
	}

	ideal2 = sprd_adc_to_ideal(raw2, hdst->adc_scale);
	mv2 = ideal2 * 1250 / 10000;
	pr_info("[ASoC:HDST9620] %s adc_left_average %d, adc_left_ideal %d, V_ideal %s %d mV\n",
		__func__, raw2, ideal2, (raw2 > 0xffe || out2) ? "outrange!" : "",
		mv2);

	/* a 3 pole jack that is already reported must stay a 3 pole jack */
	if (hdst->audio_on && (hdst->state & 0x1))
		pr_info("[ASoC:HDST9620] %s when audio_on and 3pole was reported, report 3pole instead\n",
			__func__);

	pr_info("[ASoC:HDST9620] %s sprd_half_adc_gnd %d, sprd_adc_gnd %d,sprd_one_half_adc_gnd %d, threshold_3pole %d\n",
		__func__, hdst->sprd_half_adc_gnd, hdst->sprd_adc_gnd,
		hdst->one_half_adc_gnd, hdst->threshold_3pole);

	/* the second ladder is way above ground: a mic is on the plug */
	if (ideal2 > hdst->one_half_adc_gnd) {
		if (abs(ideal - ideal2) >= hdst->one_half_adc_gnd)
			return -1;

		sprd_codec_regmap_update_bits(0xd8, 0x7, 0x1);
		sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);

		return 2;
	}

	if (ideal2 == hdst->one_half_adc_gnd)
		return -1;

	/* far below the threshold: nothing but a 3 pole plug can look like this */
	if (ideal < hdst->threshold_3pole) {
		sprd_codec_regmap_update_bits(0xd8, 0x7, 0x3);
		return 1;
	}

	sprd_codec_regmap_update_bits(0xd4, 0x2000,
				      hdst->extcon_notifier_registered ? 0 : 0x2000);

	sts0 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	insert_status = sts0 & 0x400;
	CODEC_DBG("%s val %d\n", __func__, insert_status >> 10);

	if (!insert_status) {
		ret = sprd_detect_type_through_mdet(hdst);

		if (ideal2 >= hdst->sprd_half_adc_gnd || ret)
			sprd_codec_regmap_update_bits(0xd8, 0x7, 0x3);
		else
			sprd_codec_regmap_update_bits(0xd8, 0x7, 0x1);

		return ret;
	}

	if (ideal2 >= hdst->sprd_half_adc_gnd)
		sprd_codec_regmap_update_bits(0xd8, 0x7, 0x3);
	else
		sprd_codec_regmap_update_bits(0xd8, 0x7, 0x1);

	return 0;
}

/*
 * The mic detect block of the codec watches the microphone bias current: as
 * soon as a real microphone is connected it raises its own EIC, which is what
 * tells a 4 pole headset with a mic from a 3 pole headphone.
 *
 * The source is armed here, a 500 jiffies timeout is waited for and the
 * answer is 0 for a 4 pole headset, 1 for a plug without any mic.
 */
static int sprd_detect_type_through_mdet(struct sprd_headset *hdst)
{
	const char *type_str;
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;
	int type;

	pr_info("[ASoC:HDST9620] %s enter\n", __func__);

	/*
	 * The register dump only feeds the debug print, so it stays visible at
	 * the top of the function even though gcc moves the reads into the
	 * jump label cold path.
	 */
	sts0 = 0;
	iev = 0;
	ie = 0;
	ris = 0;
	mis = 0;
	sts1 = 0;
	intc_en = 0;
	int34 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	CODEC_DBG("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		  __func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);

	sprd_headset_eic_enable(0, "HDST_INSERT_ALL_EIC", 0x400);
	sprd_headset_eic_clear(0x400, "HDST_INSERT_ALL_EIC");

	reinit_completion(&hdst->mdet_completion);

	sprd_headset_eic_enable(1, "HDST_MDET_EIC", 0x800);
	sprd_headset_eic_trig(0xb);
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);
	sprd_codec_regmap_update_bits(0xd8, 0x8, 0);

	if (wait_for_completion_timeout(&hdst->mdet_completion, 500)) {
		type = 0;
		type_str = "HEADSET_4POLE_NORMAL";
	} else {
		/* nobody pulled the mdet line: no microphone on the plug */
		sprd_headset_eic_enable(0, "HDST_MDET_EIC", 0x800);

		sts0 = 0;
		iev = 0;
		ie = 0;
		ris = 0;
		mis = 0;
		sts1 = 0;
		intc_en = 0;
		int34 = 0;
		sprd_codec_regmap_read(0x184, &sts0);
		sprd_codec_regmap_read(0x214, &iev);
		sprd_codec_regmap_read(0x218, &ie);
		sprd_codec_regmap_read(0x21c, &ris);
		sprd_codec_regmap_read(0x220, &mis);
		sprd_codec_regmap_read(0x22c, &sts1);
		sprd_codec_regmap_read(0x280, &intc_en);
		sprd_codec_regmap_read(0x288, &int34);
		pr_err("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		       __func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);

		type = 1;
		type_str = "HEADSET_NO_MIC";
	}

	hdst->button_plugged = 1;

	sprd_headset_eic_enable(1, "HDST_INSERT_ALL_EIC", 0x400);
	sprd_headset_eic_enable(0, "HDST_MDET_EIC", 0x800);
	sprd_headset_eic_clear(0x800, "HDST_MDET_EIC");

	pr_info("[ASoC:HDST9620] %s, headset_type %d (%s)\n", __func__, type,
		type_str);

	return type;
}

int sprd_process_4pole_type(struct sprd_headset *hdst)
{
	sprd_codec_regmap_update_bits(0xd8, 8, 0);

	if (!hdst->insert_started) {
		hdst->state = 3;
		sprd_headset_jack_report(hdst, &hdst->jack, 3, 3);
		hdst->plug_state = 1;
	}

	if (hdst->state == 1) {
		pr_info("%s report for 4p re_detect\n", "sprd_process_4pole_type");
		hdst->state = 3;
		sprd_headset_jack_report(hdst, &hdst->jack, 0, 3);
		sprd_headset_jack_report(hdst, &hdst->jack, hdst->state, 3);
		hdst->plug_state = 1;
	}

	hdst->insert_started = 1;
	hdst->detect_mode = 3;
	hdst->detect_deadline = jiffies + 250;

	mutex_lock(&hdst->audio_on_lock);
	if (!hdst->audio_on)
		sprd_enable_hmicbias_polling(1, 0);
	mutex_unlock(&hdst->audio_on_lock);

	sprd_codec_regmap_update_bits(0xd0, 0x700, (hdst->mic_gain & 0x7) << 8);
	sprd_codec_regmap_update_bits(0xd0, 0x2000, 0x2000);

	sprd_set_eic_trig_level("HDST_BDET_EIC", 0x8000, 1);
	sprd_headset_eic_enable(1, "HDST_BDET_EIC", 0x8000);
	sprd_headset_eic_trig(0xf);

	return 0;
}

/*
 * Average 20 ADC samples taken on the head mic line while the type-c switch
 * is pointed at the microphone, then convert the average into millivolt.
 */
int sprd_headset_get_mic_voltage(struct sprd_headset *hdst)
{
	struct iio_channel *chan = hdst->headmic_chan;
	int i, ret, v1, v2, sum = 0, adc, ideal, mv;

	sprd_codec_regmap_update_bits(0xdc, 0x40, 0x40);
	sprd_codec_regmap_update_bits(0xdc, 0x20, 0x20);
	sprd_codec_regmap_update_bits(0xdc, 0xf, 0);

	for (i = 0; i < 20; i++) {
		sprd_codec_regmap_update_bits(0xdc, 0x10, 0);
		ret = iio_read_channel_raw(chan, &v1);
		if (ret < 0) {
			pr_err("%s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
			return -EINVAL;
		}
		if (v1 < 0)
			return -EINVAL;

		sprd_codec_regmap_update_bits(0xdc, 0x10, 0x10);
		ret = iio_read_channel_raw(chan, &v2);
		if (ret < 0) {
			pr_err("%s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
			return -EINVAL;
		}
		if (v1 + v2 < 0)
			return -EINVAL;

		if (!hdst->typec_attached) {
			pr_err("typec_mic_voltage headset plugout! i %d, adc_value %d\n",
			       i, (v1 + v2) >> 1);
			return -EINVAL;
		}

		sum += (v1 + v2) >> 1;
	}

	if (sum < -19)
		return -EINVAL;

	adc = sum / 20;

	hdst->adc_cal_flag = 1;
	ideal = sprd_adc_to_ideal(adc, hdst->adc_scale);
	hdst->adc_cal_flag = 0;
	if (ideal < 0)
		return -EINVAL;

	mv = ideal * 3750 / 10000;

	pr_info("typec_mic_voltage adc_mic_average %d, adc_ideal=%d, V_ideal %s %d mV\n",
		adc, ideal, sum > 0x13feb ? "outrange!" : "", mv);

	return mv;
}

/*
 * Route the type-c path to the head mic: the head mic bias has to be off
 * while the analog switch bounces, the 32k clock stays alive across it.
 */
int sprd_headset_typec_mic_switch(struct sprd_headset *hdst)
{
	struct sprd_regulator *regu = hdst->regu;

	sprd_headset_power_set(sprd_headset_search_power(regu, "HEADMICBIAS"), 0);
	msleep(20);

	sprd_codec_regmap_update_bits(0xd4, 0x2000, 0x2000);
	usleep_range(1000, 1100);

	gpiod_set_value_cansleep(hdst->mic_gpio,
				 gpiod_get_value(hdst->mic_gpio) == 0);
	usleep_range(1000, 1100);

	sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);

	sprd_headset_power_set(sprd_headset_search_power(regu, "DIG_CLK_HID"), 1);

	sprd_codec_regmap_update_bits(0x4, 0x1000, 0);
	sprd_codec_regmap_update_bits(0x4, 0x2000, 0x2000);
	sprd_codec_regmap_update_bits(0x4, 0x8, 0x8);
	usleep_range(15000, 15100);
	sprd_codec_regmap_update_bits(0x4, 0x1000, 0x1000);

	sprd_headset_power_set(sprd_headset_search_power(regu, "HEADMICBIAS"), 1);
	sprd_headset_power_set(sprd_headset_search_power(regu, "DIG_CLK_HID"), 0);

	return 0;
}

/*
 * Dump the headset interrupt state; used by the plug-out / next-plugin paths.
 */
static void sprd_headset_dump_eic(const char *func)
{
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;

	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);

	pr_err("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
	       func, sts0, iev, ie, ris, mis, sts1, intc_en, int34);
}

/*
 * Tear the analog side down before the jack leaves, then re-arm the
 * insert-all detection for the next plug-in.
 */
void sprd_headset_prepare_plugout(struct sprd_headset *hdst)
{
	if (hdst->hw_type == 1) {
		sprd_codec_regmap_update_bits(0xdc, 0xf, 4);
		sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);
		sprd_codec_regmap_update_bits(0xd4, 0x1000, 0);
		sprd_codec_regmap_update_bits(0xd0, 0x4000, 0x4000);
		usleep_range(50, 60);
		sprd_codec_regmap_update_bits(0xd4, 0x2000,
					      hdst->extcon_notifier_registered ?
					      0 : 0x2000);
	}

	/*
	 * The vendor binary calls an FM mute hook through the CFI stub that
	 * lives at .text+0x0; only the "no fm mute" DT switch guards it.
	 */
	if (hdst->no_fm_mute)
		dsp_fm_mute_by_set_dg(0);

	sprd_headset_prepare_insert_all_plugout(hdst);

	CODEC_DBG("%s\n", "sprd_headset_prepare_plugout");
	sprd_headset_dump_eic("sprd_headset_prepare_plugout");
}

/*
 * Arm the detection for the next plug-in event.  @hw_type decides between the
 * LDETL path (4-pole, waits for the completion) and the plain insert-all path.
 */
void sprd_headset_prepare_next_plugin(struct sprd_headset *hdst)
{
	if (hdst->extcon_notifier_registered) {
		gpiod_set_value_cansleep(hdst->mic_gpio, 0);
		return;
	}

	switch (hdst->hw_type) {
	case 0:
		reinit_completion(&hdst->detect_completion);
		sprd_set_eic_trig_level("HDST_LDETL_EIC", 0x1000, 0);
		sprd_headset_eic_enable(1, "HDST_LDETL_EIC", 0x1000);
		sprd_headset_eic_trig(0xc);
		if (!wait_for_completion_timeout(&hdst->detect_completion,
						 msecs_to_jiffies(750))) {
			pr_err("failed to wait ldetl plug out\n");
			sprd_headset_dump_eic("sprd_headset_prepare_next_plugin");
			sprd_headset_reset(hdst);
		}
		hdst->detect_mode = 0;
		break;
	case 1:
		sprd_headset_eic_enable(1, "HDST_INSERT_ALL_EIC", 0x400);
		sprd_headset_eic_trig(0xa);
		break;
	default:
		break;
	}
}

/*
 * The jack left the plug: drop every headset state, silence the analog side
 * and re-arm the interrupt controller for the next insertion.
 */
void sprd_headset_insert_all_plugout(struct sprd_headset *hdst)
{
	struct sprd_headset *hd = sprd_hdst;

	sprd_headset_disable_eic();

	if (hd->button_status == 1) {
		sprd_headset_jack_report(hd, &hd->button_jack, 0,
					 hd->button_eic_mask);
		pr_info("%s headset button released by force! current button: %#x\n",
			"sprd_headset_button_release_verify",
			hd->button_eic_mask);
		hd->button_status = 0;
		hd->button_eic_mask &= ~0x7c00;
		sprd_codec_regmap_update_bits(0x214, 0x8000, 0x8000);
	}

	hdst->in_ldetl = 0;
	hdst->state &= ~3;
	sprd_headset_jack_report(hdst, &hdst->jack, 0, 3);
	sprd_enable_hmicbias_polling(0, 1);
	sprd_headset_power_set(sprd_headset_search_power(hdst->regu,
							 "HEADMICBIAS"), 0);

	if (hdst->headset_type == 0)
		pr_info("headset plug out\n");
	else if (hdst->headset_type == 1)
		pr_info("headphone plug out\n");
	else
		pr_err("headphone type error\n");

	hdst->plug_state = 0;
	hdst->insert_started = 0;
	hdst->headset_type = -1;
	hdst->button_pressed = 0;
	hdst->mic_bias_state = 0;
	hdst->button_status = 0;
	hdst->button_plugged = 0;

	if (hdst->no_fm_mute)
		dsp_fm_mute_by_set_dg(1);

	sprd_codec_regmap_update_bits(0x224, 0x400, 0x400);
	sprd_codec_regmap_update_bits(0x214, 0xffff, 0xffff);
	pr_info("%s set all internal eic trig level to %s\n",
		"sprd_set_all_eic_trig_level", "high");
	sprd_codec_regmap_update_bits(0x218, 0xffff, 0);
	pr_info("%s %s all internal eic\n", "sprd_headset_all_eic_enable",
		"disable");
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);
	sprd_codec_regmap_update_bits(0x224, 0xffff, 0xfc00);
	pr_info("%s clear all internal eic\n", "sprd_headset_clear_all_eic");
}

/*
 * Average the button ADC while a key is held down, then convert the average
 * into an ideal millivolt-equivalent value.  Returns the ideal value when the
 * conversion succeeded, otherwise the raw average (or 0xfff on read errors).
 */
int sprd_button_ideal_adc(struct sprd_headset *hdst)
{
	struct iio_channel *chan = hdst->headmic_chan;
	int i, ret, v1, v2, v, sum = 0, count = 0;
	int adc = 0xfff, ideal, mv;

	sprd_codec_regmap_update_bits(0xdc, 0x40, 0x40);
	sprd_codec_regmap_update_bits(0xdc, 0x20, 0);
	sprd_codec_regmap_update_bits(0xdc, 0xf, 0);

	for (i = 0; i < 20; i++) {
		sprd_codec_regmap_update_bits(0xdc, 0x10, 0);
		ret = iio_read_channel_raw(chan, &v1);
		if (ret < 0) {
			pr_err("%s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
			goto out;
		}
		if (v1 < 0)
			goto out;

		sprd_codec_regmap_update_bits(0xdc, 0x10, 0x10);
		ret = iio_read_channel_raw(chan, &v2);
		if (ret < 0) {
			pr_err("%s: read adc raw value failed!\n",
			       "sprd_headset_adc_get");
			goto out;
		}
		if (v1 + v2 < 0)
			goto out;

		sprd_codec_regmap_read(0x200, &v);
		if (!(v & 0x8000)) {
			pr_err("%s key released! i %d, adc_value %d, insert_state %d\n",
			       "sprd_button_ideal_adc", count, (v1 + v2) >> 1,
			       v);
			if (!count)
				goto out;
			break;
		}

		count++;
		sum += (v1 + v2) >> 1;
	}

	if (!count)
		goto out;

	adc = sum / count;
	if (adc < 0) {
		pr_err("%s adc error, adc_mic_average %d\n",
		       "sprd_button_ideal_adc", adc);
		adc = 0xfff;
	}

out:
	ideal = sprd_adc_to_ideal(adc, hdst->adc_scale);
	mv = ideal * 1250 / 10000;

	pr_info("%s adc_mic_average %d, adc_ideal=%d, V_ideal %s %d mV\n",
		"sprd_button_ideal_adc", adc, ideal,
		adc > 0xffe ? "outrange!" : "", mv);

	return ideal >= 0 ? ideal : adc;
}

/*
 * One entry of the headset button table: the ADC window that selects the
 * button, the vendor keeps a third cell per row that the detect path never
 * reads.
 */

/* key bit reported through button_jack, one per table row */
static const unsigned int headset_button_key_bits[] = {
	0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200,
};

/* map one ADC sample to the key bit of the button table row it falls into */
static unsigned int sprd_adc_to_button(int adc)
{
	struct sprd_headset *hdst = sprd_hdst;
	struct sprd_headset_button *btn;
	unsigned int i, key = 0;

	if (!hdst || !hdst->btns) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdst(%p) or hdst_btns(%p) is NULL!\n",
		       __func__, hdst, hdst ? hdst->btns : NULL);
		return 0;
	}

	for (i = 0; i < hdst->btn_count; i++) {
		btn = &hdst->btns[i];

		if (adc < btn->low || adc >= btn->high)
			continue;

		key = headset_button_key_bits[i];
		break;
	}

	pr_info("[ASoC:HDST9620] %s key %d\n", __func__, i);

	return key;
}

/*
 * Used when the button work finds out that the plug is already gone: report
 * the release of the button it may still owe the input layer.
 */
static void sprd_headset_button_release_verify(void)
{
	struct sprd_headset *hdst = sprd_hdst;

	if (hdst->button_pressed == 1) {
		sprd_headset_jack_report(hdst, &hdst->button_jack, 0,
					 hdst->button_eic_mask);
		hdst->button_pressed = 0;
		pr_info("[ASoC:HDST9620] %s headset button released by force! current button: %#x\n",
			__func__, hdst->button_eic_mask);
		hdst->button_eic_mask &= ~0x7c00;
		sprd_set_eic_trig_level("HDST_BDET_EIC", 0x8000, 1);
	}
}

/*
 * button EIC work.  0x214 bit 15 selects which slope of the bdet line the
 * block listens to: armed high the EIC fires when the line goes low (button
 * pressed), armed low it fires on the release.
 */
static void headset_button_work_func(struct work_struct *work)
{
	struct sprd_headset *hdst = sprd_hdst;
	void *pdata = hdst ? (void *)&hdst->pdata : NULL;
	unsigned int iev, dcl1, clk0, hid0;
	int adc;

	if (!hdst || !pdata) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdst(%p) or pdata(%p) is NULL!\n",
		       __func__, hdst, pdata);
		return;
	}

	if (!hdst->plug_state) {
		pr_info("[ASoC:HDST9620] button work, no headset insert!\n");

		sprd_headset_button_release_verify();
		hdst->in_ldetl = 0;

		return;
	}

	if (hdst->detect_deadline &&
	    time_before(jiffies, hdst->detect_deadline)) {
		pr_info("[ASoC:HDST9620] discard one button event for lacking of time\n");
		sprd_headset_button_eic_reenable();
		return;
	}

	down(&hdst->irq_det_ldetl_lock);

	iev = 0;
	sprd_codec_regmap_read(0x214, &iev);
	dcl1 = 0;
	sprd_codec_regmap_read(0x100, &dcl1);
	clk0 = 0;
	sprd_codec_regmap_read(0x68, &clk0);
	hid0 = 0;
	sprd_codec_regmap_read(0x144, &hid0);
	pr_info("[ASoC:HDST9620] %s polling: DCL1(0100) %x, CLK0(0068) %x, HID0(0144) %x\n",
		__func__, dcl1, clk0, hid0);

	if (iev & 0x8000) {
		/* the line went low: a button was pressed, look at the ADC */
		sprd_set_eic_trig_level("HDST_BDET_EIC", 0x8000, 0);
		sprd_enable_hmicbias_polling(0, 0);

		mutex_lock(&hdst->btn_detecting_lock);
		hdst->in_ldetl = 1;
		mutex_unlock(&hdst->btn_detecting_lock);

		if (hdst->btn_count) {
			adc = sprd_button_ideal_adc(hdst);
			hdst->button_eic_mask |= sprd_adc_to_button(adc);
		}

		if (!hdst->button_pressed) {
			sprd_headset_jack_report(hdst, &hdst->button_jack,
						 hdst->button_eic_mask,
						 hdst->button_eic_mask);
			hdst->button_pressed = 1;
			pr_info("[ASoC:HDST9620] Reporting headset button press. button: 0x%x\n",
				hdst->button_eic_mask);
		} else {
			pr_err("[ASoC:HDST9620] Headset button has been reported already. button: 0x%x\n",
			       hdst->button_eic_mask);
		}
	} else {
		/* the line went high again, arm the press slope back */
		sprd_set_eic_trig_level("HDST_BDET_EIC", 0x8000, 1);

		mutex_lock(&hdst->btn_detecting_lock);
		hdst->in_ldetl = 0;
		mutex_unlock(&hdst->btn_detecting_lock);

		mutex_lock(&hdst->audio_on_lock);
		if (!hdst->audio_on)
			sprd_enable_hmicbias_polling(1, 0);
		mutex_unlock(&hdst->audio_on_lock);

		if (hdst->button_pressed == 1) {
			sprd_headset_jack_report(hdst, &hdst->button_jack, 0,
						 hdst->button_eic_mask);
			hdst->button_pressed = 0;
			pr_info("[ASoC:HDST9620] Reporting headset button release. button: %#x\n",
				hdst->button_eic_mask);
		} else {
			pr_err("[ASoC:HDST9620] Headset button has been released already. button: %#x\n",
			       hdst->button_eic_mask);
		}
	}

	hdst->button_eic_mask &= ~0x7c00;
	sprd_headset_button_eic_reenable();

	up(&hdst->irq_det_ldetl_lock);
}

/*
 * The detect all work runs when the insert-all EIC fired (or when the typec
 * switch changed): verify the level, program the hw status, then either report
 * a type, restart the state machine or hand over to the plugout path.
 */
static void headset_detect_all_work_func(struct work_struct *work)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;
	unsigned int val, lr_val, mic_val;
	int insert_all_status = 0, insert_status = 0, ret;

	if (!hdst) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdset is NULL!\n", __func__);
		return;
	}

	pr_info("[ASoC:HDST9620] %s enter\n", __func__);

	down(&hdst->irq_det_ldetl_lock);

	if (!hdst->extcon_notifier_registered) {
		val = 0;
		sprd_codec_regmap_read(0x200, &val);
		(void)val;

		/* the level has to hold longer once a plug was reported */
		ret = sprd_headset_valid_insert_all(hdst->plug_state ? 20 : 40);
		if (ret) {
			pr_err("[ASoC:HDST9620] %s insert all invalid %d\n",
			       __func__, ret);
			goto reset_hw;
		}
	} else {
		lr_val = gpiod_get_value(hdst->lr_gpio);
		mic_val = gpiod_get_value(hdst->mic_gpio);
		pr_info("[ASoC:HDST9620] typec_notifier typec_attached %d, lr_gpio %d, mic_gpio %d\n",
			hdst->typec_attached, lr_val, mic_val);

		gpiod_set_value_cansleep(hdst->lr_gpio, hdst->typec_attached);
		insert_all_status = hdst->typec_attached;
		insert_status = 0;
		lr_val = gpiod_get_value(hdst->lr_gpio);
		pr_info("[ASoC:HDST9620] detect_all_work_func, analog typec headset '%s'. Switch D+/D- to '%s'\n",
			insert_all_status ? "ATTACHED" : "DETACHED",
			lr_val == insert_all_status ? "HEADPHONE" : "USB");
	}

	sts0 = 0;
	iev = 0;
	ie = 0;
	ris = 0;
	mis = 0;
	sts1 = 0;
	intc_en = 0;
	int34 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	CODEC_DBG("%s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x\n",
		  __func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);

	pr_info("[ASoC:HDST9620] %s plug_state_last %d\n", __func__,
		hdst->plug_state);

	if (!hdst->extcon_notifier_registered) {
		iev = 0;
		sprd_codec_regmap_read(0x214, &iev);
		sts0 = 0;
		sprd_codec_regmap_read(0x184, &sts0);
		pr_info("[ASoC:HDST9620] %s trig_level %s, insert_status %s\n",
			__func__, (iev & 0x400) ? "high" : "low",
			(sts0 & 0x8000) ? "high" : "low");

		if ((iev & 0x400) && (sts0 & 0x8000)) {
			insert_all_status = 1;
			insert_status = 1;
		} else if (!(iev & 0x400) && !(sts0 & 0x8000)) {
			insert_all_status = 0;
			insert_status = 0;
		} else {
			/* the trigger level and the status disagree */
			sprd_headset_reset(hdst);
			goto post;
		}
	}

	if (!hdst->plug_state) {
		sprd_headset_set_hw_status(hdst,
			(struct sprd_headset_hw_status *)&hdst->pdata);
		usleep_range(10000, 10100);
	}

	if (!hdst->insert_all_active)
		insert_status = 0;
	else if (hdst->extcon_notifier_registered)
		insert_status = hdst->typec_attached ? 1 : 0;

	sprd_enable_hmicbias_polling(0, 1);

	pr_info("[ASoC:HDST9620] %s plug_state_last %d, hdst_hw_status %s\n",
		__func__, hdst->plug_state, eic_hw_state[hdst->detect_mode]);

	if (insert_all_status == 1) {
		if (!hdst->plug_state) {
			sprd_headset_type_report(hdst);
		} else if (hdst->insert_all_active && insert_status) {
			sprd_headset_type_report(hdst);
		} else {
			goto reset_hw;
		}
	} else if (hdst->insert_all_active && insert_status) {
		sprd_headset_type_report(hdst);
	} else if (hdst->plug_state == 1) {
		/* a 4 pole plug was reported, hand over to the plugout path */
		sprd_headset_insert_all_plugout(hdst);
		hdst->detect_mode = 1;
		sprd_codec_regmap_update_bits(0xd8, 7, 3);
		sprd_codec_regmap_update_bits(0xd8, 8, 0);
		sprd_headset_reset_hw_status(hdst);
		sprd_headset_prepare_next_plugin(hdst);
		hdst->detect_mode = 0;
		hdst->detect_deadline = 0;
	} else {
		goto reset_hw;
	}

	goto post;

reset_hw:
	sprd_headset_reset(hdst);
	hdst->detect_mode = 0;

post:
	if (!hdst->insert_all_active) {
		sprd_codec_regmap_update_bits(0xd4, 0x2000,
			hdst->extcon_notifier_registered ? 0 : 0x2000);
		sprd_codec_regmap_update_bits(0xd8, 8, 0);
		sprd_headset_reset_hw_status(hdst);

		if (hdst->mic_bias_state >= 10) {
			sprd_headset_reset(hdst);
			hdst->detect_mode = 0;
		}

		hdst->detect_again = 0;
	}

	sts0 = 0;
	iev = 0;
	ie = 0;
	ris = 0;
	mis = 0;
	sts1 = 0;
	intc_en = 0;
	int34 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	sprd_codec_regmap_read(0x214, &iev);
	sprd_codec_regmap_read(0x218, &ie);
	sprd_codec_regmap_read(0x21c, &ris);
	sprd_codec_regmap_read(0x220, &mis);
	sprd_codec_regmap_read(0x22c, &sts1);
	sprd_codec_regmap_read(0x280, &intc_en);
	sprd_codec_regmap_read(0x288, &int34);
	pr_info("[ASoC:HDST9620] %s STS0(184) %x, INT5(214.IEV) %x, INT6(218.IE) %x, INT7(21C.RIS) %x, INT8(220.MIS) %x, INT11(22C.STS1) %x, INT32(280.INTC.EN) %x, INT34(288) %x",
		__func__, sts0, iev, ie, ris, mis, sts1, intc_en, int34);
	pr_info("[ASoC:HDST9620] %s type_detecting %d out\n", __func__,
		hdst->detect_again);

	up(&hdst->irq_det_ldetl_lock);
}

/*
 * The ldetl (left detect low) source only reports that the level of the left
 * channel changed, so the worker samples the level twice and gives the analog
 * block 20 ms in between.
 *
 * The top level EIC handler stores the pending mask in hdst->int_shadow and
 * queues this work, the semaphore serialises it against the button path.
 */
static void headset_ldetl_work_func(struct work_struct *work)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int int_shadow, mis, val, sts0, iev;
	int trig_last, insert_status;

	if (!hdst) {
		pr_err("[ASoC:HDST9620] %s: sprd_hdset is NULL!\n", __func__);
		return;
	}

	down(&hdst->irq_det_ldetl_lock);
	pr_info("[ASoC:HDST9620] %s enter\n", __func__);

	int_shadow = hdst->int_shadow;
	if (int_shadow) {
		hdst->int_shadow = 0;
		sprd_codec_intc_irq(hdst->comp2, int_shadow);
		sprd_codec_regmap_update_bits_force(0x284, int_shadow, 0);
	}

	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	if (!(mis & 0x1000)) {
		/* the latched line is already gone, start the block over */
		sprd_headset_reset(hdst);
		goto out;
	}

	/* the level has to survive the analog debounce time */
	val = 0;
	sprd_codec_regmap_read(0x200, &val);
	trig_last = (val >> 12) & 1;
	msleep(20);
	val = 0;
	sprd_codec_regmap_read(0x200, &val);
	if (trig_last != ((val >> 12) & 1)) {
		pr_err("[ASoC:HDST9620] %s check debounce failed\n", __func__);
		sprd_headset_eic_clear(0x1000, "HDST_LDETL_EIC");
		sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0x4000);
		mis = 0;
		sprd_codec_regmap_read(0x220, &mis);
		pr_info("[ASoC:HDST9620] intc force clear, INT8(220.MIS) %x, bits %x\n",
			mis, 0x4000);
		sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);
		sprd_headset_eic_trig(0xc);
		goto out;
	}

	iev = 0;
	sprd_codec_regmap_read(0x214, &iev);
	hdst->ldetl_trig_val_last = (iev >> 12) & 1;

	sts0 = 0;
	sprd_codec_regmap_read(0x184, &sts0);
	insert_status = sts0 & 0x800;
	pr_info("[ASoC:HDST9620] %s ldetl_trig_val_last %s, insert_status %s\n",
		__func__, hdst->ldetl_trig_val_last ? "high" : "low",
		insert_status ? "high" : "low");

	if (hdst->ldetl_trig_val_last && insert_status) {
		hdst->ldetl_plug_in = 1;
		pr_info("[ASoC:HDST9620] %s ldetl trig level is high, plugin?\n",
			__func__);
	} else if (!(hdst->ldetl_trig_val_last || insert_status)) {
		hdst->ldetl_plug_in = 0;
		pr_info("[ASoC:HDST9620] %s ldetl trig level is low, plugout?\n",
			__func__);
	} else {
		/* level and insert status disagree, detect from scratch */
		sprd_headset_reset(hdst);
		goto out;
	}

	/* stop the ldetl source, the analog block has to settle down */
	sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);
	sprd_headset_eic_enable(0, "HDST_LDETL_EIC", 0x1000);
	sprd_set_eic_trig_level("HDST_LDETL_EIC", 0x1000, 0);
	sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0x4000);
	mis = 0;
	sprd_codec_regmap_read(0x220, &mis);
	pr_info("[ASoC:HDST9620] intc force clear, INT8(220.MIS) %x, bits %x\n",
		mis, 0x4000);
	pr_info("[ASoC:HDST9620] %s %d ldetl_trig_val_last %d, plug_state_last %d, ldetl_plug_in %d\n",
		__func__, __LINE__, hdst->ldetl_trig_val_last,
		hdst->plug_state, hdst->ldetl_plug_in);

	if ((hdst->ldetl_trig_val_last || hdst->plug_state) &&
	    hdst->ldetl_plug_in) {
		if (hdst->ldetl_trig_val_last == 1 ||
		    hdst->ldetl_plug_in == 1) {
			/* switch from the left detect to the insert all source */
			sprd_codec_regmap_update_bits(0xd4, 0x2000, 0);
			sprd_headset_eic_enable(0, "HDST_LDETL_EIC", 0x1000);
			sprd_headset_eic_clear(0x1000, "HDST_LDETL_EIC");
			sprd_codec_regmap_update_bits_force(0x284, 0x4000,
							   0x4000);
			mis = 0;
			sprd_codec_regmap_read(0x220, &mis);
			pr_info("[ASoC:HDST9620] intc force clear, INT8(220.MIS) %x, bits %x\n",
				mis, 0x4000);
			sprd_set_eic_trig_level("HDST_LDETL_EIC", 0x1000, 0);
			usleep_range(3000, 3500);
			sprd_codec_regmap_update_bits_force(0x284, 0x4000, 0);

			reinit_completion(&hdst->insert_all_completion);
			sprd_set_eic_trig_level("HDST_INSERT_ALL_EIC", 0x400, 1);
			sprd_headset_eic_enable(1, "HDST_INSERT_ALL_EIC",
						0x400);
			sprd_headset_eic_trig(0xa);

			if (!wait_for_completion_timeout(
				    &hdst->insert_all_completion, 500)) {
				pr_err("[ASoC:HDST9620] insert_all plugin timeout\n");
				sprd_headset_reset(hdst);
				hdst->detect_mode = 0;
			} else {
				hdst->detect_mode = 2;
			}
		}
	} else {
		/* keep the left channel grounded while nothing is plugged */
		sprd_codec_regmap_update_bits(0xd4, 0x2000,
				hdst->extcon_notifier_registered ? 0 : 0x2000);
		sprd_headset_prepare_ldetl(hdst);
	}

out:
	pr_info("[ASoC:HDST9620] %s out\n", __func__);
	up(&hdst->irq_det_ldetl_lock);
}

/*
 * Debug helper: dump the analog headset registers.
 */
void sprd_dump_reg_work(struct work_struct *work)
{
	struct sprd_headset *hdst = sprd_hdst;
	unsigned int adc = 0, gpio, pmu0, hdt0, hdt1, hdt2, cdc2, cdc3, pmu1;
	unsigned int sts0, iev, ie, ris, mis, sts1, intc_en, int34;
	int ret;

	if (!hdst) {
		pr_err("%s: pdata is NULL!\n", "sprd_dump_reg_work");
		return;
	}

	ret = iio_read_channel_raw(hdst->headmic_chan, &adc);
	if (ret < 0) {
		pr_err("%s: read adc raw value failed!\n",
		       "sprd_headset_adc_get");
		adc = ret;
	}

	gpio = gpiod_get_raw_value(gpio_to_desc(hdst->aud_int_gpio));
	pr_info("adc_mic %d, gpio_detect_all %d\n", adc, gpio);

	sprd_codec_regmap_update_bits(0x2008, 0x10, 0x10);

	sprd_codec_regmap_read(0x000, &pmu0);
	sprd_codec_regmap_read(0x0d0, &hdt0);
	sprd_codec_regmap_read(0x0d4, &hdt1);
	sprd_codec_regmap_read(0x0d8, &hdt2);
	sprd_codec_regmap_read(0x0fc, &sts0);
	sprd_codec_regmap_read(0x07c, &cdc2);
	sprd_codec_regmap_read(0x080, &cdc3);
	sprd_codec_regmap_read(0x004, &pmu1);

	pr_info("ana_pmu0  | ana_hdt0 | ana_hdt1 | ana_hdt2 | ana_sts0 | ana_dcl0\n");
	pr_info("0x%08X|0x%08X|0x%08X|0x%08X|0x%08X|0x%08X\n",
		pmu0, hdt0, hdt1, hdt2, sts0, cdc2);
	pr_info("ana_cdc2 | ana_cdc3 | ana_pmu1\n");
	pr_info("0x%08X|0x%08X|0x%08X\n", cdc2, cdc3, pmu1);

	sprd_headset_dump_eic("sprd_dump_reg_work");

	(void)sts0;
	(void)iev;
	(void)ie;
	(void)ris;
	(void)mis;
	(void)sts1;
	(void)intc_en;
	(void)int34;
}

/*
 * Classify whatever was just plugged in.
 *
 * Without a type-c extcon the plain detection helper already returns the
 * headset type.  With type-c the microphone voltage has to be sampled a few
 * times: the first ladder decides whether a mic is present on the type-c
 * path, then the analog switch is flipped and the ladder is repeated.
 */
void sprd_headset_type_report(struct sprd_headset *hdst)
{
	struct sprd_headset *hd = sprd_hdst;
	int type = -1, ret, v0 = 0, v1 = 0, hpl = 0, thr;
	int i;

	hdst->button_plugged = 0;
	hdst->insert_all_active = 0;
	hdst->detect_again = 1;
	hdst->detect_deadline = 0;

	if (!hdst->extcon_notifier_registered) {
		ret = sprd_headset_type_plugged();
		pr_info("type_report headset_type %d\n", ret);

		if (ret == -1)
			goto retry;

		if (!ret) {
			type = 0;
			goto set_eu_low;
		}

		if (ret == 2) {
			if (hdst->eu_us_switch_gpio)
				gpiod_direction_output_raw(
					gpio_to_desc(hdst->eu_us_switch_gpio), 1);
		} else if (ret != 1) {
			pr_err("type_report error headset_type %d\n", ret);
			goto finish;
		}

		type = ret;
		goto det_3pole_cnt;
	}

	if (!hd) {
		pr_info("type_report headset_type %d\n", -1);
		goto retry;
	}

	if (gpiod_get_value(hd->mic_gpio))
		gpiod_set_value_cansleep(hd->mic_gpio, 0);

	for (i = 0; i < 10; i++) {
		v0 = sprd_headset_get_mic_voltage(hd);
		if (v0 < 0xa7e)
			break;
		msleep(100);
	}
	if (i == 10) {
		pr_err("read voltage fail, mic_voltage_0 %d, try_count %d",
		       v0, 10);
		goto det_3pole;
	}
	if (v0 < 0) {
		pr_err("read voltage fail, mic_voltage_0 %d, try_count %d",
		       v0, i);
		goto retry;
	}

	hpl = sprd_headset_typec_get_hpl_voltage(hd);
	sprd_headset_typec_mic_switch(hd);

	for (i = 0; i < 10; i++) {
		v1 = sprd_headset_get_mic_voltage(hd);
		if (v1 < 0xa7e)
			break;
		msleep(100);
	}
	if (i == 10) {
		pr_err("read voltage fail, mic_voltage_1 %d, try_count %d",
		       v1, 10);
		goto det_3pole;
	}
	if (v1 < 0) {
		pr_err("read voltage fail, mic_voltage_1 %d, try_count %d",
		       v1, i);
		goto retry;
	}

	ret = sprd_headset_typec_get_hpl_voltage(hd);
	if (ret < hpl)
		hpl = ret;

	thr = hd->threshold_3pole * 3750 / 10000;

	pr_info("typec mic_vol_0 %d, mic_vol_1 %d, vol_3pole %d, left_vol_min %d\n",
		v0, v1, thr, hpl);

	if (v0 >= thr && v1 >= thr) {
		if (v0 > v1)
			sprd_headset_typec_mic_switch(hd);

		sprd_codec_regmap_update_bits(0xd8, 7, hpl >= 0x1f ? 3 : 0);
		pr_info("type_report headset_type %d\n", 0);
		type = 0;
		goto set_eu_low;
	}

det_3pole:
	sprd_codec_regmap_update_bits(0xd8, 7, 3);
	pr_info("type_report headset_type %d\n", 1);
	type = 1;

det_3pole_cnt:
	if (hdst->button_status < 5 && !hdst->button_plugged &&
	    !hdst->extcon_notifier_registered) {
		pr_err("type_report det_3pole_cnt %d\n", hdst->button_status);
		hdst->button_status++;
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->detect_all_wq,
				      &hdst->detect_all_dwork, 250);
		hdst->insert_all_active = 1;
	}

eu_low:
	if (hdst->eu_us_switch_gpio)
		gpiod_direction_output_raw(gpio_to_desc(hdst->eu_us_switch_gpio), 0);

	goto finish;

set_eu_low:
	if (hdst->eu_us_switch_gpio)
		gpiod_direction_output_raw(gpio_to_desc(hdst->eu_us_switch_gpio), 0);
	type = 0;

finish:
	if (!hdst->insert_all_active)
		sprd_codec_regmap_update_bits(0x214, 0x400, 0);

	hdst->mic_bias_state = 0;

	if ((unsigned int)(type - 1) >= 2U) {
		if (!type) {
			hdst->headset_type = 0;
			sprd_process_4pole_type(hdst);
			pr_info("type_report headset plug in\n");
		} else {
			pr_err("type_report headphone type error\n");
			hdst->headset_type = -1;
		}
	} else {
		hdst->headset_type = 1;

		if (!hdst->insert_all_active) {
			sprd_headset_power_set(sprd_headset_search_power(
						       hdst->regu, "HEADMICBIAS"),
					       0);
			sprd_codec_regmap_update_bits(0xd0, 0x700, 0x700);
			sprd_codec_regmap_update_bits(0xd0, 0x2000, 0);
			sprd_codec_regmap_update_bits(0x218, 0x8000, 0);
		}

		hdst->state = 1;

		if (!hdst->insert_started) {
			sprd_headset_jack_report(hdst, &hdst->jack, 1, 1);
			hdst->detect_mode = 3;
			hdst->plug_state = 1;
		}

		hdst->insert_started = 1;
		pr_info("type_report headphone plug in\n");
	}

	if (!hdst->insert_all_active)
		sprd_headset_prepare_plugout(hdst);

	return;

retry:
	hdst->mic_bias_state++;
	hdst->headset_type = -1;

	if (hdst->mic_bias_state <= 9) {
		queue_delayed_work_on(WORK_CPU_UNBOUND, hdst->detect_all_wq,
				      &hdst->detect_all_dwork, 250);
		hdst->insert_all_active = 1;
	}
}

/*
 * Switch on the two clocks the detect block needs and dump the PMU/DCL/CLK
 * registers once they run.
 */
static int sprd_headset_regulator_init(struct sprd_headset *hdst)
{
	struct sprd_regulator *regu;
	unsigned int pmu0 = 0, pmu1 = 0, dcl1 = 0, clk0 = 0;

	regu = sprd_headset_search_power(hdst->regu, "CLK_DCL_32K");
	if (sprd_headset_power_set(regu, 1)) {
		pr_err("[ASoC:HDST9620] %s regulator CLK_DCL_32K set failed\n",
		       __func__);
		return -EINVAL;
	}

	regu = sprd_headset_search_power(hdst->regu, "DIG_CLK_INTC");
	if (sprd_headset_power_set(regu, 1)) {
		pr_err("[ASoC:HDST9620] %s regulator DIG_CLK_INTC set failed\n",
		       __func__);
		return -EINVAL;
	}

	sprd_codec_regmap_read(0x00, &pmu0);
	sprd_codec_regmap_read(0x04, &pmu1);
	sprd_codec_regmap_read(0x100, &dcl1);
	sprd_codec_regmap_read(0x68, &clk0);

	pr_info("[ASoC:HDST9620] %s PMU0(0000) %x, PMU1(0004) %x, DCL1(0100) %x, CLK0(0068) %x\n",
		__func__, pmu0, pmu1, dcl1, clk0);

	return 0;
}

/*
 * Program the HID block (bias current and clock of the button polling), mark
 * the mic bias as polling and open the EIC gate.
 */
static void sprd_hmicbias_polling_init(struct sprd_headset *hdst)
{
	unsigned int hid0 = 0, hid1 = 0, hid2 = 0, hid3 = 0, hid4 = 0;

	sprd_codec_regmap_update_bits(0x144, 0x3000, 0x3000);
	sprd_codec_regmap_update_bits(0x144, 0xc00, 0x800);
	sprd_codec_regmap_update_bits(0x148, 0xff00, 0x100);
	sprd_codec_regmap_update_bits(0x148, 0xff, 0x1);
	sprd_codec_regmap_update_bits(0x14c, 0xff00, 0x8000);
	sprd_codec_regmap_update_bits(0x14c, 0xff, 0x80);
	sprd_codec_regmap_update_bits(0x150, 0xffff, 0x8);
	sprd_codec_regmap_update_bits(0x154, 0xffff, 0x20);

	hdst->hmicbias_on = 1;

	sprd_codec_regmap_read(0x144, &hid0);
	sprd_codec_regmap_read(0x148, &hid1);
	sprd_codec_regmap_read(0x14c, &hid2);
	sprd_codec_regmap_read(0x150, &hid3);
	sprd_codec_regmap_read(0x154, &hid4);

	pr_info("[ASoC:HDST9620] %s HID0(144) %x, HID1(148) %x, HID2(14C) %x, HID3(150) %x, HID4(154) %x",
		__func__, hid0, hid1, hid2, hid3, hid4);

	sprd_codec_regmap_update_bits(0x280, 0x4000, 0x4000);
	sprd_codec_regmap_update_bits(0xd4, 0x2000,
				      hdst->extcon_notifier_registered ? 0 : 0x2000);
}

/* everything the detect block needs before the first EIC is armed */
static int sprd_detect_reg_init(struct sprd_headset *hdst)
{
	if (sprd_headset_regulator_init(hdst)) {
		pr_err("[ASoC:HDST9620] %s regulator init failed\n", __func__);
		return -EINVAL;
	}

	sprd_hmicbias_polling_init(hdst);
	sprd_headset_eic_init();

	return 0;
}

/*
 * Read one efuse cell into @val.  Returns 0 on success, or the error the
 * nvmem helpers reported.
 */
static int sprd_headset_read_efuse(struct device *dev, const char *name,
				   unsigned int *val)
{
	struct nvmem_cell *cell;
	size_t len = 0;
	void *buf;

	*val = 0;

	cell = nvmem_cell_get(dev, name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(val, buf, len < sizeof(*val) ? len : sizeof(*val));
	kfree(buf);

	return 0;
}

/*
 * The ADC ladder of every board is trimmed in efuse: A and B are the two
 * calibration points of the ladder, E1/E2 the ends of the head mic range.
 * They are read once and kept for sprd_adc_to_ideal().
 */
static int sprd_get_adc_cal_from_efuse(struct sprd_headset *hdst)
{
	unsigned int calib1 = 0, calib2 = 0, calib3 = 0;
	unsigned int a, b, e1, e2, c, d, chip_id;
	int ret;

	pr_info("[ASoC:HDST9620] %s enter\n", __func__);

	if (adc_cal_headset_valid) {
		pr_info("[ASoC:HDST9620] %s efuse A,B,E has been calculated already!\n",
			__func__);
		return 0;
	}

	ret = sprd_headset_read_efuse(&hdst->pdev->dev, "hp_adc_fir_calib_1",
				      &calib1);
	if (ret)
		goto out;
	ret = sprd_headset_read_efuse(&hdst->pdev->dev, "hp_adc_fir_calib_2",
				      &calib2);
	if (ret)
		goto out;
	ret = sprd_headset_read_efuse(&hdst->pdev->dev, "hp_adc_sec_calib",
				      &calib3);
	if (ret)
		goto out;

	pr_info("[ASoC:HDST9620] %s test[0] 0x%x %d, test[1] 0x%x %d, test[2] 0x%x %d\n",
		__func__, calib1, calib1, calib2, calib2, calib3, calib3);

	a = (calib1 >> 4) & 0xfff;
	b = (calib2 >> 4) & 0xfff;
	c = calib3 & 0xff;
	d = (calib3 >> 8) & 0xff;

	pr_info("[ASoC:HDST9620] %s d[0] %#x %d d[1] %#x %d d[2] %#x %d d[3] %#x %d\n",
		__func__, a, a, b, b, c, c, d, d);

	adc_cal_headset_valid = 1;

	chip_id = sci_get_ana_chip_id();
	pr_info("[ASoC:HDST9620] %s adie_chip_id 0x%x 0x%x\n", __func__,
		chip_id >> 16, chip_id & 0xffff);

	adc_cal_headset[0] = a;
	adc_cal_headset[1] = b;

	e1 = c * 2 + 0x960;
	adc_cal_headset[2] = e1;

	if (calib3 & 0x8000)
		e2 = ((0x3c - (d << 2)) & 0x3c) + 4 + 0x5dc;
	else
		e2 = calib1 * 4 + 0x5dc;
	adc_cal_headset[3] = e2;

	pr_info("[ASoC:HDST9620] %s A %d, B %d E1 %d E2 %d\n", __func__, a, b,
		e1, e2);

	return 0;

out:
	pr_err("[ASoC:HDST9620] %s, error: headset adc calibration fail %d\n",
	       __func__, ret);
	adc_cal_headset_valid = 0;

	return ret;
}

/* the two attributes of /sys/kernel/headset */
static struct device_attribute headset_debug_attr[] = {
	{
		.attr = { .name = "debug_level", .mode = 0644 },
		.show = sprd_headset_debug_level_show,
		.store = sprd_headset_debug_level_store,
	}, {
		.attr = { .name = "state", .mode = 0644 },
		.show = sprd_headset_state_show,
		.store = sprd_headset_state_store,
	},
};

static struct kobject *headset_debug_kobj;

static void sprd_headset_debug_sysfs_init(void)
{
	int i, ret;

	headset_debug_kobj = kobject_create_and_add("headset", kernel_kobj);
	if (!headset_debug_kobj) {
		pr_err("[ASoC:HDST9620] %s register sysfs failed. ret %d\n",
		       __func__, -ENOMEM);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(headset_debug_attr); i++) {
		ret = sysfs_create_file_ns(headset_debug_kobj,
					   &headset_debug_attr[i].attr, NULL);
		if (ret) {
			pr_err("[ASoC:HDST9620] %s create sysfs '%s' failed. ret %d\n",
			       __func__, headset_debug_attr[i].attr.name, ret);
			return;
		}
	}

	pr_info("[ASoC:HDST9620] %s success\n", __func__);
}

/*
 * Headset half of the codec probe: find the card node, parse the board
 * detection gpios, bring the detect hardware up and wire the EIC interrupt.
 * The codec probe treats -EPROBE_DEFER as "the card is not there yet".
 */
int sprd_headset_soc_probe(struct snd_soc_component *codec)
{
	struct sprd_asoc_card_priv *board;
	struct snd_soc_card *card;
	struct device_node *np, *hp_np;
	struct platform_device *hp_pdev;
	struct sprd_headset *hdst;
	unsigned int chip_id;
	int gpio_flag = 0;
	int i, ret;

	if (!codec) {
		pr_err("[ASoC:HDST9620] %s codec NULL\n", __func__);
		return -EINVAL;
	}

	if (!codec->dev) {
		pr_err("[ASoC:HDST9620] %s codec->dev NULL\n", __func__);
		return -EINVAL;
	}

	if (!codec->card) {
		pr_err("[ASoC:HDST9620] %s codec->component.card NULL\n",
		       __func__);
		return -EINVAL;
	}

	card = codec->card;
	board = card->drvdata;

	np = of_find_compatible_node(NULL, NULL, "unisoc,vbc-v4-codec-sc2730");
	if (!np) {
		pr_err("[ASoC:HDST9620] error, there must be a card0 node!\n");
		ret = -ENODEV;
		goto parse_fail;
	}

	/* the card node has to be turned into a platform device first */
	if (!of_find_device_by_node(np)) {
		pr_err("[ASoC:HDST9620] card0 node has no pdev?\n");
		ret = -EPROBE_DEFER;
		goto parse_fail;
	}

	board->hp_det_gpio =
		of_get_named_gpio_flags(np, "sprd-audio-card,hp-det-gpio", 0,
					&gpio_flag);
	board->hp_det_gpio_flags = gpio_flag & 1;
	if ((int)board->hp_det_gpio == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto parse_fail;
	}

	board->mic_det_gpio =
		of_get_named_gpio_flags(np, "sprd-audio-card,mic-det-gpio", 0,
					&gpio_flag);
	board->mic_det_gpio_flags = gpio_flag & 1;
	if ((int)board->mic_det_gpio == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto parse_fail;
	}

	/* boards without a jack detect gpio describe the headset node directly */
	if ((int)board->hp_det_gpio < 0) {
		hp_np = of_parse_phandle(np, "sprd-audio-card,headset", 0);
		if (!hp_np) {
			pr_err("[ASoC:HDST9620] parse 'sprd-audio-card,headset' failed!\n");
		} else {
			hp_pdev = of_find_device_by_node(hp_np);
			if (!hp_pdev) {
				pr_err("[ASoC:HDST9620] headset node has no pdev?\n");
				ret = -EPROBE_DEFER;
				goto parse_fail;
			}

			ret = sprd_headset_probe(hp_pdev);
			if (ret < 0) {
				if (ret != -EPROBE_DEFER)
					pr_err("[ASoC:HDST9620] sprd_headset_probe failed, ret %d\n",
					       ret);
				goto parse_fail;
			}

			if (ret)
				goto parse_fail;
		}
	}

	hdst = sprd_hdst;
	if (!hdst) {
		pr_err("[ASoC:HDST9620] %s hdst NULL\n", __func__);
		return -EINVAL;
	}

	if (!hdst->pdev) {
		pr_err("[ASoC:HDST9620] %s pdev %p, pdata %p\n", __func__,
		       hdst->pdev, (void *)&hdst->pdata);
		return -EINVAL;
	}

	if (!hdst->headmic_chan) {
		pr_err("[ASoC:HDST9620] %s adc_chan %p\n", __func__,
		       hdst->headmic_chan);
		return -EINVAL;
	}

	hdst->comp2 = codec;

	chip_id = sci_get_ana_chip_id();
	pr_info("[ASoC:HDST9620] %s adie_chip_id 0x%x 0x%x\n", __func__,
		chip_id >> 16, chip_id & 0xffff);

	ret = sprd_headset_power_init(hdst->regu, hdst->pdev);
	if (ret) {
		pr_err("[ASoC:HDST9620] %s power regulator init failed\n",
		       __func__);
		return ret;
	}

	if (sprd_detect_reg_init(hdst)) {
		pr_err("[ASoC:HDST9620] %s headset detect reg init failed\n",
		       __func__);
		return -EINVAL;
	}

	if (!hdst->hw_type)
		sprd_codec_regmap_update_bits(0xd0, 0x4000, 0);
	else if (hdst->hw_type == 1)
		sprd_codec_regmap_update_bits(0xd0, 0x4000, 0x4000);

	ret = snd_soc_card_jack_new(card, "Headset Jack", 3, &hdst->jack,
				    NULL, 0);
	if (ret) {
		pr_err("[ASoC:HDST9620] Failed to create headset jack\n");
		return ret;
	}

	ret = snd_soc_card_jack_new(card, "Headset Keyboard", 0x7c00,
				    &hdst->button_jack, NULL, 0);
	if (ret) {
		pr_err("[ASoC:HDST9620] Failed to create button jack\n");
		return ret;
	}

	if (hdst->btn_count > 6) {
		pr_warn("[ASoC:HDST9620] button number in dts is more than %d!\n",
			6);
		hdst->btn_count = 6;
	}

	for (i = 0; i < hdst->btn_count; i++) {
		ret = snd_jack_set_key(hdst->button_jack.jack,
				       headset_button_key_bits[i],
				       hdst->btns[i].code);
		if (ret) {
			pr_err("[ASoC:HDST9620] %s: Failed to set code for btn-%d\n",
			       __func__, i);
			return ret;
		}
	}

	/* the analog switch of the board, and the shared detect interrupt */
	if (hdst->pdata[0])
		gpiod_direction_output_raw(gpio_to_desc(hdst->pdata[0]), 0);

	ret = gpiod_direction_input(gpio_to_desc(hdst->aud_int_gpio));
	if (ret) {
		pr_err("[ASoC:HDST9620] gpio_direction_input failed for %d\n",
		       hdst->aud_int_gpio);
		return ret;
	}

	hdst->irq = gpiod_to_irq(gpio_to_desc(hdst->aud_int_gpio));
	CODEC_DBG("%s irq_detect_int_all %d, GPIO %d", __func__, hdst->irq,
		  hdst->aud_int_gpio);

	sema_init(&hdst->irq_det_ldetl_lock, 1);

	INIT_DELAYED_WORK(&hdst->mic_dwork, sprd_mdet_eic_work);
	hdst->mic_wq = create_singlethread_workqueue("headset_mic");
	if (!hdst->mic_wq) {
		pr_err("[ASoC:HDST9620] create_singlethread_workqueue for headset_mic failed!\n");
		ret = 0;
		goto out_mic;
	}

	INIT_DELAYED_WORK(&hdst->button_dwork, headset_button_work_func);
	hdst->button_wq = create_singlethread_workqueue("headset_button");
	if (!hdst->button_wq) {
		pr_err("[ASoC:HDST9620] create_singlethread_workqueue for headset_button failed!\n");
		ret = 0;
		goto out_button;
	}

	hdst->ldetl_trig_val_last = 0;
	hdst->button_pressed = 0;
	hdst->insert_all_active = 0;
	hdst->insert_started = 0;
	hdst->mic_bias_state = 0;
	hdst->detect_mode = 0;

	INIT_DELAYED_WORK(&hdst->detect_all_dwork, headset_detect_all_work_func);
	hdst->detect_all_wq = create_singlethread_workqueue("headset_detect_all");
	if (!hdst->detect_all_wq) {
		pr_err("[ASoC:HDST9620] create_singlethread_workqueue for headset_detect failed!\n");
		ret = 0;
		goto out_detect_all;
	}

	INIT_DELAYED_WORK(&hdst->ldetl_dwork, headset_ldetl_work_func);
	hdst->ldetl_wq = create_singlethread_workqueue("headset_ldetl");
	if (!hdst->ldetl_wq) {
		pr_err("[ASoC:HDST9620] create_singlethread_workqueue for headset_ldetl failed!\n");
		ret = 0;
		goto out_ldetl;
	}

	INIT_DELAYED_WORK(&hdst->reg_dump_dwork, sprd_dump_reg_work);
	hdst->reg_dump_wq = create_singlethread_workqueue("headset_reg_dump");
	if (!hdst->reg_dump_wq) {
		pr_err("[ASoC:HDST9620] create_singlethread_workqueue for headset_reg_dump failed!\n");
		ret = 0;
		goto out_reg_dump;
	}

	if (hdst->debug_level >= 2)
		queue_delayed_work(hdst->reg_dump_wq, &hdst->reg_dump_dwork,
				   125);

	hdst->typec_ws = wakeup_source_create("headset_detect_wakelock");
	wakeup_source_add(hdst->typec_ws);

	mutex_init(&hdst->irq_det_ldetl_mutex);
	mutex_init(&hdst->irq_det_all_lock);
	mutex_init(&hdst->irq_btn_lock);
	mutex_init(&hdst->irq_det_mic_lock);
	mutex_init(&hdst->hmicbias_polling_lock);
	mutex_init(&hdst->audio_on_lock);
	mutex_init(&hdst->btn_detecting_lock);

	gpiod_set_debounce(gpio_to_desc(hdst->aud_int_gpio),
			   hdst->aud_int_debounce * 1000);

	init_completion(&hdst->insert_all_completion);
	init_completion(&hdst->mdet_completion);
	init_completion(&hdst->detect_completion);

	sprd_headset_debug_sysfs_init();

	sprd_get_adc_cal_from_efuse(hdst);

	ret = devm_request_threaded_irq(&hdst->pdev->dev, hdst->irq,
					sprd_headset_top_eic_handler, NULL,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT |
					IRQF_COND_SUSPEND,
					"head_aud_det_int_all", hdst);
	if (ret < 0) {
		pr_err("[ASoC:HDST9620] failed to request IRQ_%d(GPIO_%d)\n",
		       hdst->irq, hdst->aud_int_gpio);
		devm_free_irq(&hdst->pdev->dev, hdst->irq, hdst);
		goto out_ws;
	}

	sprd_codec_regmap_update_bits(0xd4, 0x2000,
				      hdst->extcon_notifier_registered ? 0 : 0x2000);
	sprd_codec_regmap_update_bits(0xd8, 0x8000, 0x8000);

	{
		unsigned int ana0 = 0, ana1 = 0;

		sprd_codec_regmap_read(0xd4, &ana0);
		sprd_codec_regmap_read(0xd8, &ana1);
		CODEC_DBG("%s ANA_HDT1(00D4) %x, ANA_HDT2(00D8) %x\n", __func__,
			  ana0, ana1);
	}

	usleep_range(3000, 3500);
	sprd_headset_eic_plugin_enable();

	if (!hdst->extcon_notifier_registered) {
		sprd_headset_dump_eic(__func__);
		return 0;
	}

	gpiod_direction_output(hdst->lr_gpio, 0);
	gpiod_direction_output(hdst->mic_gpio, 0);

	hdst->extcon = extcon_get_edev_by_phandle(&hdst->pdev->dev, 0);
	if (IS_ERR(hdst->extcon)) {
		dev_err(codec->dev,
			"typec analog headset failed to find gpio extcon device, ret %d\n",
			(int)PTR_ERR(hdst->extcon));
		return (int)PTR_ERR(hdst->extcon);
	}

	hdst->extcon_nb.notifier_call = sprd_headset_typec_notifier;
	ret = extcon_register_notifier(hdst->extcon, 0x15, &hdst->extcon_nb);
	if (ret) {
		dev_err(codec->dev,
			"failed to register extcon HEADPHONE notifier, ret %d\n",
			ret);
		return ret;
	}

	if (extcon_get_state(hdst->extcon, 0x15)) {
		hdst->typec_attached = 1;
		sprd_headset_typec_work(hdst);
	}

	sprd_headset_dump_eic(__func__);

	return 0;

parse_fail:
	pr_err("[ASoC:HDST9620] sprd_headset_parse fail %d\n", ret);
	return ret;

out_ws:
	wakeup_source_remove(hdst->typec_ws);
	wakeup_source_destroy(hdst->typec_ws);
out_reg_dump:
	cancel_delayed_work_sync(&hdst->reg_dump_dwork);
	destroy_workqueue(hdst->reg_dump_wq);
out_ldetl:
	destroy_workqueue(hdst->ldetl_wq);
out_detect_all:
	destroy_workqueue(hdst->detect_all_wq);
out_button:
	destroy_workqueue(hdst->button_wq);
out_mic:
	destroy_workqueue(hdst->mic_wq);

	return ret;
}
