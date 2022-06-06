#include "liballoc.h"
#include <sys/vm.h>
#include <sys/vxkern.h>

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
		fatal("failed to get pages\n");
	}
	return P2V(paddr);
}

int
liballoc_free(void *ptr, size_t pages)
{
	kprintf("unimplemented liballoc_free\n");
	return 0;
}
