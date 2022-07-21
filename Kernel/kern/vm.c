/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * \page Virtual Memory Manager
 *
 * Overview
 * ========
 *
 * The SCAL/UX virtual memory manager manages the system's memory. It is grossly
 * divisible into two components: pmap, which is machine-dependent and directly
 * interfaces with the MMU, and vm, which is machine-independent and calls on
 * pmap.
 *
 * The design of the VMM derives mostly from that of NetBSD's UVM, which itself
 * derived significant influence from Mach VM and SunOS VM, so elements of those
 * traditions are also visible.
 *
 * Features of the VMM include:
 * - Lazy allocation
 *	Memory often does not need to be allocated until it is actually read
 *	or written to. This applies to everything from memory-mapped files to
 *	even page tables themselves.
 * - Swapping (not yet!!):
 *	Swapping out of pages to a backing store is possible. The treatment of
 *	pages backed by an actual object (e.g. a memory-mapped file) is
 *	identical to the treatment of pages backed by swap space.
 * - VM Compression:
 *	If possible, pages are compressed instead of being swapped out; a part
 *	of system memory is reserved by the VM Compressor to compress pages
 *	into. When this is no longer adequate, the compressed pages can then be
 *	swapped out.
 *
 * Concepts
 * ========
 *
 * Address Space Map (`vm_map_t`)
 * ------------------------------
 *
 * These represent a single address space. There are two kinds: a kernel map (of
 * which only a single exists) and a process map. On all current ports, the
 * current user map (if any) defines the mappings for the lower half of the
 * system virtual address space, while the kernel map defines those for the
 * higher half.
 *
 * Maps are made up of entries (`vm_map_entry_t`), which store protection mode,
 * a reference to a VM Object, offset into the VM object, and start/end address.
 *
 * VM Objects
 * ----------
 *
 * VM Objects are entities which can be mapped into an address space. Being
 * objects, the exact semantics of their mapping varies. There are three main
 * types:
 *  - Anonymous objects: Represent anonymous memory, memory which is
 * zero-initialised and not backed by a file or any other sort of object. Pages
 * of anonymous memory may be swapped out (compressed by the VM Compressor and
 * possibly also stored in a swapfile); the subsystem that does this is called
 * the Default Pager.
 * - Device objects: Directly map physical pages. Through these devices accessed
 * by memory-mapped I/O may be used.
 * - Backed objects: These are backed by some actually-existing object. They are
 * associated with a Pager, an object which can fetch pages into memory or put
 * them back to their backing store. The major example is the VNode Pager, which
 * handles memory-mapped files.
 *
 * Implementation
 * ==============
 *
 * Resident Page Tables (RPTs)
 * ---------------------------
 *
 * These are effectively inverted page tables. They store data on all resident
 * pages which may be used as memory proper (so framebuffers, device memory, etc
 * are not covered) and positioned at the beginning of all usable regions of
 * memory detected by the bootloader. They are arrays of `vm_page_t` structures.
 *
 * Physical addresses representing useful memory can therefore be quickly
 * associated with their RPT entry. The RPT entry stores information including
 * (if the platform does not make this cheap) where every virtual mapping of a
 * physical page can be found; this is used during swapout to invalidate all
 * mappings. They also store linkage into the page queues.
 *
 * Page Queues
 * -----------
 *
 * Pages of the RPTs belong to queues. There are several queues:
 *
 *  - the free queue is a freelist from which pages can be allocated
 *  - the active and inactive queues are for pageable (= swappable out, or can
 *  be written back to backing store) pages
 *  - the wired queue for pages which have been pinned so that they may not be
 *  paged out.
 *
 * Anons and Anon Maps
 * -------------------
 *
 * Anonymous memory is implemented by having an anonymous VM Object carry an
 * Anon Map. An Anon Map is made up of pointers to Anons, where an Anon
 * describes a logical page of anonymous memory; it either points to an RPT
 * entry if the page is currently resident, otherwise it stores a drumslot (a
 * unique identifier sufficient to figure out where the page has been stored in
 * a swapfile or the VM Compressor).
 *
 */

#include <stdatomic.h>

#include "kern/lock.h"
#include "libkern/klib.h"
#include "machine/spl.h"
#include "machine/vm_machdep.h"
#include "sys/queue.h"
#include "vm.h"
#include "vmem.h"

