#include <string.h>

#include "liballoc.h"
#include "posix/vfs.h"
#include "vm.h"

static int
anon_get(vm_object_t *obj, voff_t off, vm_anon_t **out, bool write)
{
	vm_amap_entry_t *aent = amap_find_anon(obj->anon.amap, NULL, off);
	vm_anon_t *anon = aent ? aent->anon : NULL;
	int r = 0;

	if (!anon) {
#ifdef DEBUG_VM
		kprintf("make a new anon for pg  %ld in amap\n", off);
#endif
		anon = kmalloc(sizeof *anon);
		anon->physpg = vm_alloc_page();
		anon->refcnt = 1;
		anon->offs = off;
		aent = kmalloc(sizeof *aent);
		aent->anon = anon;
		TAILQ_INSERT_TAIL(&obj->anon.amap->pages, aent, entries);
	} else if (anon && anon->refcnt > 1 && write) {
		vm_anon_t *newanon = anon_copy(anon);
		anon->refcnt--;
		anon = newanon;
		aent->anon = anon;
	}

	*out = anon;

	return r;
}

static int
vnode_get(vm_object_t *obj, voff_t off, vm_anon_t **out, bool write)
{
	return obj->anon.vnode->ops->getpage(obj->anon.vnode, off, out, 0);
}

vm_pagerops_t vm_anon_pagerops = {
	.get = anon_get,
};

vm_pagerops_t vm_vnode_pagerops = {
	.get = vnode_get,
};
