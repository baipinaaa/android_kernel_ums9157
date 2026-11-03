# SPDX-License-Identifier: GPL-2.0-only

zram-y	:=	zcomp.o zram_drv.o

zram-$(CONFIG_ZRAM_BACKEND_LZO)		+= backend_lzorle.o backend_lzo.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4)		+= backend_lz4.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4HC)	+= backend_lz4hc.o
zram-$(CONFIG_ZRAM_BACKEND_ZSTD)	+= backend_zstd.o
zram-$(CONFIG_ZRAM_BACKEND_DEFLATE)	+= backend_deflate.o
zram-$(CONFIG_ZRAM_BACKEND_842)		+= backend_842.o

# CONFIG_ZRAM_DEDUP 的代码（zram_dedup.c）已按设备 .ko 重建，且 zram 核心已经
# entry 化完成（struct zram_entry + 引用计数，见 G 树 a0a77aeb4），所以正常挂进来。
# 关掉去重时 zram_dedup.h 提供同名 static inline 桩，核心代码不需要 #ifdef。
zram-$(CONFIG_ZRAM_DEDUP)	+=	zram_dedup.o

obj-$(CONFIG_ZRAM)	+=	zram.o

# mainline 7.x zram 使用 guard()/cleanup 接口，本树默认开启 -Wdeclaration-after-statement
ccflags-y += -Wno-declaration-after-statement