spinlock_t	     vm_page_queues_lock = SPINLOCK_INITIALISER;
struct vm_page_queue pg_freeq = TAILQ_HEAD_INITIALIZER(pg_freeq),
		     pg_activeq = TAILQ_HEAD_INITIALIZER(pg_activeq),
		     pg_inactiveq = TAILQ_HEAD_INITIALIZER(pg_inactiveq),
		     pg_wireq = TAILQ_HEAD_INITIALIZER(pg_wireq);
vm_map_t kmap;
vmstat_t vmstat;
bool	 vm_debug_anon = false;

/*
 * note: ultimately a vm_object will always have a tree of vm_page_t's. and then
 * if you have some vnode vm_object and create a copy, it initially adds
 * vm_pages from the vnode when read-faults occur? need to consider this further
 * - it violates the principle that a vm_page_t is mapped either in one amap or
 * in one object???
 *
 * --- do all vm_object_ts really need a tree of vm_page_ts? not on amd64 anyway
 * --- the one amap/one object principle doesn't exist. they belong to either
 * one object or one anon....
 */

/*
 * we use obj->anon.parent IFF there is no vm_amap_entry for an offset within
 * obj.
 *
 * so if you copy e.g. a vnode object, what pages are already faulted in (and
 * therefore within the vnode object's amap) have their amap entries copied over
 * directly. and when there is a fault on that address, the anon is copied.
 *
 * but when there is a fault on an address not yet mapped in, the parent object
 * pager is used to map one in.
 */

static vm_amap_t *
amap_copy(vm_amap_t *amap)
{
	vm_amap_t *newamap = kmalloc(sizeof *newamap);

	newamap->curnchunk = amap->curnchunk;
	newamap->chunks = kmalloc(sizeof *newamap->chunks * amap->curnchunk);
	for (int i = 0; i < amap->curnchunk; i++) {
		if (amap->chunks[i] == NULL) {
			newamap->chunks[i] = NULL;
			continue;
		}

		newamap->chunks[i] = kcalloc(1, sizeof **newamap->chunks);
		for (int i2 = 0; i2 < ELEMENTSOF(amap->chunks[i]->anon); i2++) {
			newamap->chunks[i]->anon[i2] =
			    amap->chunks[i]->anon[i2];
			if (amap->chunks[i]->anon[i2] != NULL)
				amap->chunks[i]->anon[i2]->refcnt++;
		}
	}

	return newamap;
}

int
vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size)
{
	vm_object_t *obj;
	int	     r;

	assert((size & (PGSIZE - 1)) == 0);

	obj = vm_aobj_new(size);

	r = vm_map_object(map, obj, vaddrp, size, false);
	if (r < 0) {
		kfree(obj->anon.amap);
		kfree(obj);
		return r;
	}

	if (out)
		*out = obj;

	return 0;
}

vm_object_t *
vm_aobj_new(size_t size)
{
	vm_object_t *obj = kmalloc(sizeof *obj);

	obj->type = kVMObjAnon;
	obj->anon.parent = NULL;
	obj->anon.amap = kmalloc(sizeof(*obj->anon.amap));
	obj->anon.amap->chunks = NULL;
	obj->anon.amap->curnchunk = 0;
	obj->size = size;

	return obj;
}

static vm_map_entry_t *
map_entry_for_addr(vm_map_t *map, vaddr_t addr)
{
	vm_map_entry_t *entry;
	TAILQ_FOREACH (entry, &map->entries, queue) {
		if (addr >= entry->start && addr < entry->end)
			return entry;
	}
	return NULL;
}

int
vm_deallocate(vm_map_t *map, vaddr_t start, size_t size)
{
	spl_t		spl = splvm();
	vm_map_entry_t *entry;

	// lock(&map->lock);
	entry = map_entry_for_addr(map, start);
	if (!entry) {
		kprintf("failed to find entry for address %p\n", start);
		return -1;
	}
	assert(vmem_xfree(&map->vmem, (vmem_addr_t)start, size) >= 0);
	// vm_object_release(entry->obj);
	for (vaddr_t v = entry->start; v < entry->end; v += PGSIZE) {
		pmap_unenter(map, NULL, v, NULL);
	}
	TAILQ_REMOVE(&map->entries, entry, queue);
	kfree(entry);
	/* todo: tlb shootdowns if map is used by multiple threads */
	// unlock(&map->lock);
	splx(spl);

	return 0;
}

static void
vm_dump(vm_map_t *map)
{
	vm_map_entry_t *ent;

	TAILQ_FOREACH (ent, &map->entries, queue) {
		kprintf("%p-%p type %d", ent->start, ent->end, ent->obj->type);
	}
}

static void
copyphyspage(paddr_t dst, paddr_t src)
{
	vaddr_t dstv = P2V(dst), srcv = P2V(src);
	memcpy(dstv, srcv, PGSIZE);
}

/**
 * Create a new anon for a given offset.
 * @returns LOCKED new anon
 */
vm_anon_t *
anon_new(size_t offs)
{
	vm_anon_t *newanon = kmalloc(sizeof *newanon);
	newanon->refcnt = 1;
	spinlock_init(&newanon->lock);
	lock(&newanon->lock);
	newanon->resident = true;
	newanon->offs = offs;
	newanon->physpage = vm_pagealloc(1);
	newanon->physpage->anon = newanon;
	return newanon;
}

/**
 * Copy an anon, yielding a new anon.
 * @param anon LOCKED anon to copy
 * @returns LOCKED new anon, or NULL if OOM.
 */
vm_anon_t *
anon_copy(vm_anon_t *anon)
{
	vm_anon_t *newanon = anon_new(anon->offs);
	copyphyspage(newanon->physpage->paddr, anon->physpage->paddr);
	return newanon;
}

static vm_anon_t **
amap_anon_at(vm_amap_t *amap, pgoff_t page)
{
	size_t minnchunk = page / kAMapChunkNPages + 1;
	size_t chunk = page / kAMapChunkNPages;

	if (amap->curnchunk < minnchunk) {
		amap->chunks = krealloc(amap->chunks,
		    minnchunk * sizeof(vm_amap_chunk_t *));
		for (int i = amap->curnchunk; i < minnchunk; i++)
			amap->chunks[i] = NULL;
		amap->curnchunk = minnchunk;
	}

	if (!amap->chunks[chunk])
		amap->chunks[chunk] = kcalloc(1, sizeof *amap->chunks[chunk]);

	return &amap->chunks[chunk]->anon[(page % kAMapChunkNPages)];
}

#define VM_DBG(...)        \
	if (vm_debug_anon) \
	kprintf(__VA_ARGS__)

static int
fault_aobj(vm_map_t *map, vm_object_t *aobj, vaddr_t vaddr, voff_t voff,
    vm_fault_flags_t flags)
{
	vm_anon_t **pAnon, *anon;

	/* first, check if we have an anon already */
	pAnon = amap_anon_at(aobj->anon.amap, (voff / PGSIZE));

	if (*pAnon != NULL) {
		anon = *pAnon;
		lock(&anon->lock);

		if (!anon->resident) {
			kprintf("vm_fault: paging in not yet supported\n");
			/* paging in will set the page wired */
			assert(!(flags & kVMFaultPresent));
			unlock(&anon->lock);
			return -1;
		}

		if (anon->refcnt > 1) {
			if (flags & kVMFaultWrite) {
				VM_DBG(
				    "nonpresent; refcnt > 1; write-fault; copy %p to new page and map read-write\n",
				    anon->physpage->paddr);

				anon->refcnt--;
				*pAnon = anon_copy(*pAnon);

				if (flags & kVMFaultPresent) {
					VM_DBG(
					    " - page mapped read-only (removing)\n");
					pmap_unenter(map, anon->physpage, vaddr,
					    NULL);
				}

				unlock(&anon->lock);
				anon = *pAnon;
				vm_page_unwire(anon->physpage);
				pmap_enter(map, anon->physpage, vaddr, kVMAll);
			} else {
				VM_DBG(
				    "nonpresent; refcnt > 1; read-fault; map pg %p readonly\n",
				    anon->physpage->paddr);

				assert(!(flags & kVMFaultPresent));
				pmap_enter(map, anon->physpage, vaddr,
				    kVMRead | kVMExecute);
			}
		} else {
			if (flags & kVMFaultPresent) {
				VM_DBG(
				    "present and refcnt 1, remap pg %p readwrite\n",
				    anon->physpage->paddr);
				/** can simply upgrade to write-enabled */
				pmap_reenter(map, anon->physpage, vaddr,
				    kVMAll);
			} else {
				VM_DBG(
				    "nonpresent and refcnt 1, map pg %p readwrite\n",
				    anon->physpage->paddr);

				/** XXX FIXME: is this legal? */
				pmap_enter(map, anon->physpage, vaddr, kVMAll);
			}
		}

		unlock(&anon->lock);
		return 0;
	} else if (aobj->anon.parent) {
		kprintf("vm_fault: fetch from parent is not yet supported\n");
		/* this needs some thought to do properly */
		return -1;
	}

	VM_DBG("not present, creating new zeroed\n");

	/* page not present locally, nor in parent => map new zero page */
	anon = anon_new(voff / PGSIZE);
	VM_DBG(" - got page %p\n", anon->physpage->paddr);

	/* can just map in readwrite as it's new thus refcnt = 1 */
	pmap_enter(map, anon->physpage, vaddr, kVMAll);
	*pAnon = anon;
	/* now let it be subject to paging */
	vm_page_unwire(anon->physpage);
	unlock(&anon->lock);

	return 0;
}

