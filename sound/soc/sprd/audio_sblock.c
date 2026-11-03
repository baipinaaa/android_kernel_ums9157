// SPDX-License-Identifier: GPL-2.0
/*
 * audio_sblock.c - Unisoc audio DSP shared memory block allocator
 *
 * Reconstructed from the vendor binary module `audio-dsp-dump.ko`
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50, .text 0x3f7c = 16252
 * bytes, module `audio_dsp_dump`, depends=audio_sipc).  This file is the
 * `audio_sblock` half of that module, audio_dsp_dump.c is the character
 * device half.
 *
 * The block allocator hands out fixed size blocks of one shared memory
 * carve-out (allocated through audio_mem with AUDIO_MEM_DEBUG_DDR32_DMA
 * type 5) to the audio HAL and tells the DSP about them with the SMSG
 * command SMSG_CMD_SBLOCK_INIT (0x21).  The DSP answers with
 * sizeof(struct aud_smsg) / 0x23 messages whose first parameter is 1 when
 * new RX data arrived and 2 when it consumed all TX blocks.
 *
 * ---------------------------------------------------------------------------
 * Shared memory layout (the `poolhd` block, `smem_size` bytes):
 *
 *   0x00 struct audio_sblock_pool pool_a;   TX/RX ring states, AP -> DSP view
 *   0x30 struct audio_sblock_pool pool_b;   TX/RX ring states, DSP -> AP view
 *   0x60 TX data      (tx_blk_num * tx_blk_size)
 *        RX data      (rx_blk_num * rx_blk_size)
 *        TX descriptors of pool_a (tx_blk_num * 8)
 *        RX descriptors of pool_a (rx_blk_num * 8)
 *        TX descriptors of pool_b (tx_blk_num * 8)
 *        RX descriptors of pool_b (rx_blk_num * 8)
 *
 * The paddings in front of every data area and in front of every descriptor
 * array make them (tx_blk_num + rx_blk_num) * 16 bytes, which is what the
 * vendor size computation adds to the 0x60 byte head.  All the addresses
 * stored in the shared memory are DSP addresses (audio_addr_ap2dsp()), the
 * AP keeps the matching virtual addresses in struct audio_sblock_ring.
 *
 * ---------------------------------------------------------------------------
 * Binary layout of struct audio_sblock (kmalloc(0x58))
 *   +0x00 u8   dst
 *   +0x01 u8   channel
 *   +0x02 u16  reserved
 *   +0x04 u32  state                       (0 idle / 1 up)
 *   +0x08 void *smem_virt                  (audio_mem_vmap of mem_handle)
 *   +0x10 u32  dsp_addr                    (audio_addr_ap2dsp(mem_handle))
 *   +0x14 u32  mem_handle                  (audio_mem_alloc() return value)
 *   +0x18 u32  smem_size                   (updated by audio_mem_alloc())
 *   +0x1c u32  tx_blk_size                 ((txblocksize + 7) & ~7)
 *   +0x20 u32  rx_blk_size                 ((rxblocksize + 7) & ~7)
 *   +0x24 u32  tx_blk_num
 *   +0x28 u32  rx_blk_num
 *   +0x2c u32  reserved
 *   +0x30 struct audio_sblock_ring *ring
 *   +0x38 struct task_struct *task
 *   +0x40 void (*notify_fn)(u32 type, void *data)
 *   +0x48 void *notify_data
 *   +0x50 u32  get_err_bxblks
 *   +0x54 u32  receive_err_bxblks
 *   total 0x58
 *
 * Binary layout of struct audio_sblock_ring (kzalloc(0x98))
 *   +0x00 void *poolhd          (= smem_virt, base of pool_a)
 *   +0x08 void *tx_data
 *   +0x10 void *rx_data
 *   +0x18 u32  tx_size
 *   +0x1c u32  rx_size
 *   +0x20 void *p_txblks        (pool_a TX descriptors, written by send)
 *   +0x28 void *p_rxblks        (pool_a RX descriptors, read by receive)
 *   +0x30 void *p_txblks2       (pool_b TX descriptors, read by get)
 *   +0x38 void *p_rxblks2       (pool_b RX descriptors, written by release)
 *   +0x40 u32 *tx_bitmap        (tx_blk_num entries)
 *   +0x48 u32 *rx_bitmap        (rx_blk_num entries)
 *   +0x50 u32  txpos
 *   +0x54 spinlock_t lock_a     (send_ex)
 *   +0x58 spinlock_t lock_b     (receive, get_arrived_count)
 *   +0x5c spinlock_t lock_c     (get, put, get_free_count)
 *   +0x60 spinlock_t lock_d     (release)
 *   +0x68 wait_queue_head_t getwait
 *   +0x80 wait_queue_head_t recvwait
 *   total 0x98
 *
 * Binary function table of this file:
 *   0x0304   32 audio_sblock_send           = send_ex(dst, ch, rec, 1)
 *   0x0324  668 audio_sblock_send_ex
 *   0x05c0  448 audio_sblock_release
 *   0x0780 1064 audio_sblock_receive
 *   0x0ba8  308 audio_sblock_poll_wait
 *   0x0cdc  300 audio_sblock_init           (audio_sblock_ch_open inlined)
 *   0x0e08 1584 audio_sblock_create
 *   0x1438  616 audio_sblock_thread
 *   0x16a0 1048 audio_sblock_get
 *   0x1ab8  292 audio_sblock_destroy
 *   0x1bdc  368 audio_sblock_put
 *   0x1d4c  156 audio_sblock_register_notifier
 *   0x1de8   32 audio_sblock_send_prepare   = send_ex(dst, ch, rec, 0)
 *   0x1e08  216 audio_sblock_send_finish
 *   0x1ee0  184 audio_sblock_get_arrived_count
 *   0x1f98  184 audio_sblock_get_free_count
 * ---------------------------------------------------------------------------
 */

