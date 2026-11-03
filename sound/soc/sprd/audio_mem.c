// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc (Spreadtrum) audio memory manager.
 *
 * This file was reconstructed from the vendor binary module
 *     audio_mem.ko
 * shipped in vendor_boot_a.bin of the Sunelan Q968 (Unisoc UMS9157 / T157,
 * board "ums9157-2h10"), Android 13, kernel 5.4.210-android12-9-g96901d45d5a5.
 *
 * The vendor build path baked into the binary is:
 *   /home/foxconn/sdc1/T157/dailybuild_pfat_YQ5_new01/bsp/modules/kernel5.4/audio_driver/sprd_audio/audiomem/audio_mem.c
 *
 * Module layout of the binary (for reference):
 *   .text=0x1df0 .rodata=0x1428 .data=0xf0 .bss=0x104
 *   modinfo: description=audio memory manager driver  license=GPL v2
 *            alias=of:N*T*Cunisoc,sharkl3-audio-mem (and 4 more)
 *
 * Exported symbols: audio_mem_alloc(), audio_mem_free(), audio_mem_vmap(),
 *                   audio_mem_unmap(), audio_addr_ap2dsp().
 *
 * The driver owns the audio reserved memory layout: it reads the addresses and
 * sizes of every audio buffer from the device tree into a 22 entry table
 * (indexed by "mem type"), hands out virtual mappings for those physical
 * buffers (audio_mem_vmap) and manages one shared-memory pool with a genalloc
 * pool for the DSP message buffers (audio_smem_alloc/free).
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/genalloc.h>

/* ------------------------------------------------------------------------- */
/* Memory types                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Bits of the "mem type" space, as used by the vendor binary:
 *   the driver keeps an array of {addr, size} pairs indexed by mem type and the
 *   AP<->DSP address conversion decides per type whether the buffer has to be
 *   translated (IRAM window) or only shifted by the DDR map offset.
 */
enum audio_mem_type {
	AUDIO_MEM_SHM_AUD_STR		= 0,
	AUDIO_MEM_SHM_DSP_VBC		= 1,
	AUDIO_MEM_SHM_NXP		= 2,
	AUDIO_MEM_SHM_REG_DUMP		= 3,
	AUDIO_MEM_RESERVED4		= 4,
	AUDIO_MEM_DDR32_DMA		= 5,
	AUDIO_MEM_CMDADDR		= 6,
	AUDIO_MEM_SMSG_ADDR		= 7,
	AUDIO_MEM_OFFLOAD_ADDR		= 8,
	AUDIO_MEM_DDR32_DSPMEMDUMP	= 9,
	AUDIO_MEM_DSPLOG		= 10,
	AUDIO_MEM_RESERVED11		= 11,
	AUDIO_MEM_IRAM_PHY_ADDR		= 12,
	AUDIO_MEM_IRAM_NORMAL		= 13,
	AUDIO_MEM_IRAM_DEEPBUF		= 14,
	AUDIO_MEM_IRAM_4ARM7		= 15,
	AUDIO_MEM_CM4_SHMEM		= 16,
	AUDIO_MEM_NORMAL_CAP_NODE1	= 17,
	AUDIO_MEM_NORMAL_CAP_NODE2	= 18,
	AUDIO_MEM_NORMAL_CAP_DATA	= 19,
	AUDIO_MEM_DDR32			= 20,
	AUDIO_MEM_IRAM_AUDCP_AON	= 21,
	AUDIO_MEM_TYPE_MAX
};

/* Platform flavours, taken from of_device_id.data */
enum audio_mem_platform {
	AUDIO_MEM_PLAT_SHARKL2	= 0,
	AUDIO_MEM_PLAT_WHALE2	= 1,
	AUDIO_MEM_PLAT_SHARKL5	= 2,
	AUDIO_MEM_PLAT_ORCA	= 3,
};

/* One entry of the audio memory table (8 bytes in .bss) */
struct audio_mem_entry {
	u32 addr;
	u32 size;
};

/* Allocation record kept in the shared memory list (32 bytes) */
struct audio_smem_node {
	struct list_head node;		/* 0x00 */
	u64 owner;			/* 0x10: current at allocation time */
	u32 size;			/* 0x18 */
	u32 addr;			/* 0x1c */
};

/* Mapping record kept in the vmap list (48 bytes) */
struct audio_vmap_node {
	struct list_head node;		/* 0x00 */
	u64 owner;			/* 0x10 */
	void *vaddr;			/* 0x18 */
	void *user_vaddr;		/* 0x20: vaddr + page offset */
	u32 pages;			/* 0x28 */
	u32 reserved;			/* 0x2c */
};

/* The tracking list of the vmap records (24 bytes in .bss) */
static struct {
	struct list_head list;		/* 0x00 */
	spinlock_t lock;		/* 0x10 */
	int count;			/* 0x14 */
} mem_mp;

/* The DSP shared memory pool (40 bytes in .bss) */
static struct {
	struct list_head list;		/* 0x00 */
	spinlock_t lock;		/* 0x10 */
	u32 addr;			/* 0x14 */
	u32 size;			/* 0x18 */
	u32 used;			/* 0x1c */
	struct gen_pool *pool;		/* 0x20 */
} audio_mem_pool;

static struct audio_mem_entry audio_mem[AUDIO_MEM_TYPE_MAX];

static u32 *g_priv_data;
static u32 dma_offset;
static u32 iram_dsp_base;
static u32 iram_ap_base;

/* ------------------------------------------------------------------------- */
/* Virtual mappings of the audio physical buffers                            */
/* ------------------------------------------------------------------------- */

/*
 * void *audio_mem_vmap(phys_addr_t paddr, size_t size, u32 flag)
 *
 * Maps "size" bytes of the audio physical buffer at "paddr" with vm_map_ram()
 * and returns the virtual address of paddr (page offset included).  The record
 * is appended to the tracking list so that audio_mem_unmap() can find it.
 */
