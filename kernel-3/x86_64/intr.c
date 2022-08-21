/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/param.h>

#include <kern/sync.h>
#include <kern/task.h>
#include <kern/types.h>
#include <libkern/klib.h>
#include <machine/intr.h>
#include <x86_64/asmintr.h>
#include <x86_64/cpu.h>

typedef struct {
	uint16_t isr_low;
	uint16_t selector;
	uint8_t	 ist;
	uint8_t	 type;
	uint16_t isr_mid;
	uint32_t isr_high;
	uint32_t zero;
} __attribute__((packed)) idt_entry_t;

enum {
	kIntNumSyscall = 128,
	/* set below 224, so that we can filter it out with CR8 */
	kIntNumLAPICTimer = 223,
	kIntNumSwitch = 240,	 /* Manually invoked with INT */
	kIntNumInvlPG = 241,	 /* IPI */
	kIntNumReschedule = 242, /* IPI */
};

enum {
	kLAPICRegEOI = 0xb0,
	kLAPICRegSpurious = 0xf0,
	kLAPICRegICR0 = 0x300,
	kLAPICRegICR1 = 0x310,
	kLAPICRegTimer = 0x320,
	kLAPICRegTimerInitial = 0x380,
	kLAPICRegTimerCurrentCount = 0x390,
	kLAPICRegTimerDivider = 0x3e0,
};

enum {
	kLAPICTimerPeriodic = 0x4e20,
};

static idt_entry_t idt[256] = { 0 };
static struct md_intr_entry {
	const char	   *name;
	ipl_t		    prio;
	intr_handler_fn_t   handler;
	void		*arg;
} md_intrs[256] = { 0 };

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

void
idt_load()
{
	struct {
		uint16_t limit;
		vaddr_t	 addr;
	} __attribute__((packed)) idtr = { sizeof(idt) - 1, (vaddr_t)idt };

	asm volatile("lidt %0" : : "m"(idtr));
}

static void
pagefault_interrupt(md_intr_frame_t *frame, void *unused)
{
	int r;

	(void)unused;

	r = vm_fault(frame, curtask()->map, (vaddr_t)read_cr2(), frame->code);
	if (r < 0) {
		md_intr_frame_trace(frame);
		fatal("unhandled page fault at RIP 0x%lx\n", frame->rip);
	}
}

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
	md_intr_register(14, kSPL0, pagefault_interrupt, NULL);
	md_intr_register(kIntNumLAPICTimer, kSPL0, callout_interrupt, NULL);
	md_intr_register(kIntNumReschedule, kSPL0, sched_timeslice, NULL);
}

void lapic_eoi(void);

/** the handler called by the actual ISRs. */
#pragma GCC push_options
#pragma GCC optimize("O0")
void
handle_int(md_intr_frame_t *frame, uintptr_t num)
{
	if (num == 240) {
		/* here the context switch actually happens */
		thread_t *old = curcpu()->md.old, *next = curcpu()->curthread;
		extern spinlock_t sched_lock;

		old->md.frame = *frame;
		old->md.fs = rdmsr(kAMD64MSRFSBase);

		*frame = next->md.frame;
		wrmsr(kAMD64MSRFSBase, next->md.fs);

		spinlock_unlock(&sched_lock);
		return;
	} else if (num == kIntNumInvlPG) {
		extern vaddr_t		   invlpg_addr;
		extern volatile atomic_int invlpg_done_cnt;
		pmap_invlpg(invlpg_addr);
		atomic_fetch_add(&invlpg_done_cnt, 1);
		lapic_eoi();
		return;
	}

	if (md_intrs[num].handler == NULL) {
		fatal("unhandled interrupt %lu\n", num);
	}

	md_intrs[num].handler(frame, md_intrs[num].arg);
	if (num > 32)
		lapic_eoi();

	assert(curthread()->state == kThreadRunning);
	if (curcpu()->preempted) {
		curcpu()->preempted = false;
		sched_reschedule();
	}

	return;
}
#pragma GCC pop_options

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
	static spinlock_t calib = SPINLOCK_INITIALISER;

	spinlock_lock(&calib);

	lapic_write(kLAPICRegTimerDivider, 0x2); /* divide by 8*/
	lapic_write(kLAPICRegTimer, kIntNumLAPICTimer);

	pit_init_oneshot(hz);
	lapic_write(kLAPICRegTimerInitial, initial);

	pit_await_oneshot();
	apic_after = lapic_read(kLAPICRegTimerCurrentCount);
	// lapic_write(kLAPICRegTimer, 0x10000); /* disable*/

	spinlock_unlock(&calib);

	return (initial - apic_after) * hz;
}

int
md_intr_alloc(ipl_t prio, intr_handler_fn_t handler, void *arg)
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
md_intr_register(int vec, ipl_t prio, intr_handler_fn_t handler, void *arg)
{
	md_intrs[vec].prio = prio;
	md_intrs[vec].handler = handler;
	md_intrs[vec].arg = arg;
}

void
md_intr_frame_trace(md_intr_frame_t *frame)
{
	struct frame {
		struct frame *rbp;
		uint64_t      rip;
	} *aframe = (struct frame *)frame->rbp;
	const char *name = NULL;
	size_t	    offs = 0;

	//ksrv_backtrace((vaddr_t)frame->rip, &name, &offs);
	kprintf("Begin stack trace:\n");
	kprintf(" - %p %s+%lu\n", (void *)frame->rip, name ? name : "???", offs);


	if (aframe != NULL)
		do {
			//ksrv_backtrace((vaddr_t)aframe->rip, &name, &offs);
			kprintf(" - %p %s+%lu\n", (void *)aframe->rip, name ? name : "???",
			    offs);
		} while ((aframe = aframe->rbp) /*&& (uint64_t)aframe >= KERN_BASE &&
		    aframe->rip != 0x0*/);
}

static void send_ipi(uint32_t lapic_id, uint8_t intr)
{
	lapic_write(kLAPICRegICR1, lapic_id << 24);
	lapic_write(kLAPICRegICR0, intr);
}

void
md_ipi_invlpg(cpu_t *cpu)
{
	send_ipi(cpu->md.lapic_id, kIntNumInvlPG);
}

void
md_ipi_resched(cpu_t *cpu)
{
	send_ipi(cpu->md.lapic_id, kIntNumReschedule);
}

void
md_timer_set(uint64_t nanos)
{
	uint64_t ticks = curcpu()->md.lapic_tps * nanos / NS_PER_S;
	assert(ticks < UINT32_MAX);
	lapic_write(kLAPICRegTimerInitial, ticks);
}

uint64_t
md_timer_get_remaining()
{
	return lapic_read(kLAPICRegTimerCurrentCount) /
	    (curcpu()->md.lapic_tps * NS_PER_S);
}
