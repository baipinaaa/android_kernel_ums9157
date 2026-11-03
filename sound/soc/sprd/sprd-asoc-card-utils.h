/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sprd-asoc-card-utils.h  --  Unisoc (Spreadtrum) ASoC machine driver helpers
 *
 * Reconstructed from the vendor module snd-soc-sprd-card.ko
 * (.text 0x444c = 17492 bytes, 81 functions, built from three sources:
 *  sprd-asoc-card-utils.c, sprd-asoc-card-utils-legacy.c and
 *  vbc-rxpx-codec-sc27xx.c).
 *
 * Layout notes taken from the vendor binary:
 *   struct snd_soc_card is followed inline by a vendor private area; the
 *   original code kept it inside the card allocation (card head 0x410 bytes,
 *   then num_links * sizeof(struct snd_soc_dai_link)).  The rebuild keeps the
 *   same information in "struct sprd_asoc_card_priv" which is stored in
 *   card->drvdata; the offsets below are the vendor ones and are documented
 *   for reference only.
 */

#ifndef __SPRD_ASOC_CARD_UTILS_H
#define __SPRD_ASOC_CARD_UTILS_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <sound/soc.h>

#define SPRD_MAX_EXT_FUNC	12
#define SPRD_MAX_EXT_PA		12
#define SPRD_MAX_DAI_LINKS	8

struct sprd_asoc_card_ext_hook;

/* vendor of_device_id private data (2 x u32, vendor .rodata+0x2388..0x23af) */
struct sprd_asoc_data {
	u32 type;
	u32 codec;
};

/* board kcontrol callbacks (legacy file, used by the control table) */
int board_func_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);
int board_func_set(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);
int board_mute_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);
int board_mute_set(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);
int smartamp_boost_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int smartamp_boost_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);

/* per link cpu / codec clock description (0x28 bytes) */
struct sprd_dai_comp {
	u32 pad0;			/* +0x00 */
	u32 sysclk;			/* +0x08 */
	u32 slots;			/* +0x10 */
	u32 slot_width;			/* +0x14 */
	u32 tx_mask;			/* +0x18 */
	u32 rx_mask;			/* +0x1c */
	struct clk *clk;		/* +0x20 */
};

/* per link private data, card+0x368 + idx * 0x58 in the vendor binary */
struct sprd_card_comp {
	struct sprd_dai_comp cpu;	/* +0x00 */
	struct sprd_dai_comp codec;	/* +0x28 */
	u32 mclk_fs;			/* +0x50 */
};

struct sprd_smartamp_boost {
	int enable_gpio;		/* vendor: card+0x404 area */
	int mode_gpio;
	u32 mode;
};

struct sprd_asoc_card_priv {
	u32 mclk_fs;			/* vendor: card+0x370 */
	u32 hp_det_gpio;		/* vendor: card+0x374 hp jack detect gpio */
	u32 hp_det_gpio_flags;		/* vendor: card+0x378 */
	u32 mic_det_gpio;		/* vendor: card+0x37c mic detect gpio */
	u32 mic_det_gpio_flags;		/* vendor: card+0x380 */
	u32 codec_type;			/* vendor: card+0x384 */
	u32 board_id;			/* vendor: card+0x388 */
	u32 fm_open_src;		/* vendor: card+0x3f8 */
	u32 fm_hw_rate;			/* vendor: card+0x3fc */
	u32 codec_replace_adc_rate;	/* vendor: card+0x400 */
	struct sprd_card_comp *comps;
	struct sprd_asoc_card_ext_hook *ext_hook;	/* vendor: card+0x390 */
	struct sprd_smartamp_boost boost;
};

/* hook signature: int (*)(int func_id, int on) */
typedef int (*sprd_hook_fn)(int, int);

struct sprd_asoc_card_ext_hook {
	sprd_hook_fn fn[SPRD_MAX_EXT_FUNC];
};

struct sprd_hook_spk_priv {
	u32 gpio[SPRD_MAX_EXT_PA];
	u32 share_gpio[SPRD_MAX_EXT_PA];
	u8 used[SPRD_MAX_EXT_PA];
	u32 state[SPRD_MAX_EXT_PA];
	u32 rcv_shared_multi_spk;
	spinlock_t lock;
};

struct sprd_board_priv {
	u32 func_state[SPRD_MAX_EXT_PA];
	struct {
		u32 state;
		u32 on;
	} ext[6];
};

