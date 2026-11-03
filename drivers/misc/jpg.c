// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) JPG encoder / decoder driver
 *
 * This file was reconstructed from the AArch64 binary of the vendor module
 * "jpg.ko" shipped in vendor_boot_a.bin of the Sunelan Q968 (UMS9157 / T157)
 * firmware build 10CN_0_480 (Android 13, kernel 5.4.210-android12-9).
 *
 * The reconstruction is a 1:1 translation of the disassembly: register
 * sequences, log strings, error codes and vendor quirks are preserved.  Where
 * the original source layout cannot be recovered the binary offsets are kept as
 * comments.  Vendor sources the module was built from:
 *
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-jpg/sprd_jpg.c
 * /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/video/sprd-jpg/sprd_jpg_common.c
 *
 * Binary facts (jpg.ko):
 *   modinfo: description=SPRD JPG Driver, license=GPL v2,
 *            depends=unisoc-iommu,sprd_camsys_pw_domain, name=jpg
 *   sections: .bss=0x350 .data=0x140 .rodata=0x19c0 .text=0x2cd4
 *   thunks:   .text+0x2cac probe, +0x2cb0 remove, +0x2cb4 mmap, +0x2cb8 open,
 *             +0x2cbc release, +0x2cc0 isr, +0x2cc4 isr_thread,
 *             +0x2ccc ioctl, +0x2cd0 compat_ioctl
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/iommu.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/sprd_iommu.h>

/* ------------------------------------------------------------------------- */
/* hardware description                                                      */
/* ------------------------------------------------------------------------- */

#define JPG_MAX_CLK_SRC			10

/* register offsets (hw_dev.base relative) */
#define JPG_REG_INT_STATUS		0x2c
#define JPG_REG_INT_CLR			0x28
#define JPG_REG_DEBUG_INT		0x20
#define JPG_REG_IOMMU_INT		0x3ac
#define JPG_REG_IOMMU_INT_CLR		0x3a4
#define JPG_INT_CLR_ALL			0xf

/* interrupt status bits */
#define JPG_INT_MBIO			BIT(3)
#define JPG_INT_BSM			BIT(0)
#define JPG_INT_VLC			BIT(1)
#define JPG_INT_ALL			(JPG_INT_MBIO | JPG_INT_BSM | JPG_INT_VLC)

/* hw_dev.int_status_bits flags reported to user space */
#define JPG_STAT_MBIO			BIT(3)
#define JPG_STAT_VLC			BIT(1)
#define JPG_STAT_BSM			BIT(0)
#define JPG_STAT_ERR_RESTART		BIT(30)
#define JPG_STAT_ERR_TIMEOUT		BIT(31)

/* hw id (drv_data[0]) thresholds */
#define JPG_HW_ID_PIKE2			0xe
#define JPG_HW_ID_SHARKL3		0xf
#define JPG_HW_ID_IOMMU_INT		0x12

/* timeouts, kept as raw jiffies exactly like the binary */
#define JPG_SEM_TIMEOUT			250
#define JPG_WAIT_TIMEOUT		250

#define JPG_MMAP_MAX_SIZE		0x6000
#define JPG_IOVA_PROT			0x102

/* ioctl commands (binary encodes them as _IO('m', n) & friends) */
#define JPG_CONFIG_FREQ			_IOW('m', 1, int)
#define JPG_GET_FREQ			_IOR('m', 2, int)
#define JPG_ENABLE			_IO('m', 3)
#define JPG_DISABLE			_IO('m', 4)
#define JPG_ACQUIRE_MUTEX		_IO('m', 5)
#define JPG_RELEASE			_IO('m', 6)
#define JPG_START			_IO('m', 7)
#define JPG_RESET			_IO('m', 8)
#define JPG_ACQUAIRE_MBIO_DONE		_IOR('m', 9, int)
#define JPG_GET_IOVA			_IOWR('m', 0x0b, struct jpg_iova)
#define JPG_FREE_IOVA			_IOWR('m', 0x0c, struct jpg_iova)
#define JPG_IOMMU_ATTACH		_IO('m', 0x0d)
#define JPG_VERSION			_IO('m', 0x0e)

/* returned by jpg_poll_mbio_vlc_done() on success (binary constants) */
#define JPG_POLL_MBIO_OK		0
#define JPG_POLL_VLC_OK			4
#define JPG_POLL_TIMEOUT		1
#define JPG_POLL_BAD_ARG		(-1)

#define JPG_WAIT_MBIO			3
#define JPG_WAIT_VLC			1

/* clock map entry: rate @0x00, name @0x08, clk @0x10 (24 bytes) */
struct jpg_clk_info {
	unsigned long rate;
	const char *name;
	struct clk *clk;
};

/* 24 byte iovec shared with user space.  The binary prints size as
 * "iova_addr" and iova_addr as "size" (vendor mistake) - kept as is.
 */
struct jpg_iova {
	u32 fd;
	u32 reserved;
	u64 size;
	u64 iova_addr;
};

/* 64 byte iova bookkeeping node (kmalloc-64) */
struct jpg_iova_node {
	struct list_head node;			/* 0x00 */
	u32 fd;					/* 0x10 */
	u32 reserved;
	u64 iova_addr;				/* 0x18 */
	u64 size;				/* 0x20 */
	struct dma_buf *dmabuf;			/* 0x28 */
	struct dma_buf_attachment *attach;	/* 0x30 */
	struct sg_table *sgt;			/* 0x38 */
};

/* syscon triple (regmap @0x00, offset @0x08, mask @0x0c) */
struct jpg_syscon {
	struct regmap *map;
	u32 offset;
	u32 mask;
};

/* per open file state (16 bytes) */
struct jpg_file {
	int sem_held;
	int clk_on;
	struct sprd_jpg_hw *hw;
};

