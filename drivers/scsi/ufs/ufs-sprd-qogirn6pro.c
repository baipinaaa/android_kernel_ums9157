/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/time.h>
#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
#include <linux/sprd_sip_svc.h>
#endif
#include <linux/nvmem-consumer.h>
#include <linux/rpmb.h>
#include <linux/reset.h>
#include "ufshcd.h"
#include "ufs.h"
#include "ufshcd-pltfrm.h"
#include "ufshci.h"
#include "ufs-sprd-qogirn6pro.h"
#include "ufs_quirks.h"
#include "unipro.h"

/**
 * ufs_sprd_rmwl - read modify write into a register
 * @base - base address
 * @mask - mask to apply on read value
 * @val - actual value to write
 * @reg - register address
 */
static inline void ufs_sprd_rmwl(void __iomem *base, u32 mask, u32 val, u32 reg)
{
	u32 tmp;

	tmp = readl((base) + (reg));
	tmp &= ~mask;
	tmp |= (val & mask);
	writel(tmp, (base) + (reg));
}

static int ufs_efuse_calib_data(struct platform_device *pdev,
				const char *cell_name)
{
	struct nvmem_cell *cell;
	void *buf;
	u32 calib_data;
	size_t len;

	if (!pdev)
		return -EINVAL;

	cell = nvmem_cell_get(&pdev->dev, cell_name);
	if (IS_ERR_OR_NULL(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR_OR_NULL(buf)) {
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}

	memcpy(&calib_data, buf, min(len, sizeof(u32)));

	kfree(buf);
	nvmem_cell_put(cell);
	return calib_data;
}

int ufs_sprd_reset(struct ufs_sprd_host *host)
{
	int ret;
	dev_info(host->hba->dev, "ufs hardware reset!\n");

	regmap_update_bits(host->phy_sram_ext_ld_done.regmap,
			   host->phy_sram_ext_ld_done.reg,
			   host->phy_sram_ext_ld_done.mask,
			   host->phy_sram_ext_ld_done.mask);

	regmap_update_bits(host->phy_sram_bypass.regmap,
			   host->phy_sram_bypass.reg,
			   host->phy_sram_bypass.mask,
			   host->phy_sram_bypass.mask);

	ret = reset_control_assert(host->aon_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufsdev_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	ret = reset_control_assert(host->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	usleep_range(1000, 1100);

	ret = reset_control_deassert(host->aon_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufsdev_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	ret = reset_control_deassert(host->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

out:
	return ret;
}

static int ufs_sprd_get_syscon_reg(struct device_node *np,
				   struct syscon_ufs *reg, const char *name)
{
	struct regmap *regmap;
	u32 syscon_args[2];

	regmap = syscon_regmap_lookup_by_phandle_args(np, name, 2, syscon_args);
	if (IS_ERR(regmap)) {
		pr_err("read ufs syscon %s regmap fail\n", name);
		reg->regmap = NULL;
		reg->reg = 0x0;
		reg->mask = 0x0;
		return -EINVAL;
	}
	reg->regmap = regmap;
	reg->reg = syscon_args[0];
	reg->mask = syscon_args[1];

	return 0;
}

void read_ufs_debug_bus(struct ufs_hba *hba)
{
	u32 sigsel, debugbus_data;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (!host->syssel_reg) {
		dev_warn(hba->dev, "can't get ufs debug bus base.\n");
		return;
	}

	/* read aon ufs mphy debugbus */
	writel(0x6, host->syssel_reg);
	writel(0xD, host->syssel_reg + 0xc);
	dev_err(hba->dev, "aon ufs mphy debugbus_data as follow(syssel:0x6, mod_reg:0xD):\n");
	for (sigsel = 0x1; sigsel <= 0x8; sigsel++) {
		writel(sigsel, host->syssel_reg + 0x10);
		debugbus_data = readl(host->syssel_reg + 0x208);
		dev_err(hba->dev, "sig_sel: 0x%x. debugbus_data: 0x%x\n", sigsel, debugbus_data);
	}
	dev_err(hba->dev, "aon ufs mphy debugbus_data end.\n");

	/* read ap ufshcd debugbus */
	writel(0x0, host->syssel_reg);
	writel(0x0, host->syssel_reg + 0xc);
	dev_err(hba->dev, "ap ufshcd debugbus_data as follow(syssel:0x0, mod_reg:0x0):\n");
	for (sigsel = 0x10; sigsel <= 0x12; sigsel++) {
		writel(sigsel, host->syssel_reg + 0x10);
		debugbus_data = readl(host->syssel_reg + 0x208);
		dev_err(hba->dev, "sig_sel: 0x%x. debugbus_data: 0x%x\n", sigsel, debugbus_data);
	}
	for (sigsel = 0x16; sigsel <= 0x18; sigsel++) {
		writel(sigsel, host->syssel_reg + 0x10);
		debugbus_data = readl(host->syssel_reg + 0x208);
		dev_err(hba->dev, "sig_sel: 0x%x. debugbus_data: 0x%x\n", sigsel, debugbus_data);
	}
	dev_err(hba->dev, "ap ufshcd debugbus_data end.\n");
}

/**
 * ufs_sprd_init - find other essential mmio bases
 * @hba: host controller instance
 * Returns 0 on success, non-zero value on failure
 */
static int ufs_sprd_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_sprd_host *host;
	int ret = 0;

#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
	struct sprd_sip_svc_handle *svc_handle;
#endif

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	host->ufs_lane_calib_data1 = ufs_efuse_calib_data(pdev,
							  "ufs_cali_lane1");
	if (host->ufs_lane_calib_data1 == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"%s:get ufs_lane_calib_data1 failed!\n", __func__);
		ret =  -EPROBE_DEFER;
		goto out_variant_clear;
	}

	dev_err(&pdev->dev, "%s: ufs_lane_calib_data1: %x\n",
		__func__, host->ufs_lane_calib_data1);

	host->ufs_lane_calib_data0 = ufs_efuse_calib_data(pdev,
							  "ufs_cali_lane0");
	if (host->ufs_lane_calib_data0 == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"%s:get ufs_lane_calib_data1 failed!\n", __func__);
		ret =  -EPROBE_DEFER;
		goto out_variant_clear;
	}

	dev_err(&pdev->dev, "%s: ufs_lane_calib_data0: %x\n",
		__func__, host->ufs_lane_calib_data0);

	host->vdd_mphy = devm_regulator_get(dev, "vdd-mphy");
	ret = regulator_enable(host->vdd_mphy);
	if (ret)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &host->phy_sram_ext_ld_done,
				      "phy_sram_ext_ld_done");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &host->phy_sram_bypass,
				      "phy_sram_bypass");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &host->phy_sram_init_done,
				      "phy_sram_init_done");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &host->aon_apb_ufs_clk_en,
				      "aon_apb_ufs_clk_en");
	if (ret < 0)
		 return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &host->ufsdev_refclk_en,
				      "ufsdev_refclk_en");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node,
					&host->usb31pllv_ref2mphy_en,
				      "usb31pllv_ref2mphy_en");
	if (ret < 0)
		return -ENODEV;

	host->hclk = devm_clk_get(&pdev->dev, "ufs_hclk");
	if (IS_ERR(host->hclk)) {
		dev_warn(&pdev->dev,
			 "can't get the clock dts config: ufs_pclk\n");
			 host->hclk = NULL;
	}

	host->hclk_source = devm_clk_get(&pdev->dev, "ufs_hclk_source");
	if (IS_ERR(host->hclk_source)) {
		dev_warn(&pdev->dev,
			 "can't get the clock dts config: ufs_hclk_source\n");
			 host->hclk_source = NULL;
	}

	clk_set_parent(host->hclk, host->hclk_source);

	host->rco_100M = devm_clk_get(&pdev->dev, "ufs_rco_100M");
	if (IS_ERR(host->rco_100M)) {
		dev_warn(&pdev->dev,
			 "can't get the clock dts config: rco_100M\n");
			 host->rco_100M = NULL;
	}

	host->aon_apb_ufs_rst = devm_reset_control_get(dev, "ufsdev_soft_rst");
	if (IS_ERR(host->aon_apb_ufs_rst)) {
		dev_err(dev, "%s get ufsdev_soft_rst failed, err%ld\n",
			__func__, PTR_ERR(host->aon_apb_ufs_rst));
		host->aon_apb_ufs_rst = NULL;
		return -ENODEV;
	}

	host->ap_ahb_ufs_rst = devm_reset_control_get(dev, "ufs_soft_rst");
	if (IS_ERR(host->ap_ahb_ufs_rst)) {
		dev_err(dev, "%s get ufs_soft_rst failed, err%ld\n",
			__func__, PTR_ERR(host->ap_ahb_ufs_rst));
		host->ap_ahb_ufs_rst = NULL;
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
	ret = reset_control_assert(host->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		return -ENODEV;
	}

	usleep_range(1000, 1100);

	ret = reset_control_deassert(host->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		return -ENODEV;
	}

	ufshcd_writel(hba, CONTROLLER_ENABLE, REG_CONTROLLER_ENABLE);
	if ((ufshcd_readl(hba, REG_UFS_CCAP) & (1 << 27)))
		ufshcd_writel(hba, (CRYPTO_GENERAL_ENABLE | CONTROLLER_ENABLE),
			      REG_CONTROLLER_ENABLE);
	svc_handle = sprd_sip_svc_get_handle();
	if (!svc_handle) {
		pr_err("%s: failed to get svc handle\n", __func__);
		return -ENODEV;
	}

	ret = svc_handle->storage_ops.ufs_crypto_enable();
	pr_err("smc: enable cfg, ret:0x%x", ret);
#endif

	hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION |
		       UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS;

	hba->caps |= UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_CRYPTO | UFSHCD_CAP_WB_EN;
#ifdef CONFIG_SCSI_UFS_HPB
	hba->quirks |= UFSHCD_QUIRK_BROKEN_HPB_READ_CMD;
#endif

	host->syssel_reg = devm_ioremap(dev, REG_DEBUG_BUS_SYSSEL, 0x210);
	if (IS_ERR(host->syssel_reg)) {
		pr_err("error to ioremap ufs debug bus base.\n");
		host->syssel_reg = NULL;
	}

	return 0;

out_variant_clear:
	return ret;
}

/**
 * ufs_sprd_hw_init - controller enable and reset
 * @hba: host controller instance
 */
int ufs_sprd_hw_init(struct ufs_hba *hba)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	return ufs_sprd_reset(host);
}

static void ufs_sprd_exit(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	regmap_update_bits(host->aon_apb_ufs_clk_en.regmap,
			   host->aon_apb_ufs_clk_en.reg,
			   host->aon_apb_ufs_clk_en.mask,
			   0);

	ret = regulator_disable(host->vdd_mphy);
	if (ret)
		pr_err("disable vdd_mphy failed ret =0x%x!\n", ret);

	devm_kfree(dev, host);
	hba->priv = NULL;
}

static u32 ufs_sprd_get_ufs_hci_version(struct ufs_hba *hba)
{
	return UFSHCI_VERSION_30;
}

static int ufs_sprd_phy_sram_init_done(struct ufs_hba *hba)
{
	int ret = 0;
	uint32_t val = 0;
	uint32_t retry = 10;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	do {
		ret = regmap_read(host->phy_sram_init_done.regmap,
				  host->phy_sram_init_done.reg, &val);
		if (ret < 0)
			return ret;

		if ((val&0x1) == 0x1) {
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRLSB), 0x1c);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRMSB), 0x40);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRLSB), 0x04);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRMSB), 0x00);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGRDWRSEL), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRLSB), 0x1c);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRMSB), 0x41);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRLSB), 0x04);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRMSB), 0x00);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGRDWRSEL), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);

			return 0;
		} else {
			udelay(1000);
			retry--;
		}
	} while (retry > 0);
		return -1;
}

static int ufs_sprd_phy_init(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	ufshcd_dme_set(hba, UIC_ARG_MIB(CBREFCLKCTRL2), 0x90);
	ufshcd_dme_set(hba, UIC_ARG_MIB(CBCRCTRL), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(RXSQCONTROL,
		       UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(RXSQCONTROL,
		       UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(CBRATESEL), 0x01);

	ret = ufs_sprd_phy_sram_init_done(hba);
	if (ret)
		return ret;

	regmap_update_bits(host->phy_sram_ext_ld_done.regmap,
			   host->phy_sram_ext_ld_done.reg,
			   host->phy_sram_ext_ld_done.mask,
			   0);

	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data0 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x40);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data0 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data1 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x41);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data1 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data0 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x02);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x40);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data0 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data1 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x02);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x41);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
			    (host->ufs_lane_calib_data1 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYDISABLE), 0x0);

	return ret;
}

static int ufs_sprd_hce_enable_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status)
{
	int err = 0;
#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
	int ret = 0;
	struct sprd_sip_svc_handle *svc_handle;
#endif

	switch (status) {
	case PRE_CHANGE:
		/* Do hardware reset before host controller enable. */
		err = ufs_sprd_hw_init(hba);
		if (err) {
			dev_err(hba->dev, "%s: ufs hardware init failed!\n", __func__);
			return err;
		}
#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
		ufshcd_writel(hba, CONTROLLER_ENABLE, REG_CONTROLLER_ENABLE);
		svc_handle = sprd_sip_svc_get_handle();
		if (!svc_handle) {
			pr_err("%s: failed to get svc handle\n", __func__);
			return -ENODEV;
		}

		ret = svc_handle->storage_ops.ufs_crypto_enable();
		pr_err("smc: enable cfg, ret:0x%x", ret);
#endif
		break;
	case POST_CHANGE:
		 if (hba->vops->phy_initialization) {
			err = hba->vops->phy_initialization(hba);
			if (err) {
				dev_err(hba->dev, "Phy setup failed (%d)\n",
					err);
			}
		}
		break;
	default:
		dev_err(hba->dev, "%s: invalid status %d\n", __func__, status);
		err = -EINVAL;
		break;
	}

	return err;
}

static int ufs_sprd_apply_dev_quirks(struct ufs_hba *hba)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	host->wlun_dev_add = true;

	return 0;
}

static int ufs_sprd_pwr_change_notify(struct ufs_hba *hba,
				enum ufs_notify_change_status status,
				struct ufs_pa_layer_attr *dev_max_params,
				struct ufs_pa_layer_attr *dev_req_params)
{
	int err = 0;

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		err = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		memcpy(dev_req_params, dev_max_params,
				       sizeof(struct ufs_pa_layer_attr));
		if (dev_req_params->gear_rx == UFS_HS_G4)
			ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), 0x0);
		break;
	case POST_CHANGE:
		/* Set auto h8 ilde time to 10ms */
		if (ufshcd_is_auto_hibern8_supported(hba)) {
			hba->ahit = AUTO_H8_IDLE_TIME_10MS;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}

out:
	return err;
}

static void ufs_sprd_hibern8_notify(struct ufs_hba *hba,
				enum uic_cmd_dme cmd,
				enum ufs_notify_change_status status)
{
	u32 set;
	unsigned long flags;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	switch (status) {
	case PRE_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
			set &= ~UIC_COMMAND_COMPL;
			ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
			spin_unlock_irqrestore(hba->host->host_lock, flags);

			clk_set_parent(host->hclk, host->rco_100M);
			ufshcd_writel(hba, 0x64, REG_HCLKDIV);
		}

		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			regmap_update_bits(host->ufsdev_refclk_en.regmap,
					   host->ufsdev_refclk_en.reg,
					   host->ufsdev_refclk_en.mask,
					   host->ufsdev_refclk_en.mask);

			regmap_update_bits(host->usb31pllv_ref2mphy_en.regmap,
					   host->usb31pllv_ref2mphy_en.reg,
					   host->usb31pllv_ref2mphy_en.mask,
					   host->usb31pllv_ref2mphy_en.mask);
			clk_set_parent(host->hclk, host->hclk_source);
			ufshcd_writel(hba, 0x100, REG_HCLKDIV);
		}
		break;
	case POST_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
			set |= UIC_COMMAND_COMPL;
			ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}

		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			regmap_update_bits(host->ufsdev_refclk_en.regmap,
					   host->ufsdev_refclk_en.reg,
					   host->ufsdev_refclk_en.mask,
					   0);

			regmap_update_bits(host->usb31pllv_ref2mphy_en.regmap,
					   host->usb31pllv_ref2mphy_en.reg,
					   host->usb31pllv_ref2mphy_en.mask,
					   0);
			mdelay(2);
		}
		break;
	default:
		break;
	}
}

static void ufs_sprd_device_reset(struct ufs_hba *hba)
{
	return;
}

static inline u16 ufs_sprd_wlun_to_scsi_lun(u8 upiu_wlun_id)
{
	return (upiu_wlun_id & ~UFS_UPIU_WLUN_ID) | SCSI_W_LUN_BASE;
}

static int ufs_sprd_get_sdev(struct ufs_hba *hba, uint channel,
			     uint id, u64 lun)
{
	struct scsi_device *sdev_rpmb;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	sdev_rpmb = __scsi_add_device(hba->host, channel, id, lun, NULL);
	if (IS_ERR(sdev_rpmb)) {
		ret = PTR_ERR(sdev_rpmb);
		return ret;
	}
	host->sdev_ufs_rpmb = sdev_rpmb;

	return ret;
}

static inline int ufs_sprd_read_geometry_desc_param(struct ufs_hba *hba,
			enum geometry_desc_param param_offset,
			u8 *param_read_buf, u32 param_size)
{
	return ufshcd_read_desc_param(hba, QUERY_DESC_IDN_GEOMETRY, 0,
				      param_offset, param_read_buf, param_size);
}

#define SEC_PROTOCOL_UFS  0xEC
#define SEC_SPECIFIC_UFS_RPMB 0x0001
#define SEC_PROTOCOL_CMD_SIZE 12
#define SEC_PROTOCOL_RETRIES 3
#define SEC_PROTOCOL_RETRIES_ON_RESET 10
#define SEC_PROTOCOL_TIMEOUT msecs_to_jiffies(1000)

static int ufs_sprd_rpmb_security_out(struct scsi_device *sdev,
				      struct rpmb_frame *frames, u32 cnt)
{
	struct scsi_sense_hdr sshdr;
	u32 trans_len = cnt * sizeof(struct rpmb_frame);
	int reset_retries = SEC_PROTOCOL_RETRIES_ON_RESET;
	int ret;
	u8 cmd[SEC_PROTOCOL_CMD_SIZE];
	char *sense = NULL;

	sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sense) {
		pr_err("%s sense alloc failed\n", __func__);
		return -1;
	}

	memset(cmd, 0, SEC_PROTOCOL_CMD_SIZE);
	cmd[0] = SECURITY_PROTOCOL_OUT;
	cmd[1] = SEC_PROTOCOL_UFS;
	put_unaligned_be16(SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	cmd[4] = 0;
	put_unaligned_be32(trans_len, cmd + 6);

	ret = scsi_test_unit_ready(sdev, SEC_PROTOCOL_TIMEOUT,
				   SEC_PROTOCOL_RETRIES, &sshdr);
	if (ret)
		dev_err(&sdev->sdev_gendev,
			"%s: rpmb scsi_test_unit_ready, ret=%d\n",
			__func__, ret);

retry:
	ret = __scsi_execute(sdev, cmd, DMA_TO_DEVICE, frames, trans_len,
			     sense, &sshdr, SEC_PROTOCOL_TIMEOUT,
			     SEC_PROTOCOL_RETRIES, 0, 0, NULL);

	if (ret && scsi_sense_valid(&sshdr) &&
	    sshdr.sense_key == UNIT_ATTENTION &&
	    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
		/*
		 * Device reset might occur several times,
		 * give it one more chance
		 */
		if (--reset_retries > 0)
			goto retry;

	if (ret)
		dev_err(&sdev->sdev_gendev, "%s: failed with err %0x\n",
			__func__, ret);

	if (driver_byte(ret) & DRIVER_SENSE)
		scsi_print_sense_hdr(sdev, "rpmb: security out", &sshdr);

	kfree(sense);
	return ret;
}

static int ufs_sprd_rpmb_security_in(struct scsi_device *sdev,
				      struct rpmb_frame *frames, u32 cnt)
{
	struct scsi_sense_hdr sshdr;
	u32 alloc_len = cnt * sizeof(struct rpmb_frame);
	int reset_retries = SEC_PROTOCOL_RETRIES_ON_RESET;
	int ret;
	u8 cmd[SEC_PROTOCOL_CMD_SIZE];
	char *sense = NULL;

	sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sense) {
		pr_err("%s sense alloc failed\n", __func__);
		return -1;
	}
	memset(cmd, 0, SEC_PROTOCOL_CMD_SIZE);
	cmd[0] = SECURITY_PROTOCOL_IN;
	cmd[1] = SEC_PROTOCOL_UFS;
	put_unaligned_be16(SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	cmd[4] = 0;
	put_unaligned_be32(alloc_len, cmd + 6);

	ret = scsi_test_unit_ready(sdev, SEC_PROTOCOL_TIMEOUT,
				   SEC_PROTOCOL_RETRIES, &sshdr);
	if (ret)
		dev_err(&sdev->sdev_gendev,
			"%s: rpmb scsi_test_unit_ready, ret=%d\n",
			__func__, ret);

retry:
	ret = __scsi_execute(sdev, cmd, DMA_FROM_DEVICE, frames, alloc_len,
			     sense, &sshdr, SEC_PROTOCOL_TIMEOUT,
			     SEC_PROTOCOL_RETRIES, 0, 0, NULL);

	if (ret && scsi_sense_valid(&sshdr) &&
	    sshdr.sense_key == UNIT_ATTENTION &&
	    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
		/*
		 * Device reset might occur several times,
		 * give it one more chance
		 */
		if (--reset_retries > 0)
			goto retry;

	if (ret)
		dev_err(&sdev->sdev_gendev, "%s: failed with err %0x\n",
			__func__, ret);

	if (driver_byte(ret) & DRIVER_SENSE)
		scsi_print_sense_hdr(sdev, "rpmb: security in", &sshdr);

	kfree(sense);
	return ret;
}

static int ufs_rpmb_cmd_seq(struct device *dev,
			    struct rpmb_cmd *cmds, u32 ncmds)
{
	unsigned long flags;
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct scsi_device *sdev;
	struct rpmb_cmd *cmd;
	int i;
	int ret;

	spin_lock_irqsave(hba->host->host_lock, flags);
	sdev = host->sdev_ufs_rpmb;
	if (sdev) {
		ret = scsi_device_get(sdev);
		if (!ret && !scsi_device_online(sdev)) {
			ret = -ENODEV;
			scsi_device_put(sdev);
		}
	} else {
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (ret)
		return ret;

	for (ret = 0, i = 0; i < ncmds && !ret; i++) {
		cmd = &cmds[i];
		if (cmd->flags & RPMB_F_WRITE)
			ret = ufs_sprd_rpmb_security_out(sdev, cmd->frames,
							 cmd->nframes);
		else
			ret = ufs_sprd_rpmb_security_in(sdev, cmd->frames,
							cmd->nframes);
	}
	scsi_device_put(sdev);

	return ret;
}

static struct rpmb_ops ufshcd_rpmb_dev_ops = {
	.cmd_seq = ufs_rpmb_cmd_seq,
	.type = RPMB_TYPE_UFS,
};

static inline void ufs_sprd_rpmb_add(struct ufs_hba *hba)
{
	struct rpmb_dev *rdev;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	u8 rw_size;
	int ret;

	ret = ufs_sprd_get_sdev(hba, 0, 0,
			ufs_sprd_wlun_to_scsi_lun(UFS_UPIU_RPMB_WLUN));
	if (ret) {
		dev_warn(hba->dev, "Cannot get rpmb dev!\n");
		return;
	}

	ret = ufs_sprd_read_geometry_desc_param(hba, GEOMETRY_DESC_PARAM_RPMB_RW_SIZE,
					&rw_size, sizeof(rw_size));
	if (ret) {
		dev_warn(hba->dev, "%s: cannot get rpmb rw limit %d\n",
			dev_name(hba->dev), ret);
		rw_size = 1;
	}

	ufshcd_rpmb_dev_ops.reliable_wr_cnt = rw_size;

	ret = scsi_device_get(host->sdev_ufs_rpmb);
	rdev = rpmb_dev_register(hba->dev, &ufshcd_rpmb_dev_ops);
	if (IS_ERR(rdev)) {
		dev_warn(hba->dev, "%s: cannot register to rpmb %ld\n",
			 dev_name(hba->dev), PTR_ERR(rdev));
		goto out_put_dev;
	}

	scsi_device_put(host->sdev_ufs_rpmb);
	return;

out_put_dev:
	scsi_device_put(host->sdev_ufs_rpmb);
	host->sdev_ufs_rpmb = NULL;
	host->wlun_dev_add = false;
}

static inline void ufs_sprd_rpmb_remove(struct ufs_hba *hba)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (!host || !host->sdev_ufs_rpmb)
		return;

	rpmb_dev_unregister(hba->dev);
	scsi_device_put(host->sdev_ufs_rpmb);
	host->sdev_ufs_rpmb = NULL;
	host->wlun_dev_add = false;
}

static void ufs_sprd_dbg_register_dump(struct ufs_hba *hba)
{
	read_ufs_debug_bus(hba);
}

void ufs_sprd_setup_xfer_req(struct ufs_hba *hba, int task_tag, bool scsi_cmd)
{
	struct ufshcd_lrb *lrbp;
	struct utp_transfer_req_desc *req_desc;
	u32 data_direction;
	u32 dword_0, crypto;

	lrbp = &hba->lrb[task_tag];
	req_desc = lrbp->utr_descriptor_ptr;
	dword_0 = le32_to_cpu(req_desc->header.dword_0);
	data_direction = dword_0 & (UTP_DEVICE_TO_HOST | UTP_HOST_TO_DEVICE);
	crypto = dword_0 & UTP_REQ_DESC_CRYPTO_ENABLE_CMD;
	if (!data_direction && crypto) {
		pr_err("ufs before dword_0 = %x,%x\n", dword_0, req_desc->header.dword_0);
		dword_0 &= ~(UTP_REQ_DESC_CRYPTO_ENABLE_CMD);
		req_desc->header.dword_0 = cpu_to_le32(dword_0);
		pr_err("ufs after dword_0 = %x,%x\n", dword_0, req_desc->header.dword_0);
	}
}

static int ufs_sprd_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	mdelay(30);
	return 0;
}

static int ufs_sprd_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	udelay(100);
	return 0;
}

/*
 * struct ufs_hba_sprd_vops - UFS sprd specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static struct ufs_hba_variant_ops ufs_hba_sprd_vops = {
	.name = "sprd",
	.init = ufs_sprd_init,
	.exit = ufs_sprd_exit,
	.get_ufs_hci_version = ufs_sprd_get_ufs_hci_version,
	.hce_enable_notify = ufs_sprd_hce_enable_notify,
	.pwr_change_notify = ufs_sprd_pwr_change_notify,
	.phy_initialization = ufs_sprd_phy_init,
	.hibern8_notify = ufs_sprd_hibern8_notify,
	.apply_dev_quirks = ufs_sprd_apply_dev_quirks,
	.setup_xfer_req = ufs_sprd_setup_xfer_req,
	.dbg_register_dump = ufs_sprd_dbg_register_dump,
	.device_reset = ufs_sprd_device_reset,
	.suspend = ufs_sprd_suspend,
	.resume = ufs_sprd_resume,
};

/**
 * ufs_sprd_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_sprd_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	struct ufs_hba *hba;
	struct ufs_sprd_host *host = NULL;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &ufs_hba_sprd_vops);
	if (err) {
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);
		goto out;
	}

	hba = platform_get_drvdata(pdev);
	host = ufshcd_get_variant(hba);
	host->wlun_dev_add = false;

	/* Poll dev init complete flag to be true*/
	while (time_before(jiffies, timeout) && !host->wlun_dev_add)
		usleep_range(5000, 10000);

	if (!host->wlun_dev_add)
		dev_warn(hba->dev, "Dev init not complete!\n");
	ufs_sprd_rpmb_add(hba);
out:
	return err;
}

/**
 * ufs_sprd_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int ufs_sprd_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufs_sprd_rpmb_remove(hba);
	ufshcd_remove(hba);
	return 0;
}
/*
 * ufs_sprd_shutdown - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static void ufs_sprd_shutdown(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	ufs_sprd_rpmb_remove(hba);
	ufshcd_pltfrm_shutdown(pdev);
}
static const struct of_device_id ufs_sprd_of_match[] = {
	{ .compatible = "sprd,ufshc"},
	{},
};

/*
 * 设备 .text 0xbec/308 B：hba 取不到就 -ENODEV；ufshcd_pltfrm_suspend 失败直接返回；
 * 成功则把 read_ufs_debug_bus() 内联展开一遍（0xc20-0xc74 与 read_ufs_debug_bus
 * 的开头逐条对应：writel(0x6)、writel(0xD, +0xc)、readl(+0x208)、BIT(28) 判断）。
 * 源码里 read_ufs_debug_bus() 已经是现成的，直接调即可。
 */
static int ufs_pltfrm_sprd_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_sprd_host *host;
	int ret;

	if (!hba)
		return -ENODEV;

	host = ufshcd_get_variant(hba);

	ret = ufshcd_pltfrm_suspend(dev);
	if (ret)
		return ret;

	read_ufs_debug_bus(hba);

	/*
	 * 设备 0xca8-0xd18：写完 ap ufshcd debugbus 那段后，读 syssel_reg+0x208，
	 * 非 0 就说明电源门控没关上 —— 打一条日志、把 device resume 回去、
	 * 返回 -EAGAIN（0xd14 mov w0,#-0xb），而不是硬挂。
	 */
	if (!host->syssel_reg)
		return 0;

	writel(0x0, host->syssel_reg);
	writel(0x0, host->syssel_reg + 0xc);
	writel(0x18, host->syssel_reg + 0x10);
	if (readl(host->syssel_reg + 0x208)) {
		dev_err(hba->dev, "ufs_pwr_gate:0x%x,monitor:0x%x\n",
			readl(host->syssel_reg + 0x208), 0);
		ufshcd_pltfrm_resume(dev);
		return -EAGAIN;
	}

	return 0;
}

