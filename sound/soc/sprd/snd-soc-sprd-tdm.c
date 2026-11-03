// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) TDM CPU-DAI driver.
 *
 * This file was reconstructed from the vendor binary module
 *     snd-soc-sprd-tdm.ko
 * shipped in vendor_boot_a.bin of the Sunelan Q968 (Unisoc UMS9157 / T157,
 * board "ums9157-2h10"), Android 13, kernel 5.4.210-android12-9-g96901d45d5a5.
 *
 * The vendor build path baked into the binary is:
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/dai/tdm/tdm.c
 *
 * Module layout of the binary (for reference):
 *   .text=0x2520 .rodata=0x1298 .data=0x360 .bss=0x30 __verbose=0x498
 *   modinfo: author=SPRD  description=SPRD ASoC TDM CUP-DAI driver
 *            license=GPL v2  depends=snd-soc-sprd-card  alias=of:N*T*Cunisoc,tdm
 *
 * Exported symbols: sprd_tdm_dai_to_config(), tdm_config_get(),
 *                   tdm_config_set().
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

/* Provided by snd-soc-sprd-card.ko */
extern int get_sp_audio_debug_flag(void);

/* Bits of get_sp_audio_debug_flag() as used by this driver */
#define TDM_DBG_FLAG_PARSE	BIT(0)	/* dump DT parsed values */
#define TDM_DBG_FLAG_DUMP	BIT(2)	/* dump every register access */
extern struct regmap *arch_audio_get_agcp_ahb_gpr(void);
extern void arch_audio_set_agcp_ahb_gpr(struct regmap *map);
extern int get_agcp_ahb_set_offset(void);
extern int get_agcp_ahb_clr_offset(void);

/*
 * agdsp_access_enable()/disable() are exported by agdsp_access.ko.  The vendor
 * module carries __weak fallbacks (see the end of this file) so that the driver
 * keeps working when the AGDSP access driver is not loaded.
 */
extern int agdsp_access_enable(void);
extern void agdsp_access_disable(void);

#define TDM_RX_OFFSET		0x00
#define TDM_TX_OFFSET		0x20

/* Register offsets, RX bank. The TX bank is the same offset + 0x20. */
#define TDM_CTRL0		0x00	/* controller/int/timeout/io, bck pos delay */
#define TDM_CTRL1		0x04	/* format, threshold, slot, sync, msb ... */
#define TDM_LRCLK_DIV		0x08	/* lrclk divider, bits [31:16] */
#define TDM_BCLK_DIV		0x08	/* bclk divider, bits [15:0] (same reg) */
#define TDM_INT_STATUS		0x44	/* write-1-to-clear interrupt bits */
#define TDM_GLOBAL		0x40	/* duplex mode / auto gate / dma / int */

/* TDM_CTRL0 bits */
#define TDM_CTRL0_EN		BIT(0)
#define TDM_CTRL0_SOFT_RST	BIT(1)
#define TDM_CTRL0_TIMEOUT_EN	BIT(2)
#define TDM_CTRL0_IO_EN		BIT(3)

/* TDM_CTRL1 bits */
#define TDM_CTRL1_MST_MODE	BIT(0)
#define TDM_CTRL1_PULSE_MODE	GENMASK(2, 1)
#define TDM_CTRL1_SYNC_MODE	BIT(4)
#define TDM_CTRL1_MSB_MODE	BIT(5)
#define TDM_CTRL1_SYNC_EN	BIT(6)
#define TDM_CTRL1_LRCK_INV	BIT(3)
#define TDM_CTRL1_SLOT_NUM	GENMASK(9, 8)
#define TDM_CTRL1_SLOT_WIDTH	GENMASK(11, 10)
#define TDM_CTRL1_DATA_MODE	GENMASK(13, 12)
#define TDM_CTRL1_DATA_WIDTH	GENMASK(15, 14)
#define TDM_CTRL1_SLOT_VALID	GENMASK(23, 16)
#define TDM_CTRL1_THRESHOLD	GENMASK(31, 24)

/* TDM_GLOBAL bits */
#define TDM_GLOBAL_INT_EN	BIT(0)
#define TDM_GLOBAL_DMA_EN	BIT(1)
#define TDM_GLOBAL_AUTO_GATE	BIT(2)
#define TDM_GLOBAL_DUPLEX_MODE	GENMASK(4, 3)

/* TDM_INT_STATUS: write 1 to clear */
#define TDM_INT_CLR_TX		0x15
#define TDM_INT_CLR_RX		0x2a

/* struct tdm_config indices, mirrored by the exported kcontrol handlers */
#define TDM_CFG_FS		0
#define TDM_CFG_MST_MODE	1
#define TDM_CFG_DUPLEX_MODE	2
#define TDM_CFG_THRESHOLD	3
#define TDM_CFG_DATA_WIDTH	4
#define TDM_CFG_DATA_MODE	5
#define TDM_CFG_SLOT_WIDTH	6
#define TDM_CFG_SLOT_NUM	7
#define TDM_CFG_MSB_MODE	8
#define TDM_CFG_PULSE_MODE	9
#define TDM_CFG_SLOT_VALID	10
#define TDM_CFG_SYNC_MODE	11
#define TDM_CFG_LRCK_INVERT	12
#define TDM_CFG_RX_BCK_DLY	13
#define TDM_CFG_TX_BCK_DLY	14
#define TDM_CFG_MAX		15

#define TDM_DEF_FS		48000
#define TDM_DEF_THRESHOLD	0x80
#define TDM_DEF_SLAVE_TIMEOUT	0x1e00
#define TDM_DEF_FIFO_MODE	0x000c0002

