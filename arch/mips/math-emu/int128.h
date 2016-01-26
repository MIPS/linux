

#ifndef __int128_h__
#define __int128_h__

#include <linux/types.h>

typedef struct {
	u64 h, l;
} u128;

static inline u128 add128(u128 a, u128 b)
{
	u128 r;

	r.l = a.l + b.l;
	r.h = a.h + b.h;
	r.h += r.l < a.l;

	return r;
}

static inline u128 sub128(u128 a, u128 b)
{
	u128 r;

	r.l = a.l - b.l;
	r.h = a.h - b.h;
	r.h -= a.l < b.l;

	return r;
}

static inline u128 sll128(u128 x, unsigned s)
{
	u128 r;

	if (s == 0)
		return x;

	if (s >= 128) {
		r.h = r.l = 0;

		return r;
	}

	if (s >= 64) {
		x.h = x.l;
		x.l = 0;
		s -= 64;
	}

	r.l = x.l << s;
	r.h = (x.h << s) | (x.l >> (64 - s));

	return r;
}

static inline u128 srl128(u128 x, unsigned s)
{
	u128 r;

	if (s == 0)
		return x;

	if (s >= 128) {
		r.h = r.l = 0;

		return r;
	}

	if (s >= 64) {
		x.l = x.h;
		x.h = 0;
		s -= 64;
	}

	r.h = x.h >> s;
	r.l = (x.l >> s) | (x.h << (64 - s));

	return r;
}

static inline u128 srl128_sticky(u128 x, unsigned s)
{
	u128 r;

	if (s == 0)
		return x;

	if (s >= 128) {
		r.l = (x.h | x.l) != 0;
		r.h = 0;

		return r;
	}

	if (s > 64) {
		r.l = x.h >> (s - 64);
		r.l |= ((x.h << (128 - s)) | x.l) != 0;
		r.h = 0;

		return r;
	}

	if (s == 64) {
		r.l = x.h | (x.l != 0);
		r.h = 0;

		return r;
	}

	r.l = (x.h << (64 - s)) | (x.l >> s);
	r.l |= (x.l << (64 - s)) != 0;
	r.h = x.h >> s;

	return r;
}

static inline bool lt128(u128 a, u128 b)
{
	if (a.h < b.h)
		return true;

	if (a.h > b.h)
		return false;

	return a.l < b.l;
}

#endif /* __int128_h__ */
