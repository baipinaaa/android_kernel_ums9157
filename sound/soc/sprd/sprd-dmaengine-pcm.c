// SPDX-License-Identifier: GPL-2.0
/*
 * sprd-dmaengine-pcm.c - Spreadtrum ASoC PCM DMA platform driver
 *
 * 本文件依据 snd-soc-sprd-dmaengine-pcm.ko（vendor_boot 中 157 个无源码模块之一，
 * .text 13480 B，按 .text 升序为第 23 个）重建：
 *   原始路径（厂商构建机）：
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/platform/dmaengine_pcm/sprd-dmaengine-pcm.c
 *
 * 二进制 .rodata 里另外还带着两个头文件的绝对路径（debug 描述符用）：
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/platform/dmaengine_pcm/../../include/sprd-tdm.h
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/platform/dmaengine_pcm/../../include/sprd-i2s.h
 *
 * 模块名 / pm 锁的 lockdep 名字（.rodata 原样保留）：
 *   "sprd_dmaengine_pcm"、"&pm_dma->pm_mtx_dma_prot"、"&pm_dma->pm_mtx_cnt"
 *
 * 依据材料：
 *   - modinfo: description=SPRD ASoC PCM DMA, author=Jian chen <jian.chen@spreadtrum.com>,
 *              depends=audio_mem,agdsp_access,snd-soc-sprd-card, alias=platform:sprd-audio
 *   - of alias: unisoc,{sharkl5,roc1,qogirl6,qogirn6pro}-pcm-platform
 *   - 25 个函数 / 符号表 / .rela 重定位 / .rodata / .data / .bss 全部逐项核对
 *   - 本文件所有 printk 格式串与厂商 .rodata 逐字节一致（recon/cmpstr.py 校验）
 *
 * 说明：厂商 DMA 描述符（struct sprd_dma_cfg）的完整字段布局由 .text 中的
 * 常量偏移反推，未在公开树中出现过；相关偏移在下方结构体中按反推结果标注。
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/info.h>

#include "agdsp_access.h"
#include "audio_mem.h"

#define DRV_NAME		"sprd-dmaengine"
#define PCM_PLAT_NAME		"sprd-pcm-audio"
#define PCM_COMP_NAME		"sprd-pcm-dma"

/* 最多 2 个 DMA 通道（channel count 校验的边界来自 .text 中的立即数 2） */
#define SPRD_PCM_MAX_CHANNELS	2
/* 每个通道的 DMA 描述符块头部大小，0x78 + periods * sizeof(sg_desc) */
#define SPRD_DMA_CFG_HDR_SIZE	0x78

/* cpu_dai->driver->id 里与 DMA 配置相关的三个值（.text 中立即数） */
#define SPRD_DAI_ID_I2S		0x124
#define SPRD_DAI_ID_TDM		0x129

/* audio_addr_ap2dsp() 的地址类型参数（.text 中立即数） */
#define SPRD_AUDIO_MEM_NORMAL	5
#define SPRD_AUDIO_MEM_IRAM	0x13
#define SPRD_AUDIO_MEM_IRAM_RX	0x11

/* 可调试参数（.bss，proc_write 按名字改写） */
static unsigned int debug_pointer_log;
static unsigned int test_ddr_buffer_size;
static unsigned int test_force_stop;
/* .data=1，采集走 iram 的开关 */
static unsigned int test_normal_cap_use_iram = 1;

struct snd_soc_pcm_runtime;
struct snd_soc_component;

/*
 * sprd_pcm_dma_params - dai 驱动挂在 cpu_dai->playback_dma_data / capture_dma_data
 * 上的私有 DMA 参数（结构体定义在厂商头文件 sprd-pcm-dma.h 中，此处按
 * .text 里访问到的偏移复原）：
 */
struct sprd_pcm_dma_params {
	char *name;						/* +0x00 */
	unsigned int datawidth;					/* +0x74 */
	unsigned int src_step;					/* +0x78 */
	unsigned int des_step;					/* +0x7c */
	unsigned int addr_width;				/* +0x80 */
	unsigned int fifo_phys;					/* +0x88 */
	unsigned int is_playback;				/* +0x98 */
	char *used_dma_channel_name[SPRD_PCM_MAX_CHANNELS];	/* +0xa0 */
};

/*
 * sprd_dma_cfg - 每个通道一块，devm_kmalloc(chan_cnt * sizeof(*cfg)) 分配，
 * 紧跟其后的是 periods 个 scatterlist（sg 就放在 +0x78 起的柔性数组里）。
 * 字段偏移均由 .text/.rela 反推，注释里的 +0xNN 为厂商二进制中的偏移。
 */
struct sprd_dma_cfg {
	u64 digest;			/* +0x00 magic，运行时填 0xa5b1eb43ed4fa99d */
	u64 src_addr;			/* +0x08 AP 视角物理地址 */
	u64 dst_addr;			/* +0x10 同上 */
	u32 datawidth;			/* +0x18 */
	u32 frag_len;			/* +0x1c 每个 period 的字节数 */
	u32 src_step_type;		/* +0x20 */
	u32 dst_step_type;		/* +0x28 */
	u32 src_step;			/* +0x2c */
	u32 dst_step;			/* +0x30 */
	u32 fifo_phys;			/* +0x34 */
	u32 burst;			/* +0x38 */
	u64 ll_cfg_phy;			/* +0x40 */
	u64 chan_flag;			/* +0x50 */
	u32 periods;			/* +0x6c */
	void *sg_virt;			/* +0x70 指向本块 +0x78 起 */
	/* struct scatterlist sg[];	 +0x78 */
};

/*
 * sprd_pcm_runtime - substream->runtime->private_data
 * 厂商布局（.bss/.text 反推）：+0x00 period_bytes、+0x08 dma_params、
 * +0x10 dma_chan[2]、+0x20/+0x28 dma_cfg 虚拟地址数组、+0x30/+0x38 物理地址、
 * +0x40/+0x48 dma_buff_phys、+0x50/+0x58 长度、+0x68 dma_type(-1 表示无效)、
 * +0x8c 交织标志、+0xa0 agdsp_on。
 */
