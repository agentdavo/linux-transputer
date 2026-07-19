/*
 * arch/transputer/kernel/head.S
 * Minimal bootstrap diagnostic for the kernel image.
 *
 * This is intentionally reduced to the exact known-good sequence from
 * transputer/test_boot.S:
 *   1. Relocate workspace
 *   2. Clear processor error state
 *   3. Initialize queue heads
 *   4. Start timer
 *   5. Emit one UART marker
 *   6. Halt
 *
 * If this does not print 'S', the failure is below the complex kernel
 * bootstrap logic (BSS zeroing, start_kernel handoff, etc.).
 */

.section .text.start,"ax"
.global _start

_start:
    /* AJW +0x3BFFE4 words = +15 MiB workspace (WPtr → 0x80F00000).
     * Must be above bootmem allocation range (0x80280000 to ~0x80E00000).
     * Previous value (+0xC0000 = +3 MiB) placed WPtr at 0x80300070
     * which overlapped with bootmem hash table allocations, causing
     * return address corruption in the call stack. */
    .byte 0x23, 0x2B, 0x2F, 0x2F, 0x2E, 0xB4

    /* testerr */
    .byte 0x22, 0xF9

    /* clrhalterr */
    .byte 0x25, 0xF7

    /* mint + sthf */
    .byte 0x24, 0xF2
    .byte 0x21, 0xF8

    /* mint + stlf */
    .byte 0x24, 0xF2
    .byte 0x21, 0xFC

    /* ldc 0 ; sttimer */
    .byte 0x40
    .byte 0x25, 0xF4

    /* Emit 'S' to UART (0x10000000) — proves head.S reached.
     * LDC 0x53, LDC 0x10000000, STNL 0 = 11 bytes + 2 NOP pad. */
    .byte 0x25, 0x43                                /* ldc 0x53 ('S') */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40  /* ldc 0x10000000 */
    .byte 0xE0                                      /* stnl 0 */
    .byte 0x20, 0x20                                /* nop pad */

    /* Initialize link/timer words at MinInt..MinInt+10 exactly as in the
     * full bootstrap, then emit 'L'. */
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE0
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE1
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE2
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE3
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE4
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE5
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE6
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE7
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE8
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xE9
    .byte 0x24, 0xF2
    .byte 0x24, 0xF2
    .byte 0xEA

    /* Emit 'L' to UART — proves linker symbols resolved */
    .byte 0x24, 0x4C                                /* ldc 0x4C ('L') */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40  /* ldc 0x10000000 */
    .byte 0xE0                                      /* stnl 0 */
    .byte 0x20, 0x20                                /* nop pad */

    /* Clear BSS wordwise using the exact linker-patched bounds. */
.global __bss_start_load_point
__bss_start_load_point:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0xD1                    /* stl 1 = ptr */

.global __bss_stop_load_point
__bss_stop_load_point:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0xD2                    /* stl 2 = end */

clear_bss_loop:
    .byte 0x71                    /* ldl 1 (ptr) */
    .byte 0x72                    /* ldl 2 (end) */
    .byte 0xF4                    /* diff */
    .byte 0xC0                    /* eqc 0 -> 1 when ptr == end */
    .byte 0xAB                    /* cj +11 -> clear_bss_done when ptr == end */

clear_bss_body:
    .byte 0x71                    /* ldl 1 (ptr) */
    .byte 0x40                    /* ldc 0 */
    .byte 0x20, 0xF0              /* rev -> AReg=ptr, BReg=0 */
    .byte 0xE0                    /* stnl 0 */

    .byte 0x71                    /* ldl 1 */
    .byte 0x84                    /* adc 4 */
    .byte 0xD1                    /* stl 1 */
    .byte 0x60, 0x01              /* j -15 -> clear_bss_loop */

clear_bss_done:
    /* Emit 'G' to UART — proves BSS clear done, about to call start_kernel.
     * PFIX 0 first to clear any stale Oreg from the BSS clear loop. */
    .byte 0x20                                      /* pfix 0 (clear Oreg) */
    .byte 0x24, 0x47                                /* ldc 0x47 ('G') */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40  /* ldc 0x10000000 */
    .byte 0xE0                                      /* stnl 0 */
    .byte 0x20, 0x20, 0x20, 0x20, 0x20, 0x20       /* nop pad */
    .byte 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
    .byte 0x20

    /* Move from the temporary bootstrap workspace onto the image-resident
     * init stack.  transputer-link.c patches this LDC to
     * init_stack + THREAD_SIZE - 64.  Switching only after BSS clearing keeps
     * the clear loop from erasing its own workspace locals. */
.global __init_stack_load_point
__init_stack_load_point:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x23, 0xFC              /* gajw: WPtr = patched absolute address */

    .byte 0x60                    /* ajw -4 */
    .byte 0xBC

    .byte 0x40                    /* stl 3 = 0 (CReg slot) */
    .byte 0xD3
    .byte 0x40                    /* stl 2 = 0 (BReg slot) */
    .byte 0xD2
    .byte 0x40                    /* stl 1 = 0 (AReg/param0 slot) */
    .byte 0xD1

.global __return_label_addr
__return_label_addr:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0xD0                    /* stl 0 = halt return address */

.global __start_kernel_load_point
__start_kernel_load_point:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0xF6                    /* gcall start_kernel */

.global halt
halt:
    /* Print 'H' and stop */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x24, 0x48
    .byte 0x20, 0xF0
    .byte 0xE0
    .byte 0x21, 0xF5

.section .data
.global boot_params
.balign 4
boot_params:
    .long 0
    .long 0
    .long 0x80000070
    .long 0x00800000
    .long 0, 0, 0, 0

/*
 * Keep linker-surrogate data words so the rest of the kernel still links,
 * even though this diagnostic bootstrap does not consume them.
 */
.global __linker_stext
.balign 4
__linker_stext:  .long 0
.global __linker_etext
__linker_etext:  .long 0
.global __linker_edata
__linker_edata:  .long 0
.global __linker_end
__linker_end:    .long 0
