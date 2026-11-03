/*
 * audio-smsg.c - Unisoc SIPC/SMSG message layer (AP <-> audio DSP)
 *
 * Reconstructed from the vendor module `audio_sipc.ko`
 * (vermagic 5.4.210-android12-9-g96901d45d5a5-ab50, .text 0x4170 = 16752 bytes,
 *  43 functions, module name `audio_sipc`, build path
 *  .../audio_driver/sprd_audio/audiosipc/audio-smsg.c).
 *
 * The driver wakes the audio DSP (AGDSP) with the audio mailbox and exchanges
 * fixed 20 byte messages with it through two rings that live in the audio
 * shared memory allocated with audio_mem type 7.  Every message channel
 * (0..16) owns a software ring, a wait queue and a mutex.  The DSP answers
 * channel 2 with command 0x25 when it asserts and channel 1 with command
 * 0xbeee when it becomes ready.
 *
 * Binary function table of this file (.text offsets / sizes):
 *   aud_smsg_ch_send           @0x2d88  564   static
 *   aud_smsg_all_ch_notify     @0x26f0  412   static
 *   aud_smsg_irq_handler       @0x2464  652
 *   aud_smsg_rx_handler        @0x2440   36
 *   aud_smsg_send              @0x20b0  912
 *   aud_smsg_recv              @0x3004 2020
 *   aud_smsg_wakeup_ch         @0x1fe0  196
 *   aud_smsg_wakeup_all_ch     @0x3d88  100
 *   aud_smsg_set_mboxchan      @0x20a4   12
 *   aud_smsg_register_dump_func @0x2fec  24
 *   aud_smsg_ch_open           @0x3a70  372
 *   aud_smsg_ch_close          @0x3c14  344
 *   aud_smsg_ipc_create        @0x3888  488
 *   aud_smsg_ipc_destroy       @0x3848   64
 *   aud_smsg_dump_func         @0x288c 1276   static
 *   aud_smsg_debug_open        @0x3e34   44   static
 *   aud_smsg_debug_show        @0x3e60  596   static
 *   aud_smsg_init_debugfs      @0x3dec   72
 *   aud_smsg_suspend_init      @0x40b4   40
 *   aud_smsg_syscore_suspend   @0x40dc    8   static
 *
 * pr_debug() descriptors found in .rodata __verbose (file audio-smsg.c):
 *   aud_smsg_send        line 604  "%s: command=%d, channel=%d, ..."
 *   aud_smsg_send        line 625  "%s: wrptr=%u, rdptr=%u, txpos=0x%lx"
 *   aud_smsg_irq_handler line  94  "get smsg: wrptr=%u, rdptr=%u"
 *   aud_smsg_irq_handler line 111  "%s get smsg: wrptr=%u, rdptr=%u, rxpos=0x%lx"
 *   aud_smsg_irq_handler line 115  "%s read smsg: command=%d, ..."
 *   aud_smsg_recv        line 674  "%s: dst=%d, channel=%d, timeout=%d"
 *   aud_smsg_recv        line 772  "%s: wrptr=%d, rdptr=%d, rd=%d"
 *   aud_smsg_recv        line 776  "%s: command=%d, channel=%d, ..."
 */
#define pr_fmt(fmt) "[Audio:SMSG] " fmt

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/syscore_ops.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "audio-smsg.h"

/* provided by agdsp_access.ko */
extern void agdsp_access_dumpreg(void);

struct g_smsg_ipc g_smsg_ipc;
struct g_smsg_ipc *g_smsg[2];
struct g_smsg_ipc *g_ipc;

struct mbox_chan *g_mbox_chan;
static u32 g_print_cnt;
static u64 g_msg_log[16];

static void (*g_dump_func)(void *arg, u32 flag);
static void *g_dump_arg;

static ushort debug_enable;
static ushort assert_trigger;
module_param(debug_enable, ushort, 0644);
module_param(assert_trigger, ushort, 0644);

