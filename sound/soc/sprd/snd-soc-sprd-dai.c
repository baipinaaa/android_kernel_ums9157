// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc AGCP I2S DAI driver
 *
 * This file is reconstructed from the vendor module
 * snd-soc-sprd-dai.ko shipped in the Sunelan Q968 (UMS9157/T157)
 * vendor_boot ramdisk image.  The vendor module was built out of tree:
 *
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/dai/sprd_dai/sprd-dai.c
 *
 * Module information taken from the binary:
 *   author=Bin Pang <bin.pang@unisoc.com>
 *   description=SPRD ASoC DAI driver
 *   license=GPL
 *   alias=of:N*T*Csprd,agcp-i2s-dai
 *   depends=audio_sipc,agdsp_access,snd-soc-sprd-card
 *
 * Only port 0x125 (I2S0 TX, "AGCP_IIS0_TX") and 0x126 are accepted by
 * the driver, the DAI exposes playback only.
 *
 * Note: in the shipped binary the pr_debug()/printk() debug traces of
 * the DT parser are present in .text but unreachable (all branches jump
 * over them).  They are kept here because they exist in the vendor
 * source and are needed to identify the DT properties.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

/* SIPC channel used to talk with the audio DSP */
#define AMSG_CH_MISC			0xf
/* Timeout used for every SIPC command */
#define AMSG_NO_WAIT			(-1)

/* Supported AGCP I2S port ids (sprd,id) */
#define SPRD_I2S_PORT_MIN		0x125
#define SPRD_I2S_PORT_MAX		0x126

/* AGCP I2S register offsets (relative to priv->base) */
#define I2S_CTRL0			0x00	/* enable, io en, soft reset */
#define I2S_DIV				0x04	/* master clock divider */
#define I2S_CTRL1			0x08	/* mode, format, clk source */
#define I2S_SLAVE_TIMEOUT		0x0c
#define I2S_PCM_CFG			0x10
#define I2S_RX_WATERMARK		0x14
#define I2S_MULTI_CH			0x34
#define I2S_TX_WATERMARK		0x38
#define I2S_CLK_CTRL			0x40
#define I2S_CLKM			0x50
#define I2S_CLKN			0x5c

/* I2S_CTRL1 bits */
#define I2S_CLK_SRC_MASK		(0x3 << 0)
#define I2S_CLK_SRC(x)			((x) & 0x3)
#define I2S_TRANS_MODE			BIT(2)
#define I2S_WORK_MODE			BIT(3)
#define I2S_WIDTH_MASK			(0x3 << 4)
#define I2S_WIDTH_16BIT			(0x1 << 4)
#define I2S_WIDTH_32BIT			(0x2 << 4)
#define I2S_RTX_MODE_MASK		(0x3 << 6)
#define I2S_RTX_MODE(x)			(((x) & 0x3) << 6)
#define I2S_BUS_FRAME_MODE		BIT(8)
#define I2S_SYNC_MODE			BIT(9)
#define I2S_LEFT_CH_LVL			BIT(10)
#define I2S_CLK_INV			BIT(11)
#define I2S_DMA_EN			BIT(14)
#define I2S_BUS_TYPE			BIT(15)

/* I2S_CLK_CTRL bits */
#define I2S_CLK_SRC_SEL_MASK		(0x3 << 0)
#define I2S_CLK_SRC_SEL(x)		((x) & 0x3)
#define I2S_CLK_SRC_DIV			(0x1 << 4)
#define I2S_CLK_SRC_PLL			(0x2 << 4)
#define I2S_CLK_SRC_MASK		(0x3 << 4)
#define I2S_PCM_DUMMY_EN		BIT(7)

/* I2S_MULTI_CH bits */
#define I2S_MULTI_CH_MASK		(0xf << 12)
#define I2S_MULTI_CH(x)			(((x) & 0xf) << 12)
#define I2S_MULTI_CH_INV_MASK		(0xf << 16)
#define I2S_MULTI_CH_INV(x)		(((~(x)) & 0xf) << 16)

/* I2S_PCM_CFG bits */
#define I2S_PCM_SLOT_MASK		0x7
#define I2S_PCM_CYCLE_MASK		(0x7f << 3)

/* AUDCP clock controller register used to select the audio PLL */
#define AUDCP_CLK_CFG			0xa58
#define AUDCP_CLK_CFG_MASK		0xff001fff
#define AUDCP_CLK_SEL_MASK		0x7000	/* bits 13..15 */
#define AUDCP_CLK_SEL(x)		(((x) & 0x7) << 13)
#define AUDCP_CLK_24M576		(0x6 << 13)
#define AUDCP_CLK_19M6608		(0x81008006)

/* Audio PLL frequencies returned by the rate selection (in HZ) */
#define AUDIO_PLL_163M840		163840000
#define AUDIO_PLL_245M760		245760000
#define AUDIO_PLL_196M608		196608000

/* Width codes of the I2S_CTRL1 width field */
#define I2S_WIDTH_CODE_16		0x10
#define I2S_WIDTH_CODE_32		0x20

/* I2S work mode */
#define I2S_WORK_MODE_MASTER		0
#define I2S_WORK_MODE_SLAVE		1

/* I2S bus type */
#define I2S_BUS_TYPE_I2S		0
#define I2S_BUS_TYPE_PCM		1

/* I2S clock config mode (sprd,div-mode) */
#define I2S_DIV_MODE_INTERNAL		0
#define I2S_DIV_MODE_EXTERNAL		2

/* Unisoc ASoC data used to select the default configuration */
struct sprd_asoc_data {
	u32 device_id;
	u32 board_version;
};

