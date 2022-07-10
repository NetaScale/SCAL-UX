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

#include <sys/queue.h>

#include <machine/vm_machdep.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
	/** Links to freelist, inactive, or active queue. */
	TAILQ_ENTRY(vm_page) queue;

	bool free : 1;

	struct vm_anon	 *anon; /** if belonging to an anon, its anon */
	struct vm_object *obj;	/** if belonging to an object, its object */

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
	int refcnt; /** number of amaps referencing it; if >1, must COW. */
	vm_page_t *physpage; /** physical page if resident */
} vm_anon_t;

/**
 * A group of 32 vm_anon pointers; amaps operate with these, forming a two-level
 * table, to reduce overhead for sparse amaps.
 */
typedef struct vm_anon_group {
	vm_anon_t *anons[32];
} vm_anon_group_t;

/**
 * An anonymous map - map of anons. These are always paged by the default pager
 * (vm_compressor).
 */
typedef struct vm_amap {
	vm_anon_group_t *agroup;
} vm_amap_t;

TAILQ_HEAD(vm_page_queue, vm_page);
TAILQ_HEAD(vm_pregion_queue, vm_pregion);

/**
 * Represents a pager-backed virtual memory object.
 */
typedef struct vm_object {
	enum {
		kDirectMap,
		kKHeap,
	} type;

	union {
		struct {
			paddr_t base;
		} dmap;
		struct {
			struct vm_page_queue pgs;
		} kheap;
	};
} vm_object_t;

/**
 * Represents an entry within a vm_map_t.
 */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) queue;
	vaddr_t			  start, end;
	union {
		vm_object_t *obj;
		vm_amap_t   *amap;
	};
} vm_map_entry_t;

/**
 * Virtual address space map - either the global kernel map, or a user
 * process-specific map.
 */
typedef struct vm_map {
	TAILQ_HEAD(, vm_map_entry) entries;
	struct pmap *pmap;
} vm_map_t;

typedef struct pmap pmap_t;

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
 * @param[out] out if non-null, the created vm_amap_t is written out.
 * @param size size in bytes to allocate.
 *
 * @returns 0 for success.
 */
vaddr_t vm_allocate(vm_map_t *map, vm_amap_t **out, vaddr_t *vaddrp,
    size_t size);

/**
 * @name Arch-specific
 * @{
 */

void pmap_enter(pmap_t *pmap, paddr_t phys, vaddr_t virt, vm_prot_t prot);
void arch_vm_init(paddr_t kphys);

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
