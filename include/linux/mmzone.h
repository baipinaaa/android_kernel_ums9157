/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
#include <linux/pageblock-flags.h>
#include <linux/page-flags-layout.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/android_kabi.h>
#include <asm/page.h>


#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))


#define PAGE_ALLOC_COSTLY_ORDER 3

enum migratetype {
	MIGRATE_UNMOVABLE,
	MIGRATE_MOVABLE,
	MIGRATE_RECLAIMABLE,
#ifdef CONFIG_CMA
	
	MIGRATE_CMA,
#endif
	MIGRATE_PCPTYPES, 
	MIGRATE_HIGHATOMIC = MIGRATE_PCPTYPES,
#ifdef CONFIG_MEMORY_ISOLATION
	MIGRATE_ISOLATE,	
#endif
	MIGRATE_TYPES
};


extern const char * const migratetype_names[MIGRATE_TYPES];

#ifdef CONFIG_CMA
#  define is_migrate_cma(migratetype) unlikely((migratetype) == MIGRATE_CMA)
#  define is_migrate_cma_page(_page) (get_pageblock_migratetype(_page) == MIGRATE_CMA)
#  define get_cma_migrate_type() MIGRATE_CMA
#else
#  define is_migrate_cma(migratetype) false
#  define is_migrate_cma_page(_page) false
#  define get_cma_migrate_type() MIGRATE_MOVABLE
#endif

static inline bool is_migrate_movable(int mt)
{
	return is_migrate_cma(mt) || mt == MIGRATE_MOVABLE;
}

#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

extern int page_group_by_mobility_disabled;

#define NR_MIGRATETYPE_BITS (PB_migrate_end - PB_migrate + 1)
#define MIGRATETYPE_MASK ((1UL << NR_MIGRATETYPE_BITS) - 1)

#define get_pageblock_migratetype(page)					\
	get_pfnblock_flags_mask(page, page_to_pfn(page),		\
			PB_migrate_end, MIGRATETYPE_MASK)

struct free_area {
	struct list_head	free_list[MIGRATE_TYPES];
	unsigned long		nr_free;
};


static inline void add_to_free_area(struct page *page, struct free_area *area,
			     int migratetype)
{
	list_add(&page->lru, &area->free_list[migratetype]);
	area->nr_free++;
}


static inline void add_to_free_area_tail(struct page *page, struct free_area *area,
				  int migratetype)
{
	list_add_tail(&page->lru, &area->free_list[migratetype]);
	area->nr_free++;
}

#ifdef CONFIG_SHUFFLE_PAGE_ALLOCATOR

void add_to_free_area_random(struct page *page, struct free_area *area,
		int migratetype);
#else
static inline void add_to_free_area_random(struct page *page,
		struct free_area *area, int migratetype)
{
	add_to_free_area(page, area, migratetype);
}
#endif


static inline void move_to_free_area(struct page *page, struct free_area *area,
			     int migratetype)
{
	list_move(&page->lru, &area->free_list[migratetype]);
}

static inline struct page *get_page_from_free_area(struct free_area *area,
					    int migratetype)
{
	return list_first_entry_or_null(&area->free_list[migratetype],
					struct page, lru);
}

static inline void del_page_from_free_area(struct page *page,
		struct free_area *area)
{
	list_del(&page->lru);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
	area->nr_free--;
}

static inline bool free_area_empty(struct free_area *area, int migratetype)
{
	return list_empty(&area->free_list[migratetype]);
}

struct pglist_data;


#if defined(CONFIG_SMP)
struct zone_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
#define ZONE_PADDING(name)
#endif

#ifdef CONFIG_NUMA
enum numa_stat_item {
	NUMA_HIT,		
	NUMA_MISS,		
	NUMA_FOREIGN,		
	NUMA_INTERLEAVE_HIT,	
	NUMA_LOCAL,		
	NUMA_OTHER,		
	NR_VM_NUMA_STAT_ITEMS
};
#else
#define NR_VM_NUMA_STAT_ITEMS 0
#endif

enum zone_stat_item {
	
