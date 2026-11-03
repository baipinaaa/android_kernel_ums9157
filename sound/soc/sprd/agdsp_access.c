// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc AGDSP access driver
 *
 * Reconstructed from the vendor module found in the Q968 (UMS9157) vendor_boot
 * ramdisk, agdsp_access.ko (modinfo: "agdsp access driver", author
 * yintang.ren <yintang.ren@spreadtrum.com>).  The vendor source file was
 *
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/
 *           audio_driver/sprd_audio/agdsp_access/agdsp_access.c
 *
 * (that path is still present verbatim in the module's .rodata).
 *
 * Before the AP touches anything inside the audio DSP (AGDSP) - its IRAM, its
 * registers or the shared VBC buffers - the DSP has to be woken up.  This
 * driver keeps a reference count in a small shared memory block that the DSP
 * itself can also read, pokes the always-on PMU to un-gate the DSP and rings
 * the DSP mailbox.  Every audio module (voice_trigger_irq, snd-soc-sprd-vbc-fe,
 * ...) brackets its register accesses with agdsp_access_enable() /
 * agdsp_access_disable().
 *
 * Binary layout notes (all offsets below are the ones seen in the .ko):
 *   priv struct        3520 bytes, kmalloc()ed once, never freed
 *   shared block       8 bytes, u32 ap_enable_cnt + u32 cp_enable_cnt
 *   mailbox ring       20 x u64 message slots, index wraps 19 -> 0
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sipc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#define AGDSP_MSG_NUM		20
#define AGDSP_WAKEUP_MSG	0x64

/* smsg destination used by every vendor audio module (DSP side) */
#define AGDSP_SIPC_DST		5

/* how long to wait for "power up done" - the loop counter the compiler built
 * out of the vendor's timeout constant (0xfff0bdc0 .. 0xffffffff)
 */
#define AGDSP_POWER_UP_RETRY	0x10440

/*
 * Device tree properties used by the driver.
 */
#define AGDSP_DT_DDR_OFFSET	"sprd,ddr-addr-offset"
#define AGDSP_DT_AUTO_ACCESS	"sprd,auto_agcp_access"
#define AGDSP_DT_AGCP_AHB	"sprd,syscon-agcp-ahb"
#define AGDSP_DT_PMU_APB	"sprd,syscon-pmu-apb"

static const char *const agdsp_syscon_names[] = {
	"audcp_pmu_sleep_ctrl",
	"audcp_pmu_slp_status",
	"audcp_pmu_pwr_status4",
	"audcp_pmu_pwr_status3",
	"ap_access_ena",
};

/*
 * The vendor returns -517 ("not ready") from agdsp_access_enable() instead of a
 * standard errno; kept to stay call-compatible with the vendor modules.
 */
#define AGDSP_ERR_NOT_READY	(-517)

/**
 * struct agdsp_access_state - the 8 bytes shared with the DSP
 * @ap_enable_cnt: increased/decreased by the AP around every access
 * @cp_enable_cnt: increased by the DSP while it is awake
 */
struct agdsp_access_state {
	u32 ap_enable_cnt;
	u32 cp_enable_cnt;
};

/**
 * struct agdsp_access_reg - register offset plus bit mask, both from DT
 */
struct agdsp_access_reg {
	u32 reg;
	u32 mask;
};

/**
 * struct agdsp_access - driver private data
 */
struct agdsp_access {
	struct agdsp_access_state *state;	/* +0x00 shared block (AP mapping) */
	u32 state_phys;				/* +0x08 shared block, DSP view */
	u32 state_size;				/* +0x0c always 8 */
	u32 ddr_offset;				/* +0x10 sprd,ddr-addr-offset */
	struct task_struct *thread;		/* +0x18 smsg server thread */
	u32 ready;				/* +0x20 thread running */
	u8 dst;					/* +0x24 smsg destination */
	u8 channel;				/* +0x25 smsg channel */
	u8 pad[2];				/* +0x26 */
	struct regmap *agcp_ahb;		/* +0x28 */
	struct regmap *pmu_apb;		/* +0x30 */
	u32 auto_access;			/* +0x38 sprd,auto_agcp_access */
	struct agdsp_access_reg sleep_ctrl;	/* +0x3c */
	struct agdsp_access_reg slp_status;	/* +0x44 */
	struct agdsp_access_reg pwr_status4;	/* +0x4c */
	struct agdsp_access_reg pwr_status3;	/* +0x54 */
	struct agdsp_access_reg ap_access_ena;	/* +0x5c */
	spinlock_t lock;			/* +0x64 */
};

static struct agdsp_access *g_agdsp_access;
static struct mbox_chan *g_agdsp_mboxchan;
static u32 msg_index;
static u64 msg_val[AGDSP_MSG_NUM];

/**
 * agdsp_set_mboxchan() - register the mailbox channel used to wake the DSP
 * @chan: mailbox channel obtained by the client driver
 *
 * The audio mailbox is owned by another module (sprd mailbox / spipe), which
 * hands the channel over here.
 */
void agdsp_set_mboxchan(struct mbox_chan *chan)
{
	g_agdsp_mboxchan = chan;
}
EXPORT_SYMBOL(agdsp_set_mboxchan);

/*
 * (value & mask) >> __ffs(mask), with the vendor's "mask == 0 -> shift -1"
 * behaviour.  The compiler emitted the rbit/clz/csinv sequence for both the
 * open coded __ffs() and the zero check, so the helper reproduces it without
 * relying on the undefined __ffs(0).
 */
static inline int agdsp_shift(u32 mask)
{
	return mask ? __ffs(mask) : -1;
}

/**
 * force_on_xtl() - placeholder kept for ABI compatibility
 *
 * The vendor module only contains "mov w0, wzr; ret", i.e. it always returns
 * success.  Some audio modules call it, so the symbol has to stay.
 */
int force_on_xtl(void)
{
	return 0;
}
EXPORT_SYMBOL(force_on_xtl);

/**
 * agdsp_access_send_wakeup() - ring the DSP mailbox once
 *
 * The message payload is the constant 0x64, which is the "AGDSP wake up"
 * command understood by the DSP firmware.  The ring exists because the
 * mailbox API keeps a pointer to the message and the vendor never wanted to
 * hand it a stack buffer; msg_index wraps at AGDSP_MSG_NUM.
 */
static void agdsp_access_send_wakeup(u64 *msg)
{
	int ret;

	ret = mbox_send_message(g_agdsp_mboxchan, msg);
	if (ret < 0)
		printk(KERN_ERR "[sprd-aud-agdsp] %s, mbox send message error! ret=%d\n",
		       __func__, ret);

	mbox_chan_txdone(g_agdsp_mboxchan, 0);
}

/**
 * agdsp_access_poll_power_up() - wait until the DSP reports "up"
 * @dsp_ac: driver data
 *
 * Returns 0 when the DSP answered, -ETIMEDOUT otherwise.  The status is the
 * audcp_pmu_pwr_status4 field, evaluated as (value & mask) >> __ffs(mask).
 */
static int agdsp_access_poll_power_up(struct agdsp_access *dsp_ac)
{
	u32 value = 0;
	u32 i;
	int ret;

	for (i = 0; i < AGDSP_POWER_UP_RETRY; i++) {
		ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status4.reg,
				  &value);
		if (ret) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read error!\n",
			       __func__);
			break;
		}

		if (((value & dsp_ac->pwr_status4.mask) >>
		     agdsp_shift(dsp_ac->pwr_status4.mask)) == 0)
			return 0;

		udelay(5);
	}

	printk(KERN_ERR "[sprd-aud-agdsp] ERR: %s wait agdsp power up timeout!\n",
	       "agdsp_access_enable");
	return -ETIMEDOUT;
}

