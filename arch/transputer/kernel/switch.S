/*
 * arch/transputer/kernel/switch.S — GAJW context switch
 *
 * __transputer_context_switch(prev_thread, next_thread)
 * __transputer_start_thread(prev_thread, next_thread)
 *
 * T800 CALL saves {IPtr, AReg, BReg, CReg} at [WPtr-4..-1], WPtr -= 4.
 * After CALL: AReg = ret_addr, BReg = old CReg (NOT old AReg!).
 * Arguments are in the CALL frame: [WPtr+1]=AReg, [WPtr+2]=BReg.
 *
 * After AJW -3:
 *   [WPtr+3] = ret_addr    (CALL frame slot 0)
 *   [WPtr+4] = prev_thread (CALL frame slot 1, saved AReg)
 *   [WPtr+5] = next_thread (CALL frame slot 2, saved BReg)
 *   ldlp 7   = caller's workspace pointer
 *
 * thread_struct: offset 0=wptr, offset 4=iptr
 */
.section .text
.global __transputer_context_switch
.global __transputer_start_thread
.global __transputer_enter_exec
__transputer_context_switch:
    .byte 0x60, 0xBD        /* ajw -3 */

    /* Drain eval stack (values are garbage except stl 3 = ret_addr,
     * which redundantly writes to the CALL frame slot). */
    .byte 0xD3              /* stl 3 */
    .byte 0xD1              /* stl 1 */
    .byte 0xD2              /* stl 2 */

    /* Save caller WPtr to prev->wptr.
     * caller workspace = ldlp 7 (CALL frame 4 + AJW 3 = 7 words). */
    .byte 0x17              /* ldlp 7 = caller WPtr */
    .byte 0x74              /* ldl 4 = prev_thread (CALL frame slot 1) */
    .byte 0xE0              /* stnl 0: prev->wptr = caller WPtr */

    /* Save ret_addr to prev->iptr */
    .byte 0x73              /* ldl 3 = ret_addr (CALL frame slot 0) */
    .byte 0x74              /* ldl 4 = prev_thread */
    .byte 0xE1              /* stnl 1: prev->iptr = ret_addr */

    /* Load next->iptr then next->wptr (iptr stays in BReg across GAJW) */
    .byte 0x75              /* ldl 5 = next_thread (CALL frame slot 2) */
    .byte 0x31              /* ldnl 1 = next->iptr */
    .byte 0x75              /* ldl 5 = next_thread */
    .byte 0x30              /* ldnl 0 = next->wptr (AReg=wptr, BReg=iptr) */

    /* GAJW: WPtr = next->wptr, AReg = old_WPtr, BReg = next_iptr */
    .byte 0x23, 0xFC        /* gajw */

    /* Get next_iptr into AReg for GCALL */
    .byte 0xF0              /* rev: AReg = next_iptr */

    /* Jump to next task's saved instruction pointer */
    .byte 0xF6              /* gcall */

__transputer_start_thread:
    .byte 0x60, 0xBD        /* ajw -3 */

    /* Drain eval stack */
    .byte 0xD3              /* stl 3 */
    .byte 0xD1              /* stl 1 */
    .byte 0xD2              /* stl 2 */

    /* Save caller WPtr/IPtr so the previous task can resume later. */
    .byte 0x17              /* ldlp 7 = caller WPtr */
    .byte 0x74              /* ldl 4 = prev_thread (CALL frame slot 1) */
    .byte 0xE0              /* stnl 0: prev->wptr = caller WPtr */
    .byte 0x73              /* ldl 3 = ret_addr */
    .byte 0x74              /* ldl 4 = prev_thread */
    .byte 0xE1              /* stnl 1: prev->iptr = ret_addr */

    /* Enter the fresh workspace via a local trampoline.
     * Compute the trampoline address in AReg, then load next->wptr into
     * AReg so the trampoline address is preserved in BReg across GAJW. */
    .byte 0x46              /* ldc 6 = bytes until fresh trampoline */
    .byte 0x21, 0xFB        /* ldpi */
    .byte 0x75              /* ldl 5 = next_thread (CALL frame slot 2) */
    .byte 0x30              /* ldnl 0 = next->wptr */
    .byte 0x23, 0xFC        /* gajw */
    .byte 0xF0              /* rev */
    .byte 0xF6              /* gcall */

__transputer_fresh_trampoline:
    call ret_from_fork
    .byte 0x21, 0xF5        /* stopp if ret_from_fork ever returns */

/*
 * __transputer_enter_exec(entry, wptr)
 *
 * Enter a freshly exec'd userspace image the same way crt0 boot code does:
 * switch to the provided workspace with GAJW, then GCALL the entry point.
 * This never returns on success.
 */
__transputer_enter_exec:
    .byte 0x60, 0xBD        /* ajw -3 */
    .byte 0xD3              /* stl 3: save ret_addr (AReg from CALL) */
    .byte 0xD1              /* stl 1: save entry (BReg = 1st arg) */
    .byte 0xD2              /* stl 2: save wptr  (CReg = 2nd arg) */
    .byte 0x74              /* ldl 4 = entry */
    .byte 0x75              /* ldl 5 = wptr (AReg=wptr, BReg=entry) */
    .byte 0x23, 0xFC        /* gajw */
    .byte 0xF0              /* rev: AReg=entry */
    .byte 0xF6              /* gcall */
    .byte 0x21, 0xF5        /* stopp if exec image ever returns */

