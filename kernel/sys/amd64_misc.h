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
	kAMD64MSRGSBase = 0xc0000101,
	kAMD64MSRKernelGSBase = 0xc0000102,
};

typedef struct cpu {
	uint64_t num;
	uint64_t lapic_id;
} cpu_t;

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

static inline cpu_t *
curcpu()
{
	return (cpu_t *)rdmsr(kAMD64MSRKernelGSBase);
}

#endif /* AMD64_H_ */