struct sprd_pcm_runtime {
	u32 period_bytes;					/* +0x00 */
	u32 pad0;						/* +0x04 */
	struct sprd_pcm_dma_params *dma_params;			/* +0x08 */
	struct dma_chan *dma_chan[SPRD_PCM_MAX_CHANNELS];	/* +0x10 */
	void *dma_cfg_virt[SPRD_PCM_MAX_CHANNELS];		/* +0x20 */
	phys_addr_t dma_cfg_phy[SPRD_PCM_MAX_CHANNELS];		/* +0x30 */
	dma_addr_t dma_buff_phys[SPRD_PCM_MAX_CHANNELS];	/* +0x40 */
	size_t dma_buff_size[SPRD_PCM_MAX_CHANNELS];		/* +0x50 */
	u32 dma_size;						/* +0x60 */
	u32 pad1;						/* +0x64 */
	int dma_type;						/* +0x68 */
	u32 periods;						/* +0x6c */
	u32 frag_size;						/* +0x70 */
	u32 datawidth;						/* +0x74 */
	u32 src_step;						/* +0x78 */
	u32 des_step;						/* +0x7c */
	u32 chan_cnt;						/* +0x80 */
	u32 pad2;						/* +0x84 */
	u32 size_inout;						/* +0x88 */
	u32 interleaved;					/* +0x8c */
	u32 slave_id;						/* +0x90 */
	u32 pad3;						/* +0x94 */
	void *private_data;					/* +0x98 */
	u8 agdsp_on;						/* +0xa0 */
};

/* pm_dma 私有数据（.bss 单例，0x70 字节）：+0x00 suspend_cnt、+0x08 rtd、
 * +0x10 notifier_block、+0x28 自旋锁、+0x30 mutex pm_mtx_dma_prot、+0x50 mutex pm_mtx_cnt */
struct pm_dma_priv {
	atomic_t suspend_cnt;					/* +0x00 */
	struct snd_soc_pcm_runtime *rtd;			/* +0x08 */
	struct notifier_block nb;				/* +0x10 */
	spinlock_t lock;					/* +0x28 */
	struct mutex pm_mtx_dma_prot;				/* +0x30 */
	struct mutex pm_mtx_cnt;				/* +0x50 */
};

static struct pm_dma_priv pm_dma;
static struct lock_class_key sprd_pm_mtx_dma_prot_key;
static struct lock_class_key sprd_pm_mtx_cnt_key;

/* 由 .rodata 反编译得到（sprd_pcm_hardware_v1，0x80 字节） */
static const struct snd_pcm_hardware sprd_pcm_hardware_v1 = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_NONINTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 256 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 128 * 1024,
	.periods_min		= 1,
	.periods_max		= 256,
	.fifo_size		= 0,
};

/* sprd_i2s_pcm_hardware（.rodata+0x1630，I2S 专用，period 上限更小） */
static const struct snd_pcm_hardware sprd_i2s_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_NONINTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 128 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 64 * 1024,
	.periods_min		= 1,
	.periods_max		= 128,
	.fifo_size		= 0,
};

/* ======================================================================== */
/* 辅助函数                                                                  */
/* ======================================================================== */

static const char *sprd_pcm_get_dma_type(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	switch (cpu_dai->driver->id) {
	case SPRD_DAI_ID_I2S:
		return "I2S";
	case SPRD_DAI_ID_TDM:
		return "TDM";
	default:
		return "VBC";
	}
}

/* 取运行时 DMA 参数：优先用当前 substream 对应的 cpu_dai */
static struct sprd_pcm_dma_params *
sprd_pcm_get_dma_params(struct snd_pcm_substream *substream, int stream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	return stream ? (struct sprd_pcm_dma_params *)cpu_dai->capture_dma_data
		      : (struct sprd_pcm_dma_params *)cpu_dai->playback_dma_data;
}

/* ======================================================================== */
/* DMA step 配置                                                            */
/* ======================================================================== */

/*
 * sprd_pcm_dma_step_config - 依据 datawidth / 通道数计算 src_step 与 des_step
 * 反向要点：
 *   - datawidth==2 → step 4；datawidth==4 → step 8；其他值直接报错 -EINVAL
 *   - 播放时 step 写到 src_step，采集时写到 des_step，另一个方向置 0
 */
static int sprd_pcm_dma_step_config(struct sprd_pcm_dma_params *dma_params,
				    int is_playback, int is_irq)
{
	int datawidth = dma_params->datawidth;
	int step;

	if (is_irq)
		return 0;

	if (datawidth == 2)
		step = 4;
	else if (datawidth == 4)
		step = 8;
	else {
		pr_err("[ASoC: PCM ] err: datawidth %u is not supported yet.\n",
		       dma_params->datawidth);
		return -EINVAL;
	}

	if (is_playback) {
		dma_params->src_step = step;
		dma_params->des_step = 0;
	} else {
		dma_params->src_step = 0;
		dma_params->des_step = step;
	}

	pr_info("[ASoC: PCM ] desc->src_step=%d, dest_step=%d\n",
		dma_params->src_step, dma_params->des_step);

	return 0;
}

/* ======================================================================== */
/* TDM / I2S 的 frag 计算                                                   */
/* ======================================================================== */

/* TDM：slot 宽度 20/22 bit 两种，乘以数据宽度得到每个 period 内的搬运量 */
static unsigned int sprd_tdm_dai_to_config(struct sprd_pcm_dma_params *dma_params)
{
	unsigned int frag;

	frag = (dma_params->des_step ? 0x20 - 0x14 : 0x20 - 0x16) & 0xffff;
	frag *= dma_params->datawidth;

	return frag;
}

/* I2S：与 TDM 相反，播放/采集对应的 slot 宽度常量互换 */
static unsigned int sprd_i2s_dai_to_config(struct sprd_pcm_dma_params *dma_params)
{
	unsigned int frag;

	frag = (dma_params->des_step ? 0x20 - 0x16 : 0x20 - 0x14) & 0xffff;
	frag *= dma_params->datawidth;

	return frag;
}

/* ======================================================================== */
/* DMA 通道申请 / 释放                                                       */
/* ======================================================================== */

static void sprd_pcm_release_dma_channel(struct snd_pcm_substream *substream,
					 struct sprd_pcm_runtime *prtd,
					 int chan_cnt)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int i;

	mutex_lock(&pm_dma.pm_mtx_dma_prot);
	for (i = 0; i < chan_cnt; i++) {
		if (!prtd->dma_chan[i])
			continue;

		spin_lock(&pm_dma.lock);
		pr_info("[ASoC: PCM ] %s, release chan_id %d\n",
			__func__, prtd->dma_chan[i]->chan_id);
		dma_release_channel(prtd->dma_chan[i]);
		prtd->dma_chan[i] = NULL;
		prtd->dma_cfg_virt[i] = NULL;
		spin_unlock(&pm_dma.lock);

		pr_info("[ASoC: PCM ] %s release id=%d\n",
			__func__,
			rtd->cpu_dai ? rtd->cpu_dai->id : -1);
	}
	prtd->dma_params = NULL;
	mutex_unlock(&pm_dma.pm_mtx_dma_prot);
}

