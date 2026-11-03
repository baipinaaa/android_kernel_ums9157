// SPDX-License-Identifier: GPL-2.0
/*
 * audio_dsp_dump.c - Unisoc audio DSP log / memory dump character device
 *
 * Reconstructed from the vendor binary module `audio-dsp-dump.ko`
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50, .text 0x3f7c = 16252 bytes,
 *  module name `audio_dsp_dump`).  The module also contains audio_sblock.c,
 * which is rebuilt in the same directory; the two halves are exported through
 * one .ko, therefore the sblock helpers are plain local calls here.
 *
 * Build path embedded in .rodata:
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/
 * audio_driver/sprd_audio/audiopipe/audio_dsp_log.c
 *
 * There is one platform device per log channel, distinguished by the
 * compatible string.  Each one creates a character device whose name is the
 * "name" field selected through of_device_id::data:
 *
 *   unisoc,audio_dsp_log        -> "audio_dsp_log"        type 0  mode 0
 *   unisoc,audio_dsp_pcm        -> "audio_dsp_pcm"        type 1  mode 1
 *   unisoc,audio_dsp_mem        -> "audio_dsp_mem"        type 2  mode 2
 *   unisoc,audio_dsp_call_info  -> "audio_dsp_call_info"  type 3  mode 3
 *
 * mode == 0 ... the device drains the DSP log ring through audio_sblock.
 * mode == 1 ... same, used for the pcm path.
 * mode == 2 ... the device maps a "mem" area shared with the DSP; read()
 *              returns that area linearly (ppos based) and the DSP writes the
 *              log into it.  The area is also handed to audio_smsg through
 *              aud_smsg_register_dump_func() so the DSP can ask for a dump.
 * mode == 3 ... call_info buffers, resizable through ioctl.
 *
 * ---------------------------------------------------------------------------
 * Function layout of the binary (.text, addresses are section relative)
 *   0x0324 audio_sblock_send_ex      0x1bdc audio_sblock_put
 *   0x05c0 audio_sblock_release      0x1d4c audio_sblock_register_notifier
 *   0x0780 audio_sblock_receive      0x1de8 audio_sblock_send_prepare
 *   0x0ba8 audio_sblock_poll_wait    0x1e08 audio_sblock_send_finish
 *   0x0cdc audio_sblock_ch_open      0x1ee0 audio_sblock_get_arrived_count
 *   0x0e08 audio_sblock_create       0x1f98 audio_sblock_get_free_count
 *   0x1438 audio_sblock_thread       0x2050 audio_dsp_probe       (2120)
 *   0x16a0 audio_sblock_get          0x2898 audio_dsp_remove      ( 228)
 *   0x1ab8 audio_sblock_destroy      0x297c audio_dsp_audio_info_dump (924)
 *                                    0x2d18 audio_dsp_read        (1720)
 *                                    0x33d0 audio_dsp_write       ( 568)
 *                                    0x3608 audio_dsp_poll        ( 216)
 *                                    0x36e0 audio_dsp_ioctl       (1200)
 *                                    0x3b90 audio_dsp_open        ( 224)
 *                                    0x3c70 audio_dsp_release     (  56)
 *   .init.text 0x0058 init_module, .exit.text 0x0030 cleanup_module
 *
 * ---------------------------------------------------------------------------
 * Binary layout of struct audio_dsp_priv (kzalloc(0xf0))
 *   0x00 char name[0x40];          "audio_dsp_log", ...
 *   0x40 char type[0x40];          "log" / "pcm" / "memory" / "call_info"
 *   0x80 u32 usemem_mode;          0 log, 1 pcm, 2 mem, 3 call_info, 4 other
 *   0x84 u32 usemem_type;          sprd-usemem-type
 *   0x88 u32 usemem_bytes;         sprd-usemem-bytes (in/out of audio_mem_alloc)
 *   0x8c u32 usemem_handle;        audio_mem_alloc() result
 *   0x90 void *usemem_virt;        audio_mem_vmap() result
 *   0x98 u32 usemem_dspaddr;       audio_addr_ap2dsp() result
 *   0xa0 void *dump1_virt;  0xa8 u32 dump1_addr;  0xac u32 dump1_size;
 *   0xb0 void *dump2_virt;  0xb8 u32 dump2_addr;  0xbc u32 dump2_size;
 *   0xc0 void *dump3_virt;  0xc8 u32 dump3_addr;  0xcc u32 dump3_size;
 *   0xd0 u8 dst;            0xd1 u8 channel;
 *   0xd4 u32 tx_blk_num;    0xd8 u32 tx_blk_size;
 *   0xdc u32 rx_blk_num;    0xe0 u32 rx_blk_size;
 *   0xe4 u32 dump_total;    0xe8 u32 dump_on;
 *   total 0xf0
 *
 * Binary layout of struct dsp_log_dev (kzalloc(0xe8))
 *   0x00 struct audio_dsp_priv *priv;
 *   0x08 u32 major;  0x0c u32 minor;
 *   0x10 struct cdev cdev;         cdev_init(&dsp_log->cdev, &dsp_log_tp_fops)
 *   0x98 wait_queue_head_t mem_ready_wait;
 *   0xb0 struct mutex mutex;
 *   0xd0 u32 state;                written by ioctl 6, read by poll()
 *   0xd4 u32 mem_addr;  0xd8 u32 mem_size;  0xe0 void *mem_virt;
 *   total 0xe8
 *
 * The four fields at 0xd0 are only ever written by the ioctl handlers, they
 * mirror the memory helpers of audio_dsp_priv but live in the dsp_log object.
 * Platform data (the dsp_log pointer) is stored with platform_set_drvdata().
 *
 * Binary layout of struct audio_dsp_file (kmalloc(0x68), file->private_data)
 *   0x00 u8 dst;  0x02 u16 channel;
 *   0x10 u32 mode;         copy of priv->usemem_mode at open time
 *   0x14 u32 dsp_addr;     copy of priv->usemem_dspaddr
 *   0x18 void *buf;        copy of priv->usemem_virt
 *   0x20 u32 size;         copy of priv->usemem_bytes
 *   0x24 struct audio_log_packet pkt;   20 byte header built by read()
 *   0x38 u32 log_mode;     ioctl 2, enables the packet header
 *   0x3c u32 seq;          incremented for every packet
 *   0x40 wait_queue_head_t *waitq;       &dsp_log->mem_ready_wait
 *   0x48 struct dsp_log_dev *dsp_log;
 *   0x50 struct audio_sblock_rec rec;    { u64 addr; u32 len; }
 *   0x60 u32 offset;       consumed bytes of the current sblock
 *   total 0x68
 *
 * ---------------------------------------------------------------------------
 * ioctl command set, type 'X' = 0x58, one u32 argument:
 *   _IOW(0x58, 0)  dsp log enable      aud_send_cmd_no_wait(ch, 0x24, arg)
 *   _IOW(0x58, 1)  dsp log path set    only traced
 *   _IOW(0x58, 2)  packet header        ctx->log_mode = arg
 *   _IOW(0x58, 3)  dsp pcm path set    only traced
 *   _IOW(0x58, 4)  dsp pcm enable      aud_send_cmd_no_wait(ch, 0x20, arg)
 *   _IOW(0x58, 5)  -                   returns -EINVAL
 *   _IOW(0x58, 6)  dsp assert set      dsp_log->state = arg, wakes poll()
 *   _IOW(0x58, 7)  dump total          priv->dump_total = arg
 *   _IOW(0x58, 8)  -                   returns -EINVAL
 *   _IOW(0x58, 9)  log to memory       audio_mem_alloc(5) + aud_send_cmd(0x25)
 *   _IOW(0x58, 10) call_info enable    aud_send_cmd_no_wait(ch, 0x26, ...)
 *   _IOW(0x58, 11) call_info resize    audio_mem_alloc(usemem_type) + 0x26
 * ---------------------------------------------------------------------------
 */