/**
 * struct sprd_i2s_config - AGCP I2S configuration, filled from the DT
 * @hw_port:		I2S hardware port id
 * @bus_type:		I2S or PCM
 * @work_mode:		master or slave
 * @trans_mode:		transmission mode
 * @rtx_mode:		rx/tx mode
 * @sync_mode:		sync mode
 * @bus_frame_mode:	bus frame mode
 * @multi_ch:		multi channel mode
 * @left_ch_lvl:	left channel level
 * @clk_inv:		clock invert mode
 * @tx_watermark:	tx fifo watermark
 * @rx_watermark:	rx fifo watermark
 * @slave_timeout:	slave timeout (only in slave mode)
 * @pcm_slot:		PCM slot number
 * @pcm_cycle:		PCM cycle number
 */
struct sprd_i2s_config {
	u32 hw_port;
	u32 bus_type;
	u32 work_mode;
	u32 trans_mode;
	u32 rtx_mode;
	u32 sync_mode;
	u32 bus_frame_mode;
	u32 multi_ch;
	u32 left_ch_lvl;
	u32 clk_inv;
	u32 tx_watermark;
	u32 rx_watermark;
	u32 slave_timeout;
	u32 pcm_slot;
	u32 pcm_cycle;
};

/*
 * DMA descriptor handed to the platform DMA driver through
 * snd_soc_dai_set_dma_data().  The layout (0xb0 bytes) is shared with
 * the MCDT/VBC and the other Unisoc DAIs; several fields are reused
 * with a different meaning depending on the DAI type.
 */
struct sprd_i2s_dma_params {
	void *reserved00;	/* 0x00 */
	u32 dma_no;		/* 0x08: dma channel used for this stream */
	u32 reserved0c;		/* 0x0c */
	u32 reserved10;		/* 0x10 */
	u32 reserved[22];	/* 0x14 .. 0x6b */
	u32 fmt_width;		/* 0x6c */
	u32 reserved70;		/* 0x70 */
	u32 datawidth;		/* 0x74 */
	u32 src_step;		/* 0x78 */
	u32 des_step;		/* 0x7c */
	u32 fifo_offset;	/* 0x80 */
	u32 reserved84;		/* 0x84 */
	u32 phys_l;		/* 0x88 */
	u32 phys_r;		/* 0x8c */
	u64 phys;		/* 0x90 */
	u32 has_phy;		/* 0x98 */
	u32 reserved9c;		/* 0x9c */
	const char *chan_name;	/* 0xa0 */
	const char *suffix_r;	/* 0xa8 */
};

/**
 * struct i2s_priv - private data of the AGCP I2S port
 * @cfg:		configuration parsed from the DT
 * @dma_params:		DMA descriptor of this port
 * @rx_dma_no:		dma channel used for capture
 * @tx_dma_no:		dma channel used for playback
 * @div_mode:		clock divider mode
 * @rate_multi:		rate multiplier (div_mode 2 only)
 * @fifo_depth:		fifo depth of the controller
 * @open_cnt:		number of users of this port
 * @clk_membase:	mapping of the AUDCP clock controller
 * @base:		mapping of the I2S controller
 * @memphys:		physical address of the I2S controller
 * @reg_size:		size of the I2S controller window
 * @enable:		whether the DSP pipe has been enabled
 * @dev:		device of the platform driver
 * @port_id:		port id read from "sprd,id"
 */
struct i2s_priv {
	struct sprd_i2s_config cfg;
	struct sprd_i2s_dma_params dma_params;
	u32 rx_dma_no;
	u32 tx_dma_no;
	u32 div_mode;
	u32 rate_multi;
	u32 fifo_depth;
	atomic_t open_cnt;
	void __iomem *clk_membase;
	void __iomem *base;
	u64 memphys;
	u32 reg_size;
	u32 enable;
	struct device *dev;
	u32 port_id;
};

/*
 * Symbols provided by the Unisoc audio sound card / SIPC modules which
 * are not part of the upstream kernel.
 */
extern struct regmap *arch_audio_get_agcp_ahb_gpr(void);
extern void arch_audio_set_agcp_ahb_gpr(struct regmap *map);
extern int aud_ipc_ch_open(u8 channel);
extern int aud_ipc_ch_close(u8 channel);
extern int aud_send_cmd_no_param(u8 channel, u32 command, u32 p0, u32 p1,
				 u32 p2, u32 p3, int timeout);
extern int agdsp_access_enable(void);
extern void agdsp_access_disable(void);

static const char *const sprd_use_dma_name[8] = {
	"iis0_tx", "iis0_rx",
	"iis1_tx", "iis1_tx",
	"iis2_tx", "iis2_tx",
	"iis3_tx", "iis3_tx",
};

/* Default configuration of an I2S port */
static const struct sprd_i2s_config default_agcp_i2s_config = {
	.rtx_mode	= 3,
	.bus_frame_mode	= 0,
	.multi_ch	= 1,
	.tx_watermark	= 0x40,
	.rx_watermark	= 0xc0,
	.slave_timeout	= 0xf11,
};

/* Default configuration of a PCM port */
static const struct sprd_i2s_config default_agcp_pcm_config = {
	.bus_type	= I2S_BUS_TYPE_PCM,
	.trans_mode	= 0,
	.rtx_mode	= 3,
};

static void sprd_dai_reg_update(struct i2s_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->base + reg);
	pr_debug("%s: [0x%04lx] [0x%08x] [0x%08x]\n", __func__,
		 (unsigned long)(uintptr_t)(priv->base + reg) & 0xffff, val,
		 readl(priv->base + reg));
}

