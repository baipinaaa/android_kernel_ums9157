# SPDX-License-Identifier: GPL-2.0
#
# FocalTech ft8756 SPI touchscreen, rebuilt from the focaltech_ft8756_spi_ts.ko
# shipped in vendor_boot (Focaltech V3.2 20200422).
#
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_FT8756) += focaltech_ft8756.o

focaltech_ft8756-y := focaltech_core.o focaltech_common_interface.o \
		      focaltech_spi.o focaltech_flash.o focaltech_esdcheck.o \
		      focaltech_ex_fun.o focaltech_ex_mode.o focaltech_gesture.o \
		      focaltech_point_report_check.o fts_odm.o fts_hwconfig.o \
		      fts_sysfs.o

focaltech_ft8756-$(CONFIG_TOUCHSCREEN_FOCALTECH_FT8756_TEST) += focaltech_test/
