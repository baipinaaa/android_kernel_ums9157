# Sunelan Q968 / UMS9620 — 5.4.210 GKI 骨架上的逆向移植内核

本仓库是一个**从二进制逆向重建**的 Android 12 arm64 内核，目标设备为
**Sunelan YQ5（SoC：Unisoc UMS9620，机型标识 Q968）**。

设备出厂只提供 `boot_a.bin` 与 `vendor_boot_a.bin`（Image + 157 个 `.ko`），
不提供任何内核源码。本仓库做的事是：

> 以 **AOSP Android Common Kernel `android12-5.4.210`**（commit `1f0546b12`）为骨架，
> 把设备二进制里**真实存在**的厂商实现逐个还原进去，
> 最终产出一个能刷进真机、能正常开机跑完 init / 挂载分区 / 加载全部厂商模块、
> 功能与原厂内核等价的内核。

原厂内核版本串（来自设备 Image 与全部 157 个 `.ko` 的 `vermagic`）：

```
5.4.210-android12-9-g96901d45d5a5-ab50 SMP preempt mod_unload modversions aarch64
```

本仓库构建出的内核**刻意对齐这个字符串**，因此原厂 `.ko` 可以原样加载，
不需要重新签名、不需要替换 `vendor_boot` 分区（也就不会触发 AVB 校验失败）。

---

## 1. 现在的状态

| 项目 | 状态 |
|---|---|
| 编译 | ✅ 通过（Android clang 12.0.5 / r416183b，`-k` 全量无 error） |
| 开机 | ✅ 通过（pstore / dmesg 无 panic，`sys.boot_completed=1`） |
| 原厂模块加载 | ✅ 全部 157 个 `.ko` 成功（`version mismatch` 已放行，见 §4.6） |
| root（Magisk 补丁 ramdisk） | ✅ `su -c id` → `uid=0 ... u:r:magisk:s0` |
| 硬件通路 | ✅ 屏幕 / 触摸 / 键盘 / 振动 / 充电 / 音频 / WiFi / 蓝牙 / GNSS / FM / 传感器 / eMMC / UFS / 摄像头 / GPU / 视频编解码 / 热管理 |
| 与原厂配置差异 | 设备启用项**零缺失**；本仓库多 9 项（Mali、`XOR_BLOCKS`、zram backend，均为无害） |
| 未完成 | 展锐私有 mmc sysfs 属性 37 个、zram 的 `wb_enable`/`zstd` 属性、erofs CPU hotplug 回调 |

原厂内核与本内核的运行时对比（`_stock/check/devtest/`，167 项探针）
只发现一处真实差异：`vm.swappiness` 上限（原厂 200，早期版本误用上游 100），已修复。

---

## 2. 快速开始

依赖：`aarch64` 交叉编译器（这里用 Android clang）、`python3`、`lz4`。

```bash
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_COMPAT=arm-linux-gnueabi-   # 需要，CONFIG_COMPAT_VDSO=y
export LLVM=1                                     # 或使用 Android clang 的 bin 目录

make sprd_ums9157_defconfig
make -j$(nproc) Image
make -j$(nproc) modules
```

打包成可刷的 `boot.img`（`boot.img` 必须带 AVB footer，见 §4.5）：

```bash
# 内核段 + 原厂 ramdisk（无 root）
python3 tools/mkboot.py --image arch/arm64/boot/Image \
        --ramdisk vendor/ramdisk_stock.lz4 \
        --avb-from vendor/boot_a.bin \
        --out boot_plain.img

# 内核段 + Magisk 补丁 ramdisk（带 root）
python3 tools/mkboot.py --image arch/arm64/boot/Image \
        --ramdisk vendor/ramdisk_magisk.lz4 \
        --avb-from vendor/boot_a.bin \
        --out boot_magisk.img
```

`--avb-from` 可以是原厂 `boot_a.bin`、Magisk 修补过的 boot 镜像，或本工具
之前产出的任一 `boot.img` —— 它只从中复制 AVB vbmeta 段并重写 footer，
不重算 hash、不重新签名（unlocked 设备不校验，见 §4.5）。

两个 ramdisk 直接从现成的 boot 镜像里切出来即可：

```python
import struct
d = open('vendor/boot_a.bin','rb').read()
ks, rs = struct.unpack_from('<II', d, 0x08)
off = ((4096 + ks) + 4095) // 4096 * 4096
open('vendor/ramdisk_stock.lz4','wb').write(d[off:off+rs])
```

