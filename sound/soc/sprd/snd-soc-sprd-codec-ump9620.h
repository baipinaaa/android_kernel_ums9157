// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc ump9620 audio codec (vendor_boot: snd-soc-sprd-codec-ump9620.ko)
 *
 * The module carries two drivers which the vendor keeps in one object:
 *   - sprd-codec.c           the ALSA SoC codec (snd-soc-sprd-codec-ump9620-core.c)
 *   - sprd-headset-ump9620.c the headset / button detection state machine
 *
 * All offsets below were taken from the disassembly, the driver data is
 * 0x190 bytes and is allocated with devm_kmalloc() in sprd_codec_probe().
 */
#ifndef __SND_SOC_SPRD_CODEC_UMP9620_H
#define __SND_SOC_SPRD_CODEC_UMP9620_H

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

/* codec register windows, as tested by sprd_codec_read()/sprd_codec_write() */
#define SPRD_CODEC_ANA_BASE	0x2100
#define SPRD_CODEC_ANA_END	0x3290
#define SPRD_CODEC_DIG_BASE	0x1000
#define SPRD_CODEC_DIG_END	0x12f4
/* the analog block is reached through regmap with this base stripped */
#define SPRD_CODEC_REG_OFS	0x3000

struct sprd_codec_priv {
	struct snd_soc_component *comp;		/* +0x00 back pointer */
	u32 da_rate;			/* +0x08 sample rate, playback side */
	u32 ad_rate;			/* +0x0c sample rate, capture side */
	u32 ad1_rate;			/* +0x10 */
	u32 irq;				/* +0x14 DP (digital codec) irq number */
	u8 pad0[0x20];			/* +0x18 */

	u8 ana_state;			/* +0x38 analog power state machine */
	u8 pad1[0x0f];			/* +0x39 */

	struct regulator *ana_regulator;	/* +0x48 */
	struct regulator *headmicbias;		/* +0x50 */
	struct regulator *vb;			/* +0x58 */
	struct regulator *dahpl_chn;		/* +0x60 */
	struct regulator *bias;			/* +0x68 */

	u32 hp_chn_num;			/* +0x70 number of hp channels in use */
	u32 headmicbias_volt[2];	/* +0x74 {low, high} ADC thresholds */
	u32 vb_volt[2];			/* +0x7c */
	u8 pad_volt[0x08];		/* +0x84 */

	u32 fixed_rate[3];		/* +0x8c fixed-sample-rate from DT */
	u32 dac_lrclk_sel;		/* +0x98 */
	u32 adc_lrclk_sel;		/* +0x9c */
	u32 adc1_lrclk_sel;		/* +0xa0 */
	u32 lrdat_sel;			/* +0xa4 */
	u32 ad_rate_fixup;		/* +0xa8 record rate substitution */
	u32 pa_short_stat;		/* +0xac last PA short reading */
	u32 cp_short_stat;		/* +0xb0 step the CP short cleared at, 0 = none */
	u32 startup_cnt;		/* +0xb4 active PCM streams */

	struct mutex digital_enable_mutex;	/* +0xb8 */
	u32 dig_access_cnt;		/* +0xd8 */
	u16 dac_switch;			/* +0xdc bitmask, bit = mux shift */
	u8 pad3[0x02];			/* +0xde */
	u32 neg_cp_efuse;		/* +0xe0 */
	u32 fgu_4p2_efuse;		/* +0xe4 */

	u32 hp_mix_mode;		/* +0xe8 */
	u32 spk_pga;			/* +0xec */
	u32 spk_dg_fall;		/* +0xf0 */
	u8 pad4[0x04];			/* +0xf4 */

	struct mutex dig_access_mutex;	/* +0xf8 */
	u8 digital_en;			/* +0x118 digital block powered */
	u8 dig_access_en;		/* +0x119 */
	u8 pad6[0x02];			/* +0x11a */
	u32 ivsence_dmic;		/* +0x11c */
	u8 drv_soft_t;			/* +0x120 */
	u8 pad7;			/* +0x121 */
	u16 das_input_mux;		/* +0x122 */
	u8 daldo_bypass;		/* +0x124 */
	u8 pad8[0x03];			/* +0x125 */
	u32 clk_26m_in_sel;		/* +0x128 */

	u32 daaol_buf_dccal;		/* +0x12c */
	u32 daaor_buf_dccal;		/* +0x130 */
	u32 dahpl_buf_dccal;		/* +0x134 */
	u32 dahpr_buf_dccal;		/* +0x138 */
	u32 dahp_os_inv;		/* +0x13c */
	u16 dahp_os_d;			/* +0x140 */
	u8 pad9[0x02];			/* +0x142 */
	u32 hp_chn_type;		/* +0x144 */
	u32 add0_data_sel;		/* +0x148 */
	u8 rcv_pop_ramp;		/* +0x14c */
	u8 pad10[0x03];			/* +0x14d */
	u32 sdm_l;			/* +0x150 */
	u32 sdm_h;			/* +0x154 */

