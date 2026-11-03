// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc MCDT (multi channel data transfer) audio DMA driver, r2p0.
 *
 * Reconstructed from the vendor mcdt_hw_r2p0.ko shipped in boot_a.bin:
 *   .text 0x50e4 (20708 B), 27 functions, .rodata 0x7c8, .data 0xf0,
 *   .bss 0x3c0, .init.text 0x2c, .exit.text 0x24.
 *
 * The block sits behind the AGCP (audio DSP power domain).  All the register
 * accesses go through an "AGCP accessible" check: the agcp ahb syscon is read
 * and bit 12 tells whether the MCDT register window can be touched.  When it
 * is not set the accessors complain with
 *      "<helper> agcp mcdt clocl not available"
 * and either return 0 (reads) or drop the write.
 *
 * Register map (offsets from membase):
 *   0x000 + ch*4    DAC FIFO data port        (0x2c + ch*4 for ADC)
 *   0x060 + ch*4    DAC channel ctrl: watermark, low 9 bits forced to 1
 *   0x08c + ch*4    ADC channel ctrl: same layout
 *   0x0b0           channel enable bits: DAC bit (ch+16), ADC bit ch
 *   0x0b4/8/c        DAC FIFO interrupt enable   (ch<4 / ch<8 / ch>=8)
 *   0x0c0/4/8        DAC FIFO interrupt status   (same grouping)
 *   0x0e4 + ch*4    DAC buffer write/read pointers
 *   0x110 + ch*4    ADC buffer write/read pointers
 *   0x134/8/c        DAC FIFO status group (ch<4 / ch<8 / ch>=10)
 *   0x140           int-to-AP enable bits, one per channel
 *   0x14c           AP DMA request source select, 4 bits per channel
 *   0x158           FIFO flush: DAC bit ch, ADC bit (ch+16)
 *   0x15c / 0x160   DAC DMA acknowledge select (ch<8 / ch>=8)
 *
 * Copyright (c) 2021 Unisoc (Shanghai) Technologies Co., Ltd.
 */

#define pr_fmt(fmt) "[MCDT] " fmt

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define MCDT_DAC_CHANNEL_NUM	11
#define MCDT_ADC_CHANNEL_NUM	10
#define MCDT_DAC_DMA_NUM	7
#define MCDT_ADC_DMA_NUM	5
#define MCDT_BUF_PTR_MASK	0x3ff

/* per channel runtime state */
struct mcdt_chan {
	u32 state;	/* 1 once the interrupts are armed */
	u32 enabled;	/* 1 once the DMA channel is running */
	u32 dma_ch;	/* AP DMA channel handed out by dma_map[] */
	u32 reserved;
};

/* interrupt descriptor per channel, 0x18 bytes each */
struct mcdt_irq_desc {
	u64 arg0;
	u64 arg1;
	u64 arg2;
};

static u64 memphys;
static u64 mem_dma_phys;
static void __iomem *membase;
static struct regmap *agcp_ahb_gpr;
static spinlock_t mcdt_lock;
static struct mutex mcdt_mutex;

static struct mcdt_chan g_dac_channel[MCDT_DAC_CHANNEL_NUM];
static struct mcdt_chan g_adc_channel[MCDT_ADC_CHANNEL_NUM];
static u32 mcdt_dac_dma_channel[MCDT_DAC_DMA_NUM];
static u32 mcdt_adc_dma_channel[MCDT_ADC_DMA_NUM];

static struct mcdt_irq_desc gdac_irq_desc[MCDT_DAC_CHANNEL_NUM];
static struct mcdt_irq_desc gadc_irq_desc[MCDT_ADC_CHANNEL_NUM];

/* logical channel -> AP DMA request, from .rodata+0x670 */
static const u32 dma_map[MCDT_DAC_CHANNEL_NUM] = {
	0, 1, 2, 3, 4, 0, 0, 0, 0, 0, 5,
};

/* bit position of the FIFO status / interrupt bits, one field per channel
 * inside a 32 bit group register; the low nibble is fixed by the group.
 */
