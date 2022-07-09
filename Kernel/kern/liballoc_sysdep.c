#include <libkern/klib.h>

#include "liballoc.h"
#include "lock.h"
#include "vm.h"

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
	void *addr = vm_kalloc(pages, false);
	return addr;
}

int
liballoc_free(void *ptr, size_t pages)
{
	vm_kfree(ptr, pages);
	return 0;
}