/**
 * agdsp_access_init_thread() - smsg server answering the DSP's address query
 * @data: &struct agdsp_access
 *
 * Once the DSP firmware boots it opens the AGDSP channel and sends
 * SMSG_TYPE_CMD with flag 1; the reply carries the physical address of the
 * shared block as seen from the DSP (state_phys + ddr_offset).  The DSP uses
 * that address to read ap_enable_cnt/cp_enable_cnt.
 */
static int agdsp_access_init_thread(void *data)
{
	struct agdsp_access *dsp_ac = data;
	struct smsg mrecv;
	struct smsg msend;
	u32 ddr_offset = dsp_ac->ddr_offset;
	int ret;

	ret = smsg_ch_open(dsp_ac->dst, dsp_ac->channel, -1);
	if (ret) {
		printk(KERN_INFO "[sprd-aud-agdsp] Failed to open channel %d,dst=%d,rval=%d\n",
		       dsp_ac->channel, dsp_ac->dst, ret);
		dsp_ac->thread = NULL;
		goto out_close;
	}

	while (!kthread_should_stop()) {
		memset(&mrecv, 0, sizeof(mrecv));
		mrecv.channel = dsp_ac->channel;

		ret = smsg_recv(dsp_ac->dst, &mrecv, -1);
		if (ret == -EINTR || ret == -ETIMEDOUT) {
			msleep(20);
			continue;
		}

		switch (mrecv.type) {
		case SMSG_TYPE_OPEN:
			printk(KERN_INFO "[sprd-aud-agdsp] %s: channel %d-%d!\n",
			       "smsg_open_ack", dsp_ac->dst, dsp_ac->channel);
			memset(&msend, 0, sizeof(msend));
			msend.channel = dsp_ac->channel;
			msend.type = SMSG_TYPE_OPEN;
			msend.flag = SMSG_OPEN_MAGIC;
			msend.value = 0;
			smsg_send(dsp_ac->dst, &msend, -1);
			break;
		case SMSG_TYPE_CMD:
			if (mrecv.flag != 1)
				break;
			memset(&msend, 0, sizeof(msend));
			msend.channel = dsp_ac->channel;
			msend.type = SMSG_TYPE_DONE;
			msend.flag = 2;
			msend.value = dsp_ac->state_phys + ddr_offset;
			smsg_send(dsp_ac->dst, &msend, -1);
			break;
		default:
			break;
		}
	}

out_close:
	smsg_ch_close(dsp_ac->dst, dsp_ac->channel, -1);

	return ret;
}

