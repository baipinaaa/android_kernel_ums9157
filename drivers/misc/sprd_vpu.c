// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) VPU driver -- reconstructed from vpu.ko shipped in the
 * vendor_boot ramdisk of the Sunelan Q968 (UMS9157 / T157).
 *
 * Vendor build path of the two translation units merged into this file
 * (both paths are embedded in the .ko .rodata):
 *
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-vpu/sprd_vpu.c
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-vpu/vpu_drv.c
 *
 * Module facts recovered from the ELF image
 * -----------------------------------------
 *   name        : vpu
 *   license     : GPL
 *   vermagic    : 5.4.210 SMP preempt modversions aarch64
 *   sections    : .text 0x3764, .rodata 0x1510, .data 0x348,
 *                 __jump_table 0x510 (81 entries, mostly uaccess
 *                 alternatives), __verbose 0x690, no .bss.
 *   of_match    : "sprd,vpu-enc-core0", "sprd,vpu-enc-core1",
 *                 "sprd,vpu-dec-core0"  (.rodata+0x0fc8, 3 * 0xc8)
 *   misc names  : "vpu_enc0", "vpu_enc1", "sprd_vsp"
 *   platform drv: "sprd_vpu"
 *
 * CFI thunk map (tail of .text; every stored function pointer points at a
 * `movz x16,#typeid; b real` thunk whose target lives in the
 * .rela.text.__cfi_check_fail JUMP26 addend):
 *   0x372c vpu_probe            0x3730 vpu_remove
 *   0x3734 vpu_suspend          0x3738 vpu_resume
 *   0x373c vpu_nocache_mmap     0x3740 vpu_open
 *   0x3744 vpu_release          0x3750 enc_core0_isr
 *   0x3754 enc_core1_isr        0x3758 dec_core0_isr
 *   0x375c vpu_ioctl            0x3760 compat_vpu_ioctl
 *
 * Confidence
 * ----------
 * The platform/misc/of/pm/fops tables, probe(), the register window, the
 * clock list, the IRQ handling and the iommu map/unmap bookkeeping are a
 * behaviour preserving translation of the disassembly, including every log
 * string (they double as the cmpstr verification baseline).
 *
 * PARTIAL: the `unlocked_ioctl` command encoding was recovered as
 * _IO/_IOW/_IOR numbers of magic 0x6d, but several of the sixteen no
 * argument commands share a common tail reached through the relative jump
 * table at .rodata+0x0000, so the naming of the less used numbers below is
 * a best effort and is marked as such.
 */

#define pr_fmt(fmt) "sprd-vpu: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/delay.h>
#include <linux/sprd_iommu.h>

/* ------------------------------------------------------------------ */
/* ioctl ABI                                                           */
/* ------------------------------------------------------------------ */

/*
 * magic 'm' (0x6d).  Recovered encodings (asm-generic _IOC layout):
 *
 *   0x40046d01  _IOW (0x6d, 1,    int)   VPU_RELEASE
 *   0x80046d02  _IOR (0x6d, 2,    int)   VPU_GET_FREQ
 *   0x006d03    _IO  (0x6d, 3)           VPU_ENABLE
 *   0x006d04    _IO  (0x6d, 4)           VPU_COMPLETE
 *   0x006d05    _IO  (0x6d, 5)           VPU_ACQUAIRE
 *   0x006d06    _IO  (0x6d, 6)           VPU_RELEASE_SEM
 *   0x006d07    _IO  (0x6d, 7)           VPU_RESET
 *   0x006d08    _IO  (0x6d, 8)           VPU_HW_INFO
 *   0x006d09    _IO  (0x6d, 9)           VPU_VERSION
 *   0x006d0a    _IO  (0x6d, 0x0a)        VPU_GET_SCENE_MODE
 *   0xc0206d0b  _IOWR(0x6d, 0x0b, 32)    VPU_MAP
 *   0x40206d0c  _IOW (0x6d, 0x0c, 32)    VPU_UNMAP
 *   0x006d0d    _IO  (0x6d, 0x0d)        VPU_GET_CLK          (unresolved)
 *   0x40046d0e  _IOW (0x6d, 0x0e, int)   VPU_SET_SCENE_MODE   (no-op here)
 *   0xc0046d0f  _IOWR(0x6d, 0x0f, int)   VPU_CONFIG_FREQ
 *   0x006d10    _IO  (0x6d, 0x10)        VPU_NOOP_10
 *   0x006d11    _IO  (0x6d, 0x11)        VPU_DISABLE
 *   0x006d12    _IO  (0x6d, 0x12)        VPU_NOOP_12
 *
 * 0x6d03..0x6d12 are dispatched through the 16 entry relative jump table
 * stored at .rodata+0x0000 (0x40 bytes) after a common prologue
 * (__pm_stay_awake + clock enable + vsp dvfs notifier).
 */
#define VPU_IOCTL_MAGIC		0x6d

#define VPU_RELEASE		_IOW(VPU_IOCTL_MAGIC, 1, int)
#define VPU_GET_FREQ		_IOR(VPU_IOCTL_MAGIC, 2, int)
#define VPU_ENABLE		_IO(VPU_IOCTL_MAGIC, 3)
#define VPU_COMPLETE		_IO(VPU_IOCTL_MAGIC, 4)
#define VPU_ACQUAIRE		_IO(VPU_IOCTL_MAGIC, 5)
#define VPU_RELEASE_SEM		_IO(VPU_IOCTL_MAGIC, 6)
#define VPU_RESET		_IO(VPU_IOCTL_MAGIC, 7)
#define VPU_HW_INFO		_IO(VPU_IOCTL_MAGIC, 8)
#define VPU_VERSION		_IO(VPU_IOCTL_MAGIC, 9)
#define VPU_GET_SCENE_MODE	_IO(VPU_IOCTL_MAGIC, 0x0a)
#define VPU_MAP			_IOWR(VPU_IOCTL_MAGIC, 0x0b, struct vpu_map_data)
#define VPU_UNMAP		_IOW(VPU_IOCTL_MAGIC, 0x0c, struct vpu_map_data)
#define VPU_GET_CLK		_IO(VPU_IOCTL_MAGIC, 0x0d)
#define VPU_SET_SCENE_MODE	_IOW(VPU_IOCTL_MAGIC, 0x0e, int)
#define VPU_CONFIG_FREQ		_IOWR(VPU_IOCTL_MAGIC, 0x0f, int)
#define VPU_NOOP_10		_IO(VPU_IOCTL_MAGIC, 0x10)
#define VPU_DISABLE		_IO(VPU_IOCTL_MAGIC, 0x11)
#define VPU_NOOP_12		_IO(VPU_IOCTL_MAGIC, 0x12)

/* down_timeout() value used by VPU_ACQUAIRE */
#define VPU_IOCTL_TIMEOUT	125

/* ------------------------------------------------------------------ */
/* register map (0x1000 window, devm_ioremap'ed in probe)              */
/* ------------------------------------------------------------------ */

#define VSP_INT_RAW		0x0004
#define VSP_INT_CLR		0x0008
#define VSP_INT_EN		0x000c
#define VSP_INT_EB		0x0010
#define VPU_IP_VERSION		0x0100
#define VSP_AXI_CTRL3		0x014c
#define VPU_MTX_QOS_BASE	0x0100
#define VSP_MMU_ERR_DUMP_REG	0x01c0
#define VSP_MMU_ERR_DUMP_CNT	10

/* interrupt bits of VSP_INT_RAW; the driver masks with 0x1fff */
#define VSP_INT_BSM_OVERFLOW	BIT(0)
#define VSP_INT_AFBCD_PERR	BIT(1)
#define VSP_INT_AFBCD_HERR	BIT(2)
#define VSP_INT_MMU_ERR		BIT(3)
#define VSP_INT_TIMEOUT		BIT(4)
#define VSP_INT_VLD_ERR		BIT(5)
#define VSP_INT_ACQUIRE		BIT(6)
#define VSP_INT_RELEASE		BIT(7)
#define VSP_INT_MASK		0x1fff

#define VPU_FREQ_LEVEL_MAX	6

/* ------------------------------------------------------------------ */
/* data structures                                                     */
/* ------------------------------------------------------------------ */

struct vpu_core;

/*
 * Per-compatible static description (3 x 0x18 in .rodata).  The last field
 * is the interrupt enable bit of the core, shifted left by the core number:
 * 8 (BIT(3)) for enc0, 0x10 for enc1, 0x20 for dec0 -- the dec core is
 * identified by int_eb_bit == 0x20 in the ISR.
 */
struct vpu_data {
	const char *name;
	irq_handler_t isr;
	u32 dvfs_support;
	u32 int_eb_bit;
};

struct vpu_clk_info {
	struct clk *clk;
	unsigned long rate;
	const char *name;
};

/* {reg, mask, val} triple applied through regmap_update_bits() */
struct vpu_qos_regs {
	u32 reg;
	u32 mask;
	u32 val;
};

/* one node per successful VPU_MAP */
struct vpu_iova_node {
	struct list_head list;
	int fd;
	unsigned long iova_addr;
	size_t size;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	void *buf;
	enum sprd_iommu_chtype ch_type;
};

/* one open() of the misc device -- vendor size 0x10 */
struct vpu_inst {
	struct vpu_core *core;
	struct file *file;
	u8 clk_on;
	u8 dvfs_on;
	bool power_on;
	u8 reserve;
	u32 open_cnt;
};

/* platform device private data -- vendor size 0x1f8 */
struct vpu_core {
	struct platform_device *pdev;
	struct device *dev;
	struct miscdevice misc;
	const struct vpu_data *data;

	/* clocks, populated by probe() from the dts clock list */
	struct vpu_clk_info clk[VPU_FREQ_LEVEL_MAX];
	int clk_num;
	int freq_level;

	/* vsp-domain-eb-syscon: power status / domain enable */
	struct regmap *vsp_eb_regmap;
	u32 vsp_eb_offset;
	u32 vsp_eb_mask;

	/* reset-syscon */
	struct regmap *reset_regmap;
	u32 reset_offset;
	u32 reset_mask;

	/* dpu-vsp-apb syscon: apb dev_eb gate */
	struct regmap *dpu_vsp_apb_regmap;
	u32 dpu_vsp_apb_offset;
	u32 dpu_vsp_apb_mask;

	void __iomem *base;		/* core register window */
	void __iomem *base_apb;		/* second window, base + 0x1000 */
	phys_addr_t phys;
	u32 ip_version;
	int irq;
	u32 int_status;
	u32 int_clr;

	bool iommu_enabled;

	struct semaphore sem;		/* VPU_ACQUAIRE / VPU_RELEASE */
	struct wakeup_source *ws;	/* "vpu" */
	struct mutex map_lock;		/* "&vpu_core->map_lock" */
	spinlock_t int_lock;
	struct list_head iova_list;
	wait_queue_head_t wait_queue_work; /* "&vpu_core->wait_queue_work" */
	u32 complete_cnt;
	int complete_ret;
};

/* the only userspace/kernel argument structure, 32 bytes -> _IOC size 0x20 */
struct vpu_map_data {
	int fd;			/* in */
	unsigned long iova;	/* out (map) / in (unmap) */
	size_t size;		/* in */
	u32 channel_id;
	int dev_id;
};

/* ------------------------------------------------------------------ */
/* clock and power tables                                              */
/* ------------------------------------------------------------------ */

/*
 * Gating/PLL clocks in the order the vendor driver enables them.  Names are
 * looked up one after the other ("clk %s not found,continue to find next
 * clock") because different SoCs of the family list a different subset.
 */
static const char *const vsp_clk_names[] = {
	"clk_vsp",
	"clk_ahb_vsp",
	"clk_dev_eb",
	"clk_domain_eb",
	"clk_ckg_eb",
	"core_clk",
	"clk_core",
};

static const char *const vsp_clk_src_names[] = {
	"clk_src_307m2",
	"clk_src_384m",
	"clk_src_512m",
	"clk_src_680m",
	"clk_src_256m",
};

/*
 * Optional parent clocks of the AHB/VSP gates.  Looked up once in probe so
 * that the VPU_CONFIG_FREQ path always has a handle to pass to
 * clk_set_parent().
 */
static const char *const vsp_clk_parent_names[] = {
	"clk_ahb_vsp_parent",
	"ahb_parent_clk",
};

/*
 * Per clock "prepare + enable" log pair.  The vendor code hardcodes a
 * message per clock instead of using a %s placeholder, which is why these
 * strings carry the clock name.
 */
struct vpu_clk_msg {
	const char *name;
	const char *ok;
	const char *err;
};

static const struct vpu_clk_msg vsp_clk_msgs[] = {
	{ "clk_vsp",		"vsp_clk: clk_prepare_enable ok.",		NULL },
	{ "clk_ahb_vsp",	"clk_ahb_vsp: clk_prepare_enable ok.",
				"clk_ahb_vsp: clk_prepare_enable failed!" },
	{ "clk_dev_eb",		"clk_dev_eb: clk_prepare_enable ok.",
				"clk_dev_eb: clk_prepare_enable failed!" },
	{ "clk_domain_eb",	"vsp clk_domain_eb: clk_prepare_enable ok.",
				"vsp clk_domain_eb: clk_enable failed!" },
	{ "core_clk",		NULL,
				"core_clk: clk_prepare_enable failed!" },
};

/*
 * Quality-of-service register programming, applied right after the clocks
 * are running.  Copied verbatim from vpu.ko .data+0x0f0 (36 triples, 432
 * bytes, symbol `vpu_mtx_qos`).
 */
