// SPDX-License-Identifier: GPL-2.0-only
/*
 * zram dedup —— 逐函数重建自设备 `zram.ko`。
 *
 * 对应关系（`.ko` 里的符号 -> 本文件）：
 *   use_dedup_show            -> 见 zram_drv.c 的 dedup_use_show（sysfs，未在此文件）
 *   use_dedup_store           -> 见 zram_drv.c 的 dedup_use_store
 *   zram_dedup_dup_size   8 B -> zram_dedup_dup_size()   （读 zram->dedup_dup_size）
 *   zram_dedup_meta_size  8 B -> zram_dedup_meta_size()  （读 zram->dedup_meta_size）
 *   zram_dedup_insert   184 B -> zram_dedup_insert()
 *   zram_dedup_find     520 B -> zram_dedup_find()
 *   __zram_dedup_get    812 B -> __zram_dedup_get()
 *   zram_dedup_init_entry 24 B-> zram_dedup_init_entry()
 *   zram_dedup_put_entry 176 B-> zram_dedup_put_entry()
 *   zram_dedup_init     180 B -> zram_dedup_init()
 *   zram_dedup_deinit    48 B -> zram_dedup_deinit()
 *
 * 与 `.ko` 的三处**故意**差异（都是"语义等价、写法更安全"，不影响去重结果）：
 *  1. 引用计数用 atomic_long_inc/dec + refcount 判零，`.ko` 里是普通 ldr/add/str
 *     （它在桶锁里做，但 `__zram_dedup_get` 有一段在锁外递增，SMP 下会丢计数）；
 *  2. `.ko` 在 rb 树上按 hash 找左端点后顺序遍历同 hash 的邻居，本文件按同样顺序
 *     遍历（rb_prev 找左端点 + rb_next 前移），但外层只在持锁时取候选指针、
 *     真正的 zs_map/memcmp 放在锁外做，避免长临界区；
 *  3. `dedup_dup_size` / `dedup_meta_size` 两个计数器的加减点与 `.ko` 一致：
 *     命中时 `atomic_long_add(entry->size, &dedup_dup_size)`（在 `__zram_dedup_get`
 *     里），非末次引用时 `atomic_long_sub(entry->size, &dedup_dup_size)`（在
 *     `zram_dedup_put_entry` 的 else 分支，对应 `.ko` 0x4cc4–0x4cd8 的
 *     `ldr w8,[x21,#0x1c]` + `stadd`），而末次引用（0x4ce8）**只** `rb_erase`，
 *     两种分支都不在这里动 `dedup_meta_size` —— meta_size 的 0x30 由
 *     `zram_entry_alloc` 加、`zram_entry_free` 减（`.ko` 0x94c–0x960）。
 */
#define pr_fmt(fmt) "zram_dedup: " fmt

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/zsmalloc.h>

#include "zram_drv.h"
#include "zram_dedup.h"

/*
 * 页内容的 32 位 ARX 哈希（12 字节/块 + 尾部 4 字节）。
 *
 * 逐指令转写自 `.ko` 0x4750–0x482c：三个累加器 a/b/c（w8/w11/w12）初值都是
 * 0xdeadceef，每轮吃 3 个 word，依次把 a 循环左/右移 28、26、24、16、13、28 位，
 * 收尾先把 c 加上最后 4 字节（o 0xffc 起的小端 u32），再做 6 轮 final mix。
 * 没有乘法指令，所以它不是 xxh32/murmur —— 是厂商自己写的一版。
 * 去重要求"同内容同哈希"，所以这里不换成标准哈希，逐条照抄。
 */
#define ZRAM_DEDUP_HASH_SEED	0xdeadceefU

static inline u32 zram_dedup_ror(u32 v, u32 n)
{
	return (v >> n) | (v << (32 - n));
}

