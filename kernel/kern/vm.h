#ifndef VM_H_
#define VM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kern/kern.h"
#include "kern/queue.h"

#define ROUNDUP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))

#define PTRROUNDUP(addr) ROUNDUP(addr, (sizeof(uintptr_t)))
/** Round a value up to pointer alignment. */
#define PTRROUNDDOWN(addr) ROUNDDOWN(addr, (sizeof(uintptr_t)))

#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

#define PGSIZE 4096
#define HHDM_BASE 0xffff800000000000
#define KERN_BASE 0xffffffff80000000

#define P2V(addr) (((void *)addr) + HHDM_BASE)
#define V2P(addr) (((void *)addr) - HHDM_BASE)

#define VADDR_MAX ((vaddr_t)UINT64_MAX)

/* physical address */
typedef void *paddr_t;
/* virtual address */
typedef void *vaddr_t;
/* virtual offset */
typedef uintptr_t voff_t;

typedef struct pmap pmap_t;

typedef enum vm_prot {
	kVMRead = 0x1,
	kVMWrite = 0x2,
	kVMExecute = 0x4,
	kVMAll = kVMRead | kVMWrite | kVMExecute,
} vm_prot_t;

/* Map of a virtual address space */
typedef struct vm_map {
	spinlock_t lock;

	enum vm_map_type {
		kVMMapKernel = 0,
		kVMMapUser,
	} type;

	TAILQ_HEAD(entries, vm_map_entry) entries;
	pmap_t *pmap;
} vm_map_t;

enum vm_map_entry_inheritance {
	kVMMapEntryInheritNone,
	kVMMapEntryInheritShared,
	kVMMapEntryInheritCopy,
};

/* Entry describing some region within a map */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) entries;
	enum vm_map_entry_inheritance inheritance;
	struct vm_object *obj;
	vaddr_t vaddr;
	size_t size; /* length in bytes */
} vm_map_entry_t;

/* Entry in an amap. */
typedef struct vm_anon {
	spinlock_t lock;
	int refcnt;
	int offs; /* offset within the vm_amap */
	union {
		struct vm_page *physpg;
		/* swap descriptor of some sort goes here... */
	};
} vm_anon_t;

/* Entry in a vm_amap_t. Locked by the vm_amap's lock */
typedef struct vm_amap_entry {
	vm_anon_t *anon;
	TAILQ_ENTRY(vm_amap_entry) entries;
} vm_amap_entry_t;

/* Describes the layout of an anonymous or vnode object. */
typedef struct vm_amap {
	spinlock_t lock;
	int refcnt;
	/* an ordered queue of entries */
	TAILQ_HEAD(, vm_amap_entry) pages;
} vm_amap_t;

typedef struct vm_object {
	enum {
		kVMGeneric,   /* contiguous region of memory, kernel-internal */
		kVMAnonymous, /* anonymous/vnode memory, lazily backed */
	} type;
	spinlock_t lock;
	size_t size; /* length in bytes */
	size_t refcnt;
	union {
		struct {
			paddr_t phys; /* physical address of 1st page */
		} gen;		      /* for kVMGeneric */
		struct {
			bool copy; /* whether this is a copy-on-write mapping */
			vm_amap_t *amap;
			/* pagerops for this object */
			struct vm_pagerops *pagerops;
			/* if this object COWs another, the original parent */
			struct vm_object *parent;
			/* if this is a mapping of a vnode, its associated vnode
			 */
			struct vnode *vnode;
		} anon; /* for kVMAnonymous and kVMVNode */
	};
} vm_object_t;

/*
 * Physical page description - all pages of the physical address space available
 * representing useful RAM are described by one.
 *
 * These are each linked into a queue: either the free queue, the vm internal
 * queue, or the queue of the vm_object_t to which it belongs.
 */