	NR_FREE_PAGES,
	NR_ZONE_LRU_BASE, 
	NR_ZONE_INACTIVE_ANON = NR_ZONE_LRU_BASE,
	NR_ZONE_ACTIVE_ANON,
	NR_ZONE_INACTIVE_FILE,
	NR_ZONE_ACTIVE_FILE,
	NR_ZONE_UNEVICTABLE,
	NR_ZONE_WRITE_PENDING,	
	NR_MLOCK,		
	NR_PAGETABLE,		
	NR_KERNEL_STACK_KB,	
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
	NR_KERNEL_SCS_BYTES,	
#endif
	
	NR_BOUNCE,
	NR_ZSPAGES,		
	NR_FREE_CMA_PAGES,
	NR_VM_ZONE_STAT_ITEMS };

enum node_stat_item {
	NR_LRU_BASE,
	NR_INACTIVE_ANON = NR_LRU_BASE, 
	NR_ACTIVE_ANON,		
	NR_INACTIVE_FILE,	
	NR_ACTIVE_FILE,		
	NR_UNEVICTABLE,		
	NR_SLAB_RECLAIMABLE,
	NR_SLAB_UNRECLAIMABLE,
	NR_ISOLATED_ANON,	
	NR_ISOLATED_FILE,	
	WORKINGSET_NODES,
	WORKINGSET_REFAULT,
	WORKINGSET_ACTIVATE,
	WORKINGSET_RESTORE,
	WORKINGSET_NODERECLAIM,
	NR_ANON_MAPPED,	
	NR_FILE_MAPPED,	
	NR_FILE_PAGES,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	NR_WRITEBACK_TEMP,	
	NR_SHMEM,		
	NR_SHMEM_THPS,
	NR_SHMEM_PMDMAPPED,
	NR_FILE_THPS,
	NR_FILE_PMDMAPPED,
	NR_ANON_THPS,
	NR_UNSTABLE_NFS,	
	NR_VMSCAN_WRITE,
	NR_VMSCAN_IMMEDIATE,	
	NR_DIRTIED,		
	NR_WRITTEN,		
	NR_KERNEL_MISC_RECLAIMABLE,	
	NR_VM_NODE_STAT_ITEMS
};


#define LRU_BASE 0
#define LRU_ACTIVE 1
#define LRU_FILE 2

enum lru_list {
	LRU_INACTIVE_ANON = LRU_BASE,
	LRU_ACTIVE_ANON = LRU_BASE + LRU_ACTIVE,
	LRU_INACTIVE_FILE = LRU_BASE + LRU_FILE,
	LRU_ACTIVE_FILE = LRU_BASE + LRU_FILE + LRU_ACTIVE,
	LRU_UNEVICTABLE,
	NR_LRU_LISTS
};

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

#define for_each_evictable_lru(lru) for (lru = 0; lru <= LRU_ACTIVE_FILE; lru++)

static inline int is_file_lru(enum lru_list lru)
{
	return (lru == LRU_INACTIVE_FILE || lru == LRU_ACTIVE_FILE);
}

static inline int is_active_lru(enum lru_list lru)
{
	return (lru == LRU_ACTIVE_ANON || lru == LRU_ACTIVE_FILE);
}

#ifndef CONFIG_LRU_BALANCE_BASE_THRASHING
struct zone_reclaim_stat {
	
	unsigned long		recent_rotated[2];
	unsigned long		recent_scanned[2];
};
#endif

struct lruvec {
	struct list_head		lists[NR_LRU_LISTS];
#ifndef CONFIG_LRU_BALANCE_BASE_THRASHING
	struct zone_reclaim_stat	reclaim_stat;
	
	atomic_long_t			inactive_age;
#else
	
	unsigned long			anon_cost;
	unsigned long			file_cost;
	
	atomic_long_t			nonresident_age;
#endif
	
	unsigned long			refaults;
#ifdef CONFIG_MEMCG
	struct pglist_data *pgdat;
#endif
};


#define ISOLATE_UNMAPPED	((__force isolate_mode_t)0x2)

#define ISOLATE_ASYNC_MIGRATE	((__force isolate_mode_t)0x4)

#define ISOLATE_UNEVICTABLE	((__force isolate_mode_t)0x8)


typedef unsigned __bitwise isolate_mode_t;

enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	NR_WMARK
};

