// SPDX-License-Identifier: GPL-2.0
/*
 * sprd-compr-2stage-dma.c - Spreadtrum two stage DMA compressed offload
 *                          platform driver (ASoC compress)
 *
 * 原始厂商源码路径（来自 .rodata 内的 __FILE__ 字面量）:
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd/platform/compr_2stage_dma/sprd-compr-2stage-dma.c
 *
 * 本文件是对 UMS9157(T157) 出厂 boot_a.bin 中树外模块
 * sprd_compr_2stage_dma.ko 的反汇编重建（模块 .text 0x3838 字节，34 个函数）。
 *
 * 两级 DMA 的含义：
 *   stage0 : DSP 侧 IRAM/共享内存 <-> 采集(ADC)或播放(DAC) FIFO 的第一级搬运，
 *            使用 mcdt_adc/adc dma 通道 + wrap 地址环形缓冲；
 *   stage1 : AP DDR(压缩码流 buffer) -> DSP 共享内存 的第二级搬运，
 *            使用 dma_request_slave_channel("compr_dma_stage1") 的 sgdma 通道。
 *   两者都由 AP 侧 dmaengine 描述符驱动，DSP 通过 mail box(sipc smsg) 与 AP 交互。
 *
 * 依赖树外模块: audio_sipc, mcdt_hw_r2p0, audio_mem, agdsp_access, snd-soc-sprd-card
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/compress_driver.h>
#include <sound/compress_offload.h>
#include <sound/compress_params.h>
#include <sound/pcm.h>

/* ------------------------------------------------------------------------- */
/* 外部依赖（同 tag 的树外模块，符号由 audio_sipc/mcdt_hw_r2p0/audio_mem/    */
/* agdsp_access 导出；函数原型按这些模块的反汇编结果填写）                     */
/* ------------------------------------------------------------------------- */

/* audio_mem.ko: 音频内存池分配/映射/AP<->DSP 地址转换 */
extern u32 audio_mem_alloc(u32 mem_type, u32 *size, u32 request_size);
extern void audio_mem_free(u32 mem_type, u32 addr, u32 size);
extern void *audio_mem_vmap(u32 mem_addr, u32 flag);
extern void audio_mem_unmap(void *vaddr);
extern u32 audio_addr_ap2dsp(u32 mem_type, u32 addr, u32 flag);

/* mcdt_hw_r2p0.ko: MCDT 两路 DMA（ADC/DAC）控制 */
extern int mcdt_adc_dma_enable(void);
extern int mcdt_adc_dma_disable(void);
extern int mcdt_dac_dma_enable(void);
extern int mcdt_dac_dma_disable(void);
extern u32 mcdt_dac_dma_phy_addr(u32 direction);
extern void mcdt_ad_fifo_clr(void);
extern void mcdt_da_fifo_clr(void);

/* audio_sipc.ko: AP<->DSP 的 SIPC 通道与短消息 */
extern int aud_ipc_ch_open(u32 channel, void *param);
extern int aud_ipc_ch_close(u32 channel);
extern int aud_smsg_send(u32 channel, u16 cmd, void *data, u32 len);
extern int aud_smsg_recv(u32 channel, u16 *cmd, void *data, u32 len);

/* agdsp_access.ko: 访问 AGDSP 的内部寄存器/内存前的上电/授权开关 */
extern int agdsp_access_enable(u32 id);
extern int agdsp_access_disable(u32 id);

/* snd-soc-sprd-card.ko / 平台: 音频调试打印开关（0 = 关闭 verbose 日志） */
extern int get_sp_audio_debug_flag(void);

/*
 * 厂家的 compr_dbg(): 受 get_sp_audio_debug_flag() 控制的追踪打印，
 * 固定输出 "[ASoC:COMPR] <函数名>, line: <行号>"。
 */
#define compr_dbg()							\
	do {								\
		if (get_sp_audio_debug_flag())				\
			printk(KERN_INFO "[ASoC:COMPR] %s, line: %d\n",	\
			       __func__, __LINE__);			\
	} while (0)

/* audio_sipc 通道号：1 = 压缩流通道，6 = offload drain 通道 */
#define SPRD_COMPR_CH_STAGE0		1
#define SPRD_COMPR_CH_DRAIN		6

/* 跨模块保留的私有钩子（本模块 .text 中只有一个 ret 实现，供 card 驱动可选接管） */
extern int sprd_compr_configure_dsp(void);

/* ------------------------------------------------------------------------- */
/* 常量                                                                      */
/* ------------------------------------------------------------------------- */

#define SPRD_COMPR_DRV_NAME		"sprd_compr_2stage_dma"
#define SPRD_COMPR_PLATFORM_NAME	"sprd-compr-platform"
#define SPRD_COMPR_COMP_NAME		"sprd-compr-dma"
#define SPRD_COMPR_CARD_NAME		"sprd-compress"

/* dma_request_slave_channel() 使用的通道名 */
#define SPRD_COMPR_DMA_STAGE0		"compr_dma_stage0"
#define SPRD_COMPR_DMA_STAGE1		"compr_dma_stage1"

/* audio_mem_alloc() 的 type 参数 */
#define SPRD_COMPR_MEM_STAGE0		8	/* iram/ddr 混合，stage0 环形缓冲 */
#define SPRD_COMPR_MEM_STAGE1		5	/* DDR32 共享内存，stage1 目标 */

/* audio_addr_ap2dsp() 的 type 参数 */
#define SPRD_COMPR_ADDR_STAGE1		8
#define SPRD_COMPR_ADDR_STAGE0		5

/* DSP 侧命令 ID（sipc 短消息 cmd 字段） */
#define SPRD_COMPR_CMD_OPEN		0x6c
#define SPRD_COMPR_CMD_CLOSE		0x6d
#define SPRD_COMPR_CMD_START		0x6e
#define SPRD_COMPR_CMD_STOP		0x6f
#define SPRD_COMPR_CMD_DRAIN		0x70
#define SPRD_COMPR_CMD_NEXT_TRACK	0x71
#define SPRD_COMPR_CMD_PARTIAL_DRAIN	0x72

/* srtd->stream_state（srtd + 0x2df0）状态机 */
#define SPRD_COMPR_STATE_IDLE		0
#define SPRD_COMPR_STATE_OPEN		1
#define SPRD_COMPR_STATE_START		2
#define SPRD_COMPR_STATE_DRAIN		3
#define SPRD_COMPR_STATE_CONFIG		4
#define SPRD_COMPR_STATE_DRAIN_DONE	5

/* 原厂实现的魔数（反汇编立即数） */
#define SPRD_COMPR_STAGE0_MIN_SIZE	0x1000	/* stage0 至少 4K */
#define SPRD_COMPR_SIZE_LIMIT		0x14000	/* mmap/写指针窗口上限 */
#define SPRD_COMPR_HDR_PER_FRAG		0x78	/* 每个 fragment 的 DSP 头部 */
#define SPRD_COMPR_HDR_BASE		0x78
#define SPRD_COMPR_WRAP_OFS		0x3f0	/* stage0 末尾 wrap 区偏移 */
#define SPRD_COMPR_IRAM_RSV		0x400	/* stage0 尾部保留 */
#define SPRD_COMPR_RTD_COUNT		2

/* 编解码器编号：DSP 只认 MP3(1) 与 AAC(2) 两类 offload 码流 */
#define SPRD_COMPR_CODEC_MP3		1
#define SPRD_COMPR_CODEC_AAC		2
#define SND_AUDIOCODEC_MP3_ID		2
#define SND_AUDIOCODEC_AAC_ID		6

/* set_metadata() 支持的 metadata key（snd_compr_metadata.key） */
#define SPRD_COMPR_META_BIT_RATE	0x63
#define SPRD_COMPR_META_SAMPLE_RATE	0x64
#define SPRD_COMPR_META_CHANNELS	0x65
#define SPRD_COMPR_META_CODEC_TYPE	0x66
#define SPRD_COMPR_META_ENCODER_DELAY	0x67
#define SPRD_COMPR_META_ENCODER_PADDING	0x68

/* ------------------------------------------------------------------------- */
/* 数据结构                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * DSP 侧描述符：放在 audio_mem 共享内存里，由 DSP 固件直接读取
 * （原模块里 pdata->dma_cfg / cfg_ptr->ll_cfg 都指向这块布局）。
 */
struct sprd_dma_ll_cfg {
	u64 phy_addr;			/* DSP 侧源物理地址 */
	void *src;			/* DSP 侧源虚拟地址 */
	void *dst;			/* DSP 侧目的虚拟地址 */
	u32 dma_chan;			/* 通道号 */
	u32 wrap;			/* 回卷偏移（stage0 用） */
	u64 wrap_addr;			/* 回卷地址 */
	u32 total_len;
	u32 block_len;
	u32 tran_len;
	u32 periods;
};

/*
 * AP 侧 DMA 配置块（每个通道一块），分配方式与 sprd-dmaengine-pcm.c 相同：
 *
 *	cfg = kzalloc(sizeof(*cfg) + periods * sizeof(struct scatterlist), GFP_KERNEL);
 *	cfg->sg_virt = (char *)cfg + sizeof(*cfg);
 *
 * 头部 +0x000..+0x070 的字段偏移与 sprd-dmaengine-pcm.c 的 struct sprd_dma_cfg
 * 一致（同一套 sprd dmaengine 驱动）；本模块另外需要 AP/DSP 两侧回调与
 * DSP 描述符，故在 sg 表之后追加 tx_desc/flags/ll_cfg。
 */
struct sprd_dma_cfg {
	u32 direction;			/* +0x000 dma_transfer_direction */
	u32 chan_id;			/* +0x004 0 = stage0, 1 = stage1 */
	u64 src_addr;			/* +0x008 AP 视角源地址 */
	u64 dst_addr;			/* +0x010 AP 视角目的地址 */
	u32 datawidth;			/* +0x018 */
	u32 frag_len;			/* +0x01c 每个 period 字节数 */
	u32 src_step_type;		/* +0x020 */
	u32 dst_step_type;		/* +0x024 */
	dma_async_tx_callback callback;	/* +0x028 搬运完成回调 */
	u32 src_step;			/* +0x030 */
	u32 dst_step;			/* +0x034 */
	u32 fifo_phys;			/* +0x038 MCDT FIFO 物理地址 */
	void *callback_param;		/* +0x040 回调参数 */
	u64 ll_cfg_phy;			/* +0x048 DSP 描述符物理地址 */
	u32 src_frag_len;		/* +0x050 */
	u32 chan_cnt;			/* +0x054 */
	u64 chan_flag;			/* +0x058 dma 控制标志 */
	u32 total_len;			/* +0x060 */
	u32 block_len;			/* +0x064 */
	u32 tran_len;			/* +0x068 */
	u32 periods;			/* +0x06c sg entry 个数 */
	void *sg_virt;			/* +0x070 指向本块尾部的 sg 表 */
	struct dma_async_tx_descriptor *tx_desc;	/* 由 config 返回的描述符 */
	u32 flags;
	struct sprd_dma_ll_cfg ll_cfg;	/* DSP 侧描述符（cfg_ptr->ll_cfg） */
	/* struct scatterlist sg[];	 sg 表在 cfg->sg_virt 处 */
};