> `vendor/`（原厂镜像、`kheaders`、反汇编、原厂模块）只在完整工作区里存在，
> 不随仓库分发；见 §6 的素材清单。`tools/mkboot.py` 是本仓库的一部分。

刷机（**必须**用 `adb reboot bootloader`，不要用 `adb reboot fastboot`，原因见 §4.8）：

```bash
adb reboot bootloader
fastboot --disable-verification flash boot boot_magisk.img
fastboot reboot
```

---

## 3. 这个内核是怎么"逆向"出来的

核心思路是**用三重证据交叉验证**，而不是靠猜：

1. **符号层** — 设备的 `/proc/kallsyms`（212,012 行，其中 95,308 行属于模块）
   给出原厂 vmlinux **每个函数的名字与地址**。这是最强的"存在性"证据：
   名字在，实现就在。
2. **头文件层** — 设备 `/vendor` 里随内核发布的 `kheaders.tar.xz`（7,477 个头文件）
   是**编译期原样头文件**，包含结构体成员、函数原型、`#define`。
   它比任何反编译推断都可靠，且能直接判定"某个字段/参数到底存在不存在"。
3. **代码层** — 设备 `Image`（38,308,352 B，加载基址 `0xffffffc010080000`）
   的 `.text` 反汇编 + `.rodata` 字符串。用于确认语义：
   例如 `vmpressure_global` 里的 `mov w8,#0xca; tst w0,w8` 精确对应
   `if (!(gfp & (__GFP_HIGHMEM|__GFP_MOVABLE|__GFP_IO|__GFP_FS)))`；
   `.rodata` 里有没有 `lookup_mode` 字符串，就能判定 f2fs 的该功能存不存在。

判定规则（贯穿全项目）：

> **设备二进制里有 → 移植真实实现；设备二进制里没有 → 一行都不加，直接删掉。**

这条规则是对"用别的机型源码硬凑"的纠偏：早期版本曾整块搬入另一台机器
（UMS9620，5.4.302）的实现，导致编译通过但带进设备根本没有的 302 特性。
修正后的做法是逐符号、逐结构体成员核对。

详细步骤、脚本、每一条结论的证据链见 **[PORTING.md](PORTING.md)**。

---

## 4. 逆向过程中必须解决的 9 个坑

按"会让人卡住多久"排序。每一条都是真机复现后才定位的。

### 4.1 只有骨架，没有实现（33 个文件回退过头）

骨架树里 51,217 个源码文件中，49,025 个（95.8%）与上游 5.4.210 **逐字节相同** ——
这是符合预期的"骨架"。但早期为了"先编过"曾把一批文件回退到 5.4.210 原版，
事后用 `devver`（判据：`defns(5.4.302) - defns(5.4.210)` 与设备符号集求交）
发现 **33 个文件回退错了**，例如：

| 文件 | 设备里真有、而 5.4.210 没有的函数 |
|---|---|
| `mm/mmap.c` | `find_vma_rcu`、`__vma_merge`、`__free_vma`、`__vm_rcu_put` |
| `mm/memory.c` | `__handle_speculative_fault`、`_vm_normal_page`、`pte_map_lock` |
| `mm/rmap.c` | `__page_add_new_anon_rmap` |
| `mm/vmpressure.c` | `vmpressure_global`、`vmpressure_notifier_register` |
| `mm/page_alloc.c` | `calculate_min_free_kbytes`、`highatomic_nr_pages` |
| `kernel/power/qos.c` | `core_qos_apply`、`core_constraints_init` |
| `kernel/cpu.c` | `init_cpu_isolated` |
| `fs/proc/meminfo.c` | `sprd_dump_meminfo` |
| `arch/arm64/kernel/entry.S` | `get_pt_regs` |

也就是说设备 mm 子系统的真实身份是 **5.4.210 + Android 5.10 的
speculative page fault / process_reclaim 回流**，既不是纯 5.4.210，也不是整块 5.4.302。

### 4.2 头文件必须用"设备真身"，不能用上游或别的机型

7,477 个 kheaders 头逐个对比后，50 个"语义不同"（其中 8 个是 Windows 大小写
折叠造成的假报警）。换用真身头之后暴露出大量签名差异，例如：