void *audio_mem_vmap(phys_addr_t paddr, size_t size, u32 flag)
{
	struct audio_vmap_node *map;
	struct page **pages;
	unsigned long offset = paddr & ~PAGE_MASK;
	unsigned long end = (paddr & PAGE_MASK) + offset + size;
	size_t count = (end + ~PAGE_MASK) >> PAGE_SHIFT;
	unsigned long flags;
	void *vaddr;
	size_t i;

	map = kmalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return NULL;

	if (flag)
		pages = kmalloc_array(count, sizeof(*pages), GFP_KERNEL);
	else
		pages = kmalloc_array(count, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		kfree(map);
		return NULL;
	}

	for (i = 0; i < count; i++)
		pages[i] = pfn_to_page(__phys_to_pfn((paddr & PAGE_MASK) + (i << PAGE_SHIFT)));

	vaddr = vm_map_ram(pages, count, -1, PAGE_KERNEL);
	kfree(pages);

	map->owner = (u64)(uintptr_t)current;
	map->vaddr = vaddr;
	map->user_vaddr = vaddr + offset;
	map->pages = count;

	if (mem_mp.count) {
		spin_lock_irqsave(&mem_mp.lock, flags);
		list_add(&map->node, &mem_mp.list);
		spin_unlock_irqrestore(&mem_mp.lock, flags);
	}

	return map->user_vaddr;
}
EXPORT_SYMBOL(audio_mem_vmap);

/* void audio_mem_unmap(void *vaddr) */
void audio_mem_unmap(void *vaddr)
{
	struct audio_vmap_node *map;
	unsigned long flags;

	if (!mem_mp.count)
		return;

	spin_lock_irqsave(&mem_mp.lock, flags);
	list_for_each_entry(map, &mem_mp.list, node) {
		if (map->user_vaddr != vaddr)
			continue;
		list_del(&map->node);
		spin_unlock_irqrestore(&mem_mp.lock, flags);
		vm_unmap_ram(map->vaddr, map->pages);
		kfree(map);
		return;
	}
	spin_unlock_irqrestore(&mem_mp.lock, flags);
}
EXPORT_SYMBOL(audio_mem_unmap);

/* ------------------------------------------------------------------------- */
/* DSP shared memory pool                                                    */
/* ------------------------------------------------------------------------- */

/*
 * int audio_smem_init(u32 addr, u32 size)
 *
 * Creates the genalloc pool used by audio_smem_alloc() and initialises the two
 * tracking lists.  The pool only hands out page aligned chunks.
 */
int audio_smem_init(u32 addr, u32 size)
{
	int ret;

	audio_mem_pool.addr = addr;
	audio_mem_pool.size = (size + ~PAGE_MASK) & PAGE_MASK;
	audio_mem_pool.used = 0;
	spin_lock_init(&audio_mem_pool.lock);
	INIT_LIST_HEAD(&audio_mem_pool.list);

	audio_mem_pool.pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!audio_mem_pool.pool) {
		printk(KERN_ERR "[Audio:MEM] Failed to create smem gen pool!\n");
		return -1;
	}

	gen_pool_set_algo(audio_mem_pool.pool, gen_pool_best_fit, NULL);
	ret = gen_pool_add(audio_mem_pool.pool, audio_mem_pool.addr,
			   audio_mem_pool.size, -1);
	if (ret) {
		printk(KERN_ERR "[Audio:MEM] Failed to add smem gen pool!\n");
		gen_pool_destroy(audio_mem_pool.pool);
		return -1;
	}

	/* The mapping list is only maintained once the audio smem exists. */
	INIT_LIST_HEAD(&mem_mp.list);
	spin_lock_init(&mem_mp.lock);
	mem_mp.count = 1;

	return 0;
}

/* u32 audio_smem_alloc(u32 size) */
u32 audio_smem_alloc(u32 size)
{
	struct audio_smem_node *node;
	unsigned long flags;
	size_t len = (size + ~PAGE_MASK) & PAGE_MASK;
	u32 addr;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return 0;

	addr = gen_pool_alloc(audio_mem_pool.pool, len);
	if (!addr) {
		printk(KERN_ERR "[Audio:MEM] failed to alloc smem from gen pool\n");
		kfree(node);
		return 0;
	}

	atomic_add(len, (atomic_t *)&audio_mem_pool.used);

	node->size = len;
	node->addr = addr;
	node->owner = (u64)(uintptr_t)current;

	spin_lock_irqsave(&audio_mem_pool.lock, flags);
	list_add(&node->node, &audio_mem_pool.list);
	spin_unlock_irqrestore(&audio_mem_pool.lock, flags);

	return addr;
}

/* void audio_smem_free(u32 addr, u32 size) */
void audio_smem_free(u32 addr, u32 size)
{
	struct audio_smem_node *node;
	unsigned long flags;
	size_t len = (size + ~PAGE_MASK) & PAGE_MASK;

	atomic_sub(len, (atomic_t *)&audio_mem_pool.used);
	gen_pool_free(audio_mem_pool.pool, addr, len);

	spin_lock_irqsave(&audio_mem_pool.lock, flags);
	list_for_each_entry(node, &audio_mem_pool.list, node) {
		if (node->addr != addr)
			continue;
		list_del(&node->node);
		kfree(node);
		break;
	}
	spin_unlock_irqrestore(&audio_mem_pool.lock, flags);
}

/* ------------------------------------------------------------------------- */
/* Public allocation API                                                    */
/* ------------------------------------------------------------------------- */

/*
 * u32 audio_mem_alloc(u32 mem_type, u32 *size)
 *
 * Returns the physical address of the audio buffer "mem_type" and stores its
 * size in *size.  Only the shared memory type is allocated dynamically, every
 * other type is a fixed carve-out coming from the device tree.
 */
