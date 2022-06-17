#include <errno.h>

#include "kern/vm.h"
#include "posix_proc.h"
#include "vm_posix.h"

int
vm_mmap(posix_proc_t *proc, void **addr, size_t len, int prot, int flags,
    int fd, off_t offset)
{
	if (flags & MAP_FIXED && PGROUNDDOWN(*addr) != (uintptr_t)*addr)
		return -EINVAL; /* must be page-aligned */
	else if (PGROUNDDOWN(offset) != offset)
		return -EINVAL;

	kprintf("mmap request: addr %p, len %lu, prot %d, flags %d, fd %d, "
		"offs %ld\n",
	    *addr, len, prot, flags, fd, offset);

	if (!(flags & MAP_ANON)) {
		file_t *file;
		vm_object_t *obj;
		int r;

		if (fd == -1 || fd > 64)
			return -EBADF;

		file = proc->files[fd];
		if (!file)
			return -EBADF;

		if (!file->fops->mmap)
			return -ENODEV;

		r = file->fops->mmap(file, offset, len, flags & MAP_PRIVATE,
		    &obj);
		if (r < 0)
			return r;

		return vm_map_object(proc->proc->map, obj, addr, len,
		    flags & MAP_PRIVATE);
	} else {
		return vm_allocate(proc->proc->map, NULL, addr, len, false);
	}

	return -ENOTSUP;
}