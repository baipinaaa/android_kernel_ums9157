// SPDX-License-Identifier: GPL-2.0
/*
 * sprd-asoc-card-utils.c  --  Unisoc ASoC machine driver common helpers
 *
 * Reconstructed from snd-soc-sprd-card.ko (vendor kernel 5.4.210, T157).
 * Original file: .../audio_driver/sprd/machine/sprd_card/sprd-asoc-card-utils.c
 *
 * Function map (binary .text offsets, sizes in bytes):
 *   0x0930    64 set_agcp_ahb_offset
 *   0x0970    28 get_agcp_ahb_set_offset
 *   0x09a8    28 get_agcp_ahb_clr_offset
 *   0x09c4    28 get_sp_audio_debug_flag
 *   0x09e0 .. 0x0a90  arch_audio_{set,get}_* GPR accessors (12 x 28..44)
 *   0x0b60 .. 0x0bb0  *_null_check (6 functions, 48..64 bytes each)
 *   0x0bc0   100 asoc_sprd_register_card
 *   0x0c94   120 asoc_sprd_card_set_ops
 *   0x0cac    28 asoc_sprd_card_remove
 *   0x0cb4   428 asoc_sprd_card_probe
 *   0x0e60   896 asoc_sprd_card_parse_of
 *   0x11e0  1340 asoc_sprd_card_dai_link_of
 *   0x171c   284 asoc_sprd_card_parse_daifmt
 *   0x1838   464 asoc_sprd_card_sub_parse_of
 *   0x1a08  2076 asoc_sprd_card_dai_link_of_misc
 *   0x2224    24 asoc_sprd_card_set_compr_ops
 *   0x223c    24 asoc_sprd_card_set_init
 *   0x2254    24 asoc_sprd_card_set_bhpf
 */

#define pr_fmt(fmt) "[ASoC:BOARD] " fmt

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "sprd-asoc-card-utils.h"

/* ---------------------------------------------------------------------- */
/* register map handles (vendor .bss 0x010 .. 0x038)                      */
/* ---------------------------------------------------------------------- */
static struct regmap *g_pmu_com_apb_gpr;
static struct regmap *g_pmu_apb_gpr;
static struct regmap *g_ap_apb_gpr;
static struct regmap *g_aon_apb_gpr;
static struct regmap *g_anlg_phy_g;
static struct regmap *g_agcp_ahb_gpr;

static u32 agcp_ahb_set_offset;
static u32 agcp_ahb_clr_offset;

/* vendor .data+0x120, initial value 1 (audio debug prints enabled) */
u32 sp_audio_debug_flag = 1;
EXPORT_SYMBOL_GPL(get_sp_audio_debug_flag);

/* vendor .bss+0x190, the currently registered ext hook (owned by the card) */
struct sprd_asoc_card_ext_hook *g_card_ext_hook;

/* vendor .bss+0x040 .. 0x07f : ops / compr_ops / init / be_hw_params_fixup tables */
static struct {
	const void *tbl;
	size_t cnt;
} asoc_sprd_card_ops, asoc_sprd_card_compr_ops,
  asoc_sprd_card_init, asoc_sprd_card_bhpf;

/* ---------------------------------------------------------------------- */
/* agcp ahb offsets                                                       */
/* ---------------------------------------------------------------------- */
void set_agcp_ahb_offset(u32 set, u32 clr)
{
	agcp_ahb_set_offset = set;
	agcp_ahb_clr_offset = clr;
	/* the vendor file used a plain printk with the "[ASoC: COM ]" tag */
	printk(KERN_INFO "6[ASoC: COM ] %s agcp_ahb_set_offset 0x%x, agcp_ahb_clr_offset 0x%x\n",
	       "set_agcp_ahb_offset", set, clr);
}
EXPORT_SYMBOL_GPL(set_agcp_ahb_offset);

u32 get_agcp_ahb_set_offset(void)
{
	return agcp_ahb_set_offset;
}
EXPORT_SYMBOL_GPL(get_agcp_ahb_set_offset);

u32 get_agcp_ahb_clr_offset(void)
{
	return agcp_ahb_clr_offset;
}
EXPORT_SYMBOL_GPL(get_agcp_ahb_clr_offset);

u32 get_sp_audio_debug_flag(void)
{
	return sp_audio_debug_flag;
}

