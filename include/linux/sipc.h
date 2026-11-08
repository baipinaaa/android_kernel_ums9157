/* SPDX-License-Identifier: GPL-2.0-or-later */


#ifndef __SIPC_H
#define __SIPC_H

#include <linux/poll.h>





enum {
	SIPC_ID_AP = 0,		
	SIPC_ID_MINIAP,		
	SIPC_ID_CPW,		
	SIPC_ID_WCN,		
	SIPC_ID_GNSS,		
	SIPC_ID_PSCP,		
	SIPC_ID_PM_SYS,		
	SIPC_ID_NR_PHY,		
	SIPC_ID_V3_PHY,		
	SIPC_ID_CH,             
	SIPC_ID_NR,		
};

#define SIPC_ID_LTE		SIPC_ID_PSCP

#define MAX_OBJ_NAME_LEN		32

struct smsg {
	u8		channel;	
	u8		type;		
	u16		flag;		
	u32		value;		
};


enum {
	SMSG_CH_CTRL = 0,	
	SMSG_CH_COMM,		
	SMSG_CH_IMSBR_DATA,	
	SMSG_CH_IMSBR_CTRL,	
	SMSG_CH_PIPE,		
	SMSG_CH_PLOG,		
	SMSG_CH_TTY,		
	SMSG_CH_DATA0,		
	SMSG_CH_DATA1,		
	SMSG_CH_DATA2,		
	SMSG_CH_VBC,		
	SMSG_CH_PLAYBACK,	
	SMSG_CH_CAPTURE,	
	SMSG_CH_MONITOR_AUDIO,	
	SMSG_CH_CTRL_VOIP,	
	SMSG_CH_PLAYBACK_VOIP,	
	SMSG_CH_CAPTURE_VOIP,	
	SMSG_CH_MONITOR_VOIP,	
	SMSG_CH_DATA3,		
	SMSG_CH_DATA4,		
	SMSG_CH_DATA5,		
	SMSG_CH_DIAG,		
	SMSG_CH_PM_CTRL,	
	SMSG_CH_DUAL_SIM_PLUG,	
	SMSG_CH_END			
};


enum {
	
	SMSG_CH_DATA_BASE = 24,
	SMSG_CH_DATA6 = SMSG_CH_DATA_BASE,
	SMSG_CH_DATA7,
	SMSG_CH_DATA8,
	SMSG_CH_DATA9,
	SMSG_CH_DATA10,
	SMSG_CH_DATA11,
	SMSG_CH_DATA12,
	SMSG_CH_DATA13,

	
	SMSG_CH_PIPE_BASE = 40,
	SMSG_CH_NV = SMSG_CH_PIPE_BASE,
	SMSG_CH_DVFS,
	SMSG_CH_PIPE2,
	SMSG_CH_PIPE3,

	
	SMSG_CH_PLOG_BASE = 60,
	SMSG_CH_PLOG0 = SMSG_CH_PLOG_BASE,
	SMSG_CH_PLOG1,
	SMSG_CH_PLOG2,
	SMSG_CH_PLOG3,
	SMSG_CH_LOG_LOOP = 66,

	
	SMSG_CH_TTY_BASE  = 80,
	SMSG_CH_TTY0 = SMSG_CH_TTY_BASE,
	SMSG_CH_TTY1,
	SMSG_CH_TTY2,
	SMSG_CH_TTY3,
	SMSG_CH_TTY_SS = 88,

	
	SMSG_CH_CTRL_BASE = 100,
	SMSG_CH_PMSYS_DBG = SMSG_CH_CTRL_BASE,
	SMSG_CH_CTRL1,
	SMSG_CH_CTRL2,
	SMSG_CH_CTRL3,

	
	SMSG_CH_COMM_BASE = 120,
	SMSG_CH_COMM_SIPA = SMSG_CH_COMM_BASE,
	SMSG_CH_COMM1,
	SMSG_CH_COMM2,
	SMSG_CH_COMM3,

	
	SMSG_CH_AUDIO_BASE = 130,
	SMSG_CH_AGDSP_ACCESS = SMSG_CH_AUDIO_BASE,
	SMSG_CH_PLAYBACK_DEEP,
	SMSG_CH_PLAYBACK_CALLSCREEN,
	SMSG_CH_AUDIO2,
	SMSG_CH_AUDIO3,

	
	SMSG_CH_VOIP_BASE =  150,
	SMSG_CH_VOIP0 = SMSG_CH_VOIP_BASE,
	SMSG_CH_VOIP_DEEP,	
	SMSG_CH_VOIP2,	
	SMSG_CH_VOIP3,	

	
	SMSG_CH_RPC_BASE  = 170,
	SMSG_CH_RPC0 = SMSG_CH_RPC_BASE,
	SMSG_CH_RPC1,
	SMSG_CH_RPC2,
	SMSG_CH_RPC3,

	
	SMSG_CH_RESERVE1_BASE =  190,

	
	SMSG_CH_RESERVE2_BASE =  210,

	
	SMSG_CH_RESERVE3_BASE =  230,

	
	SMSG_CH_RESERVE4_BASE =  245,

	
	SMSG_CH_NR = 255
};
#define INVALID_CHANEL_INDEX SMSG_CH_NR