u32 audio_mem_alloc(u32 mem_type, u32 *size)
{
	u32 addr = 0;

	if (!size) {
		printk(KERN_ERR "[Audio:MEM] %s failed\n", "audio_mem_alloc");
		return 0;
	}

	if (mem_type == AUDIO_MEM_DDR32_DMA) {
		addr = audio_smem_alloc(*size);
	} else if (mem_type < AUDIO_MEM_TYPE_MAX) {
		addr = audio_mem[mem_type].addr;
		*size = audio_mem[mem_type].size;
	}

	printk(KERN_INFO "[Audio:MEM] %s mem_type=%d addr = %#x, size=%#x\n",
	       "audio_mem_alloc", mem_type, addr, *size);

	return addr;
}
EXPORT_SYMBOL(audio_mem_alloc);

/* void audio_mem_free(u32 mem_type, u32 addr, u32 size) */
void audio_mem_free(u32 mem_type, u32 addr, u32 size)
{
	if (mem_type != AUDIO_MEM_DDR32_DMA)
		return;

	audio_smem_free(addr, size);
}
EXPORT_SYMBOL(audio_mem_free);

/* ------------------------------------------------------------------------- */
/* AP <-> DSP address translation                                            */
/* ------------------------------------------------------------------------- */

/* Vectors used by the per platform conversion below (22 entries) */
#define AUDIO_ADDR_IRAM		0	/* IRAM window translation */
#define AUDIO_ADDR_DMA		1	/* shift by dma_offset */
#define AUDIO_ADDR_UNKNOWN	2	/* unsupported mem type */
#define AUDIO_ADDR_IGNORE	3	/* IRAM_AUDCP_AON: ignore */

static const u8 audio_addr_sharkl2_map[AUDIO_MEM_TYPE_MAX] = {
	AUDIO_ADDR_IRAM, AUDIO_ADDR_IRAM, AUDIO_ADDR_IRAM, AUDIO_ADDR_IRAM,
	AUDIO_ADDR_IRAM, AUDIO_ADDR_DMA, AUDIO_ADDR_IRAM, AUDIO_ADDR_IRAM,
	AUDIO_ADDR_IRAM, AUDIO_ADDR_DMA, AUDIO_ADDR_IRAM, AUDIO_ADDR_UNKNOWN,
	AUDIO_ADDR_UNKNOWN, AUDIO_ADDR_UNKNOWN, AUDIO_ADDR_UNKNOWN,
	AUDIO_ADDR_UNKNOWN, AUDIO_ADDR_UNKNOWN, AUDIO_ADDR_IRAM,
	AUDIO_ADDR_IRAM, AUDIO_ADDR_IRAM, AUDIO_ADDR_UNKNOWN,
	AUDIO_ADDR_IGNORE,
};

/* Type masks of the newer platforms */
#define AUDIO_ADDR_SHARKL5_DMA_MASK	0x1002ff	/* 0..9 */
#define AUDIO_ADDR_SHARKL5_IRAM_MASK	0xe0500		/* 10, 17, 18, 19 */
#define AUDIO_ADDR_ORCA_DMA_MASK	0x1002e0	/* 5, 6, 7, 9 */

/*
 * u32 audio_addr_ap2dsp(u32 mem_type, u32 addr, u32 dir)
 *
 * Converts an AP view address of the audio buffer "mem_type" into the DSP view.
 * "dir" is the transfer direction: for the AP->DSP direction the IRAM windows
 * are swapped and the DDR offset is added, for the DSP->AP direction the
 * opposite happens.
 */
u32 audio_addr_ap2dsp(u32 mem_type, u32 addr, u32 dir)
{
	u32 platform = g_priv_data ? g_priv_data[0] : AUDIO_MEM_PLAT_SHARKL2;
	u32 base, target;

	switch (platform) {
	case AUDIO_MEM_PLAT_ORCA:
		if (mem_type >= AUDIO_MEM_TYPE_MAX ||
		    !(BIT(mem_type) & AUDIO_ADDR_ORCA_DMA_MASK)) {
			printk(KERN_ERR "[Audio:MEM] %s not supported mem_type %d\n",
			       "audio_addr_ap2dsp_orca", mem_type);
			return 0;
		}
		base = dma_offset;
		if (dir & 1)
			base = -base;
		addr += base;
		pr_debug("[Audio:MEM] %s ret_val=%#x\n",
			 "audio_addr_ap2dsp_orca", addr);
		return addr;

	case AUDIO_MEM_PLAT_SHARKL5:
		if (mem_type < AUDIO_MEM_TYPE_MAX &&
		    (BIT(mem_type) & AUDIO_ADDR_SHARKL5_DMA_MASK)) {
			base = dma_offset;
			if (dir & 1)
				base = -base;
			addr += base;
			return addr;
		}
		if (mem_type < AUDIO_MEM_TYPE_MAX &&
		    (BIT(mem_type) & AUDIO_ADDR_SHARKL5_IRAM_MASK))
			goto iram;
		printk(KERN_ERR "[Audio:MEM] %s not supported mem_type %d\n",
		       "audio_addr_ap2dsp_sharkl5", mem_type);
		return 0;

	default:
		if (mem_type >= AUDIO_MEM_TYPE_MAX) {
			printk(KERN_INFO "[Audio:MEM] %s unknown mem_type %d,ret_val =%#x\n",
			       "audio_addr_ap2dsp", mem_type, addr);
			return addr;
		}

		switch (audio_addr_sharkl2_map[mem_type]) {
		case AUDIO_ADDR_IRAM:
iram:
			if (dir & 1) {
				base = iram_dsp_base;
				target = iram_ap_base;
			} else {
				base = iram_ap_base;
				target = iram_dsp_base;
			}
			addr = addr - base + target;
			return addr;

		case AUDIO_ADDR_DMA:
			base = dma_offset;
			if (dir & 1)
				base = -base;
			addr += base;
			return addr;

		case AUDIO_ADDR_IGNORE:
			printk(KERN_INFO "[Audio:MEM] ignroe IRAM_AUDCP_AON\n");
			return 0;

		default:
			printk(KERN_INFO "[Audio:MEM] %s unknown mem_type %d,ret_val =%#x\n",
			       "audio_addr_ap2dsp", mem_type, addr);
			return addr;
		}
	}
}
EXPORT_SYMBOL(audio_addr_ap2dsp);

