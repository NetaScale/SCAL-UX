#include <machine/pcb.h>

#include <amd64/amd64.h>

#include <kern/task.h>
#include <kern/vm.h>
#include <libkern/klib.h>
#include <stdint.h>

#include "machine/spl.h"

enum {
	kLAPICRegEOI = 0xb0,
	kLAPICRegSpurious = 0xf0,
	kLAPICRegICR0 = 0x300,
	kLAPICRegICR1 = 0x310,
	kLAPICRegTimer = 0x320,
	kLAPICRegTimerInitial = 0x380,
	kLAPICRegTimerCurrentCount = 0x390,
	kLAPICRegTimerDivider = 0x3E0,
};

enum {
	kLAPICTimerPeriodic = 0x20000,
};

enum {
	/* set below 224, so that we can filter it out with CR8 */
	kIntNumLAPICTimer = 223,
	kIntNumLocalReschedule = 254,
};

typedef struct {
	uint16_t isr_low;
	uint16_t selector;
	uint8_t	 ist;
	uint8_t	 type;
	uint16_t isr_mid;
	uint32_t isr_high;
	uint32_t zero;
} __attribute__((packed)) idt_entry_t;

static idt_entry_t idt[256] = { 0 };

static void
idt_set(uint8_t index, vaddr_t isr, uint8_t type, uint8_t ist)
{
	idt[index].isr_low = (uint64_t)isr & 0xFFFF;
	idt[index].isr_mid = ((uint64_t)isr >> 16) & 0xFFFF;
	idt[index].isr_high = (uint64_t)isr >> 32;
	idt[index].selector = 0x28; /* sixth */
	idt[index].type = type;
	idt[index].ist = ist;
	idt[index].zero = 0x0;
}

void lapic_eoi();

#define INT 0x8e
/*
 * using interrupt gates for everything now, because sometimes something strange
 * was happening with swapgs - probably interrupts nesting during the window of
 * time in which we're running at CPL 0 but have yet to swapgs, making the
 * nested interrupt fail to do so.
 * so now we explicitly enable interrupts ourselves when it is safe to do so.
 */
#define TRAP 0x8e
#define INT_USER 0xee
#define INTS(X)          \
	X(4, TRAP)       \
	X(6, TRAP)       \
	X(8, TRAP)       \
	X(10, TRAP)      \
	X(11, TRAP)      \
	X(12, TRAP)      \
	X(13, TRAP)      \
	X(14, TRAP)      \
	X(32, INT)       \
	X(33, INT)       \
	X(128, INT_USER) \
	X(223, INT)      \
	X(254, INT)

#define EXTERN_ISR_THUNK(VAL, GATE) extern void *isr_thunk_##VAL;

INTS(EXTERN_ISR_THUNK)

void
idt_init()
{
#define IDT_SET(VAL, GATE) idt_set(VAL, (vaddr_t)&isr_thunk_##VAL, GATE, 0);
	INTS(IDT_SET);
	idt_load();
}

static void
trace(intr_frame_t *frame)
{
	struct frame {
		struct frame *rbp;
		uint64_t      rip;
	} *aframe = (struct frame *)frame->rbp;

	kprintf(" - RIP %p\n", (void *)frame->rip);

	if (aframe != NULL)
		do
			kprintf(" - RIP %p\n", (void *)aframe->rip);
		while ((aframe = aframe->rbp) && aframe->rip != 0x0);
}

void callout_interrupt();
void dpcs_run();

void
handle_int(intr_frame_t *frame, uintptr_t num)
{
#ifdef DEBUG_INTERRUPT
	kprintf("interrupt %lu\n", num);
#endif

	/* interrupts remain disabled at this point */

	switch (num) {
	case kIntNumLAPICTimer:
		callout_interrupt();
		lapic_eoi();
		break;

	case kIntNumLocalReschedule: {
		if (CURCPU()->resched.state == kCalloutPending)
			callout_dequeue(&CURCPU()->resched);
		dpc_enqueue(&CURCPU()->resched.dpc);
		lapic_eoi(); /* may have been an IPI; TODO seperate vec for ipi
				resched... */
		break;
	}

	default:
		goto unhandled;
	}

	if (splget() < kSPLSoft) {
		CURCPU()->curthread->pcb.frame = *frame;
		dpcs_run();
	}

	return;

unhandled:
	kprintf("unhandled interrupt %lu\n", num);
	kprintf("cr2: 0x%lx\n", read_cr2());
	trace(frame);

	for (;;) {
		asm("hlt");
	}
}

void
idt_load()
{
	struct {
		uint16_t limit;
		vaddr_t	 addr;
	} __attribute__((packed)) idtr = { sizeof(idt) - 1, (vaddr_t)idt };

	asm volatile("lidt %0" : : "m"(idtr));
}

