/*
 * Minimal transputer kernel-side assembly syscall probe entries.
 *
 * These avoid normal C function entry so we can separate the raw
 * userspace->kernel call boundary from kernel C ABI issues.
 */

.section .text,"ax"

.global __transputer_getpid_entry
__transputer_getpid_entry:
    /* GCALL leaves the return IPtr in AReg. Save it in WPtr[0] so RET works. */
    stl 0

    /* return 1 */
    ldc 1
    ret

.global __transputer_ioctl_entry
__transputer_ioctl_entry:
    /* Normalize the GCALL entry so we can call a normal C helper. */
    stl 0
    ldl 3
    ldl 2
    ldl 1
    call __transputer_ioctl0
    ret
