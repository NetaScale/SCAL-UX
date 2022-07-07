#include <libkern/klib.h>

#include "liballoc.h"
#include "lock.h"
#include "vm.h"
#include "vmem_impl.h"

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

extern /** Kernel's virtual address space. */
    vmem_t vm_kernel_va;

extern /** Kernel wired memory. */
    vmem_t vm_kernel_wired;

void *
liballoc_alloc(size_t pages)
{
	void *addr = vm_kalloc(pages, false);
	vmem_dump(&vm_kernel_va);
	vmem_dump(&vm_kernel_wired);
	return addr;
}

int
liballoc_free(void *ptr, size_t pages)
{
	kprintf("unimplemented liballoc_free\n");
	return 0;
}