#define min_wmark_pages(z) (z->_watermark[WMARK_MIN] + z->watermark_boost)
#define low_wmark_pages(z) (z->_watermark[WMARK_LOW] + z->watermark_boost)
#define high_wmark_pages(z) (z->_watermark[WMARK_HIGH] + z->watermark_boost)
#define wmark_pages(z, i) (z->_watermark[i] + z->watermark_boost)

struct per_cpu_pages {
	int count;		
	int high;		
	int batch;		

	
	struct list_head lists[MIGRATE_PCPTYPES];
};

struct per_cpu_pageset {
	struct per_cpu_pages pcp;
#ifdef CONFIG_NUMA
	s8 expire;
	u16 vm_numa_stat_diff[NR_VM_NUMA_STAT_ITEMS];
#endif
#ifdef CONFIG_SMP
	s8 stat_threshold;
	s8 vm_stat_diff[NR_VM_ZONE_STAT_ITEMS];
#endif
};

struct per_cpu_nodestat {
	s8 stat_threshold;
	s8 vm_node_stat_diff[NR_VM_NODE_STAT_ITEMS];
};

#endif 

enum zone_type {
#ifdef CONFIG_ZONE_DMA
	
	ZONE_DMA,
#endif
#ifdef CONFIG_ZONE_DMA32
	
	ZONE_DMA32,
#endif
	
	ZONE_NORMAL,
#ifdef CONFIG_HIGHMEM
	
	ZONE_HIGHMEM,
#endif
	ZONE_MOVABLE,
#ifdef CONFIG_ZONE_DEVICE
	ZONE_DEVICE,
#endif
	__MAX_NR_ZONES

};

#ifndef __GENERATING_BOUNDS_H

struct zone {
	

	
	unsigned long _watermark[NR_WMARK];
	unsigned long watermark_boost;

	unsigned long nr_reserved_highatomic;

	
	long lowmem_reserve[MAX_NR_ZONES];

#ifdef CONFIG_NUMA
	int node;
#endif
	struct pglist_data	*zone_pgdat;
	struct per_cpu_pageset __percpu *pageset;

#ifdef CONFIG_CMA
	bool			cma_alloc;
#endif

#ifndef CONFIG_SPARSEMEM
	
	unsigned long		*pageblock_flags;
#endif 

	
	unsigned long		zone_start_pfn;

	
	atomic_long_t		managed_pages;
	unsigned long		spanned_pages;
	unsigned long		present_pages;

	const char		*name;

#ifdef CONFIG_MEMORY_ISOLATION
	
	unsigned long		nr_isolate_pageblock;
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
	
	seqlock_t		span_seqlock;
#endif

	int initialized;

	
	ZONE_PADDING(_pad1_)

	
	struct free_area	free_area[MAX_ORDER];

	
	unsigned long		flags;

	
	spinlock_t		lock;

	
	ZONE_PADDING(_pad2_)

	
	unsigned long percpu_drift_mark;

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	
	unsigned long		compact_cached_free_pfn;
	
	unsigned long		compact_cached_migrate_pfn[2];
	unsigned long		compact_init_migrate_pfn;
	unsigned long		compact_init_free_pfn;
#endif

#ifdef CONFIG_COMPACTION
	
	unsigned int		compact_considered;
	unsigned int		compact_defer_shift;
	int			compact_order_failed;
#endif

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	
	bool			compact_blockskip_flush;
#endif

	bool			contiguous;

	ZONE_PADDING(_pad3_)
	
	atomic_long_t		vm_stat[NR_VM_ZONE_STAT_ITEMS];
	atomic_long_t		vm_numa_stat[NR_VM_NUMA_STAT_ITEMS];

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
} ____cacheline_internodealigned_in_smp;

enum pgdat_flags {
	PGDAT_CONGESTED,		
	PGDAT_DIRTY,			
	PGDAT_WRITEBACK,		
	PGDAT_RECLAIM_LOCKED,		
};

enum zone_flags {
	ZONE_BOOSTED_WATERMARK,		
};

static inline unsigned long zone_managed_pages(struct zone *zone)
{
	return (unsigned long)atomic_long_read(&zone->managed_pages);
}

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

static inline bool zone_spans_pfn(const struct zone *zone, unsigned long pfn)
{
	return zone->zone_start_pfn <= pfn && pfn < zone_end_pfn(zone);
}

