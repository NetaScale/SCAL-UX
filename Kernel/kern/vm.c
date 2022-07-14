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

#include "liballoc.h"
#include "libkern/klib.h"
#include "machine/vm_machdep.h"
#include "sys/queue.h"
#include "vm.h"
#include "vmem.h"

struct vm_page_queue pg_freeq = TAILQ_HEAD_INITIALIZER(pg_freeq),
		     pg_activeq = TAILQ_HEAD_INITIALIZER(pg_activeq),
		     pg_inactiveq = TAILQ_HEAD_INITIALIZER(pg_inactiveq);
vm_map_t kmap;

/*
 * note: ultimately a vm_object will always have a tree of vm_page_t's. and then
 * if you have some vnode vm_object and create a copy, it initially adds
 * vm_pages from the vnode when read-faults occur? need to consider this further
 * - it violates the principle that a vm_page_t is mapped either in one amap or
 * in one object???
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
	vm_amap_t	  *newamap = kmalloc(sizeof *newamap);
	vm_amap_entry_t *ent;

	TAILQ_INIT(&newamap->anons);
	TAILQ_FOREACH (ent, &amap->anons, entries) {
		vm_amap_entry_t *newent = kmalloc(sizeof *newent);
		newent->anon = ent->anon;
		newent->anon->refcnt++;
		TAILQ_INSERT_TAIL(&newamap->anons, newent, entries);
	}

	return newamap;
}

/**
 * Find the amap entry representing an offset within an amap.
 */
vm_amap_entry_t *
amap_find_anon(vm_amap_t *amap, vm_anon_t **prevp, voff_t off)
{
	vm_amap_entry_t *anon;
	TAILQ_FOREACH (anon, &amap->anons, entries) {
		if (anon->anon->offs == off)
			return anon;
	}
	return NULL;
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

	if (obj)
		*out = obj;

	return 0;
}

vm_object_t *
vm_aobj_new(size_t size)
{
	vm_object_t *obj = kmalloc(sizeof *obj);

	obj->type = kVMObjAnon;
	obj->anon.amap = kmalloc(sizeof(*obj->anon.amap));
	TAILQ_INIT(&obj->anon.amap->anons);
	obj->size = size;

	return obj;
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