/**
 * agdsp_access_initialize() - allocate state, map it and start the smsg thread
 */
static int agdsp_access_initialize(struct device_node *np,
				   struct regmap *agcp_ahb,
				   struct regmap *pmu_apb,
				   u32 auto_access, u32 ddr_offset)
{
	struct agdsp_access *dsp_ac;
	struct regmap *map;
	u32 args[2];
	int i;

	dsp_ac = kmalloc(sizeof(*dsp_ac), GFP_KERNEL);
	g_agdsp_access = dsp_ac;

	if (!agcp_ahb || !dsp_ac) {
		printk(KERN_ERR "[sprd-aud-agdsp] agdsp_access init:Failed to allocate\n");
		return -ENOMEM;
	}

	memset(dsp_ac, 0, sizeof(*dsp_ac));
	spin_lock_init(&dsp_ac->lock);
	dsp_ac->agcp_ahb = agcp_ahb;
	dsp_ac->pmu_apb = pmu_apb;
	dsp_ac->ddr_offset = ddr_offset;
	dsp_ac->auto_access = auto_access;

	/*
	 * Every "reg, mask" pair comes from its own two-cell syscon property.
	 * The regmap itself was already resolved in probe, the syscon lookup
	 * here is only used to read the two cells.
	 */
	for (i = 0; i < ARRAY_SIZE(agdsp_syscon_names); i++) {
		struct agdsp_access_reg *r;

		switch (i) {
		case 0: r = &dsp_ac->sleep_ctrl; break;
		case 1: r = &dsp_ac->slp_status; break;
		case 2: r = &dsp_ac->pwr_status4; break;
		case 3: r = &dsp_ac->pwr_status3; break;
		default: r = &dsp_ac->ap_access_ena; break;
		}

		args[0] = 0;
		args[1] = 0;
		map = syscon_regmap_lookup_by_phandle_args(np,
				agdsp_syscon_names[i], 2, args);
		if (IS_ERR(map)) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s fail.\n",
			       agdsp_syscon_names[i]);
			continue;
		}

		r->reg = args[0];
		r->mask = args[1];
		printk(KERN_INFO "[sprd-aud-agdsp] %s:reg:%x,mask:%x\n",
		       agdsp_syscon_names[i], r->reg, r->mask);
	}

	dsp_ac->state_size = sizeof(struct agdsp_access_state);
	dsp_ac->state_phys = smem_alloc_ex(AGDSP_SIPC_DST, 0,
					   dsp_ac->state_size);
	if (!dsp_ac->state_phys) {
		/* no shared memory available: keep the counters in normal RAM */
		dsp_ac->state = kmalloc(dsp_ac->state_size, GFP_KERNEL);
	} else {
		dsp_ac->state = shmem_ram_vmap_nocache_ex(AGDSP_SIPC_DST, 0,
						dsp_ac->state_phys,
						dsp_ac->state_size);
	}

	if (!dsp_ac->state) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s,shmem_ram_vmap_nocache return 0.\n",
		       "agdsp_access_initialize");
		goto err_free;
	}

	dsp_ac->state->ap_enable_cnt = 0;
	dsp_ac->state->cp_enable_cnt = 0;

	if (!dsp_ac->auto_access)
		regmap_update_bits(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				   dsp_ac->ap_access_ena.mask, 0);

	dsp_ac->thread = kthread_create_on_node(agdsp_access_init_thread,
						dsp_ac, -1, "agdsp_access");
	if (IS_ERR(dsp_ac->thread)) {
		printk(KERN_ERR "[sprd-aud-agdsp]  g_agdsp_access->thread create failed\n");
		dsp_ac->thread = NULL;
		goto err_free;
	}

	wake_up_process(dsp_ac->thread);
	dsp_ac->ready = 1;

	return 0;