	struct regmap *aon_apb;		/* +0x158 syscon-aon-apb */
	struct regmap *agcp_apb;	/* +0x160 syscon-agcp-apb */
	struct regmap *aon_apb2;	/* +0x168 syscon-aon-apb (second) */
	struct regmap *dvfs_apb;	/* +0x170 syscon-dvfs-apb */

	u32 clk26m_sinout_reg;		/* +0x178 */
	u32 clk26m_sinout_mask;		/* +0x17c */

	u32 ana_chip_id;		/* +0x180 */
	u32 ana_chip_ver;		/* +0x184 */
	u8 bias_on_off;			/* +0x188 */
	u8 pad11[0x07];			/* +0x189 */

};

/* the DAPM tables of the codec, decoded from .rodata by recon/dapm.py */
extern const struct snd_kcontrol_new sprd_codec_snd_controls[];
extern const char * const c_hp_chn_type_name[];
extern const struct snd_soc_dapm_widget sprd_codec_dapm_widgets[];
extern const struct snd_soc_dapm_route sprd_codec_intercon[];
extern const struct snd_soc_component_driver soc_codec_dev_sprd_codec;
extern struct snd_soc_dai_driver sprd_codec_dai[];
/* names of the detect modes, indexed by sprd_headset.detect_mode (7 entries) */
extern const char * const eic_hw_state[];
extern const unsigned int cp_short_bg[];
extern const unsigned int cp_short_bias[];

extern void __iomem *sprd_codec_dp_base;

/* shared headset state, defined in sprd-headset-ump9620.c */
struct notifier_block;
struct extcon_dev;

/* one entry of the headset supply table (0x18 bytes) */
struct sprd_regulator {
	const char *name;			/* +0x00 */
	u32 index;				/* +0x08 */
	u8 en;					/* +0x0c */
	u8 pad;					/* +0x0d */
	u16 pad2;				/* +0x0e */
	struct regulator *reg;			/* +0x10 */
};

struct iio_channel;

/* one row of the headset button table parsed from DT (12 bytes per row) */
struct sprd_headset_button {
	u32 low;				/* +0x00 lower ADC bound */
	u32 high;				/* +0x04 upper ADC bound */
	u32 code;				/* +0x08 key code from DT */
};

