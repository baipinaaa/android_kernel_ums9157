// SPDX-License-Identifier: GPL-2.0
/*
 * sprd-fe-dai.c - Unisoc (Spreadtrum) ASoC "front end" CPU DAI driver.
 *
 * Reconstructed from snd-soc-sprd-vbc-fe.ko of the Sunelan Q968 (UMS9157 /
 * T157, product YQ5) Android 13 vendor image; the vendor source file was
 *
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/
 *   modules/kernel5.4/audio_driver/sprd/dai/vbc/v4/fe_dai/sprd-fe-dai.c
 *
 * (that path is embedded as a string in the shipped module).  The driver
 * registers 26 front end DAIs, each of them describing one PCM path towards
 * the audio DSP (VBC) or towards the MCDT cross bar.  For every DAI the
 * hw_params callback programs the MCDT channel and stores the physical
 * address of the VBC/MCDT buffer in dai->playback_dma_data /
 * dai->capture_dma_data, which the platform PCM driver picks up later.
 *
 * Module facts (from .modinfo and the ELF symbol table):
 *   name      snd_soc_sprd_vbc_fe
 *   author    Lei Ning <lei.ning@unisoc.com>
 *   depends   mcdt_hw_r2p0, snd-soc-sprd-vbc-v4, agdsp_access
 *   of match  "unisoc,fe-dai"   platform driver "vbc-dai-fe"
 *   component "sprd-dai-fe"
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

#include "sprd-mcdt-dma.h"

#define FE_TAG			"[ASoC: FE.VBC ] "

#define FE_ERR(fmt, ...)	\
	printk(KERN_ERR FE_TAG fmt, ##__VA_ARGS__)
#define FE_INFO(fmt, ...)	\
	printk(KERN_INFO FE_TAG fmt, ##__VA_ARGS__)

/* Provided by agdsp_access.ko */
extern int agdsp_access_enable(void);
extern void agdsp_access_disable(void);

/**
 * enum fe_dai_id - front end DAI index
 *
 * The value is used as index into fe_dai_id_str[] and sprd_fe_dais[] and is
 * also what the DAPM routes / machine driver use to name a path.
 */
enum fe_dai_id {
	FE_DAI_ID_NORMAL_AP01 = 0,
	FE_DAI_ID_NORMAL_AP23,
	FE_DAI_ID_CAPTURE_DSP,
	FE_DAI_ID_FAST_P,
	FE_DAI_ID_OFFLOAD,
	FE_DAI_ID_VOICE,
	FE_DAI_ID_VOIP,
	FE_DAI_ID_FM,
	FE_DAI_ID_FM_CAPTURE_AP,
	FE_DAI_ID_VOICE_CAPTURE,
	FE_DAI_ID_LOOP,
	FE_DAI_ID_A2DP_OFFLOAD,
	FE_DAI_ID_A2DP_PCM,
	FE_DAI_ID_FM_CAP_DSP,
	FE_DAI_ID_BTSCO_CAP_DSP,
	FE_DAI_ID_FM_DSP,
	FE_DAI_ID_DUMP,
	FE_DAI_ID_BTSCO_CAP_AP,
	FE_DAI_ID_VOICE_PCM_P,
	FE_DAI_ID_CODEC_TEST,
	FE_DAI_ID_HFP,
	FE_DAI_ID_RECOGNISE_CAPTURE,
	FE_DAI_ID_HIFI_P,
	FE_DAI_ID_HIFI_FAST_P,
	FE_DAI_ID_DISPLAY_PORT,
	FE_DAI_ID_MM_P,
	FE_DAI_ID_MAX
};

/* fe_dai_id_str[] */
static const char * const fe_dai_id_str[FE_DAI_ID_MAX] = {
	[0] = "FE_DAI_ID_NORMAL_AP01",
	[1] = "FE_DAI_ID_NORMAL_AP23",
	[2] = "FE_DAI_ID_CAPTURE_DSP",
	[3] = "FE_DAI_ID_FAST_P",
	[4] = "FE_DAI_ID_OFFLOAD",
	[5] = "FE_DAI_ID_VOICE",
	[6] = "FE_DAI_ID_VOIP",
	[7] = "FE_DAI_ID_FM",
	[8] = "FE_DAI_ID_FM_CAPTURE_AP",
	[9] = "FE_DAI_ID_VOICE_CAPTURE",
	[10] = "FE_DAI_ID_LOOP",
	[11] = "FE_DAI_ID_A2DP_OFFLOAD",
	[12] = "FE_DAI_ID_A2DP_PCM",
	[13] = "FE_DAI_ID_FM_CAP_DSP",
	[14] = "FE_DAI_ID_BTSCO_CAP_DSP",
	[15] = "FE_DAI_ID_FM_DSP",
	[16] = "FE_DAI_ID_DUMP",
	[17] = "FE_DAI_ID_BTSCO_CAP_AP",
	[18] = "FE_DAI_ID_VOICE_PCM_P",
	[19] = "FE_DAI_ID_CODEC_TEST",
	[20] = "FE_DAI_ID_HFP",
	[21] = "FE_DAI_ID_RECOGNISE_CAPTURE",
	[22] = "FE_DAI_ID_HIFI_P",
	[23] = "FE_DAI_ID_HIFI_FAST_P",
	[24] = "DisplayPort MultiMedia",
	[25] = "FE_DAI_ID_MM_P",
};


