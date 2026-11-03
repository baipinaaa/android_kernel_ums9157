/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCDT / VBC DMA helpers shared by the Unisoc VBC front end DAIs.
 *
 * Reconstructed from the vendor modules of the Sunelan Q968 (UMS9157/T157)
 * image:
 *   - mcdt_hw_r2p0.ko      : mcdt_{adc,dac}_dma_{enable,disable,phy_addr}()
 *   - snd-soc-sprd-vbc-fe.ko : consumers of the config below
 *
 * The vendor header is not in the public tree, so the structure layout was
 * recovered from the shipped module: it is exactly 0xb0 (176) bytes and the
 * offsets below are the ones the compiled code touches.  Fields that never
 * appear in the vendor binaries are kept as reserved blocks so that the
 * layout matches the vendor ABI byte for byte.
 */
#ifndef __SPRD_MCDT_DMA_H
#define __SPRD_MCDT_DMA_H

#include <linux/types.h>

/* ------------------------------------------------------------------------
 * MCDT channels.  The VBC front end hands these numbers to the mcdt helper
 * functions; the vendor build only ever uses the ones listed here.
 * ------------------------------------------------------------------------ */
enum sprd_mcdt_chn {
	SPRD_MCDT_CHN_VOICE	= 1,
	SPRD_MCDT_CHN_VOIP	= 2,
	SPRD_MCDT_CHN_FAST	= 3,
	SPRD_MCDT_CHN_HIFI	= 4,
	SPRD_MCDT_CHN_HIFI_FAST	= 10,
};

/* Length in 32 byte units passed together with the channel number. */
#define SPRD_MCDT_LEN_50	0x50
#define SPRD_MCDT_LEN_78	0x78
#define SPRD_MCDT_LEN_A0	0xa0
#define SPRD_MCDT_LEN_140	0x140

/**
 * struct sprd_fe_dma_config - per PCM stream DMA description
 * @name:		name of the VBC/MCDT path, e.g. "VBC DSP_CAP C"
 * @chn_l:		left channel number, or MCDT enable() result (scratch)
 * @chn_r:		right channel number
 * @chn_num:		number of channels handled by this path (2)
 * @fmt_width:		container width in bytes (2 = 16 bit, 4 = 24 bit)
 * @mcdt_chn:		MCDT channel id finally used by mcdt_hw_r2p0
 * @block_len:		fixed 0x50 in every shipped path
 * @dma_len:		DMA burst/length constant (0x50/0x78/0xa0/0x140)
 * @phy_addr_l:		left  physical address of the VBC/MCDT buffer
 * @phy_addr_r:		right physical address of the VBC/MCDT buffer
 * @phy_addr:		single physical address (MCDT paths)
 * @phy_addr2:		second physical address (normal/normal23 paths)
 * @has_phy:		1 when the path has a real physical address
 * @suffix_l:		left  VBC sub stream name, e.g. "normal_p_l"
 * @suffix_r:		right VBC sub stream name, e.g. "normal_p_r"
 */
struct sprd_fe_dma_config {
	const char *name;		/* 0x00 */
	u32 chn_l;			/* 0x08 */
	u32 chn_r;			/* 0x0c */
	u32 chn_num;			/* 0x10 */
	u32 reserved0[22];		/* 0x14 .. 0x6b */
	u32 fmt_width;			/* 0x6c */
	u32 reserved1;			/* 0x70 */
	u32 mcdt_chn;			/* 0x74 */
	u32 block_len;			/* 0x78 */
	u32 reserved2;			/* 0x7c */
	u32 dma_len;			/* 0x80 */
	u32 reserved3;			/* 0x84 */
	u32 phy_addr_l;			/* 0x88 */
	u32 phy_addr_r;			/* 0x8c */
	u32 phy_addr;			/* 0x90 */
	u32 phy_addr2;			/* 0x94 */
	u32 has_phy;			/* 0x98 */
	u32 reserved4;			/* 0x9c */
	const char *suffix_l;		/* 0xa0 */
	const char *suffix_r;		/* 0xa8 */
};

int mcdt_adc_dma_enable(u32 channel, u32 len);
int mcdt_dac_dma_enable(u32 channel, u32 len);
void mcdt_adc_dma_disable(u32 channel);
void mcdt_dac_dma_disable(u32 channel);
u32 mcdt_adc_dma_phy_addr(u32 channel);
u32 mcdt_dac_dma_phy_addr(u32 channel);

/* VBC helpers, exported by snd-soc-sprd-vbc-v4.ko */
u32 get_ap_vbc_phy_base(void);
u32 vbc_phy_ap2dsp(u32 phy);

#endif /* __SPRD_MCDT_DMA_H */
