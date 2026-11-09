/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_THREAD_INFO_H
#define __ASM_THREAD_INFO_H

#include <linux/compiler.h>

#ifndef __ASSEMBLY__

struct task_struct;

#include <asm/memory.h>
#include <asm/stack_pointer.h>
#include <asm/types.h>

typedef unsigned long mm_segment_t;


struct thread_info {
	unsigned long		flags;		
	mm_segment_t		addr_limit;	
#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	u64			ttbr0;		
#endif
	union {
		u64		preempt_count;	
		struct {
#ifdef CONFIG_CPU_BIG_ENDIAN
			u32	need_resched;
			u32	count;
#else
			u32	count;
			u32	need_resched;
#endif
		} preempt;
	};
#ifdef CONFIG_SHADOW_CALL_STACK
	void			*shadow_call_stack;
#endif
};

#define thread_saved_pc(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.sp))
#define thread_saved_fp(tsk)	\
	((unsigned long)(tsk->thread.cpu_context.fp))

void arch_setup_new_exec(void);
#define arch_setup_new_exec     arch_setup_new_exec

void arch_release_task_struct(struct task_struct *tsk);

#endif

#define TIF_SIGPENDING		0	
#define TIF_NEED_RESCHED	1	
#define TIF_NOTIFY_RESUME	2	
#define TIF_FOREIGN_FPSTATE	3	
#define TIF_UPROBE		4	
#define TIF_FSCHECK		5	
#define TIF_NOHZ		7
#define TIF_SYSCALL_TRACE	8	
#define TIF_SYSCALL_AUDIT	9	
#define TIF_SYSCALL_TRACEPOINT	10	
#define TIF_SECCOMP		11	
#define TIF_SYSCALL_EMU		12	
#define TIF_MEMDIE		18	
#define TIF_FREEZE		19
#define TIF_RESTORE_SIGMASK	20
#define TIF_SINGLESTEP		21
#define TIF_32BIT		22	
#define TIF_SVE			23	
#define TIF_SVE_VL_INHERIT	24	
#define TIF_SSBD		25	
#define TIF_TAGGED_ADDR		26	
#define TIF_MM_RELEASED		27

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_FOREIGN_FPSTATE	(1 << TIF_FOREIGN_FPSTATE)
#define _TIF_NOHZ		(1 << TIF_NOHZ)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_SYSCALL_EMU	(1 << TIF_SYSCALL_EMU)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_FSCHECK		(1 << TIF_FSCHECK)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_32BIT		(1 << TIF_32BIT)
#define _TIF_SVE		(1 << TIF_SVE)

#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_FOREIGN_FPSTATE | \
				 _TIF_UPROBE | _TIF_FSCHECK)

#define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP | \
				 _TIF_NOHZ | _TIF_SYSCALL_EMU)

#define INIT_THREAD_INFO(tsk)						\
{									\
	.flags		= _TIF_FOREIGN_FPSTATE,				\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
}

#endif 