/* hw_dev layout, verified against the binary (sizeof == 0x168) */
struct sprd_jpg_hw {
	u32 freq_level;				/* 0x000 */
	u32 reserved0;
	struct semaphore sem;			/* 0x008 */
	wait_queue_head_t wait_queue_work_MBIO;	/* 0x020 */
	u32 mbio_done;				/* 0x038 */
	u32 reserved1;
	wait_queue_head_t wait_queue_work_VLC;	/* 0x040 */
	u32 vlc_done;				/* 0x058 */
	u32 reserved2;
	wait_queue_head_t wait_queue_work_BSM;	/* 0x060 */
	u32 bsm_done;				/* 0x078 */
	u32 int_status_bits;			/* 0x07c */
	struct clk *jpg_clk;			/* 0x080 */
	struct clk *clk_src;			/* 0x088 */
	struct clk *clk_src_parent;		/* 0x090 */
	struct clk *jpg_domain_eb;		/* 0x098 */
	struct clk *clk_vsp_mq_ahb_eb;		/* 0x0a0 */
	struct clk *clk_aon_jpg_emc_eb;		/* 0x0a8 */
	struct clk *jpg_dev_eb;			/* 0x0b0 */
	struct clk *jpg_ckg_eb;			/* 0x0b8 */
	struct clk *clk_ahb_vsp;		/* 0x0c0 */
	struct clk *clk_ahb_vsp_parent;		/* 0x0c8 */
	struct clk *clk_emc_vsp;		/* 0x0d0 */
	struct clk *clk_emc_vsp_parent;		/* 0x0d8 */
	u32 irq;				/* 0x0e0 */
	u32 hw_id;				/* 0x0e4 */
	struct jpg_file *jpg_file;		/* 0x0e8 */
	struct device_node *of_node;		/* 0x0f0 */
	struct device *dev;			/* 0x0f8 */
	const void *drv_data;			/* 0x100 */
	u32 reserved3[2];
	struct jpg_clk_info *clk_map;		/* 0x110 */
	void __iomem *base;			/* 0x118 */
	phys_addr_t phys;			/* 0x120 */
	u32 reserved4[2];
	u32 freq_level_num;			/* 0x130 */
	u8 qos_found;				/* 0x134 */
	u8 reserved5[3];
	struct mutex iova_lock;			/* 0x138 */
	struct list_head iova_list;		/* 0x158 */
};

/* qos configuration block (qos_cfg, 8 bytes) */
struct jpg_qos_cfg {
	u8 awqos;
	u8 arqos_high;
	u8 arqos_low;
	u8 reserved;
	u32 offset;
};

static struct sprd_jpg_hw hw_dev;
static struct jpg_syscon regs[2];		/* 0: aon-apb-eb, 1: reset-syscon */
static struct jpg_qos_cfg qos_cfg;
static struct jpg_clk_info clock_name_map[JPG_MAX_CLK_SRC];

/*
 * The binary keeps a full "struct wakeup_source" here (208 bytes) and calls
 * __pm_stay_awake()/__pm_relax() on it without ever registering it - the vendor
 * assumes a statically initialised wakeup source works.  Behaviour preserved.
 */
static struct wakeup_source jpg_wakelock;

static const char *const jpg_clk_src[JPG_MAX_CLK_SRC] = {
	"clk_src_76m8",
	"clk_src_96m",
	"clk_src_128m",
	"clk_src_153m6",
	"clk_src_192m",
	"clk_src_256m",
	"clk_src_307m2",
	"clk_src_384m",
	"clk_src_409m6",
	"clk_src_512m",
};

/* drv_data: {hw_id, freq_level_num, qos_offset} */
static const u32 sharkle_jpg_data[]	= { 0xd, 4, 0x00 };
static const u32 pike2_jpg_data[]	= { 0xe, 4, 0x00 };
static const u32 sharkl3_jpg_data[]	= { 0xf, 4, 0x00 };
static const u32 sharkl5_jpg_data[]	= { 0x10, 4, 0x30 };
static const u32 roc1_jpg_data[]	= { 0x11, 4, 0x30 };
static const u32 sharkl5pro_jpg_data[]	= { 0x12, 4, 0x00 };
static const u32 qogirl6_jpg_data[]	= { 0x13, 4, 0x30 };
static const u32 qogirn6pro_jpg_data[]	= { 0x14, 5, 0x30 };

static const struct of_device_id of_match_table_jpg[] = {
	{ .compatible = "sprd,sharkle-jpg",    .data = sharkle_jpg_data },
	{ .compatible = "sprd,pike2-jpg",      .data = pike2_jpg_data },
	{ .compatible = "sprd,sharkl3-jpg",    .data = sharkl3_jpg_data },
	{ .compatible = "sprd,sharkl5-jpg",    .data = sharkl5_jpg_data },
	{ .compatible = "sprd,roc1-jpg",       .data = roc1_jpg_data },
	{ .compatible = "sprd,sharkl5pro-jpg", .data = sharkl5pro_jpg_data },
	{ .compatible = "sprd,qogirl6-jpg",    .data = qogirl6_jpg_data },
	{ .compatible = "sprd,qogirn6pro-jpg", .data = qogirn6pro_jpg_data },
	{ }
};

/* ------------------------------------------------------------------------- */
/* helpers                                                                   */
/* ------------------------------------------------------------------------- */

static struct sprd_jpg_hw *jpg_get_hw(void)
{
	return &hw_dev;
}

/*
 * jpg_get_clk_src_name() - return the clk of a frequency level
 *
 * Despite its name the binary returns map[index].clk (offset 0x10) and falls
 * back to level 0 when the index is out of range.
 */
static struct clk *jpg_get_clk_src_name(struct jpg_clk_info *map, int index,
					int count)
{
	pr_debug("sprd-jpg:  freq_level %d %s\n", index, map[index].name);

	if (index >= count) {
		printk(KERN_INFO "sprd-jpg: set freq_level to 0\n");
		index = 0;
	}

	return map[index].clk;
}

static int jpg_find_freq_level(struct jpg_clk_info *map, unsigned long rate,
			       int count)
{
	int i;

	if (!count)
		return 0;

	for (i = 0; i < count; i++) {
		if (map[i].rate == rate)
			return i;
	}

	return 0;
}

