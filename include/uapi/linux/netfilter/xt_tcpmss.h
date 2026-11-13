/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_TCPMSS_MATCH_H
#define _XT_TCPMSS_MATCH_H

#include <linux/types.h>

struct xt_tcpmss_match_info {
    __u16 mss_min, mss_max;
    __u8 invert;
};

#endif 

/* ---- merged from xt_TCPMSS.h (the upper-case sibling that cannot coexist
 * with xt_tcpmss.h on a case-insensitive filesystem) ---- */
#ifndef _XT_TCPMSS_H_MERGED
#define _XT_TCPMSS_H_MERGED

#include <linux/types.h>

struct xt_tcpmss_info {
	__u16 mss;
};

#define XT_TCPMSS_CLAMP_PMTU 0xffff

#endif /* _XT_TCPMSS_H_MERGED */
