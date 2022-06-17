#include "kern/queue.h"
#include "liballoc.h"
#include "vm.h"

/* !! assumptions present that all vm_anons are in-memory */

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
	vm_amap_entry_t *ent;

	newamap->lock = 0;
	newamap->refcnt = 1;
	TAILQ_INIT(&newamap->pages);
	TAILQ_FOREACH (ent, &amap->pages, entries) {
		vm_amap_entry_t *newent = kmalloc(sizeof *newent);
		newent->anon = ent->anon;
		newent->anon->refcnt++;
		TAILQ_INSERT_TAIL(&newamap->pages, newent, entries);
	}

	return newamap;
}

/**
 * Find the amap entry representing an offset within an amap.
 * @param amap LOCKED amap
 */
vm_amap_entry_t *
amap_find_anon(vm_amap_t *amap, vm_anon_t **prevp, voff_t off)
{
	vm_amap_entry_t *anon;
	TAILQ_FOREACH (anon, &amap->pages, entries) {
		if (anon->anon->offs == off)
			return anon;
	}
	return NULL;
}


vm_object_t *
vm_object_copy(vm_object_t *obj)
{
	vm_object_t *newobj = kmalloc(sizeof *newobj);

	assert(obj->type == kVMAnonymous);

	newobj->lock = 0;
	newobj->refcnt = 1;
	newobj->size = obj->size;
	newobj->type = obj->type;
	newobj->anon.parent = obj->anon.parent && obj->anon.parent->anon.vnode ?
	    obj->anon.parent :
	    obj;
	newobj->anon.amap = amap_copy(obj->anon.amap);
	newobj->anon.pagerops = &vm_anon_pagerops;

	return newobj;
}