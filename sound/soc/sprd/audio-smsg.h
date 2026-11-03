/* SPDX-License-Identifier: GPL-2.0 */
/*
 * audio-smsg.h - Unisoc SIPC/SMSG driver internal definitions
 *
 * The vendor module `audio_sipc.ko` is built from two source files:
 *
 *   audio-smsg.c  pr_fmt "[Audio:SMSG] "   message layer (rings, channels)
 *   audio-sipc.c  pr_fmt "[Audio:SIPC] "   command layer + platform driver
 *
 * Common layout of the shared objects (taken from the binary):
 *
 * struct aud_ipc g_aud_ipc (.bss+0x000, 0x170 bytes)
 *   0x000 void              *param_virt;      audio_mem_vmap(type 6)
 *   0x008 u32                param_handle;    audio_mem_alloc(type 6)
 *   0x010 u32                param_size;
 *   0x018 struct aud_mem_block sharemem[8];   {u32 type; u32 handle; void *virt; u32 size}
 *   0x0d8 struct mutex       lock;            "&aud_ipc->lock"
 *   0x0f8 struct mutex       lock_block_param; "&aud_ipc->lock_block_param"
 *   0x118 struct aud_mem_block smsg_mem;      {void *virt; void *handle; u32 size}
 *   0x130 struct mbox_client client;
 *   0x168 struct mbox_chan  *chan;
 *
 * struct g_smsg_ipc g_smsg_ipc (.bss+0x170, 0x3e8 bytes)
 *   0x000 const char        *name;            "ap-agdsp-ipc"
 *   0x008 u8                 dst;             1
 *   0x010 void              *tx_buf;
 *   0x018 u32                tx_num;
 *   0x020 u32               *tx_rdptr_p;
 *   0x028 u32               *tx_wrptr_p;
 *   0x030 void              *rx_buf;
 *   0x038 u32                rx_num;
 *   0x040 u32               *rx_rdptr_p;
 *   0x048 u32               *rx_wrptr_p;
 *   0x050 u32                reserved;
 *   0x058 void              (*callback)(void *, void *);
 *   0x068 spinlock_t         lock;
 *   0x06c spinlock_t         lock2;
 *   0x070 spinlock_t         lock3;
 *   0x078 struct mutex       channel_mutexlock[17];  "&(ipc->channel_mutexlock[i])"
 *   0x298 struct aud_smsg_ch *ch[17];
 *   0x320 u32                opening[17];     atomic_inc/dec
 *   0x364 u32                refcnt[17];      atomic
 *   0x3a8 u8                 state[17];       0 = closed, 2 = open, 3 = closing
 *   0x3b9 u8                 wake_flag[17];
 *   0x3cc u32                dsp_ready;
 *   0x3d0 u32                smem_dsp_base;
 *   0x3d4 u32                tx_rd_dsp;
 *   0x3d8 u32                tx_wr_dsp;
 *   0x3dc u32                rx_buf_dsp;
 *   0x3e0 u32                rx_rd_dsp;
 *   0x3e4 u32                rx_wr_dsp;
 *
 * struct aud_smsg_ch (kmalloc(0x1440, GFP_KERNEL | __GFP_ZERO))
 *   0x000 wait_queue_head_t  rxwait;          "&(ch->rxwait)"
 *   0x018 struct mutex       rxlock;          "&(ch->rxlock)"
 *   0x038 u32                rx_wrptr;
 *   0x03c u32                rx_rdptr;
 *   0x040 struct aud_smsg    rx_buf[256];
 *
 * struct aud_smsg (20 bytes)
 *   0x00 u16 command;
 *   0x02 u16 channel;
 *   0x04 u32 parameter[4];
 *
 * The audio shared memory (audio_mem type 7) is split into a tx ring, an rx
 * ring and the four ring indices that live in the last 0x10 bytes:
 *
 *   [ tx: (size - 0x10) / 2 ][ rx: (size - 0x10) / 2 ][ tx_rd tx_wr rx_rd rx_wr ]
 *   num = (size - 0x10) / 40        (20 byte messages per direction)
 */
#ifndef __SOUND_SOC_SPRD_AUDIO_SMSG_H
#define __SOUND_SOC_SPRD_AUDIO_SMSG_H

#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

struct dentry;

#define SIPC_DST_DSP		1
#define SIPC_MAX_CHANNEL	17
#define SIPC_MAX_CHANNEL_MASK	0x10
#define SIPC_MSG_SIZE		0x14
#define SIPC_SW_RING_LEN	256
#define SIPC_SMEM_TAIL		0x10
#define SIPC_CMD_PARAM_MEM	6
#define SIPC_SMSG_MEM		7
#define SIPC_SHAREMEM_NUM	8
#define SIPC_NOTIFY_NUM		5
#define SIPC_DUMP_MAX_MSGS	0x46
#define SIPC_LOG_MAX_MSGS	0x14
#define SIPC_CMD_TIMEOUT	750
#define SIPC_CMD_RETRY		3
#define SIPC_DRAIN_RETRY	0x100
#define SIPC_MBOX_TIMEOUT	0x1f4

