#ifndef KASAN_H_
#define KASAN_H_

#include <sys/vm.h>

#include <machine/vm_machdep.h>

#define KASAN_SHADOW_SCALE 8
#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_SCALE_SIZE (1UL << KASAN_SHADOW_SCALE_SHIFT)
#define KASAN_SHADOW_MASK (KASAN_SHADOW_SCALE - 1)

#define KASAN_BASE 0xffff800800000000
#define KASAN_SIZE (KHEAP_SIZE / 8)

static inline vaddr_t
vm_kasan_shadow_addr(vaddr_t addr)
{
	return (vaddr_t)((
	    (((uintptr_t)addr - KHEAP_BASE) >> KASAN_SHADOW_SCALE_SHIFT) +
	    KASAN_BASE));
}

#endif /* KASAN_H_ */