static void aud_smsg_dump_func(u32 arg);
static int aud_smsg_irq_handler(void *data, struct g_smsg_ipc *ipc);
static int aud_smsg_ch_send(struct g_smsg_ipc *ipc, struct aud_smsg *msg);
static int aud_smsg_all_ch_notify(struct g_smsg_ipc *ipc, struct aud_smsg *msg);

/* ------------------------------------------------------------------------ */
/* message rings                                                            */
/* ------------------------------------------------------------------------ */

static int aud_smsg_ch_send(struct g_smsg_ipc *ipc, struct aud_smsg *msg)
{
	struct aud_smsg_ch *ch;
	u32 pos;

	atomic_inc(&ipc->opening[msg->channel]);

	if (!ipc->refcnt[msg->channel] || ipc->state[msg->channel] != 2)
		goto out;

	ch = ipc->ch[msg->channel];
	if (!ch)
		goto out;

	if (ch->rx_wrptr - ch->rx_rdptr >= SIPC_SW_RING_LEN)
		printk(KERN_ERR "aud_smsg_recv timeout and command full\n");

	pos = ch->rx_wrptr & (SIPC_SW_RING_LEN - 1);
	memcpy(&ch->rx_buf[pos], msg, SIPC_MSG_SIZE);
	ch->rx_wrptr++;
	__wake_up(&ch->rxwait, TASK_INTERRUPTIBLE, 0, NULL);

out:
	atomic_dec(&ipc->opening[msg->channel]);
	return 0;
}

static int aud_smsg_all_ch_notify(struct g_smsg_ipc *ipc, struct aud_smsg *msg)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ipc->lock3, flags);
	for (i = 0; i < SIPC_MAX_CHANNEL; i++) {
		msg->channel = i;
		aud_smsg_ch_send(ipc, msg);
	}
	spin_unlock_irqrestore(&ipc->lock3, flags);

	return 0;
}

static int aud_smsg_irq_handler(void *data, struct g_smsg_ipc *ipc)
{
	unsigned long flags;
	struct aud_smsg *msg;
	u32 pos;

	spin_lock_irqsave(&ipc->lock2, flags);

	while (*ipc->rx_wrptr_p != *ipc->rx_rdptr_p) {
		pr_debug("get smsg: wrptr=%u, rdptr=%u\n",
			 *ipc->rx_wrptr_p, *ipc->rx_rdptr_p);
		pos = *ipc->rx_rdptr_p & (ipc->rx_num - 1);
		msg = ipc->rx_buf + pos * SIPC_MSG_SIZE;
		pr_debug("%s get smsg: wrptr=%u, rdptr=%u, rxpos=0x%lx\n",
			 "aud_smsg_irq_handler", *ipc->rx_wrptr_p,
			 *ipc->rx_rdptr_p, (unsigned long)pos);
		pr_debug("%s read smsg: command=%d, channel=%d,parameter0=0x%08x, parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
			 "aud_smsg_irq_handler", msg->command, msg->channel,
			 msg->parameter[0], msg->parameter[1], msg->parameter[2],
			 msg->parameter[3]);

		if (!ipc->dsp_ready &&
		    msg->command == SIPC_DSP_READY_CMD &&
		    msg->parameter[0] == SIPC_DSP_READY_CMD) {
			if (msg->channel == SIPC_DSP_READY_CMD) {
				printk(KERN_ERR "peter: dsp ready now\n");
				ipc->dsp_ready = 1;
				aud_smsg_all_ch_notify(ipc, msg);
				(*ipc->rx_rdptr_p)++;
				continue;
			}
			if (msg->channel != SIPC_DSP_ASSERT_CHAN) {
				if (ipc->dsp_ready)
					aud_smsg_ch_send(ipc, msg);
				(*ipc->rx_rdptr_p)++;
				continue;
			}
		}

		if (msg->channel == SIPC_DSP_ASSERT_CHAN &&
		    msg->command == SIPC_DSP_ASSERT_CMD) {
			ipc->dsp_ready = 0;
			printk(KERN_ERR "peter: dsp assert now:0x%08x,0x%08x,0x%08x,0x%08x\n",
			       msg->parameter[0], msg->parameter[1],
			       msg->parameter[2], msg->parameter[3]);
			aud_smsg_all_ch_notify(ipc, msg);
			(*ipc->rx_rdptr_p)++;
			agdsp_access_dumpreg();
			aud_smsg_dump_func(0);
			*ipc->tx_wrptr_p = 0;
			*ipc->tx_rdptr_p = 0;
			*ipc->rx_wrptr_p = 0;
			*ipc->rx_rdptr_p = 0;
			continue;
		}

		if (ipc->dsp_ready)
			aud_smsg_ch_send(ipc, msg);
		(*ipc->rx_rdptr_p)++;
	}

	spin_unlock_irqrestore(&ipc->lock2, flags);

	return 1;
}

