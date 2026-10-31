// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer timer and clocksource support — 2.6.39 style
 *
 * The Transputer has a simple hardware timer accessed via LDTIMER.
 * This provides a minimal software-based timer for kernel operation.
 *
 * Based on arch/h8300/kernel/time.c
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/profile.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/jiffies.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/timex.h>
#include <asm/irqflags.h>
#include <asm/uart.h>

/* Default CPU frequency (T800 @ 20MHz) — overridden by LDDEVID detection */
static unsigned long transputer_cpu_freq = 20000000UL;

/* Timer tick frequency for low priority processes (64us per tick) */
#define TRANSPUTER_TIMER_FREQ	15625UL

/* Read product identity via LDDEVID instruction (OPR 0x17C).
 * Returns: T414=0x00, T800-20=0x05, T805=0x06, T806=0x806.
 * Uses TCC builtin — inline asm corrupts the eval stack. */
extern unsigned int __lddevid(void);

static void __init detect_cpu_frequency(void)
{
	unsigned int devid = __lddevid();

	switch (devid) {
	case 0x000: /* T414 */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: T414 @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x005: /* T800-20 */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: T800-20 @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x006: /* T805 */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: T805 @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x806: /* T806 (custom 200MHz) */
		transputer_cpu_freq = 200000000UL;
		printk(KERN_INFO "  CPU: T806 @ 200MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x9000: /* SPINE T9 core (gf180mcu ip/cpu RTL, lddevid stub id) */
		transputer_cpu_freq = 80000000UL; /* GF180 target clock */
		printk(KERN_INFO "  CPU: T9 SPINE @ 80MHz (LDDEVID=0x%x)\n", devid);
		break;
	default:
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: Unknown (LDDEVID=0x%03x), assuming 20MHz\n", devid);
		break;
	}
}

static int transputer_clockevent_set_state(struct clock_event_device *evt)
{
	/* Periodic mode is driven by transputer_poll_tick() from LDTIMER. */
	return 0;
}

static struct clock_event_device transputer_clockevent = {
	.name			= "transputer-timer",
	.features		= CLOCK_EVT_FEAT_PERIODIC,
	.rating			= 200,
	.set_state_shutdown	= transputer_clockevent_set_state,
	.set_state_periodic	= transputer_clockevent_set_state,
	.tick_resume		= transputer_clockevent_set_state,
};

/*
 * Timer interrupt handler
 *
 * Kept for request_irq bookkeeping.  Delivery is cooperative: ISM §8.5
 * has ClockReg + tin, not a vectored host IRQ, and do_IRQ is never
 * entered from RTL.  transputer_poll_tick() drives the clockevent.
 */
static irqreturn_t transputer_timer_interrupt(int irq, void *dev_id)
{
	if (transputer_clockevent.event_handler)
		transputer_clockevent.event_handler(&transputer_clockevent);

	return IRQ_HANDLED;
}

/* Low-pri ClockReg1: 64 µs/tick (ISM §8.5).  Core boots Pri=1. */
#define TICKS_PER_JIFFY	(TRANSPUTER_TIMER_FREQ / HZ)

static unsigned int tick_last;
static int tick_ready;
static int tick_reentry;
static unsigned int tick_fired;

void transputer_poll_tick(void)
{
	unsigned int now, elapsed, n;

	if (!tick_ready || tick_reentry)
		return;
	if (arch_irqs_disabled())
		return;

	now = transputer_ldtimer();
	elapsed = now - tick_last;
	if (elapsed < TICKS_PER_JIFFY)
		return;

	tick_reentry = 1;
	n = 0;
	while (elapsed >= TICKS_PER_JIFFY && n < 16) {
		tick_last += TICKS_PER_JIFFY;
		elapsed -= TICKS_PER_JIFFY;
		tick_fired++;
		if (tick_fired <= 8)
			uart_putc_raw('T');
		/*
		 * Do not call legacy_timer_tick()/do_timer() from this
		 * file: TCC's large-binary reloc bug sends those
		 * cross-unit calls to a low Iptr (seen 0x00056xxx /
		 * 0x000e6xxx after the first 'T').  jiffies is the
		 * same object the rest of the kernel reads.
		 */
		jiffies++;
		n++;
	}
	tick_reentry = 0;
}

static void __init transputer_late_time(void)
{
	unsigned int t0, t1;
	unsigned long j0;
	int spins;

	uart_putc_raw('t');
	uart_putc_raw('L');
	tick_last = transputer_ldtimer();
	tick_ready = 1;
	t0 = transputer_ldtimer();
	j0 = jiffies;
	spins = 0;
	while (jiffies == j0 && spins < 300000) {
		transputer_poll_tick();
		spins++;
	}
	t1 = transputer_ldtimer();
	if (jiffies != j0) {
		printk(KERN_INFO
		       "Transputer: first tick jiffies=%lu ldtimer=%u->%u spins=%d\n",
		       jiffies, t0, t1, spins);
		uart_putc_raw('t');
		uart_putc_raw('1');
	} else {
		printk(KERN_ERR
		       "Transputer: no tick jiffies=%lu ldtimer=%u->%u spins=%d\n",
		       jiffies, t0, t1, spins);
		uart_putc_raw('t');
		uart_putc_raw('0');
	}
}

/*
 * time_init - Initialize timer subsystem
 *
 * Called during kernel boot to set up the system timer.
 */
void __init time_init(void)
{
	int ret;

	printk(KERN_INFO "Transputer: Initializing timer subsystem\n");
	detect_cpu_frequency();
	printk(KERN_INFO "  CPU frequency: %lu Hz\n", transputer_cpu_freq);
	printk(KERN_INFO "  Timer frequency: %lu Hz\n", TRANSPUTER_TIMER_FREQ);
	printk(KERN_INFO "  HZ: %d\n", HZ);

	/* setup_irq() was removed after the 2.6-era port.  Modern architectures
	 * register their timer through the public request_irq() interface. */
	ret = request_irq(TRANSPUTER_IRQ_TIMER, transputer_timer_interrupt,
			  IRQF_TIMER, "transputer-timer", NULL);
	if (ret)
		panic("Transputer: unable to request timer IRQ %d: %d\n",
		      TRANSPUTER_IRQ_TIMER, ret);

	transputer_clockevent.irq = TRANSPUTER_IRQ_TIMER;
	transputer_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&transputer_clockevent, HZ, 1,
					0xffffffffUL);

	tick_last = transputer_ldtimer();
	/* tick_ready stays 0 until late_time_init — do not tick during console_init. */
	{
		unsigned int t0 = tick_last;
		unsigned int t1 = t0;
		int spins = 0;

		while (t1 == t0 && spins < 200000) {
			t1 = transputer_ldtimer();
			spins++;
		}
		if (t1 == t0) {
			printk(KERN_ERR
			       "Transputer: LDTIMER frozen (spins=%d)\n", spins);
			uart_putc_raw('t');
			uart_putc_raw('!');
		} else {
			printk(KERN_INFO
			       "Transputer: LDTIMER %u->%u spins=%d\n",
			       t0, t1, spins);
			uart_putc_raw('t');
			uart_putc_raw('+');
		}
	}

	late_time_init = transputer_late_time;
	printk(KERN_INFO "Transputer: Timer IRQ registered (LDTIMER poll)\n");
}