enum {
	SOC_MODEM = 0,
	PCIE_MODEM,
};


struct sipc_config {
	u8 channel;
	char *name;
};

static const struct sipc_config sipc_cfg[] = {
	{SMSG_CH_CTRL, "com control"}, 
	{SMSG_CH_COMM, "com communication"}, 
	{SMSG_CH_PM_CTRL, "pm contrl"}, 
	{SMSG_CH_PMSYS_DBG, "pm debug contrl"}, 
	{SMSG_CH_DUAL_SIM_PLUG, "dual sim plug"}, 
	{SMSG_CH_PIPE, "pipe0"}, 
	{SMSG_CH_PLOG, "plog"}, 
	{SMSG_CH_DIAG, "diag"}, 
	{SMSG_CH_TTY, "stty chanel"}, 
	{SMSG_CH_DATA0, "seth0"}, 
	{SMSG_CH_DATA1, "seth1"}, 
	{SMSG_CH_DATA2, "seth2"}, 
	{SMSG_CH_DATA3, "seth3"}, 
	{SMSG_CH_DATA4, "seth4"}, 
	{SMSG_CH_DATA5, "seth5"}, 
	{SMSG_CH_DATA6, "seth6"}, 
	{SMSG_CH_DATA7, "seth7"}, 
	{SMSG_CH_DATA8, "seth8"}, 
	{SMSG_CH_DATA9, "seth9"}, 
	{SMSG_CH_DATA10, "seth10"}, 
	{SMSG_CH_DATA11, "seth11"}, 
	{SMSG_CH_DATA12, "seth12"}, 
	{SMSG_CH_DATA13, "seth13"}, 
	{SMSG_CH_VBC, "audio control"}, 
	{SMSG_CH_PLAYBACK, "audio playback"}, 
	{SMSG_CH_CAPTURE, "audio capture"}, 
	{SMSG_CH_MONITOR_AUDIO, "audio monitor"}, 
	{SMSG_CH_AGDSP_ACCESS, "agdsp access"}, 
	{SMSG_CH_CTRL_VOIP, "VOIP conrol"}, 
	{SMSG_CH_PLAYBACK_VOIP, "VOIP playback"}, 
	{SMSG_CH_CAPTURE_VOIP, "VOIP capture"}, 
	{SMSG_CH_MONITOR_VOIP, "VOIP monitor"}, 
	{SMSG_CH_TTY_SS, "stty ss chanel"}, 
	{SMSG_CH_PLAYBACK_DEEP, "audio playback deep"},  
	{SMSG_CH_IMSBR_DATA, "imsbr data"}, 
	{SMSG_CH_IMSBR_CTRL, "imsbr control"},  
	{SMSG_CH_VOIP_DEEP, "audio voip deep"},  
	{SMSG_CH_DVFS, "dvfs"},  
	{SMSG_CH_LOG_LOOP, "log loop"},  
	{SMSG_CH_COMM_SIPA, "sipa"},  
	{SMSG_CH_NV, "nvsync"}, 
	{SMSG_CH_PLAYBACK_CALLSCREEN, "audio mix uplink"},  
};

