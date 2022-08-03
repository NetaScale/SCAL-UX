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

#include <errno.h>
#include <stdatomic.h>

#include "kern/lock.h"
#include "libkern/klib.h"
#include "libkern/obj.h"
#include "machine/intr.h"
#include "machine/spl.h"
#include "machine/vm_machdep.h"
#include "sys/queue.h"
#include "sys/vm.h"
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

vm_anon_t *anon_copy(vm_anon_t *anon);
void	   anon_release(vm_anon_t *anon) LOCK_RELEASE(anon->lock);

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

			if (amap->chunks[i]->anon[i2] == NULL)
				continue;

			/* TODO: copy-on-write */
			// amap->chunks[i]->anon[i2]->refcnt++;
			newamap->chunks[i]->anon[i2] = anon_copy(
			    newamap->chunks[i]->anon[i2]);
			unlock(&newamap->chunks[i]->anon[i2]->lock);
		}
	}

	return newamap;
}

void
amap_release(vm_amap_t *amap)
{
	for (int i = 0; i < amap->curnchunk; i++) {
		if (amap->chunks[i] == NULL)
			continue;
		for (int i2 = 0; i2 < ELEMENTSOF(amap->chunks[i]->anon); i2++) {
			vm_anon_t *anon = amap->chunks[i]->anon[i2];

			if (anon == NULL)
				continue;

			lock(&anon->lock);
			anon_release(anon);
		}
		kfree(amap->chunks[i]);
	}
	kfree(amap);
}

int
vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size)
{
	vm_object_t *obj;
	int	     r;

	assert((size & (PGSIZE - 1)) == 0);

	obj = vm_aobj_new(size);

	r = vm_map_object(map, obj, vaddrp, size, false);
	if (r < 0)
		goto finish;

	if (out)
		*out = obj;

finish:
	/* object is retained by the map now */
	vm_object_release(obj);

	return 0;
}

