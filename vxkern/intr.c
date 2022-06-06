#include "amd64.h"
#include "intr.h"
#include "vm.h"
#include "vxkern.h"

typedef struct {
	uint16_t isr_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type;
	uint16_t isr_mid;
	uint32_t isr_high;
	uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct intr_frame {
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rbp;
	uint64_t code;

	uintptr_t ip;
	uint64_t cs;
	uint64_t flags;
	uintptr_t sp;
	uint64_t ss;
} __attribute__((packed)) intr_frame_t;

static idt_entry_t idt[256] = { 0 };

enum {
	kLAPICRegEOI = 0xb0,
	kLAPICRegSpurious = 0xf0,
};

static void lapic_write(uint32_t reg, uint32_t val);

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
		vaddr_t addr;
	} __attribute__((packed)) idtr = { sizeof(idt) - 1, (vaddr_t)idt };

	asm volatile("lidt %0" : : "m"(idtr));
	asm("sti");
}

void
handle_int(intr_frame_t *frame, uintptr_t num)
{
	kprintf("int %lu: ip 0x%lx, code 0x%lx,\n", num, frame->ip,
	    frame->code);
	if (num == 14) {
		uint64_t cr2;
		asm("mov %%cr2, %%rax\n"
		    "mov %%rax, %0"
		    : "=m"(cr2)
		    :
		    : "%rax");

		kprintf("cr2 was %p\n", cr2);

		for (;;)
			asm("hlt");
	}
}

extern void *isr_thunk_14;
extern void *isr_thunk_80;

void
idt_init()
{
	idt_set(0xE, (vaddr_t)&isr_thunk_14, 0x8E, 0);
	idt_set(80, (vaddr_t)&isr_thunk_80, 0x8E, 0);
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
lapic_enable(uint8_t spurvec)
{
	lapic_write(kLAPICRegSpurious,
	    lapic_read(kLAPICRegSpurious) | (1 << 8) | spurvec);
}