#define SIPC_DSP_READY_CMD	0xbeee
#define SIPC_DSP_ASSERT_CMD	0x25
#define SIPC_DSP_RESET_CMD	0x26
#define SIPC_DSP_RESET_CHAN	0x0b
#define SIPC_DSP_ASSERT_CHAN	2

/* 20 byte SIPC message exchanged with the audio DSP */
struct aud_smsg {
	u16 command;
	u16 channel;
	u32 parameter[4];
};

/* one shared buffer described in the device tree / audio_mem */
struct aud_mem_block {
	u32 type;
	u32 handle;
	void *virt;
	u32 size;
};

struct aud_smsg_ch {
	wait_queue_head_t rxwait;
	struct mutex rxlock;
	u32 rx_wrptr;
	u32 rx_rdptr;
	struct aud_smsg rx_buf[SIPC_SW_RING_LEN];
};

struct g_smsg_ipc {
	const char *name;
	u8 dst;
	void *tx_buf;
	u32 tx_num;
	u32 *tx_rdptr_p;
	u32 *tx_wrptr_p;
	void *rx_buf;
	u32 rx_num;
	u32 *rx_rdptr_p;
	u32 *rx_wrptr_p;
	u32 reserved;
	void (*callback)(void *data, void *ipc);
	spinlock_t lock;
	spinlock_t lock2;
	spinlock_t lock3;
	struct mutex channel_mutexlock[SIPC_MAX_CHANNEL];
	struct aud_smsg_ch *ch[SIPC_MAX_CHANNEL];
	u32 opening[SIPC_MAX_CHANNEL];
	u32 refcnt[SIPC_MAX_CHANNEL];
	u8 state[SIPC_MAX_CHANNEL];
	u8 wake_flag[SIPC_MAX_CHANNEL];
	u32 dsp_ready;
	u32 smem_dsp_base;
	u32 tx_rd_dsp;
	u32 tx_wr_dsp;
	u32 rx_buf_dsp;
	u32 rx_rd_dsp;
	u32 rx_wr_dsp;
};

struct aud_ipc {
	void *param_virt;
	u32 param_handle;
	u32 param_size;
	struct aud_mem_block sharemem[SIPC_SHAREMEM_NUM];
	struct mutex lock;
	struct mutex lock_block_param;
	struct aud_mem_block smsg_mem;
	struct mbox_client client;
	struct mbox_chan *chan;
};

/* audio-smsg.c */
extern struct g_smsg_ipc g_smsg_ipc;
extern struct g_smsg_ipc *g_smsg[2];
extern struct g_smsg_ipc *g_ipc;
extern struct mbox_chan *g_mbox_chan;

int aud_smsg_send(u8 dst, struct aud_smsg *msg);
int aud_smsg_recv(u8 dst, struct aud_smsg *msg, u32 timeout);
int aud_smsg_wakeup_ch(u8 dst, u8 channel);
int aud_smsg_wakeup_all_ch(struct g_smsg_ipc *ipc);
int aud_smsg_ch_open(u8 dst, u16 channel);
int aud_smsg_ch_close(u8 dst, u16 channel);
int aud_smsg_ipc_create(u8 dst, struct g_smsg_ipc *ipc);
int aud_smsg_ipc_destroy(u8 dst);
int aud_smsg_register_dump_func(void (*func)(void *arg, u32 flag), void *arg);
void aud_smsg_set_mboxchan(struct mbox_chan *chan);
int aud_smsg_init_debugfs(struct dentry *parent);

/* audio-sipc.c */
extern struct aud_ipc g_aud_ipc;

int aud_send_cmd(u16 channel, u16 command, u32 parameter0, u32 parameter1,
		 void *src, u32 size);
int aud_send_cmd_no_param(u16 channel, u16 command, u32 parameter0,
			  u32 parameter1, u32 parameter2, u32 parameter3,
			  u32 timeout);
int aud_send_cmd_no_wait(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 parameter2, u32 parameter3);
int aud_send_cmd_no_wait_param(u16 channel, u16 command, u32 parameter0,
			       u32 parameter1, void *src, u32 size);
int aud_send_cmd_result(u16 channel, u16 command, u32 parameter0,
			u32 parameter1, void *src, u32 size, void *dst);
int aud_send_use_noreplychan(u16 channel, u16 command, u32 parameter0,
			     u32 parameter1, u32 parameter2);
int aud_send_block_param(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 idx, void *src, u32 size);
int aud_recv_block_param(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 idx, void *dst, u32 size);
int aud_ipc_dump(void *buf, u32 size);
int aud_ipc_ch_open(u16 channel);
int aud_ipc_ch_close(u16 channel);
int aud_get_aud_ipc_smsg_para_addr(void **handle, void **virt, u32 *size);
int aud_get_aud_ipc_smsg_addr(void **handle, void **virt, u32 *size);
int snd_vbc_sipc_deinit(void);

#endif /* __SOUND_SOC_SPRD_AUDIO_SMSG_H */
