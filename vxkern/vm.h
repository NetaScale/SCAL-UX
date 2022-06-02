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

#define P2V(addr) (addr + HHDM_BASE)
#define V2P(addr) (addr - HHDM_BASE)

/* physical address */
typedef void *paddr_t;
/* virtual address */
typedef void *vaddr_t;

/* Map of a virtual address space */
typedef struct vm_map {
	TAILQ_HEAD(entries, vm_map_entry) entries;
}vm_map_t;

/* Entry describing some region within a map */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) entries;
	struct vm_object * obj;
	vaddr_t vaddr;
	size_t length; /* length in bytes */
} vm_map_entry_t;

typedef struct vm_object {
	enum {
		kVMGeneric, /* generic region of memory */
	} type;
	union {
		struct vm_obj_generic {
			paddr_t phys; /* page-aligned physical address */
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

/* setup vmm */
void vm_init(paddr_t kphys);
/* get n contiguous pages. returns physical address of first. */
paddr_t pmap_alloc_page(size_t n);

#endif /* VM_H_ */
