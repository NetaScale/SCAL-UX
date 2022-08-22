/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/kmem.h>
#include <kern/task.h>
#include <libkern/klib.h>
#include <libkern/obj.h>
#include <machine/intr.h>
#include <vm/vm.h>

#include <stdatomic.h>
#include <string.h>

/*!
 * Return a pointer to the slot of an amap where the anonymous page that maps
 * \p page is found. The slot may of course contain NULL.
 */
static vm_anon_t **amap_anon_at(vm_amap_t *amap, pgoff_t page);

/**
 * Create a new anon for a given offset.
 * @returns LOCKED new anon
 */
/* LOCKED */ vm_anon_t *anon_new();

/**
 * Copy an anon, yielding a new anon.
 * @param anon LOCKED anon to copy
 * @returns LOCKED new anon, or NULL if OOM.
 */
vm_anon_t *anon_copy(vm_anon_t *anon) LOCK_REQUIRES(anon->lock);

/*!
 * Find the map entry for a given virtual address.
 * @returns NULL if no map entry encompasses this address.
 */
static vm_map_entry_t *map_entry_for_addr(vm_map_t *map, vaddr_t addr)
    LOCK_REQUIRES(map->lock);

/*
 * faults
 */
static int
fault_aobj(vm_map_t *map, vm_object_t *aobj, vaddr_t vaddr, voff_t voff,
    vm_fault_flags_t flags) LOCK_REQUIRES(map->lock) LOCK_REQUIRES(aobj->lock)
{
	vm_anon_t **pAnon, *anon;

	/* first, check if we have an anon already */
	pAnon = amap_anon_at(aobj->anon.amap, (voff / PGSIZE));

	if (*pAnon != NULL) {
		anon = *pAnon;
		mutex_lock(&anon->lock);

		if (!anon->resident) {
			kprintf("vm_fault: paging in not yet supported\n");
			/* paging in will set the page wired */
			assert(!(flags & kVMFaultPresent));
			mutex_unlock(&anon->lock);
			return -1;
		}

		if (anon->refcnt > 1) {
			if (flags & kVMFaultWrite) {
				/*
				 * refcnt >1, and it's a write
				 * the anon must be duplicated, an> the new anon
				 * can then be mapped read/write.
				 */

				anon->refcnt--;
				*pAnon = anon_copy(*pAnon);

				if (flags & kVMFaultPresent) {
					/*
					 * there is an existing read-only
					 * mapping which must be removed
					 */
					pmap_unenter(map, anon->physpage, vaddr,
					    NULL);
				}

				mutex_unlock(&anon->lock);
				anon = *pAnon;
				pmap_enter(map, anon->physpage, vaddr, kVMAll);
			} else {
				/*
				 * refcnt >1 and it's a read; we can just map
				 * that anon readonly.
				 */

				assert(!(flags & kVMFaultPresent));
				pmap_enter(map, anon->physpage, vaddr,
				    kVMRead | kVMExecute);
			}
		} else {
			if (flags & kVMFaultPresent) {
				/*
				 * the only possible(?) case when there's a
				 * fault in which page is present and anon has a
				 * refcnt of 1 is that it had been mapped
				 * read-only during a CoW clone, but then the
				 * clone took a write fault.
				 */
				assert(flags & kVMFaultWrite);
				pmap_reenter(map, anon->physpage, vaddr,
				    kVMAll);
			} else {
				/*
				 * a not-present fault with an anon having a
				 * refcnt of 1 suggests it has been moved to the
				 * inactive queue
				 */
				/** XXX FIXME: is this legal? */
				pmap_enter(map, anon->physpage, vaddr, kVMAll);
			}
		}

		mutex_unlock(&anon->lock);
		return 0;
	} else if (aobj->anon.parent) {
		kprintf("vm_fault: fetch from parent is not yet supported\n");
		/* does this need some thought to do properly? */
		return -1;
	}

	/* page not present locally, nor in parent => map new zero page */
	anon = anon_new(/* voff / PGSIZE */);
	*pAnon = anon;

	/* can just map in readwrite as it's new thus refcnt = 1 */
	pmap_enter(map, anon->physpage, vaddr, kVMAll);
	mutex_unlock(&anon->lock);

	return 0;
}

