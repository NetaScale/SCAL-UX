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

#include <sys/types.h>
#include <sys/vm.h>

#include <machine/vm_machdep.h>

#include <amd64/kasan.h>

#include <libkern/obj.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kern/lock.h"
#include "kern/vmem_impl.h"
#include "sys/queue.h"
#include "sys/tree.h"

/** unique identifier for a paged-out page */
typedef uintptr_t drumslot_t;

enum { kDrumSlotInvalid = -1 };

typedef enum vm_fault_flags {
	kVMFaultPresent = 1,
	kVMFaultWrite = 2,
	kVMFaultUser = 4,
} vm_fault_flags_t;

typedef enum vm_prot {
	kVMRead = 0x1,
	kVMWrite = 0x2,
	kVMExecute = 0x4,
	kVMAll = kVMRead | kVMWrite | kVMExecute,
} vm_prot_t;

typedef struct vmstat {
	size_t pgs_free, pgs_special, pgs_wired, pgs_active;
	size_t pgs_kmem, pgs_pgtbl;
} vmstat_t;

/**
 * Represents a physical page of useable general-purpose memory.
 *
 * If neither anon nor obj are set, it may be internally managed (kmem).
 * In the case of tmpfs, both may be set.
 */
typedef struct vm_page {
	/** Links to freelist, wired, inactive, or active queue. */
	TAILQ_ENTRY(vm_page) queue;

	spinlock_t lock;
	/** number of wirings */
	uint8_t wirecnt;
	bool	free : 1;

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
#if 0 /* let's see if we can do without this? */
	TAILQ_HEAD(, vm_map_entry) entries;
#endif

	spinlock_t lock;
	int refcnt : 24, /** number of amaps referencing it; if >1, must COW. */
	    resident : 1; /** whether currently resident in memory */

	size_t offs; /** offset in bytes into amap */
	union {
		vm_page_t *physpage; /** physical page if resident */
		drumslot_t drumslot;
	};
} vm_anon_t;

#define kAMapChunkNPages 32

/* Entry in a vm_amap_t. Locked by the vm_amap's lock */
typedef struct vm_amap_chunk {
#if 0
	SPLAY_ENTRY(vm_amap_entry) stree; /** vm_anon::stree */
#endif
	vm_anon_t *anon[kAMapChunkNPages];
} vm_amap_chunk_t;

/**
 * An anonymous map - map of anons. These are always paged by the default pager
 * (vm_compressor).
 */
typedef struct vm_amap {
#if 0
	SPLAY_HEAD(vm_amap_entry_tree, vm_amap_entry) anons;
#endif
	vm_amap_chunk_t **chunks;    /**< sparse array pointers to chunks */
	size_t		  curnchunk; /**< number of slots in chunks */
} vm_amap_t;

#if 0
static inline int
vm_amap_entry_compare(vm_amap_entry_t *a, vm_amap_entry_t *b)
{
	return ((a->anon->offs > b->anon->offs) ? 1 :
		(a->anon->offs < b->anon->offs) ? -1 :
						  0);
}

#define __unused __attribute__((__unused__))

SPLAY_PROTOTYPE(vm_amap_entry_tree, vm_amap_entry, stree,
    vm_amap_entry_compare);
#endif

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
			vm_amap_t *amap;
			/** if not -1, the maximum size of this object */
			ssize_t		  maxsize;
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

/**
 * @name Arch-specific/pmap
 * @{
 */

/** Setup the virtual memory subsystem. */
void arch_vm_init(paddr_t kphys);

/**
 * Map a single page at the given virtual address. This is for use for pageable
 * memory (the page is set up for tracking by pagedaemon.)
 */
void pmap_enter(vm_map_t *map, vm_page_t *page, vaddr_t virt, vm_prot_t prot);

/**
 * Map a single page at the given virtual address - for non-pageable memory.
 */
void pmap_enter_kern(pmap_t *pmap, paddr_t phys, vaddr_t virt, vm_prot_t prot);

/** Free a pmap. */
void pmap_free(pmap_t *pmap);

/** Create a new pmap. */
pmap_t *pmap_new();

/**
 * Reset the protection flags for an existing pageable mapping.
 */
void pmap_reenter(vm_map_t *map, vm_page_t *page, vaddr_t virt, vm_prot_t prot);

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

vm_page_t *pmap_unenter_kern(vm_map_t *map, vaddr_t virt);

/**
 * Check whether a page has been accessed. The access bits are reset.
 */
bool pmap_page_accessed_reset(vm_page_t *page);

