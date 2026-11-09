/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/node.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/page-flags.h>
#include <asm/page.h>

struct notifier_block;

struct bio;

struct pagevec;

#ifdef CONFIG_WRITEBACK_SWAPCACHE
extern int writeback_swapcache;
#endif

#define SWAP_FLAG_PREFER	0x8000	
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0
#define SWAP_FLAG_DISCARD	0x10000 
#define SWAP_FLAG_DISCARD_ONCE	0x20000 
#define SWAP_FLAG_DISCARD_PAGES 0x40000 

#define SWAP_FLAGS_VALID	(SWAP_FLAG_PRIO_MASK | SWAP_FLAG_PREFER | \
				 SWAP_FLAG_DISCARD | SWAP_FLAG_DISCARD_ONCE | \
				 SWAP_FLAG_DISCARD_PAGES)
#define SWAP_BATCH 64

static inline int current_is_kswapd(void)
{
	return current->flags & PF_KSWAPD;
}


#define MAX_SWAPFILES_SHIFT	5




#ifdef CONFIG_DEVICE_PRIVATE
#define SWP_DEVICE_NUM 2
#define SWP_DEVICE_WRITE (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM)
#define SWP_DEVICE_READ (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+1)
#else
#define SWP_DEVICE_NUM 0
#endif


#ifdef CONFIG_MIGRATION
#define SWP_MIGRATION_NUM 2
#define SWP_MIGRATION_READ	(MAX_SWAPFILES + SWP_HWPOISON_NUM)
#define SWP_MIGRATION_WRITE	(MAX_SWAPFILES + SWP_HWPOISON_NUM + 1)
#else
#define SWP_MIGRATION_NUM 0
#endif


#ifdef CONFIG_MEMORY_FAILURE
#define SWP_HWPOISON_NUM 1
#define SWP_HWPOISON		MAX_SWAPFILES
#else
#define SWP_HWPOISON_NUM 0
#endif

#define MAX_SWAPFILES \
	((1 << MAX_SWAPFILES_SHIFT) - SWP_DEVICE_NUM - \
	SWP_MIGRATION_NUM - SWP_HWPOISON_NUM)


union swap_header {
	struct {
		char reserved[PAGE_SIZE - 10];
		char magic[10];			
	} magic;
	struct {
		char		bootbits[1024];	
		__u32		version;
		__u32		last_page;
		__u32		nr_badpages;
		unsigned char	sws_uuid[16];
		unsigned char	sws_volume[16];
		__u32		padding[117];
		__u32		badpages[1];
	} info;
};


struct reclaim_state {
	unsigned long reclaimed_slab;
};

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;


struct swap_extent {
	struct rb_node rb_node;
	pgoff_t start_page;
	pgoff_t nr_pages;
	sector_t start_block;
};


#define MAX_SWAP_BADPAGES \
	((offsetof(union swap_header, magic.magic) - \
	  offsetof(union swap_header, info.badpages)) / sizeof(int))

enum {
	SWP_USED	= (1 << 0),	
	SWP_WRITEOK	= (1 << 1),	
	SWP_DISCARDABLE = (1 << 2),	
	SWP_DISCARDING	= (1 << 3),	
	SWP_SOLIDSTATE	= (1 << 4),	
	SWP_CONTINUED	= (1 << 5),	
	SWP_BLKDEV	= (1 << 6),	
	SWP_ACTIVATED	= (1 << 7),	
	SWP_FS		= (1 << 8),	
	SWP_AREA_DISCARD = (1 << 9),	
	SWP_PAGE_DISCARD = (1 << 10),	
	SWP_STABLE_WRITES = (1 << 11),	
	SWP_SYNCHRONOUS_IO = (1 << 12),	
	SWP_VALID	= (1 << 13),	
					
	SWP_SCANNING	= (1 << 14),	
};

#define SWAP_CLUSTER_MAX 32UL
#define COMPACT_CLUSTER_MAX SWAP_CLUSTER_MAX