#define pr_fmt(fmt) "[Audio:SBLCK] " fmt

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>

/* number of SMSG destinations and of channels per destination */
#define SBLOCK_DST_NUM		2
#define SBLOCK_CH_NUM		17

/* SMSG command which hands the shared memory to the DSP */
#define SMSG_CMD_SBLOCK_INIT	0x21
/* SMSG answer command, parameter[0] carries AUDIO_DSP_SBLOCK_* */
#define SMSG_CMD_SBLOCK		0x23

/* notifications passed to the registered notifier */
#define AUDIO_DSP_SBLOCK_PLAYBACK	1
#define AUDIO_DSP_SBLOCK_CAPTURE	2

/* audio_mem type used by audio_sblock_create()/audio_sblock_destroy() */
#define SBLOCK_MEM_TYPE		5	/* AUDIO_MEM_DEBUG_DDR32_DMA */

/* offset of the two ring states and of the first data block in the frame */
#define SBLOCK_HEAD_SIZE	0x60

struct audio_sblock_rec {
	u64 addr;
	u32 len;
};

struct audio_sblock_pool {
	u32 tx_base_dsp;
	u32 tx_blk_num;
	u32 tx_blk_size;
	u32 tx_desc_dsp;
	u32 tx_rdptr;
	u32 tx_wrptr;
	u32 rx_base_dsp;
	u32 rx_blk_num;
	u32 rx_blk_size;
	u32 rx_desc_dsp;
	u32 rx_rdptr;
	u32 rx_wrptr;
};

struct audio_sblock_desc {
	u32 addr;
	u32 len;
};

struct audio_sblock_ring {
	void *poolhd;
	void *tx_data;
	void *rx_data;
	u32 tx_size;
	u32 rx_size;
	void *p_txblks;
	void *p_rxblks;
	void *p_txblks2;
	void *p_rxblks2;
	u32 *tx_bitmap;
	u32 *rx_bitmap;
	u32 txpos;
	spinlock_t lock_a;
	spinlock_t lock_b;
	spinlock_t lock_c;
	spinlock_t lock_d;
	wait_queue_head_t getwait;
	wait_queue_head_t recvwait;
};

struct audio_sblock {
	u8 dst;
	u8 channel;
	u16 reserved0;
	u32 state;
	void *smem_virt;
	u32 dsp_addr;
	u32 mem_handle;
	u32 smem_size;
	u32 tx_blk_size;
	u32 rx_blk_size;
	u32 tx_blk_num;
	u32 rx_blk_num;
	u32 reserved1;
	struct audio_sblock_ring *ring;
	struct task_struct *task;
	void (*notify_fn)(u32 type, void *data);
	void *notify_data;
	u32 get_err_bxblks;
	u32 receive_err_bxblks;
};

struct audio_sblock_table {
	struct audio_sblock *ch[SBLOCK_CH_NUM];
};

/* .bss+0x00, 2 * 0x88 bytes */
static struct audio_sblock_table sblocks[SBLOCK_DST_NUM];

/* provided by audio_sipc.ko ----------------------------------------------- */
extern int aud_smsg_ch_open(u8 dst, u8 channel);
extern int aud_smsg_ch_close(u8 dst, u8 channel);
extern int aud_smsg_recv(u8 dst, struct aud_smsg *msg, int timeout);
extern int aud_send_cmd_no_param(u8 channel, u32 command, u32 p0, u32 p1,
				 u32 p2, u32 p3, int timeout);

/* provided by audio_mem.ko ------------------------------------------------- */
extern u32 audio_mem_alloc(u32 mem_type, u32 *size);
extern void audio_mem_free(u32 mem_type, u32 addr, u32 size);
extern void *audio_mem_vmap(phys_addr_t paddr, size_t size, u32 flag);
extern void audio_mem_unmap(void *vaddr);
extern u32 audio_addr_ap2dsp(u32 mem_type, u32 addr, u32 dir);

/*
 * 20 byte SIPC message, same definition as the one used by audio-pipe.c.
 * parameter[0] carries the SBLOCK event of SMSG_CMD_SBLOCK answers.
 */
struct aud_smsg {
	u16 command;
	u16 channel;
	u32 parameter[4];
};

static int audio_sblock_send_ex(u8 dst, u8 channel,
				struct audio_sblock_rec *rec, u32 flag);

/*
 * Convert the AP virtual address of a block into the DSP address that the
 * DSP has to program into its descriptors.  The shared memory is one
 * contiguous frame so a plain offset computation is enough.
 */
static u32 audio_sblock_ap2dsp_addr(struct audio_sblock *sblock, u64 addr)
{
	return sblock->dsp_addr + (u32)addr - (u32)(uintptr_t)sblock->smem_virt;
}

/* ------------------------------------------------------------------------- */
/* creation and destruction                                                  */
/* ------------------------------------------------------------------------- */

/*
 * int audio_sblock_create(u8 dst, u8 channel, u32 tx_blk_num,
 *				  u32 tx_blk_size, u32 rx_blk_num,
 *				  u32 rx_blk_size, u32 mem_type)
 *
 * Allocates the shared memory, lays out the two ring states, the data areas
 * and the four descriptor arrays, fills the descriptor arrays of the "DSP
 * consumed" side and starts the SMSG listener thread.
 */