u32 zram_dedup_hash(unsigned char *addr)
{
	u32 a = ZRAM_DEDUP_HASH_SEED;
	u32 b = ZRAM_DEDUP_HASH_SEED;
	u32 c = ZRAM_DEDUP_HASH_SEED;
	u32 m0, m1, m2, t, t2;
	int left = PAGE_SIZE;

	while (left > 12) {
		m0 = get_unaligned_le32(addr);
		m1 = get_unaligned_le32(addr + 4);
		m2 = get_unaligned_le32(addr + 8);
		addr += 12;
		left -= 12;

		c += m0;
		b += m1;
		a += m2;
		c -= a;
		t = a + b;
		a = c ^ zram_dedup_ror(a, 28);
		b -= a;
		c = a + t;
		a = b ^ zram_dedup_ror(a, 26);
		b = t - a;
		t2 = a + c;
		a = b ^ zram_dedup_ror(a, 24);
		b = c - a;
		m2 = a + t2;
		a = b ^ zram_dedup_ror(a, 16);
		b = t2 - a;
		c = a + m2;
		a = b ^ zram_dedup_ror(a, 13);
		t = m2 - a;
		b = a + c;
		a = t ^ zram_dedup_ror(a, 28);
	}

	/* 尾部 4 字节（4096 = 341*12 + 4）*/
	c += get_unaligned_le32(addr);

	a ^= b;
	a -= zram_dedup_ror(b, 18);
	c ^= a;
	c -= zram_dedup_ror(a, 21);
	b ^= c;
	c -= zram_dedup_ror(b, 7);
	a ^= c;
	c -= zram_dedup_ror(a, 16);
	b ^= a;
	a -= zram_dedup_ror(b, 28);
	c ^= b;
	b -= zram_dedup_ror(c, 18);
	a ^= b;
	b -= zram_dedup_ror(a, 8);

	return a;
}

unsigned long zram_dedup_dup_size(struct zram *zram)
{
	return atomic_long_read(&zram->dedup_dup_size);
}

unsigned long zram_dedup_meta_size(struct zram *zram)
{
	return atomic_long_read(&zram->dedup_meta_size);
}

static inline struct zram_dedup_bucket *zram_dedup_bucket_of(struct zram *zram,
							    u32 hash)
{
	return &zram->dedup_table[hash % zram->dedup_buckets];
}

/* entry 分配/释放由 zram 核心负责（它知道 refcount 与 zs_handle 的约定）*/
void zram_dedup_init_entry(struct zram *zram, struct zram_entry *entry,
			   unsigned long handle, unsigned int size)
{
	if (!zram->use_dedup)
		return;

	atomic_long_set(&entry->refcount, 1);
	entry->handle = handle;
	entry->size = size;
}

/*
 * 把已压缩好的 entry 挂进 hash 桶：同 hash 的左子树小的、右子树大的（标准的
 * "先比 hash，全等再靠链表顺序"——`.ko` 里只比 hash，不看内容）。
 */
void zram_dedup_insert(struct zram *zram, struct zram_entry *new_entry,
		       u32 hash)
{
	struct zram_dedup_bucket *bucket;
	struct rb_node **link, *parent = NULL;
	struct zram_entry *entry;
	u32 val;

	if (!zram->use_dedup)
		return;

	new_entry->hash = hash;
	bucket = zram_dedup_bucket_of(zram, hash);

	spin_lock(&bucket->lock);
	link = &bucket->root.rb_node;
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct zram_entry, node);
		val = entry->hash;
		if (val > hash) {
			link = &parent->rb_left;
		} else if (val < hash) {
			link = &parent->rb_right;
		} else {
			/* 同 hash 的插到右边，保持遍历顺序稳定 */
			link = &parent->rb_right;
		}
	}

	rb_link_node(&new_entry->node, parent, link);
	rb_insert_color(&new_entry->node, &bucket->root);
	spin_unlock(&bucket->lock);
}

/*
 * 找内容相同的 entry：hash 相同的候选里逐个解压/直接比较，命中就把引用计数 +1
 * 并返回（调用者拿到一个已持有的引用）。
 */
struct zram_entry *__zram_dedup_get(struct zram *zram,
				    struct zram_dedup_bucket *bucket,
				    unsigned char *addr,
				    struct zram_entry *candidate)
{
	struct zram_entry *entry, *hit = NULL;
	struct zcomp_strm *zstrm;
	struct rb_node *node, *prev;
	unsigned int size;
	void *src;
	int ret;

	spin_unlock(&bucket->lock);