/*
 * One DMA description per front end path.  The vendor module keeps them as
 * individual .bss objects (21 of them, 0xb0 bytes each) because they are
 * handed out to the platform PCM driver through dai->playback_dma_data /
 * dai->capture_dma_data and must therefore stay valid for the whole stream.
 */
static struct sprd_fe_dma_config vbc_pcm_normal_ap01_p;
static struct sprd_fe_dma_config vbc_pcm_normal_ap01_c;
static struct sprd_fe_dma_config vbc_pcm_normal_ap23_p;
static struct sprd_fe_dma_config vbc_pcm_normal_ap23_c;
static struct sprd_fe_dma_config pcm_dsp_cap_mcdt;
static struct sprd_fe_dma_config pcm_fast_play_mcdt;
static struct sprd_fe_dma_config pcm_hifi_fast_play_mcdt;
static struct sprd_fe_dma_config pcm_voip_play_mcdt;
static struct sprd_fe_dma_config pcm_voip_record_mcdt;
static struct sprd_fe_dma_config vbc_pcm_voice_capture_mcdt;
static struct sprd_fe_dma_config pcm_loop_play_mcdt;
static struct sprd_fe_dma_config pcm_loop_record_mcdt;
static struct sprd_fe_dma_config vbc_pcm_fm_capture;
static struct sprd_fe_dma_config vbc_pcm_a2dp_p;
static struct sprd_fe_dma_config pcm_dsp_fm_cap_mcdt;
static struct sprd_fe_dma_config pcm_dsp_btsco_cap_mcdt;
static struct sprd_fe_dma_config vbc_pcm_dump;
static struct sprd_fe_dma_config vbc_btsco_cap_ap;
static struct sprd_fe_dma_config vbc_pcm_recognise_capture_mcdt;
static struct sprd_fe_dma_config pcm_voice_play_mcdt;
static struct sprd_fe_dma_config pcm_hifi_play_mcdt;

/*
 * Map a front end id (+ the stream direction) to its DMA description.
 * Returns NULL for the paths that run entirely inside the audio DSP
 * ("do not use dma" in the vendor log).
 */
static struct sprd_fe_dma_config *fe_dma_config(int id, u32 stream)
{
	switch (id) {
	case FE_DAI_ID_NORMAL_AP01:
		return stream ? &vbc_pcm_normal_ap01_c : &vbc_pcm_normal_ap01_p;
	case FE_DAI_ID_NORMAL_AP23:
		return stream ? &vbc_pcm_normal_ap23_c : &vbc_pcm_normal_ap23_p;
	case FE_DAI_ID_CAPTURE_DSP:
		return &pcm_dsp_cap_mcdt;
	case FE_DAI_ID_FAST_P:
		return &pcm_fast_play_mcdt;
	case FE_DAI_ID_VOIP:
		return stream ? &pcm_voip_record_mcdt : &pcm_voip_play_mcdt;
	case FE_DAI_ID_FM_CAPTURE_AP:
		return &vbc_pcm_fm_capture;
	case FE_DAI_ID_VOICE_CAPTURE:
		return &vbc_pcm_voice_capture_mcdt;
	case FE_DAI_ID_LOOP:
		return stream ? &pcm_loop_record_mcdt : &pcm_loop_play_mcdt;
	case FE_DAI_ID_A2DP_PCM:
		return &vbc_pcm_a2dp_p;
	case FE_DAI_ID_FM_CAP_DSP:
		return &pcm_dsp_fm_cap_mcdt;
	case FE_DAI_ID_BTSCO_CAP_DSP:
		return &pcm_dsp_btsco_cap_mcdt;
	case FE_DAI_ID_DUMP:
		return &vbc_pcm_dump;
	case FE_DAI_ID_BTSCO_CAP_AP:
		return &vbc_btsco_cap_ap;
	case FE_DAI_ID_VOICE_PCM_P:
		return &pcm_voice_play_mcdt;
	case FE_DAI_ID_RECOGNISE_CAPTURE:
		return &vbc_pcm_recognise_capture_mcdt;
	case FE_DAI_ID_HIFI_P:
	case FE_DAI_ID_MM_P:
		return &pcm_hifi_play_mcdt;
	case FE_DAI_ID_HIFI_FAST_P:
		return &pcm_hifi_fast_play_mcdt;
	case FE_DAI_ID_OFFLOAD:
	case FE_DAI_ID_VOICE:
	case FE_DAI_ID_FM:
	case FE_DAI_ID_A2DP_OFFLOAD:
	case FE_DAI_ID_FM_DSP:
	case FE_DAI_ID_CODEC_TEST:
	case FE_DAI_ID_HFP:
	case FE_DAI_ID_DISPLAY_PORT:
	default:
		return NULL;
	}
}