static uint32_t
lapic_read(uint32_t reg)
{
	return *(uint32_t *)P2V((rdmsr(kAMD64MSRAPICBase) & 0xfffff000) + reg);
}

static void
lapic_write(uint32_t reg, uint32_t val)
{
	uint32_t *addr = P2V((rdmsr(kAMD64MSRAPICBase) & 0xfffff000) + reg);
	*addr = val;
}

void
lapic_eoi()
{
	lapic_write(kLAPICRegEOI, 0x0);
}

void
lapic_enable(uint8_t spurvec)
{
	lapic_write(kLAPICRegSpurious,
	    lapic_read(kLAPICRegSpurious) | (1 << 8) | spurvec);
}

/* setup PIC to run oneshot for 1/hz sec */
static void
pit_init_oneshot(uint32_t hz)
{
	int divisor = 1193180 / hz;

	outb(0x43, 0x30 /* lohi */);

	outb(0x40, divisor & 0xFF);
	outb(0x40, divisor >> 8);
}

/* await on completion of a oneshot */
static void
pit_await_oneshot(void)
{
	do {
		/* bits 7, 6 must = 1, 5 = don't latch count, 1 = channel 0 */
		outb(0x43, (1 << 7) | (1 << 6) | (1 << 5) | (1 << 1));
	} while (!(inb(0x40) & (1 << 7))); /* check if set */
}

/* return the number of ticks per second for the lapic timer */
uint32_t
lapic_timer_calibrate()
{
	const uint32_t	  initial = 0xffffffff;
	const uint32_t	  hz = 50;
	uint32_t	  apic_after;
	static spinlock_t calib;

	lock(&calib);

	lapic_write(kLAPICRegTimerDivider, 0x2); /* divide by 8*/
	lapic_write(kLAPICRegTimer, kIntNumLAPICTimer);

	pit_init_oneshot(hz);
	lapic_write(kLAPICRegTimerInitial, initial);

	pit_await_oneshot();
	apic_after = lapic_read(kLAPICRegTimerCurrentCount);
	// lapic_write(kLAPICRegTimer, 0x10000); /* disable*/

	unlock(&calib);

	return (initial - apic_after) * hz;
}

void
arch_yield()
{
	asm("int $254"); /* 254 == kIntNumLocalReschedule */
}

void
arch_ipi_resched(cpu_t *cpu)
{
	lapic_write(kLAPICRegICR1, (uint32_t)cpu->arch_cpu.lapic_id << 24);
	lapic_write(kLAPICRegICR0, kIntNumLocalReschedule);
}

extern void swtch(void *ctx);

void
arch_resched(void *arg)
{
	cpu_t    *cpu;
	thread_t *curthr, *next;
	spl_t	  spl = splsched();

	lock(&sched_lock);
	cpu = (cpu_t *)arg;
	curthr = cpu->curthread;

	if (curthr->state == kWaiting) {
		if (curthr->wqtimeout.nanosecs != 0)
			callout_enqueue(&curthr->wqtimeout);
	} else if (curthr != cpu->idlethread) {
		TAILQ_INSERT_TAIL(&CURCPU()->runqueue, curthr, runqueue);
	}

	next = TAILQ_FIRST(&cpu->runqueue);
	if (next)
		TAILQ_REMOVE(&cpu->runqueue, next, runqueue);

#ifdef DEBUG_SCHED
	kprintf("curthr %p idle %p next %p\n", curthr, cpu->idlethread, next);
#endif

	if (!next && curthr != cpu->idlethread)
		next = cpu->idlethread;
	else if (!next || next == curthr)
		goto cont;
	cpu->curthread = next;
	cpu->arch_cpu.tss->rsp0 = (uint64_t)next->kstack;

	if (!TAILQ_EMPTY(&cpu->runqueue)) {
#ifdef DEBUG_SCHED
		kprintf("eligible for timeslicing\n");
#endif
		cpu->resched.nanosecs = 50 * NS_PER_MS;
		assert(cpu->resched.state != kCalloutPending);
		callout_enqueue(&cpu->resched);
	}

	if (!next->kernel)
		wrmsr(kAMD64MSRFSBase, next->pcb.fs);

	asm("cli");
	unlock(&sched_lock);
	spl0();
	swtch(&next->pcb.frame);
	asm("sti");

cont:
	splx(spl);
}

void
arch_timer_set(uint64_t micros)
{
#ifdef DEBUG_SCHED
	kprintf("ARCH_TIMER_SET %luus\n", micros);
#endif
	uint64_t ticks = CURCPU()->arch_cpu.lapic_tps * micros / 1000000;
	assert(ticks < UINT32_MAX);
	lapic_write(kLAPICRegTimerInitial, ticks);
}

uint64_t
arch_timer_get_remaining()
{
	return lapic_read(kLAPICRegTimerCurrentCount) /
	    (CURCPU()->arch_cpu.lapic_tps / 1000000);
}