/* ------------------------------------------------------------------------- */
/* Device tree parsing                                                       */
/* ------------------------------------------------------------------------- */

/* Read "count" u32 cells of "name" into the audio memory table entry "idx" */
static int audio_mem_read_entry(struct device_node *np, const char *name,
				unsigned int idx, const char *err, u32 count)
{
	u32 val[2] = { 0, 0 };
	int ret;

	ret = of_property_read_variable_u32_array(np, name, val, count, 0);
	if (ret < 0) {
		if (err)
			printk(KERN_ERR err, __func__);
		return ret;
	}

	audio_mem[idx].addr = val[0];
	audio_mem[idx].size = count > 1 ? val[1] : 0;

	return 0;
}

#define AUDIO_MEM_ERR_SHM_A	"[Audio:MEM] %s, ERR:Must give me the shmaddr_a addr!\n"
#define AUDIO_MEM_ERR_SHM_B	"[Audio:MEM] %s, ERR:Must give me the shmaddr_b addr!\n"
#define AUDIO_MEM_ERR_SHM_C	"[Audio:MEM] %s, ERR:Must give me the shmaddr_c addr!\n"
#define AUDIO_MEM_ERR_SHM_D	"[Audio:MEM] %s, ERR:Must give me the shmaddr_d addr!\n"
#define AUDIO_MEM_ERR_SMSG	"[Audio:MEM] %s, ERR:Must give me the smsg addr!\n"
#define AUDIO_MEM_ERR_CMD	"[Audio:MEM] %s, ERR:Must give me the smsg cmdaddr!\n"
#define AUDIO_MEM_ERR_OFFLOAD	"[Audio:MEM] %s, ERR:Must give me the offload addr!\n"
#define AUDIO_MEM_ERR_NODE1	"[Audio:MEM] %s, ERR:Must give me the iram sprd,normal-captue-linklilst-node1 addr!\n"
#define AUDIO_MEM_ERR_NODE2	"[Audio:MEM] %s, ERR:Must give me the iram sprd,normal-captue-linklilst-node2 addr!\n"
#define AUDIO_MEM_ERR_DATA	"[Audio:MEM] %s, ERR:Must give me the iram normal capture data addr!\n"
#define AUDIO_MEM_ERR_DMA_OFF	"[Audio:MEM] %s, ERR:Must give me the dma offset addr!\n"
#define AUDIO_MEM_ERR_AP_BASE	"[Audio:MEM] %s, ERR:Must give me the ap_base!\n"
#define AUDIO_MEM_ERR_DSP_BASE	"[Audio:MEM] %s, ERR:Must give me the sprd,dsp_base!\n"
#define AUDIO_MEM_ERR_ARM7	"[Audio:MEM] %s, ERR:Must give me the sprd,iram_4arm7!\n"
#define AUDIO_MEM_ERR_PHY	"[Audio:MEM] %s, ERR:Must give me the sprd,iram-phy-addr!\n"
#define AUDIO_MEM_ERR_NORMAL	"[Audio:MEM] %s, ERR:Must give me the sprd,iram_normal!\n"
#define AUDIO_MEM_ERR_AON	"[Audio:MEM] %s, ERR:Must give me the audio cp aon iram!\n"
#define AUDIO_MEM_ERR_NP	"[Audio:MEM] %s, np is NULL!\n"
#define AUDIO_MEM_ERR_DEV_NP	"[Audio:MEM] ERR: device_node is NULL!\n"
#define AUDIO_MEM_ERR_REGION	"[Audio:MEM] get phandle 'memory-region' failed!\n"
#define AUDIO_MEM_ERR_RES	"[Audio:MEM] of_address_to_resource failed!(%d)\n"
#define AUDIO_MEM_ERR_SMEM_INIT	"[Audio:MEM] %s audio_smem_init failed ret =%d\n"
#define AUDIO_MEM_WARN_CM4	"[Audio:MEM] %s, warn: parse the cm4 share memory addr failed!\n"
#define AUDIO_MEM_WARN_DMA_SIZE	"[Audio:MEM] the size(%#x) of 'sprd,ddr32-dma' is greater than the total size(%#x)\n"
#define AUDIO_MEM_WARN_DUMP_SIZE "[Audio:MEM] the requested size(%#x = %#x + %#x) is greater than the total size(%#x)\n"

/*
 * sharkl2 / sharkl3 (platform 0)
 *
 * The oldest platform only knows the IRAM windows plus the four audio capture
 * buffers; there is no shared memory pool.
 */
static int audio_mem_sharkl2_parse_dt(struct device_node *np)
{
	if (!np) {
		printk(KERN_ERR AUDIO_MEM_ERR_NP, "audio_mem_sharkl2_parse_dt");
		return -1;
	}

	audio_mem_read_entry(np, "sprd,iram_phy_addr", AUDIO_MEM_IRAM_PHY_ADDR,
			     AUDIO_MEM_ERR_PHY, 2);
	audio_mem_read_entry(np, "sprd,iram_normal", AUDIO_MEM_IRAM_NORMAL,
			     AUDIO_MEM_ERR_NORMAL, 2);
	audio_mem_read_entry(np, "sprd,iram_deepbuf", AUDIO_MEM_IRAM_DEEPBUF,
			     "[Audio:MEM] %s, sprd,iram_deepbuf!\n", 2);
	audio_mem_read_entry(np, "sprd,iram_4arm7", AUDIO_MEM_IRAM_4ARM7,
			     AUDIO_MEM_ERR_ARM7, 2);

	audio_mem_read_entry(np, "sprd,offload-addr", AUDIO_MEM_OFFLOAD_ADDR,
			     AUDIO_MEM_ERR_OFFLOAD, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-linklilst-node1",
			     AUDIO_MEM_NORMAL_CAP_NODE1, AUDIO_MEM_ERR_NODE1, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-linklilst-node2",
			     AUDIO_MEM_NORMAL_CAP_NODE2, AUDIO_MEM_ERR_NODE2, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-data",
			     AUDIO_MEM_NORMAL_CAP_DATA, AUDIO_MEM_ERR_DATA, 2);

	printk(KERN_INFO "[Audio:MEM] %s audio iram(OFFLOAD), (NORMAL_CAP NODE1), (NORMAL_CAP NODE2), (NORMAL_CAP DATA)\n",
	       "audio_mem_sharkl2_probe");
	printk(KERN_INFO "[Audio:MEM] ddr32 (addr, size) (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x)\n",
	       audio_mem[AUDIO_MEM_OFFLOAD_ADDR].addr,
	       audio_mem[AUDIO_MEM_OFFLOAD_ADDR].size,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_NODE1].addr,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_NODE1].size,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_NODE2].addr,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_NODE2].size,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_DATA].addr,
	       audio_mem[AUDIO_MEM_NORMAL_CAP_DATA].size);

	return 0;
}

