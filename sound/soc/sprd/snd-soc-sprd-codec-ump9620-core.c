// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc ump9620 audio codec - ALSA SoC codec driver
 *
 * Rebuilt from the snd-soc-sprd-codec-ump9620.ko shipped in vendor_boot
 * (SPRD, kernel 5.4.210).  The vendor keeps both the codec and the headset
 * detection code in this one module; the headset half lives in
 * sprd-headset-ump9620.c.
 *
 * The codec exposes two register windows: its own analog block at
 * 0x2100..0x3290, which is reached through the parent regmap with the 0x3000
 * base stripped, and the DSP/AGC digital block at 0x1000..0x12f4, which is a
 * directly mapped window that only exists while the AGC DSP is powered.
 */
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "snd-soc-sprd-codec-ump9620.h"

/* mirror of the analog block, kept by the headset half of the module too */
static struct regmap *codec_regmap;
static u32 codec_reg_offset;
void __iomem *sprd_codec_dp_base;

/* the two offset registers used to poke the AGC AHB bridge */
static int set_agcp_ahb_offset(u32 set, u32 clr);

/* the AGC AHB bridge itself, provided by snd-soc-sprd-card.ko */
extern struct regmap *arch_audio_get_agcp_ahb_gpr(void);
extern int get_agcp_ahb_set_offset(void);
extern int get_agcp_ahb_clr_offset(void);
extern int agcp_ahb_gpr_null_check(void);
extern void arch_audio_set_agcp_ahb_gpr(struct regmap *map);
extern void arch_audio_set_aon_apb_gpr(struct regmap *map);
extern void arch_audio_set_anlg_phy_g(struct regmap *gpr);

#define CODEC_DBG(fmt, args...)						\n	pr_debug("[ASoC:ump9620] " fmt, ##args)

void sprd_codec_set_global_regmap(struct regmap *map, unsigned int offset)
{
	codec_regmap = map;
	codec_reg_offset = offset;
}

int sprd_codec_regmap_update_bits(unsigned int reg, unsigned int mask,
				  unsigned int val)
{
	if (!codec_regmap) {
		pr_err("[ASoC:ump9620] ERR: %s codec_regmap isn't initialized!\n",
		       __func__);
		return -EINVAL;
	}

	return regmap_update_bits(codec_regmap, codec_reg_offset + reg, mask, val);
}

/* same window, plain read */
int sprd_codec_regmap_read(unsigned int reg, unsigned int *val)
{
	if (!codec_regmap) {
		pr_err("[ASoC:ump9620] ERR: %s codec_regmap isn't initialized!\n",
		       __func__);
		return -EINVAL;
	}

	return regmap_read(codec_regmap, codec_reg_offset + reg, val);
}

/*
 * The ana chip id is split over two registers: 0x2004 carries the high half,
 * 0x2000 the low one.  Both are absolute addresses, the analog window offset
 * is not added here.
 */
u32 sci_get_ana_chip_id(void)
{
	unsigned int high = 0, low = 0;

	if (regmap_read(codec_regmap, 0x2004, &high)) {
		pr_err("[ASoC:HDST9620] %s: sci_adi_read reg#%d failed!\n",
		       __func__, 0x2004);
		return 0;
	}

	if (regmap_read(codec_regmap, 0x2000, &low)) {
		pr_err("[ASoC:HDST9620] %s: sci_adi_read reg#%d failed!\n",
		       __func__, 0x2000);
		return 0;
	}

	return (high << 16) | (low & 0xffff);
}

/* same window, but always program the register even when unchanged */
int sprd_codec_regmap_update_bits_force(unsigned int reg, unsigned int mask,
					unsigned int val)
{
	if (!codec_regmap) {
		pr_err("[ASoC:ump9620] ERR: %s codec_regmap isn't initialized!\n",
		       __func__);
		return -EINVAL;
	}

	return regmap_update_bits_base(codec_regmap, codec_reg_offset + reg, mask,
				       val, NULL, false, true);
}

unsigned int sprd_codec_read(struct snd_soc_component *comp, unsigned int reg)
{
	unsigned int val = 0;
	struct sprd_codec_priv *priv;

	if (reg - SPRD_CODEC_ANA_BASE <= SPRD_CODEC_ANA_END - SPRD_CODEC_ANA_BASE) {
		if (!codec_regmap || codec_reg_offset == (u32)-1)
			return -1;
		if (regmap_read(codec_regmap, reg + codec_reg_offset - SPRD_CODEC_REG_OFS, &val)) {
			pr_err("[ASoC:ump9620] %s %s: read fail, reg 0x%x\n",
			       __func__, "arch_audio_codec_read", reg);
			return -1;
		}
		return val;
	}

	if (reg - SPRD_CODEC_DIG_BASE <= SPRD_CODEC_DIG_END - SPRD_CODEC_DIG_BASE) {
		if (agdsp_access_enable())
			return -1;
		priv = dev_get_drvdata(comp->dev);
		mutex_lock(&priv->digital_enable_mutex);
		if (!priv->dig_access_cnt || priv->dig_access_en)
			arch_audio_codec_digital_reg_enable_v2(priv);
		priv->dig_access_cnt++;
		mutex_unlock(&priv->digital_enable_mutex);

		val = readl(sprd_codec_dp_base + (reg - SPRD_CODEC_DIG_BASE));

		codec_digital_reg_disable(dev_get_drvdata(comp->dev));
		agdsp_access_disable();
		return val;
	}

	if (get_sp_audio_debug_flag())
		pr_err("[ASoC:ump9620] %s %s: read, register is not codec's reg 0x%x\n",
		       __func__, "arch_audio_codec_read", reg);
	return 0;
}