#define pr_fmt(fmt) "[Audio:DSPDUMP] " fmt

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define AUD_DSP_NAME_LEN	0x40
#define AUD_DSP_LOG_MEM_TYPE	5	/* audio_mem type used for the dump area */
#define AUD_DSP_LOG_MEM_MAX	0x500000
#define AUD_DSP_IPC_DUMP_SIZE	0x10000
#define AUD_DSP_MAX_PRINT	0x100

/* message ids used by this driver ----------------------------------------- */
#define SMSG_CMD_DSPLOG_ENABLE		0x24
#define SMSG_CMD_DSPLOG_TO_MEMORY	0x25
#define SMSG_CMD_CALL_INFO		0x26
#define SMSG_CMD_DSP_PCM_ENABLE		0x20

struct audio_dsp_priv {
	char name[AUD_DSP_NAME_LEN];
	char type[AUD_DSP_NAME_LEN];
	u32 usemem_mode;
	u32 usemem_type;
	u32 usemem_bytes;
	u32 usemem_handle;
	void *usemem_virt;
	u32 usemem_dspaddr;
	u32 reserved0;
	void *dump1_virt;
	u32 dump1_addr;
	u32 dump1_size;
	void *dump2_virt;
	u32 dump2_addr;
	u32 dump2_size;
	void *dump3_virt;
	u32 dump3_addr;
	u32 dump3_size;
	u8 dst;
	u8 channel;
	u16 reserved1;
	u32 tx_blk_num;
	u32 tx_blk_size;
	u32 rx_blk_num;
	u32 rx_blk_size;
	u32 dump_total;
	u32 dump_on;
};

struct dsp_log_dev {
	struct audio_dsp_priv *priv;
	u32 major;
	u32 minor;
	struct cdev cdev;
	u8 pad0[0x10];			/* 0x88 -> 0x98 in the vendor object */
	wait_queue_head_t mem_ready_wait;
	struct mutex mutex;
	u32 state;
	u32 mem_addr;
	u32 mem_size;
	u32 reserved0;
	void *mem_virt;
};

/* 20 byte packet header prepended by read() when log_mode is enabled ------- */
struct audio_log_packet {
	u32 magic;			/* 0x7e7e7e7e */
	u16 length;			/* payload + 0x10 */
	u16 reserved;			/* 0 */
	u16 marker;			/* 0x5a5a */
	u16 checksum;
	u32 seq;
	u16 payload_len;		/* payload + 8 */
	u8 type;			/* 0x9d */
	u8 flag;			/* 0x40 | (mode < 4) */
};

struct audio_sblock_rec {
	u64 addr;
	u32 len;
};

struct audio_dsp_file {
	u8 dst;
	u16 channel;
	u8 pad0[0x0d];
	u32 mode;
	u32 dsp_addr;
	void *buf;
	u32 size;
	struct audio_log_packet pkt;
	u32 log_mode;
	u32 seq;
	wait_queue_head_t *waitq;
	struct dsp_log_dev *dsp_log;
	struct audio_sblock_rec rec;
	u32 offset;
};

/* provided by audio_sipc.ko ----------------------------------------------- */
extern int aud_ipc_ch_open(u32 channel);
extern int aud_ipc_ch_close(u32 channel);
extern int aud_ipc_dump(void *buf, u32 size);
extern int aud_send_cmd_no_wait(u16 channel, u16 command, u32 parameter0,
				u32 parameter1, u32 parameter2, u32 parameter3);