/* globals shared by the three source files (mirror of the vendor .bss/.data) */
extern u32 sp_audio_debug_flag;
extern struct sprd_asoc_card_ext_hook *g_card_ext_hook;
extern struct sprd_hook_spk_priv hook_spk_priv;
extern struct sprd_board_priv board;
extern u32 select_mode;
extern const char *func_name[SPRD_MAX_EXT_FUNC];

extern const struct snd_kcontrol_new sprd_asoc_card_controls[];
extern const struct snd_soc_dapm_widget sprd_asoc_card_widgets[];

/* GPR accessors exported for the platform / codec drivers */
void arch_audio_set_pmu_com_apb_gpr(struct regmap *gpr);
void arch_audio_set_pmu_apb_gpr(struct regmap *gpr);
void arch_audio_set_ap_apb_gpr(struct regmap *gpr);
void arch_audio_set_aon_apb_gpr(struct regmap *gpr);
void arch_audio_set_anlg_phy_g(struct regmap *gpr);
void arch_audio_set_agcp_ahb_gpr(struct regmap *gpr);
struct regmap *arch_audio_get_pmu_com_apb_gpr(void);
struct regmap *arch_audio_get_pmu_apb_gpr(void);
struct regmap *arch_audio_get_ap_apb_gpr(void);
struct regmap *arch_audio_get_aon_apb_gpr(void);
struct regmap *arch_audio_get_anlg_phy_g(void);
struct regmap *arch_audio_get_agcp_ahb_gpr(void);

int pmu_com_apb_gpr_null_check(void);
int pmu_apb_gpr_null_check(void);
int ap_apb_gpr_null_check(void);
int aon_apb_gpr_null_check(void);
int anlg_phy_g_null_check(void);
int agcp_ahb_gpr_null_check(void);

void set_agcp_ahb_offset(u32 set, u32 clr);
u32 get_agcp_ahb_set_offset(void);
u32 get_agcp_ahb_clr_offset(void);
u32 get_sp_audio_debug_flag(void);

int agdsp_access_enable(void);
int agdsp_access_disable(void);

int asoc_sprd_register_card(struct snd_soc_card *card, struct device *dev);
int asoc_sprd_card_probe(struct device *dev, struct snd_soc_card **card_pp);
int asoc_sprd_card_remove(struct snd_soc_card *card);
int asoc_sprd_card_set_ops(struct snd_soc_card *card, const void *ops);
int asoc_sprd_card_set_compr_ops(struct snd_soc_card *card, const void *ops);
int asoc_sprd_card_set_init(struct snd_soc_card *card, const void *init);
int asoc_sprd_card_set_bhpf(struct snd_soc_card *card, const void *bhpf);

int asoc_sprd_card_parse_of(struct device_node *np, struct snd_soc_card *card);
int asoc_sprd_card_dai_link_of(struct device_node *np,
			       struct snd_soc_card *card, int idx, int single);
int asoc_sprd_card_parse_daifmt(struct device_node *np,
				struct snd_soc_card *card,
				struct device_node *codec_node,
				const char *prefix, int idx);
int asoc_sprd_card_sub_parse_of(struct device_node *np,
				struct sprd_dai_comp *comp,
				struct snd_soc_dai_link_component *dai,
				u32 *flag);
int asoc_sprd_card_dai_link_of_misc(struct device_node *np,
				    struct snd_soc_dai_link *link,
				    struct snd_soc_card *card);

int sprd_asoc_card_parse_ext_hook(struct device *dev,
				  struct sprd_asoc_card_ext_hook *hook);
void sprd_asoc_card_ext_hook_free_gpio(struct device *dev);
int sprd_asoc_card_parse_smartamp_boost(struct device *dev,
					struct sprd_smartamp_boost *boost);
int sprd_asoc_ext_hook_register(void *hook);
int sprd_asoc_ext_hook_unregister(void *hook);

int sprd_asoc_board_comm_probe(struct snd_soc_card *card);
int sprd_asoc_board_comm_late_probe(struct snd_soc_card *card);
void sprd_asoc_shutdown(struct snd_soc_pcm_substream *substream);

int sprd_audio_debug_init(void);
ssize_t snd_pcm_sprd_debug_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos);
ssize_t snd_pcm_sprd_debug_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos);

void *dfm_priv_get(void);

static inline struct sprd_asoc_card_priv *sprd_card_priv(struct snd_soc_card *card)
{
	return card ? card->drvdata : NULL;
}

#endif /* __SPRD_ASOC_CARD_UTILS_H */
