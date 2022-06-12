#ifndef AMD64_H_
#define AMD64_H_

#include <stdint.h>

#define REG_FUNCS(type, regname)                                    \
	static inline type read_##regname()                         \
	{                                                           \
		type val;                                           \
		asm volatile("mov %%" #regname ", %0" : "=r"(val)); \
		return val;                                         \
	}                                                           \
	static void write_##regname(type val)                       \
	{                                                           \
		asm volatile("mov %0, %%" #regname ::"a"(val));     \
	}

typedef uint64_t pml4e_t, pdpte_t, pde_t, pte_t;

enum {
	kAMD64MSRAPICBase = 0x1b,
	kAMD64MSRTSCDeadline = 0x6e0,
	kAMD64MSRGSBase = 0xc0000101,
	kAMD64MSRKernelGSBase = 0xc0000102,
	kAMD64MSRFSBase = 0xc0000100
};

static inline void
outb(uint16_t port, uint8_t data)
{
	asm volatile("outb %0, %1" ::"a"(data), "Nd"(port));
}

static inline uint8_t
inb(uint16_t port)
{
	uint8_t data;
	asm volatile("inb %1, %0" : "=a"(data) : "Nd"(port));
	return data;
}

static inline void
wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t high = value >> 32;
	uint32_t low = value;

	asm volatile("wrmsr" ::"c"(msr), "d"(high), "a"(low));
}

static inline uint64_t
rdmsr(uint32_t msr)
{
	uint32_t high, low;
	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
	return ((uint64_t)high << 32) | low;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
REG_FUNCS(uint64_t, cr2);
REG_FUNCS(uint64_t, cr3);
REG_FUNCS(uint64_t, cr4)
#pragma GCC diagnostic pop

#if 0 /* slow */
static inline struct cpu *
curcpu()
{
	return (struct cpu *)rdmsr(kAMD64MSRKernelGSBase);
}
#endif

static inline struct cpu *
curcpu()
{
	struct cpu *val;
	asm volatile("mov %%gs:0, %0" : "=r"(val));
	return val;
}

#endif /* AMD64_H_ */
