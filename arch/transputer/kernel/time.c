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
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/jiffies.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/timex.h>
#include <asm/irqflags.h>
#include <asm/ptrace.h>
#include <asm/uart.h>
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
#include <linux/threads.h>
extern void tp_smp_poll_ipi(void);
#endif

/* Default CPU frequency (TP32 @ 20MHz) — overridden by LDDEVID detection */
static unsigned long transputer_cpu_freq = 20000000UL;

/* Timer tick frequency for low priority processes (64us per tick) */
#define TRANSPUTER_TIMER_FREQ	15625UL

/* Read product identity via LDDEVID instruction (OPR 0x17C).
 * Returns: TP32I=0x00, TP32-20=0x05, TP32=0x06, TP32-200=0x806.
 * Uses TCC builtin — inline asm corrupts the eval stack. */
extern unsigned int __lddevid(void);

static void __init detect_cpu_frequency(void)
{
	unsigned int devid = __lddevid();

	switch (devid) {
	case 0x000: /* TP32I */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: TP32I @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x005: /* TP32-20 */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: TP32-20 @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x006: /* TP32 */
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: TP32 @ 20MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x806: /* TP32-200 (custom 200MHz) */
		transputer_cpu_freq = 200000000UL;
		printk(KERN_INFO "  CPU: TP32-200 @ 200MHz (LDDEVID=0x%03x)\n", devid);
		break;
	case 0x9000: /* TP32X RTL core (lddevid id) */
		transputer_cpu_freq = 80000000UL; /* GF180 target clock */
		printk(KERN_INFO "  CPU: TP32X @ 80MHz (LDDEVID=0x%x)\n", devid);
		break;
	default:
		transputer_cpu_freq = 20000000UL;
		printk(KERN_INFO "  CPU: Unknown (LDDEVID=0x%03x), assuming 20MHz\n", devid);
		break;
	}
}

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
#define TP_CLOCK_CPU (*(volatile unsigned int *)0x42000004UL)
#define TP_CLOCK_US (*(volatile unsigned int *)0x4200000cUL)
static struct {
	unsigned int deadline;
	int armed;
} tp_clock_control[NR_CPUS];

static u64 tp_shared_clock_read(struct clocksource *cs)
{
	return TP_CLOCK_US;
}
static struct clocksource tp_shared_clocksource = {
	.name = "tp-shared-us",
	.rating = 300,
	.read = tp_shared_clock_read,
	.mask = CLOCKSOURCE_MASK(32),
	/* 1 us = 1000 ns, with fractional bits for timekeeping adjustments. */
	.mult = 1024000,
	.shift = 10,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};
/* Provide a real clock before secondary CPUs and initcalls start. Keeping
 * the boot clock and registered clock identical avoids a jiffies-only
 * startup interval in which idle CPUs cannot enter NO_HZ.
 */
struct clocksource * __init clocksource_default_clock(void)
{
	return &tp_shared_clocksource;
}

static int tp_clockevent_take(unsigned int cpu, unsigned int now)
{
	if (!tp_clock_control[cpu].armed ||
	    (int)(now - tp_clock_control[cpu].deadline) < 0)
		return 0;
	/* Consume before dispatch: the callback may arm a new deadline. */
	tp_clock_control[cpu].armed = 0;
	return 1;
}

static int tp_clockevent_next(unsigned long delta, struct clock_event_device *evt)
{
	unsigned int cpu = cpumask_first(evt->cpumask);
	tp_clock_control[cpu].deadline = TP_CLOCK_US + delta;
	tp_clock_control[cpu].armed = 1;
	return 0;
}
#endif

static int transputer_clockevent_set_state(struct clock_event_device *evt)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	tp_clock_control[cpumask_first(evt->cpumask)].armed = 0;
#endif
	/* Periodic fallback is driven by LDTIMER; one-shot deadlines use the board. */
	return 0;
}

static const struct clock_event_device transputer_clockevent_template = {
	.name			= "transputer-timer",
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = tp_clockevent_next,
	.set_state_oneshot = transputer_clockevent_set_state,
	.set_state_oneshot_stopped = transputer_clockevent_set_state,
#else
	.features		= CLOCK_EVT_FEAT_PERIODIC,
#endif
	.rating			= 200,
	.set_state_shutdown	= transputer_clockevent_set_state,
	.set_state_periodic	= transputer_clockevent_set_state,
	.tick_resume		= transputer_clockevent_set_state,
};

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
static struct clock_event_device tp_cpu_clockevent[NR_CPUS];
#define TP_TIMER_CPU (*(volatile unsigned int *)0x42000004UL)
#define transputer_clockevent tp_cpu_clockevent[TP_TIMER_CPU]
#else
static struct clock_event_device transputer_clockevent;
#define TP_TIMER_CPU 0
#endif

