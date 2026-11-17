/* SPDX-License-Identifier: GPL-2.0 */
/* The three bFLTs have different FP types, values, rounding, flags and BM
 * state. ROLE 0 runs inside init; roles 1 and 2 are its synchronous child
 * and grandchild. Capture native status words rather than synthesizing an
 * emulator-specific FP image.
 */
extern long __transputer_syscall(long, long, long, long, long, long, long);
extern void pstate_seed(const unsigned *values);
extern void pstate_capture(unsigned *out);

static void puts_uart(const char *p)
{
	while (*p)
		*(volatile unsigned *)0x10000000 = (unsigned char)*p++;
}

static void hex_uart(unsigned x)
{
	int i;

	for (i = 28; i >= 0; i -= 4)
		*(volatile unsigned *)0x10000000 = "0123456789abcdef"[(x >> i) & 15];
}

long pstate_probe(char **envp)
{
	const unsigned values[] = {
		0x3fa00010 + ROLE, 0xc0100000 + ROLE, 0xc0300020 + ROLE,
		0x40208000 + ROLE, 0x40c00030 + ROLE, 0x40012000 + ROLE,
		3 + ROLE, 20 + ROLE * 4, 48 + ROLE * 8
	};
	const char *path = ROLE == 0 ? "/usr/bin/p-child" : "/usr/bin/p-grandchild";
	const char *argv[] = { path, 0 };
	unsigned before[11] = {0}, after[11] = {0};
	int i, failed = 0;
	long rc;

#if ROLE
	/* Fresh children should not inherit their caller's register values. */
	pstate_capture(before);
	if (before[1] || before[3] || before[5] ||
	    before[7] || before[8] || before[9])
		failed = 1;
#endif
	pstate_seed(values);
	pstate_capture(before);
	if (ROLE == 2)
		rc = __transputer_syscall(172, 1, 2, 3, 4, 5, 6); /* getpid */
	else
		rc = __transputer_syscall(2000, (long)path, (long)argv,
					  (long)envp, 0, 0, 0); /* tp_spawn */
	pstate_capture(after);

	for (i = 0; i < 11; i++) {
		/* Only the low word of a single-precision slot is defined. */
		if ((ROLE == 1 && i == 4) || (ROLE != 1 && (i == 2 || i == 6)))
			continue;
		if (after[i] != before[i]) {
			puts_uart("P-STATE-DIFF role/word/before/after ");
			hex_uart(ROLE); puts_uart("/"); hex_uart(i);
			puts_uart("/"); hex_uart(before[i]); puts_uart("/"); hex_uart(after[i]);
			puts_uart("\n");
			failed = 1;
		}
	}
	if (before[1] != values[4] || before[3] != values[2] ||
	    before[5] != values[0] || before[7] != values[6] ||
	    before[8] != values[7] || before[9] != values[8])
		failed = 1;
	/* The seed cleared flags at all levels. Roles 0 and 2 then did
	 * 0.0 / 0.0 with traps disabled. Test the sticky flag without
	 * assuming the emulator's native status-word encoding.
	 */
	if ((before[10] != 0) != (ROLE != 1))
		failed = 1;
	if (ROLE == 1 ? (before[2] != values[5] || before[6] != values[1])
		      : before[4] != values[3])
		failed = 1;
	if (rc != (ROLE == 2 ? 1 : ROLE == 1 ? 37 : 23))
		failed = 1;
	puts_uart(failed ? "P-STATE-FAIL " : "P-STATE-PASS ");
	hex_uart(ROLE);
	puts_uart("\n");
	return failed ? 99 : ROLE == 2 ? 37 : 23;
}

#if ROLE
void _start_c(int argc, char **argv, char **envp)
{
	long rc = pstate_probe(envp);

	__transputer_syscall(93, rc, 0, 0, 0, 0, 0);
	for (;;)
		;
}
#endif