/*
 * __transputer_spawn_setjmp(jmp_buf env)
 *
 * Save the caller's execution context into env[0..9] for spawn/exit.
 * Returns 0 on first call. When __transputer_spawn_longjmp() is called,
 * appears to return the exit code (non-zero).
 *
 * jmp_buf layout (10 words):
 *   env[0] = caller's WPtr
 *   env[1] = return address (IPtr)
 *   env[2..9] = caller's workspace slots 0-7
 *
 * After CALL to this function:
 *   WPtr[0] = return address
 *   WPtr[1] = env pointer (was in AReg before CALL)
 *   Caller's WPtr = WPtr + 4
 */
.global __transputer_spawn_setjmp
__transputer_spawn_setjmp:
    /* env[0] = caller's WPtr = our WPtr + 4 */
    .byte 0x14              /* ldlp 4 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0xE0              /* stnl 0 */

    /* env[1] = return address */
    .byte 0x70              /* ldl 0 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0xE1              /* stnl 1 */

    /* env[2..9] = caller's workspace slots 0-7 (at our WPtr[4..11]) */
    .byte 0x74              /* ldl 4 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE2              /* stnl 2 */

    .byte 0x75              /* ldl 5 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE3              /* stnl 3 */

    .byte 0x76              /* ldl 6 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE4              /* stnl 4 */

    .byte 0x77              /* ldl 7 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE5              /* stnl 5 */

    .byte 0x78              /* ldl 8 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE6              /* stnl 6 */

    .byte 0x79              /* ldl 9 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE7              /* stnl 7 */

    .byte 0x7A              /* ldl 10 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE8              /* stnl 8 */

    .byte 0x7B              /* ldl 11 */
    .byte 0x71              /* ldl 1 */
    .byte 0xE9              /* stnl 9 */

    /* Return 0 */
    .byte 0x40              /* ldc 0 */
    .byte 0x22, 0xF0        /* ret */

/*
 * __transputer_spawn_longjmp(jmp_buf env, int exit_code)
 *
 * Restore the execution context saved by __transputer_spawn_setjmp,
 * making it appear that setjmp returned exit_code.
 * If exit_code == 0, it is forced to 1 (POSIX convention).
 *
 * After CALL:
 *   WPtr[0] = return address (unused)
 *   WPtr[1] = env pointer
 *   WPtr[2] = exit_code
 */
.global __transputer_spawn_longjmp
__transputer_spawn_longjmp:
    /* Fix exit_code: if 0, set to 1 */
    .byte 0x72              /* ldl 2: AReg = exit_code */
    .byte 0xC0              /* eqc 0: AReg = (exit_code == 0) ? 1 : 0 */
    .byte 0xA2              /* cj +2: if AReg==0 (nonzero code), skip */
    .byte 0x41              /* ldc 1 */
    .byte 0xD2              /* stl 2: exit_code = 1 */

    /* Restore caller's workspace slots 0-7 from env[2..9]
     * We need to write to the target workspace BEFORE switching.
     * Target WPtr = env[0]. Target slot N = Mem[env[0] + N*4] */

    /* First, load target WPtr into a local for repeated use */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x30              /* ldnl 0: AReg = target WPtr */
    .byte 0xD3              /* stl 3: save target WPtr in local 3 */

    /* Restore slot 0 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x32              /* ldnl 2: AReg = saved slot 0 */
    .byte 0x73              /* ldl 3: AReg = target WPtr, BReg = slot 0 */
    .byte 0xE0              /* stnl 0: target[0] = slot 0 */

    /* Restore slot 1 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x33              /* ldnl 3: AReg = saved slot 1 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE1              /* stnl 1 */

    /* Restore slot 2 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x34              /* ldnl 4 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE2              /* stnl 2 */

    /* Restore slot 3 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x35              /* ldnl 5 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE3              /* stnl 3 */

    /* Restore slot 4 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x36              /* ldnl 6 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE4              /* stnl 4 */

    /* Restore slot 5 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x37              /* ldnl 7 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE5              /* stnl 5 */

    /* Restore slot 6 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x38              /* ldnl 8 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE6              /* stnl 6 */

    /* Restore slot 7 */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x39              /* ldnl 9 */
    .byte 0x73              /* ldl 3 */
    .byte 0xE7              /* stnl 7 */

    /* Build eval stack: CReg=exit_code, BReg=ret_addr, AReg=target_wptr */
    .byte 0x72              /* ldl 2: AReg = exit_code */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x31              /* ldnl 1: AReg = ret_addr, BReg = exit_code */
    .byte 0x71              /* ldl 1 (env) */
    .byte 0x30              /* ldnl 0: AReg = target WPtr, BReg = ret_addr, CReg = exit_code */

    /* GAJW: switch to target workspace */
    .byte 0x23, 0xFC        /* gajw */

    /* AJW -4: prepare for RET (which adds +4) */
    .byte 0x60, 0xBC        /* ajw -4 */

    /* Discard old WPtr from AReg */
    .byte 0xD3              /* stl 3: dump junk, BReg->AReg, CReg->BReg */
    /* Now: AReg = ret_addr, BReg = exit_code */

    /* Store return address for RET */
    .byte 0xD0              /* stl 0: WPtr[0] = ret_addr, BReg->AReg */
    /* Now: AReg = exit_code */

    /* RET: jump to saved return address with exit_code in AReg */
    .byte 0x22, 0xF0        /* ret */

/*
 * transputer_ldtimer — read the low-priority hardware timer.
 *
 * LDTIMER = OPR 0x22 = PFIX 2 (0x22), OPR 2 (0xF2).
 * Pushes current timer value into AReg (CReg=BReg, BReg=AReg, AReg=timer).
 * RET returns to caller with AReg (timer value) preserved.
 *
 * Low-priority timer ticks every 64μs (15,625 Hz).
 * 32-bit counter wraps every ~76.5 hours.
 */
.global transputer_ldtimer
transputer_ldtimer:
    .byte 0x22, 0xF2        /* ldtimer */
    .byte 0x22, 0xF0        /* ret */