/*
 * TDM configuration, 0x48 bytes.  It is embedded in struct tdm_priv at
 * offset 0x38 and exported through tdm_config_{set,get}().
 *
 * The two "dly" fields (13/14) are used as the TX/RX BCK position delay.
 * The remaining fields at 0x30/0x38/0x3c are not reachable through the
 * kcontrol interface.
 */
struct tdm_config {
	u32 tdm_fs;		/* 0x00: frame rate in Hz, 48000..96000 */
	u32 trx_mst_mode;	/* 0x04: 0 = slave, 1 = master */
	u32 duplex_mode;	/* 0x08: 0..3, but 2 is rejected */
	u32 trx_threshold;	/* 0x0c */
	u32 trx_data_width;	/* 0x10: 0..2 */
	u32 trx_data_mode;	/* 0x14: 0..2 */
	u32 trx_slot_width;	/* 0x18: 0..2 */
	u32 trx_slot_num;	/* 0x1c: 0..2 */
	u32 trx_msb_mode;	/* 0x20: 0..1 */
	u32 trx_pulse_mode;	/* 0x24: 0..2 */
	u32 tdm_slot_valid;	/* 0x28 */
	u32 trx_sync_mode;	/* 0x2c: 0..1 */
	u32 slave_timeout;	/* 0x30: only settable from device tree */
	u32 lrck_invert;	/* 0x34: 0..1 */
	u32 reserved38;		/* 0x38: 0x000c0002 (fifo mode, const) */
	u32 rx_fifo_depth;	/* 0x3c: 20 (const) */
	u32 rx_bck_pos_dly;	/* 0x40 */
	u32 tx_bck_pos_dly;	/* 0x44 */
};

/* struct tdm_priv, 0x80 bytes, devm_kmalloc()ed in probe */
struct tdm_priv {
	struct device *dev;		/* 0x00 */
	int stream_cnt;			/* 0x08: never written, always 0 */
	u32 reserved0c;			/* 0x0c */
	u64 reserved10;			/* 0x10 */
	u32 memphys;			/* 0x20 */
	u32 reserved24;			/* 0x24 */
	void __iomem *base;		/* 0x28 */
	u32 reg_size;			/* 0x30 */
	u32 playback;			/* 0x34: substream->stream != 0 */
	struct tdm_config config;	/* 0x38 */
};

/*
 * DMA descriptor used by the PCM DMA glue.  It is the Unisoc "sprd dma
 * config" (0xb0 bytes) shared with the VBC front-end DAIs; only the fields
 * touched by this driver are named here.
 */
struct tdm_dma_data {
	const char *name;		/* 0x00 */
	u32 channel;			/* 0x08 */
	u32 reserved0c;			/* 0x0c */
	u32 chn_num;			/* 0x10 */
	u8 pad0[0x5c];			/* 0x14..0x6f */
	u32 reserved70;			/* 0x70 */
	u32 datawidth;			/* 0x74 */
	u32 src_step;			/* 0x78 */
	u32 des_step;			/* 0x7c */
	u32 fifo_offset;		/* 0x80 */
	u8 pad1[0x0c];			/* 0x84..0x8f */
	dma_addr_t dev_paddr;		/* 0x90 */
	u32 has_phy;			/* 0x98 */
	u8 pad2[0x04];			/* 0x9c */
	const char *chan_name;		/* 0xa0 */
	void *suffix;			/* 0xa8 */
};

static struct tdm_config *dup_config;
static u8 tdm_enable;
static struct regmap *ap_ahb_gpr;
static struct regmap *audcp_clk_rf;
static struct regmap *audcp_dvfs_apb_rf;
static struct regmap *aon_apb_rf;

/*
 * Default TDM configuration, copied to tdm->config in probe and then
 * overridden by the device tree properties.
 */
static const struct tdm_config def_tdm_config = {
	.tdm_fs			= 48000,
	.trx_mst_mode		= 1,
	.duplex_mode		= 0,
	.trx_threshold		= 0x80,
	.trx_data_width		= 0,
	.trx_data_mode		= 0,
	.trx_slot_width		= 0,
	.trx_slot_num		= 0,
	.trx_msb_mode		= 0,
	.trx_pulse_mode		= 0,
	.tdm_slot_valid		= 3,
	.trx_sync_mode		= 1,
	.slave_timeout		= TDM_DEF_SLAVE_TIMEOUT,
	.lrck_invert		= 0,
	.reserved38		= TDM_DEF_FIFO_MODE,
	.rx_fifo_depth		= 20,
	.rx_bck_pos_dly		= 0,
	.tx_bck_pos_dly		= 0,
};

static struct tdm_dma_data tdm_pcm_stereo_out = {
	.name		= "TDM PCM Stereo out",
	.datawidth	= 4,
	.src_step	= 4,
};

static struct tdm_dma_data tdm_pcm_stereo_in = {
	.name		= "TDM PCM Stereo in",
	.datawidth	= 4,
	.src_step	= 4,
};

static const char *const use_dma_name[2] = { "tdm_tx", "tdm_rx" };

/*
 * Register helpers.  Every write is followed by an optional register dump
 * gated by the sprd audio debug flag bit 2.
 */
static void tdm_reg_dump(void __iomem *reg, u32 val)
{
	printk(KERN_INFO "[ASoC: TDM ] [0x%04lx] U:[0x%08x] R:[0x%08x]\n",
	       (unsigned long)reg & 0xffff, val, readl(reg));
}

