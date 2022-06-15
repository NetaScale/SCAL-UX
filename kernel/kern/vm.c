#include <string.h>

#include "kern/kern.h"
#include "kern/liballoc.h"
#include "kern/queue.h"
#include "kern/vm.h"
#include "posix/vfs.h"

vm_map_t *kmap;

void
vm_init(paddr_t kphys)
{
	vm_object_t *objs[3];
	vaddr_t vaddr;

	kprintf("vm_init\n");

	kmap = vm_map_new();
	objs[0] = kcalloc(sizeof **objs, 1);
	objs[1] = kcalloc(sizeof **objs, 1);
	objs[2] = kcalloc(sizeof **objs, 1);

	/* kernel virtual mapping */
	objs[0]->gen.phys = kphys;
	objs[0]->size = 0x80000000;
	vaddr = (vaddr_t)0xffffffff80000000;
	vm_map_object(kmap, objs[0], &vaddr, 0x80000000, false);

	/* hhdm */
	objs[1]->gen.phys = 0x0;
	objs[1]->size = 0x100000000;
	vaddr = (vaddr_t)0xffff800000000000;
	vm_map_object(kmap, objs[1], &vaddr, 0x100000000, false);

#if 0
	/* identity map from 0x1000 to 0x100000000 - for limine terminal */
	objs[2]->gen.phys = (paddr_t)0x1000;
	objs[2]->size = 0xfffff000;
	vaddr = (vaddr_t)0x1000;
	vm_map_object(kmap, objs[2], &vaddr, 0xfffff000, false);
#endif

	kprintf("vm_init done\n");
}

vm_map_t *
vm_map_new()
{
	vm_map_t *map = kcalloc(sizeof *map, 1);

	TAILQ_INIT(&map->entries);
	map->pmap = pmap_new();
	map->lock = 0;

	return map;
}

void
vm_map_print(vm_map_t *map)
{
	vm_map_entry_t *entry;
	lock(&map->lock);
	TAILQ_FOREACH (entry, &map->entries, entries) {
		kprintf("%p-%p obj %p (type %d size %p)\n", entry->vaddr,
		    entry->vaddr + (entry->size - 1), entry->obj,
		    entry->obj->type, entry->obj->size);
	}
	unlock(&map->lock);
}

int
vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size,
    bool immediate)
{
	/* TODO: !!! there is a cyclic dependency between vm_allocate and
	 * kmalloc*/
	vm_object_t *obj = kcalloc(sizeof *obj, 1);
	int r;

	size = PGROUNDUP(size);

	obj->type = kVMAnonymous;
	obj->anon.amap = kmalloc(sizeof(*obj->anon.amap));
	obj->anon.pagerops = &vm_anon_pagerops;
	TAILQ_INIT(&obj->anon.amap->pages);
	obj->anon.amap->refcnt = 1;
	obj->size = size;
	r = vm_map_object(map, obj, vaddrp, size, false);
	if (r < 0) {
		kprintf("vm_allocate failed\n");
		kfree(obj);
		return r;
	}

	if (immediate) {
		/* fill it up with pages */
	}

	if (out)
		*out = obj;

	kprintf("allocated object %p size %p at vaddr %p\n", obj, size,
	    *vaddrp);

	return 0;
}

int
vm_object_new_anon(vm_object_t **out, size_t size, vm_pagerops_t *pagerops,
    vnode_t *vn)
{
	vm_object_t *obj = kcalloc(sizeof *obj, 1);

	assert(obj);

	obj->type = kVMAnonymous;
	obj->anon.amap = kmalloc(sizeof(*obj->anon.amap));
	TAILQ_INIT(&obj->anon.amap->pages);
	obj->anon.amap->refcnt = 1;
	obj->anon.pagerops = pagerops;
	obj->size = size;
	obj->anon.vnode = vn;

	*out = obj;

	return 0;
}