static const u32 dac_fifo_sts_shift[MCDT_DAC_CHANNEL_NUM] = {
	4, 12, 20, 28, 4, 12, 20, 28, 4, 12, 20,
};
static const u32 dac_fifo_empty_shift[MCDT_DAC_CHANNEL_NUM] = {
	5, 13, 21, 29, 5, 13, 21, 29, 5, 13, 21,
};
static const u32 dac_fifo_int_shift[MCDT_DAC_CHANNEL_NUM] = {
	2, 10, 18, 26, 2, 10, 18, 26, 2, 10, 18,
};
static const u32 adc_fifo_sts_shift[MCDT_ADC_CHANNEL_NUM] = {
	1, 9, 17, 25, 1, 9, 17, 25, 1, 9,
};
static const u32 adc_fifo_empty_shift[MCDT_ADC_CHANNEL_NUM] = {
	1, 9, 17, 25, 1, 9, 17, 25, 1, 9, 17,
};
static const u32 adc_fifo_full_shift[MCDT_ADC_CHANNEL_NUM] = {
	8, 16, 24, 0, 8, 16, 24, 0, 8, 16,
};

/* DAC FIFO status register group for a channel */
static inline u32 mcdt_dac_group(u32 ch)
{
	return ch < 4 ? 0x134 : (ch < 8 ? 0x138 : 0x13c);
}

/* ADC FIFO status register group for a channel (same layout) */
static inline u32 mcdt_dma_ack_reg(u32 ch)
{
	return ch < 8 ? 0x15c : 0x160;
}

/*
 * AGCP accessibility check.  Returns false (and complains) when the MCDT
 * clock is gated.  When no syscon was provided in DT the window is assumed
 * to be accessible, matching the vendor binary.
 */
static inline bool mcdt_agcp_ready(const char *name)
{
	u32 val = 0;

	if (!agcp_ahb_gpr)
		return true;
	if (regmap_read(agcp_ahb_gpr, 0, &val))
		return true;
	if (val & BIT(12))
		return true;
	pr_err("%s agcp mcdt clocl not available\n", name);
	return false;
}

static inline u32 mcdt_reg_read(u32 off)
{
	if (!mcdt_agcp_ready("mcdt_reg_read"))
		return 0;
	return readl_relaxed(membase + off);
}

static inline void mcdt_reg_raw_write(u32 off, u32 val)
{
	if (!mcdt_agcp_ready("mcdt_reg_raw_write"))
		return;
	writel_relaxed(val, membase + off);
}

static inline void mcdt_reg_update(u32 off, u32 mask, u32 val)
{
	u32 old;

	if (!mcdt_agcp_ready("mcdt_reg_update"))
		return;
	old = readl_relaxed(membase + off);
	old &= ~mask;
	old |= val;
	writel_relaxed(old, membase + off);
}

static inline int mcdt_is_chan_fifo_sts(u32 off, u32 shift)
{
	if (!mcdt_agcp_ready("mcdt_is_chan_fifo_sts"))
		return 0;
	return (readl_relaxed(membase + off) >> shift) & 1;
}

static inline void mcdt_dac_phy_write(u32 off, u32 val)
{
	if (!mcdt_agcp_ready("mcdt_dac_phy_write"))
		return;
	writel_relaxed(val, membase + off);
}

static inline u32 mcdt_adc_phy_read(u32 off)
{
	if (!mcdt_agcp_ready("mcdt_adc_phy_read"))
		return 0;
	return readl_relaxed(membase + off);
}

/* watermark programming, DAC flavour */
static inline void mcdt_da_set_watermark(u32 ch, u32 watermark)
{
	u32 off = 0x60 + ch * 4;
	u32 old;

	if (!mcdt_agcp_ready("mcdt_da_set_watermark"))
		return;
	old = readl_relaxed(membase + off);
	old &= 0xfe00fe00;
	old |= ((watermark & 0x1ff) << 16) | 0x1ff;
	writel_relaxed(old, membase + off);
}

/* watermark programming, ADC flavour: watermark in the low half */
static inline void mcdt_ad_set_watermark(u32 ch, u32 watermark)
{
	u32 off = 0x8c + ch * 4;
	u32 old;

	if (!mcdt_agcp_ready("mcdt_ad_set_watermark"))
		return;
	old = readl_relaxed(membase + off);
	old &= 0xfe00fe00;
	old |= (watermark & 0x1ff) | 0x1ff0000;
	writel_relaxed(old, membase + off);
}