* `include/linux/rmap.h`：设备的 `try_to_unmap(page, flags, vma)` 是 **3 参数**，
  `page_lock_anon_vma_read(page)` 是 **1 参数**；而 5.4.302 与 android-5.10 上游
  都是 2 参数。
* `include/linux/swap.h`：设备用 5.10 风格的
  `__lru_cache_add_active_or_unevictable(page, vma_flags)`。
* `include/linux/power/sprd_battery_info.h`：设备**没有** `charge_full_uah` /
  `batt_id_index` / `fullbatt_advance_level`，反而多 `fullbatt_current_uA`；
  `sprd_battery_parse_battery_id()` 是 **1 参数**。

结论：**头文件是"契约"，必须与设备 .c 同源**；拿错版本的头会一路编译到链接期
才炸，或者更糟——编过但运行时行为错误。

### 4.3 符号 CRC 不一致，原厂模块拒绝加载

第一次刷机能进 first stage init，但 0.69 秒时：

```
mmc_swcq: disagrees about version of symbol blk_queue_flag_set
mmc_swcq: Unknown symbol blk_queue_flag_set (err -22)
init: Failed to load kernel modules
Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00
```

原因：`CONFIG_MODVERSIONS=y` + 原厂 `.ko` 的 `__versions` 段里记着**原厂内核导出符号的
CRC**，而我们的 genksyms 算出的值不同。定量对比：

```
设备 vmlinux 导出 12,100 个符号，本仓库 12,789 个，共同 6,007 个
其中 CRC 不同 118 个，被原厂模块引用的 38 个
全部集中在 bio/blk/blk_mq 一族
```

头文件侧已排除（`blkdev.h`/`blk-mq.h` 与 kheaders 真身 md5 相同），差异来自
genksyms 对类型展开的细节。**最终方案**：让 `kernel/module.c:check_version()` 对
CRC 不匹配只打 `pr_warn("... version mismatch ..., ignored")` 而放行 —— 因为
结构体布局来自设备真身头，ABI 是一致的。

代价：内核 `tainted` 标志多一位，dmesg 里会出现 23 条 `version mismatch` 警告。
收益：**`vendor_boot` 分区完全不用改**，157 个原厂模块原样可用。

### 4.4 `vendor_boot` 是一个必须原样保留的整体

设备的模块全在 `vendor_boot`（`mmcblk0p38`，100 MiB）的 ramdisk 里
（**没有 `vendor_dlkm` 分区**）。它的结构是：

```
boot header v4 (4096)
vendor_ramdisk 23,303,724 B (lz4 legacy, newc cpio, 677 条目, 157 个 .ko 在 lib/modules/)
vendor_ramdisk table 108 B
dtb 160,163 B
vbmeta (AVB0, 2,240 B)  @ 23,478,272
AVB footer (AVBf)       @ 104,857,536
```

只要动了其中任何一个字节，`avb_slot_verify` 就会失败（见 4.5）。所以最终方案是
**一个字节都不改 `vendor_boot`**，靠 4.3 让原厂模块能加载。

### 4.5 AVB：unlocked 也要求结构存在

`boot` 与本 `vendor_boot` 都在 `vbmeta_a` 的 chain 校验链里
（`/proc/device-tree/firmware/android/vbmeta/parts` = `vbmeta,boot,vendor_boot,dtb,...`）。
设备处于 `verifiedbootstate=orange` + `veritymode=disabled`，hash 与签名**都不校验**，
但 init 的 `avb_slot_verify` 仍然要求分区尾部有该有的 AVB 结构，否则返回
`AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION (6)`：

```
init: [libfs_avb]avb_slot_verify failed, result: 6
init: Failed to mount required partitions early ...
```

对比三个 boot 镜像即可看出规律：

| | kernel_size | image_end | 内嵌 AVB0 | AVBf footer |
|---|---|---|---|---|
| 原厂 `boot_a.bin` | 38,308,352 | 39,788,544 | @39,788,544 | @67,108,800 |
| Magisk 修补版 | 38,308,352 | 39,481,344 | @39,481,344（复制原厂） | @67,108,800 |
| 朴素打包 | 31,439,360 | 32,919,552 | **无** | **无** |

Magisk 的 image 比原厂短 30 万字节、hash 必然不匹配，却照样开机 ——
证明 **unlocked 下只需要"结构存在"**。因此打包时要写入一段 AVB0 + 末尾 AVBf
（64 字节：`AVBf` + ver 1.0 + `original_image_size` + `vbmeta_offset` + `vbmeta_size`）。
本项目 `tools/mkboot.py` 就是做这件事。

