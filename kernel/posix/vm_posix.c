#include <errno.h>

#include "kern/vm.h"
#include "posix_proc.h"
#include "vm_posix.h"

int
vm_mmap(void **addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	bool isfd = !(flags & MAP_ANON);
    posix_proc_t * proc = CURPXPROC();

	if (flags & MAP_FIXED && PGROUNDDOWN(*addr) != (uintptr_t)*addr)
		return -EINVAL; /* must be page-aligned */

	kprintf("mmap request: addr %p, len %lu, prot %d, flags %d, fd %d, "
		"offs %ld\n",
	    *addr, len, prot, flags, fd, offset);


	return -ENOTSUP;
}