static int audio_mem_sharkl2_probe(struct platform_device *pdev)
{
	int ret = audio_mem_sharkl2_parse_dt(pdev->dev.of_node);

	if (ret)
		printk(KERN_ERR "[Audio:MEM] %s audio_mem_parse_dt failed\n",
		       "audio_mem_sharkl2_probe");

	return ret;
}

/* Fetch a memory-region phandle and turn it into a resource */
static int audio_mem_get_region(struct device_node *np, int index,
				struct resource *res)
{
	struct device_node *node;
	int ret;

	node = of_parse_phandle(np, "memory-region", index);
	if (!node) {
		printk(KERN_ERR AUDIO_MEM_ERR_REGION);
		return -ENODEV;
	}

	ret = of_address_to_resource(node, 0, res);
	if (ret) {
		printk(KERN_ERR AUDIO_MEM_ERR_RES, ret);
		return ret;
	}

	return 0;
}

/* Print the (addr, size) pairs of the ddr32 related entries */
static void audio_mem_dump_ddr32(int pairs)
{
	printk(KERN_INFO "[Audio:MEM] ddr32 (addr, size) (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x)\n",
	       audio_mem[AUDIO_MEM_DDR32].addr, audio_mem[AUDIO_MEM_DDR32].size,
	       audio_mem[AUDIO_MEM_DDR32_DMA].addr, audio_mem[AUDIO_MEM_DDR32_DMA].size,
	       audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].addr,
	       audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].size,
	       audio_mem[AUDIO_MEM_CMDADDR].addr, audio_mem[AUDIO_MEM_CMDADDR].size,
	       audio_mem[AUDIO_MEM_SMSG_ADDR].addr, audio_mem[AUDIO_MEM_SMSG_ADDR].size,
	       audio_mem[AUDIO_MEM_SHM_DSP_SMARTPA].addr,
	       audio_mem[AUDIO_MEM_SHM_DSP_SMARTPA].size,
	       audio_mem[AUDIO_MEM_SHM_REG_DUMP].addr,
	       audio_mem[AUDIO_MEM_SHM_REG_DUMP].size);
	(void)pairs;
}

/* whale2: one memory-region for the whole ddr32 window plus per buffer props */
static int audio_mem_whale2_parse_dt(struct device_node *np)
{
	static const struct {
		const char *name;
		unsigned int idx;
		const char *err;
	} props[] = {
		{ "sprd,cmdaddr", AUDIO_MEM_CMDADDR, AUDIO_MEM_ERR_CMD },
		{ "sprd,smsg-addr", AUDIO_MEM_SMSG_ADDR, AUDIO_MEM_ERR_SMSG },
		{ "sprd,shmaddr-aud-str", AUDIO_MEM_SHM_AUD_STR, AUDIO_MEM_ERR_SHM_A },
		{ "sprd,shmaddr-dsp-vbc", AUDIO_MEM_SHM_DSP_VBC, AUDIO_MEM_ERR_SHM_B },
		{ "sprd,shmaddr-nxp", AUDIO_MEM_SHM_NXP, AUDIO_MEM_ERR_SHM_C },
		{ "sprd,cm4-shmem", AUDIO_MEM_CM4_SHMEM, AUDIO_MEM_WARN_CM4 },
		{ "sprd,shmaddr-reg-dump", AUDIO_MEM_SHM_REG_DUMP, AUDIO_MEM_ERR_SHM_D },
		{ "sprd,offload-addr", AUDIO_MEM_OFFLOAD_ADDR, AUDIO_MEM_ERR_OFFLOAD },
		{ "sprd,dsplog", AUDIO_MEM_DSPLOG, AUDIO_MEM_ERR_OFFLOAD },
		{ "sprd,normal-captue-linklilst-node1", AUDIO_MEM_NORMAL_CAP_NODE1, AUDIO_MEM_ERR_NODE1 },
		{ "sprd,normal-captue-linklilst-node2", AUDIO_MEM_NORMAL_CAP_NODE2, AUDIO_MEM_ERR_NODE2 },
		{ "sprd,normal-captue-data", AUDIO_MEM_NORMAL_CAP_DATA, AUDIO_MEM_ERR_DATA },
	};
	struct resource res;
	u32 val[2];
	u32 total;
	int i, ret;

	if (!np) {
		printk(KERN_ERR AUDIO_MEM_ERR_NP, "audio_mem_whale2_parse_dt");
		return -1;
	}

	ret = audio_mem_get_region(np, 0, &res);
	if (ret)
		return ret;

	printk(KERN_INFO "[Audio:MEM] %s res.start=%#lx, res.end=%#lx\n",
	       "audio_mem_whale2_parse_dt", (unsigned long)res.start,
	       (unsigned long)res.end);

	total = res.end - res.start + 1;

	/* ddr32 dma window */
	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dma", val, 1, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dma' failed!(%d)\n", ret);
		return ret;
	}
	if (val[0] > total) {
		printk(KERN_WARNING AUDIO_MEM_WARN_DMA_SIZE, val[0], total);
		val[0] = total;
	}
	audio_mem[AUDIO_MEM_DDR32_DMA].addr = res.start;
	audio_mem[AUDIO_MEM_DDR32_DMA].size = val[0];

	/* dsp memory dump buffer right behind the dma window */
	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dspmemdump",
						 val, 1, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dspmemdump' failed!(%d)\n", ret);
		return ret;
	}
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].addr = res.start + val[0];
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].size = val[1];

	/* single cell IRAM bases */
	if (!of_property_read_variable_u32_array(np, "sprd,iram-ap-base",
						 val, 1, 0)) {
		iram_ap_base = val[0];
		printk(KERN_INFO "[Audio:MEM] %s iram_ap_base=%#x\n",
		       "audio_mem_whale2_parse_dt", iram_ap_base);
	} else {
		printk(KERN_ERR AUDIO_MEM_ERR_AP_BASE,
		       "audio_mem_whale2_parse_dt");
	}

	if (!of_property_read_variable_u32_array(np, "sprd,iram-dsp-base",
						 val, 1, 0)) {
		iram_dsp_base = val[0];
		printk(KERN_INFO "[Audio:MEM] %s iram_dsp_base=%#x\n",
		       "audio_mem_whale2_parse_dt", iram_dsp_base);
	} else {
		printk(KERN_ERR AUDIO_MEM_ERR_DSP_BASE,
		       "audio_mem_whale2_parse_dt");
	}

	/* two cell (addr, size) shared memory buffers */
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_variable_u32_array(np, props[i].name,
							 val, 2, 0);
		if (ret < 0) {
			if (props[i].err)
				printk(KERN_ERR props[i].err,
				       "audio_mem_whale2_parse_dt");
			continue;
		}
		audio_mem[props[i].idx].addr = val[0];
		audio_mem[props[i].idx].size = val[1];
	}

	/* dma offset used by audio_addr_ap2dsp() */
	ret = of_property_read_variable_u32_array(np, "sprd,dma-offset", val, 1, 0);
	if (ret < 0) {
		printk(KERN_ERR AUDIO_MEM_ERR_DMA_OFF, "audio_mem_whale2_parse_dt");
		return ret;
	}
	dma_offset = val[0];

	if (audio_mem[AUDIO_MEM_DDR32_DMA].addr &&
	    audio_mem[AUDIO_MEM_DDR32_DMA].size) {
		ret = audio_smem_init(audio_mem[AUDIO_MEM_DDR32_DMA].addr,
				      audio_mem[AUDIO_MEM_DDR32_DMA].size);
		if (ret) {
			printk(KERN_INFO AUDIO_MEM_ERR_SMEM_INIT,
			       "audio_mem_whale2_parse_dt", ret);
			return ret;
		}
	}

	for (i = 0; i < AUDIO_MEM_TYPE_MAX; i++)
		printk(KERN_INFO "[Audio:MEM] %s addr= %#x, size=%#x\n",
		       "audio_mem_whale2_probe", audio_mem[i].addr,
		       audio_mem[i].size);

	return 0;
}

