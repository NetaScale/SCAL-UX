
#include <string.h>

#include "liballoc.h"
#include "vm.h"

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

static void
copyphyspage(paddr_t dst, paddr_t src)
{
	vaddr_t dstv = HHDM_BASE + dst, srcv = HHDM_BASE + src;
	memcpy(dstv, srcv, PGSIZE);
}

vm_anon_t *
anon_copy(vm_anon_t *anon)
{
	vm_anon_t *newanon = kmalloc(sizeof *newanon);
	newanon->refcnt = 1;
	newanon->lock = 0;
	newanon->offs = anon->offs;
	newanon->physpg = vm_alloc_page();
	copyphyspage(newanon->physpg->paddr, anon->physpg->paddr);
	return newanon;
}

/**
 * Handle a pagefault associated with an anon/vnode object.
 * @param map UNLOCKED map
 * @param obj LOCKED object
 * @param vaddr faulting address
 * @param poff offset of faulting address
 * @param write whether this is a write access
 */
static void
vm_fault_handle_anon(vm_map_t *map, vm_object_t *obj, vaddr_t vaddr, voff_t off,
    bool write)
{
	vm_anon_t *anon = NULL;
	vm_amap_entry_t *aent;
	int r;

	if (obj->anon.parent) {
		/* check for a local page first */
		aent = amap_find_anon(obj->anon.amap, NULL, off);
		if (anon) {
			if (aent && write && aent->anon->refcnt > 1) {
				vm_anon_t *newanon = anon_copy(anon);
				anon->refcnt--;
				aent->anon = newanon;
			}
		} else {
			/*
			 * retrieve page from parent. we don't pass write as the
			 * parent is probably a vnode cache so we will simply
			 * copy the thing. we don't ref it either as we are
			 * copying it unconditionally for now.
			 */
			r = obj->anon.parent->anon.pagerops->get(
			    obj->anon.parent, off, &anon, false);
			if (r < 0)
				fatal("vm_fault_handle_anon error 3");

			vm_anon_t *newanon = anon_copy(anon);

			anon = newanon;
			aent = kmalloc(sizeof *aent);
			aent->anon = newanon;
			TAILQ_INSERT_TAIL(&obj->anon.amap->pages, aent,
			    entries);
		}

	} else {
		assert(obj->anon.pagerops->get(obj, off, &anon, write) == 0);
	}

	pmap_map(map->pmap, anon->physpg->paddr, vaddr, PGSIZE,
	    write ? (kVMRead | kVMWrite) : kVMRead);

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

#ifdef DEBUG_VM
	kprintf("vm_fault vaddr: %p write: %d\n", vaddr, write);
#endif

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
	vm_fault_handle_anon(map, obj, vaddr, off, write);

	unlock(&obj->lock);

	return 0;
}
