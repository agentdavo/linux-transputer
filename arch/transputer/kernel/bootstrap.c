/*
 * Linux Transputer Bootstrap - Stage 2
 *
 * Simple C function that does minimal setup before calling start_kernel.
 * This replaces the complex assembly BSS clearing and setup.
 */

/* External declarations */
extern void clear_bss(void);
extern void start_kernel(void);

/* Simple bootstrap function called from head.S */
void __attribute__((noreturn)) bootstrap_stage2(void)
{
    /* Clear BSS if needed (emulator may already zero memory) */
    clear_bss();

    /* Enter the real kernel */
    start_kernel();

    /* Should never return */
    for (;;) {
    }
}
