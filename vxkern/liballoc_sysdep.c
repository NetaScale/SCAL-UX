#include "liballoc.h"
#include "vm.h"
#include "vxkern.h"

int
liballoc_lock()
{
	return 0;
}

int
liballoc_unlock()
{
	return 0;
}

void *
liballoc_alloc(size_t pages)
{
	paddr_t paddr = pmap_alloc_page(pages);
	if (paddr == NULL) {
		kprintf("failed to get pages\n");
		while (true) {
		}
	}
	kprintf("got page %p/%p\n", paddr, P2V(paddr));
	return P2V(paddr);
}

int
liballoc_free(void *ptr, size_t pages)
{
	kprintf("unimplemented liballoc_free\n");
	return 0;
}