### 4.6 模块链：替换任意一个 `.ko` 会连带炸掉依赖它的模块

在 4.3 修复之前曾试着只替换少数模块。结果是"换一个坏一片"：

```
mmc_swcq.ko     导出 dbg_add_host_log / mmc_swcq_{init,suspend,resume,finalize_request}
mmc_hsq.ko      导出 mmc_hsq_{init,suspend,resume,finalize_request}
sdhci-sprd.ko   引用上面 9 个 + 内核的 mmc_cmdq_enable/disable
zram.ko         引用内核的 blk_queue_flag_set
```

`sdhci-sprd.ko` 的 `__versions` 里记着 `mmc_swcq_init` 的 CRC，换成我们编译的
`mmc_swcq.ko` 后立刻变成 `sdhci_sprd: disagrees about version of symbol mmc_swcq_init`。
**这就是放弃"替换模块"、改为"放行 CRC"的直接原因。**

### 4.7 `sprd-vpu-pd` 撞名 —— 一个模块能让系统在第一屏无限重启

```
Error: Driver 'sprd-vpu-pd' is already registered, aborting...
initcall vpu_delay_work+0x40/0x5c [sprd_vpu_pw_domain] returned -16 after 3 usecs
init: Failed to insmod '/lib/modules/sprd-vpu-pw-domain.ko': Device or resource busy
init: Failed to load kernel modules
Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00
```

设备从 `/vendor/lib/modules/modules.load`（127 项）加载的 `sprd-vpu-pw-domain.ko`
注册的 driver 名是 `sprd-vpu-pd`；如果内核里内建了同名驱动
（`drivers/soc/sprd/domain/sprd_vpu_pm_domain.c`），`platform_driver_register()`
返回 `-EBUSY`，init 直接 panic。修法：该驱动**不编入内核**（`obj-y +=` 注释掉），
交给模块。同类风险在 `sprd-keypad`、`sprd-drm` 等"内核与模块同名"处都要小心。

### 4.8 `adb reboot fastboot` 会把设备卡在 bootloop

`adb reboot fastboot` 进的是 **fastbootd**（跑在 Android userspace 里），
它会往 `misc` 分区写一次性的 boot control block（`bootonce-bootloader`）再重启。
之后每次开机 bootloader 先读这个 BCB 就进 fastboot —— **BCB 没被消费之前，
刷什么 boot 都不启动**。刷机一律用 `adb reboot bootloader`（bootloader 模式）。

### 4.9 配置对、编译过、符号全 —— 但 sysctl 节点不存在

原厂 `/proc/sys` 里有 4,662 个叶子节点。我们用树里全部 `procname = "..."`（924 个）
去比对，发现 5 个缺失，但**变量、Kconfig、defconfig、使用处全都在树里**：

| 节点 | 原厂值 | 树里的变量 |
|---|---|---|
| `vm/swappiness` 上限 | 200 | `mm/vmscan.c:199 int vm_swappiness = 60;` |
| `vm/direct_swappiness` | 100 | `mm/vmscan.c:203 int direct_vm_swappiness = 60;` |
| `vm/writeback_swapcache` | 1 | `mm/vmscan.c:212 int writeback_swapcache;` |
| `vm/speculative_page_fault` | 1 | `mm/memory.c:97 int sysctl_speculative_page_fault = 1;` |
| `kernel/sched_clamp_min2boost` | 1 | `kernel/sched/core.c:827 unsigned int sysctl_sched_uclamp_min_to_boost = 1;` |

**只缺 `kernel/sysctl.c` 里的 ctl_table 表项。** 这类"变量在、表项忘"的缺口
用符号扫描、编译、静态分析都发现不了，只能拿**原厂 `/proc/sys` 全量清单**反查。
第一个表现是 `/vendor/etc/init/hw/init.ram.*.rc` 里 `write /proc/sys/vm/swappiness 150`
被内核拒绝（上游上限 100），值停在 60。

---

## 5. 怎么验证"真的等价"

四层验证，缺一不可：