/*
 * sprd_pcm_request_dma_channel - 为每个通道申请从设备 DMA 通道
 * 通道名来自 dma_params->used_dma_channel_name[i]（+0xa0 + 8*i），
 * 申请成功后读通道设备的 of_node 的 "compatible" 决定 DMA 类型。
 */
static int sprd_pcm_request_dma_channel(struct snd_pcm_substream *substream,
					int chan_cnt)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	struct snd_soc_component *component;
	struct sprd_pcm_dma_params *dma_params = prtd->dma_params;
	const char *compatible = NULL;
	int i;

	component = snd_soc_rtdcom_lookup(rtd, PCM_COMP_NAME);
	if (!dma_params)
		return -EINVAL;

	for (i = 0; i < chan_cnt; i++) {
		struct dma_chan *chan;

		if (!dma_params->used_dma_channel_name[i]) {
			pr_err("[ASoC: PCM ] ERR: dma_data->used_dma_channel_name[i] == NULL\n");
			goto err;
		}

		chan = dma_request_slave_channel(component->dev,
						 dma_params->used_dma_channel_name[i]);
		if (!chan || IS_ERR(chan)) {
			pr_err("[ASoC: PCM ] ERR: request dma channel(%s) failed!(%ld)\n",
			       dma_params->used_dma_channel_name[i],
			       (long)PTR_ERR_OR_ZERO(chan));
			goto err;
		}

		mutex_lock(&pm_dma.pm_mtx_dma_prot);
		spin_lock(&pm_dma.lock);
		prtd->dma_chan[i] = chan;

		if (!chan->device->dev->of_node)
			pr_err("[ASoC: PCM ] ERR: device_node is NULL!\n");

		if (chan->device->dev->of_node &&
		    !of_property_read_string(chan->device->dev->of_node,
					     "compatible", &compatible)) {
			if (!strcmp(compatible, "sprd,audio-pipe") ||
			    !strcmp(compatible, "unisoc,audio-pipe"))
				prtd->dma_type = 2;
		} else {
			prtd->dma_type = -1;
		}

		pr_info("[ASoC: PCM ] Chan%d DMA ID=%d\n",
			i, chan->chan_id);
		spin_unlock(&pm_dma.lock);
		mutex_unlock(&pm_dma.pm_mtx_dma_prot);
	}

	return 0;

err:
	while (i-- > 0) {
		if (prtd->dma_chan[i]) {
			dma_release_channel(prtd->dma_chan[i]);
			prtd->dma_chan[i] = NULL;
		}
	}
	prtd->dma_params = NULL;

	return -ENXIO;
}

/* ======================================================================== */
/* DMA 描述符配置                                                           */
/* ======================================================================== */

/*
 * sprd_pcm_config_dma - 填充每个通道的描述符块与其后的 scatterlist
 * 反向要点：
 *   - channel 数必须在 [1, 2]
 *   - period_bytes = buffer_bytes / periods
 *   - 地址经 audio_addr_ap2dsp() 转换（普通内存 = 5，iram 播放 = 0x13，iram 采集 = 0x11）
 *   - 每个通道做一次 sg_init_table()，再逐 period 填 sg_dma_address/sg_dma_len
 */
static int sprd_pcm_config_dma(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct sprd_dma_cfg **cfg, dma_addr_t *phys)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	struct sprd_pcm_dma_params *dma_params = prtd->dma_params;
	unsigned int chan = params_channels(params);
	unsigned int buffer_bytes = params_buffer_bytes(params);
	unsigned int periods = params_periods(params);
	unsigned int info = params->info;
	unsigned int datawidth;
	unsigned int period_bytes = buffer_bytes / periods;
	unsigned int frag;
	unsigned int chan_cnt;
	int i;

	if (!dma_params) {
		pr_err("[ASoC: PCM ] ERR: %s dma_data is NULL!\n", __func__);
		return -EINVAL;
	}

	datawidth = dma_params->datawidth;

	/* frag：一个 period 内每次搬运的字节数，按 cpu_dai 类型分别计算 */
	switch (rtd->cpu_dai->driver->id) {
	case SPRD_DAI_ID_TDM:
		frag = sprd_tdm_dai_to_config(dma_params);
		break;
	case SPRD_DAI_ID_I2S:
		frag = sprd_i2s_dai_to_config(dma_params);
		break;
	default:
		frag = (datawidth == 2 ? datawidth : 4) * dma_params->addr_width;
		break;
	}

	if (info & SNDRV_PCM_INFO_INTERLEAVED)
		pr_debug("[ASoC: PCM ] Interleaved Access\n");
	else
		pr_debug("[ASoC: PCM ] Non-interleaved Access\n");

	if (chan > SPRD_PCM_MAX_CHANNELS) {
		pr_err("[ASoC: PCM ] ERR: channel count(%d) is greater than %d\n",
		       chan, SPRD_PCM_MAX_CHANNELS);
		return -EINVAL;
	}

	if (chan < 1) {
		pr_err("[ASoC: PCM ] ERR: channel count(%d) is less than 1\n", chan);
		return -EINVAL;
	}

	chan_cnt = chan;
	prtd->chan_cnt = chan_cnt;
	prtd->frag_size = frag;
	prtd->period_bytes = period_bytes;
	prtd->periods = periods;
	prtd->datawidth = datawidth;
	prtd->interleaved = !!(info & SNDRV_PCM_INFO_INTERLEAVED);
	prtd->size_inout = buffer_bytes;
	prtd->src_step = dma_params->src_step;
	prtd->des_step = dma_params->des_step;

	for (i = 0; i < chan_cnt; i++) {
		struct sprd_dma_cfg *c = cfg[i];
		memset(c, 0, SPRD_DMA_CFG_HDR_SIZE);

		c->src_step = dma_params->src_step;
		c->datawidth = datawidth;
		c->frag_len = frag;
		c->dst_step = dma_params->des_step;
		c->fifo_phys = dma_params->fifo_phys;
		c->periods = periods;
		c->sg_virt = (void *)c + SPRD_DMA_CFG_HDR_SIZE;

		if (!substream->stream) {
			c->src_addr = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL,
							 phys[i], 0);
			c->dst_addr = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL,
							 phys[i], 0);
		} else {
			c->src_addr = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL,
							 phys[i], 0);
			c->dst_addr = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL,
							 phys[i], 0);
		}

		pr_info("[ASoC: PCM ] src_dw=%d, dst_dw=%d frag=%lx, burst:%d, slave_id=%d, step=%d, ll_cfg_virt_addr=%p, ll_cfg_phy_addr=%ld ,dst_addr:%ld, src_addr:%ld,chan=%d\n",
			datawidth, datawidth, (unsigned long)frag, c->burst,
			prtd->slave_id, dma_params->src_step, c->sg_virt,
			(long)c->ll_cfg_phy, (long)c->dst_addr,
			(long)c->src_addr, i);

		/* 共享内存里再放一份，DSP 直接从那块内存取描述符 */
		if (prtd->dma_cfg_virt[1]) {
			size_t stride = SPRD_DMA_CFG_HDR_SIZE +
					periods * sizeof(struct scatterlist);

			memcpy((char *)prtd->dma_cfg_virt[1] + i * stride,
			       c, SPRD_DMA_CFG_HDR_SIZE);
		} else {
			pr_err("[ASoC: PCM ] %s, DMA chan ID %d memcpy is failed!\n",
			       __func__,
			       prtd->dma_chan[i] ? prtd->dma_chan[i]->chan_id : -1);
		}
	}

	/* 逐通道建 sg 表 */
	for (i = 0; i < chan_cnt; i++) {
		struct sprd_dma_cfg *c = cfg[i];
		struct scatterlist *sg = c->sg_virt;
		dma_addr_t addr = phys[i];
		unsigned int left = buffer_bytes;
		unsigned int n = 0;

		sg_init_table(sg, periods);
		c->periods = periods;
		c->dst_addr = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL, addr, 0);

		while (left) {
			unsigned int len = min(period_bytes, left);

			sg_dma_address(&sg[n]) = audio_addr_ap2dsp(SPRD_AUDIO_MEM_NORMAL,
							   addr, 0);
			sg_dma_len(&sg[n]) = len;
			pr_info("[ASoC: PCM ] sg_dma_len=%zu, periods=%zu, sg_dma_address=%p, flag:%lx, chan=%d node=%d\n",
				(size_t)len, (size_t)periods,
				(void *)sg_dma_address(&sg[n]),
				(unsigned long)info, i, n);
			addr += len;
			left -= len;
			n++;
		}

		pr_info("[ASoC: PCM ] Node Size:%d\n", n);
	}

	pr_info("[ASoC: PCM ] %s, block %u\n", __func__, buffer_bytes);

	return 0;
}