static int aud_smsg_rx_handler(void *data, struct g_smsg_ipc *ipc)
{
	return aud_smsg_irq_handler(data, g_ipc);
}

int aud_smsg_send(u8 dst, struct aud_smsg *msg)
{
	struct g_smsg_ipc *ipc;
	struct aud_smsg_ch *ch;
	unsigned long flags;
	u64 *log;
	u32 pos, state, ready;
	int ret;

	printk(KERN_INFO "%s: dst=%d, channel=%d\n",
	       "aud_smsg_send", dst, msg->channel);

	ipc = g_smsg[dst];
	if (!ipc)
		return -ENODEV;

	ch = ipc->ch[msg->channel];
	if (!ch) {
		printk(KERN_ERR " ERR: channel is null, %s, channel %d not ope\n",
		       "aud_smsg_send", msg->channel);
		return -ENODEV;
	}

	state = ipc->state[msg->channel];
	ready = ipc->dsp_ready;
	if (state != 2 || !ready) {
		printk(KERN_ERR "ERR: %s, channel %d not opened, ipc state : %d!,ipc->dsp_ready %d\n",
		       "aud_smsg_send", msg->channel, state, ready);
		return -EINVAL;
	}

	spin_lock_irqsave(&ipc->lock, flags);

	if (*ipc->tx_wrptr_p - *ipc->tx_rdptr_p >= ipc->tx_num) {
		printk(KERN_WARNING "%s txbuf is full!\n",
		       "aud_smsg_send");
		ret = -EBUSY;
		goto unlock;
	}

	if (!ipc->dsp_ready) {
		printk(KERN_ERR "%s: dsp not ready\n",
		       "aud_smsg_send");
		ret = 0;
		goto unlock;
	}

	pos = *ipc->tx_wrptr_p & (ipc->tx_num - 1);
	memcpy(ipc->tx_buf + pos * SIPC_MSG_SIZE, msg, SIPC_MSG_SIZE);
	(*ipc->tx_wrptr_p)++;

	pr_debug("%s: command=%d, channel=%d, parameter0=0x%08x,parameter1=0x%08x, parameter2=0x%08x, parameter3=0x%08x\n",
		 "aud_smsg_send", msg->command, msg->channel, msg->parameter[0],
		 msg->parameter[1], msg->parameter[2], msg->parameter[3]);
	pr_debug("%s: wrptr=%u, rdptr=%u, txpos=0x%lx\n",
		 "aud_smsg_send", *ipc->tx_wrptr_p, *ipc->tx_rdptr_p,
		 (unsigned long)pos);

	log = &g_msg_log[g_print_cnt];
	*log = ((u64)msg->command << 48) | ((u64)msg->channel << 32) |
	       msg->parameter[0];

	ret = mbox_send_message(g_mbox_chan, log);
	if (ret < 0)
		printk(KERN_ERR "%s, mbox send message error! ret=%d\n",
		       "aud_smsg_send", ret);

	mbox_chan_txdone(g_mbox_chan, 0);

	g_print_cnt = (g_print_cnt + 1 > 15) ? 0 : g_print_cnt + 1;
	ret = 0;

unlock:
	spin_unlock_irqrestore(&ipc->lock, flags);
	return ret;
}
EXPORT_SYMBOL(aud_smsg_send);