int
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size,
    bool copy)
{
	vm_map_entry_t *entry = kcalloc(sizeof *entry, 1);
	vm_map_entry_t *entry_before; /* entry to insert after */
	vaddr_t vaddr;

	assert(vaddrp);
	vaddr = *vaddrp;

	kprintf("vm_map_object virt: %p size: 0x%lx copy: %d\n", vaddr, size,
	    copy);

	lock(&map->lock);
	entry_before = TAILQ_FIRST(&map->entries);
	entry->flags |= copy ? kVMMapEntryCopyOnWrite : 0;

	/* didn't bother testing this placement code, hope it works */
	if (vaddr == VADDR_MAX) {
		/* find a suitable place */
		/* PORT: don't encode memory map of an arch here */
		vaddr_t min = (vaddr_t)(map->type == kVMMapKernel ?
			      0xffff80000000 :
			      0x100000000);
		vaddr_t max = (vaddr_t)(map->type == kVMMapKernel ?
			      UINT64_MAX :
			      0x7fffffffffff);
		vaddr_t entry_end;

		if (!entry_before) {
			vaddr = min;
			goto next;
		}

	loop:
		entry_end = entry_before->vaddr + entry_before->size;

		kprintf("entry_end: %p entry_end + size: %p\n", entry_end,
		    entry_end + size);

		if (entry_end + size >= max) {
			kprintf("nowhere to map it, failing\n");
			return -1;
		}

		if (entry_end >= min) {
			vm_map_entry_t *entry2 = TAILQ_NEXT(entry_before,
			    entries);
			if (!entry2 || entry2->vaddr >= entry_end + size) {
				vaddr = entry_end;
				goto next;
			}
		}

		entry_before = TAILQ_NEXT(entry_before, entries);

		if (!entry_before) {
			kprintf("nowhere to map it, failing\n");
			return -1;
		} else
			goto loop;
	} else {
		vm_map_entry_t *prev = NULL;

		if (!entry_before)
			goto next;

		while (entry_before && entry_before->vaddr < vaddr) {
			prev = entry_before;
			entry_before = TAILQ_NEXT(entry_before, entries);
		}

		entry_before = prev;
	}

next:
	kprintf("  - choosing vaddr %p\n", vaddr);
	entry->flags = 0;
	entry->vaddr = vaddr;
	entry->size = size;
	entry->obj = obj;

	if (entry_before)
		TAILQ_INSERT_AFTER(&map->entries, entry_before, entry, entries);
	else
		TAILQ_INSERT_HEAD(&map->entries, entry, entries);

	if (obj->type == kVMGeneric)
		pmap_map(map->pmap, obj->gen.phys, vaddr, size);

	unlock(&map->lock);

	*vaddrp = vaddr;

	return 0;
}

/*
 * Find an entry for a given virtual address within a map.
 *
 * @param map LOCKED map to search.
 */
static vm_map_entry_t *
find_entry_for_addr(vm_map_t *map, vaddr_t vaddr)
{
	vm_map_entry_t *entry;
	TAILQ_FOREACH (entry, &map->entries, entries) {
		if (vaddr >= entry->vaddr && vaddr < entry->vaddr + entry->size)
			return entry;
	}
	return NULL;
}

/**
 * Find the amap entry representing an offset within an amap.
 * @param amap LOCKED amap
 */
static vm_amap_entry_t *
find_anon(vm_amap_t *amap, vm_anon_t **prevp, voff_t off)
{
	vm_amap_entry_t *prev = NULL;
	vm_amap_entry_t *anon;
	TAILQ_FOREACH (anon, &amap->pages, entries) {
		if (anon->anon->offs == off)
			return anon;
	}
	return NULL;
}

/**
 * Handle a pagefault associated with an anon/vnode object.
 * @param map UNLOCKED map
 * @param obj LOCKED object
 * @param vaddr faulting address
 * @param poff offset of faulting address
 * @param needcopy whether a new anonymous page is needed (i.e. COW write)
 */
