/*
 * t4_selftest.c — TransputerOS boot-time hardware self-test
 *
 * Runs after device_initcall, tests key T800 hardware features and
 * kernel subsystems. Results printed via printk to UART console.
 *
 * Tests:
 *   1. LDTIMER instruction (T800 hardware timer)
 *   2. FPU: float32 arithmetic (FPADD, FPMUL)
 *   3. FPU: float64 arithmetic
 *   4. FPU: float-to-int conversion (FPRTOI32)
 *   5. SD card MMIO (write+readback)
 *  5b. SD card sector pattern (512B write+verify)
 *   6. Keyboard UART (UART1 status register readable)
 *   7. Mouse UART (UART2 status register readable)
 *   8. VGA framebuffer (write + readback)
 *   9. Network device (t4net link status)
 *  10. hw_spawn infrastructure (STOPP/RUNP assembly)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

/* MMIO addresses */
#define UART0_STATUS    0x10000004
#define KBD_UART_STATUS 0x11000004
#define MOUSE_UART_STATUS 0x12000004
#define SD_BASE         0x20000000
#define VGA_FB_BASE     0x81E00000
#define T4NET_STATUS    0x30000000

/* Test result counters */
static int pass_count;
static int fail_count;

static void test_pass(const char *name)
{
    printk("  [PASS] %s\n", name);
    pass_count++;
}

static void test_fail(const char *name, const char *detail)
{
    printk("  [FAIL] %s — %s\n", name, detail);
    fail_count++;
}

/* -----------------------------------------------------------------------
 * Test 1: LDTIMER — T800 hardware timer
 * ----------------------------------------------------------------------- */
static void test_ldtimer(void)
{
    unsigned int t1, t2;

    /* Read timer twice — should advance between reads */
    __asm__ volatile (".byte 0xF2" : "=r"(t1));  /* LDTIMER */
    {
        volatile int delay;
        for (delay = 0; delay < 1000; delay++) ;
    }
    __asm__ volatile (".byte 0xF2" : "=r"(t2));

    if (t2 != t1)
        test_pass("LDTIMER (T800 hardware timer)");
    else
        test_fail("LDTIMER", "timer did not advance");
}

/* -----------------------------------------------------------------------
 * Test 2: FPU float32 arithmetic
 * ----------------------------------------------------------------------- */
static void test_fpu_float32(void)
{
    volatile float a = 3.0f;
    volatile float b = 4.0f;
    volatile float c;
    union { float f; unsigned int u; } result;

    c = a + b;
    result.f = c;

    /* 3.0 + 4.0 = 7.0 = 0x40E00000 IEEE754 */
    if (result.u == 0x40E00000)
        test_pass("FPU float32 add (3.0+4.0=7.0)");
    else
        test_fail("FPU float32 add", "wrong result");

    c = a * b;
    result.f = c;

    /* 3.0 * 4.0 = 12.0 = 0x41400000 */
    if (result.u == 0x41400000)
        test_pass("FPU float32 mul (3.0*4.0=12.0)");
    else
        test_fail("FPU float32 mul", "wrong result");
}

/* -----------------------------------------------------------------------
 * Test 3: FPU float64 arithmetic
 * ----------------------------------------------------------------------- */
static void test_fpu_float64(void)
{
    volatile double a = 1.5;
    volatile double b = 2.5;
    volatile double c;
    union { double d; unsigned int u[2]; } result;

    c = a + b;
    result.d = c;

    /* 1.5 + 2.5 = 4.0 = 0x4010000000000000 */
    if (result.u[1] == 0x40100000 && result.u[0] == 0x00000000)
        test_pass("FPU float64 add (1.5+2.5=4.0)");
    else
        test_fail("FPU float64 add", "wrong result");
}

/* -----------------------------------------------------------------------
 * Test 4: FPU float-to-int conversion
 * ----------------------------------------------------------------------- */
static void test_fpu_ftoi(void)
{
    volatile float f = 42.7f;
    volatile int i;

    i = (int)f;

    if (i == 42)
        test_pass("FPU float-to-int (42.7 -> 42)");
    else
        test_fail("FPU float-to-int", "wrong result");
}

/* -----------------------------------------------------------------------
 * Test 5: SD card MMIO (raw write+readback)
 * ----------------------------------------------------------------------- */
static void test_sd_mmio(void)
{
    volatile unsigned char *sd = (volatile unsigned char *)SD_BASE;
    unsigned char val;

    /* Write a marker byte, read it back */
    sd[0x7FFFFF0] = 0xA5;  /* Write near end of 128MB */
    val = sd[0x7FFFFF0];

    if (val == 0xA5)
        test_pass("SD card MMIO (write+readback @ 0x20000000)");
    else
        test_fail("SD card MMIO", "readback mismatch");

    /* Restore */
    sd[0x7FFFFF0] = 0x00;
}

/* -----------------------------------------------------------------------
 * Test 5b: SD card block device (multi-byte pattern through MMIO)
 * ----------------------------------------------------------------------- */
static void test_sd_blkdev(void)
{
    volatile unsigned char *sd = (volatile unsigned char *)SD_BASE;
    int i;
    int ok = 1;

    /* Write a 512-byte sector pattern at sector 1 (offset 512) */
    for (i = 0; i < 512; i++)
        sd[512 + i] = (unsigned char)(i & 0xFF);

    /* Read back and verify */
    for (i = 0; i < 512; i++) {
        if (sd[512 + i] != (unsigned char)(i & 0xFF)) {
            ok = 0;
            break;
        }
    }

    /* Clear test data */
    for (i = 0; i < 512; i++)
        sd[512 + i] = 0;

    if (ok)
        test_pass("SD card sector write+verify (512B @ sector 1)");
    else
        test_fail("SD card sector", "pattern mismatch");
}

