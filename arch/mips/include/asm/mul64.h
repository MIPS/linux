#ifndef _MIPS_ASM_MUL64_H
#define _MIPS_ASM_MUL64_H

#ifdef CONFIG_CPU_MIPS64_R6

static inline void mul_u64_u64(u64 a, u64 b, u64 *rl, u64 *rh)
{
	u64 r;

	if (rl) {
		asm("dmulu %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
		*rl = r;
	}

	if (rh) {
		asm("dmuhu %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
		*rh = r;
	}
}
#define mul_u64_u64 mul_u64_u64

#endif /* CONFIG_CPU_MIPS64_R6 */

#include <asm-generic/mul64.h>

#endif /* _MIPS_ASM_MUL64_H */