extern int aud_send_cmd_no_param(u8 channel, u32 command, u32 p0, u32 p1,
				 u32 p2, u32 p3, int timeout);
extern int aud_smsg_register_dump_func(void (*func)(void *arg, int unused),
				       void *arg);

/* provided by audio_mem.c -------------------------------------------------- */
extern u32 audio_mem_alloc(u32 mem_type, u32 *size);
extern void audio_mem_free(u32 mem_type, u32 addr, u32 size);
extern void *audio_mem_vmap(phys_addr_t paddr, size_t size, u32 flag);
extern void audio_mem_unmap(void *vaddr);
extern u32 audio_addr_ap2dsp(u32 mem_type, u32 addr, u32 dir);

/* provided by agdsp_access.c ---------------------------------------------- */
extern int agdsp_can_access(void);

/* provided by audio_sblock.c (same module) --------------------------------- */
extern int audio_sblock_init(u8 dst, u8 channel, u32 tx_blk_num,
			     u32 tx_blk_size, u32 rx_blk_num, u32 rx_blk_size,
			     u32 mem_type);
extern int audio_sblock_destroy(u8 dst, u8 channel);
extern int audio_sblock_send(u8 dst, u8 channel, struct audio_sblock_rec *rec);
extern int audio_sblock_get(u8 dst, u8 channel, struct audio_sblock_rec *rec,
			    int timeout);
extern int audio_sblock_receive(u8 dst, u8 channel,
				struct audio_sblock_rec *rec, int timeout);
extern int audio_sblock_release(u8 dst, u8 channel,
				struct audio_sblock_rec *rec);
extern unsigned int audio_sblock_poll_wait(u8 dst, u8 channel,
					   struct file *file, poll_table *wait);

static struct class *audio_class;
static struct dsp_log_dev *the_dsp_log;

static int audio_dsp_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos);
static ssize_t audio_dsp_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos);
static unsigned int audio_dsp_poll(struct file *file, poll_table *wait);
static long audio_dsp_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg);
static int audio_dsp_open(struct inode *inode, struct file *file);
static int audio_dsp_release(struct inode *inode, struct file *file);

static const struct file_operations dsp_log_tp_fops = {
	.owner		= THIS_MODULE,
	.read		= audio_dsp_read,
	.write		= audio_dsp_write,
	.poll		= audio_dsp_poll,
	.unlocked_ioctl	= audio_dsp_ioctl,
	.compat_ioctl	= audio_dsp_ioctl,
	.open		= audio_dsp_open,
	.release	= audio_dsp_release,
};

static const struct of_device_id dsp_of_match[] = {
	{ .compatible = "unisoc,audio_dsp_log",		.data = (void *)0 },
	{ .compatible = "unisoc,audio_dsp_pcm",		.data = (void *)1 },
	{ .compatible = "unisoc,audio_dsp_mem",		.data = (void *)2 },
	{ .compatible = "unisoc,audio_dsp_call_info",	.data = (void *)3 },
	{ },
};

/*
 * The DSP may ask for a dump at any time, in that case the shared "mem" area
 * of the device is filled with the log ring plus the three dspdumpmem
 * regions.  The first argument is the object registered below, the second one
 * tells whether the caller wants the log ring as well.
 */
static int audio_dsp_audio_info_dump(void *arg, int unused)
{
	struct dsp_log_dev *dsp_log = arg;
	struct audio_dsp_priv *priv;
	u32 *buf;
	char *dst;
	int last, end, max, first, i, off;
	int ret;

	if (!dsp_log)
		return -1;

	priv = dsp_log->priv;
	if (!priv)
		return -1;

	pr_info("%s: mem_log_addr : %x, mem_log_size:%d",
		"audio_dsp_audio_info_dump", dsp_log->mem_addr, dsp_log->mem_size);

	buf = dsp_log->mem_virt;
	if (buf) {
		end = (dsp_log->mem_size >> 2) - 3;
		last = buf[2];			/* write index kept by the DSP */
		max = (end < AUD_DSP_MAX_PRINT) ? end : AUD_DSP_MAX_PRINT;
		if (last > (u32)((dsp_log->mem_size >> 2) - 4)) {
			pr_err("last_log_pos error");
			last = (dsp_log->mem_size >> 2) - 4;
		}

		pr_info("last_log_pos:%d,total:%d, max :%d\n",
			last, end, max);

		first = ((u32)(last + 1) > (u32)max) ? (max + 1) : last;
		i = max - first;
		for (off = 0; off < i; off++)
			pr_info("dsplog:%d:  %x\n", off,
				buf[(dsp_log->mem_size >> 2) - 4 - i + off + 4]);
		for (off = 0; off < first; off++)
			pr_info("dsplog:%d:  %x\n", i + off, buf[last + off + 4]);
	}

	ret = aud_ipc_dump(priv->usemem_virt, AUD_DSP_IPC_DUMP_SIZE);
	pr_info("%s: ipc dump bytes : %d", "audio_dsp_audio_info_dump", ret);

	pr_info("%s: dump1_p:%x,size:%d, 2:%x, %d,3:%x,%d\n",
		"audio_dsp_mem_dump", priv->dump1_addr, priv->dump1_size,
		priv->dump2_addr, priv->dump2_size,
		priv->dump3_addr, priv->dump3_size);

	dst = (char *)priv->usemem_virt + AUD_DSP_IPC_DUMP_SIZE;
	if (!dsp_log->dump_total) {
		off = 0;
		goto out;
	}

	if (unused && !dsp_log->dump_on) {
		off = 0;
		goto after_dump1;
	}

	if (priv->dump1_virt && priv->dump1_size && agdsp_can_access())
		memcpy(dst, priv->dump1_virt, priv->dump1_size);
	else if (priv->dump1_size)
		memset(dst, 0, priv->dump1_size);
	else {
		off = 0;
		goto after_dump1;
	}
	off = priv->dump1_size;

after_dump1:
	if (priv->dump2_virt && priv->dump2_size) {
		if (agdsp_can_access())
			memcpy(dst + off, priv->dump2_virt, priv->dump2_size);
		else
			memset(dst + off, 0, priv->dump2_size);
		off += priv->dump2_size;
	}

	if (priv->dump3_size) {
		if (agdsp_can_access() && priv->dump3_virt)
			memcpy(dst + off, priv->dump3_virt, priv->dump3_size);
		else
			memset(dst + off, 0, priv->dump3_size);
	}
	off += priv->dump3_size;

out:
	pr_info("%s: dsp mem dump bytes : %d", "audio_dsp_audio_info_dump", off);
	return off;
}