struct sprd_headset {
	int headset_type;			/* +0x00 -1 unknown,0 headset,1 headphone */
	u8 pad0_type[0x04];			/* +0x04 */
	u8 pad0a[0x18];				/* +0x08 */
	struct platform_device *pdev;		/* +0x20 */
	/**
	 * The vendor keeps all DT parsed data in a struct sprd_headset_pdata
	 * embedded at +0x28 and passes its address to a few helpers; the fields
	 * are flattened here but the base is still needed.
	 */
	int pdata[1];				/* +0x28 first word of the embedded DT data */
	u32 hw_type;				/* +0x2c 0: 4 pole / 1: 3 pole */
	int aud_int_gpio;			/* +0x30 */
	u32 aud_int_debounce;		/* +0x34 aud_int gpio debounce, ms */
	u8 pad0c[0x04];			/* +0x38 */
	int eu_us_switch_gpio;			/* +0x3c */
	u32 threshold_3pole;			/* +0x40 3-pole threshold in ADC units */
	u8 pad0d1c[0x04];			/* +0x44 */
	u32 mic_gain;				/* +0x48 */
	u8 pad0d1b[0x04];			/* +0x4c */
	u32 sprd_adc_gnd;			/* +0x50 ground reference of the ladder */
	u32 sprd_half_adc_gnd;			/* +0x54 half of it, the second ladder */
	u32 one_half_adc_gnd;			/* +0x58 one and a half times adc_gnd */
	u8 pad0d2[0x04];			/* +0x5c */
	u32 adc_scale;				/* +0x60 */
	u8 pad0d3[0x04];			/* +0x64 */
	struct sprd_headset_button *btns;	/* +0x68 button table from DT */
	u32 btn_count;				/* +0x70 number of button rows */
	u8 pad0d4[0x0c];			/* +0x74 */
	u8 no_fm_mute;				/* +0x80 */
	u8 extcon_notifier_registered;		/* +0x81 */
	u8 pad0e1[0x06];			/* +0x82 */
	struct gpio_desc *mic_gpio;		/* +0x88 D+/D- switch, mic side */
	struct gpio_desc *lr_gpio;		/* +0x90 D+/D- switch, left/right */
	struct regulator *typec_supply;		/* +0x98 typec switch supply */
	u32 typec_supply_vol;			/* +0xa0 micro volt */
	u8 pad0e2[0x94];			/* +0xa4 */
	/* one delayed work + its private workqueue per detect source */
	struct delayed_work detect_all_dwork;	/* +0x138 */
	u8 pad0e3[0x40];			/* +0x180 */
	struct workqueue_struct *detect_all_wq;	/* +0x1c0 */
	/**
	 * signalled by the insert-all EIC of a 4 pole plug; the ldetl worker
	 * waits on it after switching the detect source over
	 */
	struct completion insert_all_completion;	/* +0x1c8 */
	struct completion mdet_completion;	/* +0x1e8 */
	struct completion detect_completion;	/* +0x208 */
	u8 hmicbias_on;				/* +0x228 */
	u8 pad1a[0x07];				/* +0x229 */
	struct sprd_regulator regu[11];		/* +0x230 */
	struct delayed_work button_dwork;	/* +0x338 */
	u8 pad1b[0x40];				/* +0x380 */
	u32 detect_mode;			/* +0x3c0 */
	u32 state;				/* +0x3c4 headset state machine */
	struct wakeup_source *typec_ws;		/* +0x3c8 */
	u32 reset_marker;			/* +0x3d0 cleared by the reset only */
	u8 audio_on;				/* +0x3d4 audio path active */
	u8 in_ldetl;				/* +0x3d5 */
	u8 detect_again;			/* +0x3d6 */
	u8 pad2[0x01];				/* +0x3d7 */
	struct mutex audio_on_lock;		/* +0x3d8 */
	struct mutex btn_detecting_lock;	/* +0x3f8 */
	u32 button_status;			/* +0x418 */
	u8 button_plugged;			/* +0x41c */
	u8 pad3a1b[0x03];			/* +0x41d */
	struct workqueue_struct *button_wq;	/* +0x420 */
	struct snd_soc_component *comp2;	/* +0x428 codec component */
	u8 pad3a2a1[0x30];			/* +0x430 */
	struct semaphore irq_det_ldetl_lock;	/* +0x460 */
	struct snd_soc_jack jack;		/* +0x478 */
	u8 pad_btn[0x08];			/* +0x500 */
	struct snd_soc_jack button_jack;	/* +0x508 */
	u8 pad_btn2[0x0c];			/* +0x590 */
	u32 button_eic_mask;			/* +0x59c */
	struct iio_channel *headmic_chan;	/* +0x5a0 */
	struct mutex irq_btn_lock;		/* +0x5a8 */
	u8 pad_irqbtn[0x20];		/* +0x5c8 */
	struct mutex irq_det_all_lock;		/* +0x5e8 */
	struct mutex irq_det_mic_lock;		/* +0x608 */
	struct delayed_work reg_dump_dwork;	/* +0x628 */
	u8 pad3a2c[0x40];			/* +0x670 */
	struct workqueue_struct *reg_dump_wq;	/* +0x6b0 */
	u32 debug_level;			/* +0x6b8 */
	u32 mic_bias_state;			/* +0x6bc */
	u8 pad_dbg[0x10];			/* +0x6c0 */
	u32 insert_started;			/* +0x6d0 */
	u8 insert_all_active;			/* +0x6d4 */
	u8 pad3b1a[0x03];			/* +0x6d5 */
	u32 irq;				/* +0x6d8 headset EIC irq number */
	u8 pad3b1b[0x04];			/* +0x6dc */
	struct delayed_work mic_dwork;		/* +0x6e0 */
	u8 pad3b1c[0x40];			/* +0x728 */
	struct workqueue_struct *mic_wq;	/* +0x768 */
	struct delayed_work ldetl_dwork;	/* +0x770 */
	u8 pad3b1d[0x40];			/* +0x7b8 */
	struct workqueue_struct *ldetl_wq;	/* +0x7f8 */
	struct mutex irq_det_ldetl_mutex;	/* +0x800 */
	struct mutex hmicbias_polling_lock;	/* +0x820 */
	u32 ldetl_trig_val_last;		/* +0x840 last level of the ldetl EIC */
	u8 pad3b2[0x0c];			/* +0x844 */
	u32 ldetl_plug_in;			/* +0x850 plug decision of the worker */
	u32 button_pressed;			/* +0x854 */
	u32 plug_state;				/* +0x858 */
	u8 adc_cal_flag;			/* +0x85c */
	u8 typec_attached;			/* +0x85d */
	u8 pad4b[0x02];				/* +0x85e */
	struct notifier_block extcon_nb;	/* +0x860 */
	struct extcon_dev *extcon;		/* +0x878 */
	/* pending EIC mask the top handler hands over to the ldetl worker */
	u32 int_shadow;				/* +0x880 */
	u8 pad5a[0x04];				/* +0x884 */
	unsigned long detect_deadline;		/* +0x888 */
	u8 pad5b[0x08];				/* +0x890 */
};