err_free:
	if (!dsp_ac->state_phys) {
		kfree(dsp_ac->state);
	} else if (dsp_ac->state) {
		shmem_ram_unmap_ex(AGDSP_SIPC_DST, 0, dsp_ac->state);
		smem_free_ex(AGDSP_SIPC_DST, 0, dsp_ac->state_phys,
			     dsp_ac->state_size);
	}

	kfree(dsp_ac);
	g_agdsp_access = NULL;
	printk(KERN_ERR "[sprd-aud-agdsp] agdsp_access init failed, exit.ret:%d\n",
	       -EINVAL);

	return -EINVAL;
}

static int agdsp_access_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *agcp_ahb, *pmu_apb;
	u32 ddr_offset = 0;
	u32 auto_access = 0;

	msg_index = 0;

	if (!of_find_property(np, AGDSP_DT_DDR_OFFSET, NULL))
		ddr_offset = 0;
	else if (of_property_read_u32(np, AGDSP_DT_DDR_OFFSET, &ddr_offset) < 0) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, parse 'sprd,ddr-addr-offset' failed!\n",
		       __func__);
		return -EINVAL;
	}

	if (of_property_read_u32(np, AGDSP_DT_AUTO_ACCESS, &auto_access) < 0)
		auto_access = 0;

	pr_debug("[sprd-aud-agdsp] %s entry auto agcp access=%d\n",
		 __func__, auto_access);

	agcp_ahb = syscon_regmap_lookup_by_phandle(np, AGDSP_DT_AGCP_AHB);
	pmu_apb = syscon_regmap_lookup_by_phandle(np, AGDSP_DT_PMU_APB);
	if (IS_ERR(agcp_ahb) || IS_ERR(pmu_apb)) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, agcp-ahb or pmu_apb not exist!\n",
		       __func__);
		return -EINVAL;
	}

	pr_debug("[sprd-aud-agdsp] %s, agcp_ahb=0x%p, pmu_apb=0x%p\n",
		 __func__, agcp_ahb, pmu_apb);

	return agdsp_access_initialize(np, agcp_ahb, pmu_apb, auto_access,
				       ddr_offset);
}