int sprd_codec_write(struct snd_soc_component *comp, unsigned int reg,
		     unsigned int val)
{
	int ret = 0;
	struct sprd_codec_priv *priv;

	if (reg - SPRD_CODEC_ANA_BASE <= SPRD_CODEC_ANA_END - SPRD_CODEC_ANA_BASE) {
		if (!codec_regmap || codec_reg_offset == (u32)-1)
			return -1;
		ret = regmap_update_bits_base(codec_regmap,
					      reg + codec_reg_offset - SPRD_CODEC_REG_OFS,
					      0xffff, val, NULL, false, false);
		if (get_sp_audio_debug_flag() & 4) {
			unsigned int rd = 0;

			regmap_read(codec_regmap,
				    reg + codec_reg_offset - SPRD_CODEC_REG_OFS, &rd);
			pr_debug("[ASoC:ump9620] %s %s: write 0x%x=0x%x, read back 0x%x\n",
				 __func__, "arch_audio_codec_write", reg, val, rd);
		}
		return ret;
	}

	if (reg - SPRD_CODEC_DIG_BASE <= SPRD_CODEC_DIG_END - SPRD_CODEC_DIG_BASE) {
		if (agdsp_access_enable())
			return -1;
		priv = dev_get_drvdata(comp->dev);
		mutex_lock(&priv->digital_enable_mutex);
		if (!priv->dig_access_cnt || priv->dig_access_en)
			arch_audio_codec_digital_reg_enable_v2(priv);
		priv->dig_access_cnt++;
		mutex_unlock(&priv->digital_enable_mutex);

		writel(val, sprd_codec_dp_base + (reg - SPRD_CODEC_DIG_BASE));

		codec_digital_reg_disable(dev_get_drvdata(comp->dev));
		agdsp_access_disable();
		return 0;
	}

	if (get_sp_audio_debug_flag())
		pr_err("[ASoC:ump9620] %s %s: write, register is not codec's reg 0x%x\n",
		       __func__, "arch_audio_codec_write", reg);
	return 0;
}

/*
 * "fixed-sample-rate" pins the two DAI clocks to one of the two rates the
 * codec can run its digital filters at; anything else leaves the trio at zero
 * and the rate follows the PCM stream.
 */
static void sprd_codec_dt_parse_mach(struct platform_device *pdev)
{
	struct sprd_codec_priv *priv = dev_get_drvdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!priv || !np) {
		pr_err("[ASoC:ump9620] %s priv %p, node %p\n", __func__, priv, np);
		return;
	}

	ret = of_property_read_variable_u32_array(np, "fixed-sample-rate",
						  &priv->fixed_rate[0], 3, NULL);
	if (ret < 0) {
		pr_err("[ASoC:ump9620] %s read fixed-sample-rate fail %d\n",
		       __func__, ret);
		priv->fixed_rate[0] = 0;
		priv->fixed_rate[1] = 0;
		priv->fixed_rate[2] = 0;
		return;
	}

	pr_debug("[ASoC:ump9620] %s %d %d %d\n", __func__, priv->fixed_rate[0],
		 priv->fixed_rate[1], priv->fixed_rate[2]);
}

int sprd_codec_clk26m_sinout(struct sprd_codec_priv *priv, bool en)
{
	unsigned int val = 0;
	int ret;

	regmap_read(priv->aon_apb, priv->clk26m_sinout_reg, &val);
	ret = regmap_update_bits(priv->aon_apb, priv->clk26m_sinout_reg,
				 priv->clk26m_sinout_mask,
				 en ? priv->clk26m_sinout_mask : 0);
	regmap_read(priv->aon_apb, priv->clk26m_sinout_reg, &val);
	pr_debug("[ASoC:ump9620] %s %d, reg 0x%x val 0x%x\n", __func__, en,
		 priv->clk26m_sinout_reg, val);
	return ret;
}

