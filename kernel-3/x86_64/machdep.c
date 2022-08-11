/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/sync.h>
#include <kern/task.h>
#include <kern/vm.h>
#include <x86_64/cpu.h>

typedef struct {
	uint16_t length;
	uint16_t base_low;
	uint8_t	 base_mid;
	uint8_t	 access;
	uint8_t	 flags;
	uint8_t	 base_high;
	uint32_t base_upper;
	uint32_t reserved;
} __attribute__((packed)) tss_gdt_entry_t;

static struct gdt {
	uint64_t	null;
	uint64_t	code16;
	uint64_t	data16;
	uint64_t	code32;
	uint64_t	data32;
	uint64_t	code64;
	uint64_t	data64;
	uint64_t	code64_user;
	uint64_t	data64_user;
	tss_gdt_entry_t tss;
} __attribute__((packed)) gdt = {
	.null = 0x0,
	.code16 = 0x8f9a000000ffff,
	.data16 = 0x8f92000000ffff,
	.code32 = 0xcf9a000000ffff,
	.data32 = 0xcf92000000ffff,
	.code64 = 0xaf9a000000ffff,
	.data64 = 0x8f92000000ffff,
	.code64_user = 0xaffa000000ffff,
	.data64_user = 0x8ff2000000ffff,
	.tss = { .length = 0x68, .access = 0x89 },
};

/*
 * TODO: "Avoid placing a page boundary in the part of the TSS that the
 * processor reads during a task switch (the first 104 bytes)" saith the
 * Intel x86 and 64 Manual. So for now it's a statically-allocated thing.
 */
static tss_t tss[64] __attribute__((aligned(4096)));

void
load_gdt()
{
	struct {
		uint16_t limit;
		vaddr_t	 addr;
	} __attribute__((packed)) gdtr = { sizeof(gdt) - 1, (vaddr_t)&gdt };

	asm volatile("lgdt %0" : : "m"(gdtr));
}

void
setup_cpu_gdt(cpu_t *cpu)
{
	static spinlock_t gdt_lock = SPINLOCK_INITIALISER;

	spinlock_lock(&gdt_lock);

	cpu->md.tss = &tss[cpu->num];

	/* for when we do dynamic allocation of tss' again, sanity check: */
	/* assert((uintptr_t)cpu->tss / PGSIZE ==
	    ((uintptr_t)cpu->tss + 104) / PGSIZE); */

	gdt.tss.length = 0x68;
	gdt.tss.base_low = (uintptr_t)cpu->md.tss;
	gdt.tss.base_mid = (uintptr_t)cpu->md.tss >> 16;
	gdt.tss.access = 0x89;
	gdt.tss.flags = 0x0;
	gdt.tss.base_high = (uintptr_t)cpu->md.tss >> 24;
	gdt.tss.base_upper = (uintptr_t)cpu->md.tss >> 32;
	gdt.tss.reserved = 0x0;
	load_gdt();
	asm volatile("ltr %0" ::"rm"((uint16_t)offsetof(struct gdt, tss)));
	spinlock_unlock(&gdt_lock);
}

void md_switch(struct thread *from, struct thread *to)
{
        curcpu()->md.old = from;
	curcpu()->curthread = to;
	/* the sched lock will be dropped here */
        asm("int $240");
}
