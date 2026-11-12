# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SPRD_POWER_DOMAIN)		+= sprd_pm_domain.o

obj-$(CONFIG_SPRD_DISP_PM_DOMAIN_SHARKL3)	+= sprd_disp_pm_domain_sharkl3.o
# NOTE: sprd_vpu_pm_domain.o must NOT be built in.  On this device the vpu
# power domain comes from the vendor module sprd-vpu-pw-domain.ko
# (/vendor/lib/modules/modules.load).  When both are present,
# platform_driver_register("sprd-vpu-pd") in the vendor module fails with
# -EBUSY, first stage init then reports "Failed to load kernel modules"
# and exits 127 -> kernel panic.
# obj-y					+= sprd_vpu_pm_domain.o