static int sprd_codec_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sprd_codec_priv *priv;
	struct device_node *digital;
	struct device *digital_dev;
	struct regmap *regmap, *pmu_apb;
	u32 args[2] = { 0 };
	u32 set_ofs = 0x100, clr_ofs = 0x200;
	u32 reg = 0, lrdat_sel = 0;
	u32 chip_id = 0, chip_ver = 0, tmp = 0;
	void *efuse;
	int ret;

	pr_debug("[ASoC:ump9620] %s enter\n", __func__);

	if (!np) {
		pr_err("[ASoC:ump9620] %s no device node\n", __func__);
		return -EINVAL;
	}

	priv = devm_kmalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, priv);

	priv->aon_apb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-aon-apb");
	if (IS_ERR(priv->aon_apb)) {
		pr_err("[ASoC:ump9620] %s get aon apb fail\n", __func__);
		priv->aon_apb = NULL;
		return -ENODEV;
	}

	regmap = syscon_regmap_lookup_by_phandle_args(np, "clk26m_sinout_pmic_en",
						      2, args);
	if (IS_ERR(regmap)) {
		pr_err("[ASoC:ump9620] %s get clk26m_sinout pmic fail\n", __func__);
		return -EINVAL;
	}
	priv->clk26m_sinout_reg = args[0];
	priv->clk26m_sinout_mask = args[1];
	pr_info("[ASoC:ump9620] clk26m sinout reg 0x%x mask 0x%x\n",
		priv->clk26m_sinout_reg, priv->clk26m_sinout_mask);

	ret = sprd_codec_clk26m_sinout(priv, true);
	if (ret)
		pr_err("[ASoC:ump9620] %s clk26m sinout enable fail %d\n",
		       __func__, ret);

	if (!pdev->dev.of_node) {
		pr_err("[ASoC:ump9620] %s no node for ana\n", __func__);
		return -ENODEV;
	}
	if (!dev_get_drvdata(&pdev->dev)) {
		pr_err("[ASoC:ump9620] %s no drvdata\n", __func__);
		return -ENOMEM;
	}

	/* the analog block lives in the parent (PMIC) regmap */
	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		pr_err("[ASoC:ump9620] %s no parent regmap\n", __func__);
		ret = -ENODEV;
		goto err_clk;
	}

	if (of_property_read_u32(np, "reg", &reg)) {
		pr_err("[ASoC:ump9620] %s get reg fail\n", "sprd_codec_ana_probe");
		ret = -ENXIO;
		goto err_clk;
	}
	pr_info("[ASoC:ump9620] %s regmap %p, reg 0x%x\n",
		"sprd_codec_ana_probe", regmap, reg);

	codec_regmap = regmap;
	codec_reg_offset = reg;

	sprd_codec_dt_parse_mach(pdev);

	digital = of_parse_phandle(np, "digital-codec", 0);
	if (!digital) {
		pr_err("[ASoC:ump9620] %s get digital codec node fail\n", __func__);
		ret = -EINVAL;
		goto err_clk;
	}

	pmu_apb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-pmu-apb");
	if (IS_ERR(pmu_apb)) {
		pr_err("[ASoC:ump9620] %s get pmu apb fail\n", __func__);
		ret = -ENODEV;
		goto err_clk;
	}

	priv->agcp_apb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-agcp-apb");
	if (IS_ERR(priv->agcp_apb)) {
		pr_err("[ASoC:ump9620] %s get agcp apb fail %ld\n", __func__,
		       PTR_ERR(priv->agcp_apb));
		ret = -ENODEV;
		goto err_clk;
	}

	priv->aon_apb2 = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-aon-apb");
	if (IS_ERR(priv->aon_apb2)) {
		pr_err("[ASoC:ump9620] %s get aon apb2 fail\n", __func__);
		priv->aon_apb2 = NULL;
		ret = -ENODEV;
		goto err_clk;
	}


	priv->dvfs_apb = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-dvfs-apb");
	if (IS_ERR(priv->dvfs_apb)) {
		pr_err("[ASoC:ump9620] %s get dvfs apb fail\n", __func__);
		priv->dvfs_apb = NULL;
		ret = -ENODEV;
		goto err_clk;
	}

	mutex_init(&priv->dig_access_mutex);
	mutex_init(&priv->digital_enable_mutex);

	if (of_property_read_u32(np, "set-offset", &set_ofs)) {
		pr_debug("[ASoC:ump9620] %s no set-offset, use 0x100\n", __func__);
		set_ofs = 0x100;
	}
	if (of_property_read_u32(np, "clr-offset", &clr_ofs)) {
		pr_debug("[ASoC:ump9620] %s no clr-offset, use 0x200\n", __func__);
		clr_ofs = 0x200;
	}
	set_agcp_ahb_offset(set_ofs, clr_ofs);

	if (of_property_read_u32(np, "lrdat-sel", &lrdat_sel)) {
		pr_debug("[ASoC:ump9620] %s no lrdat-sel\n", __func__);
		lrdat_sel = 0;
	}
	priv->lrdat_sel = lrdat_sel;

	arch_audio_set_pmu_apb_gpr(pmu_apb);

	digital_dev = &of_find_device_by_node(digital)->dev;
	if (!digital_dev) {
		pr_err("[ASoC:ump9620] %s no digital codec device\n", __func__);
		ret = -ENODEV;
		goto err_clk;
	}
	dev_set_drvdata(digital_dev, priv);

	ret = sprd_codec_dig_probe(to_platform_device(digital_dev));
	if (ret < 0) {
		pr_err("[ASoC:ump9620] %s digital codec probe fail %d\n",
		       __func__, ret);
		goto err_clk;
	}

	/* ana chip id is split over two registers: 0x2004 is the high half */
	if (regmap_read(codec_regmap, 0x2004, &tmp)) {
		pr_err("[ASoC:ump9620] %s read 0x2004 fail\n", "sci_get_ana_chip_id");
		chip_id = 0;
	} else {
		chip_id = tmp << 16;
		if (regmap_read(codec_regmap, 0x2000, &tmp))
			pr_err("[ASoC:ump9620] %s read 0x2000 fail\n",
			       "sci_get_ana_chip_id");
		else
			chip_id |= tmp & 0xffff;
	}
	priv->ana_chip_id = chip_id;

	if (regmap_read(codec_regmap, 0x2000, &tmp))
		pr_err("[ASoC:ump9620] %s read 0x2000 fail\n", "sci_get_ana_chip_ver");
	else
		chip_ver = tmp & 0xffff;
	priv->ana_chip_ver = chip_ver;
	pr_info("[ASoC:ump9620] ana chip id 0x%x, version 0x%x\n", chip_id, chip_ver);

	ret = snd_soc_register_component(&pdev->dev, &soc_codec_dev_sprd_codec,
					 sprd_codec_dai, 2);
	if (ret) {
		pr_err("[ASoC:ump9620] %s register component fail %d\n", __func__, ret);
		goto err_dig;
	}

	priv->ana_state = (priv->ana_state & 0xf8) | 2;
	priv->ana_power = 1;

	if (!priv->headmicbias) {
		priv->headmicbias = regulator_get(&pdev->dev, "HEADMICBIAS");
		if (IS_ERR(priv->headmicbias)) {
			pr_err("[ASoC:ump9620] %s get regulator %s fail\n",
			       __func__, "HEADMICBIAS");
			priv->headmicbias = NULL;
		}
	}
	if (!priv->vb) {
		priv->vb = regulator_get(&pdev->dev, "VB");
		if (IS_ERR(priv->vb)) {
			pr_err("[ASoC:ump9620] %s get regulator %s fail\n", __func__, "VB");
			priv->vb = NULL;
		}
	}
	if (priv->vb) {
		if (!regulator_enable(priv->vb)) {
			regulator_set_mode(priv->vb, 8);
			regulator_disable(priv->vb);
		}
	}
	if (!priv->dahpl_chn) {
		priv->dahpl_chn = regulator_get(&pdev->dev, "DAHPL_CHN");
		if (IS_ERR(priv->dahpl_chn)) {
			pr_err("[ASoC:ump9620] %s get regulator %s fail\n",
			       __func__, "DAHPL_CHN");
			priv->dahpl_chn = NULL;
		}
	}
	if (!priv->bias) {
		priv->bias = regulator_get(&pdev->dev, "BIAS");
		if (IS_ERR(priv->bias)) {
			pr_err("[ASoC:ump9620] %s get regulator %s fail\n", __func__, "BIAS");
			priv->bias = NULL;
		}
	}

	cp_short_check(priv);

	priv->headmicbias_volt = 0x3200000ce4ULL;
	priv->vb_volt = 0x3200000cb2ULL;

	priv->neg_cp_efuse = 0xe1;
	efuse = nvmem_cell_get(&pdev->dev, "neg_cp_efuse");
	if (!IS_ERR(efuse)) {
		void *buf = nvmem_cell_read(efuse, NULL);
		size_t len = 0;

		nvmem_cell_put(efuse);
		if (IS_ERR(buf)) {
			priv->neg_cp_efuse = 0xe1;
		} else {
			len = min_t(size_t, 4, 4);
			memcpy(&priv->neg_cp_efuse, buf, len);
			kfree(buf);
		}
	}

	priv->fgu_4p2_efuse = 0;
	efuse = nvmem_cell_get(&pdev->dev, "fgu_4p2_efuse");
	if (!IS_ERR(efuse)) {
		void *buf = nvmem_cell_read(efuse, NULL);
		size_t len = 0;

		nvmem_cell_put(efuse);
		if (IS_ERR(buf))
			priv->fgu_4p2_efuse = 0;
		else {
			len = min_t(size_t, 4, 4);
			memcpy(&priv->fgu_4p2_efuse, buf, len);
			kfree(buf);
		}
	}

	pr_debug("[ASoC:ump9620] %s neg_cp_efuse 0x%x, fgu_4p2_efuse 0x%x\n",
		 __func__, priv->neg_cp_efuse, priv->fgu_4p2_efuse);

	ret = sprd_codec_clk26m_sinout(priv, false);
	if (ret)
		pr_err("[ASoC:ump9620] %s clk26m sinout disable fail %d\n", __func__, ret);

	return 0;