static int jpg_get_mm_clk(struct sprd_jpg_hw *hw)
{
	struct clk *clk;
	int ret = 0;

	clk = devm_clk_get(hw->dev, "jpg_domain_eb");
	if (IS_ERR(clk)) {
		dev_err(hw->dev, "Failed : Can't get clock [%s]!\n",
			"jpg_domain_eb");
		dev_err(hw->dev, "jpg_domain_eb =  %p\n", clk);
		ret = PTR_ERR(clk);
		clk = NULL;
	}
	hw->jpg_domain_eb = clk;

	if (hw->hw_id == JPG_HW_ID_SHARKL3) {
		clk = devm_clk_get(hw->dev, "clk_aon_jpg_emc_eb");
		if (IS_ERR(clk)) {
			dev_err(hw->dev, "Can't get clock [%s]!\n",
				"clk_aon_jpg_emc_eb");
			dev_err(hw->dev, "clk_aon_jpg_emc_eb = %p\n", clk);
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_aon_jpg_emc_eb = clk;
	}

	clk = devm_clk_get(hw->dev, "jpg_dev_eb");
	if (IS_ERR(clk)) {
		dev_err(hw->dev, "Failed : Can't get clock [%s]!\n",
			"jpg_dev_eb");
		dev_err(hw->dev, "jpg_dev_eb =  %p\n", clk);
		ret = PTR_ERR(clk);
		clk = NULL;
	}
	hw->jpg_dev_eb = clk;

	clk = devm_clk_get(hw->dev, "jpg_ckg_eb");
	if (IS_ERR(clk)) {
		dev_err(hw->dev, "Failed : Can't get clock [%s]!\n",
			"jpg_ckg_eb");
		dev_err(hw->dev, "jpg_ckg_eb =  %p\n", clk);
		ret = PTR_ERR(clk);
		clk = NULL;
	}
	hw->jpg_ckg_eb = clk;

	if (hw->hw_id == JPG_HW_ID_PIKE2) {
		clk = devm_clk_get(hw->dev, "clk_vsp_mq_ahb_eb");
		if (IS_ERR(clk)) {
			dev_err(hw->dev, "Failed: Can't get clock [%s]! %p\n",
				"clk_vsp_mq_ahb_eb", clk);
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_vsp_mq_ahb_eb = clk;
	}

	if (hw->hw_id == JPG_HW_ID_SHARKL3) {
		clk = devm_clk_get(hw->dev, "clk_ahb_vsp");
		if (IS_ERR(clk)) {
			dev_err(hw->dev, "Failed: Can't get clock [%s]! %p\n",
				"clk_ahb_vsp", clk);
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_ahb_vsp = clk;

		clk = devm_clk_get(hw->dev, "clk_ahb_vsp_parent");
		if (IS_ERR(clk)) {
			dev_err(hw->dev,
				"clock[%s]: failed to get parent in probe!\n",
				"clk_ahb_vsp_parent");
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_ahb_vsp_parent = clk;

		clk = devm_clk_get(hw->dev, "clk_emc_vsp");
		if (IS_ERR(clk)) {
			dev_err(hw->dev, "Failed: Can't get clock [%s]! %p\n",
				"clk_emc_vsp", clk);
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_emc_vsp = clk;

		clk = devm_clk_get(hw->dev, "clk_emc_vsp_parent");
		if (IS_ERR(clk)) {
			dev_err(hw->dev,
				"clock[%s]: failed to get parent in probe!\n",
				"clk_emc_vsp_parent");
			ret = PTR_ERR(clk);
			clk = NULL;
		}
		hw->clk_emc_vsp_parent = clk;
	}

	clk = devm_clk_get(hw->dev, "jpg_clk");
	if (IS_ERR(clk)) {
		dev_err(hw->dev, "Failed : Can't get clock [%s}!\n", "jpg_clk");
		dev_err(hw->dev, "jpg_clk =  %p\n", clk);
		ret = PTR_ERR(clk);
		clk = NULL;
	}
	hw->jpg_clk = clk;

	if (hw->freq_level_num) {
		hw->clk_src_parent = hw->clk_map[0].clk;
	} else {
		printk(KERN_INFO "sprd-jpg: set freq_level to 0\n");
		pr_debug("sprd-jpg:  freq_level %d %s\n", 0,
			 hw->clk_map[0].name);
		hw->clk_src_parent = hw->clk_map[0].clk;
	}

	return ret;
}

static int jpg_clk_enable(struct sprd_jpg_hw *hw)
{
	struct clk *clk;
	int ret;

	dev_info(hw->dev, "jpg JPG_ENABLE\n");

	clk = hw->jpg_domain_eb;
	ret = clk_prepare(clk);
	if (ret)
		goto err_domain;
	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		goto err_domain;
	}
	dev_dbg(hw->dev, "jpg jpg_domain_eb clk_prepare_enable ok.\n");

	if (hw->hw_id == JPG_HW_ID_SHARKL3 && hw->clk_aon_jpg_emc_eb) {
		clk = hw->clk_aon_jpg_emc_eb;
		ret = clk_prepare(clk);
		if (ret)
			goto err_aon;
		ret = clk_enable(clk);
		if (ret) {
			clk_unprepare(clk);
			goto err_aon;
		}
		dev_dbg(hw->dev,
			"clk_aon_jpg_emc_eb clk_prepare_enable ok.\n");
	}

	clk = hw->jpg_dev_eb;
	ret = clk_prepare(clk);
	if (ret)
		goto err_dev;
	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		goto err_dev;
	}
	dev_dbg(hw->dev, "jpg_dev_eb clk_prepare_enable ok.\n");

	clk = hw->jpg_ckg_eb;
	ret = clk_prepare(clk);
	if (ret)
		goto err_ckg;
	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		goto err_ckg;
	}
	dev_dbg(hw->dev, "jpg_ckg_eb clk_prepare_enable ok.\n");

	if (hw->hw_id == JPG_HW_ID_SHARKL3) {
		ret = clk_set_parent(hw->clk_ahb_vsp,
				     hw->clk_ahb_vsp_parent);
		if (ret) {
			dev_err(hw->dev,
				"clock[%s]: clk_set_parent() failed!\n",
				"ahb_parent_clk");
			goto err_vsp;
		}
		ret = clk_prepare(hw->clk_ahb_vsp);
		if (ret)
			goto err_ahb;
		ret = clk_enable(hw->clk_ahb_vsp);
		if (ret) {
			clk_unprepare(hw->clk_ahb_vsp);
			goto err_ahb;
		}
		dev_info(hw->dev, "clk_ahb_vsp: clk_prepare_enable ok.\n");

		ret = clk_set_parent(hw->clk_emc_vsp,
				     hw->clk_emc_vsp_parent);
		if (ret) {
			dev_err(hw->dev,
				"clock[%s]: clk_set_parent() failed!\n",
				"emc_parent_clk");
			goto err_emc;
		}
		ret = clk_prepare(hw->clk_emc_vsp);
		if (ret)
			goto err_emc_pe;
		ret = clk_enable(hw->clk_emc_vsp);
		if (ret) {
			clk_unprepare(hw->clk_emc_vsp);
			goto err_emc_pe;
		}
		dev_dbg(hw->dev, "clk_emc_vsp: clk_prepare_enable ok.\n");
	}

	clk = hw->clk_vsp_mq_ahb_eb;
	if (clk) {
		ret = clk_prepare(clk);
		if (ret)
			goto err_vsp_mq;
		ret = clk_enable(clk);
		if (ret) {
			clk_unprepare(clk);
			goto err_vsp_mq;
		}
		dev_dbg(hw->dev, "jpg clk_vsp_mq_ahb_eb clk_prepare_enable ok.\n");
	}

	ret = clk_set_parent(hw->jpg_clk, hw->clk_src_parent);
	if (ret) {
		dev_err(hw->dev, "clock[%s]: clk_set_parent() failed!\n",
			"clk_jpg");
		goto err_jpg_clk;
	}
	ret = clk_set_parent(hw->jpg_clk, hw->clk_src);
	if (ret) {
		dev_err(hw->dev, "clock[%s]: clk_set_parent() failed!\n",
			"clk_jpg");
		goto err_jpg_clk;
	}

	clk = hw->jpg_clk;
	ret = clk_prepare(clk);
	if (ret)
		goto err_jpg_clk_pe;
	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		goto err_jpg_clk_pe;
	}
	dev_info(hw->dev, "jpg_clk clk_prepare_enable ok.\n");
	return 0;

err_jpg_clk_pe:
	dev_err(hw->dev, "jpg_clk clk_prepare_enable failed!\n");
err_jpg_clk:
	if (hw->hw_id == JPG_HW_ID_PIKE2 && hw->clk_vsp_mq_ahb_eb) {
		clk_disable(hw->clk_vsp_mq_ahb_eb);
		clk_unprepare(hw->clk_vsp_mq_ahb_eb);
	}
err_vsp_mq:
	dev_err(hw->dev, "clk_vsp_mq_ahb_eb: clk_prepare_enable failed!\n");
	if (hw->hw_id == JPG_HW_ID_SHARKL3 && hw->clk_emc_vsp) {
		clk_disable(hw->clk_emc_vsp);
		clk_unprepare(hw->clk_emc_vsp);
	}
err_emc_pe:
	dev_err(hw->dev, "clk_emc_vsp: clk_prepare_enable failed!\n");
err_emc:
	if (hw->hw_id == JPG_HW_ID_SHARKL3 && hw->clk_ahb_vsp) {
		clk_disable(hw->clk_ahb_vsp);
		clk_unprepare(hw->clk_ahb_vsp);
	}
err_ahb:
	dev_err(hw->dev, "clk_ahb_vsp: clk_prepare_enable failed!\n");
	clk_disable(hw->jpg_ckg_eb);
	clk_unprepare(hw->jpg_ckg_eb);
err_ckg:
	dev_err(hw->dev, "jpg_ckg_eb prepare_enable failed!\n");
	clk_disable(hw->jpg_dev_eb);
	clk_unprepare(hw->jpg_dev_eb);
err_dev:
	dev_err(hw->dev, "jpg_dev_eb clk_prepare_enable failed!\n");
	if (hw->hw_id == JPG_HW_ID_SHARKL3 && hw->clk_aon_jpg_emc_eb) {
		clk_disable(hw->clk_aon_jpg_emc_eb);
		clk_unprepare(hw->clk_aon_jpg_emc_eb);
	}
err_aon:
	dev_err(hw->dev, "clk_aon_jpg_emc_eb clk_enable failed!\n");
	clk_disable(hw->jpg_domain_eb);
	clk_unprepare(hw->jpg_domain_eb);
err_domain:
	dev_err(hw->dev, "jpg jpg_domain_eb clk_prepare_enable failed!\n");
	return ret;
}

static void jpg_clk_disable(struct sprd_jpg_hw *hw)
{
	if (hw->hw_id == JPG_HW_ID_SHARKL3) {
		if (hw->clk_ahb_vsp) {
			clk_disable(hw->clk_ahb_vsp);
			clk_unprepare(hw->clk_ahb_vsp);
		}
		if (hw->clk_emc_vsp) {
			clk_disable(hw->clk_emc_vsp);
			clk_unprepare(hw->clk_emc_vsp);
		}
	}

	clk_disable(hw->jpg_clk);
	clk_unprepare(hw->jpg_clk);

	if (hw->hw_id == JPG_HW_ID_SHARKL3 && hw->clk_aon_jpg_emc_eb) {
		clk_disable(hw->clk_aon_jpg_emc_eb);
		clk_unprepare(hw->clk_aon_jpg_emc_eb);
	}

	if (hw->clk_vsp_mq_ahb_eb) {
		clk_disable(hw->clk_vsp_mq_ahb_eb);
		clk_unprepare(hw->clk_vsp_mq_ahb_eb);
	}

	clk_disable(hw->jpg_ckg_eb);
	clk_unprepare(hw->jpg_ckg_eb);
	clk_disable(hw->jpg_dev_eb);
	clk_unprepare(hw->jpg_dev_eb);
	clk_disable(hw->jpg_domain_eb);
	clk_unprepare(hw->jpg_domain_eb);
}

/*
 * jpg_poll_mbio_vlc_done() - wait for a MBIO (arg 3) or VLC (arg 1) done irq
 *
 * Returns JPG_POLL_MBIO_OK / JPG_POLL_VLC_OK when the block reported done,
 * JPG_POLL_TIMEOUT on timeout or signal, JPG_POLL_BAD_ARG for other arguments.
 */
static int jpg_poll_mbio_vlc_done(struct sprd_jpg_hw *hw, int arg)
{
	int ret = 0;
	long timeout;

	dev_dbg(hw->dev, "jpg_poll_begin\n");

	if (arg == JPG_WAIT_MBIO) {
		if (!hw->mbio_done) {
			timeout = wait_event_interruptible_timeout(
					hw->wait_queue_work_MBIO, hw->mbio_done,
					JPG_WAIT_TIMEOUT);
			if (timeout == -ERESTARTSYS) {
				dev_err(hw->dev, "jpg error start -ERESTARTSYS\n");
				goto err_clear;
			}
			if (!timeout) {
				dev_err(hw->dev, "jpg error start  timeout\n");
				dev_err(hw->dev, "jpg_int_status %x",
					readl(hw->base + JPG_REG_DEBUG_INT));
				goto err_clear;
			}
		}
		ret = JPG_POLL_MBIO_OK;
		hw->mbio_done = 0;
		hw->int_status_bits &= ~JPG_STAT_MBIO;
	} else if (arg == JPG_WAIT_VLC) {
		if (!hw->vlc_done) {
			timeout = wait_event_interruptible_timeout(
					hw->wait_queue_work_VLC, hw->vlc_done,
					JPG_WAIT_TIMEOUT);
			if (timeout == -ERESTARTSYS) {
				dev_err(hw->dev, "jpg error start -ERESTARTSYS\n");
				goto err_clear;
			}
			if (!timeout) {
				dev_err(hw->dev, "jpg error start  timeout\n");
				dev_err(hw->dev, "jpg_int_status %x",
					readl(hw->base + JPG_REG_DEBUG_INT));
				goto err_clear;
			}
		}
		ret = JPG_POLL_VLC_OK;
		hw->vlc_done = 0;
		hw->int_status_bits &= ~JPG_STAT_VLC;
	} else {
		dev_err(hw->dev, "JPG_ACQUAIRE_MBIO_DONE error arg");
		return JPG_POLL_BAD_ARG;
	}

	dev_dbg(hw->dev, "jpg_poll_end\n");
	return ret;

err_clear:
	writel(JPG_INT_CLR_ALL, hw->base + JPG_REG_INT_CLR);
	return JPG_POLL_TIMEOUT;
}

/* ------------------------------------------------------------------------- */
/* mmap / file operations                                                    */
/* ------------------------------------------------------------------------- */

static int jpg_nocache_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	int ret;

	if (size > JPG_MMAP_MAX_SIZE) {
		printk(KERN_ERR "sprd-jpg: %s, need:%lx should be:%x ",
		       "jpg_nocache_mmap", size, JPG_MMAP_MAX_SIZE);
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = hw_dev.phys >> PAGE_SHIFT;

	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
			      vma->vm_page_prot);
	if (ret) {
		dev_err(hw_dev.dev, "%s failed\n", "jpg_nocache_mmap");
		return -EAGAIN;
	}

	dev_info(hw_dev.dev, "mmap %x,%x,%x,%lx\n", 0xc, vma->vm_start,
		 (unsigned int)size, hw_dev.phys);
	return 0;
}