static int audio_dsp_parse_mem_dt(struct device_node *nd,
				  struct audio_dsp_priv *priv)
{
	u32 value[6];
	int ret;

	priv->usemem_mode = 2;
	ret = of_property_read_u32_array(nd, "sprd-usemem-bytes",
					 &priv->usemem_bytes, 1, NULL);
	if (ret < 0) {
		pr_info("%s no sprd-usemem-type\n", "audio_dsp_parse_mem_dt");
		return ret;
	}
	pr_info("sprd-usemem-bytes %d\n", priv->usemem_bytes);

	memset(value, 0, sizeof(value));
	ret = of_property_read_u32_array(nd, "sprd,dspdumpmem", value, 6, NULL);
	if (ret < 0)
		return 0;

	priv->dump1_addr = value[0];
	priv->dump1_size = value[1];
	if (priv->dump1_size)
		priv->dump1_virt = audio_mem_vmap(priv->dump1_addr,
						  priv->dump1_size, 1);
	priv->dump2_addr = value[2];
	priv->dump2_size = value[3];
	if (priv->dump2_size)
		priv->dump2_virt = audio_mem_vmap(priv->dump2_addr,
						  priv->dump2_size, 1);
	priv->dump3_addr = value[4];
	priv->dump3_size = value[5];
	if (priv->dump3_size)
		priv->dump3_virt = audio_mem_vmap(priv->dump3_addr,
						  priv->dump3_size, 1);

	pr_info("audio_dsp_log.c: dsp_mem_dump: dump1_p:%x,size:%d, 2:%x, %d,3:%x,%d\n",
		priv->dump1_addr, priv->dump1_size,
		priv->dump2_addr, priv->dump2_size,
		priv->dump3_addr, priv->dump3_size);

	return 0;
}

static int audio_dsp_parse_dt(struct device_node *nd,
			      struct audio_dsp_priv *priv)
{
	const struct of_device_id *match;
	int ret;

	match = of_match_node(dsp_of_match, nd);
	if (!match) {
		pr_err("%s Get device id failed deffer!\n",
		       "audio_dsp_parse_dt");
		return -EPROBE_DEFER;
	}

	if ((long)match->data > 3) {
		pr_err("%s not supported type %ld\n", "audio_dsp_parse_dt",
		       (long)match->data);
		return -EINVAL;
	}

	switch ((long)match->data) {
	case 0:
		strcpy(priv->name, "audio_dsp_log");
		strcpy(priv->type, "log");
		break;
	case 1:
		strcpy(priv->name, "audio_dsp_pcm");
		strcpy(priv->type, "pcm");
		break;
	case 2:
		strcpy(priv->name, "audio_dsp_mem");
		strcpy(priv->type, "memory");
		break;
	default:
		strcpy(priv->name, "audio_dsp_call_info");
		strcpy(priv->type, "call_info");
		break;
	}

	ret = of_property_read_u32_array(nd, "sprd-usemem-type",
					 &priv->usemem_type, 1, NULL);
	if (ret < 0) {
		pr_info("%s no sprd-usemem-type\n", "audio_dsp_parse_dt");
		goto fail;
	}

	ret = of_property_read_u8_array(nd, "sprd-dst", &priv->dst, 1, NULL);
	if (ret < 0) {
		pr_info("%s no sprd-dst\n", "audio_dsp_parse_dt");
		goto fail;
	}

	ret = of_property_read_u8_array(nd, "sprd-channel", &priv->channel, 1,
					NULL);
	if (ret < 0) {
		pr_info("%s no sprd-channel\n", "audio_dsp_parse_dt");
		goto fail;
	}

	if (!strcmp(priv->type, "memory")) {
		ret = audio_dsp_parse_mem_dt(nd, priv);
		if (ret < 0)
			goto fail;
	} else if (!strcmp(priv->type, "log")) {
		priv->usemem_mode = 0;
	} else if (!strcmp(priv->type, "pcm")) {
		priv->usemem_mode = 1;
	} else if (!strcmp(priv->type, "call_info")) {
		priv->usemem_mode = 3;
		ret = of_property_read_u32_array(nd, "sprd-usemem-bytes",
						 &priv->usemem_bytes, 1, NULL);
		if (ret < 0) {
			pr_info("%s, no sprd-usemem-bytes\n",
				"audio_dsp_parse_dt");
			goto fail;
		}
	} else {
		priv->usemem_mode = 4;
	}

	ret = of_property_read_u32_array(nd, "sprd-txblocknum",
					 &priv->tx_blk_num, 1, NULL);
	if (ret < 0)
		pr_info("%s, no sprd-txblocknum\n", "audio_dsp_parse_dt");