/* DAC DMA request enable bit, ch+16 */
static inline void mcdt_da_dma_enable(u32 ch)
{
	u32 old;

	if (!mcdt_agcp_ready("mcdt_da_dma_enable"))
		return;
	old = readl_relaxed(membase + 0xb0);
	writel_relaxed(old | BIT(ch + 16), membase + 0xb0);
}

/* ADC DMA request enable bit, ch */
static inline void mcdt_ad_dma_enable(u32 ch)
{
	u32 old;

	if (!mcdt_agcp_ready("mcdt_ad_dma_enable"))
		return;
	old = readl_relaxed(membase + 0xb0);
	writel_relaxed(old | BIT(ch), membase + 0xb0);
}

/* FIFO interrupt enable for a channel: one bit per field in 0xb4/0xb8/0xbc */
static inline void mcdt_chan_fifo_int_enable(u32 ch)
{
	u32 off = ch < 4 ? 0xb4 : (ch < 8 ? 0xb8 : 0xbc);
	u32 shift = dac_fifo_int_shift[ch];
	u32 old;

	if (!mcdt_agcp_ready("mcdt_chan_fifo_int_enable"))
		return;
	old = readl_relaxed(membase + off);
	writel_relaxed(old | BIT(shift), membase + off);
}

/* FIFO interrupt acknowledge, same grouping but at 0xc0/0xc4/0xc8 */
static inline void mcdt_chan_fifo_int_clr(u32 ch)
{
	u32 off = ch < 4 ? 0xc0 : (ch < 8 ? 0xc4 : 0xc8);
	u32 shift = dac_fifo_int_shift[ch];
	u32 old;

	if (!mcdt_agcp_ready("mcdt_chan_fifo_int_clr"))
		return;
	old = readl_relaxed(membase + off);
	writel_relaxed(old | BIT(shift), membase + off);
}

/* route the channel interrupt to the AP */
static inline void mcdt_int_to_ap_enable(u32 ch)
{
	u32 old;

	if (!mcdt_agcp_ready("mcdt_int_to_ap_enable"))
		return;
	old = readl_relaxed(membase + 0x140);
	writel_relaxed(old | BIT(ch), membase + 0x140);
}

/* clear the FIFO interrupt enable bit of a channel (same registers as the
 * enable helper, the vendor binary reports it under the same name)
 */
static inline void mcdt_chan_fifo_int_enable_disable(u32 ch)
{
	u32 off = ch < 4 ? 0xb4 : (ch < 8 ? 0xb8 : 0xbc);
	u32 shift = dac_fifo_int_shift[ch];
	u32 old;

	if (!mcdt_agcp_ready("mcdt_chan_fifo_int_enable"))
		return;
	old = readl_relaxed(membase + off);
	writel_relaxed(old & ~BIT(shift), membase + off);
}

/* stop routing the channel interrupt to the AP */
static inline void mcdt_int_to_ap_disable(u32 ch)
{
	u32 old;

	if (!mcdt_agcp_ready("mcdt_int_to_ap_enable"))
		return;
	old = readl_relaxed(membase + 0x140);
	writel_relaxed(old & ~BIT(ch), membase + 0x140);
}

/* DAC DMA acknowledge select, one field per DMA channel */
static inline void mcdt_dac_dma_chan_ack_sel(u32 dma_ch)
{
	u32 shift = 4 + 4 * dma_ch;
	u32 mask = 0xf << shift;

	if (!mcdt_agcp_ready("mcdt_dac_dma_chan_ack_sel"))
		return;
	mcdt_reg_update(0x14c, mask, (5 + dma_ch) << shift);
}

static inline void mcdt_adc_dma_chan_ack_sel(u32 dma_ch)
{
	u32 shift = 12 + 4 * dma_ch;
	u32 mask = 0xf << shift;

	if (!mcdt_agcp_ready("mcdt_adc_dma_chan_ack_sel"))
		return;
	mcdt_reg_update(0x14c, mask, (5 + dma_ch) << shift);
}

