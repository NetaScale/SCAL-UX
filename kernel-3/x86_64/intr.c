/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

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

static idt_entry_t idt[256] = { 0 };
static struct md_intr_entry {
	const char	   *name;
	md_intr_handler_fn_t handler;
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
}

/** the handler called by the actual ISRs. */
void
handle_int(md_intr_frame_t *frame, uintptr_t num)
{
	if (num == 240) {
		/* context switch */
		thread_t *old = curcpu()->md.old, *next = curcpu()->curthread;
		extern spinlock_t sched_lock;

		old->md.frame = *frame;
		old->md.fs = rdmsr(kAMD64MSRFSBase);

		*frame = next->md.frame;
		wrmsr(kAMD64MSRFSBase, next->md.fs);

		spinlock_unlock(&sched_lock);
		return;
	}

	kprintf("Interrupt number %zu!\n", num);

	for (;;)
		;

	return;
}
