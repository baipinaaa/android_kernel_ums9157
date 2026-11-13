/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */


#ifndef _IP6T_HL_H
#define _IP6T_HL_H

#include <linux/types.h>

enum {
	IP6T_HL_EQ = 0,		
	IP6T_HL_NE,		
	IP6T_HL_LT,		
	IP6T_HL_GT,		
};


struct ip6t_hl_info {
	__u8	mode;
	__u8	hop_limit;
};


#endif

/* ---- merged from ip6t_HL.h (the upper-case sibling that cannot coexist
 * with ip6t_hl.h on a case-insensitive filesystem) ---- */
#ifndef _IP6T_HL_TARGET_H_MERGED
#define _IP6T_HL_TARGET_H_MERGED

/* Hop Limit modification module for ip6tables
 * Maciej Soltysiak <solt@dns.toxicfilms.tv>
 * Based on HW's TTL module */


#include <linux/types.h>

enum {
	IP6T_HL_SET = 0,
	IP6T_HL_INC,
	IP6T_HL_DEC
};

#define IP6T_HL_MAXMODE	IP6T_HL_DEC

struct ip6t_HL_info {
	__u8	mode;
	__u8	hop_limit;
};

#endif /* _IP6T_HL_TARGET_H_MERGED */
