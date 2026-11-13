# 从二进制逆向重建内核 —— 完整过程记录

本文记录 **Sunelan Q968（Unisoc UMS9620，Android 12）** 内核的逆向移植全过程：
每一步做了什么、依据什么证据、遇到什么、怎么解决。

读完应该能明白：**为什么这个仓库里的每一行展锐代码，都是设备里真实存在的**。

---

## 0. 问题的形状

设备出厂只给三个东西：

| 输入 | 大小 | 能回答什么问题 |
|---|---|---|
| `boot_a.bin`（含 `Image`） | 67,108,864 B（内核 38,308,352 B） | 有哪些函数、每个函数的机器码、有哪些字符串 |
| `vendor_boot_a.bin` | 104,857,600 B（ramdisk 23 MB，157 个 `.ko`） | 厂商哪些驱动是模块、模块依赖哪些内核符号 |
| 运行中的设备（root） | — | `/proc/kallsyms`、`/proc/config.gz`、`/vendor` 下的 kheaders |

**没有的东西**：任何 `.c`/`.h` 源码、BuildConfig、`System.map`、厂商补丁集。

**目标**：产出一个内核，满足
① 能编译 ② 能开机 ③ 能加载全部 157 个原厂模块 ④ 硬件通路正常 ⑤ 行为与原厂一致。

**约束**（都是踩出来的）：
* 必须与原厂 `vermagic` 完全一致，否则模块全部拒绝加载（§5.2）
* 尽量不改 `vendor_boot` 分区，否则触发 AVB（§5.3）
* `boot` 分区尾部必须有 AVB 结构，即使 unlocked（§5.4）

---

## 1. 素材采集

### 1.1 符号表（最强的存在性证据）

```bash
adb shell "su -c 'cat /proc/kallsyms'" > _stock/kallsyms_full.txt
```

结果 212,012 行。格式 `地址 类型 名字 [模块名]`。三个关键点：

1. **必须按地址区分内核与模块。** 有些模块符号不带 `[模块名]` 后缀，
   只靠 `len(fields)==3` 会漏；可靠的判据是地址区间：vmlinux 全部落在
   `0xffffffc010000000`–`0xffffffc011000000` 之间，模块在
   `0xffffffc012000000` 以上。
2. **类型字段**：`T/t` = 文本（函数），`D/d/B/b` = 数据，`R/r` = 只读数据，
   `W/w` = 弱符号。只统计 `tT wW` 即得 vmlinux 函数集。
3. **宏生成的符号必须剔除**，否则差集会全是噪声。剔除规则：
   `__do_sys_*`/`__se_sys_*`（`SYSCALL_DEFINE` 生成）、`__pi_*`（cache alias）、
   `trace_event_*`、`__cfi_*`、`__ksymtab_*`、以及 `_show`/`_store` 后缀的
   sysfs 宏产物（用 stem 集合匹配，见 §2.2）。

去噪后 **设备 vmlinux 函数 = 49,972 个**。

### 1.2 头文件真身（最强的契约证据）

```bash
adb shell "su -c 'ls /vendor/lib/modules/'"   # 也可能在 /system/lib/modules
adb pull /vendor/kheaders.tar.xz _stock/
```

7,477 个头文件，是**编译这个内核时用的原样头文件**。它的价值在于：
结构体成员、函数原型、`__GENKSYMS__` 分支、`#define` 全部可信。

例：设备 `include/linux/rmap.h` 的

```c
struct anon_vma *page_lock_anon_vma_read(struct page *page);        /* 1 参数 */
bool try_to_unmap(struct page *, enum ttu_flags flags, struct vm_area_struct *vma);  /* 3 参数 */
```

一行就否定了"照抄 android-5.10 上游"的可能（上游是 2 参数 / 2 参数）。

### 1.3 Image 与字符串

```bash
adb shell "su -c 'dd if=/dev/block/by-name/boot_a of=/tmp/boot.img'"   # 或从 BF 固件包取
# 拆出 Image 后：
strings -t x Image | less
```

Image 结构：`code0=0x91005a4d`（`MZ` + ARM64 指令）、`image_size=0x248a000`、
加载基址 `_text = 0xffffffc010080000`。因此**任意虚拟地址 → 文件偏移**：

```python
off = va - 0xffffffc010080000
```

用它读 `.rodata` 字符串，判断某个功能是否存在（字符串不会被 inline 优化掉）：

