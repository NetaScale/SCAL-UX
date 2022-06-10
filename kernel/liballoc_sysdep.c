#include <sys/vm.h>
#include <sys/vxkern.h>

#include "liballoc.h"

static spinlock_t alloclock;

int
liballoc_lock()
{
	lock(&alloclock);
	return 0;
}

int
liballoc_unlock()
{
	unlock(&alloclock);
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
