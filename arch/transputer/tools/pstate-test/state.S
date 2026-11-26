/* SPDX-License-Identifier: GPL-2.0 */
.text
.global pstate_seed
pstate_seed:
    ldc 0
    .byte 0x2b,0xf7          /* stflags: disable traps, clear existing flags */
#if ROLE != 1
    /* Generate a sticky FP exception with traps disabled. */
    fpldzerosn
    fpldzerosn
    .byte 0x28,0xfc          /* fpdiv */
#endif
    ldl 1
#if ROLE == 1
    fpldnldb
#else
    fpldnlsn
#endif
    ldl 1
    ldnlp 2
#if ROLE == 1
    fpldnlsn
#else
    fpldnldb
#endif
    ldl 1
    ldnlp 4
#if ROLE == 1
    fpldnldb
#else
    fpldnlsn
#endif
    ldl 1
    ldnl 8
    ldl 1
    ldnl 7
    ldl 1
    ldnl 6
    move2dinit
#if ROLE == 0
    .byte 0x2d,0xf4          /* fprp */
#elif ROLE == 1
    .byte 0x2d,0xf5          /* fprm */
#else
    .byte 0x2d,0xf6          /* fprz */
#endif
    ret

.global pstate_capture
pstate_capture:
    .byte 0x2b,0xf6          /* ldflags */
    ldl 1
    stnl 10
    ldl 1
    .byte 0x60,0xff          /* fpstall */
    ldl 1
    ldnlp 7
    .byte 0x61,0xf0          /* stmove2dinit */
    ldl 1
    ldnl 9
    ldl 1
    ldnl 8
    ldl 1
    ldnl 7
    move2dinit
    ldl 1
    .byte 0x60,0xfe          /* fpldall */
    ret