```python
d = open('recon/raw/Image','rb').read()
i = d.find(b'alloc_mode')          # 0x1c24ae6 → VA 0xffffffc011ca4ae6
i2 = d.find(b'lookup_mode')        # -1 → 设备没有这个功能
```

### 1.4 反汇编切片

按符号表把 `.text` 切成每函数一段，格式便于 grep：

```
### FUNC sprd_keypad_hw_init ffffffc010df3f64 65
  ffffffc010df3f64  stp  x29, x30, [sp, #-0x20]!
  ...
```

`_stock/asm/` 下 27 个分片（`s.s` `m.s` `u.s` `v.s` `f.s` …）。
符号名 + 地址 + 指令数三者齐全，是**语义确认**的主力工具。

### 1.5 其它

| 素材 | 用途 |
|---|---|
| `/proc/config.gz`（或 `recon/raw/config.txt`） | 4,941 项配置，判定功能开关 |
| `vendor_boot` ramdisk（lz4 legacy + newc cpio） | 157 个模块 + `modules.load`/`modules.alias` |
| `vbmeta_a`（`/dev/block/by-name/vbmeta_a`） | chain descriptor，判定哪些分区被校验 |
| `/proc/device-tree/firmware/android/vbmeta/parts` | 校验范围 |
| Ghidra 批量导出伪代码（`_stock/dec/all_sym.c`） | 快速读懂复杂函数（但**不覆盖所有符号**，展锐私有函数常缺） |

---

## 2. 骨架选择与三态分类

### 2.1 骨架

选 **AOSP Android Common Kernel `android12-5.4.210`**，`1f0546b12`。
理由：`vermagic` 的 `5.4.210-android12-9-...` 前缀完全吻合，且 Android 12 的
userspace 依赖（`binder`、`ashmem`、`ion`、`erofs`、`f2fs`）都在。

### 2.2 全树三态分类（`skelcheck.py`）

对每个文件计算 "内容 sha1"，与三个参照比较：

| 参照 | 含义 |
|---|---|
| `git show 1f0546b12:<path>` | GKI 5.4.210 原版 |
| 另一台机器（UMS9620, 5.4.302）同路径 | 可参考的近期实现（**不是**本设备的真身） |
| 当前树 | 待分类对象 |

分类结果（全 68,681 文件 / 源码 51,217）：

```
GKI_SAME          65,983 (源码 49,025)   ← 与 GKI 逐字节相同：骨架本体
A_302              2,226 (     1,944)   ← GKI 没有、302 有：疑似该机型的实现
M_302                102 (        49)   ← 两边都改过
A_NEW_SPRD            75 (        69)   ← 全新文件（展锐）
A_NEW                 63 (        55)
M_CUSTOM_PLAIN        60 (        29)
M_CUSTOM_SPRD         56 (        14)
A_9620_DIFF_SPRD      35 (        34)
A_9620_DIFF            6 (         5)
```

**95.8% 的源码与 GKI 逐字节相同** —— 这既是"骨架"的证据，也是判据：
剩下那 4.2% 才是需要逐个证明的对象。

> 教训：`A_302` 里混着大量"我们曾从 302 搬进来"的文件，与"设备真是 302 实现"
> 是两件事。必须再做 §3 的判定，不能直接采信。

### 2.3 sysfs 宏识别（避免差集全是噪声）

`sysfsmac.py`：一次性用正则从全树抽出
① 所有标识符集合 `idents`（`[A-Za-z_]\w*`）
② 22 种 sysfs 宏的 stem 集合 `stems`
（`DEVICE_ATTR`、`DEVICE_ATTR_RO/RW/WO`、`__ATTR`、`DEFINE_SHOW_ATTRIBUTE`、
`DRIVER_ATTR_*`、`cpufreq_freq_attr_*`、`sdev_attr`、`host_attr` …）

然后对每个差异符号做 O(1) 判断：`name in idents or name in stems` → 宏产物，假缺口。

> 性能教训：首版是「22 种宏 × 每个名字」去大文本里 `find`，600 秒超时。
> 先建两个 set 再查表，秒级完成。**大文件多模式匹配必须先建索引。**

---

## 3. 符号差集（`symgap.py`）

三个方向的集合运算：

```
设备函数集（§1.1 去噪后 49,972）
树标识符集（3,762,878，含全部 .c/.h/.S 的词表）
差集：设备有、树里完全没有        → 真缺口候选
差集：设备有、树无、302 有        → "302 实现"候选（含假缺口）
```

