/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file vm.h
 * @brief Interface to the kernel's virtual memory manager.
 *
 * \addtogroup Kernel
 * @{
 * \defgroup VMM Virtual Memory Management
 * The virtual memory manager manages virtual memory.
 * @}
 */

#ifndef VM_H_
#define VM_H_

#include <kern/sync.h>
#include <kern/types.h>
#include <kern/vmem_impl.h>
#include <libkern/obj.h>
#include <machine/vm.h>

#define VADDR_MAX (vaddr_t) UINT64_MAX

#define ROUNDUP(addr, align) ((((uintptr_t)addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))

#define PTRROUNDUP(addr) ROUNDUP(addr, (sizeof(uintptr_t)))
/** Round a value up to pointer alignment. */
#define PTRROUNDDOWN(addr) ROUNDDOWN(addr, (sizeof(uintptr_t)))

#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

/*!
 * Fault flags (matches i386 MMU)
 */
typedef enum vm_fault_flags {
	kVMFaultPresent = 1,
	kVMFaultWrite = 2,
	kVMFaultUser = 4,
	kVMFaultExecute = 16,
} vm_fault_flags_t;

/*!
 * Protection flags for mappings.
 */
typedef enum vm_prot {
	kVMRead = 0x1,
	kVMWrite = 0x2,
	kVMExecute = 0x4,
	kVMAll = kVMRead | kVMWrite | kVMExecute,
} vm_prot_t;

/**
 * Represents a pager-backed virtual memory object.
 */
typedef struct vm_object {
	int	refcnt;
	mutex_t lock;

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
		} kheap;
		struct {
			// vm_amap_t *amap;
			/** if not -1, the maximum size of this object */
			ssize_t		  maxsize;
			struct vm_object *parent;
		} anon;
	};
} vm_object_t;

/*!
 * @name PMap
 */

/*! Port-specific physical map. */
typedef struct pmap pmap_t;

/*!
 * Low-level mapping a single physical page to a virtual address. Mappings are
 * not tracked.
 */
void pmap_enter_kern(struct pmap *pmap, paddr_t phys, vaddr_t virt,
    vm_prot_t prot);

/*!
 * Low-level unmapping of a page. Invalidates local TLB but does not do a TLB
 * shootdown. Tracking is not touched.
 */
struct vm_page *pmap_unenter_kern(struct vm_map *map, vaddr_t virt);

/*!
 * Invalidate a page mapping for the virtual address \p addr in the current
 * address space.
 */
void
pmap_invlpg(vaddr_t addr);

/*!
 * @}
 */

/*!
 * @name Maps
 * @{
 */

/*!
 * Represents an entry within a vm_map_t.
 */
typedef struct vm_map_entry {
	TAILQ_ENTRY(vm_map_entry) queue;
	vaddr_t			  start, end;
	voff_t			  offset;
	vm_object_t		    *obj;
} vm_map_entry_t;

/*!
 * Virtual address space map - either the global kernel map, or a user
 * process-specific map.
 */
typedef struct vm_map {
	TAILQ_HEAD(, vm_map_entry) entries;
	vmem_t	     vmem;
	struct pmap *pmap;
} vm_map_t;

/*! Global kernel map. */
extern vm_map_t kmap;

/*!
 * @}
 */

/*!
 * @name Pages
 * @{
 */

enum vm_page_queue {
	kVMPageFree = 0,
	kVMPageKMem = 1,
	kVMPageWired = 2,
	kVMPageActive = 3,
	kVMPageInactive = 4,
	kVMPagePMap = 5,
};

/*!
 * Represents a physical page of useable general-purpose memory.
 *
 * If neither anon nor obj are set, it may be internally managed (kmem).
 * In the case of tmpfs, both may be set.
 */
typedef struct vm_page {
	/*! Links to its queue: kmem, wired, active, . */
	TAILQ_ENTRY(vm_page) pagequeue;

	/*! Lock on page identity. */
	mutex_t lock;

	/*! Page state. */
	enum vm_page_queue queue : 4;

	/*! for pageable mappings */
	union {
		struct vm_anon   *anon; /*! if belonging to an anon */
		struct vm_object *obj;	/*! if belonging to non-anon object */
	};

	LIST_HEAD(, pv_entry) pv_table; /*! physical page -> virtual mappings */

	paddr_t paddr; /*! physical address of page */
} vm_page_t;

typedef struct vm_pagequeue {
	TAILQ_HEAD(, vm_page) queue;
	size_t	npages;
	mutex_t lock;
} vm_pagequeue_t;

/*!
 * Physical region description.
 */
typedef struct vm_pregion {
	/*! Linkage to vm_pregion_queue. */
	TAILQ_ENTRY(vm_pregion) queue;
	/*! Base address of region. */
	paddr_t base;
	/*! Number of pages the region covers. */
	size_t npages;
	/*! Resident page table part for region. */
	vm_page_t pages[0];
} vm_pregion_t;

typedef TAILQ_HEAD(, vm_pregion) vm_pregion_queue_t;

/*! Allocate a new page. It is enqueued on the specified queue. */
vm_page_t *vm_pagealloc(bool sleep, vm_pagequeue_t *queue);

/*! Free a page. It is automatically removed from its current queue. */
void vm_pagefree(vm_page_t *page);

/*! Get the page corresponding to a particular physical address. */
vm_page_t *vm_page_from_paddr(paddr_t paddr);

/*!
 * Move a page from one queue to another.
 *
 * @param from page queue to move it from. Usually should be NULL (it is
 * determined by this function from the page's flags); if it is not, then it
 * must be locked.
 * @param to (unlocked) page queue to move it to.
 */
void vm_page_changequeue(vm_page_t *page, nullable vm_pagequeue_t *from,
    vm_pagequeue_t *to) LOCK_RELEASE(from->lock);

/*! The page queues. */
extern vm_pagequeue_t vm_pgfreeq, vm_pgkmemq, vm_pgwiredq, vm_pgactiveq,
    vm_pginactiveq, vm_pgpmapq;

/*!
 * @}
 */

/*!
 * @name Kernel memory
 * @{
 */

/*! Flags that may be passed to vm_kalloc(). */
enum vm_kalloc_flags {
	/*! immediately return NULL if no free pages currently */
	kVMKNoSleep = 0,
	/*! infallible; sleepwait for a page if no pages currently available */
	kVMKSleep = 1,
};

/** Set up the kernel memory subsystem. */
void vm_kernel_init();

/*!
 * Allocate pages of kernel heap.
 *
 * @param flags see vm_kalloc_flags
 */
vaddr_t vm_kalloc(size_t npages, enum vm_kalloc_flags flags);

/*!
 * Free pages of kernel heap.
 */
void vm_kfree(vaddr_t addr, size_t pages);

/*! @} */

#endif /* VM_H_ */
