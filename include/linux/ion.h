/* SPDX-License-Identifier: GPL-2.0-only */


#ifndef _ION_KERNEL_H
#define _ION_KERNEL_H

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/shrinker.h>
#include <linux/types.h>
#include <uapi/linux/ion.h>

#ifdef CONFIG_E_SHOW_MEM
#define MAX_MAP_USER    15

struct ion_map_info {
	pid_t pid;
	char task_name[TASK_COMM_LEN];
	struct timespec64 map_ts;
};
#endif


struct ion_buffer {
#ifdef CONFIG_E_SHOW_MEM
	union {
		struct rb_node node;
		struct list_head list;
	};
#else
	struct list_head list;
#endif
	struct ion_heap *heap;
	unsigned long flags;
	unsigned long private_flags;
	size_t size;
	void *priv_virt;
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	struct sg_table *sg_table;
	struct list_head attachments;
#ifdef CONFIG_E_SHOW_MEM
	pid_t pid;
	char task_name[TASK_COMM_LEN];
	struct timespec64 alloc_ts;
	struct ion_map_info mappers[MAX_MAP_USER];
#endif
};


struct ion_heap_ops {
	int (*allocate)(struct ion_heap *heap,
			struct ion_buffer *buffer, unsigned long len,
			unsigned long flags);
	void (*free)(struct ion_buffer *buffer);
	int (*shrink)(struct ion_heap *heap, gfp_t gfp_mask, int nr_to_scan);
	long (*get_pool_size)(struct ion_heap *heap);
};


#define ION_HEAP_FLAG_DEFER_FREE BIT(0)



#define ION_PRIV_FLAG_SHRINKER_FREE BIT(0)


struct ion_heap {
	struct plist_node node;
	enum ion_heap_type type;
	struct ion_heap_ops *ops;
	struct dma_buf_ops buf_ops;
	unsigned long flags;
	unsigned int id;
	const char *name;
	struct module *owner;

	
	struct shrinker shrinker;
	struct list_head free_list;
	size_t free_list_size;
	spinlock_t free_lock;
	wait_queue_head_t waitqueue;
	struct task_struct *task;

	
	u64 num_of_buffers;
	u64 num_of_alloc_bytes;
	u64 alloc_bytes_wm;

	
	spinlock_t stat_lock;

	
	struct dentry *debugfs_dir;
};

#define ion_device_add_heap(heap) __ion_device_add_heap(heap, THIS_MODULE)


struct ion_dma_buf_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped:1;
};

#ifdef CONFIG_ION


int __ion_device_add_heap(struct ion_heap *heap, struct module *owner);


void ion_device_remove_heap(struct ion_heap *heap);


int ion_heap_init_shrinker(struct ion_heap *heap);


int ion_heap_init_deferred_free(struct ion_heap *heap);


void ion_heap_freelist_add(struct ion_heap *heap, struct ion_buffer *buffer);


size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size);


size_t ion_heap_freelist_shrink(struct ion_heap *heap,
				size_t size);


size_t ion_heap_freelist_size(struct ion_heap *heap);


void *ion_heap_map_kernel(struct ion_heap *heap, struct ion_buffer *buffer);


void ion_heap_unmap_kernel(struct ion_heap *heap, struct ion_buffer *buffer);


int ion_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
		      struct vm_area_struct *vma);


int ion_buffer_zero(struct ion_buffer *buffer);


void ion_buffer_prep_noncached(struct ion_buffer *buffer);


struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags);


int ion_free(struct ion_buffer *buffer);



size_t ion_query_heaps_kernel(struct ion_heap_data *hdata, size_t size);

long get_ion_heap_total_pages(void);
long get_ion_pool_total_pages(void);
#else

static inline int __ion_device_add_heap(struct ion_heap *heap,
				      struct module *owner)
{
	return -ENODEV;
}

static inline int ion_heap_init_shrinker(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline int ion_heap_init_deferred_free(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline void ion_heap_freelist_add(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size)
{
	return -ENODEV;
}

static inline size_t ion_heap_freelist_shrink(struct ion_heap *heap,
					      size_t size)
{
	return -ENODEV;
}

static inline size_t ion_heap_freelist_size(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline void *ion_heap_map_kernel(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
	return ERR_PTR(-ENODEV);
}

static inline void ion_heap_unmap_kernel(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline int ion_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	return -ENODEV;
}

static inline int ion_buffer_zero(struct ion_buffer *buffer)
{
	return -EINVAL;
}

static inline void ion_buffer_prep_noncached(struct ion_buffer *buffer) {}

static inline struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
					unsigned int flags)
{
	return ERR_PTR(-ENOMEM);
}

static inline int ion_free(struct ion_buffer *buffer)
{
	return 0;
}

static inline size_t ion_query_heaps_kernel(struct ion_heap_data *hdata,
					 size_t size)
{
	return 0;
}
#endif 
#endif 
