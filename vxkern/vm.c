#include "liballoc.h"
#include "vm.h"
#include "vxkern.h"

void
vm_init(paddr_t kphys)
{
	vm_map_t *map;
	vm_object_t *objs[2];

	kprintf("vm_init\n");

	map = vm_map_new();
	objs[0] = kcalloc(sizeof **objs, 1);
	objs[1] = kcalloc(sizeof **objs, 1);

	/* kernel virtual mapping */
	objs[0]->gen.phys = kphys;
	objs[0]->gen.length = 0x80000000;
	vm_map_object(map, objs[0], (void *)0xffffffff80000000, 0x80000000);

	/* hhdm */
	objs[1]->gen.phys = 0x0;
	objs[1]->gen.length = 0x100000000;
	vm_map_object(map, objs[1], (void *)0xffff800000000000, 0x100000000);

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
vm_map_object(vm_map_t *map, vm_object_t *obj, vaddr_t *vaddr, size_t size)
{
	vm_map_entry_t *entry = kcalloc(sizeof *entry, 1);

	/* checks should go here... overlaps etc */

	kprintf("vm_map_object virt: 0x%p size: 0x%lx\n", vaddr, size);

	/* no support for anywhere yet */
	assert(vaddr != NULL);

	entry->vaddr = vaddr;
	entry->size = size;
	entry->obj = obj;

	TAILQ_INSERT_TAIL(&map->entries, entry, entries);

	if (obj->type == kVMGeneric) {
		pmap_map(map->pmap, obj->gen.phys, vaddr, size);
	} else {
		fatal("unhandled obj type %d\n", obj->type);
	}

	return 0;
}