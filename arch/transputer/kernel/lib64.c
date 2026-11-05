// SPDX-License-Identifier: GPL-2.0
/*
 * 64-bit arithmetic helpers for Transputer (32-bit architecture)
 * Minimal implementations for linking - TCC should provide these
 */

typedef long long s64;
typedef unsigned long long u64;
typedef unsigned int u32;

/* TinyCC lowers GCC's type-generic overflow builtins to these helpers. */
#define TCC_UNSIGNED_OVERFLOW(bits, type, max) \
int __tcc_add_overflow_u##bits(type a, type b, type *r) \
{ type v = (type)(a + b); *r = v; return a > (type)((max) - b); } \
int __tcc_sub_overflow_u##bits(type a, type b, type *r) \
{ *r = (type)(a - b); return a < b; } \
int __tcc_mul_overflow_u##bits(type a, type b, type *r) \
{ *r = (type)(a * b); return b != 0 && a > (type)((max) / b); }

#define TCC_SIGNED_SMALL_OVERFLOW(bits, type, wide, min, max) \
int __tcc_add_overflow_s##bits(type a, type b, type *r) \
{ wide v = (wide)a + (wide)b; *r = (type)v; return v < (wide)(min) || v > (wide)(max); } \
int __tcc_sub_overflow_s##bits(type a, type b, type *r) \
{ wide v = (wide)a - (wide)b; *r = (type)v; return v < (wide)(min) || v > (wide)(max); } \
int __tcc_mul_overflow_s##bits(type a, type b, type *r) \
{ wide v = (wide)a * (wide)b; *r = (type)v; return v < (wide)(min) || v > (wide)(max); }

TCC_UNSIGNED_OVERFLOW(8, unsigned char, 0xffu)
TCC_UNSIGNED_OVERFLOW(16, unsigned short, 0xffffu)
TCC_UNSIGNED_OVERFLOW(32, unsigned int, 0xffffffffu)
TCC_SIGNED_SMALL_OVERFLOW(8, signed char, int, -128, 127)
TCC_SIGNED_SMALL_OVERFLOW(16, short, int, -32768, 32767)
TCC_SIGNED_SMALL_OVERFLOW(32, int, long long,
			  (-2147483647LL - 1), 2147483647LL)

int __tcc_add_overflow_u64(u64 a, u64 b, u64 *r)
{
	*r = a + b;
	return *r < a;
}

int __tcc_sub_overflow_u64(u64 a, u64 b, u64 *r)
{
	*r = a - b;
	return a < b;
}

int __tcc_mul_overflow_u64(u64 a, u64 b, u64 *r)
{
	*r = a * b;
	return b != 0 && a > 0xffffffffffffffffULL / b;
}

int __tcc_add_overflow_s64(s64 a, s64 b, s64 *r)
{
	u64 ua = a, ub = b, ur = ua + ub;
	*r = (s64)ur;
	return (int)((~(ua ^ ub) & (ua ^ ur)) >> 63);
}

int __tcc_sub_overflow_s64(s64 a, s64 b, s64 *r)
{
	u64 ua = a, ub = b, ur = ua - ub;
	*r = (s64)ur;
	return (int)(((ua ^ ub) & (ua ^ ur)) >> 63);
}

int __tcc_mul_overflow_s64(s64 a, s64 b, s64 *r)
{
	u64 ua = a, ub = b;
	u64 ma = a < 0 ? 0 - ua : ua;
	u64 mb = b < 0 ? 0 - ub : ub;
	int negative = (a < 0) != (b < 0);
	u64 limit = negative ? 0x8000000000000000ULL
			     : 0x7fffffffffffffffULL;
	u64 product = ma * mb;
	int overflow = mb != 0 && ma > limit / mb;
	*r = (s64)(negative ? 0 - product : product);
	return overflow;
}

typedef union {
	u64 v;
	struct {
		u32 lo;
		u32 hi;
	} p;
} u64_u;

static u64_u u64_from_u64(u64 v)
{
	u64_u u;
	u.v = v;
	return u;
}