static inline bool zone_is_initialized(struct zone *zone)
{
	return zone->initialized;
}

static inline bool zone_is_empty(struct zone *zone)
{
	return zone->spanned_pages == 0;
}


static inline bool zone_intersects(struct zone *zone,
		unsigned long start_pfn, unsigned long nr_pages)
{
	if (zone_is_empty(zone))
		return false;
	if (start_pfn >= zone_end_pfn(zone) ||
	    start_pfn + nr_pages <= zone->zone_start_pfn)
		return false;

	return true;
}


#define DEF_PRIORITY 12


#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES * MAX_NR_ZONES)

enum {
	ZONELIST_FALLBACK,	
#ifdef CONFIG_NUMA
	
	ZONELIST_NOFALLBACK,	
#endif
	MAX_ZONELISTS
};


struct zoneref {
	struct zone *zone;	
	int zone_idx;		
};


struct zonelist {
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
};

#ifndef CONFIG_DISCONTIGMEM

extern struct page *mem_map;
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct deferred_split {
	spinlock_t split_queue_lock;
	struct list_head split_queue;
	unsigned long split_queue_len;
};
#endif


struct bootmem_data;
typedef struct pglist_data {
	struct zone node_zones[MAX_NR_ZONES];
	struct zonelist node_zonelists[MAX_ZONELISTS];
	int nr_zones;
#ifdef CONFIG_FLAT_NODE_MEM_MAP	
	struct page *node_mem_map;
#ifdef CONFIG_PAGE_EXTENSION
	struct page_ext *node_page_ext;
#endif
#endif
#if defined(CONFIG_MEMORY_HOTPLUG) || defined(CONFIG_DEFERRED_STRUCT_PAGE_INIT)
	
	spinlock_t node_size_lock;
#endif
	unsigned long node_start_pfn;
	unsigned long node_present_pages; 
	unsigned long node_spanned_pages; 
	int node_id;
	wait_queue_head_t kswapd_wait;
	wait_queue_head_t pfmemalloc_wait;
	struct task_struct *kswapd;	
	int kswapd_order;
	enum zone_type kswapd_classzone_idx;

	int kswapd_failures;		

#ifdef CONFIG_COMPACTION
	int kcompactd_max_order;
	enum zone_type kcompactd_classzone_idx;
	wait_queue_head_t kcompactd_wait;
	struct task_struct *kcompactd;
#endif
	
	unsigned long		totalreserve_pages;

#ifdef CONFIG_NUMA
	
	unsigned long		min_unmapped_pages;
	unsigned long		min_slab_pages;
#endif 

	
	ZONE_PADDING(_pad1_)
	spinlock_t		lru_lock;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	
	unsigned long first_deferred_pfn;
#endif 

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct deferred_split deferred_split_queue;
#endif

	
	struct lruvec		lruvec;

	unsigned long		flags;

	ZONE_PADDING(_pad2_)

	
	struct per_cpu_nodestat __percpu *per_cpu_nodestats;
	atomic_long_t		vm_stat[NR_VM_NODE_STAT_ITEMS];
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)
#ifdef CONFIG_FLAT_NODE_MEM_MAP
#define pgdat_page_nr(pgdat, pagenr)	((pgdat)->node_mem_map + (pagenr))
#else
#define pgdat_page_nr(pgdat, pagenr)	pfn_to_page((pgdat)->node_start_pfn + (pagenr))
#endif
#define nid_page_nr(nid, pagenr) 	pgdat_page_nr(NODE_DATA(nid),(pagenr))

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))

static inline struct lruvec *node_lruvec(struct pglist_data *pgdat)
{
	return &pgdat->lruvec;
}

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

static inline bool pgdat_is_empty(pg_data_t *pgdat)
{
	return !pgdat->node_start_pfn && !pgdat->node_spanned_pages;
}

#include <linux/memory_hotplug.h>

void build_all_zonelists(pg_data_t *pgdat);
void wakeup_kswapd(struct zone *zone, gfp_t gfp_mask, int order,
		   enum zone_type classzone_idx);
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int classzone_idx, unsigned int alloc_flags,
			 long free_pages);
