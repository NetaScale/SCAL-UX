#ifndef VMEM_H_
#define VMEM_H_

#include <stddef.h>
#include <stdint.h>

#ifndef _KERNEL
typedef int spl_t;
#else
#include <machine/spl.h>
#endif

typedef uintptr_t   vmem_addr_t;
typedef size_t	    vmem_size_t;
typedef struct vmem vmem_t;

typedef enum vmem_flag {
	kVMemSleep = 0x0,
	kVMemNoSleep = 0x1,
	/** @private */
	kVMemBootstrap = 0x2,
} vmem_flag_t;

// clang-format off
typedef int (*vmem_alloc_t)(vmem_t *vmem, vmem_size_t size,
    vmem_flag_t flags, vmem_addr_t *out);
typedef void (*vmem_free_t)(vmem_t *vmem, vmem_addr_t addr, vmem_size_t size);
// clang-format on

vmem_t *vmem_init(vmem_t *vmem, const char *name, vmem_addr_t base,
    vmem_size_t size, vmem_size_t quantum, vmem_alloc_t allocfn,
    vmem_free_t freefn, vmem_t *source, size_t qcache_max, vmem_flag_t flags,
    spl_t spl);

int vmem_xalloc(vmem_t *vmem, vmem_size_t size, vmem_size_t align,
    vmem_size_t phase, vmem_size_t nocross, vmem_addr_t min, vmem_addr_t max,
    vmem_flag_t flags, vmem_addr_t *out);

int vmem_xfree(vmem_t *vmem, vmem_addr_t addr, vmem_size_t size);

void vmem_dump(const vmem_t *vmem);

#endif /* VMEM_H_ */