再加一层 `derive()` 判定：去掉 `.NNNN` 后缀、取前缀/后缀 ≥10 字符，
若命中树标识符则视为宏展开/派生（`__do_sys_*`、`active_icc_levels_vccq1` 之类）。

迭代过程：

| 轮次 | 设备有/树无 | 设备有/树无/302 有 | 说明 |
|---|---|---|---|
| 初版 | 5,592（派生后 450） | 90 | 未剔模块、未剔除宏 |
| 修 kallsyms 过滤后 | 450 | 90 | 模块符号靠地址区间剔除 |
| 搬完 302 实现后 | 392 | 11 | 11 个是 Windows 大小写折叠假缺口 |
| 补完设备独有实现后 | 392（其中真缺口 0） | 0 | 剩下全是宏/派生/模块 |

**450 → 392 的构成**（每一类都人工核过）：

| 类别 | 数量 | 判定 |
|---|---|---|
| `__do_sys_*` / `__se_sys_*` | 44 | 宏生成，假 |
| `__pi_*`（cache alias） | 6 | 假 |
| exfat | 54 | **真缺口**（设备用 mainline `fs/exfat`，三棵树都没有） |
| mmc 私有 sysfs | 37 | **真缺口**（见 §8 未做项） |
| `fdt_*` | 23 | `scripts/dtc/libfdt` 索引盲区，假 |
| `deadline_*` / `kyber_*` | 20 | per-priority 宏生成（源文件与 302 diff=0），假 |
| `*_show` / `*_store` | 45 | sysfs 宏，假 |
| 模块符号（地址 > `0xffffffc012000000`） | 60+ | 非 vmlinux |

---

## 4. 设备版本判定（`devver.py`）—— 最重要的一步

差集能告出"缺什么"，但**不能告出"树里的实现是不是设备那一份"**。
`devver.py` 判定每个文件在设备里到底是 5.4.210 版还是 5.4.302 版：

```
A = defs(302) - defs(gki)     # 302 独有的函数名
B = defs(gki) - defs(302)     # 210 独有的函数名
设备命中 A（且不命中 B） → 设备用的是 302 版
设备命中 B（且不命中 A） → 设备用的是 210 版
两边都不命中 → ?
```

198 个候选文件的结果：

```
?  147    DEV=302  46    DEV=210  3    MIXED 2
冲突：TREE_TOO_OLD = 33   ← 树里是 210 版，但设备实际是 302 版 → 回退错了
```

33 个 `TREE_TOO_OLD` 文件的典型证据（每个都是"设备里真有、210 里没有"的函数）：

| 文件 | 设备侧独有命中 |
|---|---|
| `mm/mmap.c` | `__free_vma` `__vm_rcu_put` `__vma_merge` `find_vma_rcu` |
| `mm/memory.c` | `__handle_speculative_fault` `_vm_normal_page` `pte_map_lock` |
| `mm/page_alloc.c` | `calculate_min_free_kbytes` `highatomic_nr_pages` `inc_high_atomic_nr_pages` |
| `mm/vmpressure.c` | `vmpressure_global` `vmpressure_global_init` `vmpressure_memcg` `vmpressure_notifier_register` |
| `mm/vmscan.c` | `reclaim_pages_from_list` |
| `mm/rmap.c` | `__page_add_new_anon_rmap` |
| `mm/swap.c` | `__lru_cache_add_active_or_unevictable` |
| `mm/swapfile.c` | `get_swap_device_nolock` `si_swapinfo_nolock` |
| `mm/swap_state.c` | `total_swapcache_pages_nolock` |
| `mm/vmalloc.c` | `find_vm_area_no_wait` |
| `mm/vmstat.c` | `frag_show_dump` `pagetypeinfo_show{blockcount,free}_dump` `sprd_dump_{vmstat,pagetypeinfo,buddyinfo}` |
| `kernel/power/qos.c` | `core_qos_{apply,read_value,add_request,add_notifier,remove_notifier}` `core_constraints_init` |
| `kernel/cpu.c` | `init_cpu_isolated` |
| `kernel/irq/proc.c` | `sprd_dump_interrupts` |
| `fs/proc/meminfo.c` | `sprd_dump_meminfo` |
| `arch/arm64/include/asm/ptrace.h` | `get_pt_regs` |