static int jpg_open(struct inode *inode, struct file *file)
{
	struct jpg_file *jpg_file;

	jpg_file = kmalloc(sizeof(*jpg_file), GFP_KERNEL);
	file->private_data = jpg_file;
	jpg_file->sem_held = 0;
	jpg_file->hw = &hw_dev;
	/* the binary leaves jpg_file->clk_on uninitialised (vendor bug) */

	hw_dev.mbio_done = 0;
	hw_dev.vlc_done = 0;
	hw_dev.bsm_done = 0;

	sprd_glb_mm_pw_on_cfg();
	__pm_runtime_resume(hw_dev.dev, RPM_ACTIVE);

	dev_info(hw_dev.dev, "jpg pw_on: ret %d", 0);
	dev_info(hw_dev.dev, "%s ret %d jpg_fp %p\n", "jpg_open", 0, jpg_file);
	return 0;
}

static int jpg_release(struct inode *inode, struct file *file)
{
	struct jpg_file *jpg_file = file->private_data;

	if (!jpg_file) {
		dev_err(hw_dev.dev, "%s,%d jpg_fp NULL !\n", "jpg_release",
			0x225);
		return -EINVAL;
	}

	if (jpg_file->clk_on) {
		dev_err(hw_dev.dev, "error occurred and close clock\n");
		jpg_clk_disable(&hw_dev);
		jpg_file->clk_on = 0;
	}

	if (jpg_file->sem_held) {
		dev_err(hw_dev.dev, "error occurred and up jpg_mutex\n");
		up(&hw_dev.sem);
	}

	pm_runtime_mark_last_busy(hw_dev.dev);
	sprd_glb_mm_pw_off_cfg();
	__pm_runtime_idle(hw_dev.dev, RPM_ACTIVE);

	dev_info(hw_dev.dev, "jpg pw_off: ret %d", 0);
	dev_info(hw_dev.dev, "%s %p\n", "jpg_release", jpg_file);

	kfree(file->private_data);
	return 0;
}