int
vm_fault(vm_map_t *map, vaddr_t vaddr, vm_fault_flags_t flags)
{
	vm_map_entry_t *ent;
	voff_t		obj_off;

#ifdef DEBUG_VM_FAULT
	kprintf("vm_fault: in map %p at addr %p (flags: %d)\n", map, vaddr,
	    flags);
#endif

	if (vaddr >= (vaddr_t)KHEAP_BASE) {
		map = &kmap;
	}

	ent = map_entry_for_addr(map, vaddr);
	vaddr = (vaddr_t)PGROUNDDOWN(vaddr);

	if (!ent) {
		kprintf("vm_fault: no object at vaddr %p in map %p\n", vaddr,
		    map);
		vm_dump(map);
		return -1;
	}

	if (ent->obj->type != kVMObjAnon) {
		kprintf("vm_fault: fault in unfaultable object (type %d)\n",
		    ent->obj->type);
		return -1;
	}

	obj_off = vaddr - ent->start;

	return fault_aobj(map, ent->obj, vaddr, obj_off, flags);
}

vm_map_t *
vm_map_fork(vm_map_t *map)
{
	vm_map_t *newmap = kmalloc(sizeof *newmap);
	// vm_map_entry_t *ent;

	kprintf("vm_map_fork: not implemented properly yet\n");

	newmap->pmap = pmap_new();
	TAILQ_INIT(&newmap->entries);
	vmem_init(&newmap->vmem, "task map", USER_BASE, USER_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, 0, kSPLVM);

	if (map == &kmap)
		return newmap; /* nothing to inherit */

#if 0
        TAILQ_FOREACH(ent, &map->entries, entries) {
                if(ent->inheritance == kVMMapEntryInheritShared) {
                        vm_map_entry_t * newent = kmalloc(sizeof *newent);

                        newent->inheritance = kVMMapEntryInheritShared;
                        newent->obj = ent->obj;
                        newent->obj->refcnt++;
                        newent->size = ent->size;
                        newent->vaddr = ent->vaddr;
                } else {
                        kprintf("vm_map_fork: unhandled inheritance\n");
                }
        }
#endif

	return newmap;
}

int
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy)
{
	bool		exact = *vaddrp != VADDR_MAX;
	vmem_addr_t	addr = *vaddrp == VADDR_MAX ? 0 : (vmem_addr_t)*vaddrp;
	vm_map_entry_t *entry;
	int		r;

	assert(map);
	assert(!copy);

	r = vmem_xalloc(&map->vmem, size, 0, 0, 0, exact ? addr : 0, 0,
	    exact ? kVMemExact : 0, &addr);
	if (r < 0)
		return r;

	entry = kmalloc(sizeof *entry);
	entry->start = (vaddr_t)addr;
	entry->end = (vaddr_t)addr + size;
	entry->obj = obj;

	TAILQ_INSERT_TAIL(&map->entries, entry, queue);

	*vaddrp = (vaddr_t)addr;

	return 0;
}

vm_object_t *
vm_object_copy(vm_object_t *obj)
{
	vm_object_t *newobj = kmalloc(sizeof *newobj);

	if (obj->type != kVMObjAnon) {
		fatal("vm_object_copy: only implemented for anons as of yet\n");
	}

	spinlock_init(&newobj->lock);
	newobj->refcnt = 1;
	newobj->size = obj->size;
	newobj->type = obj->type;
	if (obj->type == kVMObjAnon) {
		newobj->anon.parent = obj->anon.parent ? obj->anon.parent :
							 NULL;
	}
	newobj->anon.amap = amap_copy(obj->anon.amap);

	return newobj;
}

void
vm_page_unwire(vm_page_t *page)
{
	spl_t spl = splvm();
	assert(page->wirecnt > 0);
	if (--page->wirecnt == 0) {
		TAILQ_REMOVE(&pg_wireq, page, queue);
		TAILQ_INSERT_HEAD(&pg_activeq, page, queue);
		vmstat.pgs_wired--;
		vmstat.pgs_active++;
	}
	splx(spl);
}