static void tdm_reg_write(struct tdm_priv *tdm, u32 off, u32 val)
{
	void __iomem *reg = tdm->base + off;

	writel(val, reg);
	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_DUMP)
		tdm_reg_dump(reg, val);
}

static u32 tdm_reg_read(struct tdm_priv *tdm, u32 off)
{
	return readl(tdm->base + off);
}

/* read-modify-write of one field */
static void tdm_reg_update(struct tdm_priv *tdm, u32 off, u32 mask, u32 val)
{
	void __iomem *reg = tdm->base + off;
	u32 v = readl(reg);

	v = (v & ~mask) | (val & mask);
	writel(v, reg);
	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_DUMP)
		tdm_reg_dump(reg, v);
}

/*
 * Direction dependent register: RX lives in the low bank, playback (TX) in
 * the bank at +0x20.
 */
static inline u32 tdm_dir(struct tdm_priv *tdm)
{
	return tdm->playback ? TDM_TX_OFFSET : TDM_RX_OFFSET;
}

static void tdm_set_duplex_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_duplex_mode");
	tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_DUPLEX_MODE,
		       tdm->config.duplex_mode << 3);
}

static void tdm_set_auto_gate_en(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_auto_gate_en");
	tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_AUTO_GATE,
		       TDM_GLOBAL_AUTO_GATE);
}

static void tdm_set_dma_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s Enable = %d\n", "tdm_set_dma_en", en);
	if (en)
		tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_DMA_EN,
			       TDM_GLOBAL_DMA_EN);
	else if (tdm->stream_cnt <= 0)
		tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_DMA_EN, 0);
}

static void tdm_set_int_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s Enable = %d\n", "tdm_set_int_en", en);
	if (en)
		tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_INT_EN,
			       TDM_GLOBAL_INT_EN);
	else if (tdm->stream_cnt <= 0)
		tdm_reg_update(tdm, TDM_GLOBAL, TDM_GLOBAL_INT_EN, 0);
}

static void tdm_timeout_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s Enable = %d\n", "tdm_timeout_en", en);
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, TDM_CTRL0_TIMEOUT_EN,
		       en ? TDM_CTRL0_TIMEOUT_EN : 0);
}

static void tdm_bck_pos_dly(struct tdm_priv *tdm)
{
	u32 dly;

	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_bck_pos_dly");
	dly = tdm->playback ? tdm->config.tx_bck_pos_dly
			    : tdm->config.rx_bck_pos_dly;
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, GENMASK(6, 4),
		       dly << 4);
}

static void tdm_clr_tx_int(struct tdm_priv *tdm)
{
	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_DUMP)
		printk(KERN_INFO "[ASoC: TDM ] [0x%04lx] R:[0x%08x]\n",
		       (unsigned long)(uintptr_t)(tdm->base + TDM_INT_STATUS) & 0xffff,
		       readl(tdm->base + TDM_INT_STATUS));

	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_clr_tx_int");
	tdm_reg_update(tdm, TDM_INT_STATUS, TDM_INT_CLR_TX, TDM_INT_CLR_TX);
}

static void tdm_clr_rx_int(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_clr_rx_int");
	tdm_reg_update(tdm, TDM_INT_STATUS, TDM_INT_CLR_RX, TDM_INT_CLR_RX);
}

static void tdm_set_threshold(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_threshold");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_THRESHOLD,
		       tdm->config.trx_threshold << 24);
}

static void tdm_set_mst_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_mst_mode");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_MST_MODE,
		       tdm->config.trx_mst_mode == 1 ? TDM_CTRL1_MST_MODE : 0);
}

static void tdm_set_data_width(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_data_width");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_DATA_WIDTH,
		       tdm->config.trx_data_width << 14);
}

static void tdm_set_data_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_data_mode");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_DATA_MODE,
		       tdm->config.trx_data_mode << 12);
}

static void tdm_set_slot_width(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_slot_width");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_SLOT_WIDTH,
		       tdm->config.trx_slot_width << 10);
}

static void tdm_set_slot_valid(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_slot_valid");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_SLOT_VALID,
		       tdm->config.tdm_slot_valid << 16);
}

static void tdm_set_slot_num(struct tdm_priv *tdm)
{
	u32 off = tdm_dir(tdm) + TDM_CTRL1;
	u32 v;

	printk(KERN_INFO "[ASoC: TDM ] %s trx_slot_num : %d \n",
	       "tdm_set_slot_num", tdm->config.trx_slot_num);
	tdm_reg_update(tdm, off, TDM_CTRL1_SLOT_NUM,
		       tdm->config.trx_slot_num << 8);

	v = tdm_reg_read(tdm, off);
	switch (tdm->config.trx_slot_num) {
	case 0:
		v = (v & 0xff00ffff) | 0x30000;
		break;
	case 1:
		v = (v & 0xff00ffff) | 0xf0000;
		break;
	case 2:
		v = v | 0xff0000;
		break;
	default:
		return;
	}
	tdm_reg_write(tdm, off, v);
}

static void tdm_set_msb_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_msb_mode");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_MSB_MODE,
		       tdm->config.trx_msb_mode == 1 ? TDM_CTRL1_MSB_MODE : 0);
}

static void tdm_set_pulse_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_pulse_mode");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_PULSE_MODE,
		       tdm->config.trx_pulse_mode << 1);
}

static void tdm_set_lrck_invert(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_lrck_invert");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_LRCK_INV,
		       tdm->config.lrck_invert == 1 ? TDM_CTRL1_LRCK_INV : 0);
}