/**
 * agdsp_can_access() - is the DSP currently reachable?
 *
 * Read-only probe used by modules that want to avoid waking the DSP.  Returns
 * 1 when everything is powered and ungated, 0 otherwise.
 */
int agdsp_can_access(void)
{
	struct agdsp_access *dsp_ac = g_agdsp_access;
	u32 value = 0;
	int ret;

	if (!dsp_ac)
		return 0;

	if (!dsp_ac->auto_access) {
		ret = regmap_read(dsp_ac->agcp_ahb,
				  dsp_ac->ap_access_ena.reg, &value);
		if (ret) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read AUDACCESS_APB_AGCP_CTRL error!\n",
			       __func__);
			return 0;
		}
		if (!(value & dsp_ac->ap_access_ena.mask)) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, AUDACCESS_APB_AGCP_ACCESS_EN not enable! value: %x\n",
			       __func__, value);
			return 0;
		}
	}

	ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->sleep_ctrl.reg, &value);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_sleep_ctrl_reg error!\n",
		       __func__);
		return 0;
	}
	if (value & dsp_ac->sleep_ctrl.mask) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, agdsp AUDACCESS_PMU_APB_AGCP_DEEP_SLEEP !value : %x\n",
		       __func__, value);
		return 0;
	}

	ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->slp_status.reg, &value);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_slp_status_reg error!\n",
		       __func__);
		return 0;
	}
	if (((value & dsp_ac->slp_status.mask) >>
	     agdsp_shift(dsp_ac->slp_status.mask)) != 6) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, BIT_PMU_APB_AGCP_SYS_SLP_STATUS not enable! value: %x\n",
		       __func__, value);
		return 0;
	}

	ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status4.reg, &value);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_pwr_status4_reg error!\n",
		       __func__);
		return 0;
	}
	if (((value & dsp_ac->pwr_status4.mask) >>
	     agdsp_shift(dsp_ac->pwr_status4.mask)) != 0) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, AUDACCESS_BIT_AGCP_SYS_STATE not enable! value: %x\n",
		       __func__, value);
		return 0;
	}

	ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status3.reg, &value);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_pwr_status3_reg error!\n",
		       __func__);
		return 0;
	}
	if (((value & dsp_ac->pwr_status3.mask) >>
	     agdsp_shift(dsp_ac->pwr_status3.mask)) != 0) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, AUDACCESS_BIT_AGCP_DSP_STATE not enable! value: %x\n",
		       __func__, value);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(agdsp_can_access);

/**
 * agdsp_access_enable() - take a reference and make sure the DSP is awake
 *
 * The first caller (ap_enable_cnt == 0) has to actually wake the DSP: the
 * AP access bit is set, the mailbox is rung and audcp_pmu_pwr_status4 is
 * polled until the DSP reports that it is up.
 */
int agdsp_access_enable(void)
{
	struct agdsp_access *dsp_ac = g_agdsp_access;
	u32 value = 0;
	int ret = 0;

	if (!dsp_ac) {
		printk(KERN_ERR "[sprd-aud-agdsp] agdsp access not init, exit\n");
		return -EINVAL;
	}

	if (!dsp_ac->ready || !dsp_ac->state) {
		printk(KERN_ERR "[sprd-aud-agdsp] agdsp access not ready, dsp_ac->ready=%d, dsp_ac->state=%p\n",
		       dsp_ac->ready, dsp_ac->state);
		return AGDSP_ERR_NOT_READY;
	}

	spin_lock(&dsp_ac->lock);

	if (!dsp_ac->auto_access) {
		ret = regmap_update_bits(dsp_ac->agcp_ahb,
					 dsp_ac->ap_access_ena.reg,
					 dsp_ac->ap_access_ena.mask,
					 dsp_ac->ap_access_ena.mask);
		if (ret) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_update_bits AUDACCESS_APB_AGCP_CTRL error!\n",
			       __func__);
			goto out;
		}

		ret = regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				  &value);
		if (ret) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read AUDACCESS_APB_AGCP_CTRL error!\n",
			       __func__);
			goto out;
		}

		if (!(value & dsp_ac->ap_access_ena.mask)) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, AUDACCESS_APB_AGCP_ACCESS_EN not enable! value: %x\n",
			       __func__, value);
			ret = -EINVAL;
			goto out;
		}
	}

	/* first reference: the DSP may be in deep sleep, wake it up */
	if (dsp_ac->state->ap_enable_cnt == 0) {
		ret = regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				  &value);
		if (!ret)
			printk(KERN_INFO "[sprd-aud-agdsp] %s, ap_access_ena_reg (wake up dsp) val = 0x%x\n",
			       __func__, value);
		else
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read ap_access_ena_reg error!\n",
			       __func__);

		if (WARN_ON_ONCE(msg_index >= AGDSP_MSG_NUM))
			msg_index = 0;

		msg_val[msg_index] = AGDSP_WAKEUP_MSG;
		agdsp_access_send_wakeup(&msg_val[msg_index]);
		msg_index = (msg_index + 1 > AGDSP_MSG_NUM - 1) ?
			    0 : msg_index + 1;

		udelay(30);
		ret = agdsp_access_poll_power_up(dsp_ac);

		ret = regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				  &value);
		if (!ret)
			printk(KERN_INFO "[sprd-aud-agdsp] %s, ap_access_ena_reg (wake up done) val = 0x%x\n",
			       __func__, value);
		else
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read ap_access_ena_reg error!\n",
			       __func__);
	}

	smp_mb();
	dsp_ac->state->ap_enable_cnt++;
	smp_mb();

	pr_debug("[sprd-aud-agdsp] %s, ap_enable_cnt=%d, cp_enable_cnt=%d.cnt=%d\n",
		 __func__, dsp_ac->state->ap_enable_cnt,
		 dsp_ac->state->cp_enable_cnt, msg_index);
out:
	spin_unlock(&dsp_ac->lock);

	return ret;
}
EXPORT_SYMBOL(agdsp_access_enable);

/**
 * agdsp_access_disable() - drop a reference, gate the DSP again at zero
 */
int agdsp_access_disable(void)
{
	struct agdsp_access *dsp_ac = g_agdsp_access;

	if (!dsp_ac || !dsp_ac->ready || !dsp_ac->state)
		return -EINVAL;

	spin_lock(&dsp_ac->lock);

	smp_mb();
	if (dsp_ac->state->ap_enable_cnt != 0) {
		smp_mb();
		dsp_ac->state->ap_enable_cnt--;
	}

	if (!dsp_ac->auto_access && dsp_ac->state->ap_enable_cnt == 0)
		regmap_update_bits(dsp_ac->agcp_ahb,
				   dsp_ac->ap_access_ena.reg,
				   dsp_ac->ap_access_ena.mask, 0);

	spin_unlock(&dsp_ac->lock);

	return 0;
}
EXPORT_SYMBOL(agdsp_access_disable);

/**
 * restore_access() - re-open the AP window after a DSP/PMU reset
 *
 * Called by the resume / DSP-recovery paths.  In auto access mode there is
 * nothing to do, the DSP drives the gating itself.
 */