extern struct sprd_headset *sprd_hdst;

/* detect result handed over by the type-detect state machine */
struct sprd_headset_hw_status {
	u32 unknown;				/* +0x00 */
	u32 type;				/* +0x04 0: headset, 1: headphone */
};

int sprd_headset_eic_plugin_enable(void);
int sprd_headset_prepare_insert_all_plugout(struct sprd_headset *hdst);
int sprd_headset_valid_insert_all(int msleep_ms);
int sprd_headset_get_mic_voltage(struct sprd_headset *hdst);
int sprd_headset_typec_mic_switch(struct sprd_headset *hdst);
void sprd_headset_prepare_plugout(struct sprd_headset *hdst);
void sprd_headset_prepare_next_plugin(struct sprd_headset *hdst);
void sprd_headset_insert_all_plugout(struct sprd_headset *hdst);
void sprd_headset_prepare_insert_all_plugout(struct sprd_headset *hdst);
int dsp_fm_mute_by_set_dg(int on);
int sprd_button_ideal_adc(struct sprd_headset *hdst);
void sprd_dump_reg_work(struct work_struct *work);
void sprd_headset_type_report(struct sprd_headset *hdst);
int sprd_process_4pole_type(struct sprd_headset *hdst);
void sprd_headset_prepare_ldetl(struct sprd_headset *hdst);
int sprd_headset_typec_get_hpl_voltage(struct sprd_headset *hdst);
void sprd_headset_jack_report(struct sprd_headset *hdst,
			      struct snd_soc_jack *jack, int status, int mask);
int sprd_headset_reset_hw_status(struct sprd_headset *hdst);
void sprd_headset_set_hw_status(struct sprd_headset *hdst,
				struct sprd_headset_hw_status *status);

/* register window handed over by the machine driver */
struct sprd_codec_regmap {
	struct regmap *map;			/* +0x00 */
	unsigned int offset;			/* +0x08 */
};

void sprd_codec_set_global_regmap(struct regmap *map, unsigned int offset);
int sprd_codec_regmap_update_bits(unsigned int reg, unsigned int mask,
				  unsigned int val);
int sprd_codec_regmap_read(unsigned int reg, unsigned int *val);
int sprd_codec_regmap_update_bits_force(unsigned int reg, unsigned int mask,
					unsigned int val);
void sprd_headset_set_global_variables(struct sprd_codec_regmap *info);
u32 sci_get_ana_chip_id(void);
/* shared with the headset half, see sprd_codec_intc_irq() */
void sprd_codec_intc_irq(struct snd_soc_component *comp, u32 int_shadow);

int sprd_headset_soc_probe(struct snd_soc_component *comp);
int headset_register_notifier(struct notifier_block *nb);
int headset_unregister_notifier(struct notifier_block *nb);
int headset_get_plug_state(void);
int vbc_close_fm_dggain(void);
int dsp_fm_mute_by_set_dg(void);
void sprd_headset_power_deinit(struct sprd_headset *hdst);
void sprd_enable_hmicbias_polling(int on, int flag);
void sprd_codec_wait(int ms);

/* codec half */
unsigned int sprd_codec_read(struct snd_soc_component *comp, unsigned int reg);
int sprd_codec_write(struct snd_soc_component *comp, unsigned int reg,
		     unsigned int val);

/* shared with the headset half of the module */
int sprd_codec_dig_probe(struct platform_device *pdev);
void sprd_codec_dig_remove(struct sprd_codec_priv *priv);
int cp_short_check(struct sprd_codec_priv *priv);
void arch_audio_codec_digital_reg_enable_v2(struct sprd_codec_priv *priv);
void codec_digital_reg_disable(struct sprd_codec_priv *priv);
int agdsp_access_enable(void);
void agdsp_access_disable(void);
u32 get_sp_audio_debug_flag(void);
int arch_audio_set_pmu_apb_gpr(struct regmap *pmu_apb);
irqreturn_t sprd_codec_dp_irq(int irq, void *data);
irqreturn_t sprd_codec_dp_irq(int irq, void *data);
int set_agcp_ahb_offset(u32 set, u32 clr);
int sprd_codec_ap_adc_power_on(struct snd_soc_component *comp, bool on);
void osc_4m_clk_cal(struct snd_soc_component *comp, bool on);
void rc_60m_clk(struct snd_soc_component *comp, bool on);
int sprd_codec_clk26m_sinout(struct sprd_codec_priv *priv, bool en);
void sprd_codec_sample_rate_setting(struct sprd_codec_priv *priv);
int sprd_codec_set_sample_rate(struct snd_soc_component *comp, u32 rate, u32 dummy);
int sprd_codec_set_ad_sample_rate(struct snd_soc_component *comp, u32 rate, u32 shift);

#endif /* __SND_SOC_SPRD_CODEC_UMP9620_H */