static void tdm_set_sync_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s Enable = %d\n", "tdm_set_sync_en", en);
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_SYNC_EN,
		       en ? TDM_CTRL1_SYNC_EN : 0);
}

static void tdm_set_sync_mode(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_sync_mode");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL1, TDM_CTRL1_SYNC_MODE,
		       tdm->config.trx_sync_mode == 1 ? TDM_CTRL1_SYNC_MODE : 0);
}

static void tdm_soft_reset(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_soft_reset");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, TDM_CTRL0_SOFT_RST, 0);
	udelay(10);
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, TDM_CTRL0_SOFT_RST, 0);
}

static void tdm_io_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s Enable = %d\n", "tdm_io_en", en);
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, TDM_CTRL0_IO_EN,
		       en ? TDM_CTRL0_IO_EN : 0);
}

static void tdm_controller_en(struct tdm_priv *tdm, u32 en)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_controller_en");
	tdm_reg_update(tdm, tdm_dir(tdm) + TDM_CTRL0, TDM_CTRL0_EN,
		       en ? TDM_CTRL0_EN : 0);
}

/*
 * Global module enable: powers the audio PCM/TDM block through the AGCP AHB
 * GPR, the audcp clk rf syscon (only when the audio debug flag is set) and
 * finally gates AGDSP access.
 */
static void tdm_module_en(u32 on)
{
	struct regmap *gpr;
	int ret;

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "tdm_module_en", __LINE__);

	if (on & 1) {
		if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_PARSE)
			pr_debug("[ASoC: TDM ] %s\n", "tdm_global_enable");

		ret = agdsp_access_enable();
		tdm_enable = 1;
		if (ret) {
			printk(KERN_ERR "[ASoC: TDM ] %s agdsp_access_enable error %d",
			       "tdm_module_en", ret);
			return;
		}

		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_set_offset() + 8, 0x20000000);
		udelay(10);
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_clr_offset() + 8, 0x20000000);
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_set_offset() + 8, 0x8000000);
		udelay(10);
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_clr_offset() + 8, 0x8000000);
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_set_offset(), 0x8000000);
		gpr = arch_audio_get_agcp_ahb_gpr();
		regmap_write(gpr, get_agcp_ahb_set_offset(), 0x10000000);
		regmap_update_bits(arch_audio_get_agcp_ahb_gpr(), 4, 0x3000000,
				   0x1000000);
		regmap_update_bits(arch_audio_get_agcp_ahb_gpr(), 4, 0xc00000,
				   0x400000);
		return;
	}

	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_set_offset() + 8, 0x20000000);
	udelay(10);
	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_clr_offset() + 8, 0x20000000);
	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_set_offset() + 8, 0x8000000);
	udelay(10);
	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_clr_offset() + 8, 0x8000000);
	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_clr_offset(), 0x8000000);
	gpr = arch_audio_get_agcp_ahb_gpr();
	regmap_write(gpr, get_agcp_ahb_clr_offset(), 0x10000000);
	regmap_update_bits(arch_audio_get_agcp_ahb_gpr(), 4, 0x3000000, 0);
	regmap_update_bits(arch_audio_get_agcp_ahb_gpr(), 4, 0xc00000, 0);

	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_PARSE)
		pr_debug("[ASoC: TDM ] %s\n", "tdm_global_disable");

	ret = agdsp_access_disable();
	tdm_enable = 0;
	if (ret)
		printk(KERN_ERR "[ASoC: TDM ] %s agdsp_access_disable error %d",
		       "tdm_module_en", ret);
}

static struct tdm_config *sprd_tdm_dai_to_config(struct snd_soc_dai *dai)
{
	struct tdm_priv *tdm = dev_get_drvdata(dai->dev);

	return &tdm->config;
}
EXPORT_SYMBOL_GPL(sprd_tdm_dai_to_config);

/*
 * The kcontrol handlers are exported for the machine driver; private_value
 * points to a descriptor whose u16 at +0x0c holds the inverted config index
 * (vendor convention, kept as is).
 */
struct sprd_tdm_kctrl_priv {
	u32 reserved[3];
	u16 index;
	u16 pad;
};

static void tdm_config_setting(u32 index, u32 value, struct tdm_config *cfg)
{
	if (!cfg) {
		printk(KERN_ERR "[ASoC: TDM ] %s config is NULL,error!\n",
		       "tdm_config_setting");
		return;
	}

	if (index > 14) {
		printk(KERN_ERR "[ASoC: TDM ] tdm echo cmd is invalid\n");
		return;
	}

	pr_debug("[ASoC: TDM ] %s tdm index %d,value %d\n",
		 "tdm_config_setting", index, value);

	switch (index) {
	case TDM_CFG_FS:
		if (value < TDM_DEF_FS || value - TDM_DEF_FS > TDM_DEF_FS)
			break;
		cfg->tdm_fs = value;
		break;
	case TDM_CFG_MST_MODE:
		if (value > 1)
			break;
		cfg->trx_mst_mode = value;
		break;
	case TDM_CFG_DUPLEX_MODE:
		if (value > 3 || value == 2)
			break;
		cfg->duplex_mode = value;
		break;
	case TDM_CFG_THRESHOLD:
		cfg->trx_threshold = value;
		break;
	case TDM_CFG_DATA_WIDTH:
		if (value > 2)
			break;
		cfg->trx_data_width = value;
		break;
	case TDM_CFG_DATA_MODE:
		if (value > 2)
			break;
		cfg->trx_data_mode = value;
		break;
	case TDM_CFG_SLOT_WIDTH:
		if (value > 2)
			break;
		cfg->trx_slot_width = value;
		break;
	case TDM_CFG_SLOT_NUM:
		if (value > 2)
			break;
		cfg->trx_slot_num = value;
		break;
	case TDM_CFG_MSB_MODE:
		if (value > 1)
			break;
		cfg->trx_msb_mode = value;
		break;
	case TDM_CFG_PULSE_MODE:
		if (value > 2)
			break;
		cfg->trx_pulse_mode = value;
		break;
	case TDM_CFG_SLOT_VALID:
		cfg->tdm_slot_valid = value;
		break;
	case TDM_CFG_SYNC_MODE:
		if (value > 1)
			break;
		cfg->trx_sync_mode = value;
		break;
	case TDM_CFG_LRCK_INVERT:
		if (value > 1)
			break;
		cfg->lrck_invert = value;
		break;
	case TDM_CFG_RX_BCK_DLY:
		cfg->rx_bck_pos_dly = value;
		break;
	case TDM_CFG_TX_BCK_DLY:
		cfg->tx_bck_pos_dly = value;
		break;
	}
}

