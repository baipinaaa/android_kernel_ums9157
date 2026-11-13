/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _XT_DSCP_H
#define _XT_DSCP_H

#include <linux/types.h>

#define XT_DSCP_MASK	0xfc	
#define XT_DSCP_SHIFT	2
#define XT_DSCP_MAX	0x3f	


struct xt_dscp_info {
	__u8 dscp;
	__u8 invert;
};

struct xt_tos_match_info {
	__u8 tos_mask;
	__u8 tos_value;
	__u8 invert;
};

#endif 

/* ---- merged from xt_DSCP.h (the upper-case sibling that cannot coexist
 * with xt_dscp.h on a case-insensitive filesystem) ---- */
#ifndef _XT_DSCP_TARGET_H_MERGED
#define _XT_DSCP_TARGET_H_MERGED

/* x_tables module for setting the IPv4/IPv6 DSCP field
 *
 * (C) 2002 Harald Welte <laforge@gnumonks.org>
 * based on ipt_FTOS.c (C) 2000 by Matthew G. Marsh <mgm@paktronix.com>
 * This software is distributed under GNU GPL v2, 1991
 *
 * See RFC2474 for a description of the DSCP field within the IP Header.
 *
 * xt_dscp.h,v 1.7 2002/03/14 12:03:13 laforge Exp
*/
#include <linux/netfilter/xt_dscp.h>
#include <linux/types.h>

/* target info */
struct xt_DSCP_info {
	__u8 dscp;
};

struct xt_tos_target_info {
	__u8 tos_value;
	__u8 tos_mask;
};

#endif /* _XT_DSCP_TARGET_H_MERGED */