int restore_access(void)
{
	/*
	 * The vendor built the message on the stack; a static slot is used here
	 * because mbox_send_message() keeps the pointer until the transfer is
	 * acknowledged.
	 */
	static u64 restore_msg = AGDSP_WAKEUP_MSG;
	struct agdsp_access *dsp_ac = g_agdsp_access;
	u32 value = 0;
	u32 cnt = 0;
	int ret;

	if (!dsp_ac) {
		printk(KERN_ERR "[sprd-aud-agdsp] agdsp access not init, exit\n");
		return -EINVAL;
	}

	if (dsp_ac->auto_access == 1) {
		pr_debug("[sprd-aud-agdsp] %s restore use auto access do nothing\n",
			 __func__);
		return 0;
	}

	if (!dsp_ac->ready || !dsp_ac->state) {
		printk(KERN_ERR "[sprd-aud-agdsp] agdsp access not ready, dsp_ac->ready=%d, dsp_ac->state=%p\n",
		       dsp_ac->ready, dsp_ac->state);
		return -EINVAL;
	}

	spin_lock(&dsp_ac->lock);

	ret = regmap_update_bits(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				 dsp_ac->ap_access_ena.mask,
				 dsp_ac->ap_access_ena.mask);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_update_bits AUDACCESS_APB_AGCP_CTRL error!\n",
		       __func__);
		goto out;
	}

	ret = regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg, &value);
	if (ret) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read AUDACCESS_APB_AGCP_CTRL error!\n",
		       __func__);
		goto out;
	}

	if (!(value & dsp_ac->ap_access_ena.mask)) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s, AUDACCESS_APB_AGCP_ACCESS_EN not enable! value: %x\n",
		       __func__, value);
		goto log;
	}

	/* only poke the DSP when the AP side believes it has access */
	if (dsp_ac->state->ap_enable_cnt == 0)
		goto log;

	ret = mbox_send_message(g_agdsp_mboxchan, &restore_msg);
	if (ret < 0)
		printk(KERN_ERR "[sprd-aud-agdsp] %s, mbox send message error!\n",
		       __func__);
	mbox_chan_txdone(g_agdsp_mboxchan, 0);
	udelay(30);

	for (cnt = 0; cnt < 1000000; cnt++) {
		ret = regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status4.reg,
				  &value);
		if (ret) {
			printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read error!\n",
			       __func__);
			goto out;
		}

		if (((value & dsp_ac->pwr_status4.mask) >>
		     agdsp_shift(dsp_ac->pwr_status4.mask)) == 0) {
			ret = 0;
			goto log;
		}

		udelay(5);
	}

	printk(KERN_ERR "[sprd-aud-agdsp] ERR: %s wait agdsp power up timeout!\n",
	       "restore_access");
	ret = -ETIMEDOUT;

log:
	regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg, &value);
	printk(KERN_INFO "[sprd-aud-agdsp] %s, ap_enable_cnt=%d, cp_enable_cnt=%d.cnt=%d, access value=%#x\n",
	       __func__, dsp_ac->state->ap_enable_cnt,
	       dsp_ac->state->cp_enable_cnt, cnt, value);
out:
	spin_unlock(&dsp_ac->lock);

	return ret;
}
EXPORT_SYMBOL(restore_access);

/**
 * disable_access_force() - unconditionally close the AP window
 *
 * Used by the DSP crash dump path, where the reference counting of
 * agdsp_access_disable() is not trusted any more.
 */
int disable_access_force(void)
{
	struct agdsp_access *dsp_ac = g_agdsp_access;
	u32 value = 0;
	int ret;

	if (!dsp_ac) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s failed %d\n",
		       __func__, __LINE__);
		return -EINVAL;
	}

	if (dsp_ac->auto_access == 1) {
		pr_debug("[sprd-aud-agdsp] %s disable_auto_access_force do nothing\n",
			 __func__);
		return 0;
	}

	if (!dsp_ac->ready || !dsp_ac->state) {
		printk(KERN_ERR "[sprd-aud-agdsp] %s failed %d\n",
		       __func__, __LINE__);
		return -EINVAL;
	}

	spin_lock(&dsp_ac->lock);

	ret = regmap_update_bits(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg,
				 dsp_ac->ap_access_ena.mask, 0);
	regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg, &value);
	printk(KERN_INFO "[sprd-aud-agdsp] %s,update register AUDACCESS_APB_AGCP_CTRL, ret=%d, access value =%#x\n",
	       __func__, ret, value);

	spin_unlock(&dsp_ac->lock);

	return 0;
}
EXPORT_SYMBOL(disable_access_force);