#define SWAP_MAP_MAX	0x3e	
#define SWAP_MAP_BAD	0x3f	
#define SWAP_HAS_CACHE	0x40	
#define SWAP_CONT_MAX	0x7f	
#define COUNT_CONTINUED	0x80	
#define SWAP_MAP_SHMEM	0xbf	


struct swap_cluster_info {
	spinlock_t lock;	
	unsigned int data:24;
	unsigned int flags:8;
};
#define CLUSTER_FLAG_FREE 1 
#define CLUSTER_FLAG_NEXT_NULL 2 
#define CLUSTER_FLAG_HUGE 4 


struct percpu_cluster {
	struct swap_cluster_info index; 
	unsigned int next; 
};

struct swap_cluster_list {
	struct swap_cluster_info head;
	struct swap_cluster_info tail;
};


struct swap_info_struct {
	unsigned long	flags;		
	signed short	prio;		
	struct plist_node list;		
	signed char	type;		
	unsigned int	max;		
	unsigned char *swap_map;	
	struct swap_cluster_info *cluster_info; 
	struct swap_cluster_list free_clusters; 
	unsigned int lowest_bit;	
	unsigned int highest_bit;	
	unsigned int pages;		
	unsigned int inuse_pages;	
	unsigned int cluster_next;	
	unsigned int cluster_nr;	
	struct percpu_cluster __percpu *percpu_cluster; 
	struct rb_root swap_extent_root;
	struct block_device *bdev;	
	struct file *swap_file;		
	unsigned int old_block_size;	
#ifdef CONFIG_FRONTSWAP
	unsigned long *frontswap_map;	
	atomic_t frontswap_pages;	
#endif
	spinlock_t lock;		
	spinlock_t cont_lock;		
	struct work_struct discard_work; 
	struct swap_cluster_list discard_clusters; 
	struct plist_node avail_lists[0]; 
};

#ifdef CONFIG_64BIT
#define SWAP_RA_ORDER_CEILING	5
#else

#define SWAP_RA_ORDER_CEILING	3
#define SWAP_RA_PTE_CACHE_SIZE	(1 << SWAP_RA_ORDER_CEILING)
#endif

struct vma_swap_readahead {
	unsigned short win;
	unsigned short offset;
	unsigned short nr_pte;
#ifdef CONFIG_64BIT
	pte_t *ptes;
#else
	pte_t ptes[SWAP_RA_PTE_CACHE_SIZE];
#endif
};


void *workingset_eviction(struct page *page);
#ifdef CONFIG_LRU_BALANCE_BASE_THRASHING
void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages);
#endif
void workingset_refault(struct page *page, void *shadow);
void workingset_activation(struct page *page);


void workingset_update_node(struct xa_node *node);
#define mapping_set_update(xas, mapping) do {				\
	if (!dax_mapping(mapping) && !shmem_mapping(mapping))		\
		xas_set_update(xas, workingset_update_node);		\
} while (0)


extern unsigned long totalreserve_pages;
extern unsigned long nr_free_buffer_pages(void);
extern unsigned long nr_free_pagecache_pages(void);


#define nr_free_pages() global_zone_page_state(NR_FREE_PAGES)



extern void lru_cache_add(struct page *);
#ifdef CONFIG_LRU_BALANCE_BASE_THRASHING
extern void lru_note_cost(struct lruvec *lruvec, bool file,
			  unsigned int nr_pages);
extern void lru_note_cost_page(struct page *page);
#else
extern void lru_cache_add_anon(struct page *page);
extern void lru_cache_add_file(struct page *page);
#endif
extern void lru_add_page_tail(struct page *page, struct page *page_tail,
			 struct lruvec *lruvec, struct list_head *head);
extern void activate_page(struct page *);
extern void mark_page_accessed(struct page *);
extern void lru_add_drain(void);
extern void lru_add_drain_cpu(int cpu);
extern void lru_add_drain_all(void);
extern void rotate_reclaimable_page(struct page *page);
extern void deactivate_file_page(struct page *page);
extern void deactivate_page(struct page *page);
extern void mark_page_lazyfree(struct page *page);
extern void swap_setup(void);