int audio_sblock_create(u8 dst, u8 channel, u32 tx_blk_num,
			       u32 tx_blk_size, u32 rx_blk_num,
			       u32 rx_blk_size, u32 mem_type)
{
	struct audio_sblock *sblock;
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *pa, *pb;
	struct audio_sblock_desc *desc;
	u32 tx_size, rx_size, tx_total, rx_total, size;
	u32 i, off;
	int ret = -ENOMEM;

	sblock = kzalloc(sizeof(*sblock), GFP_KERNEL);
	if (!sblock)
		return -ENOMEM;

	tx_size = (tx_blk_size + 7) & ~7;
	rx_size = (rx_blk_size + 7) & ~7;
	tx_total = tx_size * tx_blk_num;
	rx_total = rx_size * rx_blk_num;
	size = SBLOCK_HEAD_SIZE + tx_total + rx_total +
	       ((tx_blk_num + rx_blk_num) << 4);

	sblock->state = 0;
	sblock->dst = dst;
	sblock->channel = channel;
	sblock->tx_blk_num = tx_blk_num;
	sblock->rx_blk_num = rx_blk_num;
	sblock->tx_blk_size = tx_size;
	sblock->rx_blk_size = rx_size;
	sblock->smem_size = size;

	sblock->mem_handle = audio_mem_alloc(mem_type, &sblock->smem_size);
	printk(KERN_INFO "[Audio:SBLCK] %s smem_addr is %#x\n",
	       "audio_sblock_create", sblock->mem_handle);
	if (!sblock->mem_handle) {
		printk(KERN_ERR "[Audio:SBLCK] Failed to allocate smem for sblock\n");
		goto free_sblock;
	}

	sblock->smem_virt = audio_mem_vmap(sblock->mem_handle,
					   sblock->smem_size, 1);
	if (!sblock->smem_virt) {
		printk(KERN_ERR "[Audio:SBLCK] %s audio_mem_vmap failed for sblock->smem_virt\n",
		       "audio_sblock_create");
		goto free_mem;
	}
	memset_io(sblock->smem_virt, 0, sblock->smem_size);
	sblock->dsp_addr = audio_addr_ap2dsp(mem_type, sblock->mem_handle, 0);

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	sblock->ring = ring;
	if (!ring) {
		printk(KERN_ERR "[Audio:SBLCK] Failed to allocate memory for ring\n");
		ret = -ENOMEM;
		goto unmap_mem;
	}

	printk(KERN_INFO "[Audio:SBLCK] summer: %s head size is %d\n",
	       "audio_sblock_create", SBLOCK_HEAD_SIZE);

	/* pool_a, the ring state pair the AP writes */
	pa = sblock->smem_virt;
	pa->tx_base_dsp = sblock->dsp_addr + SBLOCK_HEAD_SIZE;
	pa->tx_blk_num = tx_blk_num;
	pa->tx_blk_size = tx_size;
	pa->tx_rdptr = 0;
	pa->tx_wrptr = 0;
	/* descriptor arrays live behind the two data areas */
	pa->tx_desc_dsp = sblock->dsp_addr + SBLOCK_HEAD_SIZE + tx_total +
			  rx_total;
	pa->rx_base_dsp = pa->tx_base_dsp + tx_total;
	pa->rx_blk_num = rx_blk_num;
	pa->rx_blk_size = rx_size;
	pa->rx_rdptr = 0;
	pa->rx_wrptr = 0;
	pa->rx_desc_dsp = pa->tx_desc_dsp + (tx_blk_num << 3);

	/* pool_b, the mirrored ring state pair the DSP writes */
	pb = (void *)pa + sizeof(*pa);
	pb->tx_base_dsp = sblock->dsp_addr + SBLOCK_HEAD_SIZE;
	pb->tx_blk_num = tx_blk_num;
	pb->tx_blk_size = tx_size;
	pb->tx_rdptr = 0;
	pb->tx_wrptr = 0;
	pb->tx_desc_dsp = pa->rx_desc_dsp + (rx_blk_num << 3);
	pb->rx_base_dsp = pb->tx_base_dsp + tx_total;
	pb->rx_blk_num = rx_blk_num;
	pb->rx_blk_size = rx_size;
	pb->rx_rdptr = 0;
	pb->rx_wrptr = 0;
	pb->rx_desc_dsp = pb->tx_desc_dsp + (tx_blk_num << 3);

	ring->tx_bitmap = kmalloc(tx_blk_num * sizeof(u32), GFP_KERNEL);
	if (!ring->tx_bitmap) {
		printk(KERN_ERR "[Audio:SBLCK] Failed to allocate memory for txrecord\n");
		ret = -ENOMEM;
		goto free_ring;
	}
	ring->rx_bitmap = kmalloc(rx_blk_num * sizeof(u32), GFP_KERNEL);
	if (!ring->rx_bitmap) {
		ret = -ENOMEM;
		goto free_ring;
	}

	ring->poolhd = sblock->smem_virt;
	ring->tx_data = sblock->smem_virt +
			(pa->tx_base_dsp - sblock->dsp_addr);
	ring->p_txblks = sblock->smem_virt +
			 (pa->tx_desc_dsp - sblock->dsp_addr);
	ring->rx_data = sblock->smem_virt +
			(pa->rx_base_dsp - sblock->dsp_addr);
	ring->p_rxblks = sblock->smem_virt +
			 (pa->rx_desc_dsp - sblock->dsp_addr);
	ring->p_txblks2 = sblock->smem_virt +
			  (pb->tx_desc_dsp - sblock->dsp_addr);
	ring->p_rxblks2 = sblock->smem_virt +
			  (pb->rx_desc_dsp - sblock->dsp_addr);
	ring->rx_size = rx_total;
	ring->tx_size = tx_total;

	/*
	 * The "DSP consumed" TX descriptors and the free bitmaps start out
	 * with every block filled in.  pool_b is the side get() reads, so its
	 * write pointer starts at tx_blk_num (all blocks are free).
	 */
	off = 0;
	for (i = 0; i < tx_blk_num; i++) {
		desc = ring->p_txblks2;
		desc[i].addr = pa->tx_base_dsp + off;
		desc[i].len = tx_size;
		ring->tx_bitmap[i] = 0;
		/* the vendor prints the pool_b RX descriptor here, keep it */
		printk(KERN_INFO "[Audio:SBLCK] summer %s:p_txblks[%d].addr 0x%08x\n",
		       "audio_sblock_create", i,
		       ((struct audio_sblock_desc *)ring->p_rxblks2)[i].addr);
		off += tx_size;
		pb->tx_wrptr++;
	}

	/* the RX descriptors are handed back to the DSP, pool_b side */
	off = 0;
	for (i = 0; i < rx_blk_num; i++) {
		desc = ring->p_rxblks2;
		desc[i].addr = pb->rx_base_dsp + off;
		desc[i].len = rx_size;
		ring->rx_bitmap[i] = 0;
		printk(KERN_INFO "[Audio:SBLCK] summer %s:p_rxblks[%d].addr 0x%08x, 0x%lx\n",
		       "audio_sblock_create", i, desc[i].addr,
		       (unsigned long)kthread_should_stop());
		off += rx_size;
		pb->rx_wrptr++;
	}
	printk(KERN_ERR "[Audio:SBLCK] summer : poolhd->rxblk_wrptr %d\n",
	       pb->rx_wrptr);

	ring->txpos = 1;
	init_waitqueue_head(&sblock->ring->getwait);
	init_waitqueue_head(&sblock->ring->recvwait);
	spin_lock_init(&ring->lock_a);
	spin_lock_init(&ring->lock_b);
	spin_lock_init(&ring->lock_c);
	spin_lock_init(&ring->lock_d);

	sblock->task = kthread_create_on_node(audio_sblock_thread, sblock, -1,
					      "audio_sblock-%d-%d", dst, channel);
	if (IS_ERR(sblock->task)) {
		ret = PTR_ERR(sblock->task);
		printk(KERN_ERR "[Audio:SBLCK] Failed to create kthread: sblock-%d-%d\n",
		       dst, channel);
		goto free_ring;
	}

	sblocks[dst].ch[channel] = sblock;
	sblock->state = 1;
	wake_up_process(sblock->task);
	return 0;

free_ring:
	if (ring->rx_bitmap)
		kfree(ring->rx_bitmap);
	if (ring->tx_bitmap)
		kfree(ring->tx_bitmap);
	kfree(ring);
unmap_mem:
	audio_mem_unmap(sblock->smem_virt);
free_mem:
	audio_mem_free(SBLOCK_MEM_TYPE, sblock->mem_handle, sblock->smem_size);
free_sblock:
	kfree(sblock);
	return ret;
}