static int audio_mem_whale2_probe(struct platform_device *pdev)
{
	int ret = audio_mem_whale2_parse_dt(pdev->dev.of_node);

	if (ret)
		printk(KERN_ERR "[Audio:MEM] %s audio_mem_parse_dt failed\n",
		       "audio_mem_whale2_probe");

	return ret;
}

/* sharkl5 / roc1 */
static int audio_mem_sharkl5_parse_dt(struct device_node *np)
{
	struct resource res, ddr32;
	u32 val[2];
	u32 total;
	int ret;

	if (!np) {
		printk(KERN_ERR AUDIO_MEM_ERR_NP, "audio_mem_sharkl5_parse_dt");
		return -1;
	}

	/* memory-region 1: the whole audio ddr32 window */
	ret = audio_mem_get_region(np, 1, &res);
	if (ret)
		return ret;

	audio_mem[AUDIO_MEM_DDR32].addr = res.start;
	audio_mem[AUDIO_MEM_DDR32].size = res.end - res.start + 1;
	printk(KERN_INFO "[Audio:MEM] ddr32 (addr, size) (%#x, %#x), (%#x, %#x), (%#x, %#x), (%#x, %#x)\n",
	       audio_mem[AUDIO_MEM_DDR32].addr, audio_mem[AUDIO_MEM_DDR32].size,
	       audio_mem[AUDIO_MEM_DDR32_DMA].addr,
	       audio_mem[AUDIO_MEM_DDR32_DMA].size,
	       audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].addr,
	       audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].size,
	       audio_mem[AUDIO_MEM_CMDADDR].addr,
	       audio_mem[AUDIO_MEM_CMDADDR].size);
	printk(KERN_INFO "[Audio:MEM] dsp_bin (addr, size): (%#x, %#x)\n",
	       (u32)res.start, (u32)(res.end - res.start + 1));

	/* memory-region 0: the ddr32 dma window base, used for the bounds checks */
	ret = audio_mem_get_region(np, 0, &ddr32);
	if (ret)
		return ret;
	total = ddr32.end - ddr32.start + 1;

	ret = of_property_read_variable_u32_array(np, "ddr32-ap-dsp-map-offset",
						 val, 1, 0);
	if (ret < 0) {
		printk(KERN_ERR AUDIO_MEM_ERR_DMA_OFF, "audio_mem_sharkl5_parse_dt");
		return ret;
	}
	dma_offset = val[0];

	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dma", val, 2, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dma' failed!(%d)\n", ret);
		return ret;
	}
	if (val[1] > total) {
		printk(KERN_WARNING AUDIO_MEM_WARN_DMA_SIZE, val[1], total);
		val[1] = total;
	}
	audio_mem[AUDIO_MEM_DDR32_DMA].addr = val[0];
	audio_mem[AUDIO_MEM_DDR32_DMA].size = val[1];

	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dspmemdump",
						 val, 2, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dspmemdump' failed!(%d)\n", ret);
		return ret;
	}
	if (val[0] + val[1] > total) {
		printk(KERN_WARNING AUDIO_MEM_WARN_DUMP_SIZE,
		       val[0] + val[1], val[0], val[1], total);
		val[1] = total - val[0];
	}
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].addr =
		audio_mem[AUDIO_MEM_DDR32_DMA].addr + val[0];
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].size = val[1];

	if (audio_mem[AUDIO_MEM_DDR32_DMA].addr &&
	    audio_mem[AUDIO_MEM_DDR32_DMA].size) {
		ret = audio_smem_init(audio_mem[AUDIO_MEM_DDR32_DMA].addr,
				      audio_mem[AUDIO_MEM_DDR32_DMA].size);
		if (ret) {
			printk(KERN_INFO AUDIO_MEM_ERR_SMEM_INIT,
			       "audio_mem_sharkl5_parse_dt", ret);
			return ret;
		}
	}

	audio_mem_read_entry(np, "sprd,cmdaddr", AUDIO_MEM_CMDADDR,
			     AUDIO_MEM_ERR_CMD, 2);
	audio_mem_read_entry(np, "sprd,smsg-addr", AUDIO_MEM_SMSG_ADDR,
			     AUDIO_MEM_ERR_SMSG, 2);
	audio_mem_read_entry(np, "sprd,shmaddr-aud-str", AUDIO_MEM_SHM_AUD_STR,
			     AUDIO_MEM_ERR_SHM_A, 2);

	printk(KERN_INFO "[Audio:MEM] %s ddr32 (COMMD_PARAMS), (SMS), (AUD_STRUCT), (DSP_VBC), (SHM_NXP), (SHM_IVS_SMARTPA) (SHM_REG_DUMP)\n",
	       "audio_mem_sharkl5_probe");
	audio_mem_dump_ddr32(7);

	audio_mem_read_entry(np, "sprd,shmaddr-dsp-vbc", AUDIO_MEM_SHM_DSP_VBC,
			     AUDIO_MEM_ERR_SHM_B, 2);
	audio_mem_read_entry(np, "sprd,shmaddr-nxp", AUDIO_MEM_SHM_NXP,
			     AUDIO_MEM_ERR_SHM_C, 2);
	audio_mem_read_entry(np, "sprd,shmaddr-dsp-smartpa",
			     AUDIO_MEM_SHM_DSP_SMARTPA, AUDIO_MEM_ERR_SHM_C, 2);
	audio_mem_read_entry(np, "sprd,shmaddr-reg-dump", AUDIO_MEM_SHM_REG_DUMP,
			     AUDIO_MEM_ERR_SHM_D, 2);

	if (of_property_read_variable_u32_array(np, "sprd,cm4-shmem", val, 2, 0) < 0)
		printk(KERN_WARNING AUDIO_MEM_WARN_CM4, "audio_mem_sharkl5_parse_dt");

	audio_mem_read_entry(np, "sprd,offload-addr", AUDIO_MEM_OFFLOAD_ADDR,
			     AUDIO_MEM_ERR_OFFLOAD, 2);
	audio_mem_read_entry(np, "sprd,dsplog", AUDIO_MEM_DSPLOG, NULL, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-linklilst-node1",
			     AUDIO_MEM_NORMAL_CAP_NODE1, AUDIO_MEM_ERR_NODE1, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-linklilst-node2",
			     AUDIO_MEM_NORMAL_CAP_NODE2, AUDIO_MEM_ERR_NODE2, 2);
	audio_mem_read_entry(np, "sprd,normal-captue-data",
			     AUDIO_MEM_NORMAL_CAP_DATA, AUDIO_MEM_ERR_DATA, 2);
	audio_mem_read_entry(np, "sprd,audcp-aon-iram",
			     AUDIO_MEM_IRAM_AUDCP_AON, AUDIO_MEM_ERR_AON, 2);

	if (!of_property_read_variable_u32_array(np, "sprd,iram-ap-base",
						 val, 1, 0)) {
		iram_ap_base = val[0];
		printk(KERN_INFO "[Audio:MEM] %s iram_ap_base=%#x\n",
		       "audio_mem_sharkl5_parse_dt", iram_ap_base);
	}

	if (!of_property_read_variable_u32_array(np, "sprd,iram-dsp-base",
						 val, 1, 0)) {
		iram_dsp_base = val[0];
		printk(KERN_INFO "[Audio:MEM] %s iram_dsp_base=%#x\n",
		       "audio_mem_sharkl5_parse_dt", iram_dsp_base);
	}

	printk(KERN_INFO "[Audio:MEM] iram(addr size), (%#x,%#x), (%#x,%#x), (%#x,%#x), (%#x,%#x)\n",
	       audio_mem[AUDIO_MEM_IRAM_PHY_ADDR].addr,
	       audio_mem[AUDIO_MEM_IRAM_PHY_ADDR].size,
	       audio_mem[AUDIO_MEM_IRAM_NORMAL].addr,
	       audio_mem[AUDIO_MEM_IRAM_NORMAL].size,
	       audio_mem[AUDIO_MEM_IRAM_DEEPBUF].addr,
	       audio_mem[AUDIO_MEM_IRAM_DEEPBUF].size,
	       audio_mem[AUDIO_MEM_IRAM_4ARM7].addr,
	       audio_mem[AUDIO_MEM_IRAM_4ARM7].size);

	return 0;
}