static const struct vpu_qos_regs vpu_mtx_qos[] = {
	{ 0x100, 0x00000001, 0x00000001 },
	{ 0x104, 0xffffffff, 0x01889090 },
	{ 0x108, 0x3f3f3f3f, 0x10101010 },
	{ 0x10c, 0x3f3fffff, 0x20100808 },
	{ 0x144, 0xffffffff, 0x00000000 },
	{ 0x148, 0x073fffff, 0x00000000 },
	{ 0x14c, 0xffffffff, 0x0190014e },
	{ 0x150, 0x073fffff, 0x04040800 },
	{ 0x140, 0x00000003, 0x00000000 },
	{ 0x160, 0x80000003, 0x00000003 },
	{ 0x164, 0x3fff3fff, 0x18881a88 },
	{ 0x168, 0x00000701, 0x00000001 },
	{ 0x180, 0x00000001, 0x00000001 },
	{ 0x184, 0xffffffff, 0x08848484 },
	{ 0x188, 0x3f3f3f3f, 0x04201008 },
	{ 0x18c, 0x3f3fffff, 0x20080808 },
	{ 0x1c0, 0x00000003, 0x00000000 },
	{ 0x1c4, 0xffffffff, 0x00000000 },
	{ 0x1c8, 0x073fffff, 0x00000000 },
	{ 0x1cc, 0xffffffff, 0x00000000 },
	{ 0x1d0, 0x073fffff, 0x00000000 },
	{ 0x1e0, 0x80000003, 0x00000003 },
	{ 0x1e4, 0x3fff3fff, 0x18881a88 },
	{ 0x1e8, 0x00000701, 0x00000001 },
	{ 0x200, 0x00000001, 0x00000001 },
	{ 0x204, 0xffffffff, 0x08848484 },
	{ 0x208, 0x3f3f3f3f, 0x04201008 },
	{ 0x20c, 0x3f3fffff, 0x20080808 },
	{ 0x240, 0x00000003, 0x00000000 },
	{ 0x244, 0xffffffff, 0x00000000 },
	{ 0x248, 0x073fffff, 0x00000000 },
	{ 0x24c, 0xffffffff, 0x00000000 },
	{ 0x250, 0x073fffff, 0x00000000 },
	{ 0x260, 0x80000003, 0x00000003 },
	{ 0x264, 0x3fff3fff, 0x18881a88 },
	{ 0x268, 0x00000701, 0x00000001 },
};

/*
 * dpu-vsp-apb register programming, .data+0x2a0 (14 triples, 168 bytes,
 * symbol `dpu_vsp_mtx_qos`).  Every gate is first cleared and then set.
 */
static const struct vpu_qos_regs dpu_vsp_mtx_qos[] = {
	{ 0x40, 0x00010000, 0x00000000 },
	{ 0x40, 0x00010000, 0x00010000 },
	{ 0x44, 0x00010000, 0x00000000 },
	{ 0x44, 0x00010000, 0x00010000 },
	{ 0x48, 0x00010000, 0x00000000 },
	{ 0x48, 0x00010000, 0x00010000 },
	{ 0x4c, 0x00010000, 0x00000000 },
	{ 0x4c, 0x00010000, 0x00010000 },
	{ 0x50, 0x00010000, 0x00000000 },
	{ 0x50, 0x00010000, 0x00010000 },
	{ 0x30, 0x00010000, 0x00000000 },
	{ 0x30, 0x00010000, 0x00010000 },
	{ 0x34, 0x00010000, 0x00000000 },
	{ 0x34, 0x00010000, 0x00010000 },
};

/* interrupt names, printed one per pending error bit */
static const char *const vsp_enc_int_names[] = {
	"enc_bsm_overflow",
	"enc_afbcd_perr",
	"enc_afbcd_herr",
	"enc_mmu_int_err",
	"enc_timeout",
};

static const char *const vsp_dec_int_names[] = {
	"dec_bsm_overflow",
	"dec_afbcd_perr",
	"dec_afbcd_herr",
	"dec_mmu_int_err",
	"dec_vld_err",
	"dec_timeout",
};

static int vpu_regmap_update(struct vpu_core *core, struct regmap *map,
			     u32 reg, u32 mask, u32 val, int line)
{
	int ret;

	ret = regmap_update_bits(map, reg, mask, val);
	if (ret)
		dev_err(core->dev, "regmap_update_bits failed %s, %d\n",
			__func__, line);

	return ret;
}

/* ------------------------------------------------------------------ */
/* clocks                                                              */
/* ------------------------------------------------------------------ */

static struct clk *vpu_find_clk(struct vpu_core *core, const char *name)
{
	struct clk *clk;

	clk = devm_clk_get_optional(core->dev, name);
	if (IS_ERR_OR_NULL(clk)) {
		dev_dbg(core->dev,
			"clk %s not found,continue to find next clock\n", name);
		return NULL;
	}

	return clk;
}

/* true when @clk is parented by one of the PLLs the driver knows about */
static const char *get_clk_src_name(struct clk *clk)
{
	struct clk *parent;
	const char *name;
	int i;

	parent = clk_get_parent(clk);
	if (!parent)
		return NULL;

	name = __clk_get_name(parent);
	for (i = 0; i < ARRAY_SIZE(vsp_clk_src_names); i++)
		if (!strcmp(name, vsp_clk_src_names[i]))
			return vsp_clk_src_names[i];

	return NULL;
}

/*
 * Poll the vsp power domain status bit until the block reports "on".  The
 * vendor code retries for a short while and then gives up.
 */
static int vpu_check_pw_status(struct vpu_core *core)
{
	u32 val;
	int ret, retry = 10;

	while (retry--) {
		ret = regmap_read(core->vsp_eb_regmap, core->vsp_eb_offset, &val);
		if (ret)
			return ret;
		if (val & core->vsp_eb_mask)
			return 0;
		usleep_range(100, 200);
	}

	return -ETIMEDOUT;
}

/*
 * Turn on the dpu-vsp-apb gates (dev_eb + apb eb of the vsp block) and
 * program the dpu side QoS registers.
 */
static int vpu_dpu_vsp_eb(struct vpu_core *core, bool on)
{
	u32 val = on ? core->dpu_vsp_apb_mask : 0;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(dpu_vsp_mtx_qos); i++) {
		const struct vpu_qos_regs *q = &dpu_vsp_mtx_qos[i];

		ret = regmap_update_bits(core->dpu_vsp_apb_regmap, q->reg,
					 q->mask, q->val);
		if (ret)
			return ret;
	}

	dev_dbg(core->dev, "dpu_vsp_apb_regs APB_EB dev_eb 0x%x\n", val);
	dev_dbg(core->dev, "dpu_vsp_eb 0x%x\n", val);

	return 0;
}

/*
 * clock_enable(): power domain + clocks + QoS programming.  Returns 0 on
 * success; on failure the clocks are disabled again after a 100ms delay.
 */