/*
 * int audio_sblock_destroy(u8 dst, u8 channel)
 *
 * Stops the listener thread, wakes up every sleeper, releases the shared
 * memory and the ring bookkeeping.
 */
int audio_sblock_destroy(u8 dst, u8 channel)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;

	printk(KERN_INFO "[Audio:SBLCK] %s dst:%d, channel:%d",
	       "audio_sblock_destroy", dst, channel);
	if (!sblock)
		return 0;

	ring = sblock->ring;
	sblock->state = 0;
	aud_smsg_ch_close(dst, channel);
	if (sblock->task && !IS_ERR(sblock->task))
		kthread_stop(sblock->task);

	if (ring) {
		wake_up(&ring->recvwait);
		wake_up(&ring->getwait);
		kfree(ring->tx_bitmap);
		kfree(ring->rx_bitmap);
		kfree(ring);
	}
	if (sblock->smem_virt)
		audio_mem_unmap(sblock->smem_virt);
	audio_mem_free(SBLOCK_MEM_TYPE, sblock->mem_handle, sblock->smem_size);
	kfree(sblock);
	sblocks[dst].ch[channel] = NULL;
	return 0;
}
EXPORT_SYMBOL(audio_sblock_destroy);

/*
 * static int audio_sblock_thread(void *arg)
 *
 * The SMSG listener.  It turns SCHED_FIFO with priority 90, sends
 * SMSG_CMD_SBLOCK_INIT with the DSP address and the size of the shared
 * memory and from then on only forwards the DSP events to the wait queues
 * of the ring and to the notifier registered by the character device.
 */
