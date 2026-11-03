/*
 * audio-sipc.c - Unisoc SIPC command layer and platform glue
 *
 * Reconstructed from the vendor module `audio_sipc.ko`.  This file owns the
 * command helpers exported to the other audio modules and the platform
 * driver that maps the shared memory (audio_mem types 0..6) and requests the
 * audio mailbox channel.
 *
 * Binary function table of this file (.text offsets / sizes):
 *   aud_send_use_noreplychan      @0x0550   52
 *   aud_send_cmd_no_wait          @0x0584  304
 *   aud_send_cmd_result           @0x06b4  936
 *   aud_recv_cmd                  @0x0a5c  360   static
 *   aud_send_cmd_no_wait_param    @0x0bc4  580
 *   aud_send_cmd_no_param         @0x0e08  496
 *   aud_send_cmd                  @0x0ff8  776
 *   aud_send_block_param          @0x1300  472
 *   aud_ipc_block_param_get       @0x14d8  300   static
 *   aud_recv_block_param          @0x1604  356
 *   aud_ipc_dump                  @0x1768  564
 *   aud_ipc_ch_open               @0x199c   84
 *   aud_ipc_ch_close              @0x19f0   68
 *   aud_get_aud_ipc_smsg_para_addr @0x1a34  68
 *   aud_get_aud_ipc_smsg_addr     @0x1a78   68
 *   snd_vbc_sipc_deinit           @0x1abc   72
 *   audio_sipc_probe              @0x1b04 1200   static (inlines aud_ipc_init,
 *                                                      audio_sipc_create and
 *                                                      aud_ipc_command_param_init)
 *   audio_sipc_remove             @0x1fb4    8   static
 *   audio_sipc_rx_callback        @0x1fbc   36   static
 */
#define pr_fmt(fmt) "[Audio:SIPC] " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "audio-smsg.h"

/* provided by audio_mem.ko */
extern u32 audio_mem_alloc(u32 type, u32 *size);
extern void *audio_mem_vmap(u32 handle, u32 size, u32 flag);

/* provided by agdsp_access.ko */
extern void agdsp_set_mboxchan(struct mbox_chan *chan);

struct aud_ipc g_aud_ipc;

static int aud_recv_cmd(u16 channel, u16 command, struct aud_smsg *msg,
			u32 timeout);
static struct aud_mem_block *aud_ipc_block_param_get(u32 idx);

static unsigned long aud_cmd_copy_back(void *dst, u32 size)
{
	unsigned long left = 0;

	if (!g_aud_ipc.param_virt)
		printk(KERN_ERR "%s param_addr_v is NULLL\n",
		       "audio_cmd_copy_back");
	if (g_aud_ipc.param_size < size) {
		printk(KERN_ERR "%s, data-size(%zu) out of range(%d)!\n",
		       "audio_cmd_copy_back", (size_t)size, g_aud_ipc.param_size);
		return size;
	}

	memcpy(dst, g_aud_ipc.param_virt, size);
	return left;
}

/*
 * Copy @size bytes from @src into the parameter buffer.  The binary folds this
 * loop into every caller, it is kept as one helper here.
 */
static int aud_cmd_copy(const void *src, u32 size)
{
	if (!g_aud_ipc.param_virt) {
		printk(KERN_ERR "%s param_addr_v is NULLL\n",
		       "audio_cmd_copy");
		return -1;
	}
	if (g_aud_ipc.param_size < size) {
		printk(KERN_ERR "%s, data-size(%zu) out of range(%d)!\n",
		       "audio_cmd_copy", (size_t)size, g_aud_ipc.param_size);
		return -1;
	}

	memcpy(g_aud_ipc.param_virt, src, size);
	printk(KERN_INFO "%s, write cmd para: txbuf_addr_v=0x%lx, tx_len=%zu\n",
	       "audio_cmd_copy", (unsigned long)g_aud_ipc.param_virt,
	       (size_t)size);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* command layer, used by the other audio modules                           */
/* ------------------------------------------------------------------------ */

static struct aud_mem_block *aud_ipc_block_param_get(u32 idx)
{
	static const u32 type_map[SIPC_NOTIFY_NUM] = { 0, 1, 2, 4, 3 };
	u32 type;
	int i;

	if (idx > 4) {
		printk(KERN_ERR "%s invalid type %d\n",
		       "aud_ipc_block_param_get", idx);
		return NULL;
	}

	type = type_map[idx];

	for (i = 0; i < SIPC_SHAREMEM_NUM; i++) {
		struct aud_mem_block *blk = &g_aud_ipc.sharemem[i];

		if (blk->type == type && blk->size)
			return blk;
	}

	return NULL;
}

int aud_send_cmd_no_param(u16 channel, u16 command, u32 parameter0,
			  u32 parameter1, u32 parameter2, u32 parameter3,
			  u32 timeout)
{
	struct aud_smsg msg;
	u32 repeat = 0;
	int ret;

	memset(&msg, 0, sizeof(msg));

	do {
		ret = aud_recv_cmd(channel, command, &msg, 0);
	} while (ret == 0);

	printk(KERN_INFO "%s,cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_send_msg", channel, command, parameter0);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x\n",
	       parameter1, parameter2);

	memset(&msg, 0, sizeof(msg));
	msg.channel = channel;
	msg.command = command;
	msg.parameter[0] = parameter0;
	msg.parameter[1] = parameter1;
	msg.parameter[2] = parameter2;
	msg.parameter[3] = parameter3;

	ret = aud_smsg_send(SIPC_DST_DSP, &msg);
	if (ret) {
		printk(KERN_ERR "%s, Failed to send cmd(0x%x), ret(%d)\n",
		       "aud_send_msg", command, ret);
		printk(KERN_ERR "%s: failed to channel=%d send command(%d), ret=%d\n",
		       "aud_send_cmd_no_param", channel, command, -EIO);
		return -1;
	}

	for (repeat = 0; repeat < SIPC_CMD_RETRY + 1; repeat++) {
		ret = aud_recv_cmd(channel, command, &msg, timeout);
		printk(KERN_WARNING "%s channel=%d, cmd=%d repeat_count=%d\n",
		       "aud_send_cmd_no_param", channel, command, repeat + 1);
		if (ret >= 0)
			break;
	}

	if (ret < 0) {
		printk(KERN_ERR "%s: failed to get channel= %d command(%d), ret=%d\n",
		       "aud_send_cmd_no_param", channel, command, ret);
		return -1;
	}

	printk(KERN_INFO "%s out,channel = %d cmd =%d ret-value:%d,repeat_count=%d\n",
	       "aud_send_cmd_no_param", channel, command, msg.parameter[3],
	       repeat + 1);

	return 0;
}
EXPORT_SYMBOL(aud_send_cmd_no_param);

int aud_send_use_noreplychan(u16 channel, u16 command, u32 parameter0,
			     u32 parameter1, u32 parameter2)
{
	return aud_send_cmd_no_param(7, channel, command, parameter0,
				     parameter1, parameter2);
}
EXPORT_SYMBOL(aud_send_use_noreplychan);

int aud_send_cmd_no_wait(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 parameter2, u32 parameter3)
{
	struct aud_smsg msg;
	int ret;

	printk(KERN_INFO "%s no wait\n", "aud_send_cmd_no_wait");
	printk(KERN_INFO "%s,cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_send_msg", command, parameter0, parameter1);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x\n",
	       parameter2, parameter3);

	memset(&msg, 0, sizeof(msg));
	msg.channel = channel;
	msg.command = command;
	msg.parameter[0] = parameter0;
	msg.parameter[1] = parameter1;
	msg.parameter[2] = parameter2;
	msg.parameter[3] = parameter3;

	ret = aud_smsg_send(SIPC_DST_DSP, &msg);
	if (ret) {
		printk(KERN_ERR "%s, Failed to send cmd(0x%x), ret(%d)\n",
		       "aud_send_msg", command, ret);
		printk(KERN_ERR "%s: channel=%d failed to send command(%d), ret=%d\n",
		       "aud_send_cmd_no_wait", channel, command, -EIO);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL(aud_send_cmd_no_wait);

static int aud_recv_cmd(u16 channel, u16 command, struct aud_smsg *msg,
			u32 timeout)
{
	struct aud_smsg recv;
	int ret;

	memset(&recv, 0, sizeof(recv));
	recv.channel = channel;
	recv.command = command;

	ret = aud_smsg_recv(SIPC_DST_DSP, &recv, timeout);
	if (ret < 0) {
		if (ret == -ENODATA) {
			printk(KERN_WARNING "%s channel =%u cmd=%u ENODATA\n",
			       "aud_recv_cmd", channel, command);
			return -ENODATA;
		}
		printk(KERN_ERR "%s, Failed to recv channel =%d cmd(0x%x),ret(%d)\n",
		       "aud_recv_cmd", channel, command, ret);
		return -EIO;
	}

	printk(KERN_INFO "%s, chan: 0x%x, cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_recv_cmd", recv.channel, recv.command, recv.parameter[0],
	       recv.parameter[1]);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x, timeout: %d\n",
	       recv.parameter[2], recv.parameter[3], timeout);

	if (recv.command != command || !msg || recv.channel != channel) {
		printk(KERN_ERR "%s, Haven't got right cmd(0x%x), got cmd(0x%x)\n",
		       "aud_recv_cmd", command, recv.command);
		printk(KERN_ERR " got chan(0x%x)\n", recv.channel);
		return -EIO;
	}

	*msg = recv;
	return 0;
}

int aud_send_cmd_result(u16 channel, u16 command, u32 parameter0,
			u32 parameter1, void *src, u32 size, void *dst)
{
	struct aud_smsg msg;
	u32 repeat = 0;
	int ret;

	mutex_lock(&g_aud_ipc.lock);

	ret = aud_cmd_copy(src, size);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to write command(%d) para, ret=%d\n",
		       "aud_send_cmd_result", parameter1, -1);
		ret = -1;
		goto unlock;
	}

	memset(&msg, 0, sizeof(msg));
	do {
		ret = aud_recv_cmd(channel, parameter1, &msg, 0);
	} while (ret == 0);

	printk(KERN_INFO "%s,cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_send_msg", command, parameter0, g_aud_ipc.param_handle);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x\n",
	       g_aud_ipc.param_handle, 0);

	memset(&msg, 0, sizeof(msg));
	msg.channel = channel;
	msg.command = command;
	msg.parameter[0] = parameter0;
	msg.parameter[1] = parameter1;
	msg.parameter[2] = g_aud_ipc.param_handle;
	msg.parameter[3] = 0;

	ret = aud_smsg_send(SIPC_DST_DSP, &msg);
	if (ret) {
		printk(KERN_ERR "%s, Failed to send cmd(0x%x), ret(%d)\n",
		       "aud_send_msg", parameter1, ret);
		printk(KERN_ERR "%s: failed to send command(%d), ret=%d\n",
		       "aud_send_cmd_result", parameter1, -EIO);
		ret = -EIO;
		goto unlock;
	}

	for (repeat = 0; repeat < SIPC_CMD_RETRY; repeat++) {
		ret = aud_recv_cmd(channel, parameter1, &msg, SIPC_CMD_TIMEOUT);
		if (ret >= 0)
			break;
	}

	if (ret < 0) {
		printk(KERN_ERR "%s: failed to get command(%d), ret=%d\n",
		       "aud_send_cmd_result", parameter1, ret);
		goto unlock;
	}

	if (!dst) {
		printk(KERN_INFO "%s out,cmd =%d id:%d ret-value:%d,repeat_count=%d\n",
		       "aud_send_cmd_result", parameter1, msg.parameter[3], ret,
		       repeat + 1);
		ret = 0;
		goto unlock;
	}

	if (aud_cmd_copy_back(dst, size)) {
		printk(KERN_ERR "%s: failed to copy back (%d) para, ret=%d\n",
		       "aud_send_cmd_result", parameter1, -EINVAL);
		ret = -EINVAL;
		goto unlock;
	}

	printk(KERN_INFO "%s out,cmd =%d id:%d ret-value:%d,repeat_count=%d\n",
	       "aud_send_cmd_result", parameter1, msg.parameter[3], ret,
	       repeat + 1);
	ret = 0;

unlock:
	mutex_unlock(&g_aud_ipc.lock);
	return ret;
}
EXPORT_SYMBOL(aud_send_cmd_result);

int aud_send_cmd_no_wait_param(u16 channel, u16 command, u32 parameter0,
			       u32 parameter1, void *src, u32 size)
{
	struct aud_smsg msg;
	int ret;

	mutex_lock(&g_aud_ipc.lock);

	ret = aud_cmd_copy(src, size);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to write command(%d) para, ret=%d\n",
		       "aud_send_cmd_no_wait_param", parameter1, -1);
		ret = -1;
		goto unlock;
	}

	printk(KERN_INFO "%s,cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_send_msg", parameter1, command, parameter0);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x\n",
	       g_aud_ipc.param_handle, 0);

	memset(&msg, 0, sizeof(msg));
	msg.channel = channel;
	msg.command = parameter1;
	msg.parameter[0] = command;
	msg.parameter[1] = parameter0;
	msg.parameter[2] = g_aud_ipc.param_handle;
	msg.parameter[3] = 0;

	ret = aud_smsg_send(SIPC_DST_DSP, &msg);
	if (ret) {
		printk(KERN_ERR "%s, Failed to send cmd(0x%x), ret(%d)\n",
		       "aud_send_msg", parameter1, ret);
		printk(KERN_ERR "%s: channel=%d failed to send command(%d), ret=%d\n",
		       "aud_send_cmd_no_wait_param", channel, parameter1, -EIO);
		ret = -EIO;
		goto unlock;
	}

	printk(KERN_INFO "%s %d wait dsp\n",
	       "aud_send_cmd_no_wait_param", 0x2e6);
	ret = 0;

unlock:
	mutex_unlock(&g_aud_ipc.lock);
	return ret;
}
EXPORT_SYMBOL(aud_send_cmd_no_wait_param);

int aud_send_cmd(u16 channel, u16 command, u32 parameter0, u32 parameter1,
		 void *src, u32 size)
{
	struct aud_smsg msg;
	u32 repeat = 0;
	int ret;

	mutex_lock(&g_aud_ipc.lock);

	ret = aud_cmd_copy(src, size);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to write command(%d) para, ret=%d\n",
		       "aud_send_cmd", parameter1, -1);
		ret = -1;
		goto unlock;
	}

	memset(&msg, 0, sizeof(msg));
	do {
		ret = aud_recv_cmd(channel, command, &msg, 0);
	} while (ret == 0);

	printk(KERN_INFO "%s in,cmd =%d id:%d ret-value:%d\n",
	       "aud_send_cmd", parameter1, command, msg.parameter[3]);
	printk(KERN_INFO "%s,cmd: 0x%x, value0: 0x%x, value1: 0x%x,\n",
	       "aud_send_msg", parameter1, command, parameter0);
	printk(KERN_INFO " value2: 0x%x, value3: 0x%x\n",
	       g_aud_ipc.param_handle, 0);

	memset(&msg, 0, sizeof(msg));
	msg.channel = channel;
	msg.command = parameter1;
	msg.parameter[0] = command;
	msg.parameter[1] = parameter0;
	msg.parameter[2] = g_aud_ipc.param_handle;
	msg.parameter[3] = 0;

	ret = aud_smsg_send(SIPC_DST_DSP, &msg);
	if (ret) {
		printk(KERN_ERR "%s, Failed to send cmd(0x%x), ret(%d)\n",
		       "aud_send_msg", command, ret);
		printk(KERN_ERR "%s: failed to send command(%d), ret=%d\n",
		       "aud_send_cmd", command, -EIO);
		ret = -EIO;
		goto unlock;
	}

	printk(KERN_INFO "%s %d wait dsp\n", "aud_send_cmd", 0x2b6);

	for (repeat = 0; repeat < SIPC_CMD_RETRY; repeat++) {
		ret = aud_recv_cmd(channel, command, &msg, SIPC_CMD_TIMEOUT);
		if (ret >= 0)
			break;
	}

	if (ret < 0) {
		printk(KERN_ERR "%s: failed to get command(%d), ret=%d\n",
		       "aud_send_cmd", command, ret);
		goto unlock;
	}

	mutex_unlock(&g_aud_ipc.lock);
	printk(KERN_INFO "%s out,cmd =%d id:%d ret-value:%d,repeat_count=%d\n",
	       "aud_send_cmd", parameter1, command, msg.parameter[3],
	       repeat + 1);
	return 0;

unlock:
	mutex_unlock(&g_aud_ipc.lock);
	return ret;
}
EXPORT_SYMBOL(aud_send_cmd);

int aud_send_block_param(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 idx, void *src, u32 size)
{
	struct aud_mem_block *blk;
	struct {
		u32 command;
		u32 type;
		u32 handle;
		u32 size;
	} param;
	int ret;

	mutex_lock(&g_aud_ipc.lock_block_param);

	blk = aud_ipc_block_param_get(idx);
	if (!blk) {
		mutex_unlock(&g_aud_ipc.lock_block_param);
		return -1;
	}

	if (blk->size < size) {
		printk(KERN_ERR "%s block_param->size=%#x < datasize=%#zx\n",
		       "aud_send_block_param", blk->size, (size_t)size);
		mutex_unlock(&g_aud_ipc.lock_block_param);
		return -1;
	}

	memcpy(blk->virt, src, size);

	memset(&param, 0, sizeof(param));
	param.command = command;
	param.type = idx;
	param.handle = blk->handle;
	param.size = size;

	printk(KERN_INFO "cmd =%d sharemem_info.id = %d, sharemem_info.type=%d,\n",
	       parameter1, command, idx);
	printk(KERN_INFO " sharemem_info.phy_iram_addr=%#x, sharemem_info.size=%#x\n",
	       blk->handle, size);

	ret = aud_send_cmd(channel, command, parameter0, parameter1, &param,
			   sizeof(param));

	mutex_unlock(&g_aud_ipc.lock_block_param);

	if (ret < 0) {
		printk(KERN_ERR "%s: failed to send command(%d), ret=%d\n",
		       "aud_send_block_param", parameter1, ret);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(aud_send_block_param);

int aud_recv_block_param(u16 channel, u16 command, u32 parameter0,
			 u32 parameter1, u32 idx, void *dst, u32 size)
{
	struct aud_mem_block *blk;
	struct {
		u32 command;
		u32 type;
		u32 handle;
		u32 size;
	} param;
	int ret;

	blk = aud_ipc_block_param_get(idx);
	if (!blk)
		return -1;

	if (blk->size > size)
		return -1;

	memset(&param, 0, sizeof(param));
	param.command = command;
	param.type = idx;
	param.handle = blk->handle;
	param.size = blk->size;

	ret = aud_send_cmd(channel, command, parameter0, parameter1, &param,
			   sizeof(param));
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to send command(%d), ret=%d\n",
		       "aud_recv_block_param", parameter1, ret);
		return -1;
	}

	memcpy(dst, blk->virt, blk->size);
	return 0;
}
EXPORT_SYMBOL(aud_recv_block_param);

int aud_ipc_dump(void *buf, u32 size)
{
	u32 off;
	int i;

	if (g_aud_ipc.smsg_mem.virt && g_aud_ipc.smsg_mem.size)
		memcpy(buf, g_aud_ipc.smsg_mem.virt, g_aud_ipc.smsg_mem.size);

	*(u64 *)((u8 *)buf + g_aud_ipc.smsg_mem.size) = 0x5a5a5a5a5a5a5a5aULL;
	off = g_aud_ipc.smsg_mem.size + 0x10;

	memcpy((u8 *)buf + off, g_aud_ipc.param_virt, g_aud_ipc.param_size);
	*(u64 *)((u8 *)buf + off + g_aud_ipc.param_size) =
		0x5b5b5b5b5b5b5b5bULL;
	off += g_aud_ipc.param_size + 0x10;

	for (i = 0; i < SIPC_SHAREMEM_NUM; i++) {
		struct aud_mem_block *blk = &g_aud_ipc.sharemem[i];

		if (!blk->size)
			continue;
		if (off + blk->size >= size)
			continue;
		memcpy((u8 *)buf + off, blk->virt, blk->size);
		off += blk->size;
	}

	if (off + 0x10 < size) {
		*(u64 *)((u8 *)buf + off) = 0x5c5c5c5c5c5c5c5cULL;
		off += 0x10;
	}

	return off;
}
EXPORT_SYMBOL(aud_ipc_dump);