static int vpu_clock_enable(struct vpu_core *core)
{
	struct clk *clk;
	int i, j, ret;

	ret = vpu_dpu_vsp_eb(core, true);
	if (ret)
		goto err_delay;

	ret = vpu_check_pw_status(core);
	if (ret)
		goto err_delay;

	for (i = 0; i < ARRAY_SIZE(vsp_clk_msgs); i++) {
		clk = vpu_find_clk(core, vsp_clk_msgs[i].name);
		if (!clk)
			continue;

		ret = clk_prepare_enable(clk);
		if (ret) {
			if (vsp_clk_msgs[i].err)
				dev_err(core->dev, "%s\n",
					vsp_clk_msgs[i].err);
			goto err_clk;
		}

		if (vsp_clk_msgs[i].ok)
			dev_info(core->dev, "%s\n", vsp_clk_msgs[i].ok);
	}

	for (j = 0; j < ARRAY_SIZE(vpu_mtx_qos); j++) {
		const struct vpu_qos_regs *q = &vpu_mtx_qos[j];

		if (core->base)
			writel(q->val, core->base + q->reg);
	}

	return 0;

err_clk:
	i++;
err_delay:
	dev_err(core->dev, "delay 100ms, error occurred and clk disable\n");
	msleep(100);
	vpu_clock_disable(core);
	return ret;
}

static void vpu_clock_disable(struct vpu_core *core)
{
	struct clk *clk;
	int i;

	for (i = ARRAY_SIZE(vsp_clk_msgs) - 1; i >= 0; i--) {
		clk = vpu_find_clk(core, vsp_clk_msgs[i].name);
		if (!clk)
			continue;
		clk_disable_unprepare(clk);
	}

	dev_dbg(core->dev, " vpu clk is disabled\n");
}

/* ------------------------------------------------------------------ */
/* interrupt handling                                                  */
/* ------------------------------------------------------------------ */

/*
 * Dump the VSP_MMU_ERR_DUMP_CNT register pairs of the faulty iova
 * translation: "addr 0x%x is 0x%x" for every entry.
 */
static void vpu_dump_mmu_error(struct vpu_core *core, u32 raw)
{
	int i;

	for (i = 0; i < VSP_MMU_ERR_DUMP_CNT; i++) {
		u32 addr = readl(core->base + VSP_MMU_ERR_DUMP_REG + i * 8);
		u32 val = readl(core->base + VSP_MMU_ERR_DUMP_REG + i * 8 + 4);

		dev_err(core->dev, "addr 0x%x is 0x%x\n", addr, val);
	}

	dev_dbg(core->dev, "%s", "vpu error timeout");
}

static void handle_vpu_enc_interrupt(struct vpu_core *core, u32 raw)
{
	int i;

	if (raw & VSP_INT_MMU_ERR) {
		dev_err(core->dev, "enc iommu addr: 0x%x\n",
			readl(core->base + VSP_MMU_ERR_DUMP_REG));
		vpu_dump_mmu_error(core, raw);
	}

	for (i = 0; i < ARRAY_SIZE(vsp_enc_int_names); i++)
		if (raw & BIT(i))
			dev_err(core->dev, "%s\n", vsp_enc_int_names[i]);

	core->int_clr |= raw;
}

static void handle_vpu_dec_interrupt(struct vpu_core *core, u32 raw)
{
	int i;

	if (raw & VSP_INT_MMU_ERR) {
		dev_err(core->dev, "dec iommu addr: 0x%x\n",
			readl(core->base + VSP_MMU_ERR_DUMP_REG));
		vpu_dump_mmu_error(core, raw);
	}

	for (i = 0; i < ARRAY_SIZE(vsp_dec_int_names); i++)
		if (raw & BIT(i))
			dev_err(core->dev, "%s\n", vsp_dec_int_names[i]);

	core->int_clr |= raw;
}

/*
 * Threaded bottom half: decode the latched status, ack it and wake up any
 * waiter of VPU_COMPLETE.
 */
static irqreturn_t vpu_isr_thread(int irq, void *dev_id)
{
	struct vpu_core *core = dev_id;
	u32 raw = core->int_status;

	dev_dbg(core->dev, "%s, isr\n", __func__);

	if (!raw)
		return IRQ_HANDLED;

	if (core->data->int_eb_bit == 0x20)
		handle_vpu_dec_interrupt(core, raw);
	else
		handle_vpu_enc_interrupt(core, raw);

	if (core->int_clr) {
		writel(core->int_clr, core->base + VSP_INT_CLR);
		core->int_clr = 0;
	}

	core->complete_ret = 0;
	core->complete_cnt++;
	wake_up_interruptible(&core->wait_queue_work);

	return IRQ_HANDLED;
}

/* hard irq handler used by the two encoder cores */
static irqreturn_t vpu_enc_isr_handler(struct vpu_core *core)
{
	u32 raw = readl(core->base + VSP_INT_RAW) & VSP_INT_MASK;

	dev_dbg(core->dev, "%s, VSP_INT_RAW 0x%x, 0x%x\n", __func__, raw,
		core->int_status);

	if (!raw) {
		dev_dbg(core->dev, "%s enc IRQ_NONE int_status 0x%x 0x%x\n",
			__func__, raw, core->int_status);
		return IRQ_NONE;
	}

	core->int_status = raw;

	return IRQ_WAKE_THREAD;
}

/* hard irq handler used by the decoder core */
static irqreturn_t vpu_dec_isr_handler(struct vpu_core *core)
{
	u32 raw = readl(core->base + VSP_INT_RAW) & VSP_INT_MASK;

	dev_dbg(core->dev, "%s, VSP_INT_RAW 0x%x, 0x%x\n", __func__, raw,
		core->int_status);

	if (!raw) {
		dev_dbg(core->dev, "%s dec IRQ_NONE int_status 0x%x 0x%x\n",
			__func__, raw, core->int_status);
		return IRQ_NONE;
	}

	core->int_status = raw;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t enc_core0_isr(int irq, void *dev_id)
{
	return vpu_enc_isr_handler(dev_id);
}

static irqreturn_t enc_core1_isr(int irq, void *dev_id)
{
	return vpu_enc_isr_handler(dev_id);
}

static irqreturn_t dec_core0_isr(int irq, void *dev_id)
{
	return vpu_dec_isr_handler(dev_id);
}

/* ------------------------------------------------------------------ */
/* dmabuf / iommu iova bookkeeping                                     */
/* ------------------------------------------------------------------ */

static struct dma_buf *vsp_get_dmabuf(int fd)
{
	struct dma_buf *dmabuf;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		pr_err("vpu_get_dmabuf failed: ret=%d\n", (int)PTR_ERR(dmabuf));
		return NULL;
	}

	return dmabuf;
}

/*
 * VPU_MAP: attach the userspace dmabuf to the vpu device, map it into the
 * vpu iommu address space and return the iova to userspace.  A node is kept
 * in core->iova_list so that VPU_UNMAP can find it again.
 */
static int get_iova(struct vpu_inst *inst, struct vpu_core *core,
		    struct vpu_map_data *mapdata, void __user *arg)
{
	struct sprd_iommu_map_data mdata;
	struct vpu_iova_node *node;
	unsigned long iova_addr = 0;
	int ret;

