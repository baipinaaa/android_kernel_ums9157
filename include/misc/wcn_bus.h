/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __WCN_BUS_H__
#define __WCN_BUS_H__

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define SDIOHAL_PUB_HEAD_RSV	4
#define PUB_HEAD_RSV	0

#define SIPC_CHN_MAX_NUM (2 * BITS_PER_LONG)
#define SIPC_CHN_ATCMD 4
#define SIPC_CHN_LOOPCHECK 11
#define SIPC_CHN_ASSERT 12
#define SIPC_CHN_LOG 5

#define CHN_MAX_NUM 32

enum wcn_hard_intf_type {
	HW_TYPE_SDIO,
	HW_TYPE_PCIE,
	HW_TYPE_SIPC,
	HW_TYPE_INVALIED
};

enum wcn_sub_sys {
	MARLIN_BLUETOOTH = 0,
	MARLIN_FM,
	MARLIN_WIFI,
	MARLIN_WIFI_FLUSH,
	MARLIN_SDIO_TX,
	MARLIN_SDIO_RX,
	MARLIN_MDBG,
	MARLIN_GNSS,
	WCN_AUTO,	
	MARLIN_ALL,
};

enum slp_subsys {
	PACKER_TX = 0,
	PACKER_RX,
	PACKER_DT_TX,
	PACKER_DT_RX,
	DT_WRITEL,
	DT_READL,
	DT_WRITE,
	DT_READ,
	DOWNLOAD,
	DBG_TOOL,
	SUBSYS_MAX,
};

enum wcn_source_type {
	WCN_SOURCE_BTWF,
	WCN_SOURCE_GNSS,
	WCN_SOURCE_WCN,
	WCN_SOURCE_CP2_ALIVE  
};

enum wcn_bus_state {
	WCN_BUS_DOWN,	
	WCN_BUS_UP		
};

enum wcn_bus_pm_state {
	BUS_PM_DISABLE = 1,	
	BUS_PM_L0s_L1_ENABLE,	
	BUS_PM_ALL_ENABLE	
};

enum sub_sys {
	BLUETOOTH = 0,
	WIFI,
	FM,
	AUTO,
};

enum wifi_pm_qos_mode {
	WIFI_STATION,
	WIFI_AP,
	WIFI_P2P_DEVICE,
	WIFI_P2P_CLIENT,
	WIFI_P2P_GO,
	WIFI_TX_HIGH_THROUGHPUT,
	WIFI_RX_HIGH_THROUGHPUT,
	WIFI_MAX,
};

enum bluetooth_pm_qos_profile {
	BT_OPP = WIFI_MAX + 1,
	BT_A2DP,
	BT_HFP,
	BT_MAX,
};

struct mbuf_t {
	struct mbuf_t *next;
	unsigned char *buf;
	unsigned long  phy;
	unsigned short len;
	unsigned short rsvd;
	unsigned int   seq;
};

enum wcn_sipc_trans_type {
	WCN_SIPC_TRANS_SBUF,
	WCN_SIPC_TRANS_SBLOCK,
};

struct wcn_sipc_chn_sbuf {
	u8	bufid;
	u32	len;
	u32	bufnum;
	u32	txbufsize;
	u32	rxbufsize;
};

struct wcn_sipc_chn_sblk {
	u32     txblocknum;
	u32     txblocksize;
	u32     rxblocknum;
	u32     rxblocksize;
	u32     basemem;
	u32     alignsize;
	u32     mapped_smem_base;
};


#define WCN_CHN_CREATE 0x1

#define WCN_CHN_CALLBACK 0x2

#define WCN_CHN_DYNAMIC 0x4
#define WCN_CHN_REGISTER_HANDLE 0x4

struct wcn_sipc_chn_info {
	u8 dst;
	u8 channel;
	int index;
	enum wcn_sipc_trans_type type;
	
	unsigned int flag;
	unsigned char name[24];
	union {
		struct wcn_sipc_chn_sbuf sbuf;
		struct wcn_sipc_chn_sblk sblk;
	};
};

union wcn_chn_config {
	int channel;
	struct wcn_sipc_chn_info sipc_ch;
};

#define WCN_SIPC_DST	3
#define SIPC_TYPE_SBUF	0
#define SIPC_TYPE_SBLOCK	1