/*
 * fe_get_dai_name() - vendor helper that also logs the invalid cases.
 *
 * Entries 19 (CODEC_TEST) and 24 (DISPLAY_PORT) have a NULL entry in
 * fe_dai_id_str[], the vendor code therefore returns an empty string for
 * them after complaining.
 */
static const char *fe_get_dai_name(int id)
{
	if (id > FE_DAI_ID_MM_P) {
		FE_ERR("invalid fe_dai_id %d\n", id);
		return "";
	}

	if (id == FE_DAI_ID_CODEC_TEST || id == FE_DAI_ID_DISPLAY_PORT) {
		FE_ERR("null dai_id string fe_dai_id=%d\n", id);
		return "";
	}

	return fe_dai_id_str[id];
}

/*
 * Program the MCDT cross bar for one front end path.  This mirrors the first
 * switch of the shipped fe_hw_params(); only the paths that really run
 * through the MCDT appear here.
 */
static int fe_mcdt_dma_config_init(int id, u32 stream)
{
	int ret = 0;

	switch (id) {
	case FE_DAI_ID_CAPTURE_DSP:
		ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_HIFI,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_FAST_P:
		ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_HIFI,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_VOIP:
		if (stream)
			ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_VOICE,
						  SPRD_MCDT_LEN_A0);
		else
			ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_VOICE,
						  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_VOICE_CAPTURE:
		ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_VOIP,
					  SPRD_MCDT_LEN_A0);
		break;
	case FE_DAI_ID_LOOP:
		if (stream)
			ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_FAST,
						  SPRD_MCDT_LEN_A0);
		else
			ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_FAST,
						  SPRD_MCDT_LEN_A0);
		break;
	case FE_DAI_ID_A2DP_PCM:
		ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_FAST,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_FM_CAP_DSP:
		ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_HIFI,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_BTSCO_CAP_DSP:
		ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_HIFI,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_VOICE_PCM_P:
		ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_VOICE,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_RECOGNISE_CAPTURE:
		ret = mcdt_adc_dma_enable(SPRD_MCDT_CHN_VOIP,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_HIFI_P:
	case FE_DAI_ID_MM_P:
		ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_VOIP,
					  SPRD_MCDT_LEN_140);
		break;
	case FE_DAI_ID_HIFI_FAST_P:
		ret = mcdt_dac_dma_enable(SPRD_MCDT_CHN_HIFI_FAST,
					  SPRD_MCDT_LEN_140);
		break;
	default:
		/* path owned by the DSP, no MCDT channel involved */
		break;
	}

	return ret;
}

/*
 * "is_normal_playback_use_24bit" in the shipped binary.
 *
 * The vendor code reads the raw format mask (params + 0x24) and falls back to
 * the channel mask (params + 0x28), runs __ffs() on it (rbit/clz) and adds
 * 0x20 when the channel mask had to be used.  The resulting container size is
 * compared with 2 (16 bit) and 6 (24 bit): 2 means "plain 16 bit", 6 means
 * "24 bit used".  Any other value logs an error and is treated as 16 bit.
 *
 * Returns 1 when the stream really is 24 bit, 0 otherwise; *fmt_ok is set to
 * 0 for the unsupported case, exactly like the two flags the compiler kept in
 * w8/w10.
 */