vm_object_t *
vm_aobj_new(size_t size)
{
	vm_object_t *obj = kmalloc(sizeof *obj);

	obj->type = kVMObjAnon;
	obj->anon.parent = NULL;
	obj->anon.amap = kcalloc(1, sizeof(*obj->anon.amap));
	obj->anon.amap->chunks = NULL;
	obj->anon.amap->curnchunk = 0;
	obj->size = size;
	obj->refcnt = 1;

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

static int
unmap_entry(vm_map_t *map, vm_map_entry_t *entry)
{
	assert(vmem_xfree(&map->vmem, (vmem_addr_t)entry->start,
		   entry->end - entry->start) >= 0);
	for (vaddr_t v = entry->start; v < entry->end; v += PGSIZE) {
		pmap_unenter(map, NULL, v, NULL);
	}
	vm_object_release(entry->obj);
	TAILQ_REMOVE(&map->entries, entry, queue);
	kfree(entry);
	/* todo: tlb shootdowns if map is used by multiple
	 * threads */
	return 0;
}

int
vm_deallocate(vm_map_t *map, vaddr_t start, size_t size)
{
	spl_t		spl = splvm();
	vm_map_entry_t *entry, *tmp;
	vaddr_t		end = start + size;

	// lock(&map->lock);

	TAILQ_FOREACH_SAFE (entry, &map->entries, queue, tmp) {
		if ((entry->start < start && entry->end <= start) ||
		    (entry->start >= end))
			continue;
		else if (entry->start >= start && entry->end <= end) {
			unmap_entry(map, entry);
		} else if (entry->start >= start && entry->end <= end) {
			fatal("unimplemented deallocate right of vm object\n");
		} else if (entry->start < start && entry->end < end) {
			fatal("unimplemented other sort of deallocate\n");
		}
	}

	// unlock(&map->lock);
	splx(spl);

	return 0;
}

static void
vm_dump(vm_map_t *map)
{
	vm_map_entry_t *ent;

	TAILQ_FOREACH (ent, &map->entries, queue) {
		assert (ent != NULL);
		assert(ent->obj != NULL);
		kprintf("%p-%p type %d\n", ent->start, ent->end,
		    ent->obj->type);
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
	newanon->physpage = vm_pagealloc_zero(1);
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

void
anon_release(vm_anon_t *anon) LOCK_RELEASE(anon->lock)
{
	if (--anon->refcnt > 0) {
		unlock(&anon->lock);
		return;
	}

	if (!anon->resident)
		fatal("anon_release: doesn't support swapped-out anons yet\n");

	assert(anon->physpage);

	VM_PAGE_QUEUES_LOCK();
	TAILQ_REMOVE(&pg_wireq, anon->physpage, queue);
	vm_pagefree(anon->physpage);
	VM_PAGE_QUEUES_UNLOCK();
	kfree(anon);
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
#if 0 /* let's do this a better way! e.g. on trying to pageout, lock page's \
	 owner => its anon and check if there is a wire bit set in the anon \
	 instead? */
	/* now let it be subject to paging */
	vm_page_unwire(anon->physpage);
#endif
	unlock(&anon->lock);

	return 0;
}

int
vm_fault(intr_frame_t *frame, vm_map_t *map, vaddr_t vaddr,
    vm_fault_flags_t flags)
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
		md_intr_frame_trace(frame);
		fatal("no object\n");
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
	vm_map_t	 *newmap = kmalloc(sizeof *newmap);
	vm_map_entry_t *ent;
	int		r;

	newmap->pmap = pmap_new();
	TAILQ_INIT(&newmap->entries);
	vmem_init(&newmap->vmem, "task map", USER_BASE, USER_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, 0, kSPLVM);

	if (map == &kmap)
		return newmap; /* nothing to inherit */

	TAILQ_FOREACH (ent, &map->entries, queue) {
		vm_object_t *newobj;
		vaddr_t	     start = ent->start;

		if (ent->obj->type != kVMObjAnon)
			fatal("vm_map_fork: only handles anon objects\n");

		newobj = vm_object_copy(ent->obj);
		assert(newobj != NULL);

		r = vm_map_object(newmap, newobj, &start, ent->end - ent->start,
		    false);
		assert(r == 0)

		    vm_object_release(newobj);
	}

	return newmap;
}

vm_map_t *
vm_map_new()
{
	vm_map_t *newmap = kmalloc(sizeof *newmap);

	newmap->pmap = pmap_new();
	TAILQ_INIT(&newmap->entries);
	vmem_init(&newmap->vmem, "task map", USER_BASE, USER_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, 0, kSPLVM);

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
	obj->refcnt++;

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
vm_object_release(vm_object_t *obj) LOCK_RELEASE(obj->lock)
{
	if (--obj->refcnt > 0) {
		unlock(&obj->lock);
		return;
	}

	if (obj->type == kVMObjAnon) {
		amap_release(obj->anon.amap);
	} else
		fatal("vm_object_release: only implemented for anons\n");

	kfree(obj);
}

void
vm_map_release(vm_map_t *map)
{
	vm_deallocate(map, (vaddr_t)USER_BASE, USER_SIZE);
	vmem_destroy(&map->vmem);
	pmap_free(map->pmap);
	kfree(map);
}

int
vm_mdl_expand(vm_mdl_t **mdl, size_t bytes)
{
	size_t	  nPages = PGROUNDUP(bytes) / PGSIZE;
	vm_mdl_t *newmdl;

	newmdl = kmalloc(sizeof *newmdl + sizeof(vm_page_t *) * nPages);
	if (!newmdl)
		return -ENOMEM;

	newmdl->offset = 0;
	newmdl->nBytes = bytes;
	newmdl->nPages = nPages;
	for (int i = 0; i < (*mdl)->nPages; i++)
		newmdl->pages[i] = (*mdl)->pages[i];

	for (int i = (*mdl)->nPages; i < nPages; i++) {
		newmdl->pages[i] = vm_pagealloc(1);
		assert(newmdl->pages[i]);
	}

	kfree(*mdl);
	*mdl = newmdl;

	return 0;
}

int
vm_mdl_new_with_capacity(vm_mdl_t **out, size_t bytes)
{
	size_t	  nPages = PGROUNDUP(bytes) / PGSIZE;
	vm_mdl_t *mdl = kmalloc(sizeof *mdl + sizeof(vm_page_t *) * nPages);

	if (!mdl)
		return -ENOMEM;

	mdl->offset = 0;
	mdl->nBytes = bytes;
	mdl->nPages = nPages;
	for (int i = 0; i < nPages; i++) {
		mdl->pages[i] = vm_pagealloc_zero(true);
		assert(mdl->pages[i] != NULL);
	}

	*out = mdl;

	return 0;
}

size_t
vm_mdl_capacity(vm_mdl_t *mdl)
{
	return mdl->nPages * PGSIZE;
}

void
vm_mdl_copy(vm_mdl_t *mdl, void *buf, size_t nBytes, off_t off)
{
	off += mdl->offset;
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t firstpage = base / PGSIZE;
	size_t lastpage = firstpage + (pageoff + nBytes - 1) / PGSIZE + 1;

	for (size_t iPage = firstpage; iPage < lastpage; iPage++) {
		vm_page_t *page;
		size_t	   tocopy;

		if (nBytes > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nBytes;

		page = mdl->pages[iPage];

		memcpy(buf + (iPage - firstpage) * PGSIZE,
		    P2V(page->paddr) + pageoff, tocopy);

		nBytes -= tocopy;
		pageoff = 0;
	}
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