	ret = of_property_read_u32_array(nd, "sprd-txblocksize",
					 &priv->tx_blk_size, 1, NULL);
	if (ret < 0)
		pr_info("%s, no sprd-txblocknum\n", "audio_dsp_parse_dt");

	ret = of_property_read_u32_array(nd, "sprd-rxblocknum",
					 &priv->rx_blk_num, 1, NULL);
	if (ret < 0)
		goto fail;

	ret = of_property_read_u32_array(nd, "sprd-rxblocksize",
					 &priv->rx_blk_size, 1, NULL);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	pr_err("%s failed", "audio_dsp_parse_dt");
	return ret;
}

static int audio_dsp_probe(struct platform_device *pdev)
{
	struct audio_dsp_priv *priv;
	struct dsp_log_dev *dsp_log;
	struct device_node *nd = pdev->dev.of_node;
	dev_t dev;
	int ret;

	pr_info("%s in\n", "audio_dsp_probe");

	if (!nd) {
		pr_err("%s defer\n", "audio_dsp_probe");
		return -EPROBE_DEFER;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (nd) {
		ret = audio_dsp_parse_dt(nd, priv);
		if (ret)
			goto parse_fail;
	}

	pr_info("%s:name=%s, dst=%hhu, channel=%hhu\n", "audio_dsp_probe",
		priv->name, priv->dst, priv->channel);
	pr_info("txblocknum=%u,txblocksize=%u rxblocknum=%u,rxblocksize=%u\n",
		priv->tx_blk_num, priv->tx_blk_size,
		priv->rx_blk_num, priv->rx_blk_size);

	if (priv->tx_blk_num || priv->rx_blk_num) {
		pr_info("begin to creat sblock\n");
		ret = audio_sblock_init(priv->dst, priv->channel,
					priv->tx_blk_num, priv->tx_blk_size,
					priv->rx_blk_num, priv->rx_blk_size,
					priv->usemem_type);
		if (ret) {
			pr_err("Failed to create sblock: %d\n", ret);
			kfree(priv);
			return ret;
		}
	} else if (priv->channel) {
		ret = aud_ipc_ch_open(priv->channel);
		if (ret) {
			pr_err("Failed to open channel : %d, ret:%d\n",
			       priv->channel, ret);
			kfree(priv);
			return ret;
		}
		pr_info("channel %d open ok\n", priv->channel);
	}

	if ((priv->usemem_mode & ~1) == 2) {
		priv->usemem_handle = audio_mem_alloc(priv->usemem_type,
						      &priv->usemem_bytes);
		pr_info("aud_dsp_addr: mem: mem type %d, mem_addr %x\n",
			priv->usemem_type, priv->usemem_handle);
		if (!priv->usemem_handle || !priv->usemem_bytes) {
			kfree(priv);
			pr_err("Failed to create sblock with addr: %x\n",
			       priv->usemem_handle);
			return -ENOMEM;
		}

		pr_info("aud_dsp_addr: mem: dsp space mem_addr %x\n",
			priv->usemem_handle);
		if (priv->usemem_bytes) {
			priv->usemem_virt = audio_mem_vmap(priv->usemem_handle,
							   priv->usemem_bytes, 1);
			memset_io(priv->usemem_virt, 0, priv->usemem_bytes);
		}
		priv->usemem_dspaddr = audio_addr_ap2dsp(priv->usemem_type,
							 priv->usemem_handle, 0);
		if (priv->usemem_mode == 2)
			aud_smsg_register_dump_func((void *)audio_dsp_audio_info_dump,
						    priv);
	}

	dsp_log = kzalloc(sizeof(*dsp_log), GFP_KERNEL);
	if (!dsp_log) {
		audio_sblock_destroy(priv->dst, priv->channel);
		kfree(priv);
		pr_err("Failed to allocate audio_pipe_device\n");
		return -ENOMEM;
	}

	dsp_log->priv = priv;
	mutex_init(&dsp_log->mutex);
	init_waitqueue_head(&dsp_log->mem_ready_wait);

	ret = alloc_chrdev_region(&dev, 0, 1, priv->name);
	if (ret) {
		kfree(dsp_log);
		audio_sblock_destroy(priv->dst, priv->channel);
		kfree(priv);
		pr_err("Failed to alloc dsp_log chrdev\n");
		return ret;
	}

	cdev_init(&dsp_log->cdev, &dsp_log_tp_fops);
	ret = cdev_add(&dsp_log->cdev, dev, 1);
	if (ret) {
		kfree(dsp_log);
		unregister_chrdev_region(dev, 1);
		audio_sblock_destroy(priv->dst, priv->channel);
		kfree(priv);
		pr_err("Failed to add audio_pipe cdev\n");
		return ret;
	}

	dsp_log->major = MAJOR(dev);
	dsp_log->minor = MINOR(dev);
	device_create(audio_class, NULL, dev, NULL, "%s", priv->name);
	platform_set_drvdata(pdev, dsp_log);

	if (priv->usemem_mode == 2)
		aud_smsg_register_dump_func((void *)audio_dsp_audio_info_dump,
					    dsp_log);

	pr_info("dsp_log_tp_probe success  %s\n", priv->name);
	return 0;

parse_fail:
	pr_err("%s failed", "audio_dsp_parse_dt");
	kfree(priv);
	pr_err("Failed to parse dsp_log device tree, ret=%d\n", ret);
	return ret;
}

static int audio_dsp_remove(struct platform_device *pdev)
{
	struct dsp_log_dev *dsp_log = platform_get_drvdata(pdev);
	struct audio_dsp_priv *priv;
	dev_t dev;
	int ret;

	if (!dsp_log)
		return -EINVAL;

	priv = dsp_log->priv;
	if (priv && priv->channel) {
		ret = aud_ipc_ch_close(priv->channel);
		if (ret)
			pr_err("Failed to close channel : %d, ret:%d\n",
			       priv->channel, ret);
	}

	if (dsp_log->mem_addr) {
		audio_mem_free(AUD_DSP_LOG_MEM_TYPE, dsp_log->mem_addr,
			       dsp_log->mem_size);
		dsp_log->mem_addr = 0;
		audio_mem_unmap(dsp_log->mem_virt);
		dsp_log->mem_virt = NULL;
	}

	cdev_del(&dsp_log->cdev);
	dev = MKDEV(dsp_log->major, dsp_log->minor);
	unregister_chrdev_region(dev, 1);

	priv = dsp_log->priv;
	if (priv) {
		audio_sblock_destroy(priv->dst, priv->channel);
		kfree(priv);
		dsp_log->priv = NULL;
	}

	kfree(dsp_log);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int audio_dsp_open(struct inode *inode, struct file *file)
{
	struct dsp_log_dev *dsp_log;
	struct audio_dsp_file *ctx;

	dsp_log = container_of(inode->i_cdev, struct dsp_log_dev, cdev);
	the_dsp_log = dsp_log;

	pr_info("%s, minor = %d, dsp_log->minor =%d\n", "audio_dsp_open",
		MINOR(inode->i_rdev), dsp_log->minor);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dst = dsp_log->priv->dst;
	ctx->channel = dsp_log->priv->channel;
	ctx->mode = dsp_log->priv->usemem_mode;
	ctx->dsp_addr = dsp_log->priv->usemem_dspaddr;
	ctx->buf = dsp_log->priv->usemem_virt;
	ctx->size = dsp_log->priv->usemem_bytes;
	ctx->waitq = &dsp_log->mem_ready_wait;
	ctx->dsp_log = dsp_log;
	ctx->seq = 0;

	file->private_data = ctx;
	return 0;
}

static int audio_dsp_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	pr_info("%s\n", "audio_dsp_release");
	return 0;
}

/*
 * Build the 20 byte header that the userspace log parser expects in front of
 * every packet.  The checksum is the one's complement of the folded length.
 */
static void audio_dsp_build_packet(struct audio_dsp_file *ctx, size_t len)
{
	struct audio_log_packet *pkt = &ctx->pkt;
	u32 tmp = (u32)len + 0x5a6a;

	pkt->magic = 0x7e7e7e7e;
	pkt->length = (u16)(len + 0x10);
	pkt->reserved = 0;
	pkt->marker = 0x5a5a;
	pkt->checksum = (u16)~(tmp + (tmp >> 16));
	pkt->seq = ctx->seq;
	pkt->payload_len = (u16)(len + 8);
	pkt->type = 0x9d;
	pkt->flag = (u8)((ctx->mode < 4 ? 1 : 0) | 0x40);
	ctx->seq++;
}

static ssize_t audio_dsp_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct audio_dsp_file *ctx = file->private_data;
	char __user *payload = buf;
	size_t copied = 0;
	int ret;
	int timeout;

	if (ctx->log_mode) {
		if (count < sizeof(struct audio_log_packet))
			return -EFAULT;
		payload = buf + sizeof(struct audio_log_packet);
		count -= sizeof(struct audio_log_packet);
	}

	if (ctx->buf && (ctx->mode & ~1) == 2) {
		/* linear read of the memory shared with the DSP */
		if (*ppos >= ctx->size)
			return 0;

		copied = ctx->size - *ppos;
		if (copied > count)
			copied = count;

		if (copy_to_user(payload, (char *)ctx->buf + *ppos, copied)) {
			pr_err("dsp_log_read: failed to copy to user!\n");
			return -EFAULT;
		}
		if (ctx->mode == 2)
			*ppos += copied;
	} else {
		if (!ctx->rec.len) {
			/*
			 * The vendor driver passes -1 (wait forever) when the
			 * file is non blocking and 0 (do not wait) otherwise,
			 * keep that polarity to stay compatible with the
			 * applications shipped on the device.
			 */
			timeout = (file->f_flags & O_NONBLOCK) ? -1 : 0;
			ret = audio_sblock_receive(ctx->dst, ctx->channel,
						   &ctx->rec, timeout);
			if (ret < 0) {
				pr_err("dsp_log_read: failed to receive block!\n");
				return ret;
			}
			ctx->offset = 0;
		}

		copied = ctx->rec.len;
		if (copied > count)
			copied = count;

		if (copy_to_user(payload,
				 (char *)(uintptr_t)ctx->rec.addr + ctx->offset,
				 copied)) {
			pr_err("dsp_log: sblock_read: failed to copy to user!\n");
			return -EFAULT;
		}

		ctx->rec.len -= copied;
		ctx->offset += copied;

		if (!ctx->rec.len) {
			ret = audio_sblock_release(ctx->dst, ctx->channel,
						   &ctx->rec);
			if (ret < 0) {
				pr_err("dsp_log: failed to release block!\n");
				return ret;
			}
		}
	}

	if ((int)copied < 1)
		return copied;

	if (!ctx->log_mode)
		return copied;

	audio_dsp_build_packet(ctx, copied);
	if (copy_to_user(buf, &ctx->pkt, sizeof(ctx->pkt))) {
		pr_err(" dsp log add packet header failed\n");
		return -EFAULT;
	}

	return copied;
}