/* The timer has no vectored IRQ on this port. The polled path invokes the
 * registered handler in IRQ context to run the kernel timer queues.
 */
static irqreturn_t transputer_timer_interrupt(int irq, void *dev_id)
{
	if (transputer_clockevent.event_handler)
		transputer_clockevent.event_handler(&transputer_clockevent);

	return IRQ_HANDLED;
}

/* Low-pri ClockReg1: 64 µs/tick (ISM §8.5).  Core boots Pri=1. */
#define TICKS_PER_JIFFY	(TRANSPUTER_TIMER_FREQ / HZ)

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
static struct {
	unsigned int last;
	int ready;
	int reentry;
	unsigned int fired;
} tp_cpu_tick[NR_CPUS];
#define tick_last tp_cpu_tick[TP_TIMER_CPU].last
#define tick_ready tp_cpu_tick[TP_TIMER_CPU].ready
#define tick_reentry tp_cpu_tick[TP_TIMER_CPU].reentry
#define tick_fired tp_cpu_tick[TP_TIMER_CPU].fired
#else
static unsigned int tick_last;
static int tick_ready;
static int tick_reentry;
static unsigned int tick_fired;
#endif

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
/* Diagnostic counters read out-of-band; no printing in the timer hot path. */
struct tp_tick_statistics {
	unsigned int polls, events, max_late_us, stopped_polls, handler_us;
	unsigned int idle_waits, idle_us;
};
struct tp_tick_statistics tp_tick_stats[NR_CPUS];

void transputer_idle_wait(void)
{
	unsigned int cpu = TP_TIMER_CPU;
	unsigned int timeout = 1, start;

	/* CPU0 polls peripheral input. APs can sleep longer when NO_HZ has
	 * stopped their tick; IPI publication wakes the board wait immediately.
	 * An armed event bounds the requested sleep by its actual deadline.
	 */
	if (cpu) {
		timeout = 50000;
		if (tp_clock_control[cpu].armed) {
			int remaining = (int)(tp_clock_control[cpu].deadline - TP_CLOCK_US);
			if (remaining < 2)
				return;
			if ((unsigned int)remaining < timeout)
				timeout = remaining;
		}
	}
	start = TP_CLOCK_US;
	tp_tick_stats[cpu].idle_waits++;
	*(volatile unsigned int *)0x42000010UL = timeout;
	tp_tick_stats[cpu].idle_us += TP_CLOCK_US - start;
}

#endif

void transputer_poll_tick(void)
{
	struct pt_regs regs = { };
	struct pt_regs *saved_regs;
	unsigned long flags;
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	unsigned int cpu = TP_TIMER_CPU, started, now;
	struct tp_tick_statistics *stats = &tp_tick_stats[cpu];
#else
	unsigned int now, elapsed, n;
#endif

	if (tick_reentry || arch_irqs_disabled())
		return;
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	/* IPI progress must not depend on the local clockevent being ready. */
	tp_smp_poll_ipi();
#endif
	if (!tick_ready)
		return;

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	/* Shutdown, detached and oneshot-stopped devices must stay inactive. */
	stats->polls++;
	if (!clockevent_state_oneshot(&transputer_clockevent)) {
		stats->stopped_polls++;
		return;
	}
	now = TP_CLOCK_US;
	if (!tp_clockevent_take(cpu, now))
		return;
	if (now - tp_clock_control[cpu].deadline > stats->max_late_us)
		stats->max_late_us = now - tp_clock_control[cpu].deadline;
	tick_reentry = 1;
	started = TP_CLOCK_US;
	local_irq_save(flags);
	saved_regs = set_irq_regs(&regs);
	irq_enter();
	transputer_timer_interrupt(TRANSPUTER_IRQ_TIMER, NULL);
	irq_exit();
	set_irq_regs(saved_regs);
	local_irq_restore(flags);
	stats->events++;
	stats->handler_us += TP_CLOCK_US - started;
	tick_reentry = 0;
#else
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
		local_irq_save(flags);
		saved_regs = set_irq_regs(&regs);
		irq_enter();
		transputer_timer_interrupt(TRANSPUTER_IRQ_TIMER, NULL);
		irq_exit();
		set_irq_regs(saved_regs);
		local_irq_restore(flags);
		n++;
	}
	tick_reentry = 0;