static int is_normal_playback_use_24bit(const struct snd_pcm_hw_params *params,
					int *fmt_ok)
{
	u32 fmt = params_format(params);
	u32 width;
	int base = 0;

	if (fmt) {
		width = __ffs(fmt);
	} else {
		u32 chn = params_channels(params);

		if (!chn)
			goto unsupported;
		base = 0x20;
		width = __ffs(chn);
	}
	width += base;

	if (width == 2) {
		*fmt_ok = 1;
		return 0;
	}

	if (width == 6) {
		*fmt_ok = 0;
		return 1;
	}

unsupported:
	FE_ERR("%s unsupported data format\n", __func__);
	*fmt_ok = 1;
	return 0;
}

/*
 * Fill the DMA description of one front end path: physical addresses of the
 * VBC buffers (or the MCDT channel address) and the sub stream names used by
 * the VBC driver.
 */
static void fe_dma_config_init(struct sprd_fe_dma_config *cfg, int id,
			       u32 stream, int is_24bit)
{
	switch (id) {
	case FE_DAI_ID_NORMAL_AP01:
		if (stream) {
			cfg->name = "VBC NORMAL AP01 CAP";
			cfg->chn_num = 2;
			cfg->mcdt_chn = 2;
			cfg->phy_addr_l = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 8);
			cfg->phy_addr_r = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0xc);
			cfg->chn_l = 3;
			cfg->chn_r = 4;
			cfg->suffix_l = "normal_c_l";
			cfg->suffix_r = "normal_c_r";
		} else {
			cfg->name = "VBC NORMAL AP01 PLY";
			cfg->chn_num = 2;
			cfg->chn_l = 1;
			cfg->chn_r = 2;
			cfg->fmt_width = is_24bit ? 4 : 2;
			cfg->dma_len = SPRD_MCDT_LEN_50;
			cfg->block_len = SPRD_MCDT_LEN_50;
			cfg->phy_addr_l = vbc_phy_ap2dsp(get_ap_vbc_phy_base());
			cfg->phy_addr_r = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 4);
			cfg->suffix_l = "normal_p_l";
			cfg->suffix_r = "normal_p_r";
		}
		break;
	case FE_DAI_ID_NORMAL_AP23:
		if (stream) {
			cfg->name = "VBC NORMAL AP23 CAP";
			cfg->chn_num = 2;
			cfg->chn_l = 2;
			cfg->chn_r = 2;
			cfg->dma_len = SPRD_MCDT_LEN_50;
			cfg->block_len = SPRD_MCDT_LEN_50;
			cfg->phy_addr = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0x18);
			cfg->phy_addr2 = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0x1c);
			cfg->suffix_l = "normal23_c_l";
			cfg->suffix_r = "normal23_c_r";
		} else {
			cfg->name = "VBC NORMAL AP23 PLY";
			cfg->chn_num = 2;
			cfg->chn_l = 0x11;
			cfg->chn_r = 0x12;
			cfg->fmt_width = is_24bit ? 4 : 2;
			cfg->dma_len = SPRD_MCDT_LEN_50;
			cfg->block_len = SPRD_MCDT_LEN_50;
			cfg->phy_addr_l = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0x10);
			cfg->phy_addr_r = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0x14);
			cfg->suffix_l = "normal23_p_l";
			cfg->suffix_r = "normal23_p_r";
		}
		break;
	case FE_DAI_ID_CAPTURE_DSP:
		cfg->name = "VBC DSP_CAP C";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_50;
		cfg->block_len = SPRD_MCDT_LEN_50;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_HIFI);
		cfg->suffix_l = "dspcap_c";
		break;
	case FE_DAI_ID_FAST_P:
		cfg->name = "VBC PCM Fast P";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_78;
		cfg->block_len = SPRD_MCDT_LEN_78;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_HIFI);
		cfg->suffix_l = "fast_p";
		break;
	case FE_DAI_ID_VOIP:
		if (stream) {
			cfg->name = "PCM voip record With MCDT";
			cfg->chn_num = 2;
			cfg->chn_l = 2;
			cfg->dma_len = SPRD_MCDT_LEN_A0;
			cfg->has_phy = 1;
			cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_VOICE);
			cfg->suffix_l = "voip_c";
		} else {
			cfg->name = "PCM voip play With MCDT";
			cfg->chn_num = 2;
			cfg->chn_l = 4;
			cfg->dma_len = SPRD_MCDT_LEN_A0;
			cfg->has_phy = 1;
			cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_VOICE);
			cfg->suffix_l = "voip_p";
		}
		break;
	case FE_DAI_ID_FM_CAPTURE_AP:
		cfg->name = "VBC PCM fm capture";
		cfg->chn_num = 2;
		cfg->chn_l = 2;
		cfg->dma_len = SPRD_MCDT_LEN_50;
		cfg->block_len = SPRD_MCDT_LEN_50;
		cfg->phy_addr = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 8);
		cfg->phy_addr2 = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0xc);
		cfg->chn_l = 3;
		cfg->chn_r = 4;
		cfg->suffix_l = "normal_c_l";
		cfg->suffix_r = "normal_c_r";
		cfg->has_phy = 1;
		break;
	case FE_DAI_ID_VOICE_CAPTURE:
		cfg->name = "VBC PCM voice C With MCDT";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_A0;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_VOIP);
		cfg->suffix_l = "voice_c";
		break;
	case FE_DAI_ID_LOOP:
		if (stream) {
			cfg->name = "PCM loop record With MCDT";
			cfg->chn_num = 2;
			cfg->chn_l = 4;
			cfg->dma_len = SPRD_MCDT_LEN_A0;
			cfg->has_phy = 1;
			cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_FAST);
			cfg->suffix_l = "loop_c";
		} else {
			cfg->name = "PCM loop play With MCDT";
			cfg->chn_num = 2;
			cfg->chn_l = 4;
			cfg->dma_len = SPRD_MCDT_LEN_A0;
			cfg->has_phy = 1;
			cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_FAST);
			cfg->suffix_l = "loop_p";
		}
		break;
	case FE_DAI_ID_A2DP_PCM:
		cfg->name = "VBC a2dp pcm";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_A0;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_FAST);
		cfg->suffix_l = "a2dppcm_p";
		break;
	case FE_DAI_ID_FM_CAP_DSP:
		cfg->name = "VBC DSP_FM_CAP C";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_140;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_HIFI);
		cfg->suffix_l = "dspfmcap_c";
		break;
	case FE_DAI_ID_BTSCO_CAP_DSP:
		cfg->name = "VBC DSP_BTSCO_CAP C";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_140;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_HIFI);
		cfg->suffix_l = "dspbtscocap_c";
		break;
	case FE_DAI_ID_DUMP:
		cfg->name = "VBC PCM dump";
		cfg->chn_num = 2;
		cfg->chn_l = 2;
		cfg->dma_len = SPRD_MCDT_LEN_50;
		cfg->block_len = SPRD_MCDT_LEN_50;
		cfg->phy_addr = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 8);
		cfg->phy_addr2 = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0xc);
		cfg->chn_l = 3;
		cfg->chn_r = 4;
		cfg->suffix_l = "normal_c_l";
		cfg->suffix_r = "normal_c_r";
		break;
	case FE_DAI_ID_BTSCO_CAP_AP:
		cfg->name = "VBC PCM btsco_cap_ap";
		cfg->chn_num = 2;
		cfg->chn_l = 2;
		cfg->chn_r = 2;
		cfg->dma_len = SPRD_MCDT_LEN_50;
		cfg->block_len = SPRD_MCDT_LEN_50;
		cfg->phy_addr = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 8);
		cfg->phy_addr2 = vbc_phy_ap2dsp(get_ap_vbc_phy_base() + 0xc);
		cfg->chn_l = 3;
		cfg->chn_r = 4;
		cfg->suffix_l = "normal_c_l";
		cfg->suffix_r = "normal_c_r";
		break;
	case FE_DAI_ID_VOICE_PCM_P:
		cfg->name = "PCM voice play With MCDT";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_A0;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_VOICE);
		cfg->suffix_l = "voice_pcm_p";
		break;
	case FE_DAI_ID_RECOGNISE_CAPTURE:
		cfg->name = "VBC PCM recognise C With MCDT";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_140;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_adc_dma_phy_addr(SPRD_MCDT_CHN_VOIP);
		cfg->suffix_l = "recognise_c";
		break;
	case FE_DAI_ID_HIFI_FAST_P:
		cfg->name = "VBC PCM HIFI Fast P";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_A0;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_HIFI_FAST);
		cfg->suffix_l = "hifi_fast_p";
		break;
	case FE_DAI_ID_HIFI_P:
	case FE_DAI_ID_MM_P:
		cfg->name = "DSP IIS HIFI P";
		cfg->chn_num = 2;
		cfg->chn_l = 4;
		cfg->dma_len = SPRD_MCDT_LEN_A0;
		cfg->has_phy = 1;
		cfg->phy_addr = mcdt_dac_dma_phy_addr(SPRD_MCDT_CHN_VOIP);
		cfg->suffix_l = "hifi_p";
		break;
	default:
		break;
	}
}