static void sprd_agcp_i2s_dma_enable(struct i2s_priv *priv, int enable)
{
	u32 val;

	pr_debug("%s: enable = %d\n", "sprd_agcp_i2s_dma_enable", enable);
	val = readl(priv->base + I2S_CTRL1);
	if (enable)
		val |= I2S_DMA_EN;
	else
		val &= ~I2S_DMA_EN;

	sprd_dai_reg_update(priv, I2S_CTRL1, val);
}

static void sprd_agcp_i2s_set_tx_watermark(struct i2s_priv *priv)
{
	u32 tx_wm = priv->cfg.tx_watermark;
	u32 val = (priv->fifo_depth - tx_wm) | (tx_wm << 8);
	u32 reg = readl(priv->base + I2S_TX_WATERMARK);

	printk(KERN_INFO "%s: tx watermark = 0x%x\n",
	       "sprd_agcp_i2s_set_tx_watermark", val);
	reg &= ~0xffff;
	reg |= val & 0xffff;

	sprd_dai_reg_update(priv, I2S_TX_WATERMARK, reg);
}

static void sprd_agcp_i2s_set_rx_watermark(struct i2s_priv *priv)
{
	u32 rx_wm = priv->cfg.rx_watermark;
	u32 val = rx_wm | ((priv->fifo_depth - rx_wm) << 8);
	u32 reg = readl(priv->base + I2S_RX_WATERMARK);

	printk(KERN_INFO "%s: rx watermark = 0x%x\n",
	       "sprd_agcp_i2s_set_rx_watermark", val);
	reg &= ~0xffff;
	reg |= val & 0xffff;

	sprd_dai_reg_update(priv, I2S_RX_WATERMARK, reg);
}

static void sprd_agcp_i2s_set_clk_div(struct i2s_priv *priv, u32 clkm, u32 clkn)
{
	u32 val;

	val = readl(priv->base + I2S_CLKM);
	val &= ~0xffff;
	val |= clkm & 0xffff;
	sprd_dai_reg_update(priv, I2S_CLKM, val);

	val = readl(priv->base + I2S_CLKM + 4);
	val &= ~(0x3f << 16);
	val |= (clkm >> 16) & (0x3f << 16);
	sprd_dai_reg_update(priv, I2S_CLKM + 4, val);

	val = readl(priv->base + I2S_CLKN);
	val &= ~0xffff;
	val |= clkn & 0xffff;
	sprd_dai_reg_update(priv, I2S_CLKN, val);

	val = readl(priv->base + I2S_CLKN + 4);
	val &= ~0x3f;
	val |= (clkn >> 16) & 0x3f;
	sprd_dai_reg_update(priv, I2S_CLKN + 4, val);
}

/*
 * Program the clock inversion and the clock source according to
 * "sprd,clk-inv".  Eight combinations are supported; each of them
 * selects the clock source (I2S_CTRL1[1:0]) and inverts (or not) the
 * bit clock (I2S_CTRL1[11]) depending on the master/slave mode.
 */
static void sprd_agcp_i2s_set_clk_inv(struct i2s_priv *priv)
{
	u32 src;
	u32 val;
	bool invert;

	printk(KERN_INFO "%s: clk_inv = %u\n", "sprd_agcp_i2s_set_clk_inv",
	       priv->cfg.clk_inv);

	if (priv->cfg.clk_inv > 7) {
		printk(KERN_WARNING "%s: unsupported clk_inv = %u\n",
		       "sprd_agcp_i2s_set_clk_inv", priv->cfg.clk_inv);
		return;
	}

	switch (priv->cfg.clk_inv) {
	case 0:
		invert = false;
		src = 1;
		break;
	case 1:
		invert = true;
		src = 1;
		break;
	case 2:
		invert = !!priv->cfg.work_mode;
		src = invert ? 1 : 2;
		break;
	case 3:
		invert = !priv->cfg.work_mode;
		src = invert ? 2 : 1;
		break;
	case 4:
		invert = !!priv->cfg.work_mode;
		src = invert ? 1 : 3;
		break;
	case 5:
		invert = !priv->cfg.work_mode;
		src = invert ? 3 : 1;
		break;
	case 6:
		invert = priv->cfg.work_mode ? true : false;
		if (!priv->cfg.work_mode) {
			src = 0;
			break;
		}
		invert = false;
		src = 1;
		break;
	default:	/* 7 */
		invert = priv->cfg.work_mode ? true : false;
		if (!priv->cfg.work_mode) {
			src = 0;
			break;
		}
		invert = true;
		src = 1;
		break;
	}

	val = readl(priv->base + I2S_CTRL1);
	if (invert)
		val |= I2S_CLK_INV;
	else
		val &= ~I2S_CLK_INV;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_CLK_SRC_MASK;
	val |= I2S_CLK_SRC(src);
	sprd_dai_reg_update(priv, I2S_CTRL1, val);
}

/*
 * Some codecs need a "dummy" frame to keep the bit clock running.  Only
 * relevant for I2S (not PCM) ports.
 */
static void sprd_agcp_i2s_set_dummy_mode(struct i2s_priv *priv, int rate,
					 int channels)
{
	u32 sync_mode = priv->cfg.sync_mode;
	u32 frame_mode = priv->cfg.bus_frame_mode;
	u32 val;

	if (priv->cfg.bus_type != I2S_BUS_TYPE_I2S)
		return;

	printk(KERN_INFO "%s: div_mode = %u bus_frame_mode = %u sync_mode = %u\n",
	       "sprd_agcp_i2s_set_dummy_mode", priv->div_mode, frame_mode,
	       sync_mode);

	if (priv->div_mode < 2) {
		if (frame_mode == 1 && sync_mode == 0) {
			val = readl(priv->base + I2S_CLK_CTRL);
			val &= ~I2S_CLK_SRC_SEL_MASK;
			val |= I2S_CLK_SRC_SEL(2);
			sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);
		}
		return;
	}

	if (priv->div_mode != I2S_DIV_MODE_EXTERNAL) {
		printk(KERN_WARNING "%s: unsupported div_mode = %u\n",
		       "sprd_agcp_i2s_set_dummy_mode", priv->div_mode);
		return;
	}

	val = readl(priv->base + I2S_CLK_CTRL);
	val &= ~I2S_CLK_SRC_SEL_MASK;
	val |= (frame_mode + 1) & I2S_CLK_SRC_SEL_MASK;
	sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);

	if (rate > 96000 || (channels == 2 && frame_mode == 1)) {
		val = readl(priv->base + I2S_CLK_CTRL);
		val |= I2S_PCM_DUMMY_EN;
		sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);
	}
}