static int tdm_config_getting(u32 index, struct tdm_config *cfg)
{
	if (!cfg) {
		printk(KERN_ERR "[ASoC: TDM ] %s config is NULL,error\n",
		       "tdm_config_getting");
		return -1;
	}

	if (index > 14) {
		printk(KERN_ERR "[ASoC: TDM ] tdm config index is invalid\n");
		return -1;
	}

	pr_debug("[ASoC: TDM ] %s tdm index %d\n", "tdm_config_getting", index);

	switch (index) {
	case TDM_CFG_FS:
		return cfg->tdm_fs;
	case TDM_CFG_MST_MODE:
		return cfg->trx_mst_mode;
	case TDM_CFG_DUPLEX_MODE:
		return cfg->duplex_mode;
	case TDM_CFG_THRESHOLD:
		return cfg->trx_threshold;
	case TDM_CFG_DATA_WIDTH:
		return cfg->trx_data_width;
	case TDM_CFG_DATA_MODE:
		return cfg->trx_data_mode;
	case TDM_CFG_SLOT_WIDTH:
		return cfg->trx_slot_width;
	case TDM_CFG_SLOT_NUM:
		return cfg->trx_slot_num;
	case TDM_CFG_MSB_MODE:
		return cfg->trx_msb_mode;
	case TDM_CFG_PULSE_MODE:
		return cfg->trx_pulse_mode;
	case TDM_CFG_SLOT_VALID:
		return cfg->tdm_slot_valid;
	case TDM_CFG_SYNC_MODE:
		return cfg->trx_sync_mode;
	case TDM_CFG_LRCK_INVERT:
		return cfg->lrck_invert;
	case TDM_CFG_RX_BCK_DLY:
		return cfg->rx_bck_pos_dly;
	case TDM_CFG_TX_BCK_DLY:
		return cfg->tx_bck_pos_dly;
	}

	return -1;
}

int tdm_config_set(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_tdm_kctrl_priv *priv;
	u32 index;

	if (!dup_config) {
		printk(KERN_ERR "[ASoC: TDM ] %s return\n", "tdm_config_set");
		return 0;
	}

	priv = (struct sprd_tdm_kctrl_priv *)kcontrol->private_value;
	index = (u16)~priv->index;
	tdm_config_setting(index, ucontrol->value.integer.value[0],
			   dup_config);
	return 0;
}
EXPORT_SYMBOL_GPL(tdm_config_set);

int tdm_config_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct sprd_tdm_kctrl_priv *priv;
	u32 index;
	int ret;

	if (!dup_config) {
		printk(KERN_ERR "[ASoC: TDM ] %s return\n", "tdm_config_get");
		return 0;
	}

	priv = (struct sprd_tdm_kctrl_priv *)kcontrol->private_value;
	index = (u16)~priv->index;
	ret = tdm_config_getting(index, dup_config);
	ucontrol->value.integer.value[0] = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(tdm_config_get);

static void sprd_tdm_init_state(struct tdm_priv *tdm, u32 stream)
{
	tdm->playback = stream != 0;

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_init_state",
	       __LINE__);

	tdm_set_duplex_mode(tdm);
	tdm_set_auto_gate_en(tdm);
	tdm_set_dma_en(tdm, 0);
	tdm_set_int_en(tdm, 0);
	tdm_timeout_en(tdm, 0);
	tdm_bck_pos_dly(tdm);
	tdm_clr_tx_int(tdm);
	tdm_clr_rx_int(tdm);
}

static void tdm_configs_set(struct tdm_priv *tdm)
{
	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_configs_set");

	tdm_set_threshold(tdm);
	tdm_set_mst_mode(tdm);
	tdm_set_data_width(tdm);
	tdm_set_data_mode(tdm);
	tdm_set_slot_width(tdm);
	tdm_set_slot_valid(tdm);
	tdm_set_slot_num(tdm);
	tdm_set_msb_mode(tdm);
	tdm_set_pulse_mode(tdm);
	tdm_set_lrck_invert(tdm);

	if (tdm->config.trx_mst_mode == 0) {
		tdm_set_sync_en(tdm, 1);
		tdm_set_sync_mode(tdm);
	}

	tdm_soft_reset(tdm);
}

