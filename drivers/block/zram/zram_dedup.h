/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * zram dedup —— 规格说明与接口（实现见 zram_dedup.c）。
 *
 * 设备 `zram.ko` 的 `struct zram` 里跟去重有关的字段（偏移取自反汇编）：
 *   +0x20 struct zram_dedup_bucket *dedup_table   vzalloc 的桶数组
 *   +0x28 unsigned long dedup_buckets             max(nr_pages >> 7, 1024)
 *   +0xe8 atomic_long_t dedup_dup_size            去重省下的压缩字节数
 *   +0xf0 atomic_long_t dedup_meta_size           entry 元数据（48 B/个）
 *   +0x1c0 bool use_dedup                         只能在 disksize == 0 时切换
 *   +0x30 struct rw_semaphore init_lock            sysfs 读写在它下面加锁
 *
 * 桶 16 字节：{ spinlock_t lock; struct rb_root root; }；
 * entry 48 字节：{ rb_node(24) / u32 hash / u32 size / atomic_long refcount /
 *                  unsigned long handle }。
 */
#ifndef _ZRAM_DEDUP_H_
#define _ZRAM_DEDUP_H_

#include "zram_drv.h"

u32 zram_dedup_hash(unsigned char *addr);
void zram_dedup_insert(struct zram *zram, struct zram_entry *new_entry, u32 hash);
struct zram_entry *zram_dedup_find(struct zram *zram, struct page *page, u32 *hash);
struct zram_entry *__zram_dedup_get(struct zram *zram,
				    struct zram_dedup_bucket *bucket,
				    unsigned char *addr,
				    struct zram_entry *candidate);
void zram_dedup_init_entry(struct zram *zram, struct zram_entry *entry,
			   unsigned long handle, unsigned int size);
bool zram_dedup_put_entry(struct zram *zram, struct zram_entry *entry);
int zram_dedup_init(struct zram *zram, size_t num_pages);
void zram_dedup_deinit(struct zram *zram);

unsigned long zram_dedup_dup_size(struct zram *zram);
unsigned long zram_dedup_meta_size(struct zram *zram);

#else /* !CONFIG_ZRAM_DEDUP */

/* 关掉去重时核心仍然走 entry 层（entry 就是 zs 句柄本身），
 * 所以只需要 put_entry 一个"恒为真"的桩，其余接口退化成空操作 —— 但
 * __zram_bvec_write 无条件调用 find/insert，所以这两个也要给桩，
 * 这样核心代码里就不用再包一层 #ifdef。 */
static inline bool zram_dedup_put_entry(struct zram *zram,
					struct zram_entry *entry)
{
	return true;
}

static inline struct zram_entry *zram_dedup_find(struct zram *zram,
						 struct page *page,
						 u32 *hash)
{
	return NULL;
}

static inline void zram_dedup_insert(struct zram *zram,
				     struct zram_entry *new_entry, u32 hash)
{
}

static inline int zram_dedup_init(struct zram *zram, size_t num_pages)
{
	return 0;
}

static inline void zram_dedup_deinit(struct zram *zram)
{
}

#endif
