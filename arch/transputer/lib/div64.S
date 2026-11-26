/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>

	.text
	.globl __div64_32
	.type __div64_32, @function
/*
 * W[1] points to the little-endian dividend, W[2] is the divisor.
 * LDIV divides C:B by A and returns quotient in A and remainder in B.
 * Its quotient fits in one word only when C < A.
 */
__div64_32:
	ldl 2
	mint
	xor
	ldl 1
	ldnl 1
	mint
	xor
	gt			/* unsigned divisor > high */
	cj .Ldivide_high

	ldl 1
	ldnl 1
	ldc 0
	ldl 1
	stnl 1			/* quotient high = 0, A = dividend high */
	j .Ldivide_low

.Ldivide_high:
	ldc 0
	ldl 1
	ldnl 1
	ldl 2
	ldiv
	ldl 1
	stnl 1			/* quotient high, A = remainder */

.Ldivide_low:
	ldl 1
	ldnl 0
	ldl 2
	ldiv
	ldl 1
	stnl 0			/* quotient low, A = remainder */
	ret
	.size __div64_32, . - __div64_32
EXPORT_SYMBOL(__div64_32)