static ssize_t audio_dsp_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct audio_dsp_file *ctx = file->private_data;
	struct audio_sblock_rec rec;
	size_t wsize;
	int ret;
	int timeout;

	memset(&rec, 0, sizeof(rec));

	timeout = (file->f_flags & O_NONBLOCK) ? -1 : 0;
	ret = audio_sblock_get(ctx->dst, ctx->channel, &rec, timeout);
	if (ret < 0) {
		pr_err("%s: sblock_write: failed to get block!\n",
		       "audio_dsp_write");
		return ret;
	}

	wsize = rec.len;
	if (wsize > count)
		wsize = count;

	pr_info("%s: sblock_write: blk_len %d, count %zd, wsize %d\n",
		"audio_dsp_write", rec.len, count, (int)wsize);

	if (copy_from_user((void *)(uintptr_t)rec.addr, buf, wsize)) {
		pr_err("%s: sblock_write: failed to copy from user!\n",
		       "audio_dsp_write");
		ret = -EFAULT;
		wsize = count;
	} else {
		rec.len = wsize;
		ret = audio_sblock_send(ctx->dst, ctx->channel, &rec);
		if (ret)
			pr_err("%s: sblock_write: failed to send block!",
			       "audio_dsp_write");
	}

	pr_info("%s: sblock_write len= %zu, ret= %d\n", "audio_dsp_write",
		wsize, ret);

	return count - wsize;
}