int aud_smsg_recv(u8 dst, struct aud_smsg *msg, u32 timeout)
{
	struct g_smsg_ipc *ipc;
	struct aud_smsg_ch *ch;
	wait_queue_head_t *wq;
	DEFINE_WAIT(wait);
	long t;
	int ret;

	ipc = g_smsg[dst];
	if (!ipc) {
		printk(KERN_ERR "%s no ipc  init failed\n",
		       "aud_smsg_recv");
		return -ENODEV;
	}

	atomic_inc(&ipc->opening[msg->channel]);

	ch = ipc->ch[msg->channel];
	if (!ch) {
		printk(KERN_ERR "%s, channel %d not opened!\n",
		       "aud_smsg_recv", msg->channel);
		ret = -ENODEV;
		goto out;
	}

	pr_debug("%s: dst=%d, channel=%d, timeout=%d\n",
		 "aud_smsg_recv", dst, msg->channel, timeout);

	wq = &ch->rxwait;

	if (!timeout) {
		if (!mutex_trylock(&ch->rxlock)) {
			printk(KERN_INFO "%s busy!%d\n",
			       "aud_smsg_recv", ipc->opening[msg->channel]);
			ret = -EBUSY;
			goto out;
		}
		if (ch->rx_wrptr == ch->rx_rdptr) {
			printk(KERN_INFO "warning %s cache is empty!\n",
			       "aud_smsg_recv");
			ret = -ENODATA;
			goto out_unlock;
		}
		t = 0;
		goto copy;
	}

	mutex_lock(&ch->rxlock);

	if ((int)timeout < 0) {
		/*
		 * Blocking wait: sleep until the DSP pushes a message, the
		 * channel is closed, the DSP goes away or somebody wakes us up
		 * explicitly with aud_smsg_wakeup_ch().
		 */
		if (ch->rx_wrptr == ch->rx_rdptr &&
		    ipc->state[msg->channel] != 3 &&
		    ipc->dsp_ready &&
		    !ipc->wake_flag[msg->channel]) {
			for (;;) {
				prepare_to_wait_event(wq, &wait,
						      TASK_INTERRUPTIBLE);
				if (ch->rx_wrptr != ch->rx_rdptr)
					break;
				if (ipc->state[msg->channel] == 3)
					break;
				if (!ipc->dsp_ready)
					break;
				if (ipc->wake_flag[msg->channel])
					break;
				if (signal_pending(current))
					break;
				schedule();
			}
			finish_wait(wq, &wait);
			if (signal_pending(current)) {
				printk(KERN_INFO "%s wait interrupted!\n",
				       "aud_smsg_recv");
				ret = -ERESTARTSYS;
				goto out_unlock;
			}
		}
		t = 0;
	} else {
		while (ch->rx_wrptr == ch->rx_rdptr &&
		       ipc->state[msg->channel] != 3 &&
		       ipc->dsp_ready) {
			prepare_to_wait_event(wq, &wait, TASK_INTERRUPTIBLE);
			if (ch->rx_wrptr != ch->rx_rdptr)
				break;
			if (ipc->state[msg->channel] == 3)
				break;
			if (!ipc->dsp_ready)
				break;
			if (signal_pending(current)) {
				t = -ERESTARTSYS;
				break;
			}
			finish_wait(wq, &wait);
			t = schedule_timeout(timeout);
			if (!t)
				break;
			prepare_to_wait_event(wq, &wait, TASK_INTERRUPTIBLE);
		}
		finish_wait(wq, &wait);
		if (t < 0) {
			printk(KERN_INFO "warning %s wait interrupted!\n",
			       "aud_smsg_recv");
			ret = t;
			goto out_unlock;
		}
		if (!t)
			goto timeout;
	}

	if (ipc->state[msg->channel] != 3 &&
	    ch->rx_wrptr != ch->rx_rdptr)
		goto copy;

	if (ipc->state[msg->channel] == 3)
		printk(KERN_ERR "warning %s smsg channel %d is free ipc->dsp_ready %d!\n",
		       "aud_smsg_recv", msg->channel, ipc->dsp_ready);
	else
		printk(KERN_ERR "%s smsg channel is free!,dsp_ready= %d, channel %d\n",
		       "aud_smsg_recv", ipc->dsp_ready, msg->channel);
	ret = ipc->dsp_ready ? -EIO : -EPIPE;
	goto out_unlock;

timeout:
	printk(KERN_INFO "warning %s wait timeout!\n",
	       "aud_smsg_recv");
	agdsp_access_dumpreg();
	printk(KERN_ERR "audio_smsg.c: timeout and reset dsp\n");
	{
		struct aud_smsg reset;
		unsigned long flags, flags2;
		int i;

		spin_lock_irqsave(&ipc->lock, flags);
		spin_lock_irqsave(&ipc->lock2, flags2);

		ipc->dsp_ready = 0;
		aud_smsg_dump_func(1);
		*ipc->tx_wrptr_p = 0;
		*ipc->tx_rdptr_p = 0;
		*ipc->rx_wrptr_p = 0;
		*ipc->rx_rdptr_p = 0;

		memset(&reset, 0, sizeof(reset));
		reset.command = SIPC_DSP_RESET_CMD;
		reset.channel = SIPC_DSP_RESET_CHAN;
		reset.parameter[0] = SIPC_DSP_RESET_CMD;
		aud_smsg_ch_send(ipc, &reset);
		reset.channel = SIPC_DSP_ASSERT_CHAN;
		aud_smsg_ch_send(ipc, &reset);

		spin_unlock_irqrestore(&ipc->lock2, flags2);
		spin_unlock_irqrestore(&ipc->lock, flags);

		for (i = 0; i < SIPC_MAX_CHANNEL; i++) {
			if (ipc->state[i] == 2)
				__wake_up(ipc->ch[i], TASK_INTERRUPTIBLE, 0,
					  NULL);
		}
	}
	ret = -ETIME;
	goto out_unlock;

copy:
	pr_debug("%s: wrptr=%d, rdptr=%d, rd=%d\n",
		 "aud_smsg_recv", ch->rx_wrptr, ch->rx_rdptr,
		 ch->rx_wrptr - ch->rx_rdptr);
	{
		u32 pos = ch->rx_rdptr & (SIPC_SW_RING_LEN - 1);

		memcpy(msg, &ch->rx_buf[pos], SIPC_MSG_SIZE);
		ch->rx_rdptr++;
	}
	pr_debug("%s: command=%d, channel=%d, parameter0=0x%08x,parameter1=0x%08x, parameter2=0x%08x, parameter3=0x%08x\n",
		 "aud_smsg_recv", msg->command, msg->channel, msg->parameter[0],
		 msg->parameter[1], msg->parameter[2], msg->parameter[3]);
	ret = 0;

out_unlock:
	ipc->wake_flag[msg->channel] = 0;
	mutex_unlock(&ch->rxlock);

out:
	atomic_dec(&ipc->opening[msg->channel]);
	return ret;
}
EXPORT_SYMBOL(aud_smsg_recv);

