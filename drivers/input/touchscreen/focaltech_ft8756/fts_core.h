/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FocalTech ft8756 SPI touchscreen driver - shared declarations.
 *
 * Reconstructed from focaltech_ft8756_spi_ts.ko.  Only the board specific
 * helpers are declared here for now; the vendor V3.3 half of the driver
 * brings its own focaltech_core.h.
 */

#ifndef _FTS_CORE_H_
#define _FTS_CORE_H_

#include <linux/types.h>

/* .bss+0xb9: androidboot.mode, e.g. "cali" or "autotest" */
extern char current_mode[100];
/* .bss+0x11d: the lcd_name boot argument */
extern char lcd_name[100];

int get_bootargs(char *buf, const char *key);

#endif /* _FTS_CORE_H_ */

/* resume worker */
void fts_ts_resume_work(struct work_struct *work);