同时反证了"点状错、不是全错"：`kernel/fork.c` 判 **DEV=210**（设备有 210 独有的
`copy_init_mm`，302 没有），`init/main.c`、`include/linux/fs.h` 也是 DEV=210。
所以正确做法是**逐文件修**，不是整体回滚。

### 4.1 用反汇编交叉验证判定

`find_vma_rcu`（`_stock/asm/f.s:31165`，`0xffffffc010477aec`，118 条指令）里：

```
ubfx x23, x1, #0x15, #2        ; READ_ONCE(vma->vm_flags) 的相关位
ldaddal                        ; 原子加（vm_ref_count）
[x21, #0x430]                  ; mm_struct 里的某个字段
```

与 302 的 `mm/mmap.c:2411 find_vma_rcu` 的 xarray + RCU 实现逐条对应。
`vmpressure_global`（`_stock/asm/v.s:31281`，198 条指令）里
`mov w8, #0xca; tst w0, w8` 精确对应
`if (!(gfp & (__GFP_HIGHMEM|__GFP_MOVABLE|__GFP_IO|__GFP_FS)))`
（2|8|0x40|0x80 = 0xCA）。

**这两条是"设备真的含 302/5.10 回流实现"的硬证据**，也是推翻"只要 5.4.210 就够"的
直接依据。

> 另一条被推翻的旧论证：曾用"设备没有 `memblock_free_ptr` / `anon_vma_trylock_read` /
> `i_mmap_trylock_read` / `userfaultfd_wp`"来论证设备跑纯 5.4.210。但这四个符号是
> **6.1** 才引入的，本来就不该在 5.4.302 里 —— 论据无效。

---

## 5. 头文件、配置、链接、刷机

### 5.1 头文件层（`khdiff.py` / `kh50.py`）

7,477 个 kheaders 头逐个比较（先做"去注释去空白"的语义归一）：

```
语义相同 5,451
语义不同    50
缺失     1,976  ← 其中 1,975 是编译产物（include/config/* 每符号一个空文件、
                   include/generated/*、arch/arm64/include/generated/*）
真缺失        1  ← include/soc/arc/aux.h（与 arm64 无关，本仓库不引入）
```

50 个语义不同的头，用 `kh50.py` 定性：

```
REVERTED_WRONG     30   ← 树是 GKI 原版，设备是 302 版 → 回退错
DEV_EXTRA_210BASE  11   ← 树基于 210，设备又加了字段
DEV_EXTRA           1
OURS_CUSTOM         8   ← Windows 大小写折叠假报警（HEAD 索引版与设备 diff=0）
```

**处理原则：直接用 kheaders 真身覆盖**，因为它是编译期原样、与设备 `.c` 100% 匹配。

> 踩坑：Windows 文件系统大小写不敏感，`xt_DSCP.h` 与 `xt_dscp.h`、`xt_HL.c` 与
> `xt_hl.c` 无法同时存在。8 个 `include/uapi/linux/netfilter*/` 大写头 + 4 个
> `net/netfilter/*.c` 永远显示为已修改，但 `git show HEAD:<path>` 的内容与 302 版
> 逐字节相同 —— 它们是假改动，**绝不能 commit**，否则会把小写文件的内容写进大写
> 文件的索引条目。

### 5.2 配置层（`kconfcheck.py`）

用 kconfiglib 按 Kconfig 依赖关系求值（不是简单文本对比），把设备 `.config` 的
1,790 个 `=y/=m` 逐个代入：

```
Kconfig symbols    15,450
设备 =y/=m          1,790
resolved 一致       1,755
undefined in tree       0
值不同                35  ← 全是工具链探测项
```

35 个不同的都是 `CC_HAS_*`、`CFI_CLANG`、`LTO_CLANG`、`SHADOW_CALL_STACK`、
`STACKPROTECTOR*`、`JUMP_LABEL`、`RELɌ`、`UBSAN_TRAP`、`DEBUG_INFO_DWARF4`、
`COMPAT_VDSO` 这类"看编译器能力"的符号 —— 本机没有编译器时 kconfiglib 把它们
stub 成 `n`。**配置层没有真实缺口**。

### 5.3 链接期：undefined symbol 的两种处理

第一轮编译能过，链接期一次报 22 个 `undefined symbol`。必须分成两类：