#define WCN_INIT_SIPC_SBUF(_dst, _channel, _flag, _name, \
			   _bufid, _len, _num, _txblocksize, _rxblocksize) \
{ \
	.dst = _dst, .channel = _channel, .type = SIPC_TYPE_SBUF, \
	.flag = _flag, .name = _name, .sbuf.bufid = _bufid, \
	.sbuf.len = _len, .sbuf.bufnum = _num, \
	.sbuf.txbufsize = _txblocksize, .sbuf.rxbufsize = _rxblocksize \
}

#define WCN_INIT_MINI_SIPC_SBUF(_dst, _channel, _flag, _name, _bufid, _len) \
{ \
	.dst = _dst, .channel = _channel, .type = SIPC_TYPE_SBUF, \
	.flag = _flag, .name = _name, .sbuf.bufid = _bufid, \
	.sbuf.len = _len, \
}

#define WCN_INIT_SIPC_SBLOCK(_dst, _channel, _flag, _name, _txblocknum, \
			     _txblocksize, _rxblocknum, _rxblocksize, \
			     _basemem, _alignsize, _mapped_smem_base) \
{ \
	.dst = _dst, .channel = _channel, .type = SIPC_TYPE_SBLOCK, \
	.flag = _flag, .name = _name, .sblk.txblocknum = _txblocknum, \
	.sblk.txblocksize = _txblocksize, .sblk.rxblocknum = _rxblocknum, \
	.sblk.rxblocksize = _rxblocksize, .sblk.basemem = _basemem, \
	.sblk.alignsize = _alignsize, \
	.sblk.mapped_smem_base = _mapped_smem_base \
}

#define WCN_INIT_MINI_SIPC_SBLOCK(_dst, _channel, _flag, _name) \
{ \
	.dst = _dst, .channel = _channel, \
	.type = SIPC_TYPE_SBLOCK, \
	.flag = _flag, .name = _name, \
}

struct mchn_ops_t {
	
	enum wcn_hard_intf_type hif_type;
	
	int channel;

	
	union wcn_chn_config chn_config;

	
	int inout;
	
	int intr_interval;
	
	int buf_size;
	
	int pool_size;
	
	int once_max_trans;
	
	int rx_threshold;
	
	int timeout;
	
	int cb_in_irq;
	
	int max_pending;
	
	int (*pop_link)(int chn, struct mbuf_t *head,
			struct mbuf_t *tail, int num);
	
	int (*push_link)(int chn, struct mbuf_t **head,
			 struct mbuf_t **tail, int *num);
	
	int (*tx_complete)(int chn, int timeout);
	int (*power_notify)(int chn, int notify);
};

struct bus_puh_t {
	unsigned int pad:6;
	unsigned int check_sum:1;
	unsigned int len:16;
	unsigned int eof:1;
	unsigned int subtype:4;
	unsigned int type:4;
}; 

struct sprdwcn_bus_ops {
	int (*preinit)(void);
	void (*deinit)(void);

	int (*chn_init)(struct mchn_ops_t *ops);
	int (*chn_deinit)(struct mchn_ops_t *ops);

	enum wcn_hard_intf_type (*get_hwintf_type)(void);
	int (*get_bus_status)(void);

	
	int (*list_alloc)(int chn, struct mbuf_t **head,
			  struct mbuf_t **tail, int *num);
	int (*list_free)(int chn, struct mbuf_t *head,
			 struct mbuf_t *tail, int num);

	
	int (*push_list)(int chn, struct mbuf_t *head,
			 struct mbuf_t *tail, int num);
	int (*push_list_direct)(int chn, struct mbuf_t *head,
			 struct mbuf_t *tail, int num);

	
	int (*push_link_wait_complete)(int chn, struct mbuf_t *head,
				       struct mbuf_t *tail, int num,
				       int timeout);
	int (*hw_pop_link)(int chn, void *head, void *tail, int num);
	int (*hw_tx_complete)(int chn, int timeout);
	int (*hw_req_push_link)(int chn, int need);

	int (*direct_read)(unsigned int addr, void *buf, unsigned int len);
	int (*direct_write)(unsigned int addr, void *buf, unsigned int len);

	int (*readbyte)(unsigned int addr, unsigned char *val);
	int (*writebyte)(unsigned int addr, unsigned char val);

