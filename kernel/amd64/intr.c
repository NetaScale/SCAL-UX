#include "amd64.h"
#include "intr.h"
#include "kern/kern.h"
#include "kern/process.h"
#include "kern/vm.h"

typedef struct {
	uint16_t isr_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type;
	uint16_t isr_mid;
	uint32_t isr_high;
	uint32_t zero;
} __attribute__((packed)) idt_entry_t;

enum {
	kX86MMUPFPresent = 0x1,
	kX86MMUPFWrite = 0x2,
	kX86MMUPFUser = 0x4,
};

enum {
	kLAPICRegEOI = 0xb0,
	kLAPICRegSpurious = 0xf0,
	kLAPICRegTimer = 0x320,
	kLAPICRegTimerInitial = 0x380,
	kLAPICRegTimerCurrentCount = 0x390,
	kLAPICRegTimerDivider = 0x3E0,
};

enum {
	kLAPICTimerPeriodic = 0x20000,
};

static idt_entry_t idt[256] = { 0 };

static inline uint64_t
rdtsc()
{
	uint32_t high, low;
	asm volatile("rdtsc" : "=a"(low), "=d"(high));
	return ((uint64_t)high << 32) | low;
}

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

void schedule(intr_frame_t *frame);

void
handle_int(intr_frame_t *frame, uintptr_t num)
{
#ifdef DEBUG_INT
	kprintf("int %lu: ip 0x%lx, code 0x%lx,\n", num, frame->rip,
	    frame->code);
#endif
	switch (num) {
	case 14: /* pagefault */
		return vm_fault(kmap, (vaddr_t)read_cr2(),
		    frame->code & kX86MMUPFWrite);

	case 81:
		return schedule(frame);

	default:
		kprintf("unhandled int %lu rip %p\n", num, (void *)frame->rip);
		for (;;) {
			__asm__("hlt");
		}
	}
}

#define INTS(X) X(4) X(6) X(8) X(10) X(11) X(12) X(13) X(14)

#define EXTERN_ISR_THUNK(VAL) extern void *isr_thunk_##VAL;

INTS(EXTERN_ISR_THUNK)
EXTERN_ISR_THUNK(80);
EXTERN_ISR_THUNK(81);

void
idt_init()
{
#define IDT_SET(VAL) idt_set(VAL, (vaddr_t)&isr_thunk_##VAL, 0x8F, 0);
	INTS(IDT_SET);
	idt_set(80, (vaddr_t)&isr_thunk_80, 0x8e, 0);
	idt_set(81, (vaddr_t)&isr_thunk_81, 0x8e, 0);
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
	const uint32_t initial = 0xffffffff;
	const uint32_t hz = 20;
	uint32_t apic_after;
	static spinlock_t calib;

	lock(&calib);

	lapic_write(kLAPICRegTimerDivider, 0x3); /* divider 16 */
	lapic_write(kLAPICRegTimer, 81);

	pit_init_oneshot(hz);
	lapic_write(kLAPICRegTimerInitial, initial);

	pit_await_oneshot();
	apic_after = lapic_read(kLAPICRegTimerCurrentCount);
	lapic_write(kLAPICRegTimer, 0x10000); /* disable*/

	unlock(&calib);

	return (initial - apic_after) * hz;
}

void
timeslicing_start()
{
	lapic_write(kLAPICRegTimer, kLAPICTimerPeriodic | 81);
	lapic_write(kLAPICRegTimerInitial, curcpu()->lapic_tps / 1);
}