| 类型 | 例子 | 处理 |
|---|---|---|
| 设备里真有 | `sysctl_sched_uclamp_min_to_boost`（`ffffffc012380d00 D`）、`pdc`（`ffffffc0123221d8 D`）、`get_ion_heap_total_pages` | 在树里补定义（照 302 实现 + 设备头声明） |
| 设备里没有 | `vma_data_pages`、`is_mergable_pad_vma`、`split_pad_vma`、`binder_alloc_shrinker_exit` | **删掉调用**或让守卫宏失效 |

举个"守卫宏"的例子：`include/linux/pgsize_migration.h`（Google 2024 的 16 KB
页迁移补丁）在 `mm/mmap.c`、`mm/memory.c`、`fs/proc/task_mmu.c` 里被调用，
设备 kallsyms 里任何一个相关符号都没有。处理方法：

```c
#if 0   /* PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT) */
```

让所有调用点走文件里已有的 `static inline` 空实现。

还有一类是 **Makefile 漏行**：`ufshpb_*`、`sprd_ufs_debug_*` 报 undefined，
源码其实一直都在树里，只是 `drivers/scsi/ufs/Makefile` 缺
`ufshcd-core-y += ufshcd.o ufs-sysfs.o ufs-sprd-debug.o` 与
`ufshcd-core-$(CONFIG_SCSI_UFS_HPB) += ufshpb.o`。

### 5.4 编译收敛：7 轮 CI 的错误分布

| 轮 | 错误数 | 主要问题 |
|---|---|---|
| 1 | 320 行 / 12 类 | `cgroup_procs_write_*` 签名、`irq/proc.c` 字符串断行、`gfp_compaction_allowed`、`uclamp_*`、`try_to_unmap` 参数、f2fs 头 |
| 2 | 118 | sched 私有扩展（`min_cap_cpu_mask`/`is_reserved`/`sched_ktime_clock`）、`RQF_ELV`、`RQ_QOS_IOPRIO`、`tlb_remove_table_sync_one`、`proc_maps_private` 成员、binder prio |
| 3 | 48 | exfat 的 `fs_parameter_description*`、`f2fs_stop_checkpoint` 返回类型、`memcpy_from_page` 重复定义、binder 的 `freeze`/`is_nested` |
| 4 | 17 | `proc_wrapper`、`binder_proc_ext`、binder 的 5 个 Android 12 ioctl 码、`binder_frozen_state_info` |
| 5 | 4 | `binder_state_hashed_fops`、`binder_alloc_shrinker_exit` |
| 6 | 22 undefined | 链接期（见 §5.3） |
| 7 | 3 undefined | 同上 |
| 8 | 48 | `sprd_battery_info.h` / `sprd_tcpm.h` 与设备真身不一致 |
| 9 | 12 | 同上收尾 |
| 10 | 0 | ✅ |

**教训：静态扫描抓不到这类错误。** 写过两个"未声明标识符"扫描器，都返回 0 ——
因为签名变更、参数个数、结构体成员缺失，在"标识符是否存在"这个维度上看不出来。
**编译器的报错就是最高效的静态分析**，`-k`（keep going）+ 去重归类比逐条猜快得多。

### 5.5 刷机：三个必须同时满足的条件

**条件 1：vermagic 完全一致**

```
5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt mod_unload modversions aarch64
```

这条串来自设备 Image 的 `.rodata`（`0xffffffc011d9f518`），并出现在全部 157 个
`.ko` 的 `vermagic=` 里。所以 `CONFIG_LOCALVERSION`（`-android12-9-g96901d45d5a5-ab50`）
与 `CONFIG_LOCALVERSION_AUTO=n` 是**必须的配置**，不是巧合。

**条件 2：boot 分区尾部有 AVB 结构**（见 §5.4 / README §4.5）

`tools/mkboot.py` 的做法：复制原厂 2304 字节 AVB0 段到 `image_end`，末尾写 64 字节
`AVBf`。**不重算 hash**——unlocked 不校验。

**条件 3：`vendor_boot` 与 `misc` 保持干净**

* `vendor_boot` 一个字节都不要改（见 README §4.4）
* `misc` 的 BCB 必须为空 —— `adb reboot fastboot` 会写 `bootonce-bootloader`，
  把设备卡在 bootloop（README §4.8）

---

## 6. 逐子系统移植清单

### 6.1 mm（工作量最大）