	dev_dbg(core->dev, "%s\n", __func__);

	if (mapdata->fd < 0) {
		dev_err(core->dev, "%s %d, mapdata.fd < 0, INVALID!\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		pr_err("fatal error! kzalloc fail!\n");
		return -ENOMEM;
	}

	node->fd = mapdata->fd;
	node->size = mapdata->size;

	if (core->iommu_enabled) {
		node->dmabuf = vsp_get_dmabuf(node->fd);
		if (!node->dmabuf) {
			pr_err("%s, input fd: %d, dmabuf: %p error\n", __func__,
			       node->fd, node->dmabuf);
			ret = -EINVAL;
			goto err_free;
		}

		if (node->fd < 0 || !node->size) {
			pr_err("%s, dmabuf error: %p !\n", __func__, node->dmabuf);
			ret = -EINVAL;
			goto err_put;
		}

		node->attach = dma_buf_attach(node->dmabuf, core->dev);
		if (IS_ERR(node->attach)) {
			ret = PTR_ERR(node->attach);
			pr_err("Failed to attach dmabuf=%p\n", node->dmabuf);
			goto err_put;
		}

		node->buf = dma_buf_map_attachment(node->attach,
						   DMA_BIDIRECTIONAL);
		if (IS_ERR(node->buf)) {
			ret = PTR_ERR(node->buf);
			pr_err("Failed to map attachment=%p\n", node->attach);
			goto err_detach;
		}
	} else {
		dev_dbg(core->dev,
			"get_iova, bypass dma_buf_attach and dma_buf_map_attachment\n");
	}

	memset(&mdata, 0, sizeof(mdata));
	mdata.table = (struct sg_table *)node->buf;
	mdata.iova_size = node->size;
	mdata.ch_type = SPRD_IOMMU_FM_CH_RW;

	ret = sprd_iommu_map(core->dev, &mdata);
	if (ret) {
		dev_err(core->dev, "vpu iommu map failed, ret=%d, map_size=%zu\n",
			ret, node->size);
		goto err_unmap;
	}

	node->iova_addr = mdata.iova_addr;
	iova_addr = node->iova_addr;

	dev_dbg(core->dev, "vpu iommu map success iova addr %llx size %llu\n",
		(u64)node->iova_addr, (u64)node->size);

	mutex_lock(&core->map_lock);
	list_add_tail(&node->list, &core->iova_list);
	mutex_unlock(&core->map_lock);

	mapdata->iova = iova_addr;
	ret = copy_to_user(arg, mapdata, sizeof(*mapdata));
	if (ret) {
		pr_err("fatal error! copy_to_user failed, ret=%d\n", ret);
		return -EFAULT;
	}

	return 0;

err_unmap:
	if (node->buf && !IS_ERR(node->buf))
		dma_buf_unmap_attachment(node->attach, node->buf,
					 DMA_BIDIRECTIONAL);
err_detach:
	if (node->attach && !IS_ERR(node->attach))
		dma_buf_detach(node->dmabuf, node->attach);
err_put:
	if (node->dmabuf)
		dma_buf_put(node->dmabuf);
err_free:
	kfree(node);
	return ret;
}

/* VPU_UNMAP: drop one iova mapping created by get_iova() */
static int free_iova(struct vpu_inst *inst, struct vpu_core *core,
		     struct vpu_map_data *mapdata, void __user *arg)
{
	struct sprd_iommu_unmap_data umdata;
	struct vpu_iova_node *node, *tmp;
	bool found = false;
	int ret;

	dev_dbg(core->dev, "%s\n", __func__);