int aud_smsg_wakeup_ch(u8 dst, u8 channel)
{
	struct g_smsg_ipc *ipc;
	int i;

	ipc = g_smsg[dst];
	if (!ipc)
		return -ENODEV;

	for (i = 0; i < SIPC_MAX_CHANNEL; i++) {
		if (channel != i)
			continue;
		if (ipc->state[i] != 2)
			continue;
		ipc->wake_flag[channel] = 1;
		__wake_up(ipc->ch[i], TASK_INTERRUPTIBLE, 0, NULL);
	}

	return 0;
}
EXPORT_SYMBOL(aud_smsg_wakeup_ch);

int aud_smsg_wakeup_all_ch(struct g_smsg_ipc *ipc)
{
	int i;

	for (i = 0; i < SIPC_MAX_CHANNEL; i++) {
		if (ipc->state[i] == 2)
			__wake_up(ipc->ch[i], TASK_INTERRUPTIBLE, 0, NULL);
	}

	return 0;
}

void aud_smsg_set_mboxchan(struct mbox_chan *chan)
{
	g_mbox_chan = chan;
}
EXPORT_SYMBOL(aud_smsg_set_mboxchan);

int aud_smsg_register_dump_func(void (*func)(void *arg, u32 flag), void *arg)
{
	g_dump_func = func;
	g_dump_arg = arg;
	return 0;
}
EXPORT_SYMBOL(aud_smsg_register_dump_func);

