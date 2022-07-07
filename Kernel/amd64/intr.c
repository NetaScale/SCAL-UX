#include <stdint.h>
#include <kern/vm.h>
#include <machine/pcb.h>
#include <libkern/klib.h>
#include <amd64/amd64.h>

typedef struct {
	uint16_t isr_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type;
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

void
idt_load()
{
	struct {
		uint16_t limit;
		vaddr_t addr;
	} __attribute__((packed)) idtr = { sizeof(idt) - 1, (vaddr_t)idt };

	asm volatile("lidt %0" : : "m"(idtr));
}

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
#define INTS(X)     \
	X(4, TRAP)  \
	X(6, TRAP)  \
	X(8, TRAP)  \
	X(10, TRAP) \
	X(11, TRAP) \
	X(12, TRAP) \
	X(13, TRAP) \
	X(14, TRAP) \
	X(32, INT)  \
	X(33, INT)  \
	X(48, INT)  \
	X(128, INT_USER)

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
		uint64_t rip;
	} *aframe = (struct frame *)frame->rbp;

	kprintf(" - RIP %p\n", (void *)frame->rip);

	if (aframe != NULL)
		do
			kprintf(" - RIP %p\n", (void *)aframe->rip);
		while ((aframe = aframe->rbp) && aframe->rip != 0x0);
}

void
handle_int(intr_frame_t *frame, uintptr_t num)
{
        kprintf("interrupt %lu\n", num);

        kprintf("cr2: 0x%lx\n", read_cr2());

        trace(frame);

        for (;;) { asm ("hlt"); }
}
