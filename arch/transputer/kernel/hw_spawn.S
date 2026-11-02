/*
 * hw_spawn.S — Hardware-assisted process spawn using STOPP/RUNP
 *
 * Uses the T800's native process scheduling instructions to implement
 * a lightweight spawn where the parent TRULY suspends (STOPP) and the
 * child runs as a real hardware-scheduled process. When the child exits,
 * it RUNPs the parent back to life.
 *
 * This enables concurrent processes: fbwm drawing while tpsh runs,
 * multiple shell commands in background, etc.
 *
 * T800 process model:
 *   - Process = workspace pointer | priority bit
 *   - WPtr[-1] = saved IPtr (when descheduled)
 *   - WPtr[-2] = link pointer (next in queue)
 *   - STOPP: saves IPtr at WPtr[-1], dequeues self
 *   - RUNP: adds process descriptor to hardware queue
 *   - STARTP: starts new process, current continues (for parallel)
 *
 * Opcodes:
 *   STOPP  = 0x21, 0xF5  (OPR 0x15)
 *   RUNP   = 0x23, 0xF9  (OPR 0x39)
 *   STARTP = 0xFD         (OPR 0x0D)
 *   LDPRI  = 0x21, 0xFE  (OPR 0x1E)
 *   GAJW   = 0x23, 0xFC  (OPR 0x3C)
 */

.section .text

/*
 * __transputer_hw_spawn_parent(child_wptr, child_iptr, parent_desc_addr)
 *
 * Called by the parent. Sets up the child process and suspends.
 *
 * After CALL, workspace layout:
 *   WPtr[+3] = return address
 *   WPtr[+4] = child_wptr  (arg0, saved AReg)
 *   WPtr[+5] = child_iptr  (arg1, saved BReg from CALL frame)
 *   WPtr[+6] = parent_desc_addr (arg2)
 *
 * The function:
 *   1. Stores child_iptr at child_wptr[-1] (scheduling slot)
 *   2. Builds parent process descriptor and stores at *parent_desc_addr
 *   3. RUNPs the child
 *   4. STOPPs the parent
 *   5. When resumed: returns 0 (success)
 */
.global __transputer_hw_spawn_parent
__transputer_hw_spawn_parent:
    .byte 0x60, 0xBD        /* ajw -3: allocate 3 local words */

    /* Drain eval stack into locals */
    .byte 0xD3              /* stl 3  (return addr) */
    .byte 0xD1              /* stl 1  (junk) */
    .byte 0xD2              /* stl 2  (junk) */

    /* Step 1: Store child_iptr at child_wptr[-1]
     * child_wptr[-1] is the scheduling IPtr slot */
    .byte 0x75              /* ldl 5 = child_iptr */
    .byte 0x74              /* ldl 4 = child_wptr */
    .byte 0x60, 0xEF        /* stnl -1: child_wptr[-1] = child_iptr */

    /* Step 2: Build parent descriptor and store it.
     * Parent descriptor = current_WPtr | priority.
     * LDLP 7 gives us the caller's WPtr (3 AJW + 4 CALL frame). */
    .byte 0x17              /* ldlp 7 = parent's WPtr (caller frame) */
    .byte 0x21, 0xFE        /* ldpri: AReg = priority */
    .byte 0xF0              /* rev: AReg = parent_WPtr, BReg = priority */
    .byte 0x25, 0xF2        /* or: AReg = parent_WPtr | priority = descriptor */
    .byte 0x76              /* ldl 6 = parent_desc_addr */
    .byte 0xE0              /* stnl 0: *parent_desc_addr = parent_descriptor */

    /* Step 3: RUNP the child.
     * Child descriptor = child_wptr | current_priority.
     * (Using low priority for child so parent can preempt if needed.) */
    .byte 0x74              /* ldl 4 = child_wptr */
    .byte 0x41              /* ldc 1: priority = low (1) */
    .byte 0x25, 0xF2        /* or: AReg = child_wptr | 1 */
    .byte 0x23, 0xF9        /* runp: add child to scheduling queue */

    /* Step 4: STOPP the parent.
     * This saves our IPtr at WPtr[-1] and dequeues us.
     * We resume at the next instruction when someone RUNPs us. */
    .byte 0x21, 0xF5        /* stopp */

    /* === RESUMED HERE after child RUNPs us === */

    /* Step 5: Return 0 (success).
     * Restore the caller's workspace and return. */
    .byte 0x40              /* ldc 0: return value = 0 */
    .byte 0xD1              /* stl 1: save return value */
    .byte 0x71              /* ldl 1: load return value to AReg */
    .byte 0x73              /* ldl 3: load return address */
    .byte 0x23, 0xBC        /* ajw +3: deallocate locals */
    .byte 0xF6              /* gcall: return to caller */

/*
 * __transputer_hw_resume_parent(parent_desc_addr)
 *
 * Called by the child when it exits or completes exec.
 * Loads the parent's process descriptor and RUNPs it.
 *
 * After CALL:
 *   WPtr[+4] = parent_desc_addr (arg0)
 */
.global __transputer_hw_resume_parent
__transputer_hw_resume_parent:
    .byte 0x60, 0xBD        /* ajw -3 */
    .byte 0xD3              /* stl 3 */
    .byte 0xD1              /* stl 1 */
    .byte 0xD2              /* stl 2 */

    /* Load parent descriptor from *parent_desc_addr */
    .byte 0x74              /* ldl 4 = parent_desc_addr */
    .byte 0x30              /* ldnl 0 = *parent_desc_addr = parent descriptor */

    /* RUNP: wake the parent */
    .byte 0x23, 0xF9        /* runp: add parent to scheduling queue */

    /* Return (child continues running or exits) */
    .byte 0x73              /* ldl 3 = return addr */
    .byte 0x23, 0xBC        /* ajw +3 */
    .byte 0xF6              /* gcall */