#define MCDT_DAC_SEL(n)							\
static inline void mcdt_ap_dac_dma_ch##n##_sel(u32 ch)			\
{									\
	if (!mcdt_agcp_ready("mcdt_ap_dac_dma_ch" #n "_sel"))		\
		return;							\
	mcdt_dac_dma_chan_ack_sel(n);					\
}

#define MCDT_ADC_SEL(n)							\
static inline void mcdt_ap_adc_dma_ch##n##_sel(u32 ch)			\
{									\
	if (!mcdt_agcp_ready("mcdt_ap_adc_dma_ch" #n "_sel"))		\
		return;							\
	mcdt_adc_dma_chan_ack_sel(n);					\
}

MCDT_DAC_SEL(0)
MCDT_DAC_SEL(1)
MCDT_DAC_SEL(2)
MCDT_DAC_SEL(3)
MCDT_DAC_SEL(4)
MCDT_DAC_SEL(5)
MCDT_ADC_SEL(0)
MCDT_ADC_SEL(1)
MCDT_ADC_SEL(2)
MCDT_ADC_SEL(3)
MCDT_ADC_SEL(4)

/**
 * mcdt_dac_phy_addr() - physical address of a DAC FIFO port
 * @ch: logical channel
 */
u64 mcdt_dac_phy_addr(u32 ch)
{
	return memphys + ch * 4;
}
EXPORT_SYMBOL(mcdt_dac_phy_addr);

/**
 * mcdt_dac_dma_phy_addr() - physical address of a DAC DMA buffer
 * @ch: logical channel
 */
u64 mcdt_dac_dma_phy_addr(u32 ch)
{
	return mem_dma_phys + ch * 4;
}
EXPORT_SYMBOL(mcdt_dac_dma_phy_addr);

/**
 * mcdt_adc_phy_addr() - physical address of an ADC FIFO port
 * @ch: logical channel
 */
u64 mcdt_adc_phy_addr(u32 ch)
{
	return memphys + ch * 4 + 0x2c;
}
EXPORT_SYMBOL(mcdt_adc_phy_addr);

/**
 * mcdt_adc_dma_phy_addr() - physical address of an ADC DMA buffer
 * @ch: logical channel
 */
u64 mcdt_adc_dma_phy_addr(u32 ch)
{
	return mem_dma_phys + ch * 4 + 0x2c;
}
EXPORT_SYMBOL(mcdt_adc_dma_phy_addr);

/**
 * mcdt_write() - push samples into a DAC FIFO
 * @ch: logical channel
 * @buf: samples to write
 * @bytes: number of bytes, rounded down to a whole number of samples
 *
 * Waits for the FIFO to report a free slot (bit set in the channel status
 * register) before each sample is pushed.
 */
int mcdt_write(u32 ch, const u32 *buf, u32 bytes)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = dac_fifo_sts_shift[ch];
	u32 words = bytes >> 2;
	u32 i;

	if (!words)
		return 0;

	for (i = 0; i < words; i++) {
		while (!mcdt_is_chan_fifo_sts(group, shift))
			usleep_range(10, 15);
		mcdt_dac_phy_write(ch * 4, buf[i]);
	}

	return 0;
}

/**
 * mcdt_read() - pull samples from an ADC FIFO
 * @ch: logical channel
 * @buf: destination buffer
 * @bytes: number of bytes, rounded down to a whole number of samples
 */
int mcdt_read(u32 ch, u32 *buf, u32 bytes)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = dac_fifo_sts_shift[ch];
	u32 words = bytes >> 2;
	u32 i;

	if (!words)
		return -1;

	for (i = 0; i < words; i++) {
		while (ch <= MCDT_ADC_CHANNEL_NUM &&
		       !mcdt_is_chan_fifo_sts(group, shift))
			usleep_range(10, 15);
		buf[i] = mcdt_adc_phy_read(ch * 4 + 0x2c);
	}

	return 0;
}

/**
 * mcdt_is_adc_full() - ADC FIFO full flag
 * @ch: logical channel, 1 based here (the binary indexes with ch - 1)
 */
int mcdt_is_adc_full(u32 ch)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = adc_fifo_full_shift[ch - 1];

	return mcdt_is_chan_fifo_sts(group, shift);
}

/**
 * mcdt_is_adc_empty() - ADC FIFO empty flag
 * @ch: logical channel
 */
int mcdt_is_adc_empty(u32 ch)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = adc_fifo_empty_shift[ch];

	return mcdt_is_chan_fifo_sts(group, shift);
}