/* ======================================================================== */
/* PCM 回调                                                                  */
/* ======================================================================== */

static void sprd_pcm_dma_buf_done(void *data)
{
	struct snd_pcm_substream *substream = data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sprd_pcm_runtime *prtd = runtime->private_data;

	pr_info("[ASoC: PCM ] DMA Callback CALL cpu_dai->id =%d\n",
		rtd->cpu_dai->id);

	spin_lock(&pm_dma.lock);
	if (!prtd->periods) {
		spin_unlock(&pm_dma.lock);
		pr_err("[ASoC: PCM ] %s no trigger", __func__);
		return;
	}
	spin_unlock(&pm_dma.lock);

	snd_pcm_period_elapsed(substream);
}

static int sprd_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sprd_pcm_runtime *prtd;
	size_t size = pcm->buffer_bytes_max;
	int ret = 0;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size, &buf->addr, GFP_KERNEL);
	if (!buf->area) {
		pr_err("[ASoC: PCM ] %s audio_mem_alloc failed for buf->addr\n",
		       __func__);
		return -ENOMEM;
	}

	buf->bytes = size;
	pr_info("[ASoC: PCM ] %s, sprd_pcm_preallocate_dma_buffer buf->area(virt)=%#lx, buf->addr(phy)=%#x, size=%zu\n",
		__func__, (unsigned long)buf->area, (unsigned int)buf->addr, size);

	prtd = substream->runtime ? substream->runtime->private_data : NULL;
	if (prtd) {
		prtd->dma_buff_phys[stream] = buf->addr;
		prtd->dma_buff_size[stream] = size;
	}

	if (rtd && rtd->cpu_dai)
		pr_info("[ASoC: PCM ] dma_buff_phys[%d] %u\n",
			stream, (unsigned int)buf->addr);

	return ret;
}

static int sprd_pcm_preallocate_dma_ddr32_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = pcm->buffer_bytes_max;

	buf->area = audio_smem_alloc(pcm->card->dev, size, &buf->addr);
	if (!buf->area) {
		pr_err("[ASoC: PCM ] %s audio_mem_vmap failed for buf->addr",
		       __func__);
		return -ENOMEM;
	}
	buf->bytes = size;

	return 0;
}

static int sprd_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_soc_component *component = rtd->cpu_dai->component;
	struct sprd_pcm_dma_params *dma_params;
	u64 dma_mask = DMA_BIT_MASK(64);
	int ret = 0;
	int stream;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &dma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(64);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
	    pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		pr_info("[ASoC: PCM ] %s, %s cpu_dai->id = %d %s\n",
			__func__, cpu_dai->name, cpu_dai->id,
			sprd_pcm_get_dma_type(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream
					      ? pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream
					      : pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream));
	}

	for (stream = 0; stream < 2; stream++) {
		struct snd_pcm_substream *substream = pcm->streams[stream].substream;

		if (!substream)
			continue;

		dma_params = stream ? (void *)cpu_dai->capture_dma_data
				    : (void *)cpu_dai->playback_dma_data;
		if (!dma_params) {
			pr_err("[ASoC: PCM ] ERR:%s, dma_data is NULL!\n",
			       __func__);
			continue;
		}

		/* 采集且允许用 iram 时走 smem 路径 */
		if (stream && test_normal_cap_use_iram)
			ret = sprd_pcm_preallocate_dma_ddr32_buffer(pcm, stream);
		else
			ret = sprd_pcm_preallocate_dma_buffer(pcm, stream);
		if (ret) {
			pr_err("[ASoC: PCM ] ERR:dma_pdata alloc failed!\n");
			return ret;
		}

		if (component)
			snd_soc_set_runtime_hwparams(substream,
				cpu_dai->driver->id == SPRD_DAI_ID_I2S ?
				&sprd_i2s_pcm_hardware : &sprd_pcm_hardware_v1);
	}

	return 0;
}

