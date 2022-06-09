#include <sys/vm.h>
#include <sys/vxkern.h>

#include "liballoc.h"
#include "sys/queue.h"

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
	objs[0]->gen.length = 0x80000000;
	vaddr = (vaddr_t)0xffffffff80000000;
	vm_map_object(kmap, objs[0], &vaddr, 0x80000000);

	/* hhdm */
	objs[1]->gen.phys = 0x0;
	objs[1]->gen.length = 0x100000000;
	vaddr = (vaddr_t)0xffff800000000000;
	vm_map_object(kmap, objs[1], &vaddr, 0x100000000);

	/* identity map from 0x1000 to 0x100000000 - for limine terminal */
	objs[2]->gen.phys = (paddr_t)0x1000;
	objs[2]->gen.length = 0xfffff000;
	vaddr = (vaddr_t)0x1000;
	vm_map_object(kmap, objs[2], &vaddr, 0xfffff000);

	vaddr = VADDR_MAX;
	vm_map_object(kmap, objs[2], &vaddr, 0x8000);

	kprintf("vm_init done\n");
}

vm_map_t *
vm_map_new()
{
	vm_map_t *map = kcalloc(sizeof *map, 1);

	TAILQ_INIT(&map->entries);
	map->pmap = pmap_new();

	return map;
}

int
vm_allocate(vm_map_t *map, vm_object_t **out, vaddr_t *vaddrp, size_t size,
    bool immediate)
{
	vm_object_t *obj = kcalloc(sizeof *obj, 1);
	int r;

	obj->type = kVMAnonymous;
	obj->anon.amap = kmalloc(sizeof(*obj->anon.amap));
	// obj->anon.size = size;
	obj->anon.off = 0x0;
	TAILQ_INIT(&obj->anon.amap->pages);
	obj->anon.amap->refcnt = 1;
	r = vm_map_object(map, obj, vaddrp, size);
	if (r < 0) {
		kprintf("failed\n");
		kfree(obj);
		return r;
	}

	if (immediate) {
		/* fill it up with pages */
	}

	return 0;
}

int
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddrp, size_t size)
{
	vm_map_entry_t *entry = kcalloc(sizeof *entry, 1);
	vm_map_entry_t *entry_before; /* entry to insert after */
	vaddr_t vaddr = *vaddrp;

	entry_before = TAILQ_FIRST(&map->entries);

	kprintf("vm_map_object virt: %p size: 0x%lx\n", vaddr, size);

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
		while (entry_before && entry_before->vaddr + size <= vaddr)
			entry_before = TAILQ_NEXT(entry_before, entries);
	}

next:
	kprintf("  - choosing vaddr %p\n", vaddr);
	entry->vaddr = vaddr;
	entry->size = size;
	entry->obj = obj;

	if (entry_before)
		TAILQ_INSERT_AFTER(&map->entries, entry_before, entry, entries);
	else
		TAILQ_INSERT_TAIL(&map->entries, entry, entries);

	if (obj->type == kVMGeneric)
		pmap_map(map->pmap, obj->gen.phys, vaddr, size);
		/* FIXME i don't think we need to pmap at all for others. do it
		 * lazily */
#if 0 
	else
		fatal("unhandled obj type %d\n", obj->type);
#endif

	*vaddrp = vaddr;

	return 0;
}

static vm_map_entry_t *
find_entry_for_addr(vm_map_t *map, vaddr_t vaddr)
{
	vm_map_entry_t *entry;
	TAILQ_FOREACH(entry, &map->entries, entries)
	{
		if (vaddr >= entry->vaddr &&
		    vaddr <= entry->vaddr + entry->size)
			return entry;
	}
	return NULL;
}

/* todo order anons, write out prev? do we need it? */
static vm_anon_t *
find_anon(vm_amap_t *amap, vm_anon_t **prevp, voff_t off)
{
	vm_anon_t *prev = NULL;
	vm_anon_t *anon;
	TAILQ_FOREACH(anon, &amap->pages, entries)
	{
		if (anon->offs == off)
			return anon;
	}
	return NULL;
}

/* handle a page fault */
void
vm_fault(vm_map_t *map, vaddr_t vaddr, bool write)
{
	vm_map_entry_t *entry = find_entry_for_addr(map, vaddr);
	vm_object_t *obj;
	size_t poff;

	kprintf("\nvm_fault vaddr: %p write: %d\n", vaddr, write);

	if (!entry)
		fatal("no entry for vaddr %p\n", vaddr);

	obj = entry->obj;

	if (obj->type == kVMGeneric)
		fatal("unhandeld fault generic");

	kprintf("entry vaddr: %p\n", entry->vaddr);
	// kprintf("offs: %ld\n", vaddr - entry->vaddr);
	poff = (vaddr - entry->vaddr) / PGSIZE;

	kprintf("page %ld\n", poff);

	/*
	 * mapping type needs to store whether COW? where though? in object? in
	 * mapentry? if in object - what if we multiply map the same object? i
	 * am therefore sure it must be in the mapentry. thus we can map a
	 * single vnode multiply.
	 */

	//if (!write) {
		voff_t off = poff + obj->anon.off / PGSIZE;
		vm_anon_t *prev;
		vm_anon_t *anon = find_anon(obj->anon.amap, &prev, off);
		if (anon) {
			kprintf("map in an existing anon");
		} else {
			kprintf("make a new anon for pg  %ld in amap\n", poff);
			anon = kmalloc(sizeof *anon);
			anon->physpg = vm_alloc_page();
			anon->refcnt = 1;
			TAILQ_INSERT_TAIL(&obj->anon.amap->pages, anon,
			    entries);
			pmap_map(map->pmap, anon->physpg->paddr, vaddr, PGSIZE);
			pmap_invlpg(vaddr);
		}
	//}

	kprintf("\n");
}