/* ------------------------------------------------------------------------- */
/* interrupt handling                                                        */
/* ------------------------------------------------------------------------- */

static irqreturn_t jpg_isr(int irq, void *dev_id)
{
	u32 status;
	u32 iommu_status;

	/*
	 * The binary dereferences hw_dev.jpg_file without a NULL check; a
	 * spurious interrupt before JPG_ACQUIRE_MUTEX therefore crashes on the
	 * real device.  Kept, but guarded so the reconstruction is testable.
	 */
	if (!hw_dev.jpg_file || !hw_dev.jpg_file->clk_on) {
		__pm_stay_awake(&jpg_wakelock);
		return IRQ_WAKE_THREAD;
	}

	status = readl(hw_dev.base + JPG_REG_INT_STATUS);
	if (status & JPG_INT_ALL) {
		if (status & JPG_INT_MBIO) {
			hw_dev.mbio_done = 1;
			hw_dev.int_status_bits |= JPG_STAT_MBIO;
			__wake_up(&hw_dev.wait_queue_work_MBIO, TASK_NORMAL, 1,
				  NULL);
			writel(JPG_INT_MBIO, hw_dev.base + JPG_REG_INT_CLR);
			dev_info(hw_dev.dev, "%s MBIO\n\n", "jpg_isr");
		}
		if (status & JPG_INT_BSM) {
			hw_dev.bsm_done = 1;
			hw_dev.int_status_bits |= JPG_STAT_BSM;
			__wake_up(&hw_dev.wait_queue_work_BSM, TASK_NORMAL, 1,
				  NULL);
			writel(JPG_INT_BSM, hw_dev.base + JPG_REG_INT_CLR);
			dev_err(hw_dev.dev, "%s BSM\n", "jpg_isr");
		}
		if (status & JPG_INT_VLC) {
			hw_dev.vlc_done = 1;
			hw_dev.int_status_bits |= JPG_STAT_VLC;
			__wake_up(&hw_dev.wait_queue_work_VLC, TASK_NORMAL, 1,
				  NULL);
			writel(JPG_INT_VLC, hw_dev.base + JPG_REG_INT_CLR);
			/*
			 * The binary contains a dev_dbg("%s VLC\n", "jpg_isr")
			 * block here that no branch reaches (dead code emitted at
			 * the end of jpg_isr).  Kept for the string table only.
			 */
		}
	}

	if (hw_dev.hw_id >= JPG_HW_ID_IOMMU_INT) {
		iommu_status = readl(hw_dev.base + JPG_REG_IOMMU_INT);
		if (iommu_status)
			writel(iommu_status, hw_dev.base + JPG_REG_IOMMU_INT_CLR);
		dev_info(hw_dev.dev, "%s iommu status 0x%x\n", "jpg_isr",
			 iommu_status);
	}

	return IRQ_HANDLED;
}

