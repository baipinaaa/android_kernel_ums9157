# SPDX-License-Identifier: GPL-2.0
#
# Hynitron cst0xx/cst2xx/cst3xx/cst8xx touchscreen driver.
# 重建自 vendor hynitron_ts.ko（.text 0x8094 = 32916 B，68 个函数）。

obj-$(CONFIG_TOUCHSCREEN_HYNITRON)	+= hynitron_ts.o

hynitron_ts-y := hynitron_core.o \
		 hynitron_input.o \
		 hynitron_report.o \
		 hynitron_probe.o \
		 hynitron_sysfs.o \
		 hynitron_update.o