	int (*write_l)(unsigned int system_addr, void *buf);
	int (*read_l)(unsigned int system_addr, void *buf);
	int (*update_bits)(unsigned int reg, unsigned int mask,
			   unsigned int val);
	int (*get_pm_policy)(void);
	int (*set_pm_policy)(enum sub_sys subsys, enum wcn_bus_pm_state state);
	unsigned int (*get_carddump_status)(void);
	void (*set_carddump_status)(unsigned int flag);
	unsigned long long (*get_rx_total_cnt)(void);

	
	int (*runtime_get)(void);
	int (*runtime_put)(void);

	int (*rescan)(void *wcn_dev);
	void (*register_rescan_cb)(void *);
	void (*remove_card)(void *wcn_dev);
	void (*reset)(void *wcn_dev);

	int (*register_pt_rx_process)(unsigned int type,
				unsigned int subtype, void *func);
	int (*register_pt_tx_release)(unsigned int type,
				      unsigned int subtype, void *func);
	int (*pt_write)(void *buf, unsigned int len, unsigned int type,
			    unsigned int subtype);
	int (*pt_read_release)(unsigned int fifo_id);

	int (*driver_register)(void);
	void (*driver_unregister)(void);

	
	int (*start_wcn)(enum wcn_sub_sys subsys);
	int (*stop_wcn)(enum wcn_sub_sys subsys);
	void (*debug_point_show)(void);
	int (*pm_qos)(unsigned int mode, bool set);
};

extern struct atomic_notifier_head wcn_reset_notifier_list;
extern struct atomic_notifier_head modem_n79_notifier_list;

extern void module_bus_init(void);
extern void module_bus_deinit(void);
extern struct sprdwcn_bus_ops *get_wcn_bus_ops(void);
extern void wcn_assert_interface(enum wcn_source_type, char *str);
extern bool wcn_is_assert(void);
bool wcn_push_list_condition_check(struct mbuf_t *head, struct mbuf_t *tail, int num);
extern bool wcn_is_power_busy(void);
int sprd_wlan_power_status_sync(int option, int value);
void mdbg_device_lock_notify(void);
void mdbg_device_unlock_notify(void);
extern void wcn_pm_qos_enable(void);
extern void wcn_pm_qos_disable(void);
extern void wcn_pm_qos_reset(void);

static inline
int sprdwcn_bus_preinit(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->preinit)
		return 0;

	return bus_ops->preinit();
}

static inline
void sprdwcn_bus_deinit(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->deinit)
		return;

	bus_ops->deinit();
}

static inline
int sprdwcn_bus_get_status(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->deinit)
		return 0;

	return bus_ops->get_bus_status();
}

static inline
int sprdwcn_bus_chn_init(struct mchn_ops_t *ops)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->chn_init)
		return 0;

	return bus_ops->chn_init(ops);
}

static inline
int sprdwcn_bus_chn_deinit(struct mchn_ops_t *ops)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->chn_deinit)
		return 0;

	return bus_ops->chn_deinit(ops);
}

static inline
int sprdwcn_bus_list_alloc(int chn, struct mbuf_t **head,
			   struct mbuf_t **tail, int *num)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->list_alloc)
		return 0;

	return bus_ops->list_alloc(chn, head, tail, num);
}

static inline
int sprdwcn_bus_list_free(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->list_free)
		return 0;

	return bus_ops->list_free(chn, head, tail, num);
}

static inline
int sprdwcn_bus_push_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->push_list)
		return 0;

	return bus_ops->push_list(chn, head, tail, num);
}

static inline
int sprdwcn_bus_push_list_direct(int chn, struct mbuf_t *head,
				 struct mbuf_t *tail, int num)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->push_list_direct)
		return 0;

	return bus_ops->push_list_direct(chn, head, tail, num);
}

static inline
int sprdwcn_bus_push_link_wait_complete(int chn, struct mbuf_t *head,
					struct mbuf_t *tail, int num,
					int timeout)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->push_link_wait_complete)
		return 0;

	return bus_ops->push_link_wait_complete(chn, head,
						    tail, num, timeout);
}

static inline
int sprdwcn_bus_hw_pop_link(int chn, void *head, void *tail, int num)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->hw_pop_link)
		return 0;

	return bus_ops->hw_pop_link(chn, head, tail, num);
}

static inline
int sprdwcn_bus_hw_tx_complete(int chn, int timeout)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->hw_tx_complete)
		return 0;

	return bus_ops->hw_tx_complete(chn, timeout);
}

static inline
int sprdwcn_bus_hw_req_push_link(int chn, int need)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->hw_req_push_link)
		return 0;

	return bus_ops->hw_req_push_link(chn, need);
}