bool zone_watermark_ok(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx,
		unsigned int alloc_flags);
bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx);

enum meminit_context {
	MEMINIT_EARLY,
	MEMINIT_HOTPLUG,
};

extern void init_currently_empty_zone(struct zone *zone, unsigned long start_pfn,
				     unsigned long size);

extern void lruvec_init(struct lruvec *lruvec);

static inline struct pglist_data *lruvec_pgdat(struct lruvec *lruvec)
{
#ifdef CONFIG_MEMCG
	return lruvec->pgdat;
#else
	return container_of(lruvec, struct pglist_data, lruvec);
#endif
}

extern unsigned long lruvec_lru_size(struct lruvec *lruvec, enum lru_list lru, int zone_idx);

#ifdef CONFIG_HAVE_MEMORY_PRESENT
void memory_present(int nid, unsigned long start, unsigned long end);
#else
static inline void memory_present(int nid, unsigned long start, unsigned long end) {}
#endif

#if defined(CONFIG_SPARSEMEM)
void memblocks_present(void);
#else
static inline void memblocks_present(void) {}
#endif

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
int local_memory_node(int node_id);
#else
static inline int local_memory_node(int node_id) { return node_id; };
#endif


#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)


static inline bool managed_zone(struct zone *zone)
{
	return zone_managed_pages(zone);
}


static inline bool populated_zone(struct zone *zone)
{
	return zone->present_pages;
}

#ifdef CONFIG_NUMA
static inline int zone_to_nid(struct zone *zone)
{
	return zone->node;
}

static inline void zone_set_nid(struct zone *zone, int nid)
{
	zone->node = nid;
}
#else
static inline int zone_to_nid(struct zone *zone)
{
	return 0;
}

static inline void zone_set_nid(struct zone *zone, int nid) {}
#endif

extern int movable_zone;

#ifdef CONFIG_HIGHMEM
static inline int zone_movable_is_highmem(void)
{
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	return movable_zone == ZONE_HIGHMEM;
#else
	return (ZONE_MOVABLE - 1) == ZONE_HIGHMEM;
#endif
}
#endif

static inline int is_highmem_idx(enum zone_type idx)
{
#ifdef CONFIG_HIGHMEM
	return (idx == ZONE_HIGHMEM ||
		(idx == ZONE_MOVABLE && zone_movable_is_highmem()));
#else
	return 0;
#endif
}

#ifdef CONFIG_ZONE_DMA
bool has_managed_dma(void);
#else
static inline bool has_managed_dma(void)
{
	return false;
}
#endif


static inline int is_highmem(struct zone *zone)
{
#ifdef CONFIG_HIGHMEM
	return is_highmem_idx(zone_idx(zone));
#else
	return 0;
#endif
}


struct ctl_table;
int min_free_kbytes_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int extra_free_kbytes_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int watermark_boost_factor_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int watermark_scale_factor_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int percpu_pagelist_fraction_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);

extern int numa_zonelist_order_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
extern char numa_zonelist_order[];
#define NUMA_ZONELIST_ORDER_LEN	16

#ifndef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data contig_page_data;
#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map

#else 

#include <asm/mmzone.h>

#endif 

extern struct pglist_data *first_online_pgdat(void);
extern struct pglist_data *next_online_pgdat(struct pglist_data *pgdat);
extern struct zone *next_zone(struct zone *zone);


#define for_each_online_pgdat(pgdat)			\
	for (pgdat = first_online_pgdat();		\
	     pgdat;					\
	     pgdat = next_online_pgdat(pgdat))

#define for_each_zone(zone)			        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))

#define for_each_populated_zone(zone)		        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))			\
		if (!populated_zone(zone))		\
			; 		\
		else

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

static inline int zonelist_node_idx(struct zoneref *zoneref)
{
	return zone_to_nid(zoneref->zone);
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes);


static __always_inline struct zoneref *next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	if (likely(!nodes && zonelist_zone_idx(z) <= highest_zoneidx))
		return z;
	return __next_zones_zonelist(z, highest_zoneidx, nodes);
}


static inline struct zoneref *first_zones_zonelist(struct zonelist *zonelist,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	return next_zones_zonelist(zonelist->_zonerefs,
							highest_zoneidx, nodes);
}