static int audio_sblock_thread(void *arg)
{
	struct audio_sblock *sblock = arg;
	struct audio_sblock_ring *ring = sblock->ring;
	struct sched_param param = { .sched_priority = 90 };
	struct audio_sblock_pool *poolhd = ring->poolhd;
	struct aud_smsg msg;
	int ret;

	sched_setscheduler(current, SCHED_FIFO, &param);

	printk(KERN_INFO "[Audio:SBLCK] %s: send smem_addr  to dsp,  sblock->channel=%d\n",
	       "audio_sblock_thread", sblock->channel);
	ret = aud_send_cmd_no_param(sblock->channel, SMSG_CMD_SBLOCK_INIT, 1,
				    sblock->dsp_addr, sblock->smem_size, 0, -1);
	printk(KERN_INFO "[Audio:SBLCK] aud_send_cmd_noparam returned.\n");
	if (ret < 0) {
		printk(KERN_ERR "[Audio:SBLCK] %s: fail to send SMSG_CMD_SBLOCK_INIT to dsp  %d\n",
		       "audio_sblock_thread", ret);
		return -EIO;
	}
	sblock->state = 1;
	printk(KERN_INFO "[Audio:SBLCK] %s listen to smsg from dsp\n",
	       "audio_sblock_thread");

	while (!kthread_should_stop()) {
		memset(&msg, 0, sizeof(msg));
		msg.command = 0;
		msg.channel = sblock->channel;
		ret = aud_smsg_recv(sblock->dst, &msg, -1);
		if (ret == -EPIPE) {
			msleep(1000);
			continue;
		}
		if (ret == -EIO || ret == -ENODEV) {
			msleep(100);
			continue;
		}
		if (msg.command != SMSG_CMD_SBLOCK)
			continue;

		if (msg.parameter[0] == AUDIO_DSP_SBLOCK_CAPTURE) {
			/* new capture data is available */
			wake_up(&ring->getwait);
		} else if (msg.parameter[0] == AUDIO_DSP_SBLOCK_PLAYBACK) {
			/* the DSP consumed the queued playback blocks */
			if (poolhd->rx_wrptr == poolhd->rx_rdptr) {
				printk(KERN_INFO "[Audio:SBLCK] p_tt: %d,%d\n",
				       poolhd->rx_wrptr, poolhd->rx_rdptr);
				continue;
			}
			wake_up(&ring->recvwait);
		} else {
			continue;
		}

		if (sblock->notify_fn)
			sblock->notify_fn(msg.parameter[0] ==
					  AUDIO_DSP_SBLOCK_PLAYBACK ?
					  AUDIO_DSP_SBLOCK_CAPTURE :
					  AUDIO_DSP_SBLOCK_PLAYBACK,
					  sblock->notify_data);
	}

	printk(KERN_INFO "[Audio:SBLCK] %d-%d thread stop", sblock->dst,
	       sblock->channel);
	return 0;
}

/*
 * static int audio_sblock_send_ex(u8 dst, u8 channel,
 *				   struct audio_sblock_rec *rec, u32 flag)
 *
 * Queues one TX block.  The descriptor is written into pool_a (the ring the
 * DSP drains) and the write pointer is advanced.  When "flag" has bit 0
 * clear (audio_sblock_send_prepare()) the caller only parks the block: if
 * the queue holds exactly one block the "tx in progress" flag is raised so
 * that send_finish() can clear it later.
 */
static int audio_sblock_send_ex(u8 dst, u8 channel,
				struct audio_sblock_rec *rec, u32 flag)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	struct audio_sblock_desc *desc;
	unsigned long flags;
	u32 idx, wrptr;

	if (!sblock) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}
	if (sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -EIO;
	}

	ring = sblock->ring;
	if (rec->addr < (u64)(uintptr_t)ring->tx_data ||
	    rec->addr + rec->len >
	    (u64)(uintptr_t)ring->tx_data + ring->tx_size) {
		printk(KERN_ERR "[Audio:SBLCK] %s:block ch:%d,error:addr:0x%p,length:%x\n",
		       "audio_sblock_send_ex", channel, (void *)rec->addr,
		       rec->len);
		return -EINVAL;
	}

	poolhd = ring->poolhd;
	spin_lock_irqsave(&ring->lock_a, flags);
	wrptr = poolhd->tx_wrptr;
	idx = wrptr % poolhd->tx_blk_num;
	desc = ring->p_txblks;
	desc[idx].addr = audio_sblock_ap2dsp_addr(sblock, rec->addr);
	desc[idx].len = rec->len;
	pr_debug("[Audio:SBLCK] sblock_send: dst=%d, channel=%d, addr=%p, len=%d\n",
		 dst, channel, (void *)rec->addr, rec->len);
	poolhd->tx_wrptr = wrptr + 1;

	if (sblock->state == 1) {
		if (!(flag & 1)) {
			if (!ring->txpos &&
			    (poolhd->tx_wrptr - poolhd->tx_rdptr) == 1)
				ring->txpos = 1;
		}
	}
	pr_debug("[Audio:SBLCK] sblock_send: channel=%d, wrptr=%d, txpos=%d, addr=%x\n",
		 channel, wrptr, idx, desc[idx].addr);
	ring->tx_bitmap[((u32)rec->addr -
			 (u32)(uintptr_t)ring->tx_data) /
			sblock->tx_blk_size] = 0;
	spin_unlock_irqrestore(&ring->lock_a, flags);

	return 0;
}

/*
 * int audio_sblock_send(u8 dst, u8 channel, struct audio_sblock_rec *rec)
 *
 * Queue one TX block and tell the DSP right away.
 */
int audio_sblock_send(u8 dst, u8 channel, struct audio_sblock_rec *rec)
{
	return audio_sblock_send_ex(dst, channel, rec, 1);
}
EXPORT_SYMBOL(audio_sblock_send);