/*
 * 每个压缩流（snd_compr_stream）的私有上下文。字段偏移全部来自反汇编的
 * ldr/str 立即数，注释里的 "+0xNNN" 就是原模块使用的偏移。
 */
struct sprd_compr_stream {
	/* +0x000: 由 sprd_compr_dma_param_out 模板整体拷贝进来的 DMA 参数 */
	struct sprd_compr_dma_params {
		u32 channel;		/* +0x000 */
		u32 hw_req_id;		/* +0x004 */
		u32 chan_cnt;		/* +0x008 = 2 (stage0 + stage1) */
		u32 frag_size;		/* +0x00c */
		u32 period_size;	/* +0x010 */
		u32 fragments;		/* +0x014 */
		u32 periods;		/* +0x018 */
		u32 buf_size;		/* +0x01c */
		u32 rate;		/* +0x020 */
		u32 channels;		/* +0x024 */
		u32 format;		/* +0x028 */
		u32 datawidth;		/* +0x02c */
		u32 reserved[24];	/* +0x030..0x08c 由模板填 0 */
	} dma_params;

	struct dma_chan *dma_chan_stage0;	/* +0x090 */
	struct dma_chan *dma_chan_stage1;	/* +0x098 */
	struct sprd_dma_cfg *dma_cfg;		/* +0x0a0 cfg_ptr */
	u32 stage0_handle;			/* +0x0a8 */
	void __iomem *stage0_virt;		/* +0x0b0 */
	u32 stage0_tot_size;			/* +0x0b8 */
	u32 stage1_dsp_addr;			/* +0x0c0 */
	void __iomem *stage0_end_virt;		/* +0x0c8 */
	void __iomem *stage1_virt;		/* +0x0d0 */
	struct dma_async_tx_descriptor *dma_tx_des[SPRD_COMPR_RTD_COUNT]; /* +0x0d8/+0x0e0 */
	u32 dma_tx_des_valid;			/* +0x0e8 */
	u32 stream_state;			/* +0x0f0 */
	u32 trigger_cmd;			/* +0x0f4 */
	u32 running;				/* +0x0f8 */
	u32 started;				/* +0x0fc */
	struct snd_compr_stream *cstream;	/* +0x100 */
	struct snd_compr_caps caps;		/* +0x108 get_caps() 直接 memcpy 出去 */
	u32 next_track;				/* +0x1cc */
	u32 direction;				/* +0x1d0 */
	void *pdata_ref;			/* +0x120 */
	/* 以下为 0x2d54 之后的尾部（编译期对齐填充省略） */
	struct snd_compr_params compr_params;	/* +0x2d54 */
	u32 bit_rate;				/* +0x2e30 同 compr_params.codec.bit_rate */
	u32 meta_sample_rate;			/* +0x2d68 */
	u32 meta_bit_rate;			/* +0x2d60 */
	u32 meta_channels;			/* +0x2d6c */
	u32 meta_codec_type;			/* +0x2d6c 同一槽位的另一种用途 */
	u32 encoder_delay;			/* SNDRV_COMPRESS_ENCODER_DELAY 元数据 */
	u32 encoder_padding;			/* SNDRV_COMPRESS_ENCODER_PADDING 元数据 */
	u32 stream_id;				/* +0x2dec */
	u32 state;				/* +0x2df0 */
	u32 codec_type;				/* +0x2df4 */
	u32 stage1_handle;			/* +0x2df8 */
	void __iomem *stage1_map;		/* +0x2e00 */
	u32 app_pointer;			/* +0x2e08 */
	u32 received_stage0;			/* +0x2e0c */
	u32 stage1_tot_size;			/* +0x2e10 */
	u32 copied_total;			/* +0x2e14 */
	u32 iram_write_pos;			/* +0x2e18 */
	u32 pre_write_pos;			/* +0x2e1c */
	u32 received_total;			/* +0x2e20 */
	u32 stage0_write_pos;			/* +0x2e24 */
	u32 avail_total;			/* +0x2e28 */
	u32 sample_rate;			/* +0x2e2c */
	u32 last_trigger_cmd;			/* +0x2e34 */
	u32 buf_done_count;			/* +0x2e38 */
	spinlock_t lock;			/* +0x2e40 */
	u32 drain_ready;			/* +0x2e44 */
	u32 trigger_flag;			/* +0x2e48 */
	u32 pending_cmd;			/* +0x2e50 */
	u32 stage0_dsp_addr;			/* +0x2f1c */
	u32 shadow_wp;				/* +0x2f18 */
	struct snd_dma_buffer pcm_buf[2];	/* +0x2ea0..0x2eb8 */
	struct work_struct drain_work;		/* drain 完成后的收尾工作 */
	u32 wake_lock_on;			/* +0x2ed0 */
	struct wakeup_source *ws;		/* +0x2ed8 */
	wait_queue_head_t flush_wait;		/* 0x0688 名字来自 __init_waitqueue_head */
	wait_queue_head_t eos_wait;		/* 0x09ad 名字来自 __init_waitqueue_head */
	wait_queue_head_t drain_wait;		/* 0x130e 名字来自 __init_waitqueue_head */
};

/* 平台数据（platform_device.dev.platform_data / component->dev->platform_data） */
struct sprd_compr_data {
	struct device *dev;			/* +0x088 */
	struct workqueue_struct *compr_wq;	/* +0x028 */
	struct sprd_compr_stream *active[SPRD_COMPR_RTD_COUNT];
	struct mutex dev_ctrl;			/* +0x110 名字 &pdata->dev_ctrl.mutex */
	wait_queue_head_t point_wait;		/* +0x064b */
	u32 dma_chn_request;			/* +0x08e5 */
	u8 agdsp_on;				/* +0xd4 */
	u32 suspended;				/* suspend 置 1，resume 清 0 */
	struct snd_info_entry *proc_entry;
	struct snd_card *card;			/* +0x018 */
	u32 point_wait_flag;
};

static int s_buf_done_count;

/* 平台驱动 / component / compress 回调表 */
static int sprd_platform_compr_probe(struct platform_device *pdev);
static int sprd_platform_compr_remove(struct platform_device *pdev);
static int sprd_platform_compr_suspend(struct platform_device *pdev,
				       pm_message_t state);
static int sprd_platform_compr_resume(struct platform_device *pdev);

static int sprd_compr_probe(struct snd_soc_component *component);
static int sprd_compress_new(struct snd_soc_pcm_runtime *rtd);

/*
 * 下面是 struct snd_compr_ops 的成员（component->driver->compr_ops），
 * 签名与 include/sound/compress_driver.h:106 一致：只收 snd_compr_stream。
 */
static int sprd_platform_compr_open(struct snd_compr_stream *cstream);
static int sprd_platform_compr_free(struct snd_compr_stream *cstream);
static int sprd_platform_compr_set_params(struct snd_compr_stream *cstream,
					  struct snd_compr_params *params);
static int sprd_platform_compr_set_metadata(struct snd_compr_stream *cstream,
					    struct snd_compr_metadata *metadata);
static int sprd_platform_compr_trigger(struct snd_compr_stream *cstream,
				       int cmd);
static int sprd_platform_compr_pointer(struct snd_compr_stream *cstream,
				       struct snd_compr_tstamp *tstamp);
static int sprd_platform_compr_copy(struct snd_compr_stream *cstream,
				    char __user *buf, size_t count);
static int sprd_platform_compr_ack(struct snd_compr_stream *cstream,
				   size_t bytes);
static int sprd_platform_compr_get_caps(struct snd_compr_stream *cstream,
					struct snd_compr_caps *caps);
static int sprd_platform_compr_get_codec_caps(struct snd_compr_stream *cstream,
					      struct snd_compr_codec_caps *codec);

/* 内部工具 */
static int compr_ch_open(u32 ch);
static int compr_ch_close(u32 ch);
static void sprd_compr_populate_codec_list(struct snd_compr_caps *caps);
static int compr_stream_config_dma0(struct snd_compr_stream *cstream, u32 frag_size);
static int compr_stream_config_dma1(struct snd_compr_stream *cstream,
				    u32 frag_cnt, u32 frag_size);
static int compr_stream_hw_params0(struct snd_compr_stream *cstream, u32 frag_size);
static int compr_stream_hw_params1(struct snd_compr_stream *cstream,
				   u32 frag_cnt, u32 frag_size);
static int sprd_compr_config_dma_channel(struct dma_chan *chan,
					 struct sprd_dma_cfg *cfg,
					 void *cb_data, u32 flags);
static void sprd_compr_dma_buf_done(void *data);
static void sprd_compr_drain_work(struct work_struct *work);
static int compr_stream_trigger(struct sprd_compr_stream *srtd);
static int sprd_platform_send_cmd(struct sprd_compr_stream *srtd, u32 cmd);
static int sprd_platform_send_param(struct sprd_compr_stream *srtd, u32 direction,
				    void *param);
static int sprd_platform_compr_update_params(struct snd_compr_stream *cstream);
static int sbuf_copy_from_user(void *dst, const void __user *src, u32 len);
static void sprd_compress_proc_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer);

/* ------------------------------------------------------------------------- */
/* DSP 相关占位钩子                                                          */
/* ------------------------------------------------------------------------- */

/*
 * int sprd_compr_configure_dsp(void)
 *
 * 原模块里这个函数体只有一条 `mov w0, wzr; ret`（.text 0x398, 8 字节）：
 * 早期版本用它给 DSP 下发两级 DMA 的 IRAM 布局，量产版本改成由
 * compr_stream_config_dma1()/compr_stream_config_dma0() 直接完成，
 * 但符号仍然导出给 snd-soc-sprd-card 使用，因此保留空实现。
 */
int sprd_compr_configure_dsp(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_compr_configure_dsp);

/* ------------------------------------------------------------------------- */
/* DMA 通道配置                                                              */
/* ------------------------------------------------------------------------- */

