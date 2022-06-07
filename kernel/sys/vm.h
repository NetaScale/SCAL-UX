#ifndef VM_H_
#define VM_H_

#include <sys/queue.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROUNDUP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr) (((addr)) & ~(align - 1))

#define PTRROUNDUP(addr) ROUNDUP(addr, (sizeof(uintptr_t)))
/** Round a value up to pointer alignment. */
#define PTRROUNDDOWN(addr) ROUNDDOWN(addr, (sizeof(uintptr_t)))

#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

#define PGSIZE 4096
#define HHDM_BASE 0xffff800000000000
#define KERN_BASE 0xffffffff80000000

#define P2V(addr) (((void*)addr) + HHDM_BASE)
#define V2P(addr) (((void*)addr) - HHDM_BASE)

/* physical address */
typedef void *paddr_t;
/* virtual address */
typedef void *vaddr_t;

typedef struct pmap pmap_t;

/* Map of a virtual address space */
typedef struct vm_map {
	TAILQ_HEAD(entries, vm_map_entry) entries;
	enum vm_map_type {
		kVMMapKernel = 0,
		kVMMapUser,
	} type;

	pmap_t *pmap;
} vm_map_t;

/* Entry describing some region within a map */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) entries;
	struct vm_object *obj;
	vaddr_t vaddr;
	size_t size; /* length in bytes */
} vm_map_entry_t;

typedef struct vm_object {
	enum {
		kVMGeneric, /* generic contiguous region of memory */
	} type;
	union {
		struct vm_obj_generic {
			paddr_t phys;  /* physical address of 1st page */
			size_t length; /* length in bytes */
		} gen;
	};
} vm_object_t;

/*
 * Physical page description - all pages of the physical address space available
 * representing useful RAM are described by one.
 */
typedef struct vm_page {
	enum vm_page_type {
		kPageFree,
		kPageVMInternal /* internally managed by the VM system */
	} type;
	uint8_t refcnt;
} vm_page_t;

/*
 * Physical region description.
 */
typedef struct vm_pregion {
	struct vm_pregion *next;
	paddr_t paddr;
	size_t npages;
	vm_page_t pages[0];
} vm_pregion_t;

/* first of the linked list of usable memory region bitmaps */
extern vm_pregion_t *g_1st_mem;
/* last of the linked list of usable memory region bitmaps */
extern vm_pregion_t *g_last_mem;

/* allocate a new vm_map */
vm_map_t *vm_map_new();

/* setup vmm */
void vm_init(paddr_t kphys);

/*
 * Map a VM object into an address space either at a given virtual address, or
 * (if \p vaddr is NULL) pick a suitable place to put it.
 *
 * @arg size is the size of area to be mapped in bytes - it must be a multiple
 * of the PAGESIZE.
 */
int vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t vaddr, size_t size);

extern vm_map_t * kmap; /* global kernel map */

/*
 * @section pmap
 */

/* get n contiguous pages. returns physical address of first. */
paddr_t pmap_alloc_page(size_t n);

/* allocate a new pmap */
pmap_t *pmap_new();

/* map a contiguous region of \p size bytes */
void pmap_map(pmap_t *pmap, paddr_t phys, vaddr_t virt, size_t size);

#endif /* VM_H_ */