#endif
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

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
static void __init tp_clockevent_selftest(void)
{
	struct clock_event_device test = { .cpumask = cpumask_of(0) };

	tp_clock_control[0].deadline = 0x10;
	tp_clock_control[0].armed = 1;
	BUG_ON(tp_clockevent_take(0, 0xfffffff0));
	BUG_ON(tp_clockevent_take(0, 0x0f));
	BUG_ON(!tp_clockevent_take(0, 0x10));
	BUG_ON(tp_clockevent_take(0, 0x11));
	tp_clock_control[0].deadline = 0xfffffff0;
	tp_clock_control[0].armed = 1;
	BUG_ON(!tp_clockevent_take(0, 0x10));
	tp_clockevent_next(1000, &test);
	BUG_ON(!tp_clock_control[0].armed);
	BUG_ON(!tp_clockevent_take(0, tp_clock_control[0].deadline));
	/* A callback rearm must survive consumption of the previous event. */
	tp_clockevent_next(1000, &test);
	BUG_ON(!tp_clock_control[0].armed);
	transputer_clockevent_set_state(&test);
	BUG_ON(tp_clockevent_take(0, tp_clock_control[0].deadline));
#if NR_CPUS > 1
	/* The callback must target its supplied device, not the executing CPU. */
	test.cpumask = cpumask_of(1);
	tp_clockevent_next(1000, &test);
	BUG_ON(tp_clock_control[0].armed || !tp_clock_control[1].armed);
	transputer_clockevent_set_state(&test);
	BUG_ON(tp_clock_control[1].armed);
#endif
	pr_info("Transputer timer deadline selftest: PASS\n");
}
#endif

/* Register the boot clock and this CPU's event device. */
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

	transputer_clockevent = transputer_clockevent_template;
	transputer_clockevent.irq = TRANSPUTER_IRQ_TIMER;
	transputer_clockevent.cpumask = cpumask_of(0);
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	tp_clockevent_selftest();
#endif
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	ret = __clocksource_register(&tp_shared_clocksource);
	if (ret)
		panic("Transputer: shared clocksource registration failed: %d", ret);
#endif
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	clockevents_config_and_register(&transputer_clockevent, 1000000, 1000, 0x3fffffffUL);
#else
	clockevents_config_and_register(&transputer_clockevent, HZ, 1, 0xffffffffUL);
#endif

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

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
/* Called by each AP after notify_cpu_starting(), with local IRQs masked.
 * IRQ descriptor setup and wall-clock initialization remain CPU0-only.
 */
void transputer_secondary_time_init(void)
{
	transputer_clockevent = transputer_clockevent_template;
	transputer_clockevent.irq = TRANSPUTER_IRQ_TIMER;
	transputer_clockevent.cpumask = cpumask_of(TP_TIMER_CPU);
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	clockevents_config_and_register(&transputer_clockevent, 1000000, 1000, 0x3fffffffUL);
#else
	clockevents_config_and_register(&transputer_clockevent, HZ, 1, 0xffffffffUL);
#endif
	tick_last = transputer_ldtimer();
	tick_ready = 1;
}
#endif

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
 * Reads the TP32 low-priority hardware timer (LDTIMER) which ticks every
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
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	/* The controller clock has one host epoch for all CPUs.  Local wrap
	 * extension avoids racing or taking locks from scheduler/printk paths.
	 * Every online CPU polls well within the 32-bit microsecond wrap period.
	 */
	static unsigned int last_us[NR_CPUS];
	static unsigned long long epoch_us[NR_CPUS];
	unsigned int cpu = TP_TIMER_CPU;
	unsigned int now;
	unsigned long flags = arch_local_irq_save();
	unsigned long long result;

	now = *(volatile unsigned int *)0x4200000cUL;
	if (now < last_us[cpu])
		epoch_us[cpu] += 1ULL << 32;
	last_us[cpu] = now;
	result = (epoch_us[cpu] + now) * 1000ULL;
	arch_local_irq_restore(flags);
	return result;
#else
	static unsigned int last_timer;
	static unsigned long long epoch_ns;
	unsigned int now;

	now = transputer_ldtimer();

	/* Detect forward wrap (counter is unsigned, monotonically increasing) */
	if (now < last_timer)
		epoch_ns += WRAP_NS;
	last_timer = now;

	return epoch_ns + (unsigned long long)now * NS_PER_TICK;
#endif
}
