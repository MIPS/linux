#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <linux/compiler.h>
#include <asm/addrspace.h>
#include <asm/barrier.h>
#include <asm/sgidefs.h>

/**
 * mips_hwtrigger_write() - Perform an easily identifiable write
 * @code: The value to write; easily visible to debuggers
 *
 * Perform a write to the uncached address of the boot flash found on most MIPS
 * development systems, which is easy to trigger on with a logic analyser or
 * veloce emulator.
 */
static inline void mips_hwtrigger_write(unsigned long code)
{
	if (!IS_ENABLED(CONFIG_MIPS_HARDWARE_TRIGGERS))
		return;

	*(volatile unsigned long *)(CKSEG1ADDR(0x1fc00000)) = code;
	mb();
}

struct pt_regs;

static inline void __mips_hwtrigger(const char *file, unsigned long line,
				    struct pt_regs *regs, unsigned long code,
				    const char *why)
{
	extern void mips_hwtrigger_info(const char *file, unsigned long line,
					struct pt_regs *regs,
					unsigned long code, const char *why);

	if (!IS_ENABLED(CONFIG_MIPS_HARDWARE_TRIGGERS))
		return;

	mips_hwtrigger_write(code);
	mips_hwtrigger_info(file, line, regs, code, why);
}

#define mips_hwtrigger(regs, code, why) \
	__mips_hwtrigger(__FILE__, __LINE__, regs, code, why)

#ifdef CONFIG_BUG

#include <asm/break.h>

static inline void __noreturn BUG(void)
{
	mips_hwtrigger_write(~0ul);
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));
	unreachable();
}

#define HAVE_ARCH_BUG

#if (_MIPS_ISA > _MIPS_ISA_MIPS1)

static inline void  __BUG_ON(unsigned long condition)
{
	if (__builtin_constant_p(condition)) {
		if (condition)
			BUG();
		else
			return;
	}
	if (condition)
		mips_hwtrigger_write(~0ul);
	__asm__ __volatile__("tne $0, %0, %1"
			     : : "r" (condition), "i" (BRK_BUG));
}

#define BUG_ON(C) __BUG_ON((unsigned long)(C))

#define HAVE_ARCH_BUG_ON

#endif /* _MIPS_ISA > _MIPS_ISA_MIPS1 */

#endif

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */
