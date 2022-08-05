/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <errno.h>

#include "kern/vm.h"
#include "libkern/klib.h"
#include "proc.h"
#include "vm_posix.h"

int
vm_mmap(proc_t *proc, void **addr, size_t len, int prot, int flags, int fd,
    off_t offset)
{
	if (flags & MAP_FIXED && PGROUNDDOWN(*addr) != (uintptr_t)*addr)
		return -EINVAL; /* must be page-aligned */
	else if (PGROUNDDOWN(offset) != offset)
		return -EINVAL;

#if DEBUG_SYSCALLS == 0
	kprintf("VM_POSIX: mmap addr %p, len %lu, prot %d, flags %d, fd %d, "
		"offs %ld\n",
	    *addr, len, prot, flags, fd, offset);
#endif

	if (!(flags & MAP_ANON)) {
		file_t *file;

		if (fd == -1 || fd > 64)
			return -EBADF;

		file = proc->files[fd];
		if (!file)
			return -EBADF;

		/* TODO(low): introduce a vnode mmap operation (for devices) */

		return vm_map_object(proc->task->map, file->vn->vmobj, addr,
		    len, offset, flags & MAP_PRIVATE);
	} else {
		return vm_allocate(proc->task->map, NULL, addr, len);
	}

	return -ENOTSUP;
}