/*
 * read_persistent_clock - Read the battery-backed RTC
 *
 * Transputer doesn't have an RTC, so we return a fixed epoch time.
 */
void read_persistent_clock64(struct timespec64 *ts)
{
	ts->tv_sec = mktime64(2025, 1, 1, 0, 0, 0);
	ts->tv_nsec = 0;
}

/*
 * sched_clock — high-resolution clock for scheduler and printk timestamps.
 *
 * Reads the T800 low-priority hardware timer (LDTIMER) which ticks every
 * 64μs (15,625 Hz).  The 32-bit counter wraps every ~76.5 hours.
 *
 * We track wraps by comparing each reading to the previous one.  On wrap,
 * we add 2^32 ticks worth of nanoseconds to the epoch.  This is safe as
 * long as sched_clock() is called at least once every ~76 hours (any
 * printk, schedule, or timer tick will do it).
 *
 * Overrides the weak default in kernel/sched_clock.c which returns
 * jiffies-based time (useless when timer interrupts don't fire).
 */

/* Nanoseconds per low-priority timer tick: 64μs = 64000 ns */
#define NS_PER_TICK	64000ULL

/* 2^32 ticks worth of nanoseconds */
#define WRAP_NS		(NS_PER_TICK * (1ULL << 32))

extern unsigned int transputer_ldtimer(void);

unsigned long long sched_clock(void)
{
	static unsigned int last_timer;
	static unsigned long long epoch_ns;
	unsigned int now;

	now = transputer_ldtimer();

	/* Detect forward wrap (counter is unsigned, monotonically increasing) */
	if (now < last_timer)
		epoch_ns += WRAP_NS;
	last_timer = now;

	return epoch_ns + (unsigned long long)now * NS_PER_TICK;
}