| 文件 | 移植内容 |
|---|---|
| `mm/mmap.c` | `find_vma_rcu`、`__vma_merge`、`__free_vma`、`__vm_rcu_put`、`vm_ref_count`/`vm_sequence` 生命周期 |
| `mm/memory.c` | `__handle_speculative_fault`、`_vm_normal_page`、`pte_map_lock`、`vm_normal_page` 分发 |
| `mm/internal.h` | `get_vma`/`put_vma`/`vma_has_changed`、非 spec-fault 的退化路径 |
| `mm/rmap.c` | `__page_add_new_anon_rmap`（保留设备 1 参数 `page_lock_anon_vma_read`、`rmap_walk_control.target_vma`，去掉 6.1 的 `try_lock`/`contended`） |
| `mm/vmpressure.c` | `vmpressure_global`（0xCA 掩码）、`vmpressure_notifier_*`、双路径 `vmpressure()`、`late_initcall` |
| `mm/page_alloc.c` | `calculate_min_free_kbytes`、`highatomic_nr_pages`、`inc_high_atomic_nr_pages`、`sprd_dump_buddyinfo` |
| `mm/vmscan.c` | `reclaim_pages_from_list`、`reclaim_pte_range`、`direct_vm_swappiness`、`writeback_swapcache` |
| `mm/vmstat.c` | `sprd_dump_vmstat`、`watermark_boost`、`CONFIG_LRU_BALANCE_BASE_THRASHING` 的 anon/file 拆分项 |
| `mm/swap*.c` | `__lru_cache_add_active_or_unevictable`（5.10 API）、`get_swap_device_nolock`、`si_swapinfo_nolock`、`total_swapcache_pages_nolock` |
| `mm/vmalloc.c` | `find_vm_area_no_wait` |
| `mm/init-mm.c` | `mm_seq` 初始化 |

关键点：**mm 的实现是"5.4.210 + Android 5.10 回流"的混合体**。
直接抄 302（已升到 6.1 风格）会用到 `anon_vma_trylock_read`、`i_mmap_trylock_read`、
双参数 `page_lock_anon_vma_read`、三参数 `try_to_unmap` 之外的东西 —— 设备全都没有。
`mm/rmap.c` 最后是"以 302 为底 + 7 处精确回退"，逐个函数改，不是整块搬。

### 6.2 sched

设备在 5.4.210 上加了展锐的调度扩展。判据（kallsyms 命中与否）逐个决定：

| 符号 | 设备 | 处理 |
|---|---|---|
| `uclamp_transform_boost` | ✅ `ffffffc0101fb318 T` | 移植实现（9 条指令，与 302 完全一致） |
| `sysctl_sched_uclamp_min_to_boost` | ✅ `ffffffc012380d00 D` | 加变量 + sysctl 表项 |
| `runqueues_push_task` | ✅ `ffffffc012336580 D` | 加 `DECLARE_PER_CPU_SHARED_ALIGNED` |
| `min_cap_cpu_mask` | ✅ `ffffffc012510050 B` | 加 extern + 定义 |
| `check_for_task_rotation` / `sched_ktime_clock` | ✅ | 随 `CONFIG_SPRD_ROTATION_TASK` |
| `is_reserved` / `mark_reserved` / `clear_reserved` | ❌ 只有 inline | 补 static inline（`cpu_reserved` per-cpu 变量设备里有） |
| `tick_nohz_cpu_hotpluggable` | ❌ | 不引入 |

### 6.3 文件系统

* **f2fs**：`SBI_IS_FREEZING`、3 参数 `f2fs_stop_checkpoint(sbi, end_io, reason)`、
  `f2fs_flush_ckpt_thread`、`f2fs_handle_stop`、`DATA_GENERIC_ENHANCE_UPDATE`、
  `F2FS_IPU_HONOR_OPU_WRITE`、`ALLOC_MODE_*`。
  **`lookup_mode` 全删** —— Image `.rodata` 里没有 `lookup_mode` 字符串，
  而 `alloc_mode` 有（`0xffffffc011ca4ae6`），一有一无，判定明确。