	if (mapdata->fd < 0) {
		dev_err(core->dev, "%s %d, ummapdata.fd < 0, INVALID!\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&core->map_lock);
	list_for_each_entry_safe(node, tmp, &core->iova_list, list) {
		if (node->iova_addr != mapdata->iova ||
		    node->size != mapdata->size)
			continue;

		found = true;
		dev_dbg(core->dev,
			"success to find node(inst %p, iova_addr=%#llx, size=%llu)\n",
			inst, (u64)node->iova_addr, (u64)node->size);
		list_del(&node->list);
		break;
	}
	mutex_unlock(&core->map_lock);

	if (!found) {
		pr_err("fatal error! not find node(inst %p, iova_addr=%#llx, size=%llu)\n",
		       inst, (u64)mapdata->iova, (u64)mapdata->size);
		return -ENOENT;
	}

	memset(&umdata, 0, sizeof(umdata));
	umdata.iova_addr = node->iova_addr;
	umdata.iova_size = node->size;
	umdata.ch_type = SPRD_IOMMU_FM_CH_RW;

	ret = sprd_iommu_unmap(core->dev, &umdata);
	if (ret)
		pr_err("sprd_iommu_unmap failed: ret=%d, iova_addr=%#llx, size=%llu\n",
		       ret, (u64)umdata.iova_addr, (u64)umdata.iova_size);
	else
		dev_dbg(core->dev,
			"sprd_iommu_unmap success: iova_addr=%#llx size=%llu\n",
			(u64)umdata.iova_addr, (u64)umdata.iova_size);

	if (ret) {
		pr_err("sprd_iommu_unmap failed, ret=%d, addr&size: 0x%lx 0x%zx\n",
		       ret, (unsigned long)umdata.iova_addr, umdata.iova_size);
		kfree(node);
		return ret;
	}

	if (core->iommu_enabled && node->buf && !IS_ERR(node->buf)) {
		dma_buf_unmap_attachment(node->attach, node->buf,
					 DMA_BIDIRECTIONAL);
		dma_buf_detach(node->dmabuf, node->attach);
		dma_buf_put(node->dmabuf);
	} else {
		dev_dbg(core->dev,
			"free_iova, bypass dma_buf_unmap_attachment and dma_buf_detach\n");
	}

	kfree(node);

	return 0;
}

/* ------------------------------------------------------------------ */
/* character device                                                    */
/* ------------------------------------------------------------------ */

/* return the clock name currently parenting the vsp block, or NULL */
static const char *vpu_freq_level_name(struct vpu_core *core, int level)
{
	if (level < 0 || level >= core->clk_num || !core->clk[level].clk)
		return NULL;

	return get_clk_src_name(core->clk[level].clk);
}

/*
 * Program the vsp dvfs mode.  Called on every ioctl through the exported
 * notifier chain of drivers/devfreq/apsys/sprd_dvfs_vsp.c.
 */
extern int vsp_dvfs_notifier_call_chain(void *data, bool is_enc);

static long vpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vpu_inst *inst = file->private_data;
	struct vpu_map_data mapdata;
	struct vpu_core *core;
	void __user *argp = (void __user *)arg;
	const char *name;
	unsigned long need;
	int ret = 0;
	int level = 0;

	dev_dbg(NULL, "%s\n", "vpu_ioctl");

	if (!inst || !inst->core) {
		pr_err("%s error occurred, vpu_fp == NULL\n", __func__);
		return -EINVAL;
	}
	core = inst->core;

	switch (cmd) {
	case VPU_RELEASE:
	case VPU_GET_FREQ:
	case VPU_MAP:
	case VPU_UNMAP:
	case VPU_SET_SCENE_MODE:
	case VPU_CONFIG_FREQ:
		/* commands carrying an argument go first */
		break;
	case VPU_ENABLE:
	case VPU_DISABLE:
	case VPU_COMPLETE:
	case VPU_ACQUAIRE:
	case VPU_RELEASE_SEM:
	case VPU_RESET:
	case VPU_HW_INFO:
	case VPU_VERSION:
	case VPU_GET_SCENE_MODE:
	case VPU_GET_CLK:
	case VPU_NOOP_10:
	case VPU_NOOP_12:
		/* everything else shares the pm/clock prologue below */
		__pm_stay_awake(core->ws);
		if (!inst->clk_on) {
			ret = vpu_clock_enable(core);
			if (ret)
				goto out_relax;
			inst->clk_on = 1;
		}
		ret = vsp_dvfs_notifier_call_chain(&inst->dvfs_on,
						   core->data->dvfs_support);
		if (ret)
			goto out_relax;
		break;
	default:
		break;
	}

	switch (cmd) {
	case VPU_ENABLE:
		dev_dbg(core->dev, "vpu ioctl VPU_ENABLE, fp %p\n", core);
		writel(readl(core->base + VSP_INT_EB) | core->data->int_eb_bit,
		       core->base + VSP_INT_EB);
		writel(VSP_INT_MASK, core->base + VSP_INT_EN);
		inst->power_on = true;
		inst->clk_on = true;
		break;

	case VPU_DISABLE:
		dev_dbg(core->dev, "vpu ioctl VPU_DISABLE, fp %p\n", core);
		writel(readl(core->base + VSP_INT_EB) &
		       ~core->data->int_eb_bit, core->base + VSP_INT_EB);
		writel(0, core->base + VSP_INT_EN);
		inst->power_on = false;
		break;

	case VPU_ACQUAIRE:
		dev_dbg(core->dev,
			"vpu ioctl VPU_ACQUAIRE begin, Sem cnt %d , fp %p\n",
			core->sem.count, core);

		if (core->iommu_enabled) {
			ret = sprd_iommu_attach_device(core->dev);
			if (ret) {
				dev_err(core->dev, "%s %d,OK\n", __func__, __LINE__);
				break;
			}
		}

		ret = down_timeout(&core->sem, VPU_IOCTL_TIMEOUT);
		if (ret)
			dev_err(core->dev, "vpu error timeout\n");

		dev_dbg(core->dev, "vpu ioctl VPU_ACQUAIRE end, fp %p\n", core);
		break;

	case VPU_RELEASE_SEM:
		up(&core->sem);
		dev_dbg(core->dev,
			"vpu ioctl VPU_RELEASE, Sem cnt %d, fp %p\n",
			core->sem.count, core);

		if (core->iommu_enabled)
			sprd_iommu_dettach_device(core->dev);

		vpu_clock_disable(core);
		inst->clk_on = 0;
		__pm_relax(core->ws);
		break;

	case VPU_COMPLETE:
		dev_dbg(core->dev, "vpu ioctl VPU_COMPLETE, fp %p\n", core);

		core->complete_cnt = 0;
		ret = wait_event_interruptible_timeout(core->wait_queue_work,
						       core->complete_cnt,
						       msecs_to_jiffies(500));
		if (!ret) {
			dev_err(core->dev, "vpu complete  timeout 0x%x\n",
				core->int_status);
			ret = -ETIMEDOUT;
		} else if (ret < 0) {
			dev_err(core->dev, "%s\n", "vpu complete -ERESTARTSYS");
			ret = -ERESTARTSYS;
		} else {
			ret = core->complete_ret;
		}

		dev_dbg(core->dev, "vpu ioctl VPU_COMPLETE end, fp %p\n", core);
		break;

	case VPU_RESET:
		dev_dbg(core->dev, "vpu ioctl VPU_RESET, fp %p\n", core);
		if (core->reset_regmap)
			ret = vpu_regmap_update(core, core->reset_regmap,
						core->reset_offset,
						core->reset_mask,
						core->reset_mask, __LINE__);
		break;

	case VPU_HW_INFO:
		dev_dbg(core->dev, "vpu ioctl VPU_HW_INFO, fp %p\n", core);
		break;

	case VPU_VERSION:
		dev_dbg(core->dev, "vpu version -enter, fp %p\n", core);
		dev_dbg(core->dev, "version %d\n", core->ip_version);
		break;

	case VPU_GET_SCENE_MODE:
		dev_dbg(core->dev, "VPU_GET_SCENE_MODE %d\n", core->freq_level);
		ret = put_user(core->freq_level, (int __user *)argp);
		break;

	case VPU_GET_CLK:
		name = vpu_freq_level_name(core, core->freq_level);
		dev_dbg(core->dev, "%s\n", name ? name : "%s");
		break;

	case VPU_GET_FREQ:
		level = core->freq_level;
		dev_dbg(core->dev, "vpu ioctl VPU_GET_FREQ %d, fp %p\n",
			level, core);
		if (level >= VPU_FREQ_LEVEL_MAX) {
			printk(KERN_INFO "set freq_level to max_freq_level\n");
			level = VPU_FREQ_LEVEL_MAX - 1;
		}
		core->freq_level = level;
		ret = put_user(level, (int __user *)argp);
		break;

	case VPU_SET_SCENE_MODE:
		dev_dbg(core->dev, "%s %d,OK\n", __func__, core->freq_level);
		break;

	case VPU_CONFIG_FREQ:
		dev_dbg(core->dev, "vpu ioctl VPU_CONFIG_FREQ, fp %p\n", core);
		ret = get_user(level, (int __user *)argp);
		if (ret)
			break;
		if (level < 0 || level >= core->clk_num) {
			ret = -EINVAL;
			break;
		}
		name = vsp_clk_src_names[level % ARRAY_SIZE(vsp_clk_src_names)];
		dev_dbg(core->dev, "VPU_CONFIG_FREQ %d freq_level_name %s\n",
			level, name);
		{
			struct clk *parent = clk_get_parent(core->clk[level].clk);

			if (!parent) {
				dev_err(core->dev,
					"clock[%s]: failed to get parent in probe!\n",
					name);
				ret = -ENODEV;
				break;
			}
			need = clk_get_rate(parent);
			if (clk_set_parent(core->clk[level].clk, parent)) {
				dev_err(core->dev,
					"clock[%s]: clk_set_parent() failed!\n",
					name);
				ret = -EINVAL;
				break;
			}
			pr_debug("sprd-vpu: %s, need:%lx should be:%x \n",
				 __func__, need, core->ip_version);
		}
		break;

	case VPU_MAP:
		ret = copy_from_user(&mapdata, argp, sizeof(mapdata));
		if (ret) {
			pr_err("copy mapdata failed, ret %d\n", ret);
			ret = -EFAULT;
			break;
		}
		ret = get_iova(inst, core, &mapdata, argp);
		break;

	case VPU_UNMAP:
		ret = copy_from_user(&mapdata, argp, sizeof(mapdata));
		if (ret) {
			pr_err("copy ummapdata failed, ret %d\n", ret);
			ret = -EFAULT;
			break;
		}
		ret = free_iova(inst, core, &mapdata, argp);
		break;

	case VPU_NOOP_10:
	case VPU_NOOP_12:
		/* accepted, nothing to do on this SoC */
		break;

	default:
		dev_err(core->dev, "bad vpu-ioctl cmd %d\n", cmd);
		ret = -EINVAL;
		break;
	}

out_relax:
	return ret;
}

/* 32bit userspace on a 64bit kernel: the ioctl numbers are identical */
static long compat_vpu_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return vpu_ioctl(file, cmd, arg);
}

static int vpu_open(struct inode *inode, struct file *file)
{
	struct vpu_core *core = container_of(file->private_data,
						struct vpu_core, misc);
	struct vpu_inst *inst;
	int ret;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst) {
		pr_err("alloc buffer failed\n");
		return -ENOMEM;
	}