static u64 u64_to_u64(u64_u u)
{
	return u.v;
}

static int u64_is_zero(u64_u v)
{
	return v.p.hi == 0 && v.p.lo == 0;
}

static int u64_is_neg(u64_u v)
{
	return (v.p.hi & 0x80000000u) != 0;
}

static u64_u u64_add(u64_u a, u64_u b)
{
	u64_u r;
	u32 lo = a.p.lo + b.p.lo;
	u32 carry = (lo < a.p.lo);
	r.p.lo = lo;
	r.p.hi = a.p.hi + b.p.hi + carry;
	return r;
}

static u64_u u64_neg(u64_u v)
{
	u64_u inv;
	u64_u one;
	inv.p.lo = ~v.p.lo;
	inv.p.hi = ~v.p.hi;
	one.p.lo = 1;
	one.p.hi = 0;
	return u64_add(inv, one);
}

/*
 * (hi:lo) / d, for hi < d, is one transputer long divide: ldiv leaves the
 * quotient in A and the remainder in B, which is how a u64 comes back -
 * the quotient in the low word, the remainder in the high one.
 */
u64 __tp_ldivqr(u32 hi, u32 lo, u32 d);
__asm__(
	"	.text\n"
	"	.globl __tp_ldivqr\n"
	"	.type __tp_ldivqr, @function\n"
	"__tp_ldivqr:\n"		/* hi = W1, lo = W2, d = W3 */
	"	ldl 1\n"
	"	ldl 2\n"
	"	ldl 3\n"
	"	ldiv\n"
	"	ret\n");

static int u32_clz(u32 x)	/* x != 0 */
{
	int n = 0;

	if (!(x & 0xffff0000u)) { n += 16; x <<= 16; }
	if (!(x & 0xff000000u)) { n += 8; x <<= 8; }
	if (!(x & 0xf0000000u)) { n += 4; x <<= 4; }
	if (!(x & 0xc0000000u)) { n += 2; x <<= 2; }
	if (!(x & 0x80000000u)) n += 1;
	return n;
}

/*
 * A divisor below 2^32 takes two long divides: the high word, then its
 * remainder with the low word.  A wider one leaves a quotient of at most
 * 32 bits, estimated from the divisor's normalised top word with the
 * dividend halved so the divide cannot overflow, then corrected once
 * (Hacker's Delight, divdu).  It replaced a 64-step shift-and-subtract
 * loop of helper calls, about 14,000 T800 cycles a divide.
 */
static u64_u u64_udivmod(u64_u dividend, u64_u divisor, u64_u *rem_out)
{
	u64_u q, r, t;

	if (divisor.p.hi == 0) {
		u32 d = divisor.p.lo;

		t.v = __tp_ldivqr(0, dividend.p.hi, d);
		q.p.hi = t.p.lo;
		t.v = __tp_ldivqr(t.p.hi, dividend.p.lo, d);
		q.p.lo = t.p.lo;
		r.p.lo = t.p.hi;
		r.p.hi = 0;
	} else {
		int n = u32_clz(divisor.p.hi);
		u64 u = dividend.v, v = divisor.v;
		u64 u1 = u >> 1;
		u32 q0;

		t.v = __tp_ldivqr((u32)(u1 >> 32), (u32)u1,
				  (u32)((v << n) >> 32));
		q0 = (u32)(((u64)t.p.lo << n) >> 31);
		if (q0 != 0)
			q0--;
		r.v = u - (u64)q0 * v;
		if (r.v >= v) {
			q0++;
			r.v -= v;
		}
		q.p.hi = 0;
		q.p.lo = q0;
	}
	if (rem_out)
		*rem_out = r;
	return q;
}

/* Division and modulo */
s64 __divdi3(s64 dividend, s64 divisor)
{
	int neg = 0;
	u64_u a = u64_from_u64((u64)dividend);
	u64_u b = u64_from_u64((u64)divisor);
	u64_u q;

	if (u64_is_zero(b))
		return 0; /* Avoid divide by zero */

	if (u64_is_neg(a)) {
		a = u64_neg(a);
		neg ^= 1;
	}
	if (u64_is_neg(b)) {
		b = u64_neg(b);
		neg ^= 1;
	}

	q = u64_udivmod(a, b, 0);
	if (neg)
		q = u64_neg(q);
	return (s64)u64_to_u64(q);
}