#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask), zone = zonelist_zone(z);	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))

#define for_next_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (zone = z->zone;	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))



#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#endif

#if !defined(CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID) && \
	!defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP)
static inline unsigned long early_pfn_to_nid(unsigned long pfn)
{
	BUILD_BUG_ON(IS_ENABLED(CONFIG_NUMA));
	return 0;
}
#endif

#ifdef CONFIG_FLATMEM
#define pfn_to_nid(pfn)		(0)
#endif

#ifdef CONFIG_SPARSEMEM


#define PA_SECTION_SHIFT	(SECTION_SIZE_BITS)
#define PFN_SECTION_SHIFT	(SECTION_SIZE_BITS - PAGE_SHIFT)

#define NR_MEM_SECTIONS		(1UL << SECTIONS_SHIFT)

#define PAGES_PER_SECTION       (1UL << PFN_SECTION_SHIFT)
#define PAGE_SECTION_MASK	(~(PAGES_PER_SECTION-1))

#define SECTION_BLOCKFLAGS_BITS \
	((1UL << (PFN_SECTION_SHIFT - pageblock_order)) * NR_PAGEBLOCK_BITS)

#if (MAX_ORDER - 1 + PAGE_SHIFT) > SECTION_SIZE_BITS
#error Allocator MAX_ORDER exceeds SECTION_SIZE
#endif

static inline unsigned long pfn_to_section_nr(unsigned long pfn)
{
	return pfn >> PFN_SECTION_SHIFT;
}
static inline unsigned long section_nr_to_pfn(unsigned long sec)
{
	return sec << PFN_SECTION_SHIFT;
}

#define SECTION_ALIGN_UP(pfn)	(((pfn) + PAGES_PER_SECTION - 1) & PAGE_SECTION_MASK)
#define SECTION_ALIGN_DOWN(pfn)	((pfn) & PAGE_SECTION_MASK)

#define SUBSECTION_SHIFT 21

#define PFN_SUBSECTION_SHIFT (SUBSECTION_SHIFT - PAGE_SHIFT)
#define PAGES_PER_SUBSECTION (1UL << PFN_SUBSECTION_SHIFT)
#define PAGE_SUBSECTION_MASK (~(PAGES_PER_SUBSECTION-1))

#if SUBSECTION_SHIFT > SECTION_SIZE_BITS
#error Subsection size exceeds section size
#else
#define SUBSECTIONS_PER_SECTION (1UL << (SECTION_SIZE_BITS - SUBSECTION_SHIFT))
#endif

#define SUBSECTION_ALIGN_UP(pfn) ALIGN((pfn), PAGES_PER_SUBSECTION)
#define SUBSECTION_ALIGN_DOWN(pfn) ((pfn) & PAGE_SUBSECTION_MASK)

struct mem_section_usage {
	DECLARE_BITMAP(subsection_map, SUBSECTIONS_PER_SECTION);
	
	unsigned long pageblock_flags[0];
};

void subsection_map_init(unsigned long pfn, unsigned long nr_pages);

struct page;
struct page_ext;
struct mem_section {
	
	unsigned long section_mem_map;

	struct mem_section_usage *usage;
#ifdef CONFIG_PAGE_EXTENSION
	
	struct page_ext *page_ext;
	unsigned long pad;
#endif
	
};

#ifdef CONFIG_SPARSEMEM_EXTREME
#define SECTIONS_PER_ROOT       (PAGE_SIZE / sizeof (struct mem_section))
#else
#define SECTIONS_PER_ROOT	1
#endif

#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT)
#define NR_SECTION_ROOTS	DIV_ROUND_UP(NR_MEM_SECTIONS, SECTIONS_PER_ROOT)
#define SECTION_ROOT_MASK	(SECTIONS_PER_ROOT - 1)

#ifdef CONFIG_SPARSEMEM_EXTREME
extern struct mem_section **mem_section;
#else
extern struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT];
#endif

static inline unsigned long *section_to_usemap(struct mem_section *ms)
{
	return ms->usage->pageblock_flags;
}