/*
 * int sprd_compr_config_dma_channel(struct dma_chan *chan,
 *                                   struct sprd_dma_cfg *cfg,
 *                                   void *cb_data, u32 flags)
 *
 * 把已经算好的 sprd_dma_cfg 下发到 dmaengine 通道，并用
 * device_prep_slave_sg() 生成描述符。flags bit2 = 不注册回调
 * （stage0 的环形缓冲由 DSP 侧自己推进，不需要 AP 回调）。
 */
static int sprd_compr_config_dma_channel(struct dma_chan *chan,
					 struct sprd_dma_cfg *cfg,
					 void *cb_data, u32 flags)
{
	struct dma_async_tx_descriptor *tx_desc;
	struct dma_device *dma_dev;
	struct snd_compr_stream *cstream = cb_data;
	int ret;

	if (!chan || !cfg) {
		pr_err("[ASoC:COMPR] %s, DMA chan ID %d config is failed!\n",
		       __func__, cfg ? cfg->chan_id : -1);
		return -EINVAL;
	}

	dma_dev = chan->device;

	/* 1. 通道参数（方向/宽度/step/wrap/长度）下发到 sprd dma 控制器 */
	ret = dma_dev->device_config(chan, (struct dma_slave_config *)cfg);
	if (ret < 0) {
		pr_err("[ASoC:COMPR] %s, DMA chan ID %d config is failed!\n",
		       __func__, cfg->chan_id);
		return ret;
	}

	/*
	 * 2. 准备 sg 描述符。Unisoc 的 dmaengine 在 5.4 上给
	 *    device_prep_slave_sg() 多了一个私有参数（DSP 侧描述符），
	 *    这里按实际调用约定做函数指针转换后调用。
	 */
	tx_desc = ((struct dma_async_tx_descriptor *(*)(struct dma_chan *,
			struct scatterlist *, unsigned int,
			enum dma_transfer_direction, unsigned long, void *))
			dma_dev->device_prep_slave_sg)
		(chan, (struct scatterlist *)cfg->sg_virt, cfg->periods,
		 cfg->direction, (unsigned long)cfg->chan_flag, &cfg->ll_cfg);
	if (!tx_desc) {
		pr_err("[ASoC:COMPR] %s, DMA chan ID %d memcpy is failed!\n",
		       __func__, cfg->chan_id);
		pr_err("[ASoC:COMPR] device_prep_slave_sg error\n");
		return -EINVAL;
	}

	/* 3. 注册搬运完成回调（flags bit2 置位表示本通道不需要回调） */
	if (!(flags & 0x4)) {
		cfg->callback = sprd_compr_dma_buf_done;
		cfg->callback_param = cb_data;
		dev_dbg(cstream ? cstream->device : NULL,
			"[ASoC:COMPR] %s, Register Callback func for DMA chan ID %d\n",
			__func__, cfg->chan_id);
	}

	cfg->tx_desc = tx_desc;
	return 0;
}

/*
 * void sprd_compr_dma_buf_done(void *data)
 *
 * 第二级 DMA（stage1）搬运完成回调：唤醒 drain/pointer 等待者、
 * 拉长 wakelock 生命周期，并据此推进 srtd->buf_done_count。
 */
static void sprd_compr_dma_buf_done(void *data)
{
	struct snd_compr_stream *cstream = data;
	struct snd_compr_runtime *runtime;
	struct sprd_compr_stream *srtd;

	if (!cstream)
		return;

	runtime = cstream->runtime;
	srtd = runtime->private_data;

	/* state 5 表示这条流已经 drain 结束，回调属于迟到的旧描述符 */
	if (srtd->state == SPRD_COMPR_STATE_DRAIN_DONE) {
		pr_info("[ASoC:COMPR] buff done ignored,stream_state=%d\n", srtd->state);
		return;
	}

	if (!srtd->wake_lock_on) {
		__pm_stay_awake(srtd->ws);
		srtd->wake_lock_on = 1;
		pr_info("[ASoC:COMPR] buff done wake_lock\n");
	}

	srtd->received_stage0 = srtd->dma_params.period_size;
	srtd->buf_done_count++;
	s_buf_done_count = srtd->buf_done_count;

	/* 唤醒 pointer() 里的等待队列 */
	wake_up(&srtd->flush_wait);
	pr_info("[ASoC:COMPR] %s in s_buf_done_count0:%d ,s_buf_done_count:%d\n",
		__func__, 0, s_buf_done_count);
}

/* ------------------------------------------------------------------------- */
/* stage1: DDR(压缩码流) -> DSP 共享内存                                     */
/* ------------------------------------------------------------------------- */

/*
 * int compr_stream_config_dma1(struct snd_compr_stream *cstream,
 *                              u32 frag_cnt, u32 frag_size)
 *
 * stage1 使用 dma_request_slave_channel("compr_dma_stage1") 的 sgdma，
 * 目标地址是 audio_addr_ap2dsp() 转换后的 DSP 侧地址，源地址是 AP DDR 里
 * 的压缩数据 buffer（由 copy()/sbuf_copy_from_user() 填充）。
 */
static int compr_stream_config_dma1(struct snd_compr_stream *cstream,
				    u32 frag_cnt, u32 frag_size)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd = runtime->private_data;
	struct sprd_dma_cfg *cfg_ptr;
	struct scatterlist *sg;
	u32 dsp_addr;
	int ret, i;

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component) {
		pr_err("[ASoC:COMPR] %s, rtd hasn't been allocated yet\n", __func__);
		return -EINVAL;
	}
	data = dev_get_platdata(component->dev);

	dsp_addr = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1, srtd->stage1_handle, 0);
	dev_dbg(component->dev,
		"[ASoC:COMPR] %s,frag_size=%d,frag_cnt=%d\n",
		__func__, frag_size, frag_cnt);

	/* 1. 申请 stage1 的 dma 通道 */
	srtd->dma_chan_stage1 = dma_request_slave_channel(data->dev,
							  SPRD_COMPR_DMA_STAGE1);
	if (!srtd->dma_chan_stage1 || IS_ERR(srtd->dma_chan_stage1)) {
		pr_err("[ASoC:COMPR] ERR:PCM Request DMA Error %p\n",
		       srtd->dma_chan_stage1);
		return -EINVAL;
	}
	data->dma_chn_request = 1;

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] dma_chn_request=%p\n",
		       (void *)srtd->dma_chan_stage1);

	/* 2. cfg 块 + frag_cnt 个 sg entry 一起分配，sg 表紧跟结构体尾部 */
	cfg_ptr = kzalloc(sizeof(*cfg_ptr) + sizeof(struct scatterlist) * frag_cnt,
			  GFP_KERNEL);
	if (!cfg_ptr) {
		pr_err("[ASoC:COMPR] dma_config_ptr alloc mem error:\n");
		return -ENOMEM;
	}

	cfg_ptr->sg_virt = (void *)((char *)cfg_ptr + sizeof(*cfg_ptr));
	sg = (struct scatterlist *)cfg_ptr->sg_virt;
	sg_init_table(sg, frag_cnt);

	srtd->dma_cfg = cfg_ptr;
	srtd->dma_params.frag_size = frag_size;
	srtd->dma_params.fragments = frag_cnt;

	/*
	 * 3. 逐个 fragment 填 sg：源是 AP DDR 里的压缩码流 buffer，
	 *    目的是 audio_addr_ap2dsp() 转换后的 DSP 共享内存地址。
	 */
	for (i = 0; i < frag_cnt; i++) {
		sg_dma_address(&sg[i]) = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1,
							   srtd->stage1_handle, 0);
		sg_dma_len(&sg[i]) = frag_size;
	}

	cfg_ptr->periods = frag_cnt;
	cfg_ptr->frag_len = frag_size;
	cfg_ptr->src_frag_len = frag_size;
	cfg_ptr->chan_cnt = 1;
	cfg_ptr->datawidth = 4;
	cfg_ptr->direction = cstream->direction;
	cfg_ptr->chan_id = 1;

	/* 4. DSP 侧描述符（stage1 只有源地址有意义） */
	cfg_ptr->ll_cfg.phy_addr = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1,
						     srtd->stage1_handle, 0);
	cfg_ptr->ll_cfg.src = srtd->stage1_virt;
	cfg_ptr->ll_cfg.dst = srtd->stage1_map;
	cfg_ptr->ll_cfg.dma_chan = 1;
	cfg_ptr->ll_cfg.wrap = 0;
	cfg_ptr->ll_cfg.wrap_addr = (u64)audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1,
							   srtd->stage1_handle
							   + srtd->stage1_tot_size, 0);
	cfg_ptr->ll_cfg.total_len = srtd->stage1_tot_size;
	cfg_ptr->ll_cfg.block_len = frag_size;
	cfg_ptr->ll_cfg.tran_len = frag_cnt * frag_size;
	cfg_ptr->ll_cfg.periods = frag_cnt;

	dev_dbg(component->dev,
		"[ASoC:COMPR] srtd frag_len=%d, block_len=%d, tran_len=%d\n",
		cfg_ptr->frag_len, cfg_ptr->ll_cfg.block_len,
		cfg_ptr->ll_cfg.tran_len);
	cfg_ptr->ll_cfg_phy = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1,
						(u32)(uintptr_t)&cfg_ptr->ll_cfg, 0);

	dev_dbg(component->dev,
		"[ASoC:COMPR] dma_config_ptr->ll_cfg.phy_addr:%p\n",
		(void *)cfg_ptr->ll_cfg.phy_addr);

	/* 5. 下发 stage1 通道，需要完成回调（唤醒 pointer/drain） */
	ret = sprd_compr_config_dma_channel(srtd->dma_chan_stage1, cfg_ptr,
					    cstream, 0);
	if (ret) {
		pr_err("[ASoC:COMPR] sprd_compr_config_dma_channel error\n");
		goto err_free;
	}

	srtd->dma_tx_des[1] = cfg_ptr->tx_desc;
	srtd->dma_tx_des_valid |= BIT(1);

	dev_dbg(component->dev,
		"[ASoC:COMPR] totsize=%zu, period=%zu, srtd->dma_tx_des[0]=0x%p\n",
		(size_t)srtd->stage1_tot_size, (size_t)frag_size,
		srtd->dma_tx_des[0]);

	dev_dbg(component->dev, "[ASoC:COMPR] %s OK!\n", __func__);
	return 0;

err_free:
	kfree(cfg_ptr);
	srtd->dma_cfg = NULL;
	return ret;
}

