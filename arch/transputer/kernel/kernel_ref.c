/*
 * Kernel reference stub to force start_kernel to be linked
 * TCC assembler doesn't support .extern directive, so we use this C stub
 */

#include <linux/init.h>

extern void start_kernel(void);

/* Function that calls start_kernel - forces linker to include it */
void __init __attribute__((used)) kernel_entry_stub(void) {
    start_kernel();
}
