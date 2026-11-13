/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */


#ifndef _IPT_TTL_H
#define _IPT_TTL_H

#include <linux/types.h>

enum {
	IPT_TTL_EQ = 0,		
	IPT_TTL_NE,		
	IPT_TTL_LT,		
	IPT_TTL_GT,		
};


struct ipt_ttl_info {
	__u8	mode;
	__u8	ttl;
};


#endif

/* ---- merged from ipt_TTL.h (the upper-case sibling that cannot coexist
 * with ipt_ttl.h on a case-insensitive filesystem) ---- */
#ifndef _IPT_TTL_TARGET_H_MERGED
#define _IPT_TTL_TARGET_H_MERGED

/* TTL modification module for IP tables
 * (C) 2000 by Harald Welte <laforge@netfilter.org> */


#include <linux/types.h>

enum {
	IPT_TTL_SET = 0,
	IPT_TTL_INC,
	IPT_TTL_DEC
};

#define IPT_TTL_MAXMODE	IPT_TTL_DEC

struct ipt_TTL_info {
	__u8	mode;
	__u8	ttl;
};

#endif /* _IPT_TTL_TARGET_H_MERGED */