u64 __udivdi3(u64 dividend, u64 divisor)
{
	u64_u a = u64_from_u64(dividend);
	u64_u b = u64_from_u64(divisor);
	u64_u q;

	if (u64_is_zero(b))
		return 0;

	q = u64_udivmod(a, b, 0);
	return u64_to_u64(q);
}

s64 __moddi3(s64 dividend, s64 divisor)
{
	int neg = 0;
	u64_u a = u64_from_u64((u64)dividend);
	u64_u b = u64_from_u64((u64)divisor);
	u64_u r;

	if (u64_is_zero(b))
		return 0;

	if (u64_is_neg(a)) {
		a = u64_neg(a);
		neg ^= 1;
	}
	if (u64_is_neg(b))
		b = u64_neg(b);

	u64_udivmod(a, b, &r);
	if (neg)
		r = u64_neg(r);
	return (s64)u64_to_u64(r);
}

u64 __umoddi3(u64 dividend, u64 divisor)
{
	u64_u a = u64_from_u64(dividend);
	u64_u b = u64_from_u64(divisor);
	u64_u r;

	if (u64_is_zero(b))
		return 0;
	u64_udivmod(a, b, &r);
	return u64_to_u64(r);
}

/* Shifts */
s64 __ashldi3(s64 value, int shift)
{
	u64_u v = u64_from_u64((u64)value);
	u64_u r;

	if (shift >= 64)
		return 0;
	if (shift == 0)
		return value;
	if (shift >= 32) {
		r.p.hi = v.p.lo << (shift - 32);
		r.p.lo = 0;
	} else {
		r.p.hi = (v.p.hi << shift) | (v.p.lo >> (32 - shift));
		r.p.lo = v.p.lo << shift;
	}
	return (s64)u64_to_u64(r);
}

s64 __ashrdi3(s64 value, int shift)
{
	u64_u v = u64_from_u64((u64)value);
	u64_u r;
	u32 sign = (v.p.hi & 0x80000000u) ? 0xFFFFFFFFu : 0u;

	if (shift >= 64)
		return sign ? -1 : 0;
	if (shift == 0)
		return value;
	if (shift >= 32) {
		r.p.lo = v.p.hi >> (shift - 32);
		r.p.hi = sign;
	} else {
		r.p.lo = (v.p.lo >> shift) | (v.p.hi << (32 - shift));
		r.p.hi = (v.p.hi >> shift) | (sign << (32 - shift));
	}
	return (s64)u64_to_u64(r);
}

u64 __lshrdi3(u64 value, int shift)
{
	u64_u v = u64_from_u64(value);
	u64_u r;

	if (shift >= 64)
		return 0;
	if (shift == 0)
		return value;
	if (shift >= 32) {
		r.p.lo = v.p.hi >> (shift - 32);
		r.p.hi = 0;
	} else {
		r.p.lo = (v.p.lo >> shift) | (v.p.hi << (32 - shift));
		r.p.hi = v.p.hi >> shift;
	}
	return u64_to_u64(r);
}

/* Built-in functions - Note: TCC provides __builtin_* as actual built-ins,
 * but kernel may need these as regular functions for linking */

/* Error handlers */
void ____wrong_branch_error(void)
{
	/* Static branch error - should not be called at runtime */
	while (1);
}

void __bad_size_call_parameter(void)
{
	/* Bad size in function call */
	while (1);
}

void __here(void)
{
	/* Marker function - no-op */
}

void __get_user_bad(void)
{
	/* Bad get_user size */
	while (1);
}

void __put_user_bad(void)
{
	/* Bad put_user size */
	while (1);
}

/* I/O stub */
void __outbyte(int c)
{
	/* TODO: Link to iserver_putc or early console */
	(void)c;
}