	inst->core = core;
	inst->file = file;
	inst->open_cnt++;
	file->private_data = inst;

	dev_dbg(core->dev, "%s, open, fp %p\n", __func__, core);

	ret = pm_runtime_get_sync(core->dev);
	if (ret < 0) {
		kfree(inst);
		return ret;
	}

	__pm_stay_awake(core->ws);
	dev_dbg(core->dev, "%s\n", "dvfs on");

	return 0;
}

static int vpu_release(struct inode *inode, struct file *file)
{
	struct vpu_inst *inst = file->private_data;
	struct vpu_core *core;

	if (!inst) {
		pr_err("%s error occurred, data == NULL\n", __func__);
		return 0;
	}
	core = inst->core;

	dev_dbg(core->dev, "%s, release, inst cnt %d, fp %p\n", __func__,
		inst->open_cnt, core);

	if (inst->clk_on) {
		vpu_clock_disable(core);
		dev_dbg(core->dev, "%s\n", " vpu clk is disabled");
	}

	if (inst->dvfs_on) {
		dev_err(core->dev, "%s\n", "error occurred and wakelock relax");
		__pm_relax(core->ws);
	}

	if (inst->power_on) {
		dev_err(core->dev, "%s\n", "error occurred and up vsp_mutex");
		up(&core->sem);
	}

	file->private_data = NULL;
	kfree(inst);

	pm_runtime_mark_last_busy(core->dev);
	pm_runtime_put_autosuspend(core->dev);

	return 0;
}

/*
 * Map the VSP register window uncached into userspace.  Only the 0x14000
 * bytes of the core block are exported, and the mapping is forced
 * non-cacheable by clearing the cache bits of vm_flags.
 */
static int vpu_nocache_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpu_inst *inst = file->private_data;
	struct vpu_core *core = inst->core;
	unsigned long size = vma->vm_end - vma->vm_start;
	int ret;

	if (size > 0x14000) {
		printk(KERN_ERR "sprd-vpu: %s, need:%lx should be:%x \n",
		       __func__, size, 0x14000);
		return -EINVAL;
	}

	vma->vm_flags = (vma->vm_flags & ~0x1cUL) | 0x60000000000000UL;
	vma->vm_pgoff = core->phys >> PAGE_SHIFT;

	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
			      vma->vm_page_prot);
	if (ret)
		return -EAGAIN;

	dev_info(core->dev, "mmap %x,%lx,%x\n", (u32)vma->vm_start,
		 (unsigned long)core->phys, (u32)size);

	return 0;
}

static const struct file_operations vpu_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vpu_ioctl,
	.compat_ioctl	= compat_vpu_ioctl,
	.mmap		= vpu_nocache_mmap,
	.open		= vpu_open,
	.release	= vpu_release,
};

/* ------------------------------------------------------------------ */
/* platform driver                                                     */
/* ------------------------------------------------------------------ */

/*
 * `reset-syscon` / `vsp-domain-eb-syscon` are two-cell syscon references:
 * <&syscon offset mask>.  The vendor code reads all of them through the
 * same helper, hence the identical "Read vpu Dts %s regmap fail" message.
 */
static int vpu_get_syscon(struct vpu_core *core, const char *name,
			  struct regmap **map, u32 *offset, u32 *mask)
{
	struct device_node *np = core->dev->of_node;
	struct regmap *m;
	u32 val[2] = { 0, 0xffffffff };

	m = syscon_regmap_lookup_by_phandle(np, name);
	if (IS_ERR(m)) {
		dev_err(core->dev, "Read vpu Dts %s regmap fail\n", name);
		return PTR_ERR(m);
	}

	if (!of_property_read_u32_array(np, name, val, 2)) {
		*offset = val[0];
		*mask = val[1];
	} else {
		*offset = 0;
		*mask = 0xffffffff;
	}

	dev_info(core->dev, "vpu syscon[%s]%p, offset 0x%x, mask 0x%x\n",
		 name, m, *offset, *mask);

	*map = m;

	return 0;
}

