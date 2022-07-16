/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <stdatomic.h>

#include "kern/lock.h"
#include "liballoc.h"
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
			amap->chunks[i]->anon[i2]->refcnt++;
		}
	}

	return newamap;
}

vm_object_t *
vm_object_copy(vm_object_t *obj)
{
	vm_object_t *newobj = kmalloc(sizeof *newobj);

	assert(obj->type == kVMObjAnon);

	spinlock_init(&newobj->lock);
	newobj->refcnt = 1;
	newobj->size = obj->size;
	newobj->type = obj->type;
	newobj->anon.parent = obj->anon.parent ? obj->anon.parent : obj;
	newobj->anon.amap = amap_copy(obj->anon.amap);

	return newobj;
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

static void
copyphyspage(paddr_t dst, paddr_t src)
{
	vaddr_t dstv = HHDM_BASE + dst, srcv = HHDM_BASE + src;
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
	vm_anon_t *newanon = kmalloc(sizeof *newanon);
	newanon->refcnt = 1;
	spinlock_init(&newanon->lock);
	lock(&newanon->lock);
	newanon->resident = true;
	newanon->offs = anon->offs;
	newanon->physpage = vm_pagealloc(1);
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
				anon->refcnt--;
				*pAnon = anon_copy(*pAnon);

				if (flags & kVMFaultPresent) {
					pmap_unenter(map, anon->physpage, vaddr,
					    NULL);
				}

				unlock(&anon->lock);
				anon = *pAnon;
				vm_page_unwire(anon->physpage);
				pmap_enter(map, anon->physpage, vaddr, kVMAll);
			} else {
				assert(!(flags & kVMFaultPresent));
				pmap_enter(map, anon->physpage, vaddr,
				    kVMRead | kVMExecute);
			}
		} else {
			if (flags & kVMFaultPresent)
				/** can simply upgrade to write-enabled */
				pmap_reenter(map, anon->physpage, vaddr,
				    kVMAll);
		}

		unlock(&anon->lock);
		return 0;
	} else if (aobj->anon.parent) {
		kprintf("vm_fault: fetch from parent is not yet supported\n");
		/* this needs some thought to do properly */
		return -1;
	}

	/* page not present locally, nor in parent => map new zero page */
	anon = anon_new(voff / PGSIZE);
	/* can just map in readwrite as it's new thus refcnt = 1 */
	pmap_enter(map, anon->physpage, vaddr, kVMAll);
	/* now let it be subject to paging */
	vm_page_unwire(anon->physpage);

	return 0;
}

int
vm_fault(vm_map_t *map, vaddr_t vaddr, vm_fault_flags_t flags)
{
	vm_map_entry_t *ent = map_entry_for_addr(map, vaddr);
	voff_t		obj_off;

	kprintf("vm_fault: in map %p at addr %p (flags: %d)\n", map, vaddr,
	    flags);

	if (!ent) {
		kprintf("vm_fault: no object at vaddr %p\n", vaddr);
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

int
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy)
{
	bool		exact = *vaddrp != VADDR_MAX;
	vmem_addr_t	addr = *vaddrp == VADDR_MAX ? 0 : (vmem_addr_t)*vaddrp;
	vm_map_entry_t *entry;
	int		r;

	r = vmem_xalloc(&map->vmem, size, 0, 0, 0, 0, 0, exact ? kVMemExact : 0,
	    &addr);
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