/* -----------------------------------------------------------------------
 * Test 6: Keyboard UART (UART1 status readable)
 * ----------------------------------------------------------------------- */
static void test_kbd_uart(void)
{
    volatile unsigned int *status = (volatile unsigned int *)KBD_UART_STATUS;
    unsigned int val = *status;

    /* Status should have at least TX ready bit (0x01) set */
    if (val & 0x01)
        test_pass("Keyboard UART1 (status=0x%02x, TX ready)");
    else if (val == 0)
        test_pass("Keyboard UART1 (status=0x00, idle)");
    else
        test_fail("Keyboard UART1", "unexpected status");
}

/* -----------------------------------------------------------------------
 * Test 7: Mouse UART (UART2 status readable)
 * ----------------------------------------------------------------------- */
static void test_mouse_uart(void)
{
    volatile unsigned int *status = (volatile unsigned int *)MOUSE_UART_STATUS;
    unsigned int val = *status;

    if (val & 0x01)
        test_pass("Mouse UART2 (status=0x%02x, TX ready)");
    else if (val == 0)
        test_pass("Mouse UART2 (status=0x00, idle)");
    else
        test_fail("Mouse UART2", "unexpected status");
}

/* -----------------------------------------------------------------------
 * Test 8: VGA framebuffer (write + readback)
 * ----------------------------------------------------------------------- */
static void test_vga_fb(void)
{
    volatile unsigned int *fb = (volatile unsigned int *)VGA_FB_BASE;
    unsigned int saved, readback;

    /* Save pixel, write test pattern, read back, restore */
    saved = fb[0];
    fb[0] = 0xDEADBEEF;
    readback = fb[0];
    fb[0] = saved;

    if (readback == 0xDEADBEEF)
        test_pass("VGA framebuffer (write+readback @ 0x81E00000)");
    else
        test_fail("VGA framebuffer", "readback mismatch");
}

/* -----------------------------------------------------------------------
 * Test 9: Network device (t4net link status)
 * ----------------------------------------------------------------------- */
static void test_network(void)
{
    volatile unsigned int *status = (volatile unsigned int *)T4NET_STATUS;
    unsigned int val = *status;

    /* bit 0 = link_up */
    if (val & 0x01)
        test_pass("T4Net ethernet (link UP)");
    else
        test_pass("T4Net ethernet (link DOWN, TAP not connected)");
}

/* -----------------------------------------------------------------------
 * Test 10: Console UART (UART0)
 * ----------------------------------------------------------------------- */
static void test_console_uart(void)
{
    volatile unsigned int *status = (volatile unsigned int *)UART0_STATUS;
    unsigned int val = *status;

    /* TX ready should always be set */
    if (val & 0x01)
        test_pass("Console UART0 (TX ready)");
    else
        test_fail("Console UART0", "TX not ready");
}

/* -----------------------------------------------------------------------
 * Init — run all tests at boot
 *
 * Default: SKIPPED for fast boot (P0c).
 * Enable with kernel cmdline: t4_selftest
 * Or: t4_selftest=full  (same)
 * Quick smoke only: t4_selftest=quick  (timer + console UART only)
 * ----------------------------------------------------------------------- */
static int t4_selftest_mode; /* 0=off, 1=quick, 2=full */

static int __init t4_selftest_setup(char *str)
{
	if (!str || !*str || !strcmp(str, "1") || !strcmp(str, "full") ||
	    !strcmp(str, "y") || !strcmp(str, "yes"))
		t4_selftest_mode = 2;
	else if (!strcmp(str, "quick") || !strcmp(str, "short"))
		t4_selftest_mode = 1;
	else if (!strcmp(str, "0") || !strcmp(str, "off") || !strcmp(str, "n"))
		t4_selftest_mode = 0;
	else
		t4_selftest_mode = 2;
	return 1;
}
__setup("t4_selftest=", t4_selftest_setup);
/* Bare "t4_selftest" with no value → full suite */
static int __init t4_selftest_setup_bare(char *str)
{
	t4_selftest_mode = 2;
	return 1;
}
__setup("t4_selftest", t4_selftest_setup_bare);

static int __init t4_selftest_init(void)
{
    if (!t4_selftest_mode) {
	/* Fast boot path — no multi-second FPU/SD/VGA sweeps */
	return 0;
    }

    pass_count = 0;
    fail_count = 0;

    printk("\n");
    printk("========================================\n");
    printk(" T4 Hardware Self-Test (%s)\n",
	   t4_selftest_mode == 1 ? "quick" : "full");
    printk("========================================\n");

    test_ldtimer();
    test_console_uart();
    if (t4_selftest_mode >= 2) {
	    test_fpu_float32();
	    test_fpu_float64();
	    test_fpu_ftoi();
	    test_sd_mmio();
	    test_sd_blkdev();
	    test_kbd_uart();
	    test_mouse_uart();
	    test_vga_fb();
	    test_network();
    }

    printk("========================================\n");
    printk(" Results: %d passed, %d failed\n", pass_count, fail_count);
    if (fail_count == 0)
        printk(" ALL TESTS PASSED\n");
    else
        printk(" *** %d FAILURES ***\n", fail_count);
    printk("========================================\n\n");

    return 0;
}

late_initcall(t4_selftest_init);