static irqreturn_t jpg_isr_thread(int irq, void *dev_id)
{
	u32 status;
	u32 iommu_status;

	if (!jpg_clk_enable(&hw_dev)) {
		status = readl(hw_dev.base + JPG_REG_INT_STATUS);
		if (status)
			writel(status, hw_dev.base + JPG_REG_INT_CLR);

		if (hw_dev.hw_id >= JPG_HW_ID_IOMMU_INT) {
			iommu_status = readl(hw_dev.base + JPG_REG_IOMMU_INT);
			dev_info(hw_dev.dev, "jpg_iommu_int raw 0x%x",
				 iommu_status);
			if (iommu_status)
				writel(iommu_status,
				       hw_dev.base + JPG_REG_IOMMU_INT_CLR);
		}

		jpg_clk_disable(&hw_dev);
	}

	__pm_relax(&jpg_wakelock);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------------- */
/* iova management                                                           */
/* ------------------------------------------------------------------------- */

/*
 * The binary reads the Unisoc dma-buf private pointer from dma_buf+0x88 and the
 * buffer size from priv+0x28, then hands "buf" to sprd_iommu_map() which looks
 * the sg_table up itself (the .table member stays NULL).
 */
static int jpg_get_iova(struct sprd_jpg_hw *hw, struct jpg_iova *info,
			void __user *arg)
{
	struct sprd_iommu_map_data mdata = { };
	struct sprd_iommu_unmap_data umdata = { };
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct jpg_iova_node *node;
	void *buf;
	int ret;

	if (sprd_iommu_attach_device(hw->dev)) {
		ret = copy_to_user(arg, info, sizeof(*info));
		if (ret) {
			dev_err(hw->dev, "copy_to_user failed, ret %d\n", ret);
			return -EFAULT;
		}
		return 0;
	}

	dmabuf = dma_buf_get(info->fd);
	if (!dmabuf || IS_ERR(dmabuf)) {
		printk(KERN_ERR "sprd-jpg: get dmabuf failed\n");
		/* the binary returns the truncated pointer value here */
		return (int)(long)dmabuf;
	}

	dma_buf_put(dmabuf);

	attach = dma_buf_attach(dmabuf, hw->dev);
	if (!attach || IS_ERR(attach)) {
		printk(KERN_ERR "sprd-jpg: Failed to attach dmabuf=%p\n",
		       dmabuf);
		return (int)(long)attach;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (!sgt || IS_ERR(sgt)) {
		printk(KERN_ERR "sprd-jpg: Failed to map attachment=%p\n",
		       dmabuf);
		ret = (int)(long)sgt;
		goto detach;
	}

	buf = *(void **)((char *)dmabuf + 0x88);
	mdata.buf = buf;
	mdata.iova_size = *(size_t *)((char *)buf + 0x28);
	mdata.ch_type = JPG_IOVA_PROT;

	ret = sprd_iommu_map(hw->dev, &mdata);
	if (ret) {
		printk(KERN_ERR
		       "sprd-jpg: jpg iommu map failed, ret=%d, map_size=%zu\n",
		       ret, mdata.iova_size);
		goto unmap_attachment;
	}

	pr_debug("sprd-jpg: jpg iommu map success iova addr=%llu size=%llu\n",
		 (unsigned long long)mdata.iova_addr,
		 (unsigned long long)mdata.iova_size);

	mutex_lock(&hw->iova_lock);

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		mutex_unlock(&hw->iova_lock);
		printk(KERN_ERR "sprd-jpg: fatal error! kzalloc fail!\n");
		umdata.iova_addr = mdata.iova_addr;
		umdata.iova_size = mdata.iova_size;
		umdata.ch_type = JPG_IOVA_PROT;
		ret = sprd_iommu_unmap(hw->dev, &umdata);
		if (ret)
			printk(KERN_ERR
			       "sprd-jpg: sprd_iommu_unmap failed, ret=%d, addr&size: 0x%lx 0x%zx\n",
			       ret, umdata.iova_addr, umdata.iova_size);
		/* vendor returns the unmap result (usually 0) here */
		goto unmap_attachment;
	}

	node->fd = info->fd;
	node->iova_addr = mdata.iova_addr;
	node->size = mdata.iova_size;
	node->dmabuf = dmabuf;
	node->attach = attach;
	node->sgt = sgt;
	list_add(&node->node, &hw->iova_list);

	mutex_unlock(&hw->iova_lock);

	info->size = mdata.iova_size;
	info->iova_addr = mdata.iova_addr;

	ret = copy_to_user(arg, info, sizeof(*info));
	if (!ret) {
		pr_debug("sprd-jpg: suceess to add map_node(iova_addr=%llu, size=%llu)\n",
			 info->size, info->iova_addr);
		return 0;
	}

	printk(KERN_ERR "sprd-jpg: fatal error! copy_to_user failed, ret=%d\n",
	       ret);

	mutex_lock(&hw->iova_lock);
	list_del(&node->node);
	kfree(node);
	mutex_unlock(&hw->iova_lock);

	umdata.iova_addr = mdata.iova_addr;
	umdata.iova_size = mdata.iova_size;
	umdata.ch_type = JPG_IOVA_PROT;
	ret = sprd_iommu_unmap(hw->dev, &umdata);
	if (ret)
		printk(KERN_ERR
		       "sprd-jpg: sprd_iommu_unmap failed, ret=%d, addr&size: 0x%lx 0x%zx\n",
		       ret, umdata.iova_addr, umdata.iova_size);

unmap_attachment:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
detach:
	dma_buf_detach(dmabuf, attach);
	return ret;
}

static int jpg_free_iova(struct sprd_jpg_hw *hw, struct jpg_iova *info)
{
	struct sprd_iommu_unmap_data umdata = { };
	struct jpg_iova_node *node;
	int ret;

	if (sprd_iommu_attach_device(hw->dev))
		return 0;

	mutex_lock(&hw->iova_lock);

	list_for_each_entry(node, &hw->iova_list, node) {
		if (node->iova_addr == info->iova_addr)
			goto found;
	}

	printk(KERN_ERR
	       "sprd-jpg: fatal error! not find node(iova_addr=%llu, size=%llu)\n",
	       info->size, info->iova_addr);
	mutex_unlock(&hw->iova_lock);
	return -EFAULT;

found:
	pr_debug("sprd-jpg: success to find node(iova_addr=%llu, size=%llu)\n",
		 (unsigned long long)info->size,
		 (unsigned long long)info->iova_addr);
	umdata.iova_addr = info->iova_addr;
	umdata.iova_size = node->size;
	umdata.ch_type = JPG_IOVA_PROT;
	list_del(&node->node);
	mutex_unlock(&hw->iova_lock);

	ret = sprd_iommu_unmap(hw->dev, &umdata);
	if (ret) {
		printk(KERN_ERR
		       "sprd-jpg: sprd_iommu_unmap failed: ret=%d, iova_addr=%llu, size=%llu\n",
		       ret, info->size, info->iova_addr);
		return ret;
	}

	dma_buf_unmap_attachment(node->attach, node->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(node->dmabuf, node->attach);
	kfree(node);
	pr_debug("sprd-jpg: sprd_iommu_unmap success: iova_addr=%llu size=%llu\n",
		 (unsigned long long)info->size,
		 (unsigned long long)info->iova_addr);
	return 0;
}

/* ------------------------------------------------------------------------- */
/* ioctl                                                                     */
/* ------------------------------------------------------------------------- */

static long jpg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct jpg_file *jpg_file = file->private_data;
	struct jpg_iova info;
	long timeout;
	int level;
	int i;
	u32 val;

	if (!jpg_file) {
		dev_err(hw_dev.dev, "%s,%d jpg_fp NULL !\n", "jpg_ioctl", 0x151);
		return -EINVAL;
	}

	switch (cmd) {
	case JPG_ENABLE:
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_ENABLE\n");
		if (!jpg_clk_enable(&hw_dev))
			jpg_file->clk_on = 1;
		return 0;

	case JPG_DISABLE:
		if (jpg_file->clk_on == 1)
			jpg_clk_disable(&hw_dev);
		jpg_file->clk_on = 0;
		dev_info(hw_dev.dev, "jpg ioctl JPG_DISABLE");
		return 0;

	case JPG_ACQUIRE_MUTEX:
		if (down_timeout(&hw_dev.sem, JPG_SEM_TIMEOUT)) {
			dev_err(hw_dev.dev, "jpg error timeout\n");
			return -EBUSY;
		}
		jpg_file->sem_held = 1;
		hw_dev.jpg_file = jpg_file;
		return 0;

	case JPG_RELEASE:
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_RELEASE\n");
		if (jpg_file->sem_held == 1) {
			jpg_file->sem_held = 0;
			up(&hw_dev.sem);
		}
		return 0;

	case JPG_START:
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_START\n");
		if (!hw_dev.vlc_done) {
			timeout = wait_event_interruptible_timeout(
					hw_dev.wait_queue_work_VLC,
					hw_dev.vlc_done, JPG_WAIT_TIMEOUT);
			if (timeout == -ERESTARTSYS) {
				dev_err(hw_dev.dev,
					"jpg error start -ERESTARTSYS\n");
				hw_dev.int_status_bits |= JPG_STAT_ERR_RESTART;
				writel(JPG_INT_CLR_ALL,
				       hw_dev.base + JPG_REG_INT_CLR);
				level = -EINVAL;
			} else if (!timeout) {
				dev_err(hw_dev.dev,
					"jpg error start  timeout\n");
				hw_dev.int_status_bits |= JPG_STAT_ERR_TIMEOUT;
				writel(JPG_INT_CLR_ALL,
				       hw_dev.base + JPG_REG_INT_CLR);
				level = -ETIMEDOUT;
			} else {
				level = 0;
			}
		} else {
			level = 0;
		}
		put_user((int)hw_dev.int_status_bits, (int __user *)arg);
		hw_dev.mbio_done = 0;
		hw_dev.vlc_done = 0;
		hw_dev.bsm_done = 0;
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_START end\n");
		return level;

	case JPG_ACQUAIRE_MBIO_DONE:
		return jpg_poll_mbio_vlc_done(&hw_dev, (int)arg);

	case JPG_IOMMU_ATTACH:
		return sprd_iommu_attach_device(hw_dev.dev);

	case JPG_RESET:
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_RESET\n");
		regmap_update_bits_base(regs[1].map, regs[1].offset,
					regs[1].mask, regs[1].mask, NULL,
					false, false);
		regmap_update_bits_base(regs[1].map, regs[1].offset,
					regs[1].mask, 0, NULL, false, false);
		if (hw_dev.qos_found) {
			val = (qos_cfg.arqos_low << 4) |
			      (qos_cfg.arqos_high << 8) | qos_cfg.awqos;
			writel(val, hw_dev.base + qos_cfg.offset);
		}
		return 0;

	case JPG_CONFIG_FREQ:
		if (get_user(level, (int __user *)arg)) {
			dev_err(hw_dev.dev, "copy ummapdata failed, ret %d",
				-EFAULT);
			return -EFAULT;
		}
		hw_dev.freq_level = level;
		if (level >= hw_dev.freq_level_num) {
			printk(KERN_INFO "sprd-jpg: set freq_level to 0\n");
			level = 0;
		}
		hw_dev.clk_src = hw_dev.clk_map[level].clk;
		dev_info(hw_dev.dev, "JPG_CONFIG_FREQ %d", hw_dev.freq_level);
		return 0;

	case JPG_GET_FREQ:
		dev_dbg(hw_dev.dev, "jpg ioctl JPG_GET_FREQ %d", 0);
		{
			unsigned long rate = clk_get_rate(hw_dev.jpg_clk);

			if (!hw_dev.freq_level_num) {
				i = 0;
			} else {
				for (i = 0; i < hw_dev.freq_level_num; i++) {
					if (hw_dev.clk_map[i].rate == rate)
						break;
				}
				if (i == hw_dev.freq_level_num)
					i = 0;
			}
		}
		if (put_user(i, (int __user *)arg)) {
			dev_err(hw_dev.dev, "copy mapdata failed, ret %d",
				-EFAULT);
			return -EFAULT;
		}
		dev_info(hw_dev.dev, "jpg ioctl JPG_GET_FREQ %d", i);
		return i;

	case JPG_VERSION:
		dev_dbg(hw_dev.dev, "jpg version -enter\n");
		put_user((int)hw_dev.hw_id, (int __user *)arg);
		return 0;

	case JPG_GET_IOVA:
		if (copy_from_user(&info, (void __user *)arg, sizeof(info))) {
			dev_err(hw_dev.dev, "copy mapdata failed, ret %d",
				-EFAULT);
			return -EFAULT;
		}
		i = jpg_get_iova(&hw_dev, &info, (void __user *)arg);
		dev_info(hw_dev.dev, "JPG_GET_IOVA end");
		return i;

	case JPG_FREE_IOVA:
		if (copy_from_user(&info, (void __user *)arg, sizeof(info))) {
			dev_err(hw_dev.dev, "copy ummapdata failed, ret %d",
				-EFAULT);
			return -EFAULT;
		}
		i = jpg_free_iova(&hw_dev, &info);
		dev_info(hw_dev.dev, "JPG_FREE_IOVA end");
		return i;

	default:
		break;
	}

	dev_dbg(hw_dev.dev, "%s,%d clk_set_parent() !\n", "jpg_ioctl", 0x15c);
	return -EINVAL;
}

static long compat_jpg_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	if (!file->f_op->unlocked_ioctl)
		return -ENOTTY;

	if (!file->private_data) {
		printk(KERN_ERR
		       "sprd-jpg: %s, jpg_ioctl error occurred, jpg_fp == NULL\n",
		       "compat_jpg_ioctl");
		return -EINVAL;
	}

	return file->f_op->unlocked_ioctl(file, cmd,
					  (unsigned long)compat_ptr(arg));
}