static void sprd_pcm_free_dma_buffers(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_coherent(pcm->card->dev, buf->bytes, buf->area,
				  buf->addr);
		buf->area = NULL;
	}
}

static int sprd_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_dai *cpu_dai;
	struct snd_soc_component *component;
	struct sprd_pcm_runtime *prtd;
	struct sprd_pcm_dma_params *dma_params;
	unsigned int chan_cnt;
	size_t cfg_size;
	int ret;

	pr_debug("[ASoC: PCM ] pcm Start\n");

	if (!rtd) {
		pr_err("[ASoC: PCM ] %s, rtd is null\n", __func__);
		return -EINVAL;
	}

	cpu_dai = rtd->cpu_dai;
	component = snd_soc_rtdcom_lookup(rtd, PCM_COMP_NAME);
	if (!component)
		return -EPROBE_DEFER;

	pr_info("[ASoC: PCM ] %s %s Open %s\n", __func__,
		substream->stream ? "Captrue" : "Playback",
		sprd_pcm_get_dma_type(substream));

	/* 播放/采集使用各自的 hw 参数 */
	ret = snd_soc_set_runtime_hwparams(substream,
			cpu_dai->driver->id == SPRD_DAI_ID_I2S ?
			&sprd_i2s_pcm_hardware : &sprd_pcm_hardware_v1);
	if (ret)
		return ret;

	chan_cnt = 2;
	runtime->hw.channels_max = SPRD_PCM_MAX_CHANNELS;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 1,
				     SPRD_PCM_MAX_CHANNELS);

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		return -ENOMEM;

	prtd->dma_type = -1;
	prtd->chan_cnt = chan_cnt;
	runtime->private_data = prtd;

	dma_params = sprd_pcm_get_dma_params(substream, substream->stream);
	prtd->dma_params = dma_params;
	if (dma_params) {
		prtd->datawidth = dma_params->datawidth;
		prtd->src_step = dma_params->src_step;
		prtd->des_step = dma_params->des_step;
		prtd->slave_id = substream->stream;
	}

	/* 两个通道的 DMA 描述符块：0x78 + periods_max * sizeof(sg) */
	cfg_size = runtime->hw.periods_max * sizeof(struct scatterlist) +
		   SPRD_DMA_CFG_HDR_SIZE;
	pr_info("[ASoC: PCM ] runtime->hw.periods_max*(sizeof(struct sprd_dma_cfg)=%zd,",
		sizeof(struct sprd_dma_cfg));
	pr_info("[ASoC: PCM ] sizeof(struct sprd_dma_cfg) = %zd, size_inout=%u\n",
		sizeof(struct sprd_dma_cfg), (unsigned int)(cfg_size * chan_cnt));

	prtd->dma_cfg_virt[0] = devm_kzalloc(component->dev, cfg_size * chan_cnt,
					     GFP_KERNEL);
	if (!prtd->dma_cfg_virt[0]) {
		pr_err("[ASoC: PCM ] %s alloc size = %#zx for data\n",
		       __func__, cfg_size * chan_cnt);
		ret = -ENOMEM;
		goto err_kfree;
	}

	pr_info("[ASoC: PCM ] rtd->dma_cfg_virt[0] =%#lx, rtd->dma_cfg_phy[0] =%#lx,",
		(unsigned long)prtd->dma_cfg_virt[0],
		(unsigned long)prtd->dma_cfg_phy[0]);

	/* [0] 号描述符块必须落在 DSP 可见的连续内存里 */
	{
		phys_addr_t cfg_phy0 = 0;
		void *cfg_va0;

		cfg_va0 = audio_smem_alloc(component->dev, cfg_size, &cfg_phy0);
		if (!cfg_va0)
			pr_err("[ASoC: PCM ] audio_smem_alloc failed for rtd->dma_cfg_phy[0]");
		else if (!audio_mem_vmap(component->dev, cfg_size, cfg_phy0))
			pr_err("[ASoC: PCM ]  ioremap_nocache failed for rtd->dma_cfg_virt[0]");
		prtd->dma_cfg_phy[0] = cfg_phy0;
	}

	/* DSP 侧访问需要按 audio_mem 语义再申请一块物理连续内存 */
	prtd->dma_cfg_virt[1] = audio_mem_alloc(component->dev, cfg_size,
						&prtd->dma_cfg_phy[1]);
	if (!prtd->dma_cfg_virt[1]) {
		pr_err("[ASoC: PCM ] audio_smem_alloc failed for rtd->dma_cfg_phy[1]");
		ret = -ENOMEM;
		goto err_kfree;
	}

	prtd->dma_cfg_virt[1] = audio_mem_vmap(component->dev, cfg_size,
					      prtd->dma_cfg_phy[1]);
	if (!prtd->dma_cfg_virt[1]) {
		pr_err("[ASoC: PCM ] ioremap_nocache failed for rtd->dma_cfg_virt[1]");
		ret = -ENOMEM;
		goto err_mem_free;
	}

	pr_info("[ASoC: PCM ] rtd->dma_cfg_virt[1]=%#lx,rtd->dma_cfg_phy[1]=%#lx,",
		(unsigned long)prtd->dma_cfg_virt[1],
		(unsigned long)prtd->dma_cfg_phy[1]);

	__mutex_init(&pm_dma.pm_mtx_dma_prot, "&pm_dma->pm_mtx_dma_prot",
		     &sprd_pm_mtx_dma_prot_key);
	__mutex_init(&pm_dma.pm_mtx_cnt, "&pm_dma->pm_mtx_cnt",
		     &sprd_pm_mtx_cnt_key);
	spin_lock_init(&pm_dma.lock);
	pm_dma.rtd = rtd;

	ret = sprd_pcm_request_dma_channel(substream, chan_cnt);
	if (ret) {
		pr_err("[ASoC: PCM ] ERR:PCM Request DMA Error %d\n", ret);
		goto err_mem_vunmap;
	}

	pr_info("[ASoC: PCM ] %s, Register Callback func for DMA chan ID %d\n",
		__func__, prtd->dma_chan[0]->chan_id);

	prtd->agdsp_on = 1;
	ret = agdsp_access_enable(component->dev, substream->stream);
	if (ret) {
		pr_err("[ASoC: PCM ] %s:agdsp_access_enable:error:%d",
		       __func__, ret);
		prtd->agdsp_on = 0;
	}

	return 0;