/*
 * int compr_stream_hw_params1(struct snd_compr_stream *cstream,
 *                             u32 frag_cnt, u32 frag_size)
 *
 * stage1 的 hw_params 薄封装：只负责把调用失败转成日志 + 错误码。
 */
static int compr_stream_hw_params1(struct snd_compr_stream *cstream,
				   u32 frag_cnt, u32 frag_size)
{
	return compr_stream_config_dma1(cstream, frag_cnt, frag_size);
}

/* ------------------------------------------------------------------------- */
/* stage0: DSP IRAM 环形缓冲 <-> MCDT FIFO                                   */
/* ------------------------------------------------------------------------- */

/*
 * int compr_stream_config_dma0(struct snd_compr_stream *cstream, u32 frag_size)
 *
 * stage0 走 MCDT 自己的两路 DMA（采集 ADC / 播放 DAC），不使用 sg 链，
 * 而是把 wrap 地址设为 buffer 末尾，由硬件自动回卷：
 *
 *      +---------------------- stage0 buffer (audio_mem type 8) -------+
 *      |  frag0 | frag1 | ... | fragN |  wrap 区 (0x3f0)  | 保留 0x400 |
 *      +--------------------------------------------------------------+
 *                                     ^ stage0_end_virt (0xc8)
 */
static int compr_stream_config_dma0(struct snd_compr_stream *cstream, u32 frag_size)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd = runtime->private_data;
	struct sprd_dma_cfg *cfg_ptr;
	struct scatterlist *sg;
	int fifo_id;
	int ret;

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component)
		return -EINVAL;
	data = dev_get_platdata(component->dev);

	dev_dbg(component->dev, "[ASoC:COMPR] %s,frag_size=%d\n",
		__func__, frag_size);

	/* 1. 申请 stage0 通道 */
	srtd->dma_chan_stage0 = dma_request_slave_channel(data->dev,
							  SPRD_COMPR_DMA_STAGE0);
	if (!srtd->dma_chan_stage0 || IS_ERR(srtd->dma_chan_stage0)) {
		pr_err("[ASoC:COMPR] ERR:PCM Request DMA Error %p\n",
		       srtd->dma_chan_stage0);
		return -EINVAL;
	}

	/* 2. 配置块：stage0 用环形缓冲，逻辑上只需 1 个 sg entry */
	cfg_ptr = kzalloc(sizeof(*cfg_ptr) + sizeof(struct scatterlist),
			  GFP_KERNEL);
	if (!cfg_ptr) {
		pr_err("[ASoC:COMPR] cfg_ptr:kzalloc error\n");
		return -ENOMEM;
	}
	srtd->dma_cfg = cfg_ptr;
	cfg_ptr->sg_virt = (void *)((char *)cfg_ptr + sizeof(*cfg_ptr));
	sg = (struct scatterlist *)cfg_ptr->sg_virt;
	sg_init_table(sg, 1);

	/* 3. 打开对应的 MCDT 通路（播放 DAC / 采集 ADC），返回 FIFO 号 */
	if (cstream->direction == SND_COMPRESS_CAPTURE)
		fifo_id = mcdt_adc_dma_enable();
	else
		fifo_id = mcdt_dac_dma_enable();

	if (fifo_id < 0) {
		pr_err("[ASoC:COMPR] %s failed\n", "compr_dma_stage0");
		goto err_free;
	}

	cfg_ptr->fifo_phys = mcdt_dac_dma_phy_addr(cstream->direction);
	cfg_ptr->datawidth = 4;
	cfg_ptr->frag_len = frag_size;
	cfg_ptr->total_len = srtd->stage0_tot_size;
	cfg_ptr->block_len = fifo_id;
	cfg_ptr->periods = 1;
	cfg_ptr->direction = cstream->direction;
	cfg_ptr->chan_id = 0;

	/* 4. 环形缓冲首段：AP 虚拟地址 -> DSP 侧物理地址 */
	sg_dma_address(sg) = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE0,
					       srtd->stage0_handle, 0);
	sg_dma_len(sg) = frag_size;

	/* 5. DSP 侧描述符：wrap 到 buffer 末尾自动回卷 */
	cfg_ptr->ll_cfg.phy_addr = sg_dma_address(sg);
	cfg_ptr->ll_cfg.src = srtd->stage0_virt;
	cfg_ptr->ll_cfg.dst = (void *)(uintptr_t)cfg_ptr->fifo_phys;
	cfg_ptr->ll_cfg.dma_chan = 0;
	cfg_ptr->ll_cfg.wrap = SPRD_COMPR_WRAP_OFS;
	cfg_ptr->ll_cfg.wrap_addr = (u64)audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE0,
							   srtd->stage0_handle
							   + srtd->stage0_tot_size
							   - SPRD_COMPR_IRAM_RSV, 0);
	cfg_ptr->ll_cfg.total_len = srtd->stage0_tot_size;
	cfg_ptr->ll_cfg.block_len = frag_size;
	cfg_ptr->ll_cfg.tran_len = frag_size;
	cfg_ptr->ll_cfg.periods = cfg_ptr->periods;
	cfg_ptr->ll_cfg_phy = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE0,
						(u32)(uintptr_t)&cfg_ptr->ll_cfg, 0);

	dev_dbg(component->dev,
		"[ASoC:COMPR] cfg_ptr->ll_cfg.phy_addr:%p,src:%p,dst:%p,wrap_addr:%p,dma_chan:%d\n",
		(void *)cfg_ptr->ll_cfg.phy_addr, cfg_ptr->ll_cfg.src,
		cfg_ptr->ll_cfg.dst, (void *)cfg_ptr->ll_cfg.wrap_addr,
		cfg_ptr->ll_cfg.dma_chan);

	/* 6. 下发 stage0 通道；flags bit2 = 不注册完成回调（DSP 侧自行消费） */
	ret = sprd_compr_config_dma_channel(srtd->dma_chan_stage0, cfg_ptr,
					    cstream, 0x4);
	if (ret) {
		pr_err("[ASoC:COMPR] %s, DMA chan ID %d config is failed!\n",
		       __func__, cfg_ptr->chan_id);
		goto err_free;
	}

	srtd->dma_tx_des[0] = cfg_ptr->tx_desc;
	srtd->dma_tx_des_valid |= BIT(0);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] dma_chn_request=%p\n",
		       (void *)srtd->dma_chan_stage0);

	dev_dbg(component->dev,
		"[ASoC:COMPR] totsize=%zu, period=%zu, hw_req_id=%d, srtd->dma_tx_des[0]=0x%p,srtd->iram_paddr:%x,dst_addr:%p,src_addr:%p,dma_chn_request:%d\n",
		(size_t)srtd->stage0_tot_size, (size_t)frag_size, fifo_id,
		srtd->dma_tx_des[0], srtd->stage0_handle,
		cfg_ptr->ll_cfg.dst, cfg_ptr->ll_cfg.src,
		data->dma_chn_request);

	return 0;

err_free:
	kfree(cfg_ptr);
	srtd->dma_cfg = NULL;
	return -EINVAL;
}

/*
 * int compr_stream_hw_params0(struct snd_compr_stream *cstream, u32 frag_size)
 *
 * stage0 的 hw_params 薄封装。
 */
static int compr_stream_hw_params0(struct snd_compr_stream *cstream, u32 frag_size)
{
	int ret;

	ret = compr_stream_config_dma0(cstream, frag_size);
	if (ret)
		pr_info("[ASoC:COMPR] compr_stream_hw_params0 error %d\n", ret);

	return ret;
}

/* ------------------------------------------------------------------------- */
/* 平台驱动 / component 注册                                                 */
/* ------------------------------------------------------------------------- */

static u64 sprd_compr_dmamask = DMA_BIT_MASK(32);

/*
 * SIPC 通道的打开/关闭包装。原模块里这两个是 static inline，被内联后
 * __func__ 取到的是 compr_ch_open / compr_ch_close 而不是调用者的名字。
 */
static int compr_ch_open(u32 ch)
{
	int ret = aud_ipc_ch_open(ch, NULL);

	if (ret)
		pr_err("[ASoC:COMPR] %s: failed to open sipc channel, ret=%d\n",
		       __func__, ret);

	return ret;
}

static int compr_ch_close(u32 ch)
{
	int ret = aud_ipc_ch_close(ch);

	if (ret)
		pr_err("[ASoC:COMPR] %s: failed to close sipc channel, ret=%d\n",
		       __func__, ret);

	return ret;
}

/*
 * 填充压缩能力表（原模块里被内联到 open/get_caps 中，符号名字串仍在 .rodata）
 */
static void sprd_compr_populate_codec_list(struct snd_compr_caps *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->direction = SND_COMPRESS_PLAYBACK;
	caps->min_fragment_size = SPRD_COMPR_STAGE0_MIN_SIZE;
	caps->max_fragment_size = SPRD_COMPR_SIZE_LIMIT;
	caps->min_fragments = 2;
	caps->max_fragments = 64;
	caps->num_codecs = 2;
	caps->codecs[0] = SND_AUDIOCODEC_MP3;
	caps->codecs[1] = SND_AUDIOCODEC_AAC;
}

static int sprd_platform_compr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	compr_dbg();

	if (get_sp_audio_debug_flag())
		dev_dbg(dev,
			"[ASoC:COMPR] %s E,devname:%s, drivername:%s\n",
			__func__, dev_name(dev),
			dev->driver ? dev->driver->name : "none");

	ret = snd_soc_register_component(dev, &sprd_platform_compr_drv,
					 NULL, 0);
	if (ret) {
		dev_dbg(dev, "[ASoC:COMPR] registering soc platform failed\n");
		pr_err("[ASoC:COMPR] %s failed\n", __func__);
	}

	compr_dbg();

	return ret;
}

static int sprd_platform_compr_remove(struct platform_device *pdev)
{
	struct sprd_compr_data *data = pdev->dev.platform_data;

	compr_dbg();

	kfree(data);
	pdev->dev.platform_data = NULL;

	snd_soc_unregister_component(&pdev->dev);

	if (compr_ch_close(SPRD_COMPR_CH_STAGE0))
		pr_err("[ASoC:COMPR] %s, Failed to close offload channel\n",
		       "compr_ch_close");

	if (compr_ch_close(SPRD_COMPR_CH_DRAIN))
		pr_err("[ASoC:COMPR] %s, Failed to close offload drain channel\n",
		       "compr_ch_close");

	compr_dbg();

	return 0;
}