/*
 * int audio_sblock_send_prepare(u8 dst, u8 channel,
 *				 struct audio_sblock_rec *rec)
 *
 * Queue one TX block without the "sent" marker, used when the caller wants
 * to batch several blocks before the DSP gets notified.
 */
int audio_sblock_send_prepare(u8 dst, u8 channel, struct audio_sblock_rec *rec)
{
	return audio_sblock_send_ex(dst, channel, rec, 0);
}

/*
 * int audio_sblock_send_finish(u8 dst, u8 channel)
 *
 * Clears the "tx in progress" marker raised by send_prepare().  When it is
 * already clear the vendor code only emits a debug message.
 */
int audio_sblock_send_finish(u8 dst, u8 channel)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];

	if (!sblock) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}
	if (sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -EIO;
	}
	if (sblock->ring->txpos)
		sblock->ring->txpos = 0;
	else
		pr_debug("[Audio:SBLCK] %s do nothing\n",
			 "audio_sblock_send_finish");

	return 0;
}

/*
 * int audio_sblock_get(u8 dst, u8 channel, struct audio_sblock_rec *rec,
 *			int timeout)
 *
 * Takes one free TX block out of pool_b.  "timeout" is expressed in jiffies,
 * a negative value waits forever and 0 only polls.
 */
int audio_sblock_get(u8 dst, u8 channel, struct audio_sblock_rec *rec,
		     int timeout)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	struct audio_sblock_desc *desc;
	unsigned long flags;
	u32 addr, idx;
	int ret;

	if (!sblock) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}
	if (sblock->state != 1)
		return -EIO;

	ring = sblock->ring;
	poolhd = ring->poolhd;

	if (poolhd->tx_rdptr == poolhd->tx_wrptr) {
		if (timeout == 0) {
			printk(KERN_ERR "[Audio:SBLCK] sblock_get %d-%d is empty!\n",
			       dst, channel);
			return -ENODATA;
		}
		if (timeout < 0) {
			ret = wait_event_interruptible(ring->getwait,
				 poolhd->tx_rdptr != poolhd->tx_wrptr ||
				 !sblock->state);
			if (ret < 0)
				printk(KERN_ERR "[Audio:SBLCK] sblock_get wait interrupted!\n");
		} else {
			ret = wait_event_interruptible_timeout(ring->getwait,
				 poolhd->tx_rdptr != poolhd->tx_wrptr,
				 timeout);
			if (ret < 0) {
				printk(KERN_ERR "[Audio:SBLCK] sblock_get wait interrupted!\n");
			} else if (ret == 0) {
				printk(KERN_ERR "[Audio:SBLCK] sblock_get wait timeout!\n");
				ret = -ETIME;
			}
		}
		if (!sblock->state) {
			printk(KERN_ERR "[Audio:SBLCK] sblock_get sblock state is idle!\n");
			return -EIO;
		}
		if (ret < 0)
			return ret;
	}

	spin_lock_irqsave(&ring->lock_c, flags);
	if (poolhd->tx_rdptr == poolhd->tx_wrptr || sblock->state != 1) {
		ret = (sblock->state == 1) ? -EAGAIN : -EIO;
		goto unlock;
	}

	idx = poolhd->tx_rdptr % poolhd->tx_blk_num;
	desc = ring->p_txblks2;
	addr = desc[idx].addr;
	if (addr < poolhd->tx_base_dsp ||
	    addr + poolhd->tx_blk_size > poolhd->tx_base_dsp + ring->tx_size) {
		sblock->get_err_bxblks++;
		printk(KERN_ERR "[Audio:SBLCK] %s:block address error:ch:%d, block error:addr:0x%p,length:%x,err bxblks:%d\n",
		       "audio_sblock_get", channel, (void *)rec->addr,
		       rec->len, sblock->get_err_bxblks);
		ret = -ENXIO;
		poolhd->tx_rdptr++;
		goto unlock;
	}

	rec->addr = (u64)(uintptr_t)sblock->smem_virt +
		    (u32)(addr - sblock->dsp_addr);
	rec->len = poolhd->tx_blk_size;
	ring->tx_bitmap[((u32)rec->addr -
			 (u32)(uintptr_t)ring->tx_data) /
			sblock->tx_blk_size] = 1;
	poolhd->tx_rdptr++;
	ret = 0;

unlock:
	spin_unlock_irqrestore(&ring->lock_c, flags);
	return ret;
}
EXPORT_SYMBOL(audio_sblock_get);

/*
 * int audio_sblock_put(u8 dst, u8 channel, struct audio_sblock_rec *rec)
 *
 * Gives a TX block taken by get() back without sending it: the block is
 * pushed in front of pool_b again (read pointer rewind) and its bitmap
 * entry is cleared.
 */
int audio_sblock_put(u8 dst, u8 channel, struct audio_sblock_rec *rec)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	struct audio_sblock_desc *desc;
	unsigned long flags;
	u32 idx, rdptr;

	if (!sblock)
		return 0;

	ring = sblock->ring;
	poolhd = ring->poolhd;
	spin_lock_irqsave(&ring->lock_c, flags);
	rdptr = poolhd->tx_rdptr - 1;
	idx = rdptr % poolhd->tx_blk_num;
	desc = ring->p_txblks;
	/*
	 * The vendor computes the DSP address from the allocation handle
	 * instead of sblock->dsp_addr here; on this platform both are the
	 * same DDR32 window, so keep the binary behaviour.
	 */
	desc[idx].addr = sblock->mem_handle +
			 (u32)rec->addr - (u32)(uintptr_t)sblock->smem_virt;
	desc[idx].len = poolhd->tx_blk_size;
	poolhd->tx_rdptr = rdptr;
	if (poolhd->tx_wrptr - poolhd->tx_rdptr == 1)
		wake_up(&ring->getwait);
	ring->tx_bitmap[((u32)rec->addr -
			 (u32)(uintptr_t)ring->tx_data) /
			sblock->tx_blk_size] = 0;
	spin_unlock_irqrestore(&ring->lock_c, flags);

	return 0;
}

