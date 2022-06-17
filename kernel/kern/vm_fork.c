#include "kern/queue.h"
#include "liballoc.h"
#include "vm.h"

vm_map_t *vm_map_fork(vm_map_t *map)
{
        vm_map_t *newmap = kmalloc(sizeof *newmap);
        vm_map_entry_t *ent;

        newmap->pmap = pmap_new();
        TAILQ_INIT(&newmap->entries);
	newmap->lock = 0;
        newmap->type = kVMMapUser;

        if (map == kmap)
                return newmap; /* nothing to inherit */

        TAILQ_FOREACH(ent, &map->entries, entries) {
                if(ent->inheritance == kVMMapEntryInheritShared) {
                        vm_map_entry_t * newent = kmalloc(sizeof *newent);

                        newent->inheritance = kVMMapEntryInheritShared;
                        newent->obj = ent->obj;
                        newent->obj->refcnt++;
                        newent->size = ent->size;
                        newent->vaddr = ent->vaddr;
                }
        }
}