#define SMSG_VALID_CH_NR (sizeof(sipc_cfg)/sizeof(struct sipc_config))



enum {
	SMSG_TYPE_NONE = 0,
	SMSG_TYPE_OPEN,		
	SMSG_TYPE_CLOSE,	
	SMSG_TYPE_DATA,		
	SMSG_TYPE_EVENT,	
	SMSG_TYPE_CMD,		
	SMSG_TYPE_DONE,		
	SMSG_TYPE_SMEM_ALLOC,	
	SMSG_TYPE_SMEM_FREE,	
	SMSG_TYPE_SMEM_DONE,	
	SMSG_TYPE_FUNC_CALL,	
	SMSG_TYPE_FUNC_RETURN,	
	SMSG_TYPE_DIE,
	SMSG_TYPE_DFS,
	SMSG_TYPE_DFS_RSP,
	SMSG_TYPE_ASS_TRG,
	SMSG_TYPE_HIGH_OFFSET, 
	SMSG_TYPE_ASSERT,
	SMSG_TYPE_NR,		
};


typedef enum {
	MMAP_CACHE = 0,
	MMAP_WRITECOMBINE,
	MMAP_NONCACHE,
} E_MMAP_TYPE;

struct smsg_callback_t {
	void (*callback)(const struct smsg  *msg, void *data);
	void *data;
};


#define	SMSG_OPEN_MAGIC		0xBEEE
#define	SMSG_CLOSE_MAGIC	0xEDDD

void smsg_callback_register(u8 dst, u8 channel,
			void (*callback)(const struct smsg *msg, void *data),
			void *data);

void smsg_callback_unregister(u8 dst, u8 channel);

#if IS_ENABLED(CONFIG_SPRD_SIPC)
int senddie_callback(struct notifier_block *nb, unsigned long code, void *unused);
#endif


int sipc_get_wakeup_flag(void);


void sipc_set_wakeup_flag(void);


void sipc_clear_wakeup_flag(void);


int smsg_ch_open(u8 dst, u8 channel, int timeout);


int smsg_ch_close(u8 dst, u8 channel, int timeout);

 
int smsg_senddie(u8 dst);


int smsg_send(u8 dst, struct smsg *msg, int timeout);


int smsg_recv(u8 dst, struct smsg *msg, int timeout);


int smsg_register_notifier(int dst, void(*handler)(phys_addr_t addr,
						void *data), void *data);


u8 sipc_channel2index(u8 ch);

int smsg_ch_wake_unlock(u8 dst, u8 channel);

#if defined(CONFIG_DEBUG_FS)
void sipc_debug_putline(struct seq_file *m, char c, int n);
#endif


static inline void smsg_set(struct smsg *msg, u8 channel,
		u8 type, u16 flag, u32 value)
{
	msg->channel = channel;
	msg->type = type;
	msg->flag = flag;
	msg->value = value;
}


static inline void smsg_open_ack(u8 dst, u16 channel)
{
	struct smsg mopen;

	pr_info("%s: channel %d-%d!\n", __func__, dst, channel);
	smsg_set(&mopen, channel, SMSG_TYPE_OPEN, SMSG_OPEN_MAGIC, 0);
	smsg_send(dst, &mopen, -1);
}


static inline void smsg_close_ack(u8 dst, u16 channel)
{
	struct smsg mclose;

	pr_info("%s: channel %d-%d!\n", __func__, dst, channel);
	smsg_set(&mclose, channel, SMSG_TYPE_CLOSE, SMSG_CLOSE_MAGIC, 0);
	smsg_send(dst, &mclose, -1);
}




u32 smem_alloc_ex(u8 dst, u16 smem, u32 size);
#define smem_alloc(dst, size) smem_alloc_ex((dst), (0), (size))


void smem_free_ex(u8 dst, u16 smem, u32 addr, u32 size);
#define smem_free(dst, addr, size) \
	smem_free_ex((dst), (0), (addr), (size))


void shmem_ram_unmap_ex(u8 dst, u16 smem, const void *mem);
#define shmem_ram_unmap(dst, mem) shmem_ram_unmap_ex((dst), (0), (mem))


void *shmem_ram_vmap_nocache_ex(u8 dst, u16 smem,
				phys_addr_t start, size_t size);
#define shmem_ram_vmap_nocache(dst, start, size) \
	shmem_ram_vmap_nocache_ex((dst), (0), (start), (size))


void *shmem_ram_vmap_cache_ex(u8 dst, u16 smem, phys_addr_t start, size_t size);
#define shmem_ram_vmap_cache(dst, start, size) \
	shmem_ram_vmap_cache_ex((dst), (0), (start), (size))


void modem_ram_unmap(u32 modem_type, const void *mem);


void *modem_ram_vmap_ex(u32 modem_type, phys_addr_t start, size_t size, E_MMAP_TYPE mtype);


void *modem_ram_vmap_nocache(u32 modem_type, phys_addr_t start, size_t size);


void *modem_ram_vmap_cache(u32 modem_type, phys_addr_t start, size_t size);



void sbuf_set_no_need_wake_lock(u8 dst, u8 channel, u32 bufnum);



int sbuf_create_ex(u8 dst, u8 channel, u16 smem, u32 bufnum,
		   u32 txbufsize, u32 rxbufsize);
#define sbuf_create(dst, channel, bufnum, txbufsize, rxbufsize) \
	sbuf_create_ex((dst), (channel), 0, (bufnum), (txbufsize), (rxbufsize))


void sbuf_destroy(u8 dst, u8 channel);


int sbuf_write(u8 dst, u8 channel, u32 bufid,
		void *buf, u32 len, int timeout);


int sbuf_read(u8 dst, u8 channel, u32 bufid,
		void *buf, u32 len, int timeout);


int sbuf_poll_wait(u8 dst, u8 channel, u32 bufid,
		struct file *file, poll_table *wait);


int sbuf_status(u8 dst, u8 channel);

#define	SBUF_NOTIFY_READY	0x00
#define	SBUF_NOTIFY_READ	0x01
#define	SBUF_NOTIFY_WRITE	0x02

int sbuf_register_notifier(u8 dst, u8 channel, u32 bufid,
		void (*handler)(int event, void *data), void *data);





struct sblock {
	void	*addr;
	u32	length;
	u16	index;
	u16	offset;
};


int sblock_create(u8 dst, u8 channel,
		u32 txblocknum, u32 txblocksize,
		u32 rxblocknum, u32 rxblocksize);


int sblock_create_ex(u8 dst, u8 channel, u8 smem,
			u32 txblocknum, u32 txblocksize,
			u32 rxblocknum, u32 rxblocksize,
			void (*handler)(int event, void *data), void *data);



int sblock_pcfg_create(u8 dst, u8 channel, u8 smem, u32 tx_blk_num, u32 tx_blk_sz,
			u32 rx_blk_num, u32 rx_blk_sz);


int sblock_pcfg_open(uint8_t dest, uint8_t channel,
			void (*notifier)(int event, void *client),
			void *client);


int sblock_close(uint8_t dest, uint8_t channel);


int sblock_get_smem_cp_addr(uint8_t dest, uint8_t channel,
			uint32_t *paddr);

void sblock_destroy(u8 dst, u8 channel);

#define	SBLOCK_NOTIFY_GET	0x01
#define	SBLOCK_NOTIFY_RECV	0x02
#define	SBLOCK_NOTIFY_STATUS	0x04
#define	SBLOCK_NOTIFY_OPEN	0x08
#define	SBLOCK_NOTIFY_CLOSE	0x10
#define SBLOCK_NOTIFY_OPEN_FAILED 0x20


int sblock_register_notifier(u8 dst, u8 channel,
		void (*handler)(int event, void *data), void *data);


int sblock_get(u8 dst, u8 channel, struct sblock *blk, int timeout);


int sblock_send(u8 dst, u8 channel, struct sblock *blk);


int sblock_send_prepare(u8 dst, u8 channel, struct sblock *blk);


int sblock_send_finish(u8 dst, u8 channel);


int sblock_receive(u8 dst, u8 channel,
		struct sblock *blk, int timeout);


int sblock_receive_loop(u8 dst, u8 channel,
			struct sblock *blk);


int sblock_release(u8 dst, u8 channel, struct sblock *blk);


int sblock_release_loop(u8 dst, u8 channel, struct sblock *blk);


int sblock_get_arrived_count(u8 dst, u8 channel);


struct sblock_mgr *sblock_mgr_get_addr(u8 dst, u8 channel);


int sblock_get_free_count(u8 dst, u8 channel);

void sblock_register_slog_clean_sendlist(void (*callback)(void));

void sblock_unregister_slog_clean_sendlist(void);


void sblock_put(u8 dst, u8 channel, struct sblock *blk);


unsigned int sblock_poll_wait(u8 dst,
			u8 channel, struct file *filp, poll_table *wait);


int sblock_query(u8 dst, u8 channel);



#define SIPX_ACK_BLK_LEN                (100)


int sipx_chan_create(u8 dst, u8 channel);


int sipx_chan_destroy(u8 dst, u8 channel);


u32 sipx_get_ack_blk_len(u8 dst);


int sipx_get(u8 dst, u8 channel, struct sblock *blk, int is_ack);


int sipx_chan_register_notifier(u8 dst, u8 channel,
		void (*handler)(int event, void *data), void *data);


int sipx_send(u8 dst, u8 channel, struct sblock *blk);


int sipx_flush(u8 dst, u8 channel);


int sipx_receive(u8 dst, u8 channel, struct sblock *blk);


int sipx_release(u8 dst, u8 channel, struct sblock *blk);


int sipx_get_arrived_count(u8 dst, u8 channel);


int sipx_get_free_count(u8 dst, u8 channel);


int sipx_put(u8 dst, u8 channel, struct sblock *blk);

#define SBLOCK_CREATE(dst, channel,\
		txblocknum, txblocksize, txpoolsize, \
		rxblocknum, rxblocksize, rxpoolsize)  \
sipx_chan_create(dst, channel)


#define SBLOCK_DESTROY(dst, channel) \
	sipx_chan_destroy(dst, channel)

#define SBLOCK_GET(dst, channel, blk, ack, timeout) \
	sipx_get(dst, channel, blk, ack)

#define SBLOCK_REGISTER_NOTIFIER(dst, channel, handler, data) \
	sipx_chan_register_notifier(dst, channel, handler, data)

#define SBLOCK_SEND(dst, channel, blk) \
	sipx_send(dst, channel, blk)

#define SBLOCK_SEND_PREPARE(dst, channel, blk) \
	sipx_send(dst, channel, blk)

#define SBLOCK_SEND_FINISH(dst, channel)\
	sipx_flush(dst, channel)

#define SBLOCK_RECEIVE(dst, channel, blk, timeout) \
	sipx_receive(dst, channel, blk)

#define SBLOCK_RELEASE(dst, channel, blk) \
	sipx_release(dst, channel, blk)

#define SBLOCK_GET_ARRIVED_COUNT(dst, channel) \
	sipx_get_arrived_count(dst, channel)

#define SBLOCK_GET_FREE_COUNT(dst, channel) \
	sipx_get_free_count(dst, channel)

#define SBLOCK_PUT(dst, channel, blk) \
	sipx_put(dst, channel, blk)


static inline unsigned long sbuf_copy_from_user(void *to, const void __user *from, unsigned long n) {
	unsigned long res = n;
	might_fault();
	if (likely(check_copy_size(to, n, false))) {
		kasan_check_write(to, n);
		res = raw_copy_from_user(to, from, n);
	}
	return res;
}