/* ---------------------------------------------------------------------- */
/* GPR (global register) accessors                                        */
/* ---------------------------------------------------------------------- */
void arch_audio_set_pmu_com_apb_gpr(struct regmap *gpr)
{
	g_pmu_com_apb_gpr = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_pmu_com_apb_gpr);

struct regmap *arch_audio_get_pmu_com_apb_gpr(void)
{
	return g_pmu_com_apb_gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_get_pmu_com_apb_gpr);

void arch_audio_set_pmu_apb_gpr(struct regmap *gpr)
{
	g_pmu_apb_gpr = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_pmu_apb_gpr);

struct regmap *arch_audio_get_pmu_apb_gpr(void)
{
	return g_pmu_apb_gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_get_pmu_apb_gpr);

void arch_audio_set_ap_apb_gpr(struct regmap *gpr)
{
	g_ap_apb_gpr = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_ap_apb_gpr);

struct regmap *arch_audio_get_ap_apb_gpr(void)
{
	return g_ap_apb_gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_get_ap_apb_gpr);

void arch_audio_set_aon_apb_gpr(struct regmap *gpr)
{
	g_aon_apb_gpr = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_aon_apb_gpr);

struct regmap *arch_audio_get_aon_apb_gpr(void)
{
	return g_aon_apb_gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_get_aon_apb_gpr);

void arch_audio_set_anlg_phy_g(struct regmap *gpr)
{
	g_anlg_phy_g = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_anlg_phy_g);

struct regmap *arch_audio_get_anlg_phy_g(void)
{
	return g_anlg_phy_g;
}
EXPORT_SYMBOL_GPL(arch_audio_get_anlg_phy_g);

void arch_audio_set_agcp_ahb_gpr(struct regmap *gpr)
{
	g_agcp_ahb_gpr = gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_set_agcp_ahb_gpr);

struct regmap *arch_audio_get_agcp_ahb_gpr(void)
{
	return g_agcp_ahb_gpr;
}
EXPORT_SYMBOL_GPL(arch_audio_get_agcp_ahb_gpr);

int pmu_com_apb_gpr_null_check(void)
{
	if (!g_pmu_com_apb_gpr) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s pmu_com_apb_gpr is not initialized!\n",
		       "pmu_com_apb_gpr_null_check");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pmu_com_apb_gpr_null_check);

int pmu_apb_gpr_null_check(void)
{
	if (!g_pmu_apb_gpr) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s pmu_apb_gpr is not initialized!\n",
		       "pmu_apb_gpr_null_check");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pmu_apb_gpr_null_check);

int ap_apb_gpr_null_check(void)
{
	if (!g_ap_apb_gpr) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s g_ap_apb_gpr is not initialized!\n",
		       "ap_apb_gpr_null_check");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ap_apb_gpr_null_check);

