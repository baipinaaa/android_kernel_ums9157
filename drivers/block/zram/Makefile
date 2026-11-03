# SPDX-License-Identifier: GPL-2.0-only

zram-y	:=	zcomp.o zram_drv.o

# 设备的 zram.ko 只导出旧 zcomp 接口（zcomp_available_algorithm /
# zcomp_available_show / zcomp_create(1 参) / zcomp_compress(3 参)，
# 见 recon/modules/zram.ko 的 70 个 STT_FUNC），压缩后端走 crypto，
# 没有 302 的 backend_* 体系，所以这里不编 backend_*.c。

# CONFIG_ZRAM_DEDUP 的代码（zram_dedup.c）已按设备 .ko 重建，且 zram 核心已经
# entry 化完成（struct zram_entry + 引用计数，见 G 树 a0a77aeb4），所以正常挂进来。
# 关掉去重时 zram_dedup.h 提供同名 static inline 桩，核心代码不需要 #ifdef。
zram-$(CONFIG_ZRAM_DEDUP)	+=	zram_dedup.o

obj-$(CONFIG_ZRAM)	+=	zram.o

# mainline 7.x zram 使用 guard()/cleanup 接口，本树默认开启 -Wdeclaration-after-statement
ccflags-y += -Wno-declaration-after-statement
