/* SPDX-License-Identifier: GPL-2.0 */
.section .text,"ax"
.global tp_boot_wptr
tp_boot_wptr:
    .byte 0x10                  /* ldlp 0 */
    .byte 0x22, 0xf0            /* ret */
.global tp_boot_park
tp_boot_park:
    .byte 0x21, 0xf5            /* stopp: no runnable successor */
    .byte 0x60, 0x0c            /* j -4, defensive if restarted */