/**
 * mcdt_is_dac_full() - DAC FIFO full flag
 * @ch: logical channel
 */
int mcdt_is_dac_full(u32 ch)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = dac_fifo_sts_shift[ch];

	return mcdt_is_chan_fifo_sts(group, shift);
}

/**
 * mcdt_is_dac_empty() - DAC FIFO empty flag
 * @ch: logical channel
 */
int mcdt_is_dac_empty(u32 ch)
{
	u32 group = mcdt_dac_group(ch);
	u32 shift = dac_fifo_empty_shift[ch];

	return mcdt_is_chan_fifo_sts(group, shift);
}

/**
 * mcdt_dac_buffer_size_avail() - bytes the DSP can still take for a channel
 * @ch: logical channel
 *
 * The write and read pointers live in 0xe4 + ch*4, ten bits each; the ring
 * holds 512 samples.
 */
u32 mcdt_dac_buffer_size_avail(u32 ch)
{
	u32 off = 0xe4 + ch * 4;
	u32 wr, rd, diff, wrap;

	wr = (mcdt_reg_read(off) >> 16) & MCDT_BUF_PTR_MASK;
	rd = mcdt_reg_read(off) & MCDT_BUF_PTR_MASK;
	diff = wr - rd;
	wrap = wr < rd;
	return (diff + (wrap << 9)) << 2;
}

/**
 * mcdt_adc_data_size_avail() - bytes of captured data pending for a channel
 * @ch: logical channel
 */
u32 mcdt_adc_data_size_avail(u32 ch)
{
	u32 off = 0x110 + ch * 4;
	u32 wr, rd, diff, wrap;

	wr = (mcdt_reg_read(off) >> 16) & MCDT_BUF_PTR_MASK;
	rd = mcdt_reg_read(off) & MCDT_BUF_PTR_MASK;
	diff = rd - wr;
	wrap = rd <= wr;
	return (diff + (wrap << 9)) << 2;
}

/**
 * mcdt_dac_dma_enable() - allocate an AP DMA channel and start a DAC channel
 * @ch: logical channel
 * @watermark: FIFO watermark, 9 bits
 *
 * Only channels 0..4 and 10 can be routed to a DMA request.
 */
int mcdt_dac_dma_enable(u32 ch, u32 watermark)
{
	u32 dma_ch;

	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;
	if (g_dac_channel[ch].state == 1)
		return -1;
	if (!((0x41f >> ch) & 1)) {
		pr_err("adc no dma id for :%d\n", ch);
		pr_err("dma_channel error:%d\n", -EINVAL);
		return -EINVAL;
	}

	dma_ch = dma_map[ch];

	mutex_lock(&mcdt_mutex);
	if (mcdt_dac_dma_channel[dma_ch]) {
		pr_err("dma_channel is already used:%d\n", dma_ch);
		mutex_unlock(&mcdt_mutex);
		return -EBUSY;
	}
	mcdt_dac_dma_channel[dma_ch] = 1;
	mutex_unlock(&mcdt_mutex);

	mcdt_dac_phy_addr(ch);

	mcdt_da_set_watermark(ch, watermark);
	mcdt_da_dma_enable(ch);

	if (dma_ch > 5)
		return -1;

	switch (dma_ch) {
	case 0:
		mcdt_ap_dac_dma_ch0_sel(ch);
	case 1:
		mcdt_ap_dac_dma_ch1_sel(ch);
	case 2:
		mcdt_ap_dac_dma_ch2_sel(ch);
	case 3:
		mcdt_ap_dac_dma_ch3_sel(ch);
	case 4:
		mcdt_ap_dac_dma_ch4_sel(ch);
	case 5:
		mcdt_ap_dac_dma_ch5_sel(ch);
	}

	pr_info("%s %d mcdt_dma_ap_channel=%d\n", __func__, 1001, dma_ch);
	mcdt_reg_raw_write(0xb0, 1);
	g_dac_channel[ch].dma_ch = dma_ch;

	return 0;
}
EXPORT_SYMBOL(mcdt_dac_dma_enable);