static int sprd_platform_compr_suspend(struct platform_device *pdev,
				       pm_message_t state)
{
	struct sprd_compr_data *data = pdev->dev.platform_data;

	compr_dbg();

	if (data)
		data->suspended = 1;

	compr_dbg();

	return 0;
}

static int sprd_platform_compr_resume(struct platform_device *pdev)
{
	struct sprd_compr_data *data = pdev->dev.platform_data;

	compr_dbg();

	if (!data)
		return 0;

	data->suspended = 0;
	wake_up(&data->point_wait);
	printk(KERN_INFO "[ASoC:COMPR] sprd compr platform wake_up\n");

	compr_dbg();

	return 0;
}

/*
 * component->driver->probe：snd_soc_register_component() 里被调用，
 * 这里建 /proc 节点、workqueue，并打开到 DSP 的 SIPC 通道。
 */
static int sprd_compr_probe(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;
	struct snd_info_entry *entry;
	struct sprd_compr_data *pdata;
	int ret;

	compr_dbg();

	/* 原模块在注册 proc 节点前留的调试标记（内联函数名） */
	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s\n",
		       "sprd_compress_proc_init");

	entry = snd_info_create_card_entry(card, SPRD_COMPR_CARD_NAME,
					   card->proc_root);
	if (entry) {
		entry->private_data = component;
		entry->c.text.read = sprd_compress_proc_read;
	}
	compr_dbg();

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	init_waitqueue_head(&pdata->point_wait);
	mutex_init(&pdata->dev_ctrl);
	pdata->dev = component->dev;
	pdata->card = card;
	pdata->proc_entry = entry;
	pdata->suspended = 0;

	pdata->compr_wq = alloc_workqueue("compr drain",
					 WQ_UNBOUND | WQ_HIGHPRI |
					 WQ_MEM_RECLAIM, 1);
	if (!pdata->compr_wq) {
		kfree(pdata);
		return -ENOMEM;
	}

	component->dev->platform_data = pdata;

	ret = compr_ch_open(SPRD_COMPR_CH_STAGE0);
	if (ret) {
		pr_err("[ASoC:COMPR] %s, Failed to open offload channel\n",
		       "compr_ch_open");
	} else if (compr_ch_open(SPRD_COMPR_CH_DRAIN)) {
		compr_ch_close(SPRD_COMPR_CH_STAGE0);
		pr_err("[ASoC:COMPR] %s, Failed to open offload drain channel\n",
		       "compr_ch_open");
	}

	compr_dbg();

	return 0;
}

/* pcm_new 回调：本平台的 DMA 只能访问 32bit 地址空间 */
static int sprd_compress_new(struct snd_soc_pcm_runtime *rtd)
{
	struct device *dev = rtd->card->dev;

	if (!dev->dma_mask) {
		dev->dma_mask = &sprd_compr_dmamask;
		if (!dev->coherent_dma_mask)
			dev->coherent_dma_mask = DMA_BIT_MASK(32);
	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* compress 流生命周期                                                        */
/* ------------------------------------------------------------------------- */

static int sprd_platform_compr_open(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd;
	int ret;

	compr_dbg();

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component)
		return -EINVAL;

	data = component->dev->platform_data;
	if (!data)
		return -EINVAL;

	dev_dbg(component->dev,
		"[ASoC:COMPR] %s E,pdata=0x%p, platform name=%s\n",
		__func__, data, dev_name(component->dev));

	if (!component->dev->dma_mask) {
		component->dev->dma_mask = &sprd_compr_dmamask;
		if (!component->dev->coherent_dma_mask)
			component->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	}

	srtd = kzalloc(sizeof(*srtd), GFP_KERNEL);
	if (!srtd) {
		pr_err("[ASoC:COMPR] %s failed\n", __func__);
		return -ENOMEM;
	}

	/* 原模块在 open 时注册的调试节点名 */
	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s\n", "compr write");

	srtd->cstream = cstream;
	srtd->direction = cstream->direction;
	srtd->stream_id = cstream->direction;
	dev_dbg(component->dev,
		"[ASoC:COMPR] %s ,srtd=0x%p,cstream->direction=%d\n",
		__func__, srtd, cstream->direction);
	srtd->state = SPRD_COMPR_STATE_OPEN;
	srtd->running = 0;
	srtd->started = 0;
	srtd->drain_ready = 0;

	runtime->private_data = srtd;

	/* 能力表：DSP 侧只接受 MP3 / AAC 两种 offload 码流 */
	sprd_compr_populate_codec_list(&srtd->caps);

	srtd->ws = wakeup_source_create(dev_name(component->dev));
	if (srtd->ws)
		wakeup_source_add(srtd->ws);

	init_waitqueue_head(&srtd->drain_wait);
	init_waitqueue_head(&srtd->eos_wait);
	init_waitqueue_head(&srtd->flush_wait);
	INIT_WORK(&srtd->drain_work, sprd_compr_drain_work);

	data->active[cstream->direction == SND_COMPRESS_PLAYBACK ? 0 : 1] = srtd;

	mutex_lock(&data->dev_ctrl);

	if (get_sp_audio_debug_flag())
		printk(KERN_INFO "[ASoC:COMPR] %s: before send open cmd\n",
		       __func__);

	ret = agdsp_access_enable(SPRD_COMPR_CH_STAGE0);
	if (ret) {
		pr_err("[ASoC:COMPR] %s:agdsp_access_enable:error:%d\n",
		       __func__, ret);
		goto err_free;
	}
	data->agdsp_on = 1;

	ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_OPEN);
	if (ret) {
		pr_err("[ASoC:COMPR] %s, Failed to open offload channel\n",
		       __func__);
		agdsp_access_disable(SPRD_COMPR_CH_STAGE0);
		data->agdsp_on = 0;
		goto err_free;
	}

	mutex_unlock(&data->dev_ctrl);

	compr_dbg();

	return 0;

err_free:
	mutex_unlock(&data->dev_ctrl);
	data->active[cstream->direction == SND_COMPRESS_PLAYBACK ? 0 : 1] = NULL;
	runtime->private_data = NULL;
	if (srtd->ws)
		wakeup_source_remove(srtd->ws);
	kfree(srtd);

	return ret;
}

static int sprd_platform_compr_free(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd = runtime->private_data;

	compr_dbg();

	if (!srtd) {
		pr_err("[ASoC:COMPR] srtd is NULL!\n");
		return -EINVAL;
	}

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component)
		return -EINVAL;

	data = component->dev->platform_data;
	if (!data)
		return -EINVAL;

	dev_dbg(component->dev,
		"[ASoC:COMPR] %s ,srtd=0x%p,cstream->direction=%d\n",
		__func__, srtd, cstream->direction);

	/* 先让 DSP 侧停掉，再回收两级 DMA */
	mutex_lock(&data->dev_ctrl);

	if (srtd->state != SPRD_COMPR_STATE_IDLE &&
	    srtd->state != SPRD_COMPR_STATE_OPEN) {
		int ret;

		/* 原模块此处的内联函数名 */
		if (get_sp_audio_debug_flag())
			printk(KERN_DEBUG "[ASoC:COMPR] %s\n",
			       "compr_stream_hw_free");

		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_CLOSE);
		printk(KERN_INFO "[ASoC:COMPR] send COMPR_CMD_CLOSE ret %d\n",
		       ret);
	}

	if (data->agdsp_on) {
		agdsp_access_disable(SPRD_COMPR_CH_STAGE0);
		data->agdsp_on = 0;
	}

	mutex_unlock(&data->dev_ctrl);

	flush_workqueue(data->compr_wq);

	if (srtd->dma_chan_stage1) {
		dma_release_channel(srtd->dma_chan_stage1);
		srtd->dma_chan_stage1 = NULL;
	}
	if (srtd->dma_chan_stage0) {
		dma_release_channel(srtd->dma_chan_stage0);
		srtd->dma_chan_stage0 = NULL;
	}

	if (srtd->wake_lock_on) {
		__pm_relax(srtd->ws);
		srtd->wake_lock_on = 0;
		printk(KERN_INFO "[ASoC:COMPR] wake_lock released\n");
	}

	if (srtd->stage1_handle) {
		audio_mem_free(SPRD_COMPR_MEM_STAGE1, srtd->stage1_handle,
			       srtd->stage1_tot_size);
		srtd->stage1_handle = 0;
	}
	if (srtd->stage1_map) {
		audio_mem_unmap(srtd->stage1_map);
		srtd->stage1_map = NULL;
	}
	if (srtd->stage0_virt) {
		audio_mem_unmap(srtd->stage0_virt);
		srtd->stage0_virt = NULL;
	}
	if (srtd->stage0_handle) {
		audio_mem_free(SPRD_COMPR_MEM_STAGE0, srtd->stage0_handle,
			       srtd->stage0_tot_size);
		srtd->stage0_handle = 0;
	}

	kfree(srtd->dma_cfg);
	srtd->dma_cfg = NULL;

	if (srtd->ws) {
		wakeup_source_remove(srtd->ws);
		wakeup_source_destroy(srtd->ws);
		srtd->ws = NULL;
	}

	data->active[cstream->direction == SND_COMPRESS_PLAYBACK ? 0 : 1] = NULL;
	runtime->private_data = NULL;
	kfree(srtd);

	compr_dbg();

	return 0;
}

/*
 * sprd_compr_config_sprd_rtd()：把 compress stream 到 snd_soc_pcm_runtime 的
 * 关联关系确认下来（dma / trigger 阶段依赖 cpu_dai 与 component）。
 * 原模块中该函数被内联进 set_params，故二进制里只留下函数名字符串。
 */
static int sprd_compr_config_sprd_rtd(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_soc_dai *cpu_dai;

	if (!rtd)
		return -EINVAL;

	cpu_dai = rtd->cpu_dai;
	if (!cpu_dai || !cpu_dai->component)
		return -EINVAL;

	return 0;
}

/*
 * 参数下发：把 HAL 给的 fragment_size / fragments / codec 信息翻译成
 * DSP 的 IRAM 环形缓冲布局 + 两级 DMA 配置。
 */
static int sprd_platform_compr_set_params(struct snd_compr_stream *cstream,
					  struct snd_compr_params *params)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd = runtime->private_data;
	u32 frag_size, frag_cnt, tot_size, size;
	int ret;

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component) {
		pr_err("[ASoC:COMPR] %s, rtd hasn't been allocated yet\n",
		       __func__);
		return -EINVAL;
	}

	data = component->dev->platform_data;
	if (!data)
		return -EINVAL;

	frag_size = params->buffer.fragment_size;
	frag_cnt = params->buffer.fragments;

	dev_dbg(component->dev,
		"[ASoC:COMPR] %s: channels: %d, sample_rate=%d\n",
		__func__, params->codec.ch_in, params->codec.sample_rate);

	dev_dbg(component->dev,
		"[ASoC:COMPR] %s: sample_rate %d, code.id=%d\n",
		__func__, params->codec.sample_rate, params->codec.id);

	if (!frag_size || !frag_cnt) {
		pr_err("[ASoC:COMPR] %s failed\n", __func__);
		return -EINVAL;
	}

	/* 整份参数留档，update_params() 从这里取 */
	memcpy(&srtd->compr_params, params, sizeof(*params));

	srtd->dma_params.frag_size = frag_size;
	srtd->dma_params.fragments = frag_cnt;

	switch (params->codec.id) {
	case SND_AUDIOCODEC_MP3_ID:
		srtd->codec_type = SPRD_COMPR_CODEC_MP3;
		break;
	case SND_AUDIOCODEC_AAC_ID:
		srtd->codec_type = SPRD_COMPR_CODEC_AAC;
		break;
	default:
		pr_err("[ASoC:COMPR] %s: Unsupported audio codec %d\n",
		       __func__, params->codec.id);
		return -EINVAL;
	}

	srtd->sample_rate = snd_pcm_rate_bit_to_rate(params->codec.rate_control);
	if (!srtd->sample_rate)
		srtd->sample_rate = params->codec.sample_rate;

	/* ---- stage0：IRAM 环形缓冲，DSP 按 fragment 头部推进 ---- */
	tot_size = frag_cnt * frag_size +
		   frag_size * SPRD_COMPR_HDR_PER_FRAG + SPRD_COMPR_HDR_BASE;

	size = tot_size;
	srtd->stage0_handle = audio_mem_alloc(SPRD_COMPR_MEM_STAGE0, &size,
					      tot_size);
	if (!srtd->stage0_handle) {
		pr_err("[ASoC:COMPR] %s failed\n", __func__);
		return -ENOMEM;
	}
	srtd->stage0_tot_size = size;

	srtd->stage0_virt = audio_mem_vmap(srtd->stage0_handle, 1);
	if (!srtd->stage0_virt) {
		ret = -ENOMEM;
		goto err_free_stage0;
	}
	memset_io(srtd->stage0_virt, 0, srtd->stage0_tot_size);

	/* 尾部 0x400 留给 DSP 放 wrap 信息 */
	srtd->stage0_end_virt = srtd->stage0_virt +
				srtd->stage0_tot_size - SPRD_COMPR_IRAM_RSV;

	/* stage0 在 DSP 侧的地址 */
	srtd->stage0_dsp_addr = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE1,
						  (u32)(uintptr_t)srtd->stage0_end_virt,
						  0);

	/* ---- stage1：DDR32 共享内存，DSP 把解码后的 PCM 写这里 ---- */
	tot_size = frag_cnt * frag_size;
	size = tot_size;
	srtd->stage1_handle = audio_mem_alloc(SPRD_COMPR_MEM_STAGE1, &size,
					      tot_size);
	if (!srtd->stage1_handle) {
		pr_err("[ASoC:COMPR] %s failed\n", __func__);
		ret = -ENOMEM;
		goto err_unmap_stage0;
	}
	srtd->stage1_tot_size = size;
	srtd->received_stage0 = frag_size;

	srtd->stage1_map = audio_mem_vmap(srtd->stage1_handle, 1);
	if (!srtd->stage1_map) {
		ret = -ENOMEM;
		goto err_free_stage1;
	}
	memset_io(srtd->stage1_map, 0, srtd->stage1_tot_size);

	srtd->stage1_dsp_addr = audio_addr_ap2dsp(SPRD_COMPR_ADDR_STAGE0,
						  srtd->stage1_handle, 0);

	/* ---- 把 rtd 的信息配置给 DSP 侧 ---- */
	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s\n",
		       "sprd_compr_config_sprd_rtd");

	ret = sprd_compr_config_sprd_rtd(cstream);
	if (ret) {
		pr_err("[ASoC:COMPR] %s, error!!! ret=%d\n", __func__, ret);
		pr_err("[ASoC:COMPR] sprd_compr_config_sprd_rtd error %d\n", ret);
		goto err_unmap_stage1;
	}

	/* ---- 两级 DMA：先 stage1（DSP→DDR），再 stage0（AP→IRAM） ---- */
	ret = compr_stream_config_dma1(cstream, frag_cnt, frag_size);
	if (ret) {
		printk(KERN_DEBUG "[ASoC:COMPR] compr_stream_hw_params1 error %d\n",
		       ret);
		goto err_unmap_stage1;
	}

	ret = compr_stream_config_dma0(cstream, frag_size);
	if (ret) {
		printk(KERN_DEBUG "[ASoC:COMPR] compr_stream_hw_params0 error %d\n",
		       ret);
		goto err_unmap_stage1;
	}

	/* 参数同步给 DSP */
	ret = sprd_platform_compr_update_params(cstream);
	if (ret)
		goto err_unmap_stage1;

	srtd->state = SPRD_COMPR_STATE_CONFIG;

	dev_dbg(component->dev, "[ASoC:COMPR] %s OK!\n", __func__);
	dev_dbg(component->dev,
		"[ASoC:COMPR] srtd buffer.fragments=%d, fragment_size=%d\n",
		frag_cnt, frag_size);

	compr_dbg();

	return 0;

err_unmap_stage1:
	audio_mem_unmap(srtd->stage1_map);
	srtd->stage1_map = NULL;
err_free_stage1:
	audio_mem_free(SPRD_COMPR_MEM_STAGE1, srtd->stage1_handle,
		       srtd->stage1_tot_size);
	srtd->stage1_handle = 0;
err_unmap_stage0:
	audio_mem_unmap(srtd->stage0_virt);
	srtd->stage0_virt = NULL;
err_free_stage0:
	audio_mem_free(SPRD_COMPR_MEM_STAGE0, srtd->stage0_handle,
		       srtd->stage0_tot_size);
	srtd->stage0_handle = 0;

	return ret;
}

/* 元数据（编码器延迟/填充、码率、声道）由 HAL 下到 DSP 参数结构里 */
static int sprd_platform_compr_set_metadata(struct snd_compr_stream *cstream,
					    struct snd_compr_metadata *metadata)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *srtd = runtime->private_data;
	u32 value = metadata->value[0];

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	switch (metadata->key) {
	case SPRD_COMPR_META_BIT_RATE:
		srtd->meta_bit_rate = value;
		srtd->bit_rate = value;
		printk(KERN_DEBUG "[ASoC:COMPR] set_metadata,bitrate=%d\n",
		       srtd->bit_rate);
		break;
	case SPRD_COMPR_META_SAMPLE_RATE:
		srtd->meta_sample_rate = value;
		printk(KERN_DEBUG "[ASoC:COMPR] set_metadata,samplerate=%d\n",
		       srtd->meta_sample_rate);
		break;
	case SPRD_COMPR_META_CHANNELS:
		srtd->meta_channels = value;
		printk(KERN_DEBUG "[ASoC:COMPR] set_metadata,channel=%d\n",
		       srtd->meta_channels);
		break;
	case SPRD_COMPR_META_CODEC_TYPE:
		srtd->meta_codec_type = value;
		break;
	case SPRD_COMPR_META_ENCODER_DELAY:
		srtd->encoder_delay = value;
		printk(KERN_DEBUG "[ASoC:COMPR] %s, got encoder delay %u\n",
		       __func__, srtd->encoder_delay);
		break;
	case SPRD_COMPR_META_ENCODER_PADDING:
		srtd->encoder_padding = value;
		printk(KERN_DEBUG "[ASoC:COMPR] %s, got encoder padding %u\n",
		       __func__, srtd->encoder_padding);
		break;
	default:
		pr_err("[ASoC:COMPR] %s: Unsupported metadata key %d\n",
		       __func__, metadata->key);
		return -EINVAL;
	}

	return 0;
}

/*
 * 播放位置：HAL 用 copied_total 做 A/V 同步，必须等 DSP 真正写进去。
 * 等待队列挂在 data->point_wait 上，由 drain_work / resume 唤醒。
 */
static int sprd_platform_compr_pointer(struct snd_compr_stream *cstream,
				       struct snd_compr_tstamp *tstamp)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *srtd = runtime->private_data;
	u32 count;

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	/* FIFO 里 DSP 上报的写指针 */
	count = srtd->received_total - srtd->copied_total;

	tstamp->byte_offset = srtd->app_pointer;
	tstamp->copied_total = srtd->copied_total;
	tstamp->pcm_frames = srtd->copied_total / 4;
	tstamp->pcm_io_frames = srtd->received_total / 4;
	tstamp->sampling_rate = srtd->sample_rate;

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: count = %d,app_pointer=%d,buff_size=%d,received_total=%d\n",
		       __func__, count, srtd->app_pointer, runtime->buffer_size,
		       srtd->received_total);

	printk(KERN_DEBUG "[ASoC:COMPR] %s: copied_total: %d bit_rate: %d sample_rate: %d  pcm_io_frames: %d\n",
	       __func__, tstamp->copied_total, srtd->bit_rate,
	       tstamp->sampling_rate, tstamp->pcm_io_frames);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s, wait_event\n", __func__);

	wait_event_interruptible_timeout(srtd->flush_wait, srtd->drain_ready,
					 msecs_to_jiffies(20));

	(void)rtd;

	return 0;
}

/*
 * 从用户空间拷码流到 IRAM 环形缓冲，两级 DMA 的 stage0 每写满一个
 * fragment，DSP 就会把它搬到解码器。
 */
