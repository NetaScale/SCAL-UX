/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef VM_H_
#define VM_H_

#include <machine/vm_machdep.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kern/lock.h"
#include "kern/vmem_impl.h"
#include "sys/queue.h"
#include "sys/tree.h"

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
/** unique identifier for a paged-out page */
typedef uintptr_t drumslot_t;

enum { kDrumSlotInvalid = -1 };

typedef enum vm_prot {
	kVMRead = 0x1,
	kVMWrite = 0x2,
	kVMExecute = 0x4,
	kVMAll = kVMRead | kVMWrite | kVMExecute,
} vm_prot_t;

/**
 * Represents a physical page of useable general-purpose memory.
 *
 * If neither anon nor obj are set, it may be internally managed (kmem).
 * In the case of tmpfs, both may be set.
 */
typedef struct vm_page {
	/** Links to freelist, wired, inactive, or active queue. */
	TAILQ_ENTRY(vm_page) queue;
	/** Links to vm_object_t::pgtree */
	SPLAY_ENTRY(vm_page) pgtree;

	_Atomic bool lock;
	bool free : 1;

	/** for pageable mappings */
	union {
		struct vm_anon   *anon; /** if belonging to an anon */
		struct vm_object *obj;	/** if belonging to non-anon object */
	};

	LIST_HEAD(, pv_entry) pv_table; /* physical page -> virtual mappings */

	paddr_t paddr; /** physical address of page */
} vm_page_t;

/*
 * Physical region description.
 */
typedef struct vm_pregion {
	TAILQ_ENTRY(vm_pregion) queue;
	paddr_t			paddr;
	size_t			npages;
	vm_page_t		pages[0];
} vm_pregion_t;

/**
 * Represents a logical page of pageable memory. May be resident or not.
 */
typedef struct vm_anon {
	TAILQ_HEAD(, vm_map_entry) entries;

	spinlock_t lock;
	int refcnt : 24, /** number of amaps referencing it; if >1, must COW. */
	    resident : 1; /** whether currently resident in memory */

	size_t offs; /** offset in bytes into amap */
	union {
		vm_page_t *physpage; /** physical page if resident */
		drumslot_t drumslot;
	};
} vm_anon_t;

/* Entry in a vm_amap_t. Locked by the vm_amap's lock */
typedef struct vm_amap_entry {
	TAILQ_ENTRY(vm_amap_entry) entries; /** vm_anon::entries */
	vm_anon_t		  *anon;
} vm_amap_entry_t;

/**
 * An anonymous map - map of anons. These are always paged by the default pager
 * (vm_compressor).
 */
typedef struct vm_amap {
	TAILQ_HEAD(, vm_amap_entry) anons;
} vm_amap_t;

TAILQ_HEAD(vm_page_queue, vm_page);
TAILQ_HEAD(vm_pregion_queue, vm_pregion);

/**
 * Represents a pager-backed virtual memory object.
 */
typedef struct vm_object {
	int	   refcnt;
	spinlock_t lock;

	enum {
		kDirectMap,
		kKHeap,
		kVMObjAnon,
	} type;
	size_t size; /**< size in bytes */

	union {
		struct {
			paddr_t base;
		} dmap;
		struct {
			struct vm_page_queue pgs;
		} kheap;
		struct {
			vm_amap_t	  *amap;
			struct vm_object *parent;
		} anon;
	};
} vm_object_t;

/**
 * Represents an entry within a vm_map_t.
 */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) queue;
	vaddr_t			  start, end;
	vm_object_t		    *obj;
} vm_map_entry_t;

/**
 * Virtual address space map - either the global kernel map, or a user
 * process-specific map.
 */
typedef struct vm_map {
	TAILQ_HEAD(, vm_map_entry) entries;
	vmem_t	     vmem;
	struct pmap *pmap;
} vm_map_t;

typedef struct pmap pmap_t;

/** The swapper thread loop. */
void swapper(void *unused);

/** Activate a map. */
void vm_activate(vm_map_t *map);

/**
 * Allocate a single page; optionally sleep to wait for one to become available.
 */
vm_page_t *vm_allocpage(bool sleep);

/**
 * Allocate anonymous memory and map it into the given map.
 *
 * @param[in,out] vaddrp pointer to a vaddr specifying where to map at. If the
 * pointed-to value is VADDR_MAX, then first fit is chosen. The address at which
 * the object was mapped, if not explicitly specified, is written out.
 * @param[out] out if non-null, the created vm_object_t is written out.
 * @param size size in bytes to allocate.
 *
 * @returns 0 for success.
 */
int vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size);

/** Allocate a new anonymous VM object of size \p size bytes. */
vm_object_t *vm_aobj_new(size_t size);

/**
 * Map a VM object into an address space either at a given virtual address, or
 * (if \p vaddr points to vaddr of VADDR_MAX) pick a suitable place to put it.
 *
 * @param size is the size of area to be mapped in bytes - it must be a multiple
 * of the PAGESIZE.
 * @param[in,out] vaddrp points to a vaddr_t describing the preferred address.
 * If VADDR_MAX, then anywhere is fine. The result is written to its referent.
 * @param copy whether to copy \p obj instead of mapping it shared
 * (copy-on-write optimisation may be employed.)
 */
int vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy);

/**
 * @name Arch-specific
 * @{
 */

void arch_vm_init(paddr_t kphys);

/**
 * Map a single page at the given virtual address - for non-pageable memory.
 */
void pmap_enter(vm_map_t *map, vm_page_t *page, vaddr_t virt, vm_prot_t prot);

/**
 * Map a single page at the given virtual address. This is for use for pageable
 * memory (the page is set up for tracking by pagedaemon.)
 */
void pmap_enter_kern(pmap_t *pmap, paddr_t phys, vaddr_t virt, vm_prot_t prot);

/**
 * Unmap a single page of a pageable mapping. CPU local TLB invalidated; not
 * others. TLB shootdown may therefore be required afterwards. Page's pv_table
 * updated accordingly.
 *
 * @param map from which map to remove the mapping.
 * @param page which page to unmap from \p map.
 * @param virt virtual address to be unmapped from \p map.
 * @param pv optionally specify which pv_entry_t represents this mapping, if
 * already known, so it does not have to be found again.
 */
void pmap_unenter(vm_map_t *map, vm_page_t *page, vaddr_t virt, pv_entry_t *pv);

/** @} */

void vm_kernel_init();

/**
 * Allocate pages of kernel heap.
 *
 * @param wait whether it is permitted to wait for pages to become available; if
 * bit 0 is set, then waiting is permitted. If bit 1 is set, then the VMem
 * allocation is done with kVMBootstrap passed.
 */
vaddr_t vm_kalloc(size_t npages, int wait);

/**
 * Free pages of kernel heap.
 */
void vm_kfree(vaddr_t addr, size_t pages);

extern struct vm_page_queue    pg_freeq, pg_activeq, pg_inactiveq;
extern struct vm_pregion_queue vm_pregion_queue;
extern vm_map_t		       kmap; /** global kernel map */

#endif /* VM_H_ */