/*
 * lrclk / bclk dividers.  Derived from the 0xc000 (49152) reference clock:
 *   lrclk_div = 0xc000 / (fs / 1000), halved in pulse mode
 *   bclk_div  = (0xc000 / (khz * slot_factor * width_factor)) / 2 - 1
 * Only master mode (trx_mst_mode == 1) programs them.
 */
static void tdm_set_lrclk_div_num(struct tdm_priv *tdm)
{
	u32 off = tdm_dir(tdm) + TDM_LRCLK_DIV;
	u32 khz, div, v;

	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_lrclk_div_num");
	if (tdm->config.trx_mst_mode != 1)
		return;

	khz = (u32)(((u64)tdm->config.tdm_fs * 0x10624dd3ULL) >> 38);
	div = 0xc000 / khz;
	if (tdm->config.trx_pulse_mode == 0)
		div >>= 1;

	v = tdm_reg_read(tdm, off);
	v = (v & 0xffff) | (div << 16);
	/* The vendor binary subtracts 0x10000 from the merged value, i.e. it
	 * programs (div - 1) << 16.  Kept bit-exact on purpose.
	 */
	v -= 0x10000;
	tdm_reg_write(tdm, off, v);
}

static void tdm_set_bclk_div_num(struct tdm_priv *tdm)
{
	u32 off = tdm_dir(tdm) + TDM_BCLK_DIV;
	u32 khz, slot_factor, width_factor, bclk, div, v;

	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_set_bclk_div_num");
	if (tdm->config.trx_mst_mode != 1)
		return;

	khz = (u32)(((u64)tdm->config.tdm_fs * 0x10624dd3ULL) >> 38);

	switch (tdm->config.trx_slot_num) {
	case 0:
		slot_factor = 2;
		break;
	case 1:
		slot_factor = 4;
		break;
	default:
		slot_factor = 8;
		break;
	}

	width_factor = tdm->config.trx_slot_width == 0 ? 0x10 : 0x20;

	bclk = khz * slot_factor * width_factor;
	div = 0xc000 / bclk;
	div >>= 1;
	div -= 1;

	v = tdm_reg_read(tdm, off);
	v = (v & ~0xffff) | (div & 0xffff);
	tdm_reg_write(tdm, off, v);
}

static int sprd_tdm_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct tdm_priv *tdm = dev_get_drvdata(dai->dev);

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_startup", __LINE__);

	tdm_module_en(1);

	if (tdm_enable) {
		printk(KERN_INFO "[ASoC: TDM ] tdm_clk_set_cfg set reg!\n");
		regmap_update_bits(audcp_clk_rf, 0xb8, 0x10000, 0x10000);
		regmap_update_bits(audcp_clk_rf, 0xe8, 0x1, 0x1);
		regmap_update_bits(audcp_clk_rf, 0xf4, 0x1, 0x1);
	} else {
		printk(KERN_INFO "[ASoC: TDM ] tdm is not open\n");
	}

	if (tdm_enable) {
		printk(KERN_INFO "[ASoC: TDM ] tdm_audcp_dvfs_set set reg!\n");
		regmap_update_bits(audcp_dvfs_apb_rf, 0x1f4, 0x1e, 0);
		regmap_update_bits(audcp_dvfs_apb_rf, 0x9b4, 0x20, 0);
		regmap_update_bits(audcp_dvfs_apb_rf, 0xa58, 0x40000000,
				   0x40000000);
		regmap_update_bits(audcp_dvfs_apb_rf, 0xa58, 0x3, 0x3);
		regmap_update_bits(audcp_dvfs_apb_rf, 0xa58, 0x1ff8, 0);
	} else {
		printk(KERN_INFO "[ASoC: TDM ] tdm is not open\n");
	}

	if (tdm_enable) {
		printk(KERN_INFO "[ASoC: TDM ] tdm_aud_access_en set reg!\n");
		regmap_update_bits(aon_apb_rf, 0x14c, 0x1, 0x1);
	} else {
		printk(KERN_INFO "[ASoC: TDM ] tdm is not open\n");
	}

	sprd_tdm_init_state(tdm, substream->stream);
	tdm_configs_set(tdm);
	tdm_set_lrclk_div_num(tdm);
	tdm_set_bclk_div_num(tdm);

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_startup", __LINE__);
	return 0;
}

static void sprd_tdm_shutdown(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_shutdown",
	       __LINE__);
	tdm_module_en(0);
}

static int sprd_tdm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct tdm_priv *tdm = dev_get_drvdata(dai->dev);
	struct tdm_dma_data *dma;
	u32 format, width;
	u32 steps;

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_hw_params",
	       __LINE__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma = &tdm_pcm_stereo_out;
		dma->channel = 21;
		dma->chan_name = use_dma_name[0];
		dma->fifo_offset = 0x20 -
			(u16)(tdm->config.reserved38 >> 16);
	} else {
		dma = &tdm_pcm_stereo_in;
		dma->channel = 22;
		dma->chan_name = use_dma_name[1];
		dma->fifo_offset = 0x20 -
			(u16)tdm->config.rx_fifo_depth;
	}

	/* data width in bytes */
	dma->datawidth = tdm->config.trx_data_width == 0 ? 2 : 4;

	/* slot width in bytes */
	steps = tdm->config.trx_slot_width == 0 ? 2 : 4;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma->src_step = steps;
	else
		dma->des_step = steps;

	/*
	 * The vendor binary reads the sample-width mask directly out of
	 * struct snd_pcm_hw_params (masks[1].bits[0] at 0x24, bits[1] at 0x28)
	 * and derives the PCM bit width from its lowest set bit.
	 */
	format = *(const u32 *)((const void *)params + 0x24);
	if (format) {
		width = __ffs(format);
	} else {
		format = *(const u32 *)((const void *)params + 0x28);
		width = format ? (0x20 + __ffs(format)) : 0;
	}

	printk(KERN_INFO
	       "[ASoC: TDM ] format %d, desc.datawidth %d, desc.src_step %d, desc.des_step %d\n",
	       width, dma->datawidth, dma->src_step, dma->des_step);

	dma->dev_paddr = tdm->memphys + 0x400;
	printk(KERN_INFO "[ASoC: TDM ] dma_data->dev_paddr[0]: 0x %d ",
	       (u32)dma->dev_paddr);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dai->playback_dma_data = dma;
	else
		dai->capture_dma_data = dma;

	if (tdm->config.trx_mst_mode != 1)
		return 0;

	tdm_set_lrclk_div_num(tdm);
	tdm_set_bclk_div_num(tdm);

	return 0;
}

