#ifndef SYS_VM_H_
#define SYS_VM_H_

#include <stdint.h>

#define VADDR_MAX (vaddr_t) UINT64_MAX

#define ROUNDUP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))

#define PTRROUNDUP(addr) ROUNDUP(addr, (sizeof(uintptr_t)))
/** Round a value up to pointer alignment. */
#define PTRROUNDDOWN(addr) ROUNDDOWN(addr, (sizeof(uintptr_t)))

#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

/** virtual/physical address */
typedef void *vaddr_t, *paddr_t;
/** virtual/physical address offset */
typedef uintptr_t voff_t, poff_t;
/** physical pageframe number (multiply by PGSIZE to get physical address) */
typedef uintptr_t ppg_t;
/** page offset (multiply by PGSIZE to get bytes offset */
typedef uintptr_t pgoff_t;

#endif /* SYS_VM_H_ */
