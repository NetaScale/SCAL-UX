#include <sys/param.h>

#include <machine/pcb.h>

#include <amd64/amd64.h>

#include <kern/task.h>
#include <kern/vm.h>
#include <libkern/klib.h>
#include <stdatomic.h>
#include <stdint.h>

#include "amd64/asm_intr.h"
#include "kern/ksrv.h"
#include "machine/intr.h"
#include "machine/spl.h"
#include "posix/proc.h"
#include "posix/sys.h"
#include "sys/queue.h"

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
	kIntNumSyscall = 128,
	/* set below 224, so that we can filter it out with CR8 */
	kIntNumLAPICTimer = 223,
	kIntNumLocalReschedule = 254,
	kIntNumInvlPG = 255,
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
static struct md_intr_entry {
	const char	   *name;
	spl_t		  prio;
	intr_handler_fn_t handler;
	void	     *arg;
} md_intrs[256] = { 0 };

void lapic_eoi();
int  posix_syscall(intr_frame_t *frame);
void callout_interrupt();
void dpcs_run();
int  vm_fault(intr_frame_t *frame, vm_map_t *map, vaddr_t addr,
     vm_fault_flags_t flags);
void swtch(void *ctx);

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

static void intr_page_fault(intr_frame_t *frame, void *arg);
static void intr_lapic_timer(intr_frame_t *frame, void *arg);
static void intr_syscall(intr_frame_t *frame, void *arg);
static void intr_local_resched(intr_frame_t *frame, void *arg);
static void intr_invlpg(intr_frame_t *frame, void *arg);