/*
 * The vendor driver logs "%s %s do not use dma" for the paths that only exist
 * inside the audio DSP.
 */
static void fe_dma_config_skip(int id)
{
	FE_INFO("%s %s do not use dma\n", __func__, fe_get_dai_name(id));
}

static int fe_dai_probe(struct snd_soc_dai *dai);
static int fe_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai);
static int fe_hw_free(struct snd_pcm_substream *substream,
		      struct snd_soc_dai *dai);

static const struct snd_soc_dai_ops sprd_fe_dai_ops = {
	.hw_params	= fe_hw_params,
	.hw_free	= fe_hw_free,
};

static struct snd_soc_dai_driver sprd_fe_dais[FE_DAI_ID_MAX] = {
	[0] = {
		.name = "FE_DAI_ID_NORMAL_AP01",
		.id = 0,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_NORMAL_AP01_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_NORMAL_AP01_C",
		},
		.playback = {
			.stream_name = "FE_DAI_NORMAL_AP01_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_NORMAL_AP01_P",
		},
	},
	[1] = {
		.name = "FE_DAI_ID_NORMAL_AP23",
		.id = 1,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_NORMAL_AP23_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_NORMAL_AP23_C",
		},
		.playback = {
			.stream_name = "FE_DAI_NORMAL_AP23_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_NORMAL_AP23_P",
		},
	},
	[2] = {
		.name = "FE_DAI_ID_CAPTURE_DSP",
		.id = 2,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_CAP_DSP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_CAP_DSP_C",
		},
	},
	[3] = {
		.name = "FE_DAI_ID_FAST_P",
		.id = 3,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_FAST_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_FAST_P",
		},
	},
	[4] = {
		.name = "FE_DAI_ID_OFFLOAD",
		.id = 4,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.compress_new = snd_soc_new_compress,
		.playback = {
			.stream_name = "FE_DAI_OFFLOAD_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_OFFLOAD_P",
		},
	},
	[5] = {
		.name = "FE_DAI_ID_VOICE",
		.id = 5,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_VOICE_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOICE_C",
		},
		.playback = {
			.stream_name = "FE_DAI_VOICE_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOICE_P",
		},
	},
	[6] = {
		.name = "FE_DAI_ID_VOIP",
		.id = 6,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_VOIP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOIP_C",
		},
		.playback = {
			.stream_name = "FE_DAI_VOIP_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOIP_P",
		},
	},
	[7] = {
		.name = "FE_DAI_ID_FM",
		.id = 7,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_FM_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_FM_P",
		},
	},
	[8] = {
		.name = "FE_DAI_ID_FM_CAPTURE_AP",
		.id = 8,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_FM_CAP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_FM_CAP_C",
		},
	},
	[9] = {
		.name = "FE_DAI_ID_VOICE_CAPTURE",
		.id = 9,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_VOICE_CAP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOICE_CAP_C",
		},
	},
	[10] = {
		.name = "FE_DAI_ID_LOOP",
		.id = 10,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_LOOP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_LOOP_C",
		},
		.playback = {
			.stream_name = "FE_DAI_LOOP_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_LOOP_P",
		},
	},
	[11] = {
		.name = "FE_DAI_ID_A2DP_OFFLOAD",
		.id = 11,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.compress_new = snd_soc_new_compress,
		.playback = {
			.stream_name = "FE_DAI_A2DP_OFFLOAD_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_A2DP_OFFLOAD_P",
		},
	},
	[12] = {
		.name = "FE_DAI_ID_A2DP_PCM",
		.id = 12,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_A2DP_PCM_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_A2DP_PCM_P",
		},
	},
	[13] = {
		.name = "FE_DAI_ID_FM_CAP_DSP",
		.id = 13,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_FM_CAP_DSP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_FM_CAP_DSP_C",
		},
	},
	[14] = {
		.name = "FE_DAI_ID_BTSCO_CAP_DSP",
		.id = 14,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_BTSCO_CAP_DSP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_BTSCO_CAP_DSP_C",
		},
	},
	[15] = {
		.name = "FE_DAI_ID_FM_DSP",
		.id = 15,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_FM_DSP_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_FM_DSP_P",
		},
	},
	[16] = {
		.name = "FE_DAI_ID_DUMP",
		.id = 16,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_DUMP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_DUMP_C",
		},
	},
	[17] = {
		.name = "FE_DAI_ID_BTSCO_CAP_AP",
		.id = 17,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_BTCAP_AP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_BTCAP_AP_C",
		},
	},
	[18] = {
		.name = "FE_DAI_ID_VOICE_PCM_P",
		.id = 18,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_VOICE_PCM_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_VOICE_PCM_P",
		},
	},
	[19] = {
		.name = "FE_DAI_ID_CODEC_TEST",
		.id = 19,
		.capture = {
			.stream_name = "FE_DAI_CODEC_TEST_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "None",
		},
		.playback = {
			.stream_name = "FE_DAI_CODEC_TEST_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "None",
		},
	},
	[20] = {
		.name = "FE_DAI_ID_HFP",
		.id = 20,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_HFP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_HFP_C",
		},
		.playback = {
			.stream_name = "FE_DAI_HFP_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_HFP_P",
		},
	},
	[21] = {
		.name = "FE_DAI_ID_RECOGNISE_CAPTURE",
		.id = 21,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.capture = {
			.stream_name = "FE_DAI_RECOGNISE_CAP_C",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_RECOGNISE_CAP_C",
		},
	},
	[22] = {
		.name = "FE_DAI_ID_HIFI_P",
		.id = 22,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_HIFI_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_HIFI_P",
		},
	},
	[23] = {
		.name = "FE_DAI_ID_HIFI_FAST_P",
		.id = 23,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_HIFI_FAST_P",
			.formats = 0x44,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_HIFI_FAST_P",
		},
	},
	[24] = {
		.name = "DisplayPort MultiMedia",
		.id = 24,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "DisplayPort MultiMedia Playback",
			.formats = 0x444,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 8,
			.aif_name = "DP_DL",
		},
	},
	[25] = {
		.name = "FE_DAI_ID_MM_P",
		.id = 25,
		.ops = &sprd_fe_dai_ops,
		.probe = fe_dai_probe,
		.playback = {
			.stream_name = "FE_DAI_MM_P",
			.formats = 0x444,
			.rates = 0x40000000,
			.rate_min = 8000,
			.rate_max = 192000,
			.channels_min = 1,
			.channels_max = 2,
			.aif_name = "FE_IF_MM_P",
		},
	},
};