/*
 * int audio_sblock_receive(u8 dst, u8 channel, struct audio_sblock_rec *rec,
 *			    int timeout)
 *
 * Takes one capture block out of pool_a.  The DSP fills the descriptors of
 * that ring, so the address and the length come straight from the shared
 * memory.
 */
int audio_sblock_receive(u8 dst, u8 channel, struct audio_sblock_rec *rec,
			 int timeout)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	struct audio_sblock_desc *desc;
	unsigned long flags;
	u32 idx;
	int ret;

	if (!sblock) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}
	if (sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -EIO;
	}

	ring = sblock->ring;
	poolhd = ring->poolhd;

	if (poolhd->rx_wrptr == poolhd->rx_rdptr) {		if (timeout == 0) {
			printk(KERN_ERR "[Audio:SBLCK] sblock_receive %d-%d is empty!\n",
			       dst, channel);
			return -ENODATA;
		}
		if (timeout < 0) {
			ret = wait_event_interruptible(ring->recvwait,
				 poolhd->rx_wrptr != poolhd->rx_rdptr ||
				 !sblock->state);
			if (ret < 0)
				printk(KERN_ERR "[Audio:SBLCK] sblock_receive wait interrupted!\n");
		} else {
			ret = wait_event_interruptible_timeout(ring->recvwait,
				 poolhd->rx_wrptr != poolhd->rx_rdptr,
				 timeout);
			if (ret < 0) {
				printk(KERN_ERR "[Audio:SBLCK] sblock_receive wait interrupted!\n");
			} else if (ret == 0) {
				printk(KERN_ERR "[Audio:SBLCK] sblock_receive wait timeout!\n");
				ret = -ETIME;
			}
		}
		if (!sblock->state) {
			printk(KERN_ERR "[Audio:SBLCK] sblock_receive sblock state is idle!\n");
			return -EIO;
		}
		if (ret < 0)
			return ret;
	}

	spin_lock_irqsave(&ring->lock_b, flags);
	if (poolhd->rx_wrptr == poolhd->rx_rdptr || sblock->state != 1) {
		ret = (sblock->state == 1) ? -EAGAIN : -EIO;
		goto unlock;
	}

	idx = poolhd->rx_rdptr % poolhd->rx_blk_num;
	desc = ring->p_rxblks;
	rec->addr = (u64)(uintptr_t)sblock->smem_virt +
		    (u32)(desc[idx].addr - sblock->dsp_addr);
	rec->len = desc[idx].len;
	poolhd->rx_rdptr++;

	if (rec->addr < (u64)(uintptr_t)ring->rx_data ||
	    rec->addr + rec->len >
	    (u64)(uintptr_t)ring->rx_data + ring->rx_size) {
		rec->len = 0;
		rec->addr = 0;
		sblock->receive_err_bxblks++;
		printk(KERN_ERR "[Audio:SBLCK] %s:ch:%d, block error:addr:0x%p,length:%x,err blk cnt:%d\n",
		       "audio_sblock_receive", channel, (void *)rec->addr,
		       rec->len, sblock->receive_err_bxblks);
		ret = (sblock->state == 1) ? -EAGAIN : -EIO;
		goto unlock;
	}

	ring->rx_bitmap[((u32)rec->addr -
			 (u32)(uintptr_t)ring->rx_data) /
			sblock->rx_blk_size] = 1;
	ret = 0;

unlock:
	spin_unlock_irqrestore(&ring->lock_b, flags);
	return ret;
}
EXPORT_SYMBOL(audio_sblock_receive);

/*
 * int audio_sblock_release(u8 dst, u8 channel, struct audio_sblock_rec *rec)
 *
 * Hands a capture block back to the DSP through pool_b, so that the firmware
 * can refill it.
 */
int audio_sblock_release(u8 dst, u8 channel, struct audio_sblock_rec *rec)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	struct audio_sblock_desc *desc;
	unsigned long flags;
	u32 idx, wrptr;

	if (!sblock || sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}

	ring = sblock->ring;
	poolhd = ring->poolhd;
	spin_lock_irqsave(&ring->lock_d, flags);
	wrptr = poolhd->rx_wrptr;
	idx = wrptr % poolhd->rx_blk_num;
	if (rec->addr < (u64)(uintptr_t)ring->rx_data ||
	    rec->addr + rec->len >
	    (u64)(uintptr_t)ring->rx_data + poolhd->rx_blk_size) {
		printk(KERN_ERR "[Audio:SBLCK] sblock_release ch:%d,addr error!addr=0x%p,length:%d",
		       channel, (void *)rec->addr, rec->len);
		spin_unlock_irqrestore(&ring->lock_d, flags);
		return -EINVAL;
	}

	desc = ring->p_rxblks2;
	desc[idx].addr = audio_sblock_ap2dsp_addr(sblock, rec->addr);
	desc[idx].len = poolhd->rx_blk_size;
	poolhd->rx_wrptr = wrptr + 1;
	ring->rx_bitmap[((u32)rec->addr -
			 (u32)(uintptr_t)ring->rx_data) /
			sblock->rx_blk_size] = 0;
	spin_unlock_irqrestore(&ring->lock_d, flags);

	return 0;
}
EXPORT_SYMBOL(audio_sblock_release);