err_dig:
	sprd_codec_dig_remove(priv);
err_clk:
	sprd_codec_clk26m_sinout(priv, false);
	pr_err("[ASoC:ump9620] %s fail %d\n", __func__, ret);
	return ret;
}

static int sprd_codec_remove(struct platform_device *pdev)
{
	struct sprd_codec_priv *priv = dev_get_drvdata(&pdev->dev);

	if (priv->headmicbias) {
		regulator_set_mode(priv->headmicbias, 2);
		regulator_disable(priv->headmicbias);
		regulator_put(priv->headmicbias);
		priv->headmicbias = NULL;
	}
	if (priv->vb) {
		regulator_set_mode(priv->vb, 2);
		regulator_disable(priv->vb);
		regulator_put(priv->vb);
		priv->vb = NULL;
	}
	if (priv->dahpl_chn) {
		regulator_set_mode(priv->dahpl_chn, 2);
		regulator_disable(priv->dahpl_chn);
		regulator_put(priv->dahpl_chn);
		priv->dahpl_chn = NULL;
	}
	if (priv->bias) {
		regulator_set_mode(priv->bias, 2);
		regulator_disable(priv->bias);
		regulator_put(priv->bias);
		priv->bias = NULL;
	}
	/* the PMIC half keeps a fifth supply, released here as well */
	if (priv->ana_regulator) {
		regulator_set_mode(priv->ana_regulator, 2);
		regulator_disable(priv->ana_regulator);
		regulator_put(priv->ana_regulator);
		priv->ana_regulator = NULL;
	}

	snd_soc_unregister_component(&pdev->dev);
	return 0;
}


