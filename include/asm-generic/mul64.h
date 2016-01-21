#ifndef _ASM_GENERIC_MUL64_H
#define _ASM_GENERIC_MUL64_H

#include <linux/types.h>

#if defined(CONFIG_ARCH_SUPPORTS_INT128) && defined(__SIZEOF_INT128__)

#ifndef mul_u64_u32_shr
static inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static inline u64 mul_u64_u64_shr(u64 a, u64 mul, unsigned int shift)
{
	return (u64)(((unsigned __int128)a * mul) >> shift);
}
#endif /* mul_u64_u64_shr */

#else /* !CONFIG_ARCH_SUPPORTS_INT128 || !defined(__SIZEOF_INT128__) */

#ifndef mul_u64_u32_shr
static inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
	u32 ah, al;
	u64 ret;

	al = a;
	ah = a >> 32;

	ret = ((u64)al * mul) >> shift;
	if (ah)
		ret += ((u64)ah * mul) << (32 - shift);

	return ret;
}
#endif /* mul_u64_u32_shr */

#ifndef mul_u64_u64_shr
static inline u64 mul_u64_u64_shr(u64 a, u64 b, unsigned int shift)
{
	union {
		u64 ll;
		struct {
#ifdef __BIG_ENDIAN
			u32 high, low;
#else
			u32 low, high;
#endif
		} l;
	} rl, rm, rn, rh, a0, b0;
	u64 c;

	a0.ll = a;
	b0.ll = b;

	rl.ll = (u64)a0.l.low * b0.l.low;
	rm.ll = (u64)a0.l.low * b0.l.high;
	rn.ll = (u64)a0.l.high * b0.l.low;
	rh.ll = (u64)a0.l.high * b0.l.high;

	/*
	 * Each of these lines computes a 64-bit intermediate result into "c",
	 * starting at bits 32-95.  The low 32-bits go into the result of the
	 * multiplication, the high 32-bits are carried into the next step.
	 */
	rl.l.high = c = (u64)rl.l.high + rm.l.low + rn.l.low;
	rh.l.low = c = (c >> 32) + rm.l.high + rn.l.high + rh.l.low;
	rh.l.high = (c >> 32) + rh.l.high;

	/*
	 * The 128-bit result of the multiplication is in rl.ll and rh.ll,
	 * shift it right and throw away the high part of the result.
	 */
	if (shift == 0)
		return rl.ll;
	if (shift < 64)
		return (rl.ll >> shift) | (rh.ll << (64 - shift));
	return rh.ll >> (shift & 63);
}
#endif /* mul_u64_u64_shr */

#endif /* !CONFIG_ARCH_SUPPORTS_INT128 || !defined(__SIZEOF_INT128__) */

#ifndef mul_u64_u32_div
static inline u64 mul_u64_u32_div(u64 a, u32 mul, u32 divisor)
{
	union {
		u64 ll;
		struct {
#ifdef __BIG_ENDIAN
			u32 high, low;
#else
			u32 low, high;
#endif
		} l;
	} u, rl, rh;

	u.ll = a;
	rl.ll = (u64)u.l.low * mul;
	rh.ll = (u64)u.l.high * mul + rl.l.high;

	/* Bits 32-63 of the result will be in rh.l.low. */
	rl.l.high = do_div(rh.ll, divisor);

	/* Bits 0-31 of the result will be in rl.l.low.	*/
	do_div(rl.ll, divisor);

	rl.l.high = rh.l.low;
	return rl.ll;
}
#endif /* mul_u64_u32_div */

#endif /* _ASM_GENERIC_MUL64_H */
