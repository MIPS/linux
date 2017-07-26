#ifndef _ASM_STACKTRACE_H
#define _ASM_STACKTRACE_H

#include <asm/ptrace.h>

#ifdef CONFIG_KALLSYMS
extern int raw_show_trace;
extern unsigned long unwind_stack(struct task_struct *task, unsigned long *sp,
				  unsigned long *fp, unsigned long pc,
				  unsigned long *ra);
extern unsigned long unwind_stack_by_address(unsigned long stack_page,
					     unsigned long *sp,
					     unsigned long *fp,
					     unsigned long pc,
					     unsigned long *ra);
#else
#define raw_show_trace 1
static inline unsigned long unwind_stack(struct task_struct *task,
	unsigned long *sp, unsigned long *fp, unsigned long pc,
	unsigned long *ra)
{
	return 0;
}
#endif

static __always_inline void prepare_frametrace(struct pt_regs *regs)
{
#ifndef CONFIG_KALLSYMS
	/*
	 * Remove any garbage that may be in regs (specially func
	 * addresses) to avoid show_raw_backtrace() to report them
	 */
	memset(regs, 0, sizeof(*regs));
#endif
	__asm__ __volatile__(
		".set push\n\t"
		".set noat\n\t"
#ifdef CONFIG_64BIT
		"1: dla $at, 1b\n\t"
		"sd $at, %0\n\t"
		"sd $sp, %1\n\t"
		"sd $fp, %2\n\t"
		"sd $ra, %3\n\t"
#else
		"1: la $at, 1b\n\t"
		"sw $at, %0\n\t"
		"sw $sp, %1\n\t"
		"sw $fp, %2\n\t"
		"sw $ra, %3\n\t"
#endif
		".set pop\n\t"
		: "=m" (regs->cp0_epc),
		"=m" (regs->regs[29]), "=m" (regs->regs[30]),
		"=m" (regs->regs[31])
		: : "memory");
}

#endif /* _ASM_STACKTRACE_H */