static const struct file_operations jpg_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = jpg_ioctl,
	.compat_ioctl = compat_jpg_ioctl,
	.mmap = jpg_nocache_mmap,
	.open = jpg_open,
	.release = jpg_release,
};

static struct miscdevice jpg_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sprd_jpg",
	.fops = &jpg_fops,
};

/* ------------------------------------------------------------------------- */
/* probe / remove                                                            */
/* ------------------------------------------------------------------------- */

static int jpg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device_node *qos;
	struct resource *res;
	struct clk *clk;
	struct regmap *map;
	u32 args[2];
	unsigned long rate;
	const u32 *drv_data;
	int len;
	int i;
	int count = 0;
	int ret;

	match = of_match_node(of_match_table_jpg, np);
	if (!match) {
		dev_err(&pdev->dev, "failed to find matched id for sprd_jpg");
		return -ENODEV;
	}
	hw_dev.drv_data = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get IORESOURCE_MEM JPG");
		return -EINVAL;
	}

	hw_dev.phys = res->start;
	hw_dev.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw_dev.base)) {
		dev_err(&pdev->dev, "jpg_parse_dt failed");
		return -EINVAL;
	}
	hw_dev.of_node = np;
	hw_dev.dev = &pdev->dev;

	dev_info(hw_dev.dev, "sprd_jpg_phys jpg:  0X%lx",
		 (unsigned long)hw_dev.phys);
	dev_info(hw_dev.dev, "sprd_jpg_virt jpg:  0x%lx",
		 (unsigned long)hw_dev.base);
	dev_info(hw_dev.dev, " hw_dev.irq  0X%x",
		 platform_get_irq(pdev, 0));
	hw_dev.irq = platform_get_irq(pdev, 0);

	map = syscon_regmap_lookup_by_phandle_args(np, "aon-apb-eb-syscon", 2,
						   args);
	if (IS_ERR(map)) {
		dev_err(&pdev->dev, "Read JPG Dts %s regmap fail",
			"aon-apb-eb-syscon");
		memset(&regs[0], 0, sizeof(regs[0]));
	} else {
		regs[0].map = map;
		regs[0].offset = args[0];
		regs[0].mask = args[1];
		dev_info(hw_dev.dev,
			 "JPG syscon[%s]%p, offset 0x%x, mask 0x%x",
			 "aon-apb-eb-syscon", map, args[0], args[1]);
	}

	map = syscon_regmap_lookup_by_phandle_args(np, "reset-syscon", 2, args);
	if (IS_ERR(map)) {
		dev_err(&pdev->dev,
			"Read JPG Dts %s regmap fail", "reset-syscon");
		memset(&regs[1], 0, sizeof(regs[1]));
	} else {
		regs[1].map = map;
		regs[1].offset = args[0];
		regs[1].mask = args[1];
		dev_info(hw_dev.dev,
			 "JPG syscon[%s]%p, offset 0x%x, mask 0x%x",
			 "reset-syscon", map, args[0], args[1]);
	}

	for (i = 0; i < JPG_MAX_CLK_SRC; i++) {
		clk = of_clk_get_by_name(np, jpg_clk_src[i]);
		if (!clk || IS_ERR(clk)) {
			dev_info(hw_dev.dev,
				 "clk %s not found,continue to find next clock",
				 jpg_clk_src[i]);
			continue;
		}
		rate = clk_get_rate(clk);
		clock_name_map[count].rate = rate;
		clock_name_map[count].name = jpg_clk_src[i];
		clock_name_map[count].clk = clk;
		dev_info(hw_dev.dev,
			 "jpg clk in dts file: clk[%d] = (%ld, %s)",
			 count, rate, jpg_clk_src[i]);
		count++;
	}
	hw_dev.clk_map = clock_name_map;

	qos = of_parse_phandle(np, "jpg_qos", 0);
	if (!qos) {
		dev_warn(&pdev->dev, "can't find jpg qos cfg node");
	} else {
		if (of_property_read_variable_u8_array(qos, "awqos",
						       &qos_cfg.awqos, 1, NULL))
			dev_warn(&pdev->dev,
				 "read awqos_low failed, use default");
		if (of_property_read_variable_u8_array(qos, "arqos-low",
						       &qos_cfg.arqos_low, 1,
						       NULL))
			dev_warn(&pdev->dev,
				 "read arqos-low failed, use default");
		if (of_property_read_variable_u8_array(qos, "arqos-high",
						       &qos_cfg.arqos_high, 1,
						       NULL))
			dev_warn(&pdev->dev,
				 "read arqos-high failed, use default");

		dev_info(hw_dev.dev, "%x, %x, %x, %x", qos_cfg.awqos,
			 qos_cfg.arqos_high, qos_cfg.arqos_low, qos_cfg.offset);
		hw_dev.qos_found = 1;
	}

	drv_data = hw_dev.drv_data;
	hw_dev.freq_level = 0;
	hw_dev.hw_id = drv_data[0];
	hw_dev.freq_level_num = drv_data[1];
	qos_cfg.offset = drv_data[2];

	init_waitqueue_head(&hw_dev.wait_queue_work_MBIO);
	hw_dev.mbio_done = 0;
	init_waitqueue_head(&hw_dev.wait_queue_work_VLC);
	hw_dev.vlc_done = 0;
	init_waitqueue_head(&hw_dev.wait_queue_work_BSM);
	hw_dev.bsm_done = 0;
	hw_dev.int_status_bits = 0;

	sema_init(&hw_dev.sem, 1);
	hw_dev.jpg_file = NULL;
	hw_dev.jpg_clk = NULL;
	hw_dev.clk_src = NULL;
	hw_dev.clk_src_parent = NULL;
	hw_dev.jpg_domain_eb = NULL;
	hw_dev.clk_vsp_mq_ahb_eb = NULL;
	hw_dev.clk_aon_jpg_emc_eb = NULL;
	hw_dev.jpg_dev_eb = NULL;
	hw_dev.jpg_ckg_eb = NULL;

	mutex_init(&hw_dev.iova_lock);
	INIT_LIST_HEAD(&hw_dev.iova_list);

	jpg_get_mm_clk(&hw_dev);

	ret = misc_register(&jpg_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"cannot register miscdev on minor=%d (%d)",
			MISC_DYNAMIC_MINOR, ret);
		return ret;
	}

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
		dev_info(&pdev->dev, "jpg: set dma mask as 64bit");
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	} else {
		dev_err(&pdev->dev, "jpg: failed to set dma mask!");
	}

	ret = devm_request_threaded_irq(&pdev->dev, hw_dev.irq, jpg_isr,
					jpg_isr_thread, 0, "JPG", &hw_dev);
	if (ret) {
		dev_err(&pdev->dev, "jpg: failed to request irq!");
		misc_deregister(&jpg_dev);
		return -EINVAL;
	}

	__pm_runtime_set_status(&pdev->dev, RPM_ACTIVE);
	pm_runtime_enable(&pdev->dev);
	dev_err(&pdev->dev, "sprd_jpg probe with genpd");

	len = 0;
	(void)len;
	return 0;
}

static int jpg_remove(struct platform_device *pdev)
{
	misc_deregister(&jpg_dev);
	free_irq(hw_dev.irq, &hw_dev);

	if (hw_dev.clk_src)
		clk_put(hw_dev.clk_src);
	if (hw_dev.jpg_clk)
		clk_put(hw_dev.jpg_clk);
	if (hw_dev.jpg_ckg_eb)
		clk_put(hw_dev.jpg_ckg_eb);
	if (hw_dev.jpg_dev_eb)
		clk_put(hw_dev.jpg_dev_eb);
	if (hw_dev.jpg_domain_eb)
		clk_put(hw_dev.jpg_domain_eb);
	if (hw_dev.clk_vsp_mq_ahb_eb)
		clk_put(hw_dev.clk_vsp_mq_ahb_eb);

	return 0;
}

static struct platform_driver jpg_driver = {
	.probe = jpg_probe,
	.remove = jpg_remove,
	.driver = {
		.name = "sprd-jpg",
		.owner = THIS_MODULE,
		.of_match_table = of_match_table_jpg,
	},
};

module_platform_driver(jpg_driver);

MODULE_DESCRIPTION("SPRD JPG Driver");
MODULE_LICENSE("GPL v2");