static const struct snd_soc_component_driver sprd_fe_dai_component = {
	.name		= "sprd-dai-fe",
};

static int fe_dai_probe(struct snd_soc_dai *dai)
{
	const struct snd_soc_dai_driver *drv;
	struct snd_soc_dapm_route route;

	if (!dai || !dai->driver) {
		FE_ERR("%s invalid params\n", __func__);
		return -EINVAL;
	}

	drv = dai->driver;

	/* connect the FE stream name to its DAPM AIF widget */
	if (drv->playback.stream_name && drv->playback.aif_name) {
		memset(&route, 0, sizeof(route));
		route.sink = drv->playback.stream_name;
		route.source = drv->playback.aif_name;
		dev_dbg(dai->dev, "%s add route for widget %s", __func__,
			drv->playback.aif_name);
		snd_soc_dapm_add_routes(&dai->component->dapm, &route, 1);
	}

	if (drv->capture.stream_name && drv->capture.aif_name) {
		memset(&route, 0, sizeof(route));
		route.sink = drv->capture.stream_name;
		route.source = drv->capture.aif_name;
		dev_dbg(dai->dev, "%s add route for widget %s", __func__,
			drv->capture.aif_name);
		snd_soc_dapm_add_routes(&dai->component->dapm, &route, 1);
	}

	return 0;
}