* **exfat**：设备用 mainline `fs/exfat`（不是 `drivers/staging/exfat`）。
  铁证：设备 defconfig 只有 `CONFIG_EXFAT_FS=y` + `CONFIG_EXFAT_DEFAULT_IOCHARSET="utf8"`，
  **没有** staging 专有的 `CONFIG_EXFAT_DONT_MOUNT_VFAT`。
  实现上取 v5.11 全套（14 文件）+ 从 v5.13 移植 4 个设备独有函数
  （`exfat_ioctl`/`exfat_compat_ioctl`/`exfat_trim_fs`/`__exfat_free_cluster`），
  并把 5.4 不存在的 API 逐处适配：`blkdev_issue_flush(bdev, GFP_KERNEL, NULL)`（3 参）、
  `mpage_readpages` + `a_ops->readpages`（不是 `readahead`）、
  `struct fs_parameter_enum` 而非 `constant_table`、4 参 `__fsparam`/2 参 `fsparam_enum`、
  `f_fsid.val[0/1]` 而非 `u64_to_fsid`。
  同时把 `drivers/staging/exfat` 的 `config EXFAT_FS` 改名 `EXFAT_STAGING`，避免同名。

### 6.4 其它

| 子系统 | 内容 |
|---|---|
| `kernel/power/qos.c` | `core_qos_*` 一整套（`min_core`/`max_core` 约束） |
| `kernel/cpu.c` | `__cpu_isolated_mask`、`init_cpu_isolated`、`cpu_down` 的 isolated 判定 |
| `kernel/irq/proc.c` | `sprd_dump_interrupts`（sysdump 用，`arch_show_interrupts(NULL, ...)`） |
| `kernel/panic.c` | `sprd_emergency_restart`、panic 时 `get_pt_regs` + `sprd_dump_stack_reg` |
| `arch/arm64/kernel/entry.S` | `ENTRY(get_pt_regs)`（35 条指令，保存完整的用户寄存器快照） |
| `fs/proc/meminfo.c` | `sprd_dump_meminfo`、`si_meminfo_nolock`、`HighAtomicFree`、`IonTotalHeap/Pool` |
| `fs/block_dev.c` | `nr_blockdev_pages_nolock` |
| `drivers/android/binder.c` | Android 12 的 freeze 通知（`binder_ref_freeze`、`BC/BR_*FREEZE*`）、`binder_proc_ext` 包装、展锐 `binder_state_hashed`/`binder_transactions_hashed` debugfs |
| `drivers/usb/typec/tcpm/sprd_tcpm.c` | 统一用设备真身头 `include/linux/usb/sprd_tcpm.h`（比 302 多 11 个字段） |
| `drivers/power/supply/sprd_battery_info.c` | 按设备头删 `charge_full_uah`/`batt_id_*`/`fullbatt_advance_level`，`sprd_battery_parse_battery_id()` 改 1 参 |
| `net/xfrm` + `net/ipv4|ipv6` | `CONFIG_XFRM_FRAGMENT` 整块（`ip4_do_xfrm_frag`/`ip6_do_xfrm_frag`/`get_xfrm_fragment`） |
| `drivers/scsi/ufs` | Makefile 补 `ufshpb.o`/`ufs-sprd-debug.o` |

---

## 7. 功能等价性验证

### 7.1 探针（`_stock/check/devtest/`）

`devtest.sh` 167 项**只读**探针，`run.sh ours|stock` 封装，
`cmp.py` 生成差异表（允许白名单：MAC 随机化、`MemTotal` 因 zram 配置微差、
loop 设备数、负载导致的 `Slab` 差异）。

原厂基线 vs 本内核，首轮只发现 1 个真差异：`vm.swappiness`（60 vs 150）。
根因是 `kernel/sysctl.c` 漏表项（README §4.9），修复后一致。

### 7.2 dmesg 交叉比对

把两边的 dmesg 历史做直方图对比，发现我们早期报的"问题"绝大多数原厂同有：

```
45 条 asoc_sprd_card ... parse for codec failed
38 条 charge_set_basp: convert basp = -1\x0a failed   ← vendor 模块日志 bug（字面 \x0a）
21 条 vbc-rxpx-codec-sc27xx probe failed
19 条 [Audio:SMSG] aud_smsg_recv cache is empty!
 5 条 bq2560xX_chg i2c retry write fail
```

**教训：不要在没有基线的情况下判"这是 bug"。** 先采原厂基线，再对比。

### 7.3 用工厂测试工具做黑盒验收

设备自带 `com.sprd.validationtools`（工厂测试），它**必须系统签名 + `sharedUserId`
才能跑全项**，因此是极好的黑盒验收器：只换内核、app 与系统身份不变，
失败点必然落在内核/驱动侧。

实例（README §4 之外的补充）：**按键测试只有 POWER/音量能按**。
原版 smali 里按键列表按机型选（`ro.product.system.model == "Q968"` → 22 项），
其中 3 个走 `gpio-keys`、19 个走 matrix keypad。对照
`sprd_keypad_hw_init`（`0xffffffc010df3f64`，65 条指令）发现我们漏了末尾三笔
寄存器写（`+0x00` 行列表 + `+0x04` + `+0x24`），控制器根本没启动扫描。
补上即修复（commit `ad636a26a`）。

这条闭环很有价值：**它证明了"符号齐、编译过、能开机"之后，仍会有功能性缺口，
而工厂测试是最快的发现手段。**

---

## 8. 未完成项与后续方向

1. **mmc 私有 sysfs 属性 37 个**。设备 `drivers/mmc/core/{mmc.c,sd.c}` 里有一批
   `mmc_cid_show`/`mmc_csd_show`/`mmc_ssr_show`/`mmc_cmdq_en_show` 等属性，
   上游 5.10 与 302 都没有源码，只能按 `_stock/asm/m.s` 逆向（`.rodata` 里的格式串
   已解析出来，见工作区笔记）。
2. **zram 的 `wb_enable`/`zstd` 属性**：设备在旧 zcomp 接口版 zram 上加的自定义
   `dev_attr`，需按 `0xffffffc010c55540` 的反汇编补。
3. **erofs CPU hotplug 回调**（`erofs_cpu_online`/`erofs_cpu_offline`）。
4. **Full LTO**：原厂用 Full LTO（`# CONFIG_THINLTO is not set` + `LTO_CLANG=y`），
   本仓库用 ThinLTO，功能等价但生成布局不同。

---

## 9. 方法论总结

1. **三重证据**（符号 / 头文件 / 机器码+字符串）互相验证，任何单一证据都可能骗人。
2. **"有"能证明，"无"不能证明**：kallsyms 命中说明设备真有；
   但 inline 函数无符号，判定"没有"必须靠 `.rodata` 字符串 + 反汇编。
3. **头文件是契约**：换错版本的头会一路编到链接期才炸，或者更糟——编过但行为错。
4. **先采基线再判 bug**：没有原厂对照，会把别人的正常行为当自己的缺陷。
5. **编译器的报错是最有效的静态分析**：签名/参数/成员类错误，自研扫描器抓不到。
6. **配置、符号、编译都对，运行时仍可能错**（sysctl 表项、驱动同名冲突）：
   必须真机 + 基线对比。
7. **黑盒验收器（工厂测试）是发现"功能缺口"的最快手段**。

---

## 10. 复现清单

```bash
# 1) 采素材
adb shell "su -c 'cat /proc/kallsyms'"       > _stock/kallsyms_full.txt
adb pull /vendor/kheaders.tar.xz                _stock/
adb shell "su -c 'dd if=/dev/block/by-name/boot_a of=/sdcard/boot.img'"
adb shell "su -c 'dd if=/dev/block/by-name/vendor_boot_a of=/sdcard/vendor_boot.img'"

# 2) 分类与差集
python recon/skelcheck.py                       # 三态分类
python recon/symgap.py                         # 符号差集
python recon/devver.py                         # 设备版本判定
python recon/khdiff.py && python recon/kh50.py  # 头文件
python recon/kconfcheck.py                     # 配置

# 3) 编译
export ARCH=arm64 CROSS_COMPILE_COMPAT=arm-linux-gnueabi-
make sprd_ums9157_defconfig && make -j$(nproc) Image && make -j$(nproc) modules

# 4) 打包与刷机
python tools/mkboot.py --image arch/arm64/boot/Image \
       --ramdisk vendor/ramdisk_magisk.lz4 --avb-from vendor/boot_a.bin \
       --out boot_magisk.img
adb reboot bootloader && fastboot --disable-verification flash boot boot_magisk.img

# 5) 验证
_stock/check/devtest/run.sh ours
_stock/check/devtest/cmp.py                     # 与原厂基线对比
```

---

## 11. 最后

这个仓库的价值不在于"能编过"，而在于**每一条展锐代码都能追溯到设备二进制里的
一个符号、一个结构体字段、一段机器码或一个字符串**。

反过来也一样：凡是设备里找不到证据的东西，这里就没有 —— 包括那些"上游有、
别的机型有、看起来很合理"的东西。