/** @} */

/** The swapper thread loop. */
void swapper(void *unused);

/**
 * @name Maps
 * @{
 */

/** Activate a map. */
void vm_activate(vm_map_t *map);

/**
 * Allocate anonymous memory and map it into the given map.
 *
 * @param[in,out] vaddrp pointer to a vaddr specifying where to map at. If the
 * pointed-to value is VADDR_MAX, then first fit is chosen. The address at which
 * the object was mapped, if not explicitly specified, is written out.
 * @param[out] out if non-null, the [non-retained] vm_object_t is written out.
 * @param size size in bytes to allocate.
 *
 * @returns 0 for success.
 */
int vm_allocate(vm_map_t *map, GEN_RETURNS_UNRETAINED vm_object_t **out,
    vaddr_t *vaddrp, size_t size);

/**
 * Deallocate address space from a given map. For now only handles deallocation
 * of whole objects, which are released.
 */
int vm_deallocate(vm_map_t *map, vaddr_t start, size_t size);

/**
 * Fork a map.
 *
 * @returns retained new vm_map_t
 */
vm_map_t *vm_map_fork(vm_map_t *map);

/** Create a new, empty map. */
vm_map_t *vm_map_new();

/**
 * Map a VM object into an address space either at a given virtual address, or
 * (if \p vaddr points to vaddr of VADDR_MAX) pick a suitable place to put it.
 *
 * @param size is the size of area to be mapped in bytes - it must be a multiple
 * of the PAGESIZE.
 * @param obj the object to be mapped. It is retained.
 * @param[in,out] vaddrp points to a vaddr_t describing the preferred address.
 * If VADDR_MAX, then anywhere is fine. The result is written to its referent.
 * @param copy whether to copy \p obj instead of mapping it shared
 * (copy-on-write optimisation may be employed.)
 */
int vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy) LOCK_REQUIRES(obj->lock);

/** Release a reference to a map. The map may be fully freed. */
void vm_map_release(vm_map_t *map);

/** @} */

/**
 * Allocate a single page; optionally sleep to wait for one to become available.
 * @param sleep whether to sleep the thread until a page is available
 * @returns LOCKED page, initially on the wired queue.
 */
vm_page_t *vm_pagealloc(bool sleep);

/**
 * As vm_pagealloc, but zero the page. TODO merge them.
 */
vm_page_t *vm_pagealloc_zero(bool sleep);

/**
 * Free a page. Page must have already been removed from its queue (for now.)
 * Obviously call at SPL VM.
 */
void vm_pagefree(vm_page_t *page);

/**
 * Release a wire reference to a page; the page is placed into the active queue
 * and becomes subject to paging if new wirecnt is 0.
 */
void vm_page_unwire(vm_page_t *page);

/**
 * @name Objects
 * @{
 */

/** Allocate a new anonymous VM object of size \p size bytes. */
vm_object_t *vm_aobj_new(size_t size);

/**
 * Create a (copy-on-write optimised) copy of a @locked VM object.

 * The exact semantics of a copy vary depending on what sort of object is
 * copied:
 * - Copying an anonymous object copies all the pages belonging to that
 * anonymous object (albeit with copy-on-write optimisation)
 * - Copying another type of object yields a new anonymous object with no pages;
 * the new object is assigned the copied object as parent, and when a page is
 * absent from the copied object, its parent is checked to see whether it holds
 * the page. Changes to the parent are therefore reflected in the copied object;
 * unless and until the child object tries to write to one of these pages, which
 * copies it.
 */
vm_object_t *vm_object_copy(vm_object_t *obj);

/** Release a reference to a @locked VM object. */
void vm_object_release(vm_object_t *obj) LOCK_REQUIRES(obj->lock);

/** @} */

/**
 * @name Kernel memory
 * @{
 */

/** Set up the kernel memory subsystem. */
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

/** @} */

/** lock the page queues; must be acquired at SPL VM */
#define VM_PAGE_QUEUES_LOCK() lock(&vm_page_queues_lock)
/** unlock the page queues */
#define VM_PAGE_QUEUES_UNLOCK() unlock(&vm_page_queues_lock)

extern struct vm_page_queue    pg_freeq, pg_activeq, pg_inactiveq, pg_wireq;
extern struct vm_pregion_queue vm_pregion_queue;
extern spinlock_t	       vm_page_queues_lock;
extern vm_map_t		       kmap; /** global kernel map */
extern vmstat_t		       vmstat;

#endif /* VM_H_ */