static unsigned int audio_dsp_poll(struct file *file, poll_table *wait)
{
	struct audio_dsp_file *ctx = file->private_data;
	struct dsp_log_dev *dsp_log;
	unsigned int ret;

	if (ctx->mode < 2)
		return audio_sblock_poll_wait(ctx->dst, ctx->channel, file, wait);

	if (ctx->mode != 2)
		return 0;

	if (wait && ctx->waitq && wait->_qproc)
		wait->_qproc(file, ctx->waitq, wait);

	dsp_log = ctx->dsp_log;
	mutex_lock(&dsp_log->mutex);
	ret = (dsp_log->state == 0) ? 0 : (POLLIN | POLLRDNORM);
	mutex_unlock(&dsp_log->mutex);

	return ret;
}

/* ioctl 9: reserve a shared memory area and hand it over to the DSP -------- */
static int audio_dsp_log_to_memory(struct audio_dsp_file *ctx, u32 enable)
{
	struct dsp_log_dev *dsp_log = ctx->dsp_log;
	u32 size = enable;
	u32 addr;
	u32 handle;
	int ret;

	mutex_lock(&dsp_log->mutex);

	if (enable) {
		if (size > AUD_DSP_LOG_MEM_MAX)
			size = AUD_DSP_LOG_MEM_MAX;

		if (dsp_log->mem_addr) {
			audio_mem_free(AUD_DSP_LOG_MEM_TYPE, dsp_log->mem_addr,
				       dsp_log->mem_size);
			dsp_log->mem_addr = 0;
			audio_mem_unmap(dsp_log->mem_virt);
			dsp_log->mem_virt = NULL;
		}

		handle = audio_mem_alloc(AUD_DSP_LOG_MEM_TYPE, &size);
		if (!handle) {
			ret = -ENOMEM;
			goto out;
		}

		dsp_log->mem_addr = handle;
		dsp_log->mem_virt = audio_mem_vmap(handle, size, 0);
		dsp_log->mem_size = size;

		addr = audio_addr_ap2dsp(AUD_DSP_LOG_MEM_TYPE, handle, 0);
		ret = aud_send_cmd_no_wait(ctx->channel,
					   SMSG_CMD_DSPLOG_TO_MEMORY,
					   addr, size, 0, 0);
		pr_info("DSP_LOG_TO_MEMORY_ENABLE:addr:%x, size_bytes:%d\n",
			addr, size);
	} else {
		pr_info("DSP_LOG_TO_MEMORY_DISABLE:addr:%x, size_bytes:%d\n",
			dsp_log->mem_addr, dsp_log->mem_size);

		ret = aud_send_cmd_no_param(ctx->channel,
					    SMSG_CMD_DSPLOG_TO_MEMORY,
					    0, 0, 0, 0, -1);

		if (dsp_log->mem_addr) {
			audio_mem_free(AUD_DSP_LOG_MEM_TYPE, dsp_log->mem_addr,
				       dsp_log->mem_size);
			dsp_log->mem_addr = 0;
			audio_mem_unmap(dsp_log->mem_virt);
			dsp_log->mem_virt = NULL;
		}
	}

out:
	mutex_unlock(&dsp_log->mutex);
	return ret;
}

/* ioctl 10/11: enable, disable or resize the call_info buffer -------------- */
static int audio_dsp_call_info_cmd(struct audio_dsp_file *ctx, u32 enable)
{
	struct dsp_log_dev *dsp_log = ctx->dsp_log;
	struct audio_dsp_priv *priv = dsp_log->priv;
	u32 size = enable ? 0 : ctx->size;
	int ret;

	ret = aud_send_cmd_no_wait(ctx->channel, SMSG_CMD_CALL_INFO,
				   ctx->dsp_addr, size, 0, 0);
	if (ret < 0) {
		pr_err("%s: fail to send AT command to dsp  %d\n",
		       "audio_dsp_ioctl", ret);
		return -EIO;
	}

	pr_info("call_info : %s send %s successfully, mem_addr_dsp:0x%x, mem_size:0x%x\n",
		"audio_dsp_ioctl", enable ? "disable" : "enable",
		ctx->dsp_addr, ctx->size);

	return 0;
}