/*
 * Configure the controllers from the parsed DT: clock source/divider,
 * watermarks, PCM parameters and finally the clock dividers when the
 * port is the master of the bus.
 */
static int sprd_agcp_i2s_config_param(struct i2s_priv *priv, int rate,
				      int channels, int width)
{
	struct regmap *map;
	u32 val, reg, wm;
	u32 src_clk;
	int clkm, clkn, sck, div, ret;

	map = arch_audio_get_agcp_ahb_gpr();
	printk(KERN_INFO "%s: rate (%d) channel (%d) width (%d) mode (%s)\n",
	       "sprd_agcp_i2s_config_param", rate, channels, width,
	       priv->cfg.work_mode == I2S_WORK_MODE_SLAVE ?
	       "I2S_SLAVE" : "I2S_MASTER");

	if (!map)
		return -1;

	ret = agdsp_access_enable();
	if (ret)
		return ret;

	/* Select the audio PLL feeding the I2S block */
	reg = AUDCP_CLK_CFG;
	val = readl(priv->clk_membase + reg);
	val &= AUDCP_CLK_CFG_MASK;
	writel(val, priv->clk_membase + reg);

	switch (rate) {
	case 8000:
		val = readl(priv->clk_membase + reg);
		val &= ~AUDCP_CLK_SEL_MASK;
		val |= AUDCP_CLK_SEL(5);
		writel(val, priv->clk_membase + reg);
		ret = aud_send_cmd_no_param(AMSG_CH_MISC, 2, 5, 0, 0, 0,
					    AMSG_NO_WAIT);
		if (ret < 0)
			printk(KERN_ERR "%s: set DVFS (%d) failed\n",
			       "sprd_agcp_i2s_config_param", 5);
		src_clk = AUDIO_PLL_163M840;
		break;
	case 9600:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
		val = readl(priv->clk_membase + reg);
		val &= ~AUDCP_CLK_SEL_MASK;
		val |= AUDCP_CLK_24M576;
		writel(val, priv->clk_membase + reg);
		ret = aud_send_cmd_no_param(AMSG_CH_MISC, 2, 3, 0, 0, 0,
					    AMSG_NO_WAIT);
		if (ret < 0)
			printk(KERN_ERR "%s: set DVFS (%d) failed\n",
			       "sprd_agcp_i2s_config_param", 3);
		src_clk = AUDIO_PLL_245M760;
		break;
	case 96000:
	case 192000:
		val = readl(priv->clk_membase + reg);
		val |= AUDCP_CLK_19M6608;
		writel(val, priv->clk_membase + reg);
		src_clk = AUDIO_PLL_196M608;
		break;
	default:
		printk(KERN_ERR "%s: unsupported sample rate (%d)\n",
		       "sprd_agcp_i2s_config_param", rate);
		return -524;	/* -EREMOTEIO */
	}

	agdsp_access_disable();
	printk(KERN_INFO "%s: src_clk = %dHZ\n", "sprd_agcp_i2s_config_param",
	       src_clk);

	/* Bus/work/transfer modes */
	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_BUS_TYPE;
	val |= (priv->cfg.bus_type & 1) << 15;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_WORK_MODE;
	val |= (priv->cfg.work_mode & 1) << 3;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_TRANS_MODE;
	val |= (priv->cfg.trans_mode & 1) << 2;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_RTX_MODE_MASK;
	val |= I2S_RTX_MODE(priv->cfg.rtx_mode);
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_SYNC_MODE;
	val |= (priv->cfg.sync_mode & 1) << 9;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_BUS_FRAME_MODE;
	val |= (priv->cfg.bus_frame_mode & 1) << 8;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	/* Multi channel mode: the inverted value is stored next to it */
	val = readl(priv->base + I2S_MULTI_CH);
	val &= ~I2S_MULTI_CH_MASK;
	val |= I2S_MULTI_CH(priv->cfg.multi_ch);
	sprd_dai_reg_update(priv, I2S_MULTI_CH, val);

	val = readl(priv->base + I2S_MULTI_CH);
	val &= ~I2S_MULTI_CH_INV_MASK;
	val |= I2S_MULTI_CH_INV(priv->cfg.multi_ch);
	sprd_dai_reg_update(priv, I2S_MULTI_CH, val);

	/* Sample width */
	switch (width) {
	case 16:
		wm = I2S_WIDTH_CODE_16;
		break;
	case 24:
	case 32:
		wm = I2S_WIDTH_CODE_32;
		break;
	default:
		printk(KERN_ERR "%s: unsupported width (%d)\n",
		       "sprd_agcp_i2s_config_param", width);
		return -524;	/* -EREMOTEIO */
	}

	val = readl(priv->base + I2S_CTRL1);
	val &= ~I2S_WIDTH_MASK;
	val |= wm;
	sprd_dai_reg_update(priv, I2S_CTRL1, val);

	sprd_agcp_i2s_set_clk_inv(priv);
	sprd_agcp_i2s_set_tx_watermark(priv);
	sprd_agcp_i2s_set_rx_watermark(priv);

	/* Left channel level, only used when the sync signal is internal */
	if (!priv->cfg.sync_mode) {
		val = readl(priv->base + I2S_CTRL1);
		val &= ~I2S_LEFT_CH_LVL;
		val |= (priv->cfg.left_ch_lvl & 1) << 10;
		sprd_dai_reg_update(priv, I2S_CTRL1, val);
	}

	/* Slave needs a timeout to release the bus when the master dies */
	if (priv->cfg.work_mode == I2S_WORK_MODE_SLAVE) {
		val = readl(priv->base + I2S_SLAVE_TIMEOUT);
		val &= ~0xfff;
		val |= priv->cfg.slave_timeout & 0xfff;
		sprd_dai_reg_update(priv, I2S_SLAVE_TIMEOUT, val);
	}

	sprd_agcp_i2s_set_dummy_mode(priv, rate, channels);

	/* PCM mode parameters */
	if (priv->cfg.bus_type == I2S_BUS_TYPE_PCM) {
		val = readl(priv->base + I2S_PCM_CFG);
		val &= ~I2S_PCM_SLOT_MASK;
		val |= priv->cfg.pcm_slot & I2S_PCM_SLOT_MASK;
		sprd_dai_reg_update(priv, I2S_PCM_CFG, val);

		val = readl(priv->base + I2S_PCM_CFG);
		val &= ~I2S_PCM_CYCLE_MASK;
		val |= (priv->cfg.pcm_cycle & 0x7f) << 3;
		sprd_dai_reg_update(priv, I2S_PCM_CFG, val);
	}

	/* The slave does not generate the clocks */
	if (priv->cfg.work_mode == I2S_WORK_MODE_SLAVE)
		return 0;

	sck = channels * rate * width;

	switch (priv->div_mode) {
	case I2S_DIV_MODE_INTERNAL:
	default:
		div = src_clk / (2 * sck);
		if (src_clk == div * 2 * sck) {
			div -= 1;
			printk(KERN_INFO "%s: iis_sck = %uHZ iis_clkd = 0x%x\n",
			       "sprd_agcp_i2s_config_param", sck, div);

			val = readl(priv->base + I2S_CLK_CTRL);
			val &= ~I2S_CLK_SRC_MASK;
			sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);

			val = readl(priv->base + I2S_DIV);
			val &= ~0xffff;
			val |= div & 0xffff;
			sprd_dai_reg_update(priv, I2S_DIV, val);
		} else {
			clkm = (2 * sck) / 100;
			clkn = src_clk / 100;
			printk(KERN_INFO "%s: iis_sck = %uHZ clkm = 0x%x clkn = 0x%x\n",
			       "sprd_agcp_i2s_config_param", sck, clkm, clkn);

			val = readl(priv->base + I2S_CLK_CTRL);
			val &= ~I2S_CLK_SRC_MASK;
			val |= I2S_CLK_SRC_DIV;
			sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);

			sprd_agcp_i2s_set_clk_div(priv, clkm, clkn);
		}
		break;
	case I2S_DIV_MODE_EXTERNAL:
		if (!priv->cfg.work_mode) {
			int base_rate = priv->rate_multi * rate;

			if (rate == 192000)
				div = 0;
			else
				div = src_clk / (2 * base_rate) - 1;

			sck = channels * rate * width * (2 * div + 2);
			clkm = sck / 100;
			clkn = src_clk / 100;

			printk(KERN_INFO "%s: iis_sck = %uHZ iis_clkd = 0x%x\n",
			       "sprd_agcp_i2s_config_param", base_rate, div);
			printk(KERN_INFO "%s: clkm = 0x%x clkn = 0x%x\n",
			       "sprd_agcp_i2s_config_param", clkm, clkn);

			val = readl(priv->base + I2S_CLK_CTRL);
			val &= ~I2S_CLK_SRC_MASK;
			val |= I2S_CLK_SRC_PLL;
			sprd_dai_reg_update(priv, I2S_CLK_CTRL, val);

			sprd_agcp_i2s_set_clk_div(priv, clkm, clkn);

			val = readl(priv->base + I2S_DIV);
			val &= ~0xffff;
			val |= div & 0xffff;
			sprd_dai_reg_update(priv, I2S_DIV, val);
		}
		break;
	}

	sprd_agcp_i2s_dma_enable(priv, 0);
	return 0;
}