/*
 * unsigned int audio_sblock_poll_wait(u8 dst, u8 channel, struct file *file,
 *				       poll_table *wait)
 *
 * poll() helper: reports POLLIN when capture data is queued.
 */
unsigned int audio_sblock_poll_wait(u8 dst, u8 channel, struct file *file,
				    poll_table *wait)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;

	if (!sblock)
		return -ENODEV;
	if (sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready to poll !\n",
		       dst, channel);
		return -ENODEV;
	}

	ring = sblock->ring;
	poolhd = ring->poolhd;
	poll_wait(file, &ring->getwait, wait);
	poll_wait(file, &ring->recvwait, wait);

	if (poolhd->rx_wrptr == poolhd->rx_rdptr)
		return 0;

	return POLLIN | POLLRDNORM;
}
EXPORT_SYMBOL(audio_sblock_poll_wait);

/* ------------------------------------------------------------------------- */
/* bookkeeping                                                              */
/* ------------------------------------------------------------------------- */

/* int audio_sblock_get_arrived_count(u8 dst, u8 channel) */
int audio_sblock_get_arrived_count(u8 dst, u8 channel)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	unsigned long flags;
	int count;

	if (!sblock || sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}

	ring = sblock->ring;
	poolhd = ring->poolhd;
	spin_lock_irqsave(&ring->lock_b, flags);
	count = poolhd->rx_wrptr - poolhd->rx_rdptr;
	spin_unlock_irqrestore(&ring->lock_b, flags);

	return count;
}

/* int audio_sblock_get_free_count(u8 dst, u8 channel) */
int audio_sblock_get_free_count(u8 dst, u8 channel)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];
	struct audio_sblock_ring *ring;
	struct audio_sblock_pool *poolhd;
	unsigned long flags;
	int count;

	if (!sblock || sblock->state != 1) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}

	ring = sblock->ring;
	poolhd = ring->poolhd;
	spin_lock_irqsave(&ring->lock_c, flags);
	count = poolhd->tx_wrptr - poolhd->tx_rdptr;
	spin_unlock_irqrestore(&ring->lock_c, flags);

	return count;
}

/*
 * int audio_sblock_register_notifier(u8 dst, u8 channel,
 *				      void (*notify_fn)(u32 type, void *data),
 *				      void *data)
 *
 * Only one notifier per channel, the vendor driver refuses a second one
 * with -EBUSY.
 */
int audio_sblock_register_notifier(u8 dst, u8 channel,
				   void (*notify_fn)(u32 type, void *data),
				   void *data)
{
	struct audio_sblock *sblock = sblocks[dst].ch[channel];

	if (!sblock) {
		printk(KERN_ERR "[Audio:SBLCK] sblock-%d-%d not ready!\n",
		       dst, channel);
		return -ENODEV;
	}
	if (sblock->notify_fn) {
		printk(KERN_ERR "[Audio:SBLCK] sblock handler already registered\n");
		return -EBUSY;
	}

	sblock->notify_fn = notify_fn;
	sblock->notify_data = data;

	return 0;
}

/* ------------------------------------------------------------------------- */
/* initialisation                                                           */
/* ------------------------------------------------------------------------- */

/*
 * static int audio_sblock_ch_open(u8 dst, u8 channel)
 *
 * Opens the SMSG channel of one block ring.  The vendor keeps this as a
 * separate static helper, it is inlined into audio_sblock_init() by the
 * compiler (its three printk() calls keep "audio_sblock_ch_open" as %s).
 */
static int audio_sblock_ch_open(u8 dst, u8 channel)
{
	int ret;

	if (dst >= SBLOCK_DST_NUM) {
		printk(KERN_ERR "[Audio:SBLCK] %s, invalid dst:%d\n",
		       "audio_sblock_ch_open", dst);
		return -ENODEV;
	}

	ret = aud_smsg_ch_open(dst, channel);
	if (!ret)
		printk(KERN_INFO "[Audio:SBLCK] %s aud_smsg_ch_opened dst %d,channel=%d\n",
		       "audio_sblock_ch_open", dst, channel);
	else
		printk(KERN_ERR "[Audio:SBLCK] %s, Failed to open channel\n",
		       "audio_sblock_ch_open");

	return ret;
}

/*
 * int audio_sblock_init(u8 dst, u8 channel, u32 tx_blk_num, u32 tx_blk_size,
 *			 u32 rx_blk_num, u32 rx_blk_size, u32 mem_type)
 */
int audio_sblock_init(u8 dst, u8 channel, u32 tx_blk_num, u32 tx_blk_size,
		      u32 rx_blk_num, u32 rx_blk_size, u32 mem_type)
{
	int ret;

	ret = audio_sblock_ch_open(dst, channel);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:SBLCK] %s audio_sblock_ch_open failed open\n",
		       "audio_sblock_init");
		return ret;
	}

	ret = audio_sblock_create(dst, channel, tx_blk_num, tx_blk_size,
				  rx_blk_num, rx_blk_size, mem_type);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:SBLCK] %s audio_sblock_create failed open\n",
		       "audio_sblock_init");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(audio_sblock_init);