static inline
int sprdwcn_bus_direct_read(unsigned int addr,
			    void *buf, unsigned int len)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->direct_read)
		return 0;

	return bus_ops->direct_read(addr, buf, len);
}

static inline
int sprdwcn_bus_direct_write(unsigned int addr,
			     void *buf, unsigned int len)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->direct_write)
		return 0;

	return bus_ops->direct_write(addr, buf, len);
}

static inline
int sprdwcn_bus_reg_read(unsigned int addr,
			    void *buf, unsigned int len)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->read_l)
		return 0;

	return bus_ops->read_l(addr, buf);
}

static inline
int sprdwcn_bus_reg_write(unsigned int addr,
			     void *buf, unsigned int len)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->write_l)
		return 0;

	return bus_ops->write_l(addr, buf);
}

static inline
int sprdwcn_bus_update_bits(unsigned int reg, unsigned int mask,
			    unsigned int val)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->update_bits)
		return 0;

	return bus_ops->update_bits(reg, mask, val);
}

static inline
int sprdwcn_bus_aon_readb(unsigned int addr, unsigned char *val)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->readbyte)
		return 0;

	return bus_ops->readbyte(addr, val);
}

static inline
int sprdwcn_bus_aon_writeb(unsigned int addr, unsigned char val)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->writebyte)
		return 0;

	return bus_ops->writebyte(addr, val);
}

static inline
enum wcn_bus_pm_state sprdwcn_bus_get_pm_policy(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->get_pm_policy)
		return 0;

	return bus_ops->get_pm_policy();
}

static inline
int sprdwcn_bus_set_pm_policy(enum sub_sys subsys, enum wcn_bus_pm_state state)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->set_pm_policy)
		return 0;

	return bus_ops->set_pm_policy(subsys, state);
}

static inline
unsigned int sprdwcn_bus_get_carddump_status(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->get_carddump_status)
		return 0;

	return bus_ops->get_carddump_status();
}

static inline
void sprdwcn_bus_set_carddump_status(unsigned int flag)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->set_carddump_status)
		return;

	bus_ops->set_carddump_status(flag);
}

static inline
unsigned long long sprdwcn_bus_get_rx_total_cnt(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->get_rx_total_cnt)
		return 0;

	return bus_ops->get_rx_total_cnt();
}

static inline
int sprdwcn_bus_runtime_get(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->runtime_get)
		return 0;

	return bus_ops->runtime_get();
}

static inline
int sprdwcn_bus_runtime_put(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->runtime_put)
		return 0;

	return bus_ops->runtime_put();
}

static inline
int sprdwcn_bus_rescan(void *wcn_dev)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->rescan)
		return 0;

	return bus_ops->rescan(wcn_dev);
}

static inline
void sprdwcn_bus_register_rescan_cb(void *func)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->register_rescan_cb)
		return;

	bus_ops->register_rescan_cb(func);
}

static inline
void sprdwcn_bus_reset(void *wcn_dev)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->reset)
		return;

	bus_ops->reset(wcn_dev);
}

static inline
void sprdwcn_bus_remove_card(void *wcn_dev)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->remove_card)
		return;

	bus_ops->remove_card(wcn_dev);
}

static inline
enum wcn_hard_intf_type sprdwcn_bus_get_hwintf_type(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->get_hwintf_type)
		return HW_TYPE_INVALIED;

	return bus_ops->get_hwintf_type();
}

static inline
int sprdwcn_start(enum wcn_sub_sys subsys)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->start_wcn)
		return -ENODEV;

	return bus_ops->start_wcn(subsys);
}

static inline
int sprdwcn_stop(enum wcn_sub_sys subsys)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->stop_wcn)
		return -ENODEV;

	return bus_ops->stop_wcn(subsys);
}

static inline
void sprdwcn_bus_debug_point_show(void)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->debug_point_show)
		return;

	bus_ops->debug_point_show();
}

static inline
int sprdwcn_bus_pm_qos_set(unsigned int mode, bool set)
{
	struct sprdwcn_bus_ops *bus_ops = get_wcn_bus_ops();

	if (!bus_ops || !bus_ops->pm_qos)
		return 0;

	return bus_ops->pm_qos(mode, set);
}

static inline
int wcn_bus_init(void)
{
	module_bus_init();
	return 0;
}

static inline
void wcn_bus_deinit(void)
{
	module_bus_deinit();
}

#endif