static int sprd_tdm_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	static const u32 dump_regs[] = {
		0x00, 0x04, 0x08, 0x0c, 0x10, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x40
	};
	struct tdm_priv *tdm = dev_get_drvdata(dai->dev);
	u32 bit;
	int i;

	printk(KERN_INFO "[ASoC: TDM ] %s %d \n", "sprd_tdm_trigger",
	       __LINE__);

	if (cmd > SNDRV_PCM_TRIGGER_DRAIN)
		return -EINVAL;

	bit = 1 << cmd;
	if (bit & 0x29) {
		/* STOP, PAUSE_RELEASE, RESUME */
		tdm_set_dma_en(tdm, 0);
		tdm_controller_en(tdm, 0);
		udelay(10);
		tdm_io_en(tdm, 0);
	} else if (bit & 0x52) {
		/* START, SUSPEND, DRAIN */
		tdm_io_en(tdm, 1);
		udelay(10);
		tdm_controller_en(tdm, 1);
		tdm_set_dma_en(tdm, 1);
	} else {
		return -EINVAL;
	}

	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_DUMP) {
		for (i = 0; i < ARRAY_SIZE(dump_regs); i++)
			printk(KERN_INFO
			       "[ASoC: TDM ] A:[0x%08lx] R:[0x%08x]\n",
			       (unsigned long)tdm->base + dump_regs[i],
			       readl(tdm->base + dump_regs[i]));
	}

	return 0;
}

