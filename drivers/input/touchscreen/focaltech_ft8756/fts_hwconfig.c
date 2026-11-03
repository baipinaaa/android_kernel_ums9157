// SPDX-License-Identifier: GPL-2.0
/*
 * FocalTech ft8756 SPI touchscreen driver - board specific helpers.
 *
 * Reconstructed from focaltech_ft8756_spi_ts.ko of the vendor_boot
 * ramdisk (chip FT8756, "FocalTech Touchscreen Driver").
 *
 * STATUS: work in progress.  Only the boot argument helper below has been
 * decoded so far.  The plan for the rest of the module: 84 of the 141
 * function symbols are covered by the vendor V3.3 sources that already
 * live in drivers/vendor/common/touchscreen_v2/focaltech_touch_V3_3/,
 * the remaining 57 are board specific sysfs entry points that have to be
 * written from the disassembly.  See recon/out/fts_notes.md.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/string.h>

#include "fts_core.h"

/**
 * get_bootargs() - fetch one androidboot token from /chosen
 * @buf: destination, must be large enough for the value
 * @key: token name including its "androidboot." prefix
 *
 * The value is copied up to the next blank, so no trailing space ends up
 * in @buf.
 *
 * Return: 0 on success, 1 when @key is absent, -EIO when /chosen is
 * missing and -EINVAL when bootargs itself cannot be read.
 */
int get_bootargs(char *buf, const char *key)
{
	struct device_node *np;
	const char *bootargs;
	const char *p;

	np = of_find_node_opts_by_path("/chosen", NULL);
	if (!np) {
		pr_err("[FTS_TS/E]%s:Can't get the /chosen\n", __func__);
		return -EIO;
	}

	if (of_property_read_string(np, "bootargs", &bootargs)) {
		pr_err("[FTS_TS/E]%s:Can't get the bootargs\n", __func__);
		return -EINVAL;
	}

	p = strstr(bootargs, key);
	if (!p) {
		pr_err("[FTS_TS/E]%s:Read bootargs %s fail\n", __func__, key);
		return 1;
	}

	/* the token value follows the key, it ends at the next blank */
	p += strlen("androidboot.");
	while (*p && *p != ' ')
		*buf++ = *p++;
	*buf = '\0';

	return 0;
}

/* mode string of the running boot, filled by check_current_mode() */
char current_mode[100];

/* panel name of the running boot, read by fts_ts_init() */
char lcd_name[100];

/**
 * check_current_mode() - suspend the controller in factory test boots
 *
 * In the calibration and autotest boot modes the panel is driven by the
 * factory tooling, so the touch controller has to stay powered down.
 */
void check_current_mode(void)
{
	get_bootargs(current_mode, "androidboot.mode");

	pr_info("[FTS_TS/I]%s:current_mode is %s\n\n", "check_current_mode",
		current_mode);

	if (strstr(current_mode, "cali"))
		fts_ts_suspend();
	else if (strstr(current_mode, "autotest"))
		fts_ts_suspend();
}