/*
 * The interrupt controller is a single register pair: bit 0 of @enable
 * selects "unmask", anything else masks the @mask lines.
 */
static void sprd_codec_intc_enable(int enable, u32 mask)
{
	regmap_update_bits_base(codec_regmap, codec_reg_offset + 0x280,
				mask, (enable & 0x1) ? mask : 0,
				NULL, false, false);
}

/*
 * Log and settle the codec interrupt block after an EIC was shut down or
 * re-armed: only the second case has to give the analog bias time to settle.
 *
 * @comp is unused in the vendor build, the module only passes the shadow of
 * the pending lines.
 */
void sprd_codec_intc_irq(struct snd_soc_component *comp, u32 int_shadow)
{
	if (int_shadow & 0xf00) {
		if (get_sp_audio_debug_flag() & 1)
			pr_info("[ASoC:ump9620] IRQ shut down int_shadow 0x%x\n",
				int_shadow);
	} else {
		if (int_shadow & 0x30fc) {
			if (get_sp_audio_debug_flag() & 1)
				pr_info("[ASoC:ump9620] IRQ clear and enable, int_shadow 0x%x\n",
					int_shadow);
		}

		sprd_codec_wait(250);
	}
}

static int sprd_codec_pcm_hw_startup(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	struct sprd_headset *hdst = sprd_hdst;

	if (!priv->startup_cnt) {
		mutex_lock(&hdst->audio_on_lock);
		hdst->hmicbias_polling = 1;
		mutex_unlock(&hdst->audio_on_lock);

		sprd_enable_hmicbias_polling(0, 0);
	}

	priv->startup_cnt++;

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s, startup_cnt=%d\n", __func__, priv->startup_cnt);

	return 0;
}

static void sprd_codec_pcm_hw_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	struct sprd_headset *hdst = sprd_hdst;

	if (priv->startup_cnt) {
		priv->startup_cnt--;
		if (priv->startup_cnt)
			goto done;
	}

	mutex_lock(&hdst->audio_on_lock);
	hdst->hmicbias_polling = 0;
	mutex_unlock(&hdst->audio_on_lock);

	sprd_enable_hmicbias_polling(1, 0);

done:
	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s, startup_cnt=%d\n", __func__, priv->startup_cnt);
}

static int sprd_codec_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	unsigned int rate = params_rate(params);

	if (substream->stream) {
		priv->adc_lrclk_sel = priv->fixed_rate[1] ? : rate;
		sprd_codec_set_ad_sample_rate(comp, priv->adc_lrclk_sel, 0);

		if (get_sp_audio_debug_flag() & 1)
			pr_info("[ASoC:ump9620] Capture rate is [%u]\n",
				priv->adc_lrclk_sel);

		priv->adc1_lrclk_sel = priv->fixed_rate[2] ? : rate;
		sprd_codec_set_ad_sample_rate(comp, priv->adc1_lrclk_sel, 4);

		if (get_sp_audio_debug_flag() & 1)
			pr_info("[ASoC:ump9620] Capture(ADC1) rate is [%u]\n",
				priv->adc1_lrclk_sel);

		return 0;
	}

	priv->dac_lrclk_sel = priv->fixed_rate[0] ? : rate;
	sprd_codec_set_sample_rate(comp, priv->dac_lrclk_sel);

	if (get_sp_audio_debug_flag() & 1)
		pr_info("[ASoC:ump9620] Playback rate is [%u], ivsence_dmic_type %d\n",
			priv->dac_lrclk_sel, priv->ivsence_dmic);

	switch (priv->ivsence_dmic) {
	case 1:
		priv->adc_lrclk_sel = priv->fixed_rate[1] ? : rate;
		sprd_codec_set_ad_sample_rate(comp, priv->adc_lrclk_sel, 0);
		break;
	case 2:
		priv->adc1_lrclk_sel = priv->fixed_rate[2] ? : rate;
		sprd_codec_set_ad_sample_rate(comp, priv->adc1_lrclk_sel, 4);
		break;
	default:
		if (get_sp_audio_debug_flag() & 1)
			CODEC_DBG("%s to do nothing\n", __func__);
		break;
	}

	return 0;
}

static int sprd_codec_pcm_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	return 0;
}

/*
 * The capture rate can be forced from DT for the platforms whose modem keeps
 * a fixed AD rate.
 */
static void codec_reconfig_dai_rate(struct snd_soc_component *comp)
{
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	struct device *dev = comp->dev;
	unsigned int rate;

	if (!dev->of_node)
		return;

	if (of_property_read_u32(dev->of_node, "sprd,replace-rate", &rate) ||
	    !rate)
		return;

	if (priv->fixed_rate[1] || priv->fixed_rate[2]) {
		dev_warn(dev, "%s, both replacingrate and fixed rate are set for adc\n",
			 __func__);
		return;
	}

	if (rate != 48000 && rate != 32000)
		return;

	priv->ad_rate_fixup = rate;
	dev_info(dev, "%s, use rate(%u) for the unsupported rate capture\n",
		 __func__, rate);
}