/**
 * mcdt_dac_dma_disable() - release the AP DMA channel of a DAC channel
 * @ch: logical channel
 */
int mcdt_dac_dma_disable(u32 ch)
{
	u32 dma_ch;

	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;
	if (!g_dac_channel[ch].enabled)
		return 0;

	mcdt_reg_update(0xb0, BIT(ch + 16), 0);
	mcdt_dac_phy_addr(ch);

	mutex_lock(&mcdt_mutex);
	dma_ch = g_dac_channel[ch].dma_ch;
	if (dma_ch > 6) {
		mutex_unlock(&mcdt_mutex);
		return -1;
	}
	mcdt_dac_dma_channel[dma_ch] = 0;
	mutex_unlock(&mcdt_mutex);

	g_dac_channel[ch].enabled = 0;
	g_dac_channel[ch].dma_ch = 0;

	return 0;
}
EXPORT_SYMBOL(mcdt_dac_dma_disable);

/**
 * mcdt_da_fifo_clr() - flush a DAC FIFO
 * @ch: logical channel
 */
int mcdt_da_fifo_clr(u32 ch)
{
	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;

	if (!mcdt_agcp_ready("mcdt_da_fifo_clr"))
		return 0;
	mcdt_reg_update(0x158, BIT(ch), BIT(ch));

	return 0;
}
EXPORT_SYMBOL(mcdt_da_fifo_clr);

/**
 * mcdt_adc_dma_enable() - allocate an AP DMA channel and start an ADC channel
 * @ch: logical channel
 * @watermark: FIFO watermark, 9 bits
 */
int mcdt_adc_dma_enable(u32 ch, u32 watermark)
{
	u32 dma_ch;

	if (ch >= MCDT_ADC_CHANNEL_NUM)
		return -1;
	if (g_adc_channel[ch].state == 1)
		return -1;
	if (ch >= 5) {
		pr_err("adc no dma id for :%d\n", ch);
		pr_err("adc dma_channel error:%d\n", -EINVAL);
		return -EINVAL;
	}

	dma_ch = dma_map[ch];

	mutex_lock(&mcdt_mutex);
	if (mcdt_adc_dma_channel[dma_ch]) {
		pr_err("adc dma_channel is already used:%d\n", dma_ch);
		mutex_unlock(&mcdt_mutex);
		return -EBUSY;
	}
	mcdt_adc_dma_channel[dma_ch] = 1;
	mutex_unlock(&mcdt_mutex);

	mcdt_adc_phy_addr(ch);

	mcdt_ad_set_watermark(ch, watermark);
	mcdt_ad_dma_enable(ch);

	if (dma_ch > 4)
		return -1;

	switch (dma_ch) {
	case 0:
		mcdt_ap_adc_dma_ch0_sel(ch);
	case 1:
		mcdt_ap_adc_dma_ch1_sel(ch);
	case 2:
		mcdt_ap_adc_dma_ch2_sel(ch);
	case 3:
		mcdt_ap_adc_dma_ch3_sel(ch);
	case 4:
		mcdt_ap_adc_dma_ch4_sel(ch);
	}

	pr_info("%s %d mcdt_dma_ap_channel=%d\n", __func__, 1001, dma_ch);
	mcdt_reg_raw_write(0xb0, 1);
	g_adc_channel[ch].dma_ch = dma_ch;

	return 0;
}
EXPORT_SYMBOL(mcdt_adc_dma_enable);

/**
 * mcdt_adc_dma_disable() - release the AP DMA channel of an ADC channel
 * @ch: logical channel
 */
int mcdt_adc_dma_disable(u32 ch)
{
	u32 dma_ch;

	if (ch >= MCDT_ADC_CHANNEL_NUM)
		return -1;
	if (!g_adc_channel[ch].enabled)
		return 0;

	mcdt_reg_update(0xb0, BIT(ch), 0);
	mcdt_adc_phy_addr(ch);

	mutex_lock(&mcdt_mutex);
	dma_ch = g_adc_channel[ch].dma_ch;
	if (dma_ch > 4) {
		mutex_unlock(&mcdt_mutex);
		return -1;
	}
	mcdt_adc_dma_channel[dma_ch] = 0;
	mutex_unlock(&mcdt_mutex);

	g_adc_channel[ch].enabled = 0;
	g_adc_channel[ch].dma_ch = 0;

	return 0;
}
EXPORT_SYMBOL(mcdt_adc_dma_disable);

