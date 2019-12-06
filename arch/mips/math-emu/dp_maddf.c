/*
 * IEEE754 floating point arithmetic
 * double precision: MADDF.f (Fused Multiply Add)
 * MADDF.fmt: FPR[fd] = FPR[fd] + (FPR[fs] x FPR[ft])
 *
 * MIPS floating point support
 * Copyright (C) 2015-2016 Imagination Technologies, Ltd.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; version 2 of the License.
 */

#include "ieee754dp.h"
#include "int128.h"

enum maddf_flags {
	maddf_negate_product	= 1 << 0,
};

static union ieee754dp _dp_maddf(union ieee754dp z, union ieee754dp x,
				 union ieee754dp y, enum maddf_flags flags)
{
	int re;
	int rs;
	u128 rm, am;

	COMPXDP;
	COMPYDP;
	COMPZDP;

	EXPLODEXDP;
	EXPLODEYDP;
	EXPLODEZDP;

	FLUSHXDP;
	FLUSHYDP;
	FLUSHZDP;

	ieee754_clearcx();

	switch (zc) {
	case IEEE754_CLASS_SNAN:
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754dp_nanxcpt(z);
	case IEEE754_CLASS_DNORM:
		DPDNORMZ;
	/* QNAN is handled separately below */
	}

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754dp_nanxcpt(y);

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754dp_nanxcpt(x);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return y;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return x;


	/*
	 * Infinity handling
	 */
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		ieee754_setcx(IEEE754_INVALID_OPERATION);
		return ieee754dp_indef();

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		return ieee754dp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		if (zc == IEEE754_CLASS_ZERO) {
			if ((xs ^ ys) == zs)
				return z;
			return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);
		}
		/* Multiplication is 0 so just return z */
		return z;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		DPDNORMX;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		DPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		if (zc == IEEE754_CLASS_QNAN)
			return z;
		else if (zc == IEEE754_CLASS_INF)
			return ieee754dp_inf(zs);
		/* fall through to real computations */
	}

	/* Finally get to do some computation */

	/*
	 * Do the multiplication bit first
	 *
	 * rm = xm * ym, re = xe + ye basically
	 *
	 * At this point xm and ym should have been normalized.
	 */
	assert(xm & DP_HIDDEN_BIT);
	assert(ym & DP_HIDDEN_BIT);

	re = xe + ye + DP_EBIAS + 1;
	rs = xs ^ ys;
	if (flags & maddf_negate_product)
		rs ^= 1;

	/* shunt to top of word */
	xm <<= 64 - (DP_FBITS + 2);
	ym <<= 64 - (DP_FBITS + 1);

	/* multiply 64b xm * 64b ym to give 128b result */
	mul_u64_u64(xm, ym, &rm.l, &rm.h);

	if (!(rm.h & BIT_ULL(62))) {
		rm = sll128(rm, 1);
		re--;
	}

	if (zc == IEEE754_CLASS_ZERO) {
		rm = srl128_sticky(rm, 71);
		return ieee754dp_format(rs, re - DP_EBIAS, rm.l);
	}

	/* And now the addition */
	assert(zm & DP_HIDDEN_BIT);

	am.h = zm << (126 - 64 - 52);
	am.l = 0;

	ze += DP_EBIAS;

	if (zs == rs) {
		if (re > ze) {
			am = srl128_sticky(am, re - ze);
		} else if (ze > re) {
			rm = srl128_sticky(rm, ze - re);
			re = ze;
		}

		rm = add128(rm, am);

		if (rm.h & BIT_ULL(63))
			rm = srl128_sticky(rm, 1);
		else
			re--;

		rm = srl128_sticky(rm, 71);
	} else {
		if (re > ze) {
			am = srl128_sticky(am, re - ze);
			rm = sub128(rm, am);
		} else if (ze > re) {
			rm = srl128_sticky(rm, ze - re);
			rm = sub128(am, rm);
			re = ze;
			rs ^= 1;
		} else {
			if (lt128(am, rm)) {
				rm = sub128(rm, am);
			} else if (lt128(rm, am)) {
				rm = sub128(am, rm);
				rs ^= 1;
			} else {
				return ieee754dp_zero(ieee754_csr.rm == FPU_CSR_RD);
			}
		}

		re--;

		if (rm.h) {
			unsigned s;
			s = (rm.h ? __builtin_clzll(rm.h) : 64) - 1;
			rm = sll128(rm, s);
			if (rm.l)
				rm.h |= 0x1;
			re -= s;
		} else {
			unsigned s;

			s = rm.l ? __builtin_clzll(rm.l) : 64;

			if (s == 0) {
				rm.h = (rm.l >> 1) | (rm.l & 0x1);
				re -= 63;
			} else {
				s--;
				rm.h = rm.l << s;
				re -= s + 64;
			}
		}

		rm = srl128_sticky(rm, 71);
	}

	return ieee754dp_format(rs, re - DP_EBIAS + 1, rm.l);
}

union ieee754dp ieee754dp_maddf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y)
{
	return _dp_maddf(z, x, y, 0);
}

union ieee754dp ieee754dp_msubf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y)
{
	return _dp_maddf(z, x, y, maddf_negate_product);
}