```bash
# ① 符号层：设备有、我们也有
recon/symgap.py                 # 设备 vmlinux 函数集 vs 树标识符集，输出差集

# ② 配置层：设备启用的项一个不缺
recon/kconfcheck.py             # kconfiglib 求解，对比设备 .config

# ③ 头文件层：语义一致
recon/khdiff.py                 # 7,477 个 kheaders 头逐个语义对比

# ④ 运行时：真机探针，与原厂逐项对比
_stock/check/devtest/run.sh ours
# ...刷回原厂内核...
_stock/check/devtest/run.sh stock
_stock/check/devtest/cmp.py     # 输出差异报告
```

`devtest.sh` 是一个**只读**的 167 项探针：内核身份、cmdline、config、CPU/调速器、
中断、内存/CMA/swap/zram、存储与文件系统、DRM/背光/输入/音频/网络/传感器/电源、
热管理、devfreq、`/proc/sys` 关键项、debugfs、模块清单。
它不写任何节点，可以安全地在客户机上跑。

---

## 6. 素材清单（不在仓库内）

这些是逆向的**输入**，体积大、涉及厂商版权，不随仓库分发。
放在工作区 `_stock/` 下，路径供参考：

| 文件 | 说明 |
|---|---|
| `_stock/kallsyms_full.txt` | 设备 `/proc/kallsyms` 全量（212,012 行） |
| `_stock/kheaders.tar.xz` | 设备内核头文件真身（7,477 个） |
| `recon/raw/Image` | 设备内核 Image（38,308,352 B，`_text=0xffffffc010080000`） |
| `recon/raw/config.txt` | 设备 `.config`（4,941 项） |
| `recon/modules/*.ko` | `vendor_boot` 里全部 157 个原厂模块 |
| `_stock/asm/*.s` | 按符号切片的反汇编（`### FUNC <name> <addr> <insn>`） |
| `_stock/dec/all_sym.c` | Ghidra 伪代码（108 MB，39,381 个函数） |
| `_stock/vendor_boot_*.bin` | 原厂 `vendor_boot` 拆出的 header / ramdisk / table / dtb |

复现分析需要的脚本（工作区 `recon/`，未入库）：
`skelcheck.py`（三态分类）、`symgap.py`（符号差集）、`devver.py`（设备版本判定）、
`khdiff.py`/`kh50.py`（头文件对比）、`kconfcheck.py`（配置求解）、
`devtest.sh`/`run.sh`/`cmp.py`（真机探针与对比）、
`mkboot.py`/`mkvendorboo*.py`（打包）。

---

## 7. 目录导航

* `PORTING.md` — **详细移植过程**：每一步做了什么、用了什么证据、遇到什么、怎么解决
* `Documentation/android-common-kernel-patches.md` — 上游 GKI 的补丁规范（原文保留）
* `arch/arm64/configs/sprd_ums9157_defconfig` — 设备配置（已按原厂 `.config` 校准）
* `drivers/soc/sprd/`、`drivers/usb/typec/tcpm/sprd_tcpm.c`、
  `drivers/power/supply/sprd_battery_info.c` — 展锐私有驱动（逆向还原的主要部分）
* `mm/` — 除上游代码外，含设备独有的 speculative page fault、`vmpressure_global`、
  `process_reclaim`、`sprd_dump_*` 系列
* `.github/workflows/kernel.yml` — CI（Android clang 12.0.5，产物含 `Image`/`boot.img`/模块）

---

## 8. 已知未完成项

1. **展锐私有 mmc sysfs 属性 37 个**（`mmc_cid_show`、`mmc_csd_show`、`mmc_ssr_show`…
   由 `drivers/mmc/core/{mmc.c,sd.c}` 里的 `DEVICE_ATTR` 系列提供）。
   上游与参考机型都没有源码，只能按 `_stock/asm/m.s` 逐个逆向。
2. **zram 的 `wb_enable` / `zstd` 属性**：设备在旧 zcomp 接口版本上加的自定义属性，
   需要按反汇编补 `dev_attr`。
3. **erofs CPU hotplug 回调**（`erofs_cpu_online` / `erofs_cpu_offline`）。
4. **Full LTO**：原厂用 Full LTO，本仓库用 ThinLTO（可换，但构建时间大幅上升）。

这些都不影响开机与现有功能，属于"原厂有、我们还没有"的增量。

---

## 9. 许可

内核部分沿用上游 GPL-2.0（见 `COPYING`）。展锐相关的逆向还原代码仅用于
本设备的固件维护，不包含任何厂商源码副本。