static void
vm_fault_handle_anon(vm_map_t *map, vm_object_t *obj, vaddr_t vaddr, voff_t off,
    bool needcopy)
{
	vm_anon_t *anon;

	obj->anon.pagerops->get(obj, off, &anon, needcopy);
	pmap_map(map->pmap, anon->physpg->paddr, vaddr, PGSIZE);
	pmap_invlpg(vaddr);
}

/* handle a page fault */
int
vm_fault(vm_map_t *map, vaddr_t vaddr, bool write)
{
	vm_map_entry_t *entry;
	vm_object_t *obj;
	voff_t off;
	bool needcopy;

	kprintf("vm_fault vaddr: %p write: %d\n", vaddr, write);

	lock(&map->lock);
	entry = find_entry_for_addr(map, vaddr);
	if (entry)
		lock(&entry->obj->lock);
	unlock(&map->lock);

	if (!entry) {
		kprintf("no entry for vaddr %p\n", vaddr);
		vm_map_print(map);
		return -1;
	}

	obj = entry->obj;
	if (obj->type == kVMGeneric) {
		kprintf("unexpected object type for obj %p\n", obj);
		vm_map_print(map);
		return -1;
	}
	off = vaddr - entry->vaddr;
	needcopy = (entry->flags & kVMMapEntryCopyOnWrite) && write;
	vm_fault_handle_anon(map, obj, vaddr, off, needcopy);

	unlock(&obj->lock);

	return 0;
}

static void
copyphyspage(paddr_t dst, paddr_t src)
{
	vaddr_t dstv = HHDM_BASE + dst, srcv = HHDM_BASE + src;
	memcpy(dstv, srcv, PGSIZE);
}

static int
anon_get(vm_object_t *obj, voff_t off, vm_anon_t **out, bool needcopy)
{
	vm_amap_entry_t *aent = find_anon(obj->anon.amap, NULL, off);
	vm_anon_t *anon = aent ? aent->anon : NULL;

	if (anon) {
		kprintf("map in an existing anon\n");
		if (needcopy) {
			vm_anon_t *newanon = kmalloc(sizeof *anon);
			newanon->offs = anon->offs;
			newanon->refcnt = 1;
			newanon->physpg = vm_alloc_page();
			anon->refcnt--;
			copyphyspage(newanon->physpg->paddr,
			    anon->physpg->paddr);
			anon = newanon;
		}
	} else {
		/*
		 * needcopy irrelevant; we're getting a brand new page for it
		 * regardless  -- FIXME: but we're linking it into our amap so
		 * this comment is now incorrect. Only link it in if we must.
		 */
		kprintf("make a new anon for pg  %ld in amap\n", off);
		anon = kmalloc(sizeof *anon);
		anon->physpg = vm_alloc_page();
		anon->refcnt = 1;
		anon->offs = off;
		aent = kmalloc(sizeof *aent);
		aent->anon = anon;
		TAILQ_INSERT_TAIL(&obj->anon.amap->pages, aent, entries);
	}

	*out = anon;

	return 0;
}

static int
vnode_get(vm_object_t *obj, voff_t off, vm_anon_t **out, bool needcopy)
{
	vnode_t *vn = obj->anon.vnode;
	vm_amap_entry_t *aent = find_anon(vn->vmobj->anon.amap, NULL, off);
	vm_anon_t *anon = aent ? aent->anon : NULL;

	if (!anon)
		assert(vn->ops->getpage(vn, off, &anon, 0) == 0);

	if (needcopy) {
		vm_anon_t *newanon = kmalloc(sizeof *anon);
		newanon->offs = anon->offs;
		newanon->refcnt = 1;
		newanon->physpg = vm_alloc_page();
		anon->refcnt--;
		copyphyspage(newanon->physpg->paddr, anon->physpg->paddr);
		anon = newanon;
	}
	*out = anon;

	return 0;
}

vm_pagerops_t vm_anon_pagerops = {
	.get = anon_get,
};

vm_pagerops_t vm_vnode_pagerops = {
	.get = vnode_get,
};