void
idt_init()
{
#define IDT_SET(VAL) idt_set(VAL, (vaddr_t)&isr_thunk_##VAL, INT, 0);
	NORMAL_INTS(IDT_SET);
#undef IDT_SET

#define IDT_SET(VAL, GATE) idt_set(VAL, (vaddr_t)&isr_thunk_##VAL, GATE, 0);
	SPECIAL_INTS(IDT_SET);
#undef IDT_SET

	idt_load();

	md_intr_register(14, kSPLVM, intr_page_fault, NULL);
	md_intr_register(kIntNumLAPICTimer, kSPLHard, intr_lapic_timer, NULL);
	md_intr_register(kIntNumSyscall, kSPL0, intr_syscall, NULL);
	md_intr_register(kIntNumLocalReschedule, kSPLSched, intr_local_resched,
	    NULL);
	md_intr_register(kIntNumInvlPG, kSPLHigh, intr_invlpg, NULL);
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

/** the handler called by the actual ISRs. */
void
handle_int(intr_frame_t *frame, uintptr_t num)
{
#ifdef DEBUG_INTERRUPT
	kprintf("interrupt %lu\n", num);
#endif

	/* interrupts remain disabled at this point */

	CURTHREAD()->pcb.frame = *frame;

	if (md_intrs[num].handler == NULL) {
		kprintf("unhandled interrupt %lu\n", num);
		md_intr_frame_trace(frame);
		fatal("...");
	}

	md_intrs[num].handler(frame, md_intrs[num].arg);

	if (splget() < kSPLSoft) {
		dpcs_run();
	}

	return;
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

static void
intr_page_fault(intr_frame_t *frame, void *arg)
{
	if (vm_fault(frame, CURTHREAD()->task->map, (vaddr_t)read_cr2(),
		frame->code) < 0)
		fatal("unhandled page fault\n");
}

static void
intr_lapic_timer(intr_frame_t *frame, void *arg)
{
	callout_interrupt();
	lapic_eoi();
}

static void
intr_syscall(intr_frame_t *frame, void *arg)
{

	posix_syscall(frame);
}

static void
intr_local_resched(intr_frame_t *frame, void *arg)
{
	if (CURCPU()->resched.state == kCalloutPending)
		callout_dequeue(&CURCPU()->resched);
	dpc_enqueue(&CURCPU()->resched.dpc);
	lapic_eoi(); /* may have been an IPI; TODO seperate vec for ipi
			resched... */
}

static void
intr_invlpg(intr_frame_t *frame, void *arg)
{
	extern vaddr_t	    invlpg_addr;
	extern volatile int invlpg_done_cnt;
	void		    invlpg(vaddr_t vaddr);
	invlpg(invlpg_addr);
	atomic_fetch_add(&invlpg_done_cnt, 1);
	lapic_eoi();
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

void
arch_ipi_invlpg(cpu_t *cpu)
{
	lapic_write(kLAPICRegICR1, (uint32_t)cpu->arch_cpu.lapic_id << 24);
	lapic_write(kLAPICRegICR0, kIntNumInvlPG); /* NMI */
}

void
md_eoi()
{
	return lapic_eoi();
}

int
md_intr_alloc(spl_t prio, intr_handler_fn_t handler, void *arg)
{
	uint8_t vec = 0;

	for (int i = MAX(prio << 4, 32); i < 256; i++)
		if (md_intrs[i].handler == NULL) {
			vec = i;
			break;
		}

	if (vec == 0) {
		kprintf("md_intr_alloc: out of vectors for priority %lu\n",
		    prio);
		return -1;
	}

	md_intr_register(vec, prio, handler, arg);
	return vec;
}

void
md_intr_register(int vec, spl_t prio, intr_handler_fn_t handler, void *arg)
{
	md_intrs[vec].prio = prio;
	md_intrs[vec].handler = handler;
	md_intrs[vec].arg = arg;
}

void
md_intr_frame_trace(intr_frame_t *frame)
{
	struct frame {
		struct frame *rbp;
		uint64_t      rip;
	} *aframe = (struct frame *)frame->rbp;
	const char *name;
	size_t	    offs;

	ksrv_backtrace((vaddr_t)frame->rip, &name, &offs);
	kprintf(" - %p %s+%lu\n", (void *)frame->rip, name, offs);

	if (aframe != NULL)
		do {
			ksrv_backtrace((vaddr_t)aframe->rip, &name, &offs);
			kprintf(" - %p %s+%lu\n", (void *)aframe->rip, name,
			    offs);
		} while ((aframe = aframe->rbp) && aframe->rip != 0x0);
}

/**
 * Complete a task which has no longer any threads.
 * (this should probably be for proc, not task, and definitely not in intr.c.)
 */
static void
task_complete(task_t *task)
{
	task->pxproc->status = kProcCompleted;
	vm_map_release(task->map);
	task->map = NULL;
#if 0
	task_t *parent =  task->parent;
	
	task->pxproc->status = kProcCompleted;

	lock(&parent->lock);
	waitq_wake_one(&parent->pxproc->waitwq, 0);
	unlock(&parent->lock);
#endif
}

void
arch_resched(void *arg)
{
	cpu_t    *cpu;
	thread_t *curthr, *next;
	spl_t	  spl = splsched();

	lock(&sched_lock);
	cpu = (cpu_t *)arg;
	curthr = cpu->curthread;

	lock(&curthr->lock);

	if (curthr->state == kWaiting) {
		if (curthr->wqtimeout.nanosecs != 0)
			callout_enqueue(&curthr->wqtimeout);
	} else if (curthr->state == kExiting) {
		task_t *task = curthr->task;
		kprintf("May exit thread %p!\n", curthr);
		vm_deallocate(task->map, curthr->stack - USER_STACK_SIZE,
		    USER_STACK_SIZE);
		LIST_REMOVE(curthr, threads);
		if (LIST_EMPTY(&task->threads))
			task_complete(task);
		goto next;
	} else if (curthr != cpu->idlethread) {
		TAILQ_INSERT_TAIL(&CURCPU()->runqueue, curthr, runqueue);
	}

	unlock(&curthr->lock);

next:
	next = TAILQ_FIRST(&cpu->runqueue);
	if (next)
		TAILQ_REMOVE(&cpu->runqueue, next, runqueue);

#ifdef DEBUG_SCHED
	kprintf("curthr %p idle %p next %p\n", curthr, cpu->idlethread, next);
#endif

	if (!next && curthr != cpu->idlethread)
		next = cpu->idlethread;
	else if (!next || next == curthr) {
		goto cont;
	}
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
	vm_activate(next->task->map);
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
