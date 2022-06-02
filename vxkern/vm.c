#include "liballoc.h"
#include "vm.h"
#include "vxkern.h"

void vm_init(paddr_t kphys)
{
	vm_map_t *map;
	vm_map_entry_t * entries[2];
	vm_object_t * objs[2];

	kprintf("vm_init\n");

	map = kcalloc(sizeof *map, 1);
	entries[0] = kcalloc(sizeof **entries, 1);
	entries[1] = kcalloc(sizeof **entries, 1);
	objs[0] = kcalloc(sizeof **objs, 1);
	objs[1] = kcalloc(sizeof **objs, 1);

	TAILQ_INIT(&map->entries);
	TAILQ_INSERT_TAIL(&map->entries, entries[0], entries);
	TAILQ_INSERT_TAIL(&map->entries, entries[1], entries);

	/* kernel virtual mapping */
	entries[0]->vaddr = (void*)0xffffffff80000000;
	entries[0]->length = 0x80000000;
	entries[0]->obj = objs[0];
	objs[0]->gen.phys = kphys;
	objs[0]->gen.length = 0x80000000;

	/* hhdm */
	entries[1]->vaddr = (void*)0xffff80000000;
	entries[1]->length = 0x100000000;
	entries[1]->obj = objs[1];
	objs[1]->gen.phys = 0x0;
	objs[1]->gen.length = 0x80000000;

	kprintf("vm_init done\n");
}