static int sprd_agcp_i2s_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct i2s_priv *priv = dev_get_drvdata(dai->dev);
	struct regmap *map;
	int ret;

	map = arch_audio_get_agcp_ahb_gpr();
	printk(KERN_INFO "%s: open_cnt = %d\n", "sprd_agcp_i2s_startup",
	       atomic_read(&priv->open_cnt));

	if (atomic_read(&priv->open_cnt)) {
		atomic_inc(&priv->open_cnt);
		return 0;
	}

	if (!map)
		return -1;

	ret = agdsp_access_enable();
	if (ret)
		return ret;

	ret = aud_send_cmd_no_param(AMSG_CH_MISC, 1, 0, 0, 0, 0,
				    AMSG_NO_WAIT);
	if (ret)
		printk(KERN_ERR "%s: restore DVFS failed\n",
		       "sprd_agcp_i2s_startup");

	priv->enable = 1;

	regmap_update_bits(map, 8, 0x1000, 0x1000);
	udelay(10);
	regmap_update_bits(map, 8, 0x1000, 0);
	udelay(10);
	regmap_update_bits(map, 4, 0x8000, 0);
	regmap_update_bits(map, 4, 0x20000000, 0x20000000);
	regmap_update_bits(map, 0, 1, 1);

	atomic_inc(&priv->open_cnt);
	return 0;
}