static int sprd_codec_soc_probe(struct snd_soc_component *comp)
{
	struct sprd_codec_priv *priv = snd_soc_component_get_drvdata(comp);
	struct snd_info_entry *entry;

	if (!priv) {
		pr_err("[ASoC:ump9620] %s sprd_codec NULL error\n", __func__);
		return -EINVAL;
	}

	if (get_sp_audio_debug_flag() & 1)
		CODEC_DBG("%s\n", __func__);

	codec_reconfig_dai_rate(comp);

	comp->name = "sprd-codec";
	sprd_hdst->comp2 = comp;

	entry = snd_info_create_card_entry(comp->card, "sprd-codec",
					   comp->dev);
	if (entry) {
		entry->private_data = priv;
		entry->c.text.read = sprd_codec_proc_read;
	}

	entry = snd_info_create_card_entry(comp->card, "aud-glb", comp->dev);
	if (entry) {
		entry->private_data = priv;
		entry->c.text.read = aud_glb_reg_read;
	}

	entry = snd_info_create_card_entry(comp->card, "short-check-stat",
					   comp->dev);
	if (entry) {
		entry->private_data = priv;
		entry->c.text.read = pa_short_stat_proc_read;
	}

	snd_soc_dapm_ignore_suspend(&comp->dapm, "Normal-Playback");
	snd_soc_dapm_ignore_suspend(&comp->dapm, "Normal-Capture-AP01");
	snd_soc_dapm_ignore_suspend(&comp->dapm, "Virt Output Pin");

	ret = sprd_headset_soc_probe(comp);
	if (ret == -EPROBE_DEFER) {
		if (!(get_sp_audio_debug_flag() & 1))
			return -EPROBE_DEFER;

		pr_info("[ASoC:ump9620] The headset is not ready now\n");
		return -EPROBE_DEFER;
	}

	snd_soc_component_update_bits(comp, 0x3054, 0x100, 0x100);

	return 0;
}

static int sprd_codec_soc_remove(struct snd_soc_component *comp)
{
	if (sprd_hdst) {
		if (sprd_hdst->extcon_notifier_registered)
			extcon_unregister_notifier(sprd_hdst->extcon, 0x15,
						   &sprd_hdst->extcon_nb);

		sprd_headset_power_deinit(sprd_hdst);
	}

	return 0;
}

/* dump the digital and analog halves of the codec register space */
static int sprd_codec_proc_read(struct seq_file *m, void *v)
{
	struct sprd_codec_priv *priv = m->private;
	struct snd_soc_component *comp = priv->comp;
	unsigned int i, r0, r1, r2, r3;

	if (agdsp_access_enable())
		return 0;

	seq_printf(m, "%s digital part\n", comp->name ? comp->name : "codec");

	for (i = 0x1000; i <= 0x12e5; i += 0x10) {
		r0 = snd_soc_component_read32(comp, i);
		r1 = snd_soc_component_read32(comp, i + 4);
		r2 = snd_soc_component_read32(comp, i + 8);
		r3 = snd_soc_component_read32(comp, i + 0xc);
		seq_printf(m, "0x%04x | 0x%04x 0x%04x 0x%04x 0x%04x\n",
			   i & 0xffff, r0, r1, r2, r3);
	}

	agdsp_access_disable();

	seq_printf(m, "%s analog part\n", comp->name ? comp->name : "codec");

	for (i = 0x3000; i <= 0x3150; i += 0x10) {
		r0 = snd_soc_component_read32(comp, i);
		r1 = snd_soc_component_read32(comp, i + 4);
		r2 = snd_soc_component_read32(comp, i + 8);
		r3 = snd_soc_component_read32(comp, i + 0xc);
		seq_printf(m, "0x%04x | 0x%04x 0x%04x 0x%04x 0x%04x\n",
			   i & 0xffff, r0, r1, r2, r3);
	}

	return 0;
}

/* audio global register dump */
static const char * const aud_glb_reg_name[] = {
	"aud_glb",	"aud_top",	"aud_clk",	"aud_rst",
	"aud_int",	"aud_int_en",	"aud_dma",	"aud_pcm",
	"aud_adc",	"aud_dac",	"aud_mix",	"aud_pga",
	"aud_vbc",	"aud_iis0",	"aud_iis1",	"aud_iis2",
	"aud_iis3",	"aud_spdif",	"aud_dsi",	"aud_dsi1",
	"aud_fm",	"aud_src",	"aud_agcp",	"aud_agcp1",
	"aud_agcp2",	"aud_agcp3",	"aud_aon",	"aud_aon1",
	"aud_aon2",	"aud_aon3",
};

static int aud_glb_reg_read(struct seq_file *m, void *v)
{
	struct sprd_codec_priv *priv = m->private;
	unsigned int val;
	int i;

	seq_printf(m, "audio global register dump\n");

	for (i = 0; i < ARRAY_SIZE(aud_glb_reg_name); i++) {
		val = 0;
		regmap_read(priv->agcp_apb, i * 4, &val);
		seq_printf(m, "%27s+%2d: %#08lx %#08x\n", aud_glb_reg_name[i],
			   i, (unsigned long)(i * 4), val);
	}

	(void)v;

	return 0;
}

