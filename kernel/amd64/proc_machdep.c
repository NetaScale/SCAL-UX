
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kern/klock.h"
#include "kern/liballoc.h"
#include "kern/process.h"
#include "kern/vm.h"

typedef struct {
	uint16_t length;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access;
	uint8_t flags;
	uint8_t base_high;
	uint32_t base_upper;
	uint32_t reserved;
} __attribute__((packed)) tss_gdt_entry_t;

static struct gdt {
	uint64_t null;
	uint64_t code16;
	uint64_t data16;
	uint64_t code32;
	uint64_t data32;
	uint64_t code64;
	uint64_t data64;
	uint64_t code64_user;
	uint64_t data64_user;
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

static spinlock_t gdt_lock;

void
load_gdt()
{
	struct {
		uint16_t limit;
		vaddr_t addr;
	} __attribute__((packed)) gdtr = { sizeof(gdt) - 1, (vaddr_t)&gdt };

	asm volatile("lgdt %0" : : "m"(gdtr));
}

void
setup_cpu_gdt(cpu_t *cpu)
{
	lock(&gdt_lock);

	/*
	 * TODO: "Avoid placing a page boundary in the part of the TSS that the
	 * processor reads during a task switch (the first 104 bytes)" saith the
	 * Intel x86 and 64 Manual.
	 */
	kprintf("Adr 1 %lu, Addr 2 %lu\n", (uintptr_t)&cpu->tss / PGSIZE,
	    ((uintptr_t)&cpu->tss + PGSIZE) / PGSIZE);
	assert((uintptr_t)&cpu->tss / PGSIZE ==
	    ((uintptr_t)&cpu->tss + PGSIZE) / PGSIZE);
	memset(&cpu->tss, 0x0, sizeof(cpu->tss));

	gdt.tss.length = 0x68;
	gdt.tss.base_low = (uintptr_t)&cpu->tss;
	gdt.tss.base_mid = (uintptr_t)&cpu->tss >> 16;
	gdt.tss.access = 0x89;
	gdt.tss.flags = 0x0;
	gdt.tss.base_high = (uintptr_t)&cpu->tss >> 24;
	gdt.tss.base_upper = (uintptr_t)&cpu->tss >> 32;
	gdt.tss.reserved = 0x0;

	asm volatile("ltr %0" ::"rm"((uint16_t)offsetof(struct gdt, tss)));
	unlock(&gdt_lock);
}