int aon_apb_gpr_null_check(void)
{
	if (!g_aon_apb_gpr) {
		/* the vendor message misses the space after %s */
		printk(KERN_ERR "3[ASoC: COM ] ERR: %saon_apb_gpr is not initialized!\n",
		       "aon_apb_gpr_null_check");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(aon_apb_gpr_null_check);

int anlg_phy_g_null_check(void)
{
	if (!g_anlg_phy_g) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s anlg_phy_g2 is not initialized!\n",
		       "anlg_phy_g_null_check");
		return -EFAULT;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(anlg_phy_g_null_check);

int agcp_ahb_gpr_null_check(void)
{
	if (!g_agcp_ahb_gpr) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s g_agcp_ahb_gpr isn't initialized!\n",
		       "agcp_ahb_gpr_null_check");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(agcp_ahb_gpr_null_check);

/* ---------------------------------------------------------------------- */
/* agdsp access window                                                    */
/* ---------------------------------------------------------------------- */
static int __agdsp_access_enable(void)
{
	pr_debug("[ASoC:BOARD] %s\n", "__agdsp_access_enable");
	return 0;
}

static int __agdsp_access_disable(void)
{
	pr_debug("[ASoC:BOARD] %s\n", "__agdsp_access_disable");
	return 0;
}

int agdsp_access_enable(void)
{
	return __agdsp_access_enable();
}

int agdsp_access_disable(void)
{
	return __agdsp_access_disable();
}

int asoc_sprd_register_card(struct snd_soc_card *card, struct device *dev)
{
	int ret = agdsp_access_enable();

	if (!ret) {
		ret = devm_snd_soc_register_card(dev, card);
		agdsp_access_disable();
	}
	return ret;
}
EXPORT_SYMBOL_GPL(asoc_sprd_register_card);

/* ---------------------------------------------------------------------- */
/* ops tables                                                             */
/* ---------------------------------------------------------------------- */
int asoc_sprd_card_set_ops(struct snd_soc_card *card, const void *ops)
{
	asoc_sprd_card_ops.tbl = ops;
	asoc_sprd_card_ops.cnt = card ? *(const u32 *)((const char *)card + sizeof(void *)) : 0;
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_sprd_card_set_ops);

int asoc_sprd_card_set_compr_ops(struct snd_soc_card *card, const void *ops)
{
	asoc_sprd_card_compr_ops.tbl = ops;
	asoc_sprd_card_compr_ops.cnt = card ? *(const u32 *)((const char *)card + sizeof(void *)) : 0;
	return 0;
}

int asoc_sprd_card_set_init(struct snd_soc_card *card, const void *init)
{
	asoc_sprd_card_init.tbl = init;
	asoc_sprd_card_init.cnt = card ? *(const u32 *)((const char *)card + sizeof(void *)) : 0;
	return 0;
}

int asoc_sprd_card_set_bhpf(struct snd_soc_card *card, const void *bhpf)
{
	asoc_sprd_card_bhpf.tbl = bhpf;
	asoc_sprd_card_bhpf.cnt = card ? *(const u32 *)((const char *)card + sizeof(void *)) : 0;
	return 0;
}

int asoc_sprd_card_remove(struct snd_soc_card *card)
{
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_sprd_card_remove);

/* ---------------------------------------------------------------------- */
/* card allocation / probe                                                */
/* ---------------------------------------------------------------------- */
int asoc_sprd_card_probe(struct device *dev, struct snd_soc_card **card_pp)
{
	struct snd_soc_card *card;
	struct sprd_asoc_card_priv *priv;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct regmap *gpr;
	int num_links = 1, ret;

	gpr = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-agcp-ahb");
	if (IS_ERR(gpr)) {
		printk(KERN_ERR "3[ASoC:BOARD] warning: [%s] Get agcp ahb grp failed!(%ld)\n",
		       "asoc_sprd_card_probe", PTR_ERR(gpr));
		gpr = NULL;
	}
	g_agcp_ahb_gpr = gpr;

	if (np && of_get_child_by_name(np, "sprd-audio-card,dai-link")) {
		child = of_get_next_child(np, NULL);
		if (child) {
			num_links = 0;
			do {
				num_links++;
				child = of_get_next_child(np, child);
			} while (child);
		} else {
			num_links = 0;
		}
	}

	if (num_links <= 0)
		num_links = 1;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card->dev = dev;
	card->num_links = num_links;
	card->owner = THIS_MODULE;
	card->dai_link = devm_kcalloc(dev, num_links,
				      sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	priv->comps = devm_kcalloc(dev, num_links, sizeof(struct sprd_card_comp),
				   GFP_KERNEL);
	if (!priv->comps)
		return -ENOMEM;

	card->drvdata = priv;

	if (!np || !of_device_is_available(np)) {
		printk(KERN_ERR "3[ASoC:BOARD] ERR: %s sound node in dts is not available!\n",
		       "asoc_sprd_card_probe");
		return -ENODEV;
	}

	ret = asoc_sprd_card_parse_of(np, card);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "parse error %d\n", ret);
		return ret;
	}

	*card_pp = card;
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_sprd_card_probe);

/* ---------------------------------------------------------------------- */
/* DT parsing                                                             */
/* ---------------------------------------------------------------------- */
int asoc_sprd_card_parse_of(struct device_node *np, struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct sprd_asoc_card_priv *priv = sprd_card_priv(card);
	struct device_node *link;
	int ret = 0, idx;

	if (!np) {
		dev_err(dev, "%s: node is NULL\n", "asoc_sprd_card_parse_of");
		return -EINVAL;
	}

	snd_soc_of_parse_card_name(card, "sprd-audio-card,name");
	dev_dbg(dev, "New sprd-card: %s\n", card->name);

	if (of_find_property(np, "sprd-audio-card,widgets", NULL)) {
		ret = snd_soc_of_parse_audio_simple_widgets(card,
							    "sprd-audio-card,widgets");
		if (ret)
			goto out;
	}

	if (of_find_property(np, "sprd-audio-card,routing", NULL)) {
		ret = snd_soc_of_parse_audio_routing(card,
						     "sprd-audio-card,routing");
		if (ret)
			goto out;
	}

	if (!of_property_read_u32(np, "sprd-audio-card,mclk-fs", &priv->mclk_fs))
		dev_dbg(dev, "mclk-fs: %u\n", priv->mclk_fs);

	if (!of_property_read_u32(np, "sprd-audio-card,fm-hw-rate",
				  &priv->fm_hw_rate))
		dev_dbg(dev, "fm-hw-rate = %u\n", priv->fm_hw_rate);

	if (!of_property_read_u32(np, "sprd-audio-card,codec-replace-adc-rate",
				  &priv->codec_replace_adc_rate))
		dev_dbg(dev, "codec_replace_adc_rate=%u\n",
			priv->codec_replace_adc_rate);

	if (!of_property_read_u32(np, "sprd-audio-card,fm-open-src",
				  &priv->fm_open_src))
		dev_dbg(dev, "is_fm_open_src=%u\n", priv->fm_open_src);

	if (of_get_child_by_name(np, "sprd-audio-card,dai-link")) {
		link = of_get_next_child(np, NULL);
		if (!link)
			goto no_link;

		for (idx = 0; link; link = of_get_next_child(np, link), idx++) {
			dev_dbg(dev, "\tlink %d:\n", idx);
			ret = asoc_sprd_card_dai_link_of(link, card, idx, 0);
			if (ret < 0) {
				dev_err(dev, "%s: Parsing dai link %d failed(%d)!\n",
					"asoc_sprd_card_parse_of", idx, ret);
				return ret;
			}
		}
	} else {
		ret = asoc_sprd_card_dai_link_of(np, card, 0, 1);
		if (ret < 0)
			goto out;
	}

no_link:
	if (!of_property_read_u32(np, "sprd-audio-card,codec-type",
				  &priv->codec_type))
		dev_dbg(dev, "\tcodec : %s / %d\n", card->name,
			priv->codec_type);

	if (!card->name)
		card->name = card->dai_link[0].name;

	ret = sprd_asoc_card_parse_ext_hook(dev, &priv->ext_hook);
	if (ret == -EPROBE_DEFER)
		goto out;
	if (!ret)
		g_card_ext_hook = priv->ext_hook;

	ret = sprd_asoc_card_parse_smartamp_boost(dev, &priv->boost);
out:
	return ret;
}

int asoc_sprd_card_parse_daifmt(struct device_node *np,
				struct snd_soc_card *card,
				struct device_node *codec_node,
				const char *prefix, int idx)
{
	struct snd_soc_dai_link *link = &card->dai_link[idx];
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int fmt, base;

	fmt = snd_soc_of_parse_daifmt(np, prefix, &bitclkmaster, &framemaster);
	base = fmt & ~SND_SOC_DAIFMT_FORMAT_MASK;

	if (prefix[0] && !bitclkmaster && !framemaster) {
		dev_dbg(card->dev, "Revert to legacy daifmt parsing\n");
		fmt = (fmt & ~(SND_SOC_DAIFMT_MASTER_MASK | 0xf1)) |
		      snd_soc_of_parse_daifmt(card->dev->of_node, NULL, NULL, NULL);
	} else if (bitclkmaster != codec_node) {
		fmt = base | (framemaster == codec_node ?
			      SND_SOC_DAIFMT_CBM_CFS : SND_SOC_DAIFMT_CBM_CFM);
	} else {
		fmt = base | (framemaster == codec_node ?
			      SND_SOC_DAIFMT_CBS_CFS : SND_SOC_DAIFMT_CBS_CFM);
	}

	link->dai_fmt = fmt;
	return fmt;
}

int asoc_sprd_card_sub_parse_of(struct device_node *np,
				struct sprd_dai_comp *comp,
				struct snd_soc_dai_link_component *dai,
				u32 *flag)
{
	struct of_phandle_args args;
	struct clk *clk;
	u32 v;
	int ret;

	ret = of_parse_phandle_with_args(np, "sound-dai", "#sound-dai-cells",
					 0, &args);
	if (ret) {
		printk(KERN_ERR "3[ASoC:BOARD] ERR: %s parse phandle '%s' failed!(%d)\n",
		       "asoc_sprd_card_sub_parse_of", "sound-dai", ret);
		return ret;
	}

	dai->of_node = args.np;
	if (flag)
		*flag = args.args_count;

	ret = snd_soc_of_get_dai_name(np, &dai->dai_name);
	if (ret < 0) {
		printk(KERN_ERR "3[ASoC:BOARD] ERR: %s get dai name for '%s' failed!(%d)\n",
		       "asoc_sprd_card_sub_parse_of", np->name, ret);
		return ret;
	}

	ret = snd_soc_of_parse_tdm_slot(np, &comp->tx_mask, &comp->rx_mask,
					&comp->slots, &comp->slot_width);
	if (ret) {
		printk(KERN_ERR "3[ASoC:BOARD] ERR: %s parse tdm for '%s' failed!(%d)\n",
		       "asoc_sprd_card_sub_parse_of", np->name, ret);
		return ret;
	}

	if (of_find_property(np, "clocks", NULL)) {
		clk = of_clk_get(np, 0);
		if (!IS_ERR(clk)) {
			comp->sysclk = clk_get_rate(clk);
			comp->clk = clk;
		}
	} else if (!of_property_read_u32(np, "system-clock-frequency", &v)) {
		comp->sysclk = v;
	} else {
		clk = of_clk_get(args.np, 0);
		if (!IS_ERR(clk))
			comp->sysclk = clk_get_rate(clk);
	}

	return 0;
}

/* ---------------------------------------------------------------------- */
/* dai link parsing                                                       */
/* ---------------------------------------------------------------------- */
int asoc_sprd_card_dai_link_of(struct device_node *np,
			       struct snd_soc_card *card, int idx, int single)
{
	struct device *dev = card->dev;
	struct sprd_asoc_card_priv *priv = sprd_card_priv(card);
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *dai_comp;
	struct of_phandle_args args;
	const char *prefix = (single & 1) ? "sprd-audio-card," : "";
	struct device_node *cpu = NULL, *plat = NULL, *codec = NULL;
	char name[0x80];
	u32 v, v2[2];
	u32 flag = 0;
	int ret;

	snprintf(name, sizeof(name), "%scpu", prefix);
	cpu = of_get_child_by_name(np, name);
	snprintf(name, sizeof(name), "%splat", prefix);
	plat = of_get_child_by_name(np, name);
	snprintf(name, sizeof(name), "%scodec", prefix);
	codec = of_get_child_by_name(np, name);

	if (!cpu || !codec) {
		dev_err(dev, "%s: Can't find cpu(%p) or codec(%p) DT node\n",
			"asoc_sprd_card_dai_link_of", cpu, codec);
		ret = -EINVAL;
		goto out;
	}

	asoc_sprd_card_parse_daifmt(np, card, codec, prefix, idx);

	if (!of_property_read_u32(np, "mclk-fs", &v))
		priv->comps[idx].mclk_fs = v;

	link = &card->dai_link[idx];
	dai_comp = devm_kmalloc(dev, 0x48, GFP_KERNEL | __GFP_ZERO);
	if (!dai_comp) {
		ret = -ENOMEM;
		goto out;
	}
	link->cpus = dai_comp;
	link->codecs = (void *)((char *)dai_comp + 0x18);
	link->platforms = (void *)((char *)dai_comp + 0x30);

	ret = asoc_sprd_card_sub_parse_of(cpu, &priv->comps[idx].cpu,
					  link->cpus, &flag);
	if (ret < 0) {
		link->num_cpus = 0;
		link->num_codecs = 0;
		link->num_platforms = 0;
		devm_kfree(dev, dai_comp);
		goto out;
	}
	link->num_cpus = 1;

	ret = asoc_sprd_card_sub_parse_of(codec, &priv->comps[idx].codec,
					  link->codecs, NULL);
	if (ret == -ENOENT) {
		/* no 'sound-dai' in the codec node: fall back to a dummy codec */
		printk(KERN_INFO "6[ASoC:BOARD] %s: parse for codec failed. Go to use a dummy codec.\n",
		       "asoc_sprd_card_dai_link_of");
		if (!of_property_read_u32_array(codec, "sound-dai", v2, 2)) {
			if (v2[0] == 0) {
				if (v2[1] == 1) {
					link->codecs->name = "sprd-dummy-playback";
					link->codecs->dai_name =
						"sprd-dummy-playback-dai";
				} else {
					link->codecs->name = "snd-soc-dummy";
					link->codecs->dai_name =
						"snd-soc-dummy-dai";
				}
				ret = 0;
			} else {
				printk(KERN_ERR "3[ASoC:BOARD] %s: not a need of dummy dai!\n",
				       "asoc_sprd_card_dummy_codec_sel");
				ret = -EINVAL;
			}
		} else {
			printk(KERN_ERR "3[ASoC:BOARD] %s: read 'sound-dai' as u32 failed!(%d)\n",
			       "asoc_sprd_card_dummy_codec_sel", ret);
			ret = -EINVAL;
		}
	}

	if (ret < 0)
		goto fail;

	link->num_codecs = 1;

	if (!link->cpus->dai_name || !link->codecs->dai_name) {
		dev_err(dev, "%s: xx_dai_name is NULL\n",
			"asoc_sprd_card_dai_link_of");
		ret = -EINVAL;
		goto fail;
	}

	if (plat) {
		of_parse_phandle_with_args(plat, "sound-dai",
					   "#sound-dai-cells", 0, &args);
		link->platforms->of_node = args.np;
	} else {
		link->platforms->of_node = link->cpus->of_node;
	}
	link->num_platforms = 1;

	ret = asoc_sprd_card_dai_link_of_misc(np, link, card);
	if (ret) {
		printk(KERN_INFO "6[ASoC:BOARD] %s: asoc_sprd_card_dai_link_of_misc return error.\n",
		       "asoc_sprd_card_dai_link_of");
		goto fail;
	}

	dev_info(dev, "\tname : %s\n", link->name ? link->name : "null");
	dev_info(dev, "\tstream_name : %s\n",
		 link->stream_name ? link->stream_name : "null");
	dev_dbg(dev, "\tformat : %04x\n", link->dai_fmt);
	dev_dbg(dev, "\tcpu : %s / %d\n", link->cpus->dai_name,
		priv->comps[idx].cpu.sysclk);
	dev_dbg(dev, "\tcodec : %s / %d\n", link->codecs->dai_name,
		priv->comps[idx].codec.sysclk);

	/* vendor quirk: drop the cpu dai name when no bitclock master was given */
	if (!flag)
		link->cpus->dai_name = NULL;

	ret = 0;
	goto out;
fail:
	link->num_cpus = 0;
	link->num_codecs = 0;
	link->num_platforms = 0;
	devm_kfree(dev, dai_comp);
out:
	if (cpu)
		of_node_put(cpu);
	if (plat)
		of_node_put(plat);
	if (codec)
		of_node_put(codec);
	return ret;
}

/* ---------------------------------------------------------------------- */
/* misc per link properties                                               */
/* ---------------------------------------------------------------------- */
#define SPRD_LINK_FLAG_IGNORE_PMDOWN	15
#define SPRD_LINK_FLAG_IGNORE_SUSPEND	2
#define SPRD_LINK_FLAG_DYNAMIC		7
#define SPRD_LINK_FLAG_NO_PCM		6
#define SPRD_LINK_FLAG_DPCM_PLAYBACK	11
#define SPRD_LINK_FLAG_DPCM_CAPTURE	10

/* bit field living at link+0x79 in the vendor binary */
#define SPRD_LINK_FLAGS(link)	(*(u32 *)((char *)(link) + 0x78))

static void asoc_sprd_card_link_flag(struct device_node *np,
				     struct snd_soc_dai_link *link,
				     const char *prop, int bit,
				     const char *fmt)
{
	u32 v;

	if (!of_property_read_u32(np, prop, &v)) {
		if (v)
			SPRD_LINK_FLAGS(link) |= BIT(bit);
		else
			SPRD_LINK_FLAGS(link) &= ~BIT(bit);
	} else {
		/* -EINVAL means "property missing", anything else is an error */
		SPRD_LINK_FLAGS(link) &= ~BIT(bit);
	}
	pr_debug(fmt, __func__, v);
}

static int asoc_sprd_card_read_link_flag(struct device_node *np,
					 struct snd_soc_dai_link *link,
					 const char *prop, int bit,
					 const char *dbg,
					 const char *err)
{
	u32 v;
	int ret;

	ret = of_property_read_u32(np, prop, &v);
	if (!ret) {
		if (v)
			SPRD_LINK_FLAGS(link) |= BIT(bit);
		else
			SPRD_LINK_FLAGS(link) &= ~BIT(bit);
	} else {
		if (ret == -EINVAL)
			SPRD_LINK_FLAGS(link) &= ~BIT(bit);
		else
			pr_info(err, __func__);
	}
	pr_debug(dbg, __func__, v);
	return 0;
}

static const void *asoc_sprd_card_table_lookup(struct device_node *np,
					       const char *prop,
					       const void *tbl, size_t cnt,
					       size_t size)
{
	const void *ret = NULL;
	u32 idx;
	int err;

	err = of_property_read_u32(np, prop, &idx);
	if (err) {
		if (err != -EINVAL)
			pr_info("%s", prop);
		return NULL;
	}
	if (tbl && idx < cnt)
		ret = (const char *)tbl + idx * size;
	return ret;
}

int asoc_sprd_card_dai_link_of_misc(struct device_node *np,
				    struct snd_soc_dai_link *link,
				    struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct device_node *codec = NULL;
	char name_buf[0x40];
	const void *tbl;
	u32 v[2], idx;
	int ret;

	asoc_sprd_card_read_link_flag(np, link, "ignore-pmdown-time",
				      SPRD_LINK_FLAG_IGNORE_PMDOWN,
				      "[ASoC:BOARD] %s ignore_pmdown_time: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'ignore-pmdown-time' failed.\n");
	asoc_sprd_card_read_link_flag(np, link, "ignore-suspend",
				      SPRD_LINK_FLAG_IGNORE_SUSPEND,
				      "[ASoC:BOARD] %s ignore_suspend: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'ignore-suspend' failed.\n");
	asoc_sprd_card_read_link_flag(np, link, "dynamic",
				      SPRD_LINK_FLAG_DYNAMIC,
				      "%s dynamic: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'dynamic' failed.\n");
	asoc_sprd_card_read_link_flag(np, link, "no-pcm",
				      SPRD_LINK_FLAG_NO_PCM,
				      "%s no_pcm: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'no-pcm' failed.\n");
	asoc_sprd_card_read_link_flag(np, link, "dpcm-playback",
				      SPRD_LINK_FLAG_DPCM_PLAYBACK,
				      "%s dpcm-playback: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'dpcm-playback' failed.\n");
	asoc_sprd_card_read_link_flag(np, link, "dpcm-capture",
				      SPRD_LINK_FLAG_DPCM_CAPTURE,
				      "%s dpcm-capture: %u\n",
				      "4[ASoC:BOARD] %s Read prop 'dpcm-capture' failed.\n");

	/* trigger: two u32 values, each 0..2 */
	ret = of_property_read_u32_array(np, "trigger", v, 2);
	if (!ret) {
		if (v[0] <= 2)
			link->trigger[0] = v[0];
		if (v[1] <= 2)
			link->trigger[1] = v[1];
		pr_debug("%s trigger: %u %u\n", __func__, v[0], v[1]);
	} else if (ret != -EINVAL) {
		pr_info("4[ASoC:BOARD] %s Read prop 'trigger' failed.\n",
			__func__);
	}

	/* ops */
	if (!of_property_read_u32(np, "ops", &idx)) {
		tbl = asoc_sprd_card_ops.tbl;
		link->ops = (tbl && idx < asoc_sprd_card_ops.cnt) ?
			(const struct snd_soc_ops *)(tbl + idx * 0x30) : NULL;
		pr_debug("%s ops seletct: %u\n", __func__, idx);
	} else {
		link->ops = NULL;
		pr_info("4[ASoC:BOARD] %s Read prop 'ops' failed.\n", __func__);
	}

	/* compr-ops */
	if (!of_property_read_u32(np, "compr-ops", &idx)) {
		tbl = asoc_sprd_card_compr_ops.tbl;
		link->compr_ops = (tbl && idx < asoc_sprd_card_compr_ops.cnt) ?
			(const struct snd_soc_compr_ops *)(tbl + idx * 0x20) : NULL;
		pr_debug("%s compr_ops seletct: %u\n", __func__, idx);
	} else {
		link->compr_ops = NULL;
		pr_info("4[ASoC:BOARD] %s Read prop 'compr-ops' failed.\n",
			__func__);
	}

	/* init */
	if (!of_property_read_u32(np, "init", &idx)) {
		tbl = asoc_sprd_card_init.tbl;
		link->init = (tbl && idx < asoc_sprd_card_init.cnt) ?
			((const struct snd_soc_ops *const *)tbl)[idx] : NULL;
		pr_debug("%s init seletct: %u\n", __func__, idx);
	} else {
		link->init = NULL;
		pr_info("4[ASoC:BOARD] %s Read prop 'init' failed.\n", __func__);
	}

	/* be-hw-params-fixup */
	if (!of_property_read_u32(np, "be-hw-params-fixup", &idx)) {
		tbl = asoc_sprd_card_bhpf.tbl;
		link->be_hw_params_fixup = (tbl && idx < asoc_sprd_card_bhpf.cnt) ?
			((int (*const *)(struct snd_soc_pcm_runtime *,
					 struct snd_pcm_hw_params *))tbl)[idx]
			: NULL;
		pr_debug("%s be-hw-params-fixup seletct: %u\n", __func__, idx);
	} else {
		link->be_hw_params_fixup = NULL;
		pr_info("4[ASoC:BOARD] %s Read prop 'be-hw-params-fixup' failed.\n",
			__func__);
	}

	/* link name */
	ret = of_property_read_string(np, "link-name", &link->name);
	if (ret == -EINVAL) {
		size_t len = strlen(link->cpus->dai_name) +
			     strlen(link->codecs->dai_name) + 2;
		char *p = devm_kmalloc(dev, len, GFP_KERNEL);

		if (!p) {
			printk(KERN_ERR "3[ASoC:BOARD] name devm_kzalloc failed\n");
			return -ENOMEM;
		}
		snprintf(p, len, "%s-%s", link->cpus->dai_name,
			 link->codecs->dai_name);
		link->name = p;
	} else if (ret) {
		pr_info("4[ASoC:BOARD] %s use default 'name'\n", __func__);
	}

	/* stream name */
	ret = of_property_read_string(np, "stream-name", &link->stream_name);
	if (ret) {
		size_t len = strlen(link->cpus->dai_name) +
			     strlen(link->codecs->dai_name) + 2;
		char *p = devm_kmalloc(dev, len, GFP_KERNEL);

		if (!p) {
			printk(KERN_ERR "3[ASoC:BOARD] stream_name devm_kzalloc failed\n");
			return -ENOMEM;
		}
		snprintf(p, len, "%s-%s", link->cpus->dai_name,
			 link->codecs->dai_name);
		link->stream_name = p;
		if (ret != -EINVAL)
			pr_info("4[ASoC:BOARD] %s use default 'stream-name'\n",
				__func__);
	}

	(void)name_buf;
	(void)codec;
	return 0;
}

/* ---------------------------------------------------------------------- */
/* audio debug sysfs interface (card proc entry counterpart)              */
/* ---------------------------------------------------------------------- */
static ssize_t sp_audio_debug_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%08x\n", sp_audio_debug_flag);
}