static int pa_short_stat_proc_read(struct seq_file *m, void *v)
{
	struct sprd_codec_priv *priv = m->private;

	seq_printf(m, "CP short stat=%d\nPA short stat=%d\n",
		   priv->cp_short_stat, priv->pa_short_stat);
	(void)v;

	return 0;
}

/*
 * The digital codec node carries its own compatible string, which is all the
 * codec needs to know which SoC family it runs on: the agcp variant routes
 * everything through the AGC DSP bridge, the aon variant has a plain irq.
 */
static const char *sprd_codec_get_soc_type(struct device_node *np)
{
	const char *type = NULL;

	if (of_property_read_string(np, "compatible", &type)) {
		pr_err("[ASoC:ump9620] %s node '%s' has no compatible prop?\n",
		       __func__, np->name);
		return NULL;
	}

	return type;
}

/*
 * Probe the digital half of the codec: look up the syscon that guards its
 * register window, take the DSP irq when the SoC has one, set the default
 * playback rate and map the DSP register page.
 */
int sprd_codec_dig_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sprd_codec_priv *priv = dev_get_platdata(&pdev->dev);
	struct resource *res;
	struct regmap *gpr;
	const char *soc_type;
	unsigned int fs = 0;
	int irq = 0;
	int ret;

	if (!np) {
		pr_err("[ASoC:ump9620] %s np NULL error\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!priv) {
		pr_err("[ASoC:ump9620] %s sprd_codec_priv NULL error\n",
		       __func__);
		return -ENOMEM;
	}

	soc_type = sprd_codec_get_soc_type(np);
	if (!soc_type) {
		pr_err("[ASoC:ump9620] %s error, unknown soc type %d",
		       __func__, -ENODEV);
		return -EINVAL;
	}

	if (strstr(soc_type, "agcp")) {
		gpr = syscon_regmap_lookup_by_phandle(np,
						      "sprd,syscon-agcp-ahb");
		if (IS_ERR(gpr)) {
			pr_err("[ASoC:ump9620] %s Get the codec aon apb syscon failed %ld\n",
			       __func__, PTR_ERR(gpr));
			return -EPROBE_DEFER;
		}
		arch_audio_set_agcp_ahb_gpr(gpr);
	} else {
		gpr = syscon_regmap_lookup_by_phandle(np,
						      "sprd,syscon-aon-apb");
		if (IS_ERR(gpr)) {
			pr_err("[ASoC:ump9620] %s Get the codec aon apb syscon failed %ld\n",
			       __func__, PTR_ERR(gpr));
			return -EPROBE_DEFER;
		}
		arch_audio_set_aon_apb_gpr(gpr);

		irq = platform_get_irq(pdev, 0);
		if (!irq) {
			pr_err("[ASoC:ump9620] Must give me the DP IRQ, error\n");
			return -EINVAL;
		}
		priv->irq = irq;

		if (get_sp_audio_debug_flag() & 1)
			CODEC_DBG("%s Set DP IRQ is %u\n", __func__, irq);

		ret = devm_request_threaded_irq(&pdev->dev, priv->irq,
						sprd_codec_dp_irq, NULL, 0,
						"sprd_codec_dp", priv);
		if (ret) {
			pr_err("[ASoC:ump9620] Request irq dp failed\n");
			return ret;
		}

		priv->da_rate = 44100;
		if (!of_property_read_variable_u32_array(np, "sprd,def_da_fs",
							 &fs, 1, 0)) {
			priv->da_rate = fs;

			if (get_sp_audio_debug_flag() & 1)
				CODEC_DBG("%s Change DA default fs to %u\n",
					  __func__, fs);
		}
	}

	gpr = syscon_regmap_lookup_by_phandle(np, "sprd,anlg-phy-g-syscon");
	if (IS_ERR(gpr))
		dev_warn(&pdev->dev,
			 "Get the sprd,anlg-phy-g-syscon failed %ld\n",
			 PTR_ERR(gpr));
	else
		arch_audio_set_anlg_phy_g(gpr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("[ASoC:ump9620] error, Must give me the codec DP reg address\n");
		return -EINVAL;
	}

	sprd_codec_dp_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sprd_codec_dp_base)) {
		pr_err("[ASoC:ump9620] error, cannot create iomap address for codec DP\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * A shorted charge pump or a shorted PA is detected by the codec itself:
 * both rails are powered up for the test, the BG divider (0x301c bits 8..10)
 * and the BIAS current (0x3020 bits 0..3) are walked through six steps and
 * bit 4 of 0x3198 says whether the short is gone.  The step that cleared it
 * is kept in priv->cp_short_stat for the proc entry.
 */
int cp_short_check(struct sprd_codec_priv *priv)
{
	const char *name[2] = { "BG", "BIAS" };
	struct snd_soc_component *comp;
	struct regulator *reg[2];
	int mode[2];
	int i, ret, nregs;

	if (!priv || !priv->comp)
		return 0;

	comp = priv->comp;

	reg[0] = regulator_get(comp->dev, name[0]);
	if (IS_ERR(reg[0])) {
		pr_err("[ASoC:ump9620] error, Failed to request %ld %s\n",
		       PTR_ERR(reg[0]), name[0]);
		return 0;
	}

	ret = regulator_enable(reg[0]);
	if (ret) {
		pr_err("[ASoC:ump9620] %s, regulator_enable failed", __func__);
		regulator_put(reg[0]);
		return 0;
	}

	mode[0] = regulator_get_mode(reg[0]);
	regulator_set_mode(reg[0], 2);
	nregs = 1;

	reg[1] = regulator_get(comp->dev, name[1]);
	if (IS_ERR(reg[1])) {
		pr_err("[ASoC:ump9620] error, Failed to request %ld %s\n",
		       PTR_ERR(reg[1]), name[1]);
		goto out;
	}

	ret = regulator_enable(reg[1]);
	if (ret) {
		pr_err("[ASoC:ump9620] %s, regulator_enable failed", __func__);
		regulator_put(reg[1]);
		goto out;
	}

	mode[1] = regulator_get_mode(reg[1]);
	regulator_set_mode(reg[1], 2);
	nregs = 2;

	usleep_range(100, 150);

	snd_soc_component_update_bits(comp, 0x301c, 0x40, 0x40);
	usleep_range(50, 100);

	for (i = 0; i < ARRAY_SIZE(cp_short_bg); i++) {
		snd_soc_component_update_bits(comp, 0x301c, 0x700,
					      cp_short_bg[i]);
		snd_soc_component_update_bits(comp, 0x3020, 0xf,
					      cp_short_bias[i]);

		sprd_codec_wait(20);

		if (snd_soc_component_read32(comp, 0x3198) & 0x10) {
			priv->cp_short_stat = i + 1;
			break;
		}
	}

	/* leave both dividers at their reset value */
	snd_soc_component_update_bits(comp, 0x301c, 0x700, 0x700);
	snd_soc_component_update_bits(comp, 0x3020, 0xf, 0);

out:
	/* restore the rails in the opposite order, BIAS first */
	for (i = nregs - 1; i >= 0; i--) {
		regulator_set_mode(reg[i], mode[i]);
		regulator_disable(reg[i]);
		regulator_put(reg[i]);
	}

	return 0;
}

/*
 * The digital half of the codec sits behind the AGC DSP AHB bridge: it can
 * only be touched while the DSP is awake, and the bridge has to be told about
 * the module before its registers answer.  The reference count that decides
 * when the block may really be switched off lives in the caller.
 */
void arch_audio_codec_digital_reg_enable_v2(struct sprd_codec_priv *priv)
{
	struct regmap *gpr;
	unsigned int val = 0;
	int ret;

	if (agcp_ahb_gpr_null_check()) {
		pr_err("[ASoC:ump9620] %s agcp_ahb_gpr_null_check failed!",
		       __func__);
		return;
	}

	if (agdsp_access_enable()) {
		pr_err("[ASoC:ump9620] %s, agdsp_access_enable failed!\n",
		       __func__);
		return;
	}

	gpr = arch_audio_get_agcp_ahb_gpr();
	ret = regmap_write(gpr, get_agcp_ahb_set_offset(), 0x200000);
	if (ret >= 0) {
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_set_offset(), 0x400000);
	}

	regmap_read(arch_audio_get_agcp_ahb_gpr(), 0, &val);

	CODEC_DBG("%s AHB_MODULE_EB0_STS 0x%x\n", __func__, val);

	agdsp_access_disable();
}

/* the mirror image: drop both module bits of the bridge again */
static void arch_audio_codec_digital_reg_disable_v2(void)
{
	struct regmap *gpr;
	int ret;

	if (agdsp_access_enable()) {
		pr_err("[ASoC:ump9620] %s, agdsp_access_enable failed!\n",
		       __func__);
		return;
	}

	gpr = arch_audio_get_agcp_ahb_gpr();
	ret = regmap_write(gpr, get_agcp_ahb_clr_offset(), 0x400000);
	if (ret >= 0) {
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_clr_offset(), 0x200000);
	}

	agdsp_access_disable();
}