extern void __lru_cache_add_active_or_unevictable(struct page *page,
						unsigned long vma_flags);

static inline void lru_cache_add_active_or_unevictable(struct page *page,
						struct vm_area_struct *vma)
{
	return __lru_cache_add_active_or_unevictable(page, vma->vm_flags);
}


extern unsigned long zone_reclaimable_pages(struct zone *zone);
extern unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
					gfp_t gfp_mask, nodemask_t *mask);
extern int __isolate_lru_page(struct page *page, isolate_mode_t mode);
extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
						  unsigned long nr_pages,
						  gfp_t gfp_mask,
						  bool may_swap);
extern unsigned long mem_cgroup_shrink_node(struct mem_cgroup *mem,
						gfp_t gfp_mask, bool noswap,
						pg_data_t *pgdat,
						unsigned long *nr_scanned);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
#ifdef CONFIG_DIRECT_SWAPPINESS
extern int direct_vm_swappiness;
#endif
extern int remove_mapping(struct address_space *mapping, struct page *page);
extern unsigned long vm_total_pages;
#ifdef CONFIG_RCC
#define RCC_MODE_ANON   1
#define RCC_MODE_FILE   2
extern int try_to_free_pages_ex(int nr_pages, int mode);
#endif

extern unsigned long reclaim_pages(struct list_head *page_list);
#ifdef CONFIG_NUMA
extern int node_reclaim_mode;
extern int sysctl_min_unmapped_ratio;
extern int sysctl_min_slab_ratio;
#else
#define node_reclaim_mode 0
#endif

extern int page_evictable(struct page *page);
extern void check_move_unevictable_pages(struct pagevec *pvec);

extern int kswapd_run(int nid);
extern void kswapd_stop(int nid);

#ifdef CONFIG_SWAP

#include <linux/blk_types.h> 


extern int swap_readpage(struct page *page, bool do_poll);
extern int swap_writepage(struct page *page, struct writeback_control *wbc);
extern void end_swap_bio_write(struct bio *bio);
extern int __swap_writepage(struct page *page, struct writeback_control *wbc,
	bio_end_io_t end_write_func);
extern int swap_set_page_dirty(struct page *page);

int add_swap_extent(struct swap_info_struct *sis, unsigned long start_page,
		unsigned long nr_pages, sector_t start_block);
int generic_swapfile_activate(struct swap_info_struct *, struct file *,
		sector_t *);



#define SWAP_ADDRESS_SPACE_SHIFT	14
#define SWAP_ADDRESS_SPACE_PAGES	(1 << SWAP_ADDRESS_SPACE_SHIFT)
extern struct address_space *swapper_spaces[];
#define swap_address_space(entry)			    \
	(&swapper_spaces[swp_type(entry)][swp_offset(entry) \
		>> SWAP_ADDRESS_SPACE_SHIFT])
extern unsigned long total_swapcache_pages(void);
#ifdef CONFIG_SPRD_SYSDUMP
extern unsigned long total_swapcache_pages_nolock(void);
#endif
extern void show_swap_cache_info(void);
extern int add_to_swap(struct page *page);
extern int add_to_swap_cache(struct page *, swp_entry_t, gfp_t);
extern int __add_to_swap_cache(struct page *page, swp_entry_t entry);
extern void __delete_from_swap_cache(struct page *, swp_entry_t entry);
extern void delete_from_swap_cache(struct page *);
extern void free_page_and_swap_cache(struct page *);
extern void free_pages_and_swap_cache(struct page **, int);
extern struct page *lookup_swap_cache(swp_entry_t entry,
				      struct vm_area_struct *vma,
				      unsigned long addr);
extern struct page *read_swap_cache_async(swp_entry_t, gfp_t,
			struct vm_area_struct *vma, unsigned long addr,
			bool do_poll);