/**
 * mcdt_ad_fifo_clr() - flush an ADC FIFO
 * @ch: logical channel
 */
int mcdt_ad_fifo_clr(u32 ch)
{
	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;

	if (!mcdt_agcp_ready("mcdt_ad_fifo_clr"))
		return 0;
	mcdt_reg_update(0x158, BIT(ch + 16), BIT(ch + 16));

	return 0;
}
EXPORT_SYMBOL(mcdt_ad_fifo_clr);

/**
 * mcdt_dac_int_enable() - arm the interrupts of a DAC channel
 * @ch: logical channel
 * @watermark: FIFO watermark
 * @arg0: first cookie stored in the channel descriptor
 * @arg1: second cookie, the descriptor is only stored when this one is set
 */
int mcdt_dac_int_enable(u32 ch, u32 watermark, void *arg0, void *arg1)
{
	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;
	if (g_dac_channel[ch].state == 1)
		return -1;
	if (g_dac_channel[ch].enabled == 1)
		return -1;

	mcdt_da_set_watermark(ch, watermark);

	if (arg1) {
		if (ch * sizeof(struct mcdt_irq_desc) > 0x100)
			return -1;
		gdac_irq_desc[ch].arg0 = (u64)arg0;
		gdac_irq_desc[ch].arg1 = (u64)arg1;
	}

	mcdt_chan_fifo_int_enable(ch);

	if (ch >= MCDT_ADC_CHANNEL_NUM)
		return -1;

	if (!g_adc_channel[ch].state)
		mcdt_int_to_ap_enable(ch);

	g_dac_channel[ch].state = 1;

	return 0;
}
EXPORT_SYMBOL(mcdt_dac_int_enable);

/**
 * mcdt_adc_int_enable() - arm the interrupts of an ADC channel
 * @ch: logical channel
 * @watermark: FIFO watermark
 * @arg0: first cookie stored in the channel descriptor
 * @arg1: second cookie
 */
int mcdt_adc_int_enable(u32 ch, u32 watermark, void *arg0, void *arg1)
{
	if (ch >= MCDT_ADC_CHANNEL_NUM)
		return -1;
	if (g_adc_channel[ch].state == 1)
		return -1;
	if (g_adc_channel[ch].enabled == 1)
		return -1;

	mcdt_ad_set_watermark(ch, watermark);

	if (arg1) {
		if (ch * sizeof(struct mcdt_irq_desc) > 0x100)
			return -1;
		gadc_irq_desc[ch].arg0 = (u64)arg0;
		gadc_irq_desc[ch].arg1 = (u64)arg1;
	}

	mcdt_chan_fifo_int_enable(ch);

	if (!g_dac_channel[ch].state)
		mcdt_int_to_ap_enable(ch);

	g_adc_channel[ch].state = 1;

	return 0;
}
EXPORT_SYMBOL(mcdt_adc_int_enable);

/**
 * mcdt_dac_disable() - stop a DAC channel completely
 * @ch: logical channel
 */
int mcdt_dac_disable(u32 ch)
{
	u32 dma_ch;

	if (ch >= MCDT_DAC_CHANNEL_NUM)
		return -1;

	if (g_dac_channel[ch].state) {
		if (ch * sizeof(struct mcdt_irq_desc) > 0x100)
			return -1;
		if (gdac_irq_desc[ch].arg1) {
			mcdt_chan_fifo_int_enable_disable(ch);
			mcdt_chan_fifo_int_clr(ch);
			gdac_irq_desc[ch].arg0 = 0;
			gdac_irq_desc[ch].arg1 = 0;
			if (ch <= 9 && !g_adc_channel[ch].state)
				mcdt_int_to_ap_disable(ch);
		}
		g_dac_channel[ch].state = 0;
	}

	if (ch * sizeof(struct mcdt_chan) > 0xb0)
		return -1;

	if (g_dac_channel[ch].enabled) {
		mcdt_reg_update(0xb0, BIT(ch + 16), 0);
		mutex_lock(&mcdt_mutex);
		dma_ch = g_dac_channel[ch].dma_ch;
		if (dma_ch > 6) {
			mutex_unlock(&mcdt_mutex);
			return -1;
		}
		mcdt_dac_dma_channel[dma_ch] = 0;
		mutex_unlock(&mcdt_mutex);
		g_dac_channel[ch].enabled = 0;
	}

	mcdt_dac_phy_addr(ch);

	return 0;
}
EXPORT_SYMBOL(mcdt_dac_disable);