/**
 * agdsp_access_dumpreg() - dump the whole AGDSP handshake state
 *
 * Debug helper; the vendor version does not NULL-check g_agdsp_access, which
 * is kept as-is because callers only use it from the DSP dump path.
 */
void agdsp_access_dumpreg(void)
{
	struct agdsp_access *dsp_ac = g_agdsp_access;
	u32 value = 0;

	printk(KERN_INFO "[sprd-aud-agdsp] %s, ap_enable_cnt = %d\n",
	       __func__, dsp_ac->state->ap_enable_cnt);

	if (!regmap_read(dsp_ac->agcp_ahb, dsp_ac->ap_access_ena.reg, &value))
		printk(KERN_INFO "[sprd-aud-agdsp] %s, ap_access_ena_reg (dsp assert) val = 0x%x\n",
		       __func__, value);
	else
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read ap_access_ena_reg error!\n",
		       __func__);

	if (!regmap_read(dsp_ac->pmu_apb, dsp_ac->sleep_ctrl.reg, &value))
		printk(KERN_INFO "[sprd-aud-agdsp] %s, audcp_pmu_sleep_ctrl_reg (dsp assert) val = 0x%x\n",
		       __func__, value);
	else
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_sleep_ctrl_reg error!\n",
		       __func__);

	if (!regmap_read(dsp_ac->pmu_apb, dsp_ac->slp_status.reg, &value))
		printk(KERN_INFO "[sprd-aud-agdsp] %s, audcp_pmu_slp_status_reg (dsp assert) val = 0x%x\n",
		       __func__, value);
	else
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_slp_status_reg error!\n",
		       __func__);

	if (!regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status3.reg, &value))
		printk(KERN_INFO "[sprd-aud-agdsp] %s, audcp_pmu_pwr_status3_reg (dsp assert) val = 0x%x\n",
		       __func__, value);
	else
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_pwr_status3_reg error!\n",
		       __func__);

	if (!regmap_read(dsp_ac->pmu_apb, dsp_ac->pwr_status4.reg, &value))
		printk(KERN_INFO "[sprd-aud-agdsp] %s, audcp_pmu_pwr_status4_reg (dsp assert) val = 0x%x\n",
		       __func__, value);
	else
		printk(KERN_ERR "[sprd-aud-agdsp] %s, regmap_read audcp_pmu_pwr_status4_reg error!\n",
		       __func__);
}
EXPORT_SYMBOL(agdsp_access_dumpreg);

static const struct of_device_id agdsp_access_of_match[] = {
	{ .compatible = "unisoc,agdsp-access" },
	{ }
};
MODULE_DEVICE_TABLE(of, agdsp_access_of_match);

static struct platform_driver agdsp_access_driver = {
	.probe = agdsp_access_probe,
	.driver = {
		.name = "agdsp_access",
		.of_match_table = agdsp_access_of_match,
	},
};

static int __init agdsp_access_init(void)
{
	return platform_driver_register(&agdsp_access_driver);
}

static void __exit agdsp_access_exit(void)
{
	platform_driver_unregister(&agdsp_access_driver);
}

module_init(agdsp_access_init);
module_exit(agdsp_access_exit);

MODULE_AUTHOR("yintang.ren <yintang.ren@spreadtrum.com>");
MODULE_DESCRIPTION("agdsp access driver");
MODULE_LICENSE("GPL");
