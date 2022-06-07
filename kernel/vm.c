#include <sys/vm.h>
#include <sys/vxkern.h>

#include "liballoc.h"
#include "sys/queue.h"

vm_map_t *kmap;

void
vm_init(paddr_t kphys)
{
	vm_object_t *objs[3];

	kprintf("vm_init\n");

	kmap = vm_map_new();
	objs[0] = kcalloc(sizeof **objs, 1);
	objs[1] = kcalloc(sizeof **objs, 1);
	objs[2] = kcalloc(sizeof **objs, 1);

	/* kernel virtual mapping */
	objs[0]->gen.phys = kphys;
	objs[0]->gen.length = 0x80000000;
	vm_map_object(kmap, objs[0], (void *)0xffffffff80000000, 0x80000000);

	/* hhdm */
	objs[1]->gen.phys = 0x0;
	objs[1]->gen.length = 0x100000000;
	vm_map_object(kmap, objs[1], (void *)0xffff800000000000, 0x100000000);

	/* identity map from 0x1000 to 0x100000000 - for limine terminal */
	objs[2]->gen.phys = (paddr_t)0x1000;
	objs[2]->gen.length = 0xfffff000;
	vm_map_object(kmap, objs[2], (paddr_t)0x1000, 0xfffff000);

	vm_map_object(kmap, objs[2], 0x0, 0x8000);

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
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t vaddr, size_t size)
{
	vm_map_entry_t *entry = kcalloc(sizeof *entry, 1);
	vm_map_entry_t *entry_before; /* entry to insert after */

	entry_before = TAILQ_FIRST(&map->entries);

	kprintf("vm_map_object virt: 0x%p size: 0x%lx\n", vaddr, size);

	/* didn't bother testing this placement code, hope it works */
	if (vaddr == NULL) {
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
	else
		fatal("unhandled obj type %d\n", obj->type);

	return 0;
}