/**
 * mcdt_adc_disable() - stop an ADC channel completely
 * @ch: logical channel
 */
int mcdt_adc_disable(u32 ch)
{
	u32 dma_ch;

	if (ch >= MCDT_ADC_CHANNEL_NUM)
		return -1;

	if (g_adc_channel[ch].state) {
		if (ch * sizeof(struct mcdt_irq_desc) > 0x100)
			return -1;
		if (gadc_irq_desc[ch].arg1) {
			mcdt_chan_fifo_int_enable_disable(ch);
			mcdt_chan_fifo_int_clr(ch);
			gadc_irq_desc[ch].arg0 = 0;
			gadc_irq_desc[ch].arg1 = 0;
			if (ch <= 9 && !g_dac_channel[ch].state)
				mcdt_int_to_ap_disable(ch);
		}
		g_adc_channel[ch].state = 0;
	}

	if (ch * sizeof(struct mcdt_chan) > 0xa0)
		return -1;

	if (g_adc_channel[ch].enabled) {
		mcdt_reg_update(0xb0, BIT(ch), 0);
		mutex_lock(&mcdt_mutex);
		dma_ch = g_adc_channel[ch].dma_ch;
		if (dma_ch > 4) {
			mutex_unlock(&mcdt_mutex);
			return -1;
		}
		mcdt_adc_dma_channel[dma_ch] = 0;
		mutex_unlock(&mcdt_mutex);
		g_adc_channel[ch].enabled = 0;
	}

	mcdt_adc_phy_addr(ch);

	return 0;
}
EXPORT_SYMBOL(mcdt_adc_disable);

static const struct of_device_id mcdt_of_match[] = {
	{ .compatible = "unisoc,mcdt-r2p0" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcdt_of_match);

static int mcdt_probe(struct platform_device *pdev)
{
	struct device_node *nd = pdev->dev.of_node;
	struct resource *res;
	u32 offset;
	int ret;

	if (!nd) {
		pr_err("ERR: %s, node is NULL!\n", __func__);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("mcdt reg parse error!\n");
		return -EINVAL;
	}

	ret = of_property_read_variable_u32_array(nd, "sprd,ap-addr-offset",
						  &offset, 1, 0);
	if (ret < 0) {
		pr_err("ERR: %s :no property of 'reg'\n", __func__);
		return -EINVAL;
	}

	agcp_ahb_gpr = syscon_regmap_lookup_by_phandle(nd,
						       "sprd,syscon-agcp-ahb");
	if (IS_ERR(agcp_ahb_gpr)) {
		pr_err("ERR: [%s] Get the agcp ahb syscon failed!\n", __func__);
		agcp_ahb_gpr = NULL;
	}

	memphys = res->start;
	mem_dma_phys = res->start - offset;
	membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(membase)) {
		pr_err("ERR:mcdt reg address ioremap_nocache error!\n");
		return -ENOMEM;
	}

	mutex_init(&mcdt_mutex);

	return 0;
}

static struct platform_driver mcdt_driver = {
	.probe = mcdt_probe,
	.driver = {
		.name = "mcdt_sprd",
		.of_match_table = mcdt_of_match,
	},
};

static int __init mcdt_init(void)
{
	return platform_driver_register(&mcdt_driver);
}

static void __exit mcdt_exit(void)
{
	platform_driver_unregister(&mcdt_driver);
}

module_init(mcdt_init);
module_exit(mcdt_exit);

MODULE_DESCRIPTION("Unisoc MCDT audio DMA driver");
MODULE_LICENSE("GPL v2");