static const struct dev_pm_ops ufs_sprd_pm_ops = {
	.suspend = ufs_pltfrm_sprd_suspend,
	.resume = ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume = ufshcd_pltfrm_runtime_resume,
	.runtime_idle = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver ufs_sprd_pltform = {
	.probe = ufs_sprd_probe,
	.remove = ufs_sprd_remove,
	.shutdown = ufs_sprd_shutdown,
	.driver = {
		.name = "ufshcd-sprd",
		.pm = &ufs_sprd_pm_ops,
		.of_match_table = of_match_ptr(ufs_sprd_of_match),
	},
};
module_platform_driver(ufs_sprd_pltform);

/* ---------------------------------------------------------------------------
 * Power-mode comparison (device: ufs_compare_max_pwr_mode, 0x2f2c/1408 + 0x34ac/152)
 *
 * 设备的符号表里这个函数叫 ufs_sprd_pwr_change_compare，但函数体里两处
 * __func__ 用的是 "ufs_compare_max_pwr_mode" —— 日志要逐字复现，源码里就用后者。
 * ------------------------------------------------------------------------ */

/* DME 属性：前一秒读 6 个存起来，后一秒再读 6 个比较（见 08 文档 8.143） */
#define UFS_DME_GEAR_RX_PRE	0x15830000
#define UFS_DME_LANE_RX_PRE	0x15680000
#define UFS_DME_GEAR_TX_PRE	0x15800000
#define UFS_DME_LANE_TX_PRE	0x15600000
#define UFS_DME_PWR_RX_PRE	0x156a0000
#define UFS_DME_PWR_TX_PRE	0x15710000
#define UFS_DME_GEAR_RX_POST	0x15810000
#define UFS_DME_LANE_RX_POST	0x15610000
#define UFS_DME_PWR_RX_POST	0x15870000
#define UFS_DME_PWR_TX_POST	0x15860000

/* 两份快照：pre 是切换前记下的，post 是切换后回读的（8.146） */
static u32 pwr_now_raw_pre[7];
static u32 pwr_now_raw_post[7];
/* 两份额外的展示快照（8.146/8.147） */
static u32 final_params_raw[7];
static u32 max_pwr_info_raw[7];
/* 两个独立的失败计数器，pre/post 各一（8.147 纠正 8.143 的"只有一个"） */
static int times_pre_compare_fail;
static int times_post_compare_fail;

/*
 * .rodata+0xb48 的 7 项表，越界的 pwr 档位 clamp 到 0（8.149）。
 *
 * ⚠ 表内实际排列未经二进制确认。能确定的是：.ko 里一共只存在四个相关的串
 * （"INVALID MODE"、"FASTAUTO_MODE"、"SLOWAUTO_MODE"、"SLOW_MODE"），
 * 而且 clamp 上界是 6，所以必然是 7 项、部分重复。这里按"前四项 + 三项重复"填，
 * 只保证串集合与设备一致；要定确切顺序，需要读出 .data.rel.ro 里那张指针表
 * 的重定位目标（xrefko.py 报 0 relocation，说明用的是 R_AARCH64_RELATIVE，
 * 当前工具链还没支持）。
 */
static const char *ufshcd_print_pwr_info_names[] = {
	"INVALID MODE", "FAST MODE", "SLOW_MODE",
	"FASTAUTO_MODE", "SLOWAUTO_MODE", "SLOW_MODE", "FAST MODE",
};

/*
 * 打一组 power layer 参数。p 的布局就是 struct ufs_pa_layer_attr 展开成 u32[7]：
 * [0]=gear_rx [1]=gear_tx [2]=lane_rx [3]=lane_tx [4]=pwr_rx [5]=pwr_tx [6]=hs_rate
 *
 * 两条分支打两条不同的格式串（8.149）：pwr_rx/pwr_tx 都是 2（最低档）时不需要
 * 显示具体档位名，直接写死 SLOW_MODE 并把 hs_rate 标成 rate don't care。
 * 全程走 dev_err —— 设备就是这么写的，即使是"正常"的那条。
 */
static void ufshcd_print_pwr_info(struct device *dev, const char *tag,
				  u32 *p, const char *label)
{
	if (p[4] == 2 && p[5] == 2)
		dev_err(dev,
			"%s:%s [RX, TX]: gear=[%d, %d], lane[%d, %d], pwr[%s, %s], rate don't care = %d\n",
			tag, label, p[0], p[1], p[2], p[3],
			"SLOW_MODE", "SLOW_MODE", p[6]);
	else
		dev_err(dev,
			"%s:%s [RX, TX]: gear=[%d, %d], lane[%d, %d], pwr[%s, %s], rate = %d\n",
			tag, label, p[0], p[1], p[2], p[3],
			ufshcd_print_pwr_info_names[p[4] > 6 ? 0 : p[4]],
			ufshcd_print_pwr_info_names[p[5] > 6 ? 0 : p[5]], p[6]);
}

/*
 * 读一次当前 power layer 参数到 raw[]。
 * 六次读取里有三对是"高优先级读不到才试退路"（8.145），最后两个用 peer=1 读对端。
 */
static int ufs_get_sprd_pwr_now(struct ufs_hba *hba, u32 *raw)
{
	ufshcd_dme_get_attr(hba, UFS_DME_GEAR_RX_PRE, &raw[0], 0);
	ufshcd_dme_get_attr(hba, UFS_DME_LANE_RX_PRE, &raw[1], 0);
	ufshcd_dme_get_attr(hba, UFS_DME_GEAR_TX_PRE, &raw[2], 0);
	ufshcd_dme_get_attr(hba, UFS_DME_LANE_TX_PRE, &raw[3], 0);
	/*
	 * pwr_rx / pwr_tx 是同一个 DME 属性 0x15710000 的两个 nibble：
	 * 高 4 位是 rx、低 4 位是 tx（设备 0x304c-0x3078：
	 * `ubfx w10,w8,#4,#4` + `and w8,w8,#0xf` + `stp w10,w8,[raw,#0x10]`）。
	 */
	if (ufshcd_dme_get_attr(hba, 0x15710000, &v, 0))
		goto invalid_pwr;
	raw[4] = (v >> 4) & 0xf;
	raw[5] = v & 0xf;

	return 0;

invalid_pwr:
	dev_err(hba->dev, "%s: invalid max pwm rx gear read = %d\n",
		"ufs_compare_max_pwr_mode", raw[4]);
	dev_err(hba->dev, "%s: invalid max pwm tx gear read = %d\n",
		"ufs_compare_max_pwr_mode", raw[5]);
	return -EINVAL;
}

/*
 * 设备符号 ufs_sprd_pwr_change_compare。成功返回 arg（不是布尔），
 * 比较结果只走计数器与日志 —— 这一点最容易实现错（8.148）。
 *
 * 七条比较规则（8.146/8.147）：gear_* / lane_* / hs_rate 要求"当前 <= 新值"，
 * pwr_rx / pwr_tx 要求"当前 >= 新值"。任何一条不满足就记 pre 侧失败。
 */
static int ufs_sprd_pwr_change_compare(struct ufs_hba *hba, int pre,
					  struct ufs_pa_layer_attr *out, u32 arg)
{
	u32 *raw = pre ? pwr_now_raw_pre : pwr_now_raw_post;
	const char *tag;
	u32 v;

	if (!out) {
		dev_err(hba->dev, "%s: incoming final_params is NULL\n",
			"ufs_compare_max_pwr_mode");
		return -EINVAL;
	}

	if (ufs_get_sprd_pwr_now(hba, raw))
		return -EINVAL;

	if (raw[4] == 2 && raw[5] == 2)
		raw[6] = 0;

	/* pwr_tx > 2 时按是否为 4 把 pwr_rx/pwr_tx 一起压回 1 或 2（8.144） */
	if (out->pwr_tx > 2) {
		if (out->pwr_tx == 4) {
			out->pwr_rx = 1;
			out->pwr_tx = 1;
		} else {
			out->pwr_rx = 2;
			out->pwr_tx = 2;
		}
	}

	memcpy(final_params_raw, out, sizeof(final_params_raw));
	memcpy(max_pwr_info_raw, raw, sizeof(max_pwr_info_raw));

	if (out->gear_rx <= raw[0] && out->gear_tx <= raw[1] &&
	    out->lane_rx <= raw[2] && out->lane_tx <= raw[3] &&
	    out->pwr_rx  >= raw[4] && out->pwr_tx  >= raw[5] &&
	    out->hs_rate <= raw[6])
		return arg;

	/*
	 * 设备里有两条失败路径、两个独立计数器（8.147 纠正 8.143 的“只有一个”）：
	 * pre 侧记 times_pre_compare_fail 并打 "ufs pwr pre err"，
	 * post 侧记 times_post_compare_fail 并打 "ufs pwr post err"。
	 */
	if (pre) {
		times_pre_compare_fail++;
		tag = "ufs pwr pre err";
	} else {
		times_post_compare_fail++;
		tag = "ufs pwr post err";
	}

	dev_err(hba->dev, "=== ufs err ! final more than max ===\n");
	ufshcd_print_pwr_info(hba->dev, tag, pwr_now_raw_pre, "pre_now");
	ufshcd_print_pwr_info(hba->dev, tag, pwr_now_raw_post, "post_now");
	ufshcd_print_pwr_info(hba->dev, tag, final_params_raw, " final ");
	ufshcd_print_pwr_info(hba->dev, tag, max_pwr_info_raw, " max ");

	if (out->lane_rx > 6 || out->lane_tx > 6)
		dev_err(hba->dev,
			"%s: invalid connected lanes value. rx=%d, tx=%d\n",
			"ufs_compare_max_pwr_mode", out->lane_rx, out->lane_tx);

	return -EINVAL;
}

MODULE_DESCRIPTION("SPRD Specific UFSHCI driver");
MODULE_LICENSE("GPL v2");