static int audio_mem_sharkl5_probe(struct platform_device *pdev)
{
	int ret = audio_mem_sharkl5_parse_dt(pdev->dev.of_node);

	if (ret)
		printk(KERN_ERR "[Audio:MEM] %s audio_mem_parse_dt failed\n",
		       "audio_mem_sharkl5_probe");

	return ret;
}

/* orca */
static int audio_mem_orca_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource res, ddr32;
	u32 val[2];
	int ret;

	if (!np) {
		printk(KERN_ERR AUDIO_MEM_ERR_NP, "audio_mem_orca_probe");
		return -ENODEV;
	}

	ret = audio_mem_get_region(np, 1, &res);
	if (ret)
		return -ENODEV;

	audio_mem[AUDIO_MEM_DDR32].addr = res.start;
	audio_mem[AUDIO_MEM_DDR32].size = res.end - res.start + 1;
	printk(KERN_INFO "[Audio:MEM] dsp_bin (addr, size): (%#x, %#x)\n",
	       (u32)res.start, (u32)(res.end - res.start + 1));

	ret = audio_mem_get_region(np, 0, &ddr32);
	if (ret)
		return -ENODEV;

	ret = of_property_read_variable_u32_array(np, "ddr32-ap-dsp-map-offset",
						 val, 1, 0);
	if (ret < 0) {
		printk(KERN_ERR AUDIO_MEM_ERR_DMA_OFF, "audio_mem_orca_probe");
		return ret;
	}
	dma_offset = val[0];

	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dma", val, 2, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dma' failed!(%d)\n", ret);
		return ret;
	}
	audio_mem[AUDIO_MEM_DDR32_DMA].addr = val[0];
	audio_mem[AUDIO_MEM_DDR32_DMA].size = val[1];

	ret = of_property_read_variable_u32_array(np, "sprd,ddr32-dspmemdump",
						 val, 2, 0);
	if (ret < 0) {
		printk(KERN_ERR "[Audio:MEM] read 'sprd,ddr32-dspmemdump' failed!(%d)\n", ret);
		return ret;
	}
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].addr = val[0];
	audio_mem[AUDIO_MEM_DDR32_DSPMEMDUMP].size = val[1];

	if (audio_mem[AUDIO_MEM_DDR32_DMA].addr &&
	    audio_mem[AUDIO_MEM_DDR32_DMA].size) {
		ret = audio_smem_init(audio_mem[AUDIO_MEM_DDR32_DMA].addr,
				      audio_mem[AUDIO_MEM_DDR32_DMA].size);
		if (ret) {
			printk(KERN_INFO AUDIO_MEM_ERR_SMEM_INIT,
			       "audio_mem_orca_probe", ret);
			return -1;
		}
	}

	audio_mem_read_entry(np, "sprd,cmdaddr", AUDIO_MEM_CMDADDR,
			     AUDIO_MEM_ERR_CMD, 2);
	audio_mem_read_entry(np, "sprd,smsg-addr", AUDIO_MEM_SMSG_ADDR,
			     AUDIO_MEM_ERR_SMSG, 2);

	printk(KERN_INFO "[Audio:MEM] %s ddr32 (COMMD_PARAMS), (SMS), (DSPMEMDUMP), (DDR32)\n",
	       "audio_mem_orca_probe");

	audio_mem_read_entry(np, "sprd,audcp-aon-iram",
			     AUDIO_MEM_IRAM_AUDCP_AON, AUDIO_MEM_ERR_AON, 2);
	printk(KERN_INFO "[Audio:MEM] iram audcp aon (addr size), (%#x, %#x)\n",
	       audio_mem[AUDIO_MEM_IRAM_AUDCP_AON].addr,
	       audio_mem[AUDIO_MEM_IRAM_AUDCP_AON].size);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Driver                                                                    */