/* ------------------------------------------------------------------------ */
/* message channels                                                         */
/* ------------------------------------------------------------------------ */

int aud_smsg_ch_open(u8 dst, u16 channel)
{
	struct g_smsg_ipc *ipc;
	struct aud_smsg_ch *ch;
	int ret;

	if (channel > SIPC_MAX_CHANNEL_MASK)
		return -1;

	ipc = g_smsg[dst];
	if (!ipc) {
		printk(KERN_ERR "ERR:%s ipc ENODEV\n",
		       "aud_smsg_ch_open");
		return -ENODEV;
	}

	mutex_lock(&ipc->channel_mutexlock[channel]);

	if (ipc->state[channel] == 2) {
		atomic_inc(&ipc->refcnt[channel]);
		mutex_unlock(&ipc->channel_mutexlock[channel]);
		return 0;
	}

	ch = kmalloc(sizeof(*ch), GFP_KERNEL | __GFP_ZERO);
	if (!ch) {
		ret = -ENOMEM;
		mutex_unlock(&ipc->channel_mutexlock[channel]);
		return ret;
	}

	init_waitqueue_head(&ch->rxwait);
	mutex_init(&ch->rxlock);

	ipc->ch[channel] = ch;
	ipc->state[channel] = 2;
	ipc->opening[channel] = 1;
	atomic_dec(&ipc->opening[channel]);
	atomic_inc(&ipc->refcnt[channel]);

	mutex_unlock(&ipc->channel_mutexlock[channel]);
	return 0;
}
EXPORT_SYMBOL(aud_smsg_ch_open);

int aud_smsg_ch_close(u8 dst, u16 channel)
{
	struct g_smsg_ipc *ipc;
	struct aud_smsg_ch *ch;
	int n = 0;

	if (channel > SIPC_MAX_CHANNEL_MASK)
		return -1;

	ipc = g_smsg[dst];
	if (!ipc)
		return 0;

	ch = ipc->ch[channel];
	if (!ch)
		return 0;

	mutex_lock(&ipc->channel_mutexlock[channel]);

	if (atomic_sub_return(1, &ipc->refcnt[channel]) != 0) {
		mutex_unlock(&ipc->channel_mutexlock[channel]);
		return 0;
	}

	ipc->state[channel] = 3;

	if (ipc->ch[channel]) {
		while (ipc->opening[channel]) {
			printk(KERN_ERR "channel %d, is busy\n",
			       channel);
			__wake_up(&ch->rxwait, TASK_UNINTERRUPTIBLE, 1, NULL);
			msleep(1000);
			if (n++ > 0x62)
				break;
		}
		kfree(ch);
		ipc->ch[channel] = NULL;
	}

	ipc->state[channel] = 0;

	mutex_unlock(&ipc->channel_mutexlock[channel]);
	return 0;
}
EXPORT_SYMBOL(aud_smsg_ch_close);

int aud_smsg_ipc_create(u8 dst, struct g_smsg_ipc *ipc)
{
	unsigned long flags;
	int i;

	g_print_cnt = 0;

	if (!ipc->callback)
		ipc->callback = aud_smsg_irq_handler;

	spin_lock_init(&ipc->lock);
	spin_lock_init(&ipc->lock3);

	for (i = 0; i < SIPC_MAX_CHANNEL; i++) {
		ipc->refcnt[i] = 0;
		mutex_init(&ipc->channel_mutexlock[i]);
	}

	(void)flags;

	g_smsg[dst] = ipc;
	ipc->dsp_ready = 1;

	return 0;
}
EXPORT_SYMBOL(aud_smsg_ipc_create);