int
vm_fault(md_intr_frame_t *frame, vm_map_t *map, vaddr_t vaddr,
    vm_fault_flags_t flags)
{
	int r;
	vm_map_entry_t *ent;
	voff_t		obj_off;

	if (curthread()->in_pagefault) {
		spinlock_unlock(&lock_msgbuf);
		md_intr_frame_trace(frame);
		fatal("Nested page fault\n");
	}
	curthread()->in_pagefault = true;

#ifdef DEBUG_VM_FAULT
	kprintf("vm_fault: in map %p at addr %p (flags: %d)\n", map, vaddr,
	    flags);
#endif

	if (vaddr >= (vaddr_t)KHEAP_BASE) {
		map = &kmap;
	}

	mutex_lock(&map->lock);

	ent = map_entry_for_addr(map, vaddr);
	vaddr = (vaddr_t)PGROUNDDOWN(vaddr);

	if (!ent) {
		spinlock_unlock(&lock_msgbuf);
		kprintf("vm_fault: no object at vaddr %p in map %p\n", vaddr,
		    map);
		r = -1;
		goto unlockmap;
	}

	mutex_lock(&ent->obj->lock);

	if (ent->obj->type != kVMObjAnon) {
		kprintf("vm_fault: fault in unfaultable object (type %d)\n",
		    ent->obj->type);
		r = -1;
		goto unlockall;
	}

	obj_off = vaddr - ent->start;

	r = fault_aobj(map, ent->obj, vaddr, obj_off + ent->offset, flags);

unlockall:
	mutex_unlock(&ent->obj->lock);
unlockmap:
	mutex_unlock(&map->lock);

	curthread()->in_pagefault = false;

	return r;
}

/*
 * maps
 */

int
vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size)
{
	vm_object_t *obj;
	int	     r;

	assert((size & (PGSIZE - 1)) == 0);

	obj = vm_aobj_new(size);

	r = vm_map_object(map, obj, vaddrp, size, 0, false);
	if (r < 0)
		goto finish;

	if (out)
		*out = obj;

finish:
	/* object is retained by the map now */
	vm_object_release(obj);

	return 0;
}

static vm_map_entry_t *
map_entry_for_addr(vm_map_t *map, vaddr_t addr) LOCK_REQUIRES(map->lock)
{
	vm_map_entry_t *entry;
	TAILQ_FOREACH (entry, &map->entries, queue) {
		if (addr >= entry->start && addr < entry->end)
			return entry;
	}
	return NULL;
}

static int
unmap_entry(vm_map_t *map, vm_map_entry_t *entry) LOCK_REQUIRES(map->lock)
{
	assert(vmem_xfree(&map->vmem, (vmem_addr_t)entry->start,
		   entry->end - entry->start) >= 0);
	for (vaddr_t v = entry->start; v < entry->end; v += PGSIZE) {
		pmap_unenter(map, NULL, v, NULL);
	}
	vm_object_release(entry->obj);
	TAILQ_REMOVE(&map->entries, entry, queue);
	kmem_free(entry, sizeof(*entry));
	/* todo: tlb shootdowns if map is used by multiple
	 * threads */
	return 0;
}

int
vm_deallocate(vm_map_t *map, vaddr_t start, size_t size)
{
	vm_map_entry_t *entry, *tmp;
	vaddr_t		end = start + size;

	mutex_lock(&map->lock);

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

	mutex_unlock(&map->lock);

	return 0;
}

vm_map_t *
vm_map_new()
{
	vm_map_t *newmap = kmem_alloc(sizeof *newmap);

	newmap->pmap = pmap_new();
	TAILQ_INIT(&newmap->entries);
	vmem_init(&newmap->vmem, "task map", USER_BASE, USER_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, 0, kSPL0);

	return newmap;
}

void
vm_map_release(vm_map_t *map)
{
	vm_deallocate(map, (vaddr_t)USER_BASE, USER_SIZE);
	vmem_destroy(&map->vmem);
	pmap_free(map->pmap);
	kmem_free(map, sizeof(*map));
}

vm_map_t *
vm_map_fork(vm_map_t *map)
{
	vm_map_t	 *newmap = kmem_alloc(sizeof(*newmap));
	vm_map_entry_t *ent;
	int		r;

	newmap->pmap = pmap_new();
	TAILQ_INIT(&newmap->entries);
	vmem_init(&newmap->vmem, "task map", USER_BASE, USER_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, 0, kSPL0);

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
		    ent->offset, false);
		assert(r == 0);

		vm_object_release(newobj);
	}

	return newmap;
}

int
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    voff_t offset, bool copy)
{
	bool		exact = *vaddrp != VADDR_MAX;
	vmem_addr_t	addr = *vaddrp == VADDR_MAX ? 0 : (vmem_addr_t)*vaddrp;
	vm_map_entry_t *entry;
	int		r;

	assert(map != NULL && obj != NULL);
	assert((size & (PGSIZE - 1)) == 0);

	if (copy) {
		vm_object_t *newobj = vm_object_copy(obj);
		obj = newobj;
	} else {
		vm_object_retain(obj);
	}

	r = vmem_xalloc(&map->vmem, size, 0, 0, 0, exact ? addr : 0, 0,
	    exact ? kVMemExact : 0, &addr);
	if (r < 0) {
		/* TODO: free copy if necessary? */
		return r;
	}

	entry = kmem_alloc(sizeof *entry);
	entry->start = (vaddr_t)addr;
	entry->end = (vaddr_t)addr + size;
	entry->offset = offset;
	entry->obj = obj;

	TAILQ_INSERT_TAIL(&map->entries, entry, queue);

	*vaddrp = (vaddr_t)addr;

	return r;
}

/*
 * objects
 */