static int sprd_platform_compr_copy(struct snd_compr_stream *cstream,
				    char __user *buf, size_t count)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *srtd = runtime->private_data;
	u32 write_pos, avail, copy_size, ret = 0;

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	/* 有数据要写就先拿住 wakelock，free/tirgger 时再放 */
	if (!srtd->wake_lock_on && srtd->ws) {
		__pm_stay_awake(srtd->ws);
		srtd->wake_lock_on = 1;
	}

	avail = runtime->buffer_size - (srtd->app_pointer - srtd->copied_total);
	if (count > avail) {
		printk(KERN_DEBUG "[ASoC:COMPR] %s: no space,avail=%d,count=%d\n",
		       __func__, avail, (u32)count);
		count = avail;
	}
	if (!count)
		return 0;

	copy_size = srtd->app_pointer % runtime->buffer_size;
	write_pos = srtd->iram_write_pos;

	if (copy_size + count <= runtime->buffer_size) {
		ret = sbuf_copy_from_user(srtd->stage0_virt + write_pos, buf, count);
	} else {
		u32 first = runtime->buffer_size - copy_size;

		ret = sbuf_copy_from_user(srtd->stage0_virt + write_pos, buf, first);
		ret += sbuf_copy_from_user(srtd->stage0_virt, buf + first,
					   count - first);
	}

	if (ret) {
		pr_err("[ASoC:COMPR] %s copy err %u\n", __func__, ret);
		return -EFAULT;
	}

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: count = %d,app_pointer=%d,buff_size=%d,received_total=%d\n",
		       __func__, (u32)count, srtd->app_pointer,
		       runtime->buffer_size, srtd->received_total);

	srtd->app_pointer += count;
	srtd->avail_total += count;

	printk(KERN_DEBUG "[ASoC:COMPR] %s: count=%zu,stage0=%u,stage1=%u,total=%u,avail_total=%u,app_pointer=%u\n",
	       __func__, count, srtd->received_stage0, srtd->received_total,
	       srtd->dma_params.buf_size, runtime->buffer_size,
	       srtd->app_pointer);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: iram_write_pos = %d, before\n",
		       __func__, srtd->iram_write_pos);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: iram_write_pos = %d,copy_size=%d\n",
		       __func__, srtd->iram_write_pos, (u32)count);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s,before,app_pointer=%d,copied_total=%d\n",
		       __func__, srtd->app_pointer, srtd->copied_total);

	printk(KERN_DEBUG "[ASoC:COMPR] %s, DMA copied totol=%d, avail_total=%d, buf_done_count=%d\n",
	       __func__, srtd->copied_total, srtd->avail_total,
	       srtd->buf_done_count);

	/* 唤醒可能在 pointer() 里等待的线程 */
	wake_up_interruptible(&srtd->drain_wait);

	return count;
}

static int sprd_platform_compr_ack(struct snd_compr_stream *cstream,
				   size_t bytes)
{
	compr_dbg();

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s E, info=0x%p\n",
		       __func__, cstream);

	return 0;
}

static int sprd_platform_compr_get_caps(struct snd_compr_stream *cstream,
					struct snd_compr_caps *caps)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *srtd = runtime->private_data;

	compr_dbg();

	if (!srtd) {
		pr_err("[ASoC:COMPR] srtd is NULL!\n");
		return -EINVAL;
	}

	memcpy(caps, &srtd->caps, sizeof(*caps));

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s, OK\n", __func__);

	compr_dbg();

	return 0;
}

/* DSP 侧只支持 MP3 / AAC 两种 offload 码流，其余一律拒掉 */
static int sprd_platform_compr_get_codec_caps(struct snd_compr_stream *cstream,
					      struct snd_compr_codec_caps *codec)
{
	compr_dbg();

	switch (codec->codec) {
	case SND_AUDIOCODEC_MP3_ID:
		codec->num_descriptors = 1;
		codec->descriptor[0].max_channel = 2;
		codec->descriptor[0].min_bit_rate = 32;
		codec->descriptor[0].max_bit_rate = 320;
		break;
	case SND_AUDIOCODEC_AAC_ID:
		codec->num_descriptors = 2;
		codec->descriptor[0].max_channel = 2;
		codec->descriptor[0].min_bit_rate = 8;
		codec->descriptor[0].max_bit_rate = 512;
		break;
	default:
		pr_err("[ASoC:COMPR] %s: Unsupported audio codec %d\n",
		       __func__, codec->codec);
		return -EINVAL;
	}

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: sample_rate %d, code.id=%d\n",
		       __func__, cstream->runtime->buffer_size, codec->codec);

	compr_dbg();

	return 0;
}

/*
 * 带 __check_object_size() 的用户空间拷贝，返回没能拷进去的字节数
 */
static int sbuf_copy_from_user(void *dst, const void __user *src, u32 len)
{
	int ret;

	might_fault();
	__check_object_size(dst, len, false);
	ret = __arch_copy_from_user(dst, src, len);
	return ret;
}

/* 下发给 DSP 的参数块（sipc 短消息 payload） */
struct sprd_compr_ipc_msg {
	u32 cmd;
	u32 direction;
	u32 stream_id;
	u32 codec_type;
	u32 sample_rate;
	u32 bit_rate;
	u32 channels;
	u32 stage0_dsp_addr;
	u32 stage1_dsp_addr;
	u32 frag_size;
	u32 frag_cnt;
	u32 encoder_delay;
	u32 encoder_padding;
};

static int sprd_platform_send_cmd(struct sprd_compr_stream *srtd, u32 cmd)
{
	struct sprd_compr_data *data = srtd->pdata_ref;
	struct sprd_compr_ipc_msg msg;
	int ret;

	compr_dbg();

	if (!data)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = cmd;
	msg.direction = srtd->direction;
	msg.stream_id = srtd->stream_id;
	msg.codec_type = srtd->codec_type;
	msg.sample_rate = srtd->sample_rate;
	msg.bit_rate = srtd->bit_rate;
	msg.channels = srtd->meta_channels;
	msg.stage0_dsp_addr = srtd->stage0_dsp_addr;
	msg.stage1_dsp_addr = srtd->stage1_dsp_addr;
	msg.frag_size = srtd->dma_params.frag_size;
	msg.frag_cnt = srtd->dma_params.fragments;
	msg.encoder_delay = srtd->encoder_delay;
	msg.encoder_padding = srtd->encoder_padding;

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s\n", "compr_send_cmd");

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s:cmd=%d,stream_id=%d\n",
		       __func__, cmd, srtd->stream_id);

	mutex_lock(&data->dev_ctrl);
	ret = aud_smsg_send(SPRD_COMPR_CH_STAGE0, cmd, &msg, sizeof(msg));
	mutex_unlock(&data->dev_ctrl);

	if (ret) {
		pr_err("[ASoC:COMPR] %s, Failed to send cmd(0x%x), ret(%d)\n",
		       __func__, cmd, ret);
		pr_err("[ASoC:COMPR] %s: failed to send command(%d), ret=%d\n",
		       __func__, cmd, ret);
		return ret;
	}

	return 0;
}

static int sprd_platform_send_param(struct sprd_compr_stream *srtd,
				    u32 direction, void *param)
{
	struct sprd_compr_data *data = srtd->pdata_ref;
	int ret;

	compr_dbg();

	if (!data || !param)
		return -EINVAL;

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s:cmd=%d,stream_id=%d,buff=0x%p,buff_size=%d\n",
		       __func__, direction, srtd->stream_id, param,
		       (int)sizeof(struct sprd_compr_ipc_msg));

	mutex_lock(&data->dev_ctrl);
	ret = aud_smsg_send(SPRD_COMPR_CH_DRAIN, SPRD_COMPR_CMD_OPEN,
			    param, sizeof(struct sprd_compr_ipc_msg));
	mutex_unlock(&data->dev_ctrl);

	if (ret) {
		pr_err("[ASoC:COMPR] %s, Failed to send cmd(0x%x), ret(%d)\n",
		       __func__, SPRD_COMPR_CMD_OPEN, ret);
		pr_err("[ASoC:COMPR] %s: failed to send command(%d), ret=%d\n",
		       __func__, SPRD_COMPR_CMD_OPEN, ret);
	}

	return ret;
}

/* 把 HAL 参数结构打包成 DSP 的 sprd_compr_ipc_msg 并下发 */
static int sprd_platform_compr_update_params(struct snd_compr_stream *cstream)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *srtd = runtime->private_data;
	struct sprd_compr_ipc_msg msg;

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = SPRD_COMPR_CMD_OPEN;
	msg.direction = srtd->direction;
	msg.stream_id = srtd->stream_id;
	msg.codec_type = srtd->codec_type;
	msg.sample_rate = srtd->sample_rate;
	msg.bit_rate = srtd->bit_rate ? srtd->bit_rate :
		       srtd->compr_params.codec.bit_rate;
	msg.channels = srtd->meta_channels ? srtd->meta_channels : 2;
	msg.stage0_dsp_addr = srtd->stage0_dsp_addr;
	msg.stage1_dsp_addr = srtd->stage1_dsp_addr;
	msg.frag_size = srtd->dma_params.frag_size;
	msg.frag_cnt = srtd->dma_params.fragments;
	msg.encoder_delay = srtd->encoder_delay;
	msg.encoder_padding = srtd->encoder_padding;

	return sprd_platform_send_param(srtd, srtd->direction, &msg);
}

/*
 * 两级 DMA 的重新提交：drain 结束后 DSP 要求把描述符重新挂回队列
 */
static int compr_stream_trigger(struct sprd_compr_stream *srtd)
{
	struct sprd_compr_data *data = srtd->pdata_ref;
	struct dma_async_tx_descriptor *tx;
	int ret, i;

	compr_dbg();

	if (!data || !srtd->cstream) {
		printk(KERN_DEBUG "[ASoC:COMPR] no trigger\n");
		return -EINVAL;
	}

	printk(KERN_DEBUG "[ASoC:COMPR] E\n");
	printk(KERN_DEBUG "[ASoC:COMPR] %s:stop out\n", __func__);

	/* 先把上一轮残留的描述符从队列上摘下来 */
	for (i = 0; i < SPRD_COMPR_RTD_COUNT; i++) {
		if (!(srtd->dma_tx_des_valid & BIT(i)))
			continue;
		if (!srtd->dma_tx_des[i])
			continue;
		dmaengine_terminate_all(i ? srtd->dma_chan_stage1 :
					srtd->dma_chan_stage0);
	}

	ret = compr_stream_config_dma1(srtd->cstream,
				       srtd->dma_params.fragments,
				       srtd->dma_params.frag_size);
	if (ret) {
		printk(KERN_DEBUG "[ASoC:COMPR] %s, hw_param_err, ret=%d\n",
		       __func__, ret);
		return ret;
	}

	ret = compr_stream_config_dma0(srtd->cstream, srtd->dma_params.frag_size);
	if (ret) {
		printk(KERN_DEBUG "[ASoC:COMPR] %s, hw_param_err,ret=%d\n",
		       __func__, ret);
		return ret;
	}

	/* 把两级描述符重新挂回 dmaengine 队列 */
	for (i = 0; i < SPRD_COMPR_RTD_COUNT; i++) {
		if (!(srtd->dma_tx_des_valid & BIT(i)))
			continue;
		tx = srtd->dma_tx_des[i];
		if (!tx)
			continue;
		ret = dmaengine_submit(tx);
		if (ret < 0) {
			pr_err("[ASoC:COMPR] dmaengine_submit error:%d,i:%d\n",
			       ret, i);
			return ret;
		}
	}

	printk(KERN_DEBUG "[ASoC:COMPR] %s:start out\n", __func__);
	printk(KERN_DEBUG "[ASoC:COMPR] COMPR out\n");

	return 0;
}