/*
 * Drop one reference on the digital block; the last user switches it off.
 * The vendor counts references even when the block was never enabled, so a
 * count of zero simply goes straight to the power down path.
 */
void codec_digital_reg_disable(struct sprd_codec_priv *priv)
{
	mutex_lock(&priv->digital_enable_mutex);

	if (priv->dig_access_cnt) {
		priv->dig_access_cnt--;
		if (priv->dig_access_cnt)
			goto out;
	}

	arch_audio_codec_digital_reg_disable_v2();

out:
	mutex_unlock(&priv->digital_enable_mutex);
}

static const struct of_device_id codec_of_match[] = {
	{ .compatible = "unisoc,ump9620-audio-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, codec_of_match);

static struct platform_driver sprd_codec_driver = {
	.probe = sprd_codec_probe,
	.remove = sprd_codec_remove,
	.driver = {
		.name = "sprd-codec",
		.of_match_table = codec_of_match,
		.owner = THIS_MODULE,
	},
};

static int __init sprd_codec_driver_init(void)
{
	return platform_driver_register(&sprd_codec_driver);
}

static void __exit sprd_codec_driver_exit(void)
{
	platform_driver_unregister(&sprd_codec_driver);
}

module_init(sprd_codec_driver_init);
module_exit(sprd_codec_driver_exit);

MODULE_DESCRIPTION("SPRD-CODEC ALSA SoC codec driver");
MODULE_LICENSE("GPL");