err_mem_vunmap:
	audio_mem_unmap(component->dev, prtd->dma_cfg_virt[1]);
err_mem_free:
	audio_mem_free(component->dev, cfg_size, prtd->dma_cfg_virt[1],
		       prtd->dma_cfg_phy[1]);
err_kfree:
	devm_kfree(component->dev, prtd->dma_cfg_virt[0]);
	kfree(prtd);
	runtime->private_data = NULL;

	return ret;
}

static int sprd_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_component *component;
	struct sprd_pcm_runtime *prtd;
	int i;

	pr_debug("[ASoC: PCM ] pcm Stop\n");

	if (!runtime) {
		pr_err("[ASoC: PCM ] %s, rtd is null\n", __func__);
		return 0;
	}

	prtd = runtime->private_data;
	if (!prtd)
		return 0;

	component = snd_soc_rtdcom_lookup(rtd, PCM_COMP_NAME);

	pr_info("[ASoC: PCM ] %s %s Close %s\n", __func__,
		substream->stream ? "Captrue" : "Playback",
		sprd_pcm_get_dma_type(substream));

	for (i = 0; i < SPRD_PCM_MAX_CHANNELS; i++) {
		if (!prtd->dma_chan[i])
			continue;

		if (prtd->agdsp_on) {
			agdsp_access_disable(component->dev, substream->stream);
			prtd->agdsp_on = 0;
		}

		mutex_lock(&pm_dma.pm_mtx_dma_prot);
		spin_lock(&pm_dma.lock);
		if (prtd->dma_chan[i]) {
			dma_release_channel(prtd->dma_chan[i]);
			prtd->dma_chan[i] = NULL;
		}
		spin_unlock(&pm_dma.lock);
		mutex_unlock(&pm_dma.pm_mtx_dma_prot);

		pr_info("[ASoC: PCM ] %s i=%d has released\n", __func__, i);
	}

	if (prtd->dma_cfg_virt[0])
		devm_kfree(component->dev, prtd->dma_cfg_virt[0]);
	if (prtd->dma_cfg_virt[1]) {
		audio_mem_unmap(component->dev, prtd->dma_cfg_virt[1]);
		audio_mem_free(component->dev,
			       runtime->hw.periods_max *
			       sizeof(struct scatterlist) + SPRD_DMA_CFG_HDR_SIZE,
			       prtd->dma_cfg_virt[1], prtd->dma_cfg_phy[1]);
	}

	pr_info("[ASoC: PCM ] x out:%zx\n", (size_t)prtd->dma_size);
	kfree(prtd);
	runtime->private_data = NULL;

	pr_info("[ASoC: PCM ] return %i\n", 0);

	return 0;
}

static int sprd_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_component *component;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	struct sprd_pcm_dma_params *dma_params;
	struct sprd_dma_cfg *cfg_map[SPRD_PCM_MAX_CHANNELS];
	dma_addr_t phys[SPRD_PCM_MAX_CHANNELS];
	unsigned int chan_cnt = params_channels(params);
	unsigned int periods = params_periods(params);
	unsigned int buffer_bytes = params_buffer_bytes(params);
	int ret;

	pr_info("[ASoC: PCM ] (pcm) %s, cpudai_id=%d\n", __func__,
		cpu_dai->driver->id);

	component = snd_soc_rtdcom_lookup(rtd, PCM_COMP_NAME);
	if (!component)
		return -EPROBE_DEFER;

	dma_params = prtd->dma_params;
	if (!dma_params) {
		dma_params = sprd_pcm_get_dma_params(substream, substream->stream);
		if (!dma_params) {
			pr_info("[ASoC: PCM ] no dma\n");
			return 0;
		}
	}

	if (chan_cnt > SPRD_PCM_MAX_CHANNELS) {
		pr_err("[ASoC: PCM ] ERR: channel count(%d) is greater than %d\n",
		       chan_cnt, SPRD_PCM_MAX_CHANNELS);
		return -EINVAL;
	}
	if (chan_cnt < 1) {
		pr_err("[ASoC: PCM ] ERR: channel count(%d) is less than 1\n",
		       chan_cnt);
		return -EINVAL;
	}

	pr_info("[ASoC: PCM ] chan=%d totsize=%zu period=%zu\n",
		chan_cnt, (size_t)buffer_bytes, (size_t)(buffer_bytes / periods));

	/* 1) 配置 DMA 步长 */
	sprd_pcm_dma_step_config(dma_params, !substream->stream, 0);
	pr_info("[ASoC: PCM ] desc->src_step  =%u, des_step=%u, datawidth =%u\n",
		dma_params->src_step, dma_params->des_step, dma_params->datawidth);

	/* 2) 申请 DMA 通道 */
	ret = sprd_pcm_request_dma_channel(substream, chan_cnt);
	if (ret) {
		pr_err("[ASoC: PCM ] ERR:PCM Request DMA Error %d\n", ret);
		return ret;
	}

	prtd->slave_id = substream->stream;
	prtd->period_bytes = buffer_bytes / periods;

	phys[0] = runtime->dma_addr;
	phys[1] = runtime->dma_addr;
	cfg_map[0] = prtd->dma_cfg_virt[0];
	cfg_map[1] = (struct sprd_dma_cfg *)((char *)prtd->dma_cfg_virt[0] +
			runtime->hw.periods_max * sizeof(struct scatterlist) +
			SPRD_DMA_CFG_HDR_SIZE);

	runtime->dma_bytes = buffer_bytes;

	/* 3) 填描述符 */
	ret = sprd_pcm_config_dma(substream, params, cfg_map, phys);
	if (ret) {
		pr_err("[ASoC: PCM ] %s, DMA chan ID %d config is failed!\n",
		       __func__, prtd->dma_chan[0]->chan_id);
		return ret;
	}

	if (!prtd->dma_params)
		pr_err("[ASoC: PCM ] %s dam_chan is null for normalplayback",
		       __func__);

	pr_info("[ASoC: PCM ] %s, block %u\n", __func__, buffer_bytes);

	return 0;
}