static int fe_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct sprd_fe_dma_config *cfg;
	const char *name;
	u32 stream = substream->stream;
	int id = dai->id;
	int fmt_ok = 0;
	int is_24bit;
	int ret;

	name = fe_get_dai_name(id);
	FE_INFO("%s fe dai: %s(%d) %s\n", __func__, name, id,
		stream ? "capture" : "playback");

	ret = agdsp_access_enable();
	if (ret) {
		FE_ERR("%s, agdsp_access_enable failed!\n", __func__);
		return ret;
	}

	ret = fe_mcdt_dma_config_init(id, stream);
	if (ret < 0) {
		FE_ERR("%s failed dai_id=%d stream=%d\n",
		       "mcdt_dma_config_init", id, stream);
		FE_ERR("%s mcdt config init failed\n", __func__);
		agdsp_access_disable();
		return ret;
	}

	agdsp_access_disable();

	/*
	 * Container width of the stream: the vendor driver decides between
	 * the "16 bit" and "24 bit in 32 bit" MCDT channel layout here.
	 */
	is_24bit = is_normal_playback_use_24bit(params, &fmt_ok);

	cfg = fe_dma_config(id, stream);
	if (!cfg) {
		fe_dma_config_skip(id);
		snd_soc_dai_set_dma_data(dai, substream, NULL);
		return 0;
	}

	fe_dma_config_init(cfg, id, stream, is_24bit);

	/*
	 * MCDT channel width: the VBC paths without a physical address run on
	 * the 16 bit channel unless the stream really is 24 bit, everything
	 * else keeps the 32 bit channel.
	 */
	cfg->mcdt_chn = (cfg->has_phy || is_24bit) ? 4 : 2;

	snd_soc_dai_set_dma_data(dai, substream, cfg);
	return 0;
}