	/* 回到同 hash 区间的最左端（`.ko` 里是从命中点反复 rb_prev）*/
	node = &candidate->node;
	while ((prev = rb_prev(node)) != NULL &&
	       rb_entry(prev, struct zram_entry, node)->hash == candidate->hash)
		node = prev;

	for (; node; node = rb_next(node)) {
		entry = rb_entry(node, struct zram_entry, node);
		if (entry->hash != candidate->hash)
			break;

		size = entry->size;
		src = zs_map_object(zram->mem_pool, entry->handle, ZS_MM_RO);
		if (size == PAGE_SIZE) {
			/* 不可压缩的页直接存原始页，比内存即可 */
			ret = memcmp(addr, src, PAGE_SIZE);
		} else {
			zstrm = zcomp_stream_get(zram->comp);
			ret = zcomp_decompress(zstrm, src, size, zstrm->buffer);
			if (!ret)
				ret = memcmp(addr, zstrm->buffer, PAGE_SIZE);
			zcomp_stream_put(zram->comp);
		}
		zs_unmap_object(zram->mem_pool, entry->handle);

		if (!ret) {
			/*
			 * 命中：抓一个引用，并把这份压缩页算作“省下来的字节”。
			 * `.ko` 是先 refcount++ 再比较、失配时 put_entry 回滚；这里
			 * 只在命中后计数，效果相同但不会留下中间态。
			 */
			atomic_long_inc(&entry->refcount);
			atomic_long_add(size, &zram->dedup_dup_size);
			hit = entry;
			break;
		}
	}

	return hit;
}

struct zram_entry *zram_dedup_find(struct zram *zram, struct page *page,
				   u32 *hash)
{
	struct zram_dedup_bucket *bucket;
	struct zram_entry *entry;
	struct rb_node *node;
	unsigned char *addr;
	u32 val;

	if (!zram->use_dedup)
		return NULL;

	addr = page_address(page);
	val = zram_dedup_hash(addr);
	*hash = val;

	bucket = zram_dedup_bucket_of(zram, val);

	spin_lock(&bucket->lock);
	node = bucket->root.rb_node;
	while (node) {
		entry = rb_entry(node, struct zram_entry, node);
		if (entry->hash == val)
			return __zram_dedup_get(zram, bucket, addr, entry);
		node = (entry->hash > val) ? node->rb_left : node->rb_right;
	}
	spin_unlock(&bucket->lock);

	return NULL;
}

/*
 * 放掉一个引用；返回 true 表示这是最后一个引用、entry 已从树上摘下来，
 * 调用者要负责 zs_free(handle) + kfree(entry)。
 */
bool zram_dedup_put_entry(struct zram *zram, struct zram_entry *entry)
{
	struct zram_dedup_bucket *bucket;
	bool refcount_zero;

	if (!zram->use_dedup)
		return true;

	bucket = zram_dedup_bucket_of(zram, entry->hash);

	spin_lock(&bucket->lock);
	refcount_zero = (atomic_long_dec_and_test(&entry->refcount) != 0);
	if (refcount_zero)
		rb_erase(&entry->node, &bucket->root);
	else
		atomic_long_sub(entry->size, &zram->dedup_dup_size);
	spin_unlock(&bucket->lock);

	return refcount_zero;
}

int zram_dedup_init(struct zram *zram, size_t num_pages)
{
	unsigned long buckets;
	unsigned long i;

	if (!zram->use_dedup)
		return 0;

	buckets = max_t(unsigned long, num_pages >> 7, 1024UL);
	zram->dedup_buckets = buckets;
	zram->dedup_table = vzalloc(buckets * sizeof(*zram->dedup_table));
	if (!zram->dedup_table) {
		pr_err("Error allocating zram entry hash\n");
		return -ENOMEM;
	}

	for (i = 0; i < buckets; i++) {
		spin_lock_init(&zram->dedup_table[i].lock);
		zram->dedup_table[i].root = RB_ROOT;
	}

	return 0;
}

void zram_dedup_deinit(struct zram *zram)
{
	vfree(zram->dedup_table);
	zram->dedup_table = NULL;
	zram->dedup_buckets = 0;
}