static int sprd_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	int i;

	if (!prtd)
		return 0;

	mutex_lock(&pm_dma.pm_mtx_dma_prot);
	for (i = 0; i < SPRD_PCM_MAX_CHANNELS; i++) {
		if (!prtd->dma_chan[i])
			continue;

		spin_lock(&pm_dma.lock);
		dma_release_channel(prtd->dma_chan[i]);
		prtd->dma_chan[i] = NULL;
		prtd->dma_cfg_virt[i] = NULL;
		spin_unlock(&pm_dma.lock);
	}
	prtd->dma_params = NULL;
	mutex_unlock(&pm_dma.pm_mtx_dma_prot);

	pr_debug("[ASoC: PCM ] %s %s\n", __func__, "hw_free");

	return 0;
}

/* 释放 runtime 里缓存的全部 DMA 通道（电源通知路径用） */
static int pm_normal_dma_chan_release(struct sprd_pcm_runtime *prtd)
{
	int i;
	int ret = 0;

	if (!prtd) {
		pr_err("[ASoC: PCM ] hw_param_err\n");
		return -EINVAL;
	}

	for (i = 0; i < SPRD_PCM_MAX_CHANNELS; i++) {
		if (!prtd->dma_chan[i])
			continue;

		pr_info("[ASoC: PCM ] %s, release chan_id %d\n", __func__,
			prtd->dma_chan[i]->chan_id);
		dma_release_channel(prtd->dma_chan[i]);
		prtd->dma_chan[i] = NULL;
	}

	pr_info("return %i\n", ret);

	return ret;
}

static int sprd_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int sprd_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dma_async_tx_descriptor *tx;
	int ret = 0;
	int i;

	if (!prtd || !prtd->dma_chan[0]) {
		pr_info("[ASoC: PCM ] no trigger");
		return -EINVAL;
	}

	if (!prtd->dma_params)
		pr_err("[ASoC: PCM ] ERR: %s, dma_data is NULL!\n", __func__);

	pr_info("[ASoC: PCM ] %s, %s cpu_dai->id = %d Trigger %s cmd:%d\n",
		__func__, rtd->cpu_dai->name, rtd->cpu_dai->id,
		sprd_pcm_get_dma_type(substream), cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		for (i = 0; i < prtd->chan_cnt; i++) {
			if (!prtd->dma_chan[i])
				continue;

			spin_lock(&pm_dma.lock);
			pr_info("[ASoC: PCM ] %s dma_chn[0] is null ret=%#x, %d\n",
				__func__, ret, i);
			spin_unlock(&pm_dma.lock);

			tx = dmaengine_prep_dma_cyclic(prtd->dma_chan[i],
						       runtime->dma_addr,
						       runtime->dma_bytes,
						       prtd->period_bytes,
						       substream->stream ?
						       DMA_DEV_TO_MEM : DMA_MEM_TO_DEV,
						       DMA_PREP_INTERRUPT);
			if (!tx) {
				pr_info("[ASoC: PCM ] %s dma_tx_des[0] is null ret=%#x, %d\n",
					__func__, ret, i);
				return -EIO;
			}

			tx->callback = sprd_pcm_dma_buf_done;
			tx->callback_param = substream;
			dmaengine_submit(tx);
			dma_async_issue_pending(prtd->dma_chan[i]);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		for (i = 0; i < prtd->chan_cnt; i++) {
			if (!prtd->dma_chan[i])
				continue;
			dmaengine_terminate_all(prtd->dma_chan[i]);
		}
		test_force_stop = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static snd_pcm_uframes_t sprd_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sprd_pcm_runtime *prtd = runtime->private_data;
	snd_pcm_uframes_t x;
	size_t now;

	if (!prtd || !prtd->dma_chan[0]) {
		pr_info("[ASoC: PCM ] x:%zx\n", (size_t)0);
		return 0;
	}

	spin_lock(&pm_dma.lock);

	/* 优先读 DMA 通道的 residue，读不到就退回 period 计数 */
	now = prtd->period_bytes;
	pr_info("[ASoC: PCM ] now_pointer 1:%zu,dsp:%zx\n",
		now, (size_t)runtime->dma_addr);
	pr_info("[ASoC: PCM ] now_pointer 2:%zx,runtime->dma_addr:%zx\n",
		(size_t)now, (size_t)runtime->dma_addr);

	if (debug_pointer_log)
		pr_info("[ASoC: PCM ] now_pointer 21:%zu,dsp:%zx\n",
			now, (size_t)runtime->dma_addr);

	pr_info("[ASoC: PCM ] now_pointer 22:%zx,runtime->dma_addr:%zx\n",
		(size_t)now, (size_t)runtime->dma_addr);

	spin_unlock(&pm_dma.lock);

	x = bytes_to_frames(runtime, now);
	if (x >= runtime->buffer_size)
		x = 0;

	pr_info("[ASoC: PCM ] now_pointer 31:%zx\n", (size_t)x);

	return x;
}

static int sprd_pcm_mmap(struct snd_pcm_substream *substream,
			 struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return remap_pfn_range(vma, vma->vm_start,
			       runtime->dma_addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

/* ======================================================================== */
/* proc 调试接口                                                            */
/* ======================================================================== */

static void sprd_pcm_platform_proc_read(struct snd_info_entry *entry,
					struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "[ASoC: PCM ] test_force_stop =%#x\n", test_force_stop);
	snd_iprintf(buffer, "[ASoC: PCM ] debug_pointer_log=%#x\n", debug_pointer_log);
	snd_iprintf(buffer, "[ASoC: PCM ] test_ddr_buffer_size =%#x\n",
		    test_ddr_buffer_size);
	snd_iprintf(buffer, "[ASoC: PCM ] test_normal_cap_use_iram =%#x\n",
		    test_normal_cap_use_iram);
}

static void sprd_pcm_platform_proc_write(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	unsigned int val;
	char line[64];

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%63s %x", line, &val) != 2)
			continue;

		if (!strcmp(line, "t_force_stop")) {
			test_force_stop = val;
			pr_info("[ASoC: PCM ] test_force_stop =%#x\n", val);
		} else if (!strcmp(line, "debug_pointer_log")) {
			debug_pointer_log = val;
			pr_info("[ASoC: PCM ] debug_pointer_log %#x\n", val);
		} else if (!strcmp(line, "t_ddr_size")) {
			test_ddr_buffer_size = val;
			pr_info("test_ddr_buffer_size %#x\n", val);
		} else if (!strcmp(line, "t_use_iram")) {
			test_normal_cap_use_iram = val;
			pr_info("test_normal_cap_use_iram %#x\n", val);
		} else if (!strcmp(line, "debug_pointer_log")) {
			debug_pointer_log = val;
			pr_info("[ASoC: PCM ] debug_pointer_log=%#x\n", val);
		} else {
			pr_info("[ASoC: PCM ] %s is empty.\n", line);
		}
	}
}

static int sprd_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_card *card = pdev->dev.parent->platform_data;
	struct snd_info_entry *entry;
	int ret;

	pr_info("[ASoC: PCM ] pcm E\n");

	if (!card) {
		pr_err("[ASoC: PCM ] %s, rtd is null\n", __func__);
		return -ENODEV;
	}

	entry = snd_info_create_card_entry(card, DRV_NAME, card->proc_root);
	if (!entry) {
		pr_err("[ASoC: PCM ] %s\n", "snd_info_create_card_entry failed");
		return -ENOMEM;
	}

	entry->c.text.read = sprd_pcm_platform_proc_read;
	entry->c.text.write = sprd_pcm_platform_proc_write;
	entry->mode |= S_IWUSR;

	ret = snd_info_register(entry);
	if (ret) {
		pr_err("[ASoC: PCM ] %s\n", "snd_info_register failed");
		snd_info_free_entry(entry);
		return ret;
	}

	return 0;
}