int aud_smsg_ipc_destroy(u8 dst)
{

	g_smsg[dst] = NULL;
	return 0;
}
EXPORT_SYMBOL(aud_smsg_ipc_destroy);

/* ------------------------------------------------------------------------ */
/* debug                                                                     */
/* ------------------------------------------------------------------------ */

static void aud_smsg_dump_func(u32 arg)
{
	struct g_smsg_ipc *ipc = g_ipc;
	struct aud_smsg *msg;
	u32 rd, wr, num, cnt, last, i, pos;

	/* ---------------------------- rx ring ---------------------------- */
	num = ipc->rx_num;
	rd = *ipc->rx_rdptr_p;
	wr = *ipc->rx_wrptr_p;
	last = (num - 1) & (rd - 1);

	printk(KERN_INFO "%s:rxbuf_addr_p:%x, rxbuf_rdptr_p:%x, rxbuf_wrptr_p:%x\n",
	       "aud_smsg_rx_dump", ipc->rx_buf_dsp, ipc->rx_rd_dsp,
	       ipc->rx_wr_dsp);
	printk(KERN_INFO "%s rx_smsg: wrptr=%u, rdptr=%u, size:%u, last msg addr:%x, rx_rd:%d, last pos:%d\n",
	       "aud_smsg_rx_dump", wr, rd, num,
	       rd * SIPC_MSG_SIZE + ipc->rx_buf_dsp, rd, (int)last);
	cnt = num < SIPC_DUMP_MAX_MSGS ? num : SIPC_DUMP_MAX_MSGS;
	printk(KERN_INFO "start to print last %d msgs audio dsp sent to ap\n", cnt);
	printk(KERN_INFO "last_log_pos:%d,total msg count:%d\n", (int)last, num);

	printk(KERN_INFO "start to print msgs ap sent to audio dsp but not being processed, count:%d\n",
	       wr - rd);

	for (i = 0; rd + i != wr; i++) {
		pos = (num - 1) & (rd + i);
		msg = ipc->rx_buf + pos * SIPC_MSG_SIZE;
		printk(KERN_INFO "%s : smsg:%d, wrptr=%u, rdptr=%u, rx_rd:%u, rxpos=0x%lx\n",
		       "aud_smsg_rx_dump", (int)i, wr, rd, rd,
		       (unsigned long)(rd + i));
		printk(KERN_INFO "%s smsg:%d: command=0x%x, channel=0x%x,parameter0=0x%08x, parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_rx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
	}

	for (i = 0; i < cnt; i++) {
		pos = (num + last + 1 - cnt + i) & (num - 1);
		msg = ipc->rx_buf + pos * SIPC_MSG_SIZE;
		printk(KERN_INFO "%s : -%d,prev rx read smsg: command=0x%x, channel=0x%x,parameter0=0x%08x,parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_rx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
		printk(KERN_INFO "%s : -%d, prev rx read smsg: command=0x%x, channel=0x%x,parameter0=0x%08x, parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_rx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
	}

	/* ---------------------------- tx ring ---------------------------- */
	num = ipc->tx_num;
	rd = *ipc->tx_rdptr_p;
	wr = *ipc->tx_wrptr_p;
	last = (num - 1) & (rd - 1);

	printk(KERN_INFO "%s:txbuf_addr_p:%x, txbuf_rdptr_p:%x, txbuf_wrptr_p:%x\n",
	       "aud_smsg_tx_dump", ipc->smem_dsp_base, ipc->tx_rd_dsp,
	       ipc->tx_wr_dsp);
	printk(KERN_INFO "%s tx_smsg: wrptr=%u, rdptr=%u, msg count:%u, last msg addr:%x, rx_rd:%d, last pos:%d\n",
	       "aud_smsg_tx_dump", wr, rd, num,
	       rd * SIPC_MSG_SIZE + ipc->smem_dsp_base, rd, (int)last);
	cnt = num < SIPC_DUMP_MAX_MSGS ? num : SIPC_DUMP_MAX_MSGS;
	printk(KERN_INFO "start to print last %d msgs ap sent to audio dsp\n", cnt);
	printk(KERN_INFO "last_log_pos:%d,total msg count:%d\n", (int)last, num);

	printk(KERN_INFO "start to print msgs ap sent to audio dsp but not being processed, count:%d\n",
	       wr - rd);

	for (i = 0; rd + i != wr; i++) {
		pos = (num - 1) & (rd + i);
		msg = ipc->tx_buf + pos * SIPC_MSG_SIZE;
		printk(KERN_INFO "%s : smsg:%d, wrptr=%u, rdptr=%u, rx_rd:%u, rxpos=0x%lx\n",
		       "aud_smsg_tx_dump", (int)i, wr, rd, rd,
		       (unsigned long)(rd + i));
		printk(KERN_INFO "%s smsg:%d: command=0x%x, channel=0x%x,parameter0=0x%08x, parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_tx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
	}

	for (i = 0; i < cnt; i++) {
		pos = (num + last + 1 - cnt + i) & (num - 1);
		msg = ipc->tx_buf + pos * SIPC_MSG_SIZE;
		printk(KERN_INFO "%s : -%d,prev tx read smsg: command=0x%x, channel=0x%x,parameter0=0x%08x,parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_tx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
		printk(KERN_INFO "%s : -%d, prev tx read smsg: command=0x%x, channel=0x%x,parameter0=0x%08x, parameter1=0x%08x,parameter2=0x%08x, parameter3=0x%08x\n",
		       "aud_smsg_tx_dump", (int)i, msg->command, msg->channel,
		       msg->parameter[0], msg->parameter[1], msg->parameter[2],
		       msg->parameter[3]);
	}

	if (g_dump_func)
		g_dump_func(g_dump_arg, arg);
}

static int aud_smsg_debug_show(struct seq_file *m, void *v)
{
	struct g_smsg_ipc *ipc;
	int i, j;

	for (i = 0; i < 2; i++) {
		ipc = g_smsg[i];
		if (!ipc)
			continue;
		seq_printf(m, "sipc: %s:\n", ipc->name);
		seq_printf(m, "dst: 0x%0x\n", ipc->dst);
		seq_printf(m, "txbufAddr: 0x%0x, txbufsize: 0x%0x,txbufrdptr: [0x%0x]=%u, txbufwrptr: [0x%0x]=%u\n",
			   ipc->tx_buf, ipc->tx_num, *ipc->tx_rdptr_p, *ipc->tx_rdptr_p,
			   *ipc->tx_wrptr_p, *ipc->tx_wrptr_p);
		seq_printf(m, "rxbufAddr: 0x%0x, rxbufsize: 0x%0x,rxbufrdptr: [0x%0x]=%u, rxbufwrptr: [0x%0x]=%u\n",
			   ipc->rx_buf, ipc->rx_num, *ipc->rx_rdptr_p, *ipc->rx_rdptr_p,
			   *ipc->rx_wrptr_p, *ipc->rx_wrptr_p);
		for (j = 0; j < SIPC_MAX_CHANNEL; j++)
			seq_printf(m, "channel[%d] states: %d\n", j,
				   ipc->state[j]);
	}

	return 0;
}

static int aud_smsg_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, aud_smsg_debug_show, inode->i_private);
}

static const struct file_operations aud_smsg_debug_fops = {
	.llseek = seq_lseek,
	.read = seq_read,
	.open = aud_smsg_debug_open,
	.release = single_release,
};

int aud_smsg_init_debugfs(struct dentry *parent)
{
	if (!parent)
		return -ENXIO;

	debugfs_create_file("audio-smsg-sipc", 0444, parent, NULL,
			    &aud_smsg_debug_fops);
	return 0;
}