static int sprd_platform_compr_trigger(struct snd_compr_stream *cstream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_component *component;
	struct sprd_compr_data *data;
	struct sprd_compr_stream *srtd = runtime->private_data;
	unsigned long flags;
	int ret = 0;

	compr_dbg();

	if (!srtd)
		return -EINVAL;

	component = snd_soc_rtdcom_lookup(rtd, SPRD_COMPR_COMP_NAME);
	if (!component)
		return -EINVAL;

	data = component->dev->platform_data;
	if (!data)
		return -EINVAL;

	srtd->pdata_ref = data;
	srtd->trigger_cmd = cmd;

	dev_dbg(component->dev, "[ASoC:COMPR] %s: cmd %d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_dbg(component->dev,
			"[ASoC:COMPR] %s: SNDRV_PCM_TRIGGER_START\n", __func__);

		if (srtd->state == SPRD_COMPR_STATE_DRAIN_DONE) {
			/* next_track / partial drain 后的重新开流 */
			dev_dbg(component->dev,
				"[ASoC:COMPR] %s: next_track=%d,receive_total[%d]=%d\n",
				__func__, srtd->next_track, 0,
				srtd->received_total);

			srtd->state = SPRD_COMPR_STATE_CONFIG;
			srtd->next_track = 1;
			ret = compr_stream_trigger(srtd);
			if (ret)
				break;
		}

		if (srtd->state != SPRD_COMPR_STATE_CONFIG) {
			pr_err("[ASoC:COMPR] %s: wrong state %d\n",
			       __func__, srtd->state);
			ret = -EINVAL;
			break;
		}

		/* DSP 把 stage1 当 PCM 输出缓冲，开流前清干净 */
		if (srtd->stage1_map)
			memset_io(srtd->stage1_map, 0, srtd->stage1_tot_size);

		spin_lock_irqsave(&srtd->lock, flags);
		srtd->running = 1;
		srtd->started = 1;
		spin_unlock_irqrestore(&srtd->lock, flags);

		mcdt_da_fifo_clr();

		srtd->pending_cmd = SPRD_COMPR_CMD_START;
		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_START);
		if (ret) {
			pr_err("[ASoC:COMPR] %s failed\n", __func__);
			break;
		}

		if (srtd->pending_cmd) {
			pr_err("[ASoC:COMPR] %s: no responsed cmd=%d\n",
			       __func__, srtd->pending_cmd);
			ret = -ETIMEDOUT;
			break;
		}

		srtd->state = SPRD_COMPR_STATE_START;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(component->dev,
			"[ASoC:COMPR] %s: SNDRV_PCM_TRIGGER_STOP\n", __func__);

		spin_lock_irqsave(&srtd->lock, flags);
		srtd->running = 0;
		srtd->started = 0;
		spin_unlock_irqrestore(&srtd->lock, flags);

		mcdt_da_fifo_clr();
		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_STOP);
		if (ret) {
			pr_err("[ASoC:COMPR] %s failed\n", __func__);
			break;
		}

		srtd->state = SPRD_COMPR_STATE_CONFIG;
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s: SNDRV_PCM_TRIGGER_PAUSE_PUSH\n",
		       __func__);

		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_STOP);
		if (ret) {
			pr_err("[ASoC:COMPR] %s failed\n", __func__);
			break;
		}

		srtd->running = 0;
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s: SNDRV_PCM_TRIGGER_PAUSE_RELEASE\n",
		       __func__);

		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_START);
		if (ret) {
			pr_err("[ASoC:COMPR] %s failed\n", __func__);
			break;
		}

		srtd->running = 1;
		break;

	case SNDRV_COMPRESS_DRAIN:
	{
		u32 padding_cnt = srtd->encoder_padding;

		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s: SNDRV_COMPRESS_DRAIN, cmd=%d, total=%d\n",
		       __func__, cmd, srtd->avail_total);

		if (srtd->app_pointer < srtd->avail_total)
			printk(KERN_DEBUG
			       "[ASoC:COMPR] partial drain,app_pointer=%d, received_stage0=%d\n",
			       srtd->app_pointer, srtd->received_stage0);

		if (padding_cnt)
			printk(KERN_DEBUG
			       "[ASoC:COMPR] partial drain,padding_cnt=%d,app_pointer=%d, received_stage0=%d\n",
			       padding_cnt, srtd->app_pointer,
			       srtd->received_stage0);

		srtd->state = SPRD_COMPR_STATE_DRAIN;
		srtd->drain_ready = 0;
		wake_up_interruptible(&srtd->drain_wait);

		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_DRAIN);
		if (ret) {
			pr_err("[ASoC:COMPR] drain out err!\n");
			break;
		}

		printk(KERN_DEBUG "[ASoC:COMPR] %s: out of drain\n", __func__);
		printk(KERN_DEBUG "[ASoC:COMPR] wake up on drain\n");
		break;
	}

	case SND_COMPR_TRIGGER_NEXT_TRACK:
		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s: SND_COMPR_TRIGGER_NEXT_TRACK\n",
		       __func__);

		srtd->next_track = 1;
		srtd->state = SPRD_COMPR_STATE_DRAIN_DONE;
		ret = sprd_platform_send_cmd(srtd, SPRD_COMPR_CMD_NEXT_TRACK);
		if (ret) {
			pr_err("[ASoC:COMPR] %s failed\n", __func__);
			break;
		}
		break;

	default:
		pr_err("[ASoC:COMPR] %s: Unsupported stream type\n", __func__);
		ret = -EINVAL;
		break;
	}

	compr_dbg();

	return ret;
}

/*
 * DSP 把最后一个 fragment 解完后会回一条 sipc 消息，这里做收尾：
 * 唤醒 drain 等待者、释放 wakelock、把状态推到 DRAIN_DONE。
 */
static void sprd_compr_drain_work(struct work_struct *work)
{
	struct sprd_compr_stream *srtd =
		container_of(work, struct sprd_compr_stream, drain_work);
	struct sprd_compr_data *data = srtd->pdata_ref;
	unsigned long flags;
	u16 msg_cmd = 0;
	int ret;

	compr_dbg();

	if (!data)
		return;

	printk(KERN_DEBUG
	       "[ASoC:COMPR] %s,before,app_pointer=%d,copied_total=%d\n",
	       __func__, srtd->app_pointer, srtd->copied_total);

	ret = aud_smsg_recv(SPRD_COMPR_CH_DRAIN, &msg_cmd, NULL, 0);
	if (ret < 0) {
		pr_err("[ASoC:COMPR] drain out err!\n");
		return;
	}

	printk(KERN_DEBUG
	       "[ASoC:COMPR] %s,after,app_pointer=%d,copied_total=%d,avail_total=%d\n",
	       __func__, srtd->app_pointer, srtd->copied_total,
	       srtd->avail_total);

	spin_lock_irqsave(&srtd->lock, flags);
	srtd->drain_ready = 1;
	srtd->pending_cmd = 0;
	spin_unlock_irqrestore(&srtd->lock, flags);

	if (srtd->stage1_map)
		memset_io(srtd->stage1_map, 0, srtd->stage1_tot_size);

	srtd->state = SPRD_COMPR_STATE_DRAIN_DONE;
	s_buf_done_count++;

	wake_up_interruptible(&srtd->drain_wait);
	wake_up(&data->point_wait);

	if (srtd->wake_lock_on) {
		__pm_relax(srtd->ws);
		srtd->wake_lock_on = 0;
		if (get_sp_audio_debug_flag())
			printk(KERN_DEBUG "[ASoC:COMPR] drain done, wake_lock released\n");
	}

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG
		       "[ASoC:COMPR] %s: waited out, drain_ready=%d,stream_state=%d,ret=%d\n",
		       __func__, srtd->drain_ready, srtd->state, ret);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: drain finished, ret=%d\n",
		       __func__, ret);

	if (get_sp_audio_debug_flag())
		printk(KERN_DEBUG "[ASoC:COMPR] %s: drain done, cmd=0x%x\n",
		       __func__, msg_cmd);

	compr_dbg();
}

/* /proc/asound/cardX/sprd-compress：只有累计缓冲完成次数的统计 */
static void sprd_compress_proc_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%d\n", s_buf_done_count);
}

/* ------------------------------------------------------------------------- */
/* 模块描述表                                                                */
/* ------------------------------------------------------------------------- */

static const struct of_device_id sprd_compress_of_match[] = {
	{ .compatible = "unisoc,sharkl5-compress-platform" },
	{ .compatible = "unisoc,roc1-compress-platform" },
	{ .compatible = "unisoc,qogirl6-compress-platform" },
	{ .compatible = "unisoc,qogirn6pro-compress-platform" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_compress_of_match);

static const struct snd_compr_ops sprd_platform_compr_ops = {
	.open = sprd_platform_compr_open,
	.free = sprd_platform_compr_free,
	.set_params = sprd_platform_compr_set_params,
	.set_metadata = sprd_platform_compr_set_metadata,
	.trigger = sprd_platform_compr_trigger,
	.pointer = sprd_platform_compr_pointer,
	.copy = sprd_platform_compr_copy,
	.ack = sprd_platform_compr_ack,
	.get_caps = sprd_platform_compr_get_caps,
	.get_codec_caps = sprd_platform_compr_get_codec_caps,
};

static const struct snd_soc_component_driver sprd_platform_compr_drv = {
	.name = SPRD_COMPR_COMP_NAME,
	.probe = sprd_compr_probe,
	.pcm_new = sprd_compress_new,
	.compr_ops = &sprd_platform_compr_ops,
};

static struct platform_driver sprd_platform_compr_driver = {
	.probe = sprd_platform_compr_probe,
	.remove = sprd_platform_compr_remove,
	.suspend = sprd_platform_compr_suspend,
	.resume = sprd_platform_compr_resume,
	.driver = {
		.name = SPRD_COMPR_PLATFORM_NAME,
		.of_match_table = sprd_compress_of_match,
	},
};
module_platform_driver(sprd_platform_compr_driver);

MODULE_DESCRIPTION("ASoC Spreadtrum Compress Platform driver");
MODULE_AUTHOR("Yintang Ren<yintang.ren@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:compress-platform");
MODULE_ALIAS("platform:sprd_compr_2stage_dma");