static int fe_hw_free(struct snd_pcm_substream *substream,
		      struct snd_soc_dai *dai)
{
	const char *name;
	u32 stream = substream->stream;
	int id = dai->id;
	int ret;

	name = fe_get_dai_name(id);
	FE_INFO("%s fe dai: %s(%d) %s\n", __func__, name, id,
		stream ? "capture" : "playback");

	ret = agdsp_access_enable();
	if (ret) {
		FE_ERR("%s, agdsp_access_enable failed!\n", __func__);
		return ret;
	}

	switch (id) {
	case FE_DAI_ID_CAPTURE_DSP:
	case FE_DAI_ID_FM_CAP_DSP:
	case FE_DAI_ID_BTSCO_CAP_DSP:
		mcdt_adc_dma_disable(SPRD_MCDT_CHN_HIFI);
		break;
	case FE_DAI_ID_FAST_P:
		mcdt_dac_dma_disable(SPRD_MCDT_CHN_HIFI);
		break;
	case FE_DAI_ID_VOIP:
		if (stream)
			mcdt_adc_dma_disable(SPRD_MCDT_CHN_VOICE);
		else
			mcdt_dac_dma_disable(SPRD_MCDT_CHN_VOICE);
		break;
	case FE_DAI_ID_VOICE_CAPTURE:
	case FE_DAI_ID_RECOGNISE_CAPTURE:
		mcdt_adc_dma_disable(SPRD_MCDT_CHN_VOIP);
		break;
	case FE_DAI_ID_LOOP:
		if (stream)
			mcdt_adc_dma_disable(SPRD_MCDT_CHN_FAST);
		else
			mcdt_dac_dma_disable(SPRD_MCDT_CHN_FAST);
		break;
	case FE_DAI_ID_A2DP_PCM:
		mcdt_dac_dma_disable(SPRD_MCDT_CHN_FAST);
		break;
	case FE_DAI_ID_VOICE_PCM_P:
		mcdt_dac_dma_disable(SPRD_MCDT_CHN_VOICE);
		break;
	case FE_DAI_ID_HIFI_P:
	case FE_DAI_ID_MM_P:
		mcdt_dac_dma_disable(SPRD_MCDT_CHN_VOIP);
		break;
	case FE_DAI_ID_HIFI_FAST_P:
		mcdt_dac_dma_disable(SPRD_MCDT_CHN_HIFI_FAST);
		break;
	default:
		/* nothing to do for the DSP-only paths */
		break;
	}

	agdsp_access_disable();
	return ret;
}

static int sprd_fe_dai_dev_probe(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s: dev name %s\n", __func__, dev_name(&pdev->dev));

	return snd_soc_register_component(&pdev->dev, &sprd_fe_dai_component,
					  sprd_fe_dais, ARRAY_SIZE(sprd_fe_dais));
}

static int sprd_fe_dai_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id sprd_fe_dai_of_match[] = {
	{ .compatible = "unisoc,fe-dai" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_fe_dai_of_match);

static struct platform_driver sprd_fe_dai_driver = {
	.probe	= sprd_fe_dai_dev_probe,
	.remove	= sprd_fe_dai_dev_remove,
	.driver	= {
		.name		= "vbc-dai-fe",
		.of_match_table	= sprd_fe_dai_of_match,
	},
};

static int __init sprd_fe_dai_driver_init(void)
{
	return platform_driver_register(&sprd_fe_dai_driver);
}

static void __exit sprd_fe_dai_driver_exit(void)
{
	platform_driver_unregister(&sprd_fe_dai_driver);
}

module_init(sprd_fe_dai_driver_init);
module_exit(sprd_fe_dai_driver_exit);

MODULE_AUTHOR("Lei Ning <lei.ning@unisoc.com>");
MODULE_DESCRIPTION("SPRD ASoC FRONT END CPU DAI");
MODULE_LICENSE("GPL");