static int sprd_agcp_i2s_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct i2s_priv *priv = dev_get_drvdata(dai->dev);
	struct regmap *map;
	int ret;

	map = arch_audio_get_agcp_ahb_gpr();
	printk(KERN_INFO "%s: open_cnt = %d\n", "sprd_agcp_i2s_shutdown",
	       atomic_read(&priv->open_cnt));

	if (atomic_read(&priv->open_cnt) != 1) {
		atomic_dec(&priv->open_cnt);
		return 0;
	}

	if (!map)
		return 0;

	regmap_update_bits(map, 0, 1, 0);
	regmap_update_bits(map, 4, 0x20000000, 0);

	ret = aud_send_cmd_no_param(AMSG_CH_MISC, 3, 0, 0, 0, 0,
				    AMSG_NO_WAIT);
	if (ret < 0)
		printk(KERN_ERR "%s: restore DVFS failed\n",
		       "sprd_agcp_i2s_shutdown");

	if (priv->enable == 1) {
		ret = aud_send_cmd_no_param(AMSG_CH_MISC, 0, 0, 0, 0, 0,
					    AMSG_NO_WAIT);
		if (ret < 0)
			printk(KERN_ERR "%s: restore DVFS failed\n",
			       "sprd_agcp_i2s_shutdown");
		else
			priv->enable = 0;
	}

	agdsp_access_disable();
	atomic_dec(&priv->open_cnt);
	return 0;
}

static int sprd_agcp_i2s_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct i2s_priv *priv = dev_get_drvdata(dai->dev);
	struct sprd_i2s_dma_params *dma = &priv->dma_params;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	unsigned int rate = params_rate(params);
	u32 width_bits, code, src_step, des_step;
	u32 lo, hi;
	int ret;

	printk(KERN_INFO "%s: port (%d)\n", "sprd_agcp_i2s_hw_params",
	       priv->cfg.hw_port);

	dma->dma_no = playback ? priv->tx_dma_no : priv->rx_dma_no;
	dma->chan_name = sprd_use_dma_name[playback ?
					  (priv->cfg.hw_port * 2) :
					  (priv->cfg.hw_port * 2 + 1)];

	if (playback)
		dma->fifo_offset = priv->fifo_depth - priv->cfg.tx_watermark;
	else
		dma->fifo_offset = priv->cfg.rx_watermark;

	/* Sample width is derived from the format mask */
	lo = params->masks[SNDRV_PCM_HW_PARAM_FORMAT -
			   SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0];
	hi = params->masks[SNDRV_PCM_HW_PARAM_FORMAT -
			   SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[1];
	code = lo ? __ffs(lo) : (32 + __ffs(hi));

	switch (code) {
	case 2:		/* SNDRV_PCM_FORMAT_S16_LE */
		width_bits = 2;
		src_step = playback ? 2 : 0;
		des_step = playback ? 0 : 2;
		rate = 16;
		break;
	case 6:		/* SNDRV_PCM_FORMAT_S24_LE */
	case 10:	/* SNDRV_PCM_FORMAT_S32_LE */
		width_bits = 4;
		src_step = playback ? 4 : 0;
		des_step = playback ? 0 : 4;
		rate = 32;
		break;
	default:
		printk(KERN_ERR "%s: unsupported format %d\n",
		       "sprd_agcp_i2s_hw_params", code);
		return -524;	/* -EREMOTEIO */
	}

	dma->datawidth = width_bits;
	dma->src_step = src_step;
	dma->des_step = des_step;

	printk(KERN_INFO "%s: format %d, width %d, src step %d, des step %d\n",
	       "sprd_agcp_i2s_hw_params", code, width_bits, src_step,
	       des_step);

	dma->phys = (u32)priv->memphys;

	snd_soc_dai_set_dma_data(dai, substream, dma);

	ret = sprd_agcp_i2s_config_param(priv, params_rate(params),
					 params_channels(params), rate);
	if (ret)
		printk(KERN_ERR "%s: sprd_agcp_i2s_config_param() failed\n",
		       "sprd_agcp_i2s_hw_params");

	return ret;
}

static int sprd_agcp_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct i2s_priv *priv = dev_get_drvdata(dai->dev);
	unsigned int bit = 1 << cmd;

	if (cmd > SNDRV_PCM_TRIGGER_DRAIN)
		return -EINVAL;

	/* STOP / PAUSE_RELEASE / RESUME */
	if (bit & 0x29) {
		sprd_agcp_i2s_dma_enable(priv, 0);
		return 0;
	}

	/* START / SUSPEND / DRAIN */
	if (bit & 0x52) {
		sprd_agcp_i2s_dma_enable(priv, 1);
		return 0;
	}

	return -EINVAL;
}

static int sprd_agcp_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_dai_driver *drv = dai->driver;
	struct snd_soc_dapm_route route;
	struct i2s_priv *priv = dev_get_drvdata(dai->dev);

	memset(&route, 0, sizeof(route));

	if (drv->playback.stream_name && drv->playback.aif_name) {
		route.sink = drv->playback.stream_name;
		route.source = drv->playback.aif_name;
		dev_dbg(dai->dev, "%s add route for widget %s", __func__,
			drv->playback.aif_name);
		snd_soc_dapm_add_routes(&dai->component->dapm, &route, 1);
	}

	if (drv->capture.stream_name && drv->capture.aif_name) {
		route.sink = drv->capture.stream_name;
		route.source = drv->capture.aif_name;
		dev_dbg(dai->dev, "%s add route for widget %s", __func__,
			drv->capture.aif_name);
		snd_soc_dapm_add_routes(&dai->component->dapm, &route, 1);
	}

	(void)priv;
	return 0;
}