static int tdm_drv_probe(struct platform_device *pdev)
{
	static const char *const dt_props[] = {
		"sprd,duplex_mode",
		"sprd,slave_timeout",
		"sprd,tdm_fs",
		"sprd,trx_mst_mode",
		"sprd,trx_threshold",
		"sprd,trx_data_width",
		"sprd,trx_data_mode",
		"sprd,trx_slot_width",
		"sprd,trx_slot_num",
		"sprd,trx_msb_mode",
		"sprd,trx_pulse_mode",
		"sprd,tdm_slot_valid",
		"sprd,trx_sync_mode",
	};

	/*
	 * One format string per property, exactly as the vendor object stores them
	 * (they cannot be folded into a single "Change %s to %d" because the
	 * binary carries thirteen separate __dynamic_pr_debug descriptors).
	 */
	static const char *const change_fmt[] = {
		"[ASoC: TDM ] Change duplex_mode to %d!\n",
		"[ASoC: TDM ] Change slave_timeout to %d!\n",
		"[ASoC: TDM ] Change tdm_fs to %d!\n",
		"[ASoC: TDM ] Change trx_mst_mode to %d!\n",
		"[ASoC: TDM ] Change trx_threshold to %d!\n",
		"[ASoC: TDM ] Change trx_data_width to %d!\n",
		"[ASoC: TDM ] Change trx_data_mode to %d!\n",
		"[ASoC: TDM ] Change trx_slot_width to %d!\n",
		"[ASoC: TDM ] Change trx_slot_num to %d!\n",
		"[ASoC: TDM ] Change trx_msb_mode to %d!\n",
		"[ASoC: TDM ] Change trx_pulse_mode to %d!\n",
		"[ASoC: TDM ] Change tdm_slot_valid to %d!\n",
		"[ASoC: TDM ] Change trx_sync_mode to %d!\n",
	};
	struct device_node *np = pdev->dev.of_node;
	struct platform_device *pdev_dev = pdev;
	struct tdm_priv *tdm;
	struct resource *res;
	u32 *fields[ARRAY_SIZE(dt_props)];
	u32 val;
	int i, ret;

	printk(KERN_INFO "[ASoC: TDM ] %s\n", "tdm_drv_probe");

	if (!np) {
		printk(KERN_ERR "[ASoC: TDM ] ERR: %s, node is NULL!\n",
		       "tdm_drv_probe");
		return -ENODEV;
	}

	tdm = devm_kmalloc(&pdev->dev, sizeof(*tdm), GFP_KERNEL);
	if (!tdm)
		return -ENOMEM;

	tdm->dev = &pdev->dev;

	if (!arch_audio_get_agcp_ahb_gpr()) {
		ap_ahb_gpr = syscon_regmap_lookup_by_phandle(np,
							"sprd,syscon-agcp-ahb");
		if (IS_ERR(ap_ahb_gpr)) {
			printk(KERN_ERR "[ASoC: TDM ] ERR: Get the tdm ap apb syscon failed!\n");
			return -EINVAL;
		}
		arch_audio_set_agcp_ahb_gpr(ap_ahb_gpr);
	}

	audcp_clk_rf = syscon_regmap_lookup_by_phandle(np,
						      "sprd,syscon-clk-rf");
	if (IS_ERR(audcp_clk_rf)) {
		printk(KERN_ERR "[ASoC: TDM ] ERR: Get the tdm audcp clk rf syscon failed!\n");
		return -EINVAL;
	}

	audcp_dvfs_apb_rf = syscon_regmap_lookup_by_phandle(np,
						"sprd,syscon-dvfs-apb");
	if (IS_ERR(audcp_dvfs_apb_rf)) {
		printk(KERN_ERR "[ASoC: TDM ] ERR: Get the tdm audcp dvfs apb rf syscon failed!\n");
		return -EINVAL;
	}

	aon_apb_rf = syscon_regmap_lookup_by_phandle(np,
						    "sprd,syscon-aon-apb");
	if (IS_ERR(aon_apb_rf)) {
		printk(KERN_ERR "[ASoC: TDM ] ERR: Get the tdm aon apb rf syscon failed!\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR "[ASoC: TDM ] ERR:Must give me the base address!\n");
		return -EINVAL;
	}

	tdm->memphys = res->start;
	tdm->reg_size = resource_size(res);
	tdm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tdm->base)) {
		printk(KERN_ERR "[ASoC: TDM ] ERR:tdm reg address ioremap_nocache error !\n");
		return -EINVAL;
	}

	if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_PARSE)
		pr_debug("[ASoC: TDM ] tdm->memphys[0x%p] tdm->membase:[0x%p]",
			 (void *)(uintptr_t)tdm->memphys, tdm->base);

	memcpy(&tdm->config, &def_tdm_config, sizeof(tdm->config));

	fields[0] = &tdm->config.duplex_mode;
	fields[1] = &tdm->config.slave_timeout;
	fields[2] = &tdm->config.tdm_fs;
	fields[3] = &tdm->config.trx_mst_mode;
	fields[4] = &tdm->config.trx_threshold;
	fields[5] = &tdm->config.trx_data_width;
	fields[6] = &tdm->config.trx_data_mode;
	fields[7] = &tdm->config.trx_slot_width;
	fields[8] = &tdm->config.trx_slot_num;
	fields[9] = &tdm->config.trx_msb_mode;
	fields[10] = &tdm->config.trx_pulse_mode;
	fields[11] = &tdm->config.tdm_slot_valid;
	fields[12] = &tdm->config.trx_sync_mode;

	for (i = 0; i < ARRAY_SIZE(dt_props); i++) {
		ret = of_property_read_variable_u32_array(np, dt_props[i],
							 &val, 1, 0);
		if (!ret) {
			*fields[i] = val;
			if (get_sp_audio_debug_flag() & TDM_DBG_FLAG_PARSE)
				pr_debug(change_fmt[i], val);
		}
	}

	tdm->stream_cnt = 0;
	platform_set_drvdata(pdev_dev, tdm);

	ret = snd_soc_register_component(&pdev->dev, &sprd_tdm_component,
					 &sprd_tdm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register DAI failed: %d\n", ret);
		return ret;
	}

	dup_config = &tdm->config;

	return 0;
}

static int tdm_drv_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id tdm_of_match[] = {
	{ .compatible = "unisoc,tdm" },
	{ },
};
MODULE_DEVICE_TABLE(of, tdm_of_match);

static const struct snd_soc_dai_ops sprd_tdm_dai_ops = {
	.startup	= sprd_tdm_startup,
	.shutdown	= sprd_tdm_shutdown,
	.hw_params	= sprd_tdm_hw_params,
	.trigger	= sprd_tdm_trigger,
};

static const struct snd_soc_component_driver sprd_tdm_component = {
	.name		= "sprd-tdm",
};

static struct snd_soc_dai_driver sprd_tdm_dai = {
	.name		= "tdm-dai",
	.id		= 0x129,
	.ops		= &sprd_tdm_dai_ops,
	.capture	= {
		.stream_name	= NULL,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 8000,
		.rate_max	= 96000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.playback	= {
		.stream_name	= NULL,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
};

static struct platform_driver tdm_driver = {
	.probe	= tdm_drv_probe,
	.remove	= tdm_drv_remove,
	.driver	= {
		.name		= "tdm",
		.of_match_table	= tdm_of_match,
	},
};

/*
 * The vendor .rodata also carries the two literals below, but no code in the
 * module references them (the whole .text was disassembled and neither string
 * has a relocation nor an ADRP/ADD pair pointing at it).  They are reproduced
 * verbatim so that a byte comparison against the original finds them:
 *
 *	"[ASoC: TDM ] %s return value %ld,id %d\n"
 *	"tdm->memphys[0x%p] tdm->membase:[0x%p]"
 */

module_platform_driver(tdm_driver);

/*
 * Weak fallbacks, exactly as in the vendor module (each carries a struct
 * _ddebug descriptor and only traces).  The module loader resolves these
 * references against agdsp_access.ko when that module is loaded.
 */
__weak int agdsp_access_enable(void)
{
	pr_debug("[ASoC: TDM ] %s\n", __func__);
	return 0;
}

__weak void agdsp_access_disable(void)
{
	pr_debug("[ASoC: TDM ] %s\n", __func__);
}

MODULE_DESCRIPTION("SPRD ASoC TDM CUP-DAI driver");
MODULE_DESCRIPTION("TDM DAI driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("of:N*T*Cunisoc,tdm");
MODULE_ALIAS("of:N*T*Cunisoc,tdmC*");