static inline struct mem_section *__nr_to_section(unsigned long nr)
{
	unsigned long root = SECTION_NR_TO_ROOT(nr);

	if (unlikely(root >= NR_SECTION_ROOTS))
		return NULL;

#ifdef CONFIG_SPARSEMEM_EXTREME
	if (!mem_section || !mem_section[root])
		return NULL;
#endif
	return &mem_section[root][nr & SECTION_ROOT_MASK];
}
extern unsigned long __section_nr(struct mem_section *ms);
extern size_t mem_section_usage_size(void);


#define	SECTION_MARKED_PRESENT	(1UL<<0)
#define SECTION_HAS_MEM_MAP	(1UL<<1)
#define SECTION_IS_ONLINE	(1UL<<2)
#define SECTION_IS_EARLY	(1UL<<3)
#define SECTION_MAP_LAST_BIT	(1UL<<4)
#define SECTION_MAP_MASK	(~(SECTION_MAP_LAST_BIT-1))
#define SECTION_NID_SHIFT	3

static inline struct page *__section_mem_map_addr(struct mem_section *section)
{
	unsigned long map = section->section_mem_map;
	map &= SECTION_MAP_MASK;
	return (struct page *)map;
}

static inline int present_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_MARKED_PRESENT));
}

static inline int present_section_nr(unsigned long nr)
{
	return present_section(__nr_to_section(nr));
}

static inline int valid_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_HAS_MEM_MAP));
}

static inline int early_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_IS_EARLY));
}

static inline int valid_section_nr(unsigned long nr)
{
	return valid_section(__nr_to_section(nr));
}

static inline int online_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_IS_ONLINE));
}

static inline int online_section_nr(unsigned long nr)
{
	return online_section(__nr_to_section(nr));
}

#ifdef CONFIG_MEMORY_HOTPLUG
void online_mem_sections(unsigned long start_pfn, unsigned long end_pfn);
#ifdef CONFIG_MEMORY_HOTREMOVE
void offline_mem_sections(unsigned long start_pfn, unsigned long end_pfn);
#endif
#endif

static inline struct mem_section *__pfn_to_section(unsigned long pfn)
{
	return __nr_to_section(pfn_to_section_nr(pfn));
}

extern unsigned long __highest_present_section_nr;

static inline int subsection_map_index(unsigned long pfn)
{
	return (pfn & ~(PAGE_SECTION_MASK)) / PAGES_PER_SUBSECTION;
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static inline int pfn_section_valid(struct mem_section *ms, unsigned long pfn)
{
	int idx = subsection_map_index(pfn);

	return test_bit(idx, ms->usage->subsection_map);
}
#else
static inline int pfn_section_valid(struct mem_section *ms, unsigned long pfn)
{
	return 1;
}
#endif

#ifndef CONFIG_HAVE_ARCH_PFN_VALID
static inline int pfn_valid(unsigned long pfn)
{
	struct mem_section *ms;

	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	ms = __nr_to_section(pfn_to_section_nr(pfn));
	if (!valid_section(ms))
		return 0;
	
	return early_section(ms) || pfn_section_valid(ms, pfn);
}
#endif

static inline int pfn_present(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return present_section(__nr_to_section(pfn_to_section_nr(pfn)));
}


#ifdef CONFIG_NUMA
#define pfn_to_nid(pfn)							\
({									\
	unsigned long __pfn_to_nid_pfn = (pfn);				\
	page_to_nid(pfn_to_page(__pfn_to_nid_pfn));			\
})
#else
#define pfn_to_nid(pfn)		(0)
#endif

#define early_pfn_valid(pfn)	pfn_valid(pfn)
void sparse_init(void);
#else
#define sparse_init()	do {} while (0)
#define sparse_index_init(_sec, _nid)  do {} while (0)
#define pfn_present pfn_valid
#define subsection_map_init(_pfn, _nr_pages) do {} while (0)
#endif 


struct mminit_pfnnid_cache {
	unsigned long last_start;
	unsigned long last_end;
	int last_nid;
};

#ifndef early_pfn_valid
#define early_pfn_valid(pfn)	(1)
#endif

void memory_present(int nid, unsigned long start, unsigned long end);


#ifdef CONFIG_HOLES_IN_ZONE
#define pfn_valid_within(pfn) pfn_valid(pfn)
#else
#define pfn_valid_within(pfn) (1)
#endif

#endif 
#endif 
#endif 