typedef struct vm_page {
	enum vm_page_type {
		kPageFree,
		kPageVMInternal, /* internally managed by the VM system */
		kPageObject,	 /* belongs to (exactly one) vm_object_t */
	} type;

	vm_object_t *obj;
	paddr_t paddr;
	TAILQ_ENTRY(vm_page) entries; /* link for queue */
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

typedef struct vm_pagerops {
	/*
	 * Get a page from backing store.
	 * @param 4 Whether the page is needed to fill a write request for a
	 * COW object.
	 */
	int (*get)(vm_object_t *, voff_t, vm_anon_t **, bool /* needcopy? */);
} vm_pagerops_t;

/** Activate a pmap, i.e. load it into CR3 or equivalent immediately. */
void vm_activate(pmap_t *pmap);

/* allocate a physical page */
vm_page_t *vm_alloc_page();

/* allocate a new vm_map */
vm_map_t *vm_map_new();

vm_map_t *vm_map_fork(vm_map_t *map);

vm_amap_entry_t *amap_find_anon(vm_amap_t *amap, vm_anon_t **prevp, voff_t off);
/** copy an anon (does not decrement \p anon's refcnt) */
vm_anon_t *anon_copy(vm_anon_t *anon);

/**
 * Allocate anonymous memory and map it into the given map. All other
 * parameters akin to vm_map_object.
 *
 * @param[in,out,nullable] vaddrp pointer to a vaddr specifying where to map the
 * memory at. If the pointed-to value is VADDR_MAX, then the fit is chosen. The
 * resultant address is written out if vaddpr is not NULL.
 * @param[out] out resultant VM object, set if not NULL.
 */
int vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size,
    bool immediate);

/**
 * Deallocate address space from a given map.
 */
int vm_deallocate(vm_map_t *map, vaddr_t start, size_t size);

/* handle a page fault */
int vm_fault(vm_map_t *map, vaddr_t vaddr, bool write);

/* setup vmm */
void vm_init(paddr_t kphys);

/*
 * Map a VM object into an address space either at a given virtual address, or
 * (if \p vaddr is NULL) pick a suitable place to put it.
 *
 * @param size is the size of area to be mapped in bytes - it must be a multiple
 * of the PAGESIZE.
 * @param[in,out] vaddrp points to a vaddr_t describing the preferred address.
 * If VADDR_MAX, then anywhere is fine. The result is written to its referent.
 * @param copy whether to make this a copy-on-write mapping (irrelevant for
 * kVMGeneric objects).
 */
int vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy);

/**
 * Allocate a new anonymous/vnode object.
 */
int vm_object_new_anon(vm_object_t **out, size_t size, vm_pagerops_t *pagerops,
    struct vnode *vn);

/**
 * Release a reference to an object. Its associated resources may be freed if
 * no references remain.
 */
int vm_object_release(vm_object_t *obj);

/**
 * Make a copy-on-write duplicate of an object.
 *
 * @param obj LOCKED object to duplicate
 */
vm_object_t *vm_object_copy(vm_object_t *obj);

/* get n contiguous pages. returns physical address of first. */
paddr_t pmap_alloc_page(size_t n);

/* Create a new pmap, inheriting the higher half from the kernel. */
pmap_t *pmap_new();

/* map a contiguous region of \p size bytes */
void pmap_map(pmap_t *pmap, paddr_t phys, vaddr_t virt, size_t size,
    vm_prot_t prot);

/* invalidate tlb entry for address */
void pmap_invlpg(vaddr_t addr);

/** translate virt to physical address with respect to \p pmap */
paddr_t pmap_trans(pmap_t *pmap, vaddr_t virt);

/* print pmap statistics */
void pmap_stats();

/* set to 1 when the VM system is up and running */
extern bool vm_up;

extern vm_pregion_t *g_1st_mem;
extern vm_pregion_t *g_last_mem;
extern vm_pagerops_t vm_anon_pagerops;
extern vm_pagerops_t vm_vnode_pagerops;
extern vm_map_t *kmap; /* global kernel map */

#endif /* VM_H_ */