extern struct page *__read_swap_cache_async(swp_entry_t, gfp_t,
			struct vm_area_struct *vma, unsigned long addr,
			bool *new_page_allocated);
extern struct page *swap_cluster_readahead(swp_entry_t entry, gfp_t flag,
				struct vm_fault *vmf);
extern struct page *swapin_readahead(swp_entry_t entry, gfp_t flag,
				struct vm_fault *vmf);


extern atomic_long_t nr_swap_pages;
extern long total_swap_pages;
extern atomic_t nr_rotate_swap;
extern bool has_usable_swap(void);


static inline bool vm_swap_full(void)
{
	return atomic_long_read(&nr_swap_pages) * 2 < total_swap_pages;
}

static inline long get_nr_swap_pages(void)
{
	return atomic_long_read(&nr_swap_pages);
}

extern void si_swapinfo(struct sysinfo *);
#ifdef CONFIG_SPRD_SYSDUMP
extern void si_swapinfo_nolock(struct sysinfo *);
#endif
extern swp_entry_t get_swap_page(struct page *page);
extern void put_swap_page(struct page *page, swp_entry_t entry);
extern swp_entry_t get_swap_page_of_type(int);
extern int get_swap_pages(int n, swp_entry_t swp_entries[], int entry_size);
extern int add_swap_count_continuation(swp_entry_t, gfp_t);
extern void swap_shmem_alloc(swp_entry_t);
extern int swap_duplicate(swp_entry_t);
extern int swapcache_prepare(swp_entry_t);
extern void swap_free(swp_entry_t);
extern void swapcache_free_entries(swp_entry_t *entries, int n);
extern int free_swap_and_cache(swp_entry_t);
extern int swap_type_of(dev_t, sector_t, struct block_device **);
extern unsigned int count_swap_pages(int, int);
extern sector_t map_swap_page(struct page *, struct block_device **);
extern sector_t swapdev_block(int, pgoff_t);
extern int page_swapcount(struct page *);
extern int __swap_count(swp_entry_t entry);
extern int __swp_swapcount(swp_entry_t entry);
extern int swp_swapcount(swp_entry_t entry);
extern struct swap_info_struct *page_swap_info(struct page *);
extern struct swap_info_struct *swp_swap_info(swp_entry_t entry);
extern bool reuse_swap_page(struct page *, int *);
extern int try_to_free_swap(struct page *);
struct backing_dev_info;
extern int init_swap_address_space(unsigned int type, unsigned long nr_pages);
extern void exit_swap_address_space(unsigned int type);
extern struct swap_info_struct *get_swap_device(swp_entry_t entry);
#ifdef CONFIG_SPRD_SYSDUMP
extern struct swap_info_struct *get_swap_device_nolock(swp_entry_t entry);
#endif
sector_t swap_page_sector(struct page *page);

static inline void put_swap_device(struct swap_info_struct *si)
{
	rcu_read_unlock();
}

#else 

static inline int swap_readpage(struct page *page, bool do_poll)
{
	return 0;
}

static inline struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return NULL;
}

#define swap_address_space(entry)		(NULL)
#define get_nr_swap_pages()			0L
#define total_swap_pages			0L
#define total_swapcache_pages()			0UL
#define vm_swap_full()				0

#define si_swapinfo(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)
#ifdef CONFIG_SPRD_SYSDUMP
#define total_swapcache_pages_nolock()		0UL
#define si_swapinfo_nolock(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)
#endif

#define free_page_and_swap_cache(page) \
	put_page(page)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

static inline void show_swap_cache_info(void)
{
}

#define free_swap_and_cache(e) ({(is_migration_entry(e) || is_device_private_entry(e));})
#define swapcache_prepare(e) ({(is_migration_entry(e) || is_device_private_entry(e));})

static inline int add_swap_count_continuation(swp_entry_t swp, gfp_t gfp_mask)
{
	return 0;
}

static inline void swap_shmem_alloc(swp_entry_t swp)
{
}