int aud_ipc_ch_open(u16 channel)
{
	int ret;

	ret = aud_smsg_ch_open(SIPC_DST_DSP, channel);
	if (!ret)
		return 0;

	printk(KERN_ERR "%s, Failed to open channel:ret:%d\n",
	       "aud_ipc_ch_open", ret);
	return ret;
}
EXPORT_SYMBOL(aud_ipc_ch_open);

int aud_ipc_ch_close(u16 channel)
{
	int ret;

	ret = aud_smsg_ch_close(SIPC_DST_DSP, channel);
	if (!ret)
		return 0;

	printk(KERN_ERR "%s, Failed to close channel\n",
	       "aud_ipc_ch_close");
	return -1;
}
EXPORT_SYMBOL(aud_ipc_ch_close);

int aud_get_aud_ipc_smsg_para_addr(void **handle, void **virt, u32 *size)
{
	*handle = (void *)(unsigned long)g_aud_ipc.param_handle;
	*virt = g_aud_ipc.param_virt;
	*size = g_aud_ipc.param_size;

	if (!*virt || !*size || !*handle)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(aud_get_aud_ipc_smsg_para_addr);

int aud_get_aud_ipc_smsg_addr(void **handle, void **virt, u32 *size)
{
	*handle = g_aud_ipc.smsg_mem.handle;
	*virt = g_aud_ipc.smsg_mem.virt;
	*size = g_aud_ipc.smsg_mem.size;

	if (!*virt || !*size || !*handle)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(aud_get_aud_ipc_smsg_addr);

int snd_vbc_sipc_deinit(void)
{
	int ret;

	ret = aud_smsg_ch_close(SIPC_DST_DSP, 0);
	if (ret)
		printk(KERN_ERR "%s, Failed to close channel\n",
		       "aud_ipc_ch_close");

	g_ipc = NULL;
	return ret;
}
EXPORT_SYMBOL(snd_vbc_sipc_deinit);

/* ------------------------------------------------------------------------ */
/* platform driver                                                          */
/* ------------------------------------------------------------------------ */

static int aud_smsg_syscore_suspend(void)
{
	return 0;
}

static struct syscore_ops sipc_syscore_ops = {
	.suspend = aud_smsg_syscore_suspend,
};

static int aud_smsg_suspend_init(void)
{
	register_syscore_ops(&sipc_syscore_ops);
	return 0;
}

static int audio_sipc_rx_callback(struct mbox_client *client, void *data)
{
	return aud_smsg_irq_handler(data, g_ipc);
}

/*
 * audio_sipc_create() is inlined into the probe by the vendor compiler, it is
 * kept inline here as well.
 */
static int audio_sipc_probe(struct platform_device *pdev)
{
	struct g_smsg_ipc *ipc = &g_smsg_ipc;
	struct mbox_client *client = &g_aud_ipc.client;
	u32 size = 0, half, num;
	u32 handle, h6, sz6, i;
	void *base, *rx, *end;

	printk(KERN_INFO "%s in\n", "audio_sipc_probe");
	printk(KERN_INFO "%s\n", "aud_ipc_init");

	mutex_init(&g_aud_ipc.lock_block_param);
	mutex_init(&g_aud_ipc.lock);

	handle = audio_mem_alloc(SIPC_SMSG_MEM, &size);
	if (!handle) {
		printk(KERN_ERR "%s: failed to create sipc, ret=%d\n",
		       "aud_ipc_init", -1);
		goto mbox;
	}

	base = audio_mem_vmap(handle, size, 0);
	if (!base) {
		printk(KERN_ERR "%s:ioremap txbuf_addr return NULL\n",
		       "audio_sipc_create");
		goto mbox;
	}

	memset_io(base, 0, size);

	half = (size - SIPC_SMEM_TAIL) / 2;
	num = (size - SIPC_SMEM_TAIL) / (2 * SIPC_MSG_SIZE);
	rx = base + half;
	end = rx + half;

	printk(KERN_INFO "%s: ioremap txbuf: vbase=%#zx, pbase=0x%x, size=0x%x\n",
	       "audio_sipc_create", (size_t)base, handle, half);

	ipc->name = "ap-agdsp-ipc";
	ipc->dst = SIPC_DST_DSP;
	ipc->tx_buf = base;
	ipc->tx_num = num;
	ipc->tx_rdptr_p = end;
	ipc->tx_wrptr_p = end + 1;
	ipc->rx_buf = rx;
	ipc->rx_num = num;
	ipc->rx_rdptr_p = end + 2;
	ipc->rx_wrptr_p = end + 3;
	ipc->reserved = 0;
	ipc->smem_dsp_base = handle;
	ipc->tx_rd_dsp = handle + 2 * half;
	ipc->tx_wr_dsp = handle + 2 * half + 4;
	ipc->rx_buf_dsp = handle + half;
	ipc->rx_rd_dsp = handle + 2 * half + 8;
	ipc->rx_wr_dsp = handle + 2 * half + 0xc;

	aud_smsg_ipc_create(SIPC_DST_DSP, ipc);
	printk(KERN_ERR "%s successfully\n", "audio_sipc_create");

	h6 = audio_mem_alloc(SIPC_CMD_PARAM_MEM, &sz6);
	if (!h6) {
		printk(KERN_ERR "%s %d aud_ipc_command_param_init failed\n",
		       "aud_ipc_init", 0x1d7);
		g_ipc = NULL;
		goto mbox;
	}

	g_aud_ipc.param_virt = audio_mem_vmap(h6, sz6, 0);
	if (!g_aud_ipc.param_virt)
		printk(KERN_ERR "%s ioremap_nocache failed\n",
		       "aud_ipc_command_param_init");

	g_aud_ipc.param_handle = h6;
	g_aud_ipc.param_size = sz6;
	printk(KERN_INFO " sharemem_info.phy_iram_addr=%#x, sharemem_info.size=%#x\n",
	       h6, sz6);

	for (i = 0; i < SIPC_NOTIFY_NUM; i++) {
		u32 hsz = 0;
		void *virt;

		h6 = audio_mem_alloc(i, &hsz);
		if (!h6)
			continue;

		virt = audio_mem_vmap(h6, hsz, 0);
		memset_io(virt, 0, hsz);

		g_aud_ipc.sharemem[i].type = i;
		g_aud_ipc.sharemem[i].handle = h6;
		g_aud_ipc.sharemem[i].virt = virt;
		g_aud_ipc.sharemem[i].size = hsz;

		printk(KERN_INFO "cmd =%d sharemem_info.id = %d, sharemem_info.type=%d,\n",
		       i, h6, i);
	}

mbox:
	g_ipc = g_smsg[SIPC_DST_DSP];

	memset(client, 0, sizeof(*client));
	client->dev = &pdev->dev;
	client->tx_tout = SIPC_MBOX_TIMEOUT;
	client->rx_callback = audio_sipc_rx_callback;

	g_aud_ipc.chan = mbox_request_channel(client, 0);
	if (IS_ERR_OR_NULL(g_aud_ipc.chan)) {
		dev_err(&pdev->dev, "mailbox request chan fail !!!\n");
		return (int)(unsigned long)g_aud_ipc.chan;
	}

	printk(KERN_INFO "mailbox request success\n");
	g_mbox_chan = g_aud_ipc.chan;
	agdsp_set_mboxchan(g_aud_ipc.chan);

	aud_smsg_suspend_init();

	return 0;
}

static int audio_sipc_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id audio_sipc_of_match[] = {
	{ .compatible = "unisoc,audio_sipc" },
	{ }
};

static struct platform_driver audio_sipc_driver = {
	.probe = audio_sipc_probe,
	.remove = audio_sipc_remove,
	.driver = {
		.name = "audio_sipc",
		.of_match_table = audio_sipc_of_match,
	},
};

static int __init audio_sipc_init(void)
{
	return platform_driver_register(&audio_sipc_driver);
}
module_init(audio_sipc_init);

static void __exit audio_sipc_exit(void)
{
	platform_driver_unregister(&audio_sipc_driver);
}
module_exit(audio_sipc_exit);

MODULE_AUTHOR("Chen Gaopeng");
MODULE_DESCRIPTION("audio_sipc");
MODULE_DESCRIPTION("Audio SIPC/SMSG driver");
MODULE_LICENSE("GPL");