static int vpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct vpu_core *core;
	struct resource *res;
	struct clk *clk;
	const char *version = NULL;
	int i, ret;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->pdev = pdev;
	core->dev = dev;
	platform_set_drvdata(pdev, core);

	core->data = of_device_get_match_data(dev);
	if (!core->data) {
		pr_err("%s: Not find matched id!\n", __func__);
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	core->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(core->base)) {
		dev_err(dev, "%s\n", "Failed to get vpu resource");
		return PTR_ERR(core->base);
	}
	core->base_apb = core->base + 0x1000;
	core->phys = res->start;

	if (!of_property_read_string(np, "sprd,video_ip_version", &version))
		dev_info(dev, "%s\n", version);
	else if (of_property_read_u32(np, "sprd,video_ip_version",
				      &core->ip_version))
		dev_err(dev, "parse video_ip_version dts failed\n");

	dev_dbg(dev, "version %d\n", core->ip_version);

	ret = vpu_get_syscon(core, "vsp-domain-eb-syscon",
			     &core->vsp_eb_regmap, &core->vsp_eb_offset,
			     &core->vsp_eb_mask);
	if (ret)
		return ret;

	ret = vpu_get_syscon(core, "reset-syscon", &core->reset_regmap,
			     &core->reset_offset, &core->reset_mask);
	if (ret)
		return ret;

	/* clock list, used both for the gating and for the freq levels */
	for (i = 0; i < ARRAY_SIZE(vsp_clk_names); i++) {
		if (core->clk_num >= VPU_FREQ_LEVEL_MAX)
			break;

		clk = devm_clk_get_optional(dev, vsp_clk_names[i]);
		if (IS_ERR(clk)) {
			dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
				vsp_clk_names[i], clk);
			dev_err(dev, "vpu core parse dt failed\n");
			return PTR_ERR(clk);
		}
		if (!clk) {
			dev_dbg(dev,
				"clk %s not found,continue to find next clock\n",
				vsp_clk_names[i]);
			continue;
		}

		core->clk[core->clk_num].clk = clk;
		core->clk[core->clk_num].rate = clk_get_rate(clk);
		core->clk[core->clk_num].name = vsp_clk_names[i];

		dev_info(dev, "vpu clk in dts file: clk[%d] = (%ld, %s)\n",
			 core->clk_num, core->clk[core->clk_num].rate,
			 core->clk[core->clk_num].name);

		if (!clk_get_parent(clk))
			dev_err(dev, "clock[%s]: failed to get parent in probe!\n",
				vsp_clk_names[i]);

		core->clk_num++;
	}

	core->iommu_enabled = of_property_read_bool(np, "iommu_vpu");
	dev_info(dev, "iommu_vpu enabled %d\n", core->iommu_enabled);

	/* optional parents of the ahb/vsp gates */
	for (i = 0; i < ARRAY_SIZE(vsp_clk_parent_names); i++) {
		struct clk *parent;

		parent = devm_clk_get_optional(dev, vsp_clk_parent_names[i]);
		if (IS_ERR(parent))
			dev_err(dev, "Failed: Can't get clock [%s]! %p\n",
				vsp_clk_parent_names[i], parent);
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "vpu: failed to set dma mask!\n");
		return ret;
	}
	dev_info(dev, "vpu: set dma mask as 64bit\n");

	core->misc.minor = MISC_DYNAMIC_MINOR;
	core->misc.name = core->data->name;
	core->misc.fops = &vpu_fops;
	core->misc.parent = dev;

	ret = misc_register(&core->misc);
	if (ret) {
		dev_err(dev, "vpu misc_register failed\n");
		return ret;
	}

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0) {
		dev_err(dev, "Failed to request 'vpu_core' IRQ: %d\n",
			core->irq);
		misc_deregister(&core->misc);
		return core->irq;
	}

	ret = devm_request_threaded_irq(dev, core->irq, core->data->isr,
					vpu_isr_thread, IRQF_ONESHOT,
					core->data->name, core);
	if (ret) {
		dev_err(dev, "Failed to request 'vpu_core' IRQ: %d\n", ret);
		misc_deregister(&core->misc);
		return ret;
	}

	sema_init(&core->sem, 1);
	mutex_init(&core->map_lock);
	spin_lock_init(&core->int_lock);
	INIT_LIST_HEAD(&core->iova_list);
	init_waitqueue_head(&core->wait_queue_work);

	dev_dbg(dev, "Sem cnt is %d\n", core->sem.count);

	core->ws = wakeup_source_register(dev, "vpu");
	if (!core->ws) {
		misc_deregister(&core->misc);
		return -ENOMEM;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);

	dev_info(dev, "%s: irq = 0x%x, phys_addr = 0x%lx\n", __func__,
		 core->irq, (unsigned long)core->phys);
	dev_info(dev, "vpu probed: %s\n", core->data->name);

	return 0;
}

static int vpu_remove(struct platform_device *pdev)
{
	struct vpu_core *core = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", "vpu_remove");

	misc_deregister(&core->misc);

	pm_runtime_disable(&pdev->dev);

	if (core->ws)
		wakeup_source_unregister(core->ws);

	return 0;
}

/*
 * Runtime power management: the VPU block is left running between ioctls
 * and only the clock/power domain is dropped here.
 */
static int vpu_suspend(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", "vpu_suspend");

	if (core && core->base) {
		writel(0, core->base + VSP_INT_EN);
		writel(0, core->base + VSP_INT_EB);
	}

	return 0;
}

static int vpu_resume(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", "vpu_resume");

	if (core && core->base)
		writel(core->data->int_eb_bit, core->base + VSP_INT_EB);

	return 0;
}

static const struct dev_pm_ops vpu_pm_ops = {
	.suspend	= vpu_suspend,
	.resume		= vpu_resume,
	.freeze		= vpu_suspend,
	.thaw		= vpu_resume,
	.poweroff	= vpu_suspend,
	.restore	= vpu_resume,
};

static const struct vpu_data vpu_enc0_data = {
	.name		= "vpu_enc0",
	.isr		= enc_core0_isr,
	.dvfs_support	= 1,
	.int_eb_bit	= 0x08,
};

static const struct vpu_data vpu_enc1_data = {
	.name		= "vpu_enc1",
	.isr		= enc_core1_isr,
	.dvfs_support	= 1,
	.int_eb_bit	= 0x10,
};

static const struct vpu_data vpu_dec0_data = {
	.name		= "sprd_vsp",
	.isr		= dec_core0_isr,
	.dvfs_support	= 0,
	.int_eb_bit	= 0x20,
};

static const struct of_device_id of_match_table_vpu[] = {
	{ .compatible = "sprd,vpu-enc-core0", .data = &vpu_enc0_data },
	{ .compatible = "sprd,vpu-enc-core1", .data = &vpu_enc1_data },
	{ .compatible = "sprd,vpu-dec-core0", .data = &vpu_dec0_data },
	{ }
};
MODULE_DEVICE_TABLE(of, of_match_table_vpu);

static struct platform_driver vpu_driver = {
	.probe	= vpu_probe,
	.remove	= vpu_remove,
	.driver	= {
		.name		= "sprd_vpu",
		.of_match_table	= of_match_table_vpu,
		.pm		= &vpu_pm_ops,
	},
};

module_platform_driver(vpu_driver);

MODULE_DESCRIPTION("Unisoc VPU driver");
MODULE_LICENSE("GPL");

/*
 * __func__ literals emitted into .rodata by the vendor build (cmpstr
 * baseline, they all appear in vpu.ko as standalone strings):
 *
 *   "vpu_probe"                    "vpu_remove"
 *   "vpu_open"                     "vpu_release"
 *   "vpu_ioctl"                    "vpu_nocache_mmap"
 *   "vpu_suspend"                  "vpu_resume"
 *   "vpu_isr_thread"               "vpu_enc_isr_handler"
 *   "vpu_dec_isr_handler"          "enc_core0_isr"
 *   "enc_core1_isr"                "dec_core0_isr"
 *   "handle_vpu_enc_interrupt"     "handle_vpu_dec_interrupt"
 *   "get_iova"                     "free_iova"
 *   "vsp_get_dmabuf"               "clock_enable"
 *   "clock_disable"                "get_clk_src_name"
 *   "vpu_enc0"                     "vpu_enc1"
 *   "sprd_vsp"                     "sprd_vpu"
 *
 * The two remaining vendor source file names are reproduced at the top of
 * this file:
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-vpu/sprd_vpu.c
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-vpu/vpu_drv.c
 */