static const struct snd_soc_dai_ops sprd_agcp_i2s_dai_ops = {
	.startup	= sprd_agcp_i2s_startup,
	.shutdown	= sprd_agcp_i2s_shutdown,
	.hw_params	= sprd_agcp_i2s_hw_params,
	.trigger	= sprd_agcp_i2s_trigger,
};

static struct snd_soc_dai_driver sprd_agcp_i2s_dai = {
	.name		= "AGCP_IIS0_TX",
	.id		= SPRD_I2S_PORT_MIN,
	.probe		= sprd_agcp_i2s_dai_probe,
	.ops		= &sprd_agcp_i2s_dai_ops,
	.playback	= {
		.stream_name	= "AGCP_IIS0_TX Playback",
		.formats	= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.channels_min	= 1,
		.channels_max	= 8,
		.aif_name	= "AGCP_IIS0_TX",
	},
};

static const struct snd_soc_component_driver sprd_agcp_i2s_component = {
	.name		= "sprd-agcp-i2s",
};

static int sprd_agcp_i2s_parse_dt(struct platform_device *pdev,
				  struct i2s_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	const struct sprd_i2s_config *def;
	u32 val;
	int ret;

	val = 0;
	ret = of_property_read_variable_u32_array(np, "sprd,tx-dma-no", &val,
						  1, NULL);
	if (ret < 0) {
		printk(KERN_ERR "%s: sprd,tx-dma-no not found\n",
		       "sprd_agcp_i2s_parse_dt");
		return ret;
	}
	priv->tx_dma_no = val;
	pr_debug("%s: tx_dma_no = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->tx_dma_no);

	ret = of_property_read_variable_u32_array(np, "sprd,rx-dma-no", &val,
						  1, NULL);
	if (ret < 0) {
		printk(KERN_ERR "%s: sprd,rx-dma-no not found\n",
		       "sprd_agcp_i2s_parse_dt");
		return ret;
	}
	priv->rx_dma_no = val;
	pr_debug("%s: rx_dma_no = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->rx_dma_no);

	if (of_property_read_variable_u32_array(np, "sprd,div-mode", &val,
						1, NULL)) {
		printk(KERN_WARNING "%s: sprd,div-mode not found\n",
		       "sprd_agcp_i2s_parse_dt");
		val = 0;
	}
	priv->div_mode = val;
	pr_debug("%s: div_mode = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->div_mode);

	if (of_property_read_variable_u32_array(np, "sprd,rate-multi", &val,
						1, NULL)) {
		printk(KERN_WARNING "%s: sprd,rate-multi not found\n",
		       "sprd_agcp_i2s_parse_dt");
		val = 1;
	}
	priv->rate_multi = val;
	pr_debug("%s: rate_multi = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->rate_multi);

	if (of_property_read_variable_u32_array(np, "sprd,fifo-depth", &val,
						1, NULL)) {
		printk(KERN_WARNING "%s: sprd,fifo-depth not found\n",
		       "sprd_agcp_i2s_parse_dt");
		val = 0x100;
	}
	priv->fifo_depth = val;
	pr_debug("%s: fifo_depth = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->fifo_depth);

	ret = of_property_read_variable_u32_array(np, "sprd,bus-type", &val,
						  1, NULL);
	if (ret < 0) {
		printk(KERN_ERR "%s: sprd,bus-type not found\n",
		       "sprd_agcp_i2s_parse_dt");
		return ret;
	}
	pr_debug("%s: bus_type = %u\n", "sprd_agcp_i2s_parse_dt", val);

	/* I2S and PCM ports have different default settings */
	def = val ? &default_agcp_pcm_config : &default_agcp_i2s_config;
	memcpy(&priv->cfg, def, sizeof(priv->cfg));
	priv->cfg.bus_type = val;

	if (of_property_read_variable_u32_array(np, "sprd,hw-port", &val, 1,
						NULL))
		val = priv->cfg.hw_port;
	priv->cfg.hw_port = val;
	pr_debug("%s: hw_port = %u\n", "sprd_agcp_i2s_parse_dt",
		 priv->cfg.hw_port);

	if (of_property_read_variable_u32_array(np, "sprd,work-mode", &val, 1,
						NULL))
		val = priv->cfg.work_mode;
	priv->cfg.work_mode = val;
	pr_debug("%s: work_mode = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,trans-mode", &val, 1,
						NULL))
		val = priv->cfg.trans_mode;
	priv->cfg.trans_mode = val;
	pr_debug("%s: trans_mode = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,rtx-mode", &val, 1,
						NULL))
		val = priv->cfg.rtx_mode;
	priv->cfg.rtx_mode = val;
	pr_debug("%s: rtx_mode = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,sync-mode", &val, 1,
						NULL))
		val = priv->cfg.sync_mode;
	priv->cfg.sync_mode = val;
	pr_debug("%s: sync_mode = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,bus-frame-mode", &val,
						1, NULL))
		val = priv->cfg.bus_frame_mode;
	priv->cfg.bus_frame_mode = val;
	pr_debug("%s: bus_frame_mode = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,multi-ch", &val, 1,
						NULL))
		val = priv->cfg.multi_ch;
	priv->cfg.multi_ch = val;
	pr_debug("%s: multi_ch = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,left-ch-lvl", &val, 1,
						NULL))
		val = priv->cfg.left_ch_lvl;
	priv->cfg.left_ch_lvl = val;
	pr_debug("%s: left_ch_lvl = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,clk-inv", &val, 1,
						NULL))
		val = priv->cfg.clk_inv;
	priv->cfg.clk_inv = val;
	pr_debug("%s: clk_inv = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,tx-watermark", &val,
						1, NULL))
		val = priv->cfg.tx_watermark;
	priv->cfg.tx_watermark = val;
	pr_debug("%s: tx_watermark = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,rx-watermark", &val,
						1, NULL))
		val = priv->cfg.rx_watermark;
	priv->cfg.rx_watermark = val;
	pr_debug("%s: rx_watermark = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,slave-timeout", &val,
						1, NULL))
		val = priv->cfg.slave_timeout;
	priv->cfg.slave_timeout = val;
	pr_debug("%s: slave_timeout = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,pcm-slot", &val, 1,
						NULL))
		val = priv->cfg.pcm_slot;
	priv->cfg.pcm_slot = val;
	pr_debug("%s: pcm_slot = %u\n", "sprd_agcp_i2s_parse_dt", val);

	if (of_property_read_variable_u32_array(np, "sprd,pcm-cycle", &val, 1,
						NULL))
		val = priv->cfg.pcm_cycle;
	priv->cfg.pcm_cycle = val;
	pr_debug("%s: pcm_cycle = %u\n", "sprd_agcp_i2s_parse_dt", val);

	return 0;
}