static int audio_dsp_call_info_resize(struct audio_dsp_file *ctx, u32 bytes)
{
	struct dsp_log_dev *dsp_log = ctx->dsp_log;
	struct audio_dsp_priv *priv = dsp_log->priv;
	u32 addr;
	u32 handle;
	int ret;

	mutex_lock(&dsp_log->mutex);

	audio_mem_free(priv->usemem_type, priv->usemem_handle,
		       priv->usemem_bytes);
	priv->usemem_bytes = bytes;

	handle = audio_mem_alloc(priv->usemem_type, &priv->usemem_bytes);
	if (!handle) {
		pr_err("Failed to create sblock with addr: %x\n", handle);
		mutex_unlock(&dsp_log->mutex);
		return -ENOMEM;
	}

	priv->usemem_handle = handle;
	pr_info("aud_dsp_addr: mem: dsp space mem_addr %x\n", handle);

	if (priv->usemem_bytes) {
		priv->usemem_virt = audio_mem_vmap(handle, priv->usemem_bytes, 1);
		memset_io(priv->usemem_virt, 0, priv->usemem_bytes);
	}

	addr = audio_addr_ap2dsp(priv->usemem_type, handle, 0);
	priv->usemem_dspaddr = addr;
	ctx->dsp_addr = addr;
	ctx->buf = priv->usemem_virt;
	ctx->size = priv->usemem_bytes;

	ret = aud_send_cmd_no_wait(ctx->channel, SMSG_CMD_CALL_INFO, addr,
				   priv->usemem_bytes, 0, 0);
	if (ret < 0) {
		pr_err("%s: fail to send AT command to dsp  %d\n",
		       "audio_dsp_ioctl", ret);
		mutex_unlock(&dsp_log->mutex);
		return -EIO;
	}

	pr_info("call_info : %s change size successfully, mem_addr_dsp:0x%x, mem_size:0x%x\n",
		"audio_dsp_ioctl", addr, priv->usemem_bytes);

	mutex_unlock(&dsp_log->mutex);
	return ret;
}

static long audio_dsp_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct audio_dsp_file *ctx = file->private_data;
	struct dsp_log_dev *dsp_log;
	u32 value = (u32)arg;
	int ret = 0;

	if (!ctx)
		return -EFAULT;

	dsp_log = ctx->dsp_log;
	if (!dsp_log)
		return -EFAULT;

	pr_info("%s enter into\n", "audio_dsp_ioctl");

	switch (cmd) {
	case _IOW(0x58, 0, u32):
		pr_info(" dsp log enable %ld, audio_buf->channel: %d\n",
			(long)arg, ctx->channel);
		ret = aud_send_cmd_no_wait(ctx->channel, SMSG_CMD_DSPLOG_ENABLE,
					   value, 0, 0, 0);
		if (ret < 0) {
			pr_err("dsp_log %s: fail to send AT command to dsp  %d\n",
			       "audio_dsp_ioctl", ret);
			return -EIO;
		}
		pr_info("dsp_log : %s send enable successfully\n",
			"audio_dsp_ioctl");
		break;
	case _IOW(0x58, 1, u32):
		pr_info(" dsp log path set %ld\n", (long)arg);
		break;
	case _IOW(0x58, 2, u32):
		ctx->log_mode = value;
		break;
	case _IOW(0x58, 3, u32):
		pr_info(" dsp pcm path set %ld\n", (long)arg);
		break;
	case _IOW(0x58, 4, u32):
		pr_info(" dsp pcm enable set %ld\n", (long)arg);
		ret = aud_send_cmd_no_wait(ctx->channel,
					   SMSG_CMD_DSP_PCM_ENABLE, value, 0, 0,
					   0);
		if (ret < 0) {
			pr_err("%s: fail to send AT command to dsp  %d\n",
			       "audio_dsp_ioctl", ret);
			return -EIO;
		}
		pr_info("dsp_log : %s send enable successfully\n",
			"audio_dsp_ioctl");
		break;
	case _IOW(0x58, 5, u32):
		return -EINVAL;
	case _IOW(0x58, 6, u32):
		pr_info(" dsp assert set %ld\n", (long)arg);
		mutex_lock(&dsp_log->mutex);
		dsp_log->state = value;
		if (dsp_log->state == 1)
			__wake_up(ctx->waitq, TASK_NORMAL, 0, NULL);
		mutex_unlock(&dsp_log->mutex);
		break;
	case _IOW(0x58, 7, u32):
		dsp_log->priv->dump_total = value;
		break;
	case _IOW(0x58, 8, u32):
		return -EINVAL;
	case _IOW(0x58, 9, u32):
		ret = audio_dsp_log_to_memory(ctx, value);
		if (ret < 0)
			return ret;
		break;
	case _IOW(0x58, 10, u32):
		ret = audio_dsp_call_info_cmd(ctx, value);
		if (ret < 0)
			return ret;
		break;
	case _IOW(0x58, 11, u32):
		ret = audio_dsp_call_info_resize(ctx, value);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static struct platform_driver audio_dsp_driver = {
	.probe	= audio_dsp_probe,
	.remove	= audio_dsp_remove,
	.driver	= {
		.name		= "sprd_audio_dsp_dump",
		.owner		= THIS_MODULE,
		.of_match_table	= dsp_of_match,
	},
};

static int __init init_module(void)
{
	audio_class = class_create(THIS_MODULE, "sprd_audio_dsp_dump");
	if (IS_ERR(audio_class)) {
		pr_err("Failed to create audio dsp dump class\n");
		return PTR_ERR(audio_class);
	}

	return platform_driver_register(&audio_dsp_driver);
}

static void __exit cleanup_module(void)
{
	class_destroy(audio_class);
	platform_driver_unregister(&audio_dsp_driver);
}

module_init(init_module);
module_exit(cleanup_module);

MODULE_AUTHOR("SPRD");
MODULE_DESCRIPTION("SIPC/AUDIO_DSP_LOG driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sprd_audio_dsp_dump");