/* ------------------------------------------------------------------------- */

static int audio_mem_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	u32 platform;
	int ret;

	if (!np) {
		printk(KERN_ERR AUDIO_MEM_ERR_DEV_NP);
		return -EINVAL;
	}

	match = of_match_node(audio_mem_match_table, np);
	if (!match) {
		printk(KERN_ERR "[Audio:MEM] %s line[%d] Get the pcm of device id failed!\n",
		       "audio_mem_probe", 0x4ae);
		return -ENODEV;
	}

	g_priv_data = devm_kmalloc(&pdev->dev, sizeof(*g_priv_data), GFP_KERNEL);
	if (!g_priv_data)
		return -ENOMEM;

	platform = *(const u32 *)match->data;
	g_priv_data[0] = platform;

	switch (platform) {
	case AUDIO_MEM_PLAT_SHARKL2:
		ret = audio_mem_sharkl2_probe(pdev);
		break;
	case AUDIO_MEM_PLAT_WHALE2:
		ret = audio_mem_whale2_probe(pdev);
		break;
	case AUDIO_MEM_PLAT_SHARKL5:
		ret = audio_mem_sharkl5_probe(pdev);
		break;
	case AUDIO_MEM_PLAT_ORCA:
		ret = audio_mem_orca_probe(pdev);
		break;
	default:
		printk(KERN_ERR "[Audio:MEM] %s %d unknown platform_type[%d]\n",
		       "audio_mem_probe", 0x4c2, platform);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int audio_mem_remove(struct platform_device *pdev)
{
	if (g_priv_data) {
		devm_kfree(&pdev->dev, g_priv_data);
		g_priv_data = NULL;
	}

	return 0;
}

static const struct of_device_id audio_mem_match_table[] = {
	{ .compatible = "unisoc,sharkl3-audio-mem",	.data = (void *)AUDIO_MEM_PLAT_SHARKL2 },
	{ .compatible = "unisoc,audio-mem-whale2",	.data = (void *)AUDIO_MEM_PLAT_WHALE2 },
	{ .compatible = "unisoc,audio-mem-sharkl5",	.data = (void *)AUDIO_MEM_PLAT_SHARKL5 },
	{ .compatible = "unisoc,audio-mem-roc1",	.data = (void *)AUDIO_MEM_PLAT_SHARKL5 },
	{ .compatible = "unisoc,audio-mem-orca",	.data = (void *)AUDIO_MEM_PLAT_ORCA },
	{ }
};

static struct platform_driver audio_mem_driver = {
	.probe		= audio_mem_probe,
	.remove		= audio_mem_remove,
	.driver		= {
		.name		= "audio_mem",
		.of_match_table	= audio_mem_match_table,
	},
};

module_platform_driver(audio_mem_driver);

MODULE_DESCRIPTION("audio memory manager driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("of:N*T*Cunisoc,sharkl3-audio-mem");
MODULE_ALIAS("of:N*T*Cunisoc,sharkl3-audio-memC*");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-whale2");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-whale2C*");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-sharkl5");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-sharkl5C*");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-roc1");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-roc1C*");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-orca");
MODULE_ALIAS("of:N*T*Cunisoc,audio-mem-orcaC*");