static void
copyphyspage(paddr_t dst, paddr_t src)
{
	vaddr_t dstv = P2V(dst), srcv = P2V(src);
	memcpy(dstv, srcv, PGSIZE);
}

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
	vm_amap_t *newamap = kmem_alloc(sizeof(*newamap));

	newamap->curnchunk = amap->curnchunk;
	newamap->chunks = kmem_alloc(sizeof(vm_amap_chunk_t *)
	    * amap->curnchunk);
	for (int i = 0; i < amap->curnchunk; i++) {
		if (amap->chunks[i] == NULL) {
			newamap->chunks[i] = NULL;
			continue;
		}

		newamap->chunks[i] = kmem_zalloc(sizeof(**newamap->chunks));
		for (int i2 = 0; i2 < elementsof(amap->chunks[i]->anon); i2++) {
			vm_anon_t *oldanon = amap->chunks[i]->anon[i2];
			newamap->chunks[i]->anon[i2] = oldanon;

			if (oldanon == NULL)
				continue;

			mutex_lock(&oldanon->lock);
			oldanon->refcnt++;
			pmap_reenter_all_readonly(oldanon->physpage);
			mutex_unlock(&oldanon->lock);
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
		for (int i2 = 0; i2 < elementsof(amap->chunks[i]->anon); i2++) {
			vm_anon_t *anon = amap->chunks[i]->anon[i2];

			if (anon == NULL)
				continue;

			anon_release(anon);
		}
		kmem_free(amap->chunks[i], sizeof(*amap->chunks[i]));
	}
	kmem_free(amap, sizeof(*amap));
}

vm_anon_t *
anon_new()
{
	vm_anon_t *newanon = kmem_alloc(sizeof *newanon);
	newanon->refcnt = 1;
	mutex_init(&newanon->lock);
	mutex_lock(&newanon->lock);
	newanon->resident = true;
	newanon->physpage = vm_pagealloc(1, &vm_pgactiveq);
	newanon->physpage->anon = newanon;
	return newanon;
}

vm_anon_t *
anon_copy(vm_anon_t *anon) LOCK_REQUIRES(anon->lock)
{
	vm_anon_t *newanon = anon_new();
	copyphyspage(newanon->physpage->paddr, anon->physpage->paddr);
	return newanon;
}

void
anon_release(vm_anon_t *anon)
{
	if (--anon->refcnt > 0)
		return;

	if (!anon->resident)
		fatal("anon_release: doesn't support swapped-out anons yet\n");

	assert(anon->physpage);

	vm_page_free(anon->physpage);
	kmem_free(anon, sizeof(*anon));
}

static vm_anon_t **
amap_anon_at(vm_amap_t *amap, pgoff_t page)
{
	size_t minnchunk = page / kAMapChunkNPages + 1;
	size_t chunk = page / kAMapChunkNPages;

	if (amap->curnchunk < minnchunk) {
		amap->chunks = kmem_realloc(amap->chunks,
		    amap->curnchunk * sizeof(vm_amap_chunk_t *),
		    minnchunk * sizeof(vm_amap_chunk_t *));
		for (int i = amap->curnchunk; i < minnchunk; i++)
			amap->chunks[i] = NULL;
		amap->curnchunk = minnchunk;
	}

	if (!amap->chunks[chunk])
		amap->chunks[chunk] = kmem_zalloc(sizeof(*amap->chunks[chunk]));

	return &amap->chunks[chunk]->anon[(page % kAMapChunkNPages)];
}

vm_object_t *
vm_aobj_new(size_t size)
{
	vm_object_t *obj = kmem_alloc(sizeof(*obj));

	mutex_init(&obj->lock);
	obj->type = kVMObjAnon;
	obj->anon.parent = NULL;
	obj->anon.amap = kmem_alloc(sizeof(*obj->anon.amap));
	obj->anon.amap->chunks = NULL;
	obj->anon.amap->curnchunk = 0;
	obj->size = size;
	obj->refcnt = 1;

	return obj;
}

vm_object_t *
vm_object_copy(vm_object_t *obj)
{
	vm_object_t *newobj = kmem_alloc(sizeof *newobj);

	mutex_lock(&obj->lock);

	if (obj->type != kVMObjAnon) {
		fatal("vm_object_copy: only implemented for anons as of yet\n");
	}

	mutex_init(&newobj->lock);
	newobj->refcnt = 1;
	newobj->size = obj->size;
	newobj->type = obj->type;
	if (obj->type == kVMObjAnon) {
		newobj->anon.parent = obj->anon.parent ? obj->anon.parent :
							 NULL;
	}
	newobj->anon.amap = amap_copy(obj->anon.amap);

	mutex_unlock(&obj->lock);

	return newobj;
}

void
vm_object_retain(vm_object_t *obj)
{
	obj->refcnt++;
}

void
vm_object_release(vm_object_t *obj)
{
	if (--obj->refcnt > 0)
		return;

	if (obj->type == kVMObjAnon)
		amap_release(obj->anon.amap);
	else
		fatal("vm_object_release: only implemented for anons\n");

	kmem_free(obj, sizeof(*obj));
}