static ssize_t sp_audio_debug_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long long v;
	int ret;

	ret = kstrtoull(buf, 0, &v);
	if (ret) {
		printk(KERN_ERR "3[ASoC: COM ] ERR: %s kstrtoull failed!\n",
		       "sprd_audio_debug_store");
		return ret;
	}
	sp_audio_debug_flag = (u32)v;
	return count;
}

static struct kobject *sprd_audio_debug_kobj;
static struct kobj_attribute sp_audio_debug_attr =
	__ATTR(sp_audio_debug_flag, 0644, sp_audio_debug_show,
	       sp_audio_debug_store);

int sprd_audio_debug_init(void)
{
	int ret;

	sprd_audio_debug_kobj = kobject_create_and_add("audio_debug", kernel_kobj);
	if (!sprd_audio_debug_kobj) {
		pr_err("create sysfs failed. ret = %d\n", -ENOMEM);
		return -ENOMEM;
	}

	ret = sysfs_create_file(sprd_audio_debug_kobj,
				&sp_audio_debug_attr.attr);
	if (ret) {
		pr_err("register sysfs failed. ret = %d\n", ret);
		kobject_put(sprd_audio_debug_kobj);
		sprd_audio_debug_kobj = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sprd_audio_debug_init);

/* card proc file "asoc-sprd-debug" read/write, private_data is the flag */
ssize_t snd_pcm_sprd_debug_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	u32 *flag = file->private_data;
	char tmp[16];
	int len;

	if (!flag)
		return -EINVAL;

	len = scnprintf(tmp, sizeof(tmp), "0x%08x\n", *flag);
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

ssize_t snd_pcm_sprd_debug_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	u32 *flag = file->private_data;
	char tmp[16];
	u32 v;
	int ret;

	if (!flag)
		return -EINVAL;

	if (count >= sizeof(tmp))
		count = sizeof(tmp) - 1;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	tmp[count] = 0;

	ret = kstrtou32(tmp, 0, &v);
	if (ret)
		return ret;

	*flag = v;
	return count;
}