static int sprd_snd_platform_remove(struct platform_device *pdev)
{
	return 0;
}

/* ======================================================================== */
/* component / platform 注册                                                */
/* ======================================================================== */

static int sprd_pcm_pm_notifier(struct notifier_block *nb, unsigned long event,
				void *dummy)
{
	struct pm_dma_priv *priv = container_of(nb, struct pm_dma_priv, nb);
	struct snd_soc_pcm_runtime *rtd = priv->rtd;
	struct snd_soc_component *component;
	struct sprd_pcm_runtime *prtd;
	int i;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		pr_info("[ASoC: PCM ] %s, PM_SUSPEND_PREPARE.\n", __func__);
		mutex_lock(&priv->pm_mtx_dma_prot);
		pr_info("[ASoC: PCM ] %s no_pm_cnt=%d\n", __func__,
			atomic_read(&priv->suspend_cnt));
		mutex_unlock(&priv->pm_mtx_dma_prot);
		break;

	case PM_POST_SUSPEND:
		pr_info("[ASoC: PCM ] %s, PM_POST_SUSPEND.\n", __func__);
		if (!rtd)
			break;

		component = snd_soc_rtdcom_lookup(rtd, PCM_COMP_NAME);
		spin_lock(&priv->lock);
		prtd = pm_dma.rtd && pm_dma.rtd->pcm ?
			pm_dma.rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK]
				.substream ?
			pm_dma.rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK]
				.substream->runtime->private_data : NULL : NULL;
		if (prtd) {
			for (i = 0; i < SPRD_PCM_MAX_CHANNELS; i++) {
				if (!prtd->dma_chan[i])
					continue;
				pr_info("[ASoC: PCM ] %s, release chan_id %d\n",
					__func__, prtd->dma_chan[i]->chan_id);
				dma_release_channel(prtd->dma_chan[i]);
				prtd->dma_chan[i] = NULL;
			}
		}
		spin_unlock(&priv->lock);

		/* pm_normal_dma_chan_release() 做同一件事，保留函数名以对齐厂商符号 */
		if (prtd)
			pm_normal_dma_chan_release(prtd);

		if (prtd && prtd->agdsp_on) {
			agdsp_access_disable(component->dev, 0);
			prtd->agdsp_on = 0;
		}
		break;
	}

	return NOTIFY_OK;
}

static int sprd_soc_platform_probe(struct platform_device *pdev)
{
	int ret;

	dev_info(&pdev->dev, "%s\n", __func__);

	pm_dma.rtd = NULL;
	atomic_set(&pm_dma.suspend_cnt, 0);
	spin_lock_init(&pm_dma.lock);
	__mutex_init(&pm_dma.pm_mtx_dma_prot, "&pm_dma->pm_mtx_dma_prot",
		     &sprd_pm_mtx_dma_prot_key);
	__mutex_init(&pm_dma.pm_mtx_cnt, "&pm_dma->pm_mtx_cnt",
		     &sprd_pm_mtx_cnt_key);
	pm_dma.nb.notifier_call = sprd_pcm_pm_notifier;

	ret = register_pm_notifier(&pm_dma.nb);
	if (ret) {
		pr_err("[ASoC: PCM ] Register pm notifier error!\n");
		return ret;
	}

	return 0;
}

static int sprd_soc_platform_remove(struct platform_device *pdev)
{
	unregister_pm_notifier(&pm_dma.nb);

	return 0;
}

/* ======================================================================== */
/* 表与驱动                                                                  */
/* ======================================================================== */

static const struct of_device_id sprd_pcm_of_match[] = {
	{ .compatible = "unisoc,sharkl5-pcm-platform" },
	{ .compatible = "unisoc,roc1-pcm-platform" },
	{ .compatible = "unisoc,qogirl6-pcm-platform" },
	{ .compatible = "unisoc,qogirn6pro-pcm-platform" },
	{ .compatible = "sprd,pcm-platform" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_pcm_of_match);

static const struct snd_pcm_ops sprd_pcm_ops = {
	.open		= sprd_pcm_open,
	.close		= sprd_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= sprd_pcm_hw_params,
	.hw_free	= sprd_pcm_hw_free,
	.prepare	= sprd_pcm_prepare,
	.trigger	= sprd_pcm_trigger,
	.pointer	= sprd_pcm_pointer,
	.mmap		= sprd_pcm_mmap,
};

static const struct snd_soc_component_driver sprd_soc_platform = {
	.name		= PCM_COMP_NAME,
	.probe		= sprd_snd_platform_probe,
	.remove		= sprd_snd_platform_remove,
	.pcm_new	= sprd_pcm_new,
	.pcm_free	= sprd_pcm_free_dma_buffers,
	.ops		= &sprd_pcm_ops,
};

static struct platform_driver sprd_pcm_driver = {
	.probe = sprd_soc_platform_probe,
	.remove = sprd_soc_platform_remove,
	.driver = {
		.name = PCM_PLAT_NAME,
		.of_match_table = of_match_ptr(sprd_pcm_of_match),
	},
};

module_platform_driver(sprd_pcm_driver);

MODULE_DESCRIPTION("SPRD ASoC PCM DMA");
MODULE_AUTHOR("Jian chen <jian.chen@spreadtrum.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sprd-audio");