static int sprd_agcp_i2s_probe(struct platform_device *pdev)
{
	struct i2s_priv *priv = platform_get_drvdata(pdev);
	struct resource *res;
	void __iomem *clk;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(&pdev->dev, "%s: devm_ioremap_resource() failed\n",
			"sprd_agcp_i2s_probe");
		return -EINVAL;
	}

	priv->enable = 0;
	priv->memphys = res->start;
	priv->reg_size = res->end - res->start + 1;

	/*
	 * The AUDCP clock controller is mapped by hand, right after the
	 * I2S controller, and the vendor only checks priv->base here.
	 */
	clk = ioremap(0x56250000, 0x3000);
	priv->clk_membase = clk;
	if (IS_ERR(priv->base)) {
		dev_err(&pdev->dev, "%s: ioremap() clk_membase failed\n",
			"sprd_agcp_i2s_probe");
		return -EINVAL;
	}

	ret = sprd_agcp_i2s_parse_dt(pdev, priv);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: sprd_agcp_i2s_parse_dt() failed\n",
			"sprd_agcp_i2s_probe");
		return ret;
	}

	atomic_set(&priv->open_cnt, 0);
	printk(KERN_INFO "%s: membase(%pK) memphys(%pK) dma number tx(%d) rx(%d)\n",
	       "sprd_agcp_i2s_probe", priv->base, (void *)(uintptr_t)priv->memphys,
	       priv->tx_dma_no, priv->rx_dma_no);

	ret = snd_soc_register_component(&pdev->dev, &sprd_agcp_i2s_component,
					 &sprd_agcp_i2s_dai, 1);
	return ret;
}

static int sprd_dai_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct i2s_priv *priv;
	struct regmap *map;
	int ret;

	ret = aud_ipc_ch_open(AMSG_CH_MISC);
	if (ret < 0) {
		printk(KERN_ERR "Failed to open AMSG_CH_MISC channel\n");
		return ret;
	}

	if (!arch_audio_get_agcp_ahb_gpr()) {
		map = syscon_regmap_lookup_by_phandle(np,
						      "sprd,syscon-agcp-ahb");
		if (IS_ERR(map)) {
			ret = -EPROBE_DEFER;
			goto out;
		}
		arch_audio_set_agcp_ahb_gpr(map);
	}

	priv = devm_kmalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out;
	}

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	ret = of_property_read_variable_u32_array(np, "sprd,id", &priv->port_id,
						  1, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: failed to get port id\n",
			"sprd_dai_probe");
		goto out;
	}

	if ((u32)(priv->port_id - SPRD_I2S_PORT_MIN) >
	    (SPRD_I2S_PORT_MAX - SPRD_I2S_PORT_MIN)) {
		dev_err(&pdev->dev, "%s: unknown port id (%u)\n",
			"sprd_dai_probe", priv->port_id);
		ret = -EINVAL;
		goto out;
	}

	ret = sprd_agcp_i2s_probe(pdev);
out:
	return ret;
}

static int sprd_dai_remove(struct platform_device *pdev)
{
	struct i2s_priv *priv = platform_get_drvdata(pdev);
	int ret;

	if ((u32)(priv->port_id - SPRD_I2S_PORT_MIN) >
	    (SPRD_I2S_PORT_MAX - SPRD_I2S_PORT_MIN)) {
		dev_err(&pdev->dev, "%s: unknown port id (%u)\n",
			"sprd_dai_remove", priv->port_id);
		return -EINVAL;
	}

	iounmap(priv->clk_membase);
	snd_soc_unregister_component(&pdev->dev);

	ret = aud_ipc_ch_close(AMSG_CH_MISC);
	if (ret < 0) {
		printk(KERN_ERR "Failed to close AMSG_CH_MISC channel\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id sprd_dai_of_match[] = {
	{ .compatible = "sprd,agcp-i2s-dai" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_dai_of_match);

static struct platform_driver sprd_dai_driver = {
	.probe	= sprd_dai_probe,
	.remove	= sprd_dai_remove,
	.driver	= {
		.name		= "sprd-dai",
		.of_match_table	= sprd_dai_of_match,
	},
};

static int __init sprd_dai_driver_init(void)
{
	return platform_driver_register(&sprd_dai_driver);
}

static void __exit sprd_dai_driver_exit(void)
{
	platform_driver_unregister(&sprd_dai_driver);
}

module_init(sprd_dai_driver_init);
module_exit(sprd_dai_driver_exit);

MODULE_AUTHOR("Bin Pang <bin.pang@unisoc.com>");
MODULE_DESCRIPTION("SPRD ASoC DAI driver");
MODULE_LICENSE("GPL");
