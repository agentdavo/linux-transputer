/*
 * hw_spawn.S — Hardware-assisted process spawn using STOPP/RUNP
 *
 * Uses the TP32's native process scheduling instructions to implement
 * a lightweight spawn where the parent TRULY suspends (STOPP) and the
 * child runs as a real hardware-scheduled process. When the child exits,
 * it RUNPs the parent back to life.
 *
 * This enables concurrent processes: fbwm drawing while tpsh runs,
 * multiple shell commands in background, etc.
 *
 * TP32 process model:
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
 *   WPtr[0] = return address
 *   WPtr[1] = child_wptr
 *   WPtr[2] = child_iptr
 *   WPtr[3] = parent_desc_addr
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
    /* Step 1: Store child_iptr at child_wptr[-1]
     * child_wptr[-1] is the scheduling IPtr slot */
    .byte 0x72              /* ldl 2 = child_iptr */
    .byte 0x71              /* ldl 1 = child_wptr */
    .byte 0x60, 0xEF        /* stnl -1: child_wptr[-1] = child_iptr */

    /* Step 2: Build parent descriptor and store it.
     * STOPP saves the continuation relative to this call frame. */
    .byte 0x10              /* ldlp 0 = current WPtr */
    .byte 0x21, 0xFE        /* ldpri: AReg = priority */
    .byte 0x24, 0xFB        /* or: AReg = parent_WPtr | priority = descriptor */
    .byte 0x73              /* ldl 3 = parent_desc_addr */
    .byte 0xE0              /* stnl 0: *parent_desc_addr = parent_descriptor */

    /* Step 3: RUNP the child.
     * Child descriptor = child_wptr | current_priority.
     * (Using low priority for child so parent can preempt if needed.) */
    .byte 0x71              /* ldl 1 = child_wptr */
    .byte 0x41              /* ldc 1: priority = low (1) */
    .byte 0x24, 0xFB        /* or: AReg = child_wptr | 1 */
    .byte 0x23, 0xF9        /* runp: add child to scheduling queue */

    /* Step 4: STOPP the parent.
     * This saves our IPtr at WPtr[-1] and dequeues us.
     * We resume at the next instruction when someone RUNPs us. */
    .byte 0x21, 0xF5        /* stopp */

    /* === RESUMED HERE after child RUNPs us === */

    /* Step 5: Return 0 through the original call frame. */
    .byte 0x40              /* ldc 0: return value = 0 */
    .byte 0x22, 0xF0        /* ret */

/*
 * __transputer_hw_resume_parent(parent_desc_addr, stop_wptr)
 *
 * The parent can run immediately after RUNP and free the child image.
 * Move to the kernel's permanent stopping workspace first. This
 * function never returns to the child's workspace.
 */
.global __transputer_hw_resume_parent
__transputer_hw_resume_parent:
    .byte 0x71              /* ldl 1 = parent_desc_addr */
    .byte 0x30              /* ldnl 0 = *parent_desc_addr = parent descriptor */
    .byte 0x40              /* ldc 0 */
    .byte 0x71              /* ldl 1 = parent_desc_addr */
    .byte 0xE0              /* stnl 0: clear the descriptor; pop original into A */
    .byte 0x72              /* ldl 2 = stop_wptr, parent descriptor moves to B */
    .byte 0x23, 0xFC        /* gajw: leave the child workspace */
    .byte 0xF0              /* rev: place parent descriptor in A */
    .byte 0x23, 0xF9        /* runp: add parent to scheduling queue */
    .byte 0x21, 0xF5        /* stopp */
    .byte 0x60, 0x0C        /* j -4: stop again if ever resumed */