static inline int swap_duplicate(swp_entry_t swp)
{
	return 0;
}

static inline void swap_free(swp_entry_t swp)
{
}

static inline void put_swap_page(struct page *page, swp_entry_t swp)
{
}

static inline struct page *swap_cluster_readahead(swp_entry_t entry,
				gfp_t gfp_mask, struct vm_fault *vmf)
{
	return NULL;
}

static inline struct page *swapin_readahead(swp_entry_t swp, gfp_t gfp_mask,
			struct vm_fault *vmf)
{
	return NULL;
}

static inline int swap_writepage(struct page *p, struct writeback_control *wbc)
{
	return 0;
}

static inline struct page *lookup_swap_cache(swp_entry_t swp,
					     struct vm_area_struct *vma,
					     unsigned long addr)
{
	return NULL;
}

static inline int add_to_swap(struct page *page)
{
	return 0;
}

static inline int add_to_swap_cache(struct page *page, swp_entry_t entry,
							gfp_t gfp_mask)
{
	return -1;
}

static inline void __delete_from_swap_cache(struct page *page,
							swp_entry_t entry)
{
}

static inline void delete_from_swap_cache(struct page *page)
{
}

static inline int page_swapcount(struct page *page)
{
	return 0;
}

static inline int __swap_count(swp_entry_t entry)
{
	return 0;
}

static inline int __swp_swapcount(swp_entry_t entry)
{
	return 0;
}

static inline int swp_swapcount(swp_entry_t entry)
{
	return 0;
}

#define reuse_swap_page(page, total_map_swapcount) \
	(page_trans_huge_mapcount(page, total_map_swapcount) == 1)

static inline int try_to_free_swap(struct page *page)
{
	return 0;
}

static inline swp_entry_t get_swap_page(struct page *page)
{
	swp_entry_t entry;
	entry.val = 0;
	return entry;
}

#endif 

#ifdef CONFIG_THP_SWAP
extern int split_swap_cluster(swp_entry_t entry);
#else
static inline int split_swap_cluster(swp_entry_t entry)
{
	return 0;
}
#endif

#ifdef CONFIG_MEMCG
static inline int mem_cgroup_swappiness(struct mem_cgroup *memcg)
{
	
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return vm_swappiness;

	
	if (mem_cgroup_disabled() || mem_cgroup_is_root(memcg))
		return vm_swappiness;

	return memcg->swappiness;
}
#else
static inline int mem_cgroup_swappiness(struct mem_cgroup *mem)
{
	return vm_swappiness;
}
#endif

#if defined(CONFIG_SWAP) && defined(CONFIG_MEMCG) && defined(CONFIG_BLK_CGROUP)
extern void mem_cgroup_throttle_swaprate(struct mem_cgroup *memcg, int node,
					 gfp_t gfp_mask);
#else
static inline void mem_cgroup_throttle_swaprate(struct mem_cgroup *memcg,
						int node, gfp_t gfp_mask)
{
}
#endif

#ifdef CONFIG_MEMCG_SWAP
extern void mem_cgroup_swapout(struct page *page, swp_entry_t entry);
extern int mem_cgroup_try_charge_swap(struct page *page, swp_entry_t entry);
extern void mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages);
extern long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg);
extern bool mem_cgroup_swap_full(struct page *page);
#else
static inline void mem_cgroup_swapout(struct page *page, swp_entry_t entry)
{
}

static inline int mem_cgroup_try_charge_swap(struct page *page,
					     swp_entry_t entry)
{
	return 0;
}

static inline void mem_cgroup_uncharge_swap(swp_entry_t entry,
					    unsigned int nr_pages)
{
}

static inline long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	return get_nr_swap_pages();
}

static inline bool mem_cgroup_swap_full(struct page *page)
{
#ifdef CONFIG_WRITEBACK_SWAPCACHE
	return writeback_swapcache ? true : vm_swap_full();
#else
	return vm_swap_full();
#endif
}
#endif

#endif 
#endif 