#ifdef CONFIG_ARM64

static inline unsigned long unalign_copy_to_user(void __user *to,
		const void *from,
		unsigned long n)
{
	
	if (((unsigned long)from & 7) && (n < 16)) {
		while (n) {
			if (copy_to_user(to++, from++, 1))
				break;
			n--;
		}
		return n;
	}

	return copy_to_user(to, from, n);
}


static inline unsigned long unalign_copy_from_user(void *to,
		const void __user *from,
		unsigned long n)
{
	unsigned c1, c2, c3;

	
	c1 = !((unsigned long)to & 0x7) && (n < 16);
	if (c1)
		return sbuf_copy_from_user(to, from, n);

	
	c2 = !((unsigned long)to & 0x7) && !((unsigned long)from & 0x7);
	if (c2)
		return sbuf_copy_from_user(to, from, n);

	
	c3 = !(((unsigned long)to ^ (unsigned long)from) & 0x7) && (n > 15);
	if (c3)
		return sbuf_copy_from_user(to, from, n);

	while (n) {
		if (sbuf_copy_from_user(to++, from++, 1))
			break;
		n--;
	}

	return n;
}

static inline void unalign_memcpy(void *to, const void *from, size_t n)
{
	if (((unsigned long)to & 7) == ((unsigned long)from & 7)) {
		while (((unsigned long)from & 7) && n) {
			*(char *)(to++) = *(char *)(from++);
			n--;
		}
		memcpy(to, from, n);
	} else if (((unsigned long)to & 3) == ((unsigned long)from & 3)) {
		while (((unsigned long)from & 3) && n) {
			*(char *)(to++) = *(char *)(from++);
			n--;
		}
		while (n >= 4) {
			*(u32 *)(to) = *(u32 *)(from);
			to += 4;
			from += 4;
			n -= 4;
		}
		while (n) {
			*(char *)(to++) = *(char *)(from++);
			n--;
		}
	} else {
		while (n) {
			*(char *)(to++) = *(char *)(from++);
			n--;
		}
	}
}

static inline unsigned long _unalign_copy_to_user(void __user *to,
						    const void *from,
						    unsigned long n)
{
	if (((unsigned long)from & 7) && (n < 16)) {
		while (n) {
			if (_copy_to_user(to++, from++, 1))
				break;
			n--;
		}
		return n;
	}

	return _copy_to_user(to, from, n);
}

static inline unsigned long _unalign_copy_from_user(void *to,
						    const void __user *from,
						    unsigned long n)
{
	unsigned c1, c2, c3;

	c1 = !((unsigned long)to & 0x7) && !(n < 16);
	if (c1)
		return _copy_from_user(to, from, n);

	c2 = !((unsigned long)to & 0x7) && !((unsigned long)from & 0x7);
	if (c2)
		return _copy_from_user(to, from, n);

	c3 = !(((unsigned long)to ^ (unsigned long)from) & 0x7) && (n > 15);
	if (c3)
		return _copy_from_user(to, from, n);

	while (n) {
		if (_copy_from_user(to++, from++, 1))
			break;
		n--;
	}

	return n;
}
#else
static inline unsigned long unalign_copy_to_user(void __user *to,
		const void *from,
		unsigned long n)
{
	return copy_to_user(to, from, n);
}
static inline unsigned long unalign_copy_from_user(void *to,
		const void __user *from,
		unsigned long n)
{
	return sbuf_copy_from_user(to, from, n);
}
static inline void *unalign_memcpy(void *to, const void *from, size_t n)
{
	return memcpy(to, from, n);
}

static inline unsigned long _unalign_copy_to_user(void __user *to,
						    const void *from,
						    unsigned long n)
{
	return _copy_to_user(to, from, n);
}

static inline unsigned long _unalign_copy_from_user(void *to,
						    const void __user *from,
						    unsigned long n)
{
	return _copy_from_user(to, from, n);
}
#endif

#endif
