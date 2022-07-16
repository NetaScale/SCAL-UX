/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/*
 * Entry point of the POSIX subsystem.
 */

#include <fcntl.h>
#include <string.h>

#include "kern/task.h"
#include "libkern/klib.h"
#include "machine/spl.h"
#include "proc.h"
#include "vfs.h"

typedef struct ustar_hdr {
	char filename[100];
	char mode[8];
	char ownerid[8];
	char groupid[8];

	char size[12];
	char mtime[12];

	char checksum[8];
	char type;
	char link[100];

	char ustar[6];
	char version[2];

	char owner[32];
	char group[32];

	char dev_major[8];
	char dev_minor[8];

	char prefix[155];
} __attribute__((__packed__)) ustar_hdr_t;

enum {
	kUStarNormal = '0',
	kUStarDirectory = '5',
};

static void
start_init(void *bin)
{
	/*
	 * .globl start
	 * start:
	 *   movq $2, %rax ; PXSYS_exec
	 *   movq $init, %rdi
	 *   int $0x80
	 * init:
	 *   .string "/init\0"
	 */
	static uint8_t initcode[] = { 0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00,
		0x48, 0xc7, 0xc7, 0x10, 0x00, 0x40, 0x00, 0xcd, 0x80, 0x2f,
		0x69, 0x6e, 0x69, 0x74, 0x00, 0x00 };
	task_t	       *task1 = task_fork(&task0);
	task1->pxproc = kmalloc(sizeof(proc_t));
	task1->pxproc->task = task1;
	memset(task1->pxproc->files, 0x0, sizeof task1->pxproc->files);
	thread_t *thr1 = thread_new(task1, false);
	vaddr_t	  vaddr = (vaddr_t)0x400000;

	assert(vm_allocate(task1->map, NULL, &vaddr, 4096) == 0);
#if 0
	/* fault it in */
	vm_fault(task1->map, 0x0, true);
	memcpy(P2V(pmap_trans(task1->map->pmap, 0x0)), initcode,
	    sizeof(initcode));
#else
	vm_activate(task1->map);
	/* memcpy it in */
	vm_activate(&kmap);
#endif

#if 0
	assert(sys_open(task1->pxproc, "/dev/console", O_RDWR) >= 0);
	assert(sys_open(task1->pxproc, "/dev/console", O_RDWR) >= 0);
	assert(sys_open(task1->pxproc, "/dev/console", O_RDWR) >= 0);

	thr1->pcb.frame.rsp = (uintptr_t)kmalloc(4096) + 4096;
	thr1->pcb.frame.rip = 0x400000;
	thr1->pcb.frame.rdi = 0x0;
	thr1->pcb.frame.rbp = 0x0;
	thread_run(thr1);
#endif
	for (;;) {
		asm("hlt");
	}
}

static int
oct2i(unsigned char *s, int size)
{
	int n = 0;
	while (size-- > 0) {
		n *= 8;
		n += *s - '0';
		s++;
	}
	return n;
}

void
posix_main(void *initbin, size_t size)
{
	kprintf("POSIX subsystem is going up\n");

	/* reset system priority level, everything should now be ready to go */
	spl0();

	tmpfs_mountroot();
	vnode_t *tvn = NULL;

	root_vnode->ops->mkdir(root_vnode, &root_dev, "dev");

	void autoconf();
	autoconf();

	kprintf("unpacking initrd...\n");
	for (size_t i = 0; i < size;) {
		ustar_hdr_t *star = initbin + i;
		int	     fsize = oct2i((unsigned char *)star->size, 11);

		if (!*star->filename)
			break;
		else if (!*(star->filename + 2))
			goto next;

		switch (star->type) {
		case kUStarDirectory: {
			int	 r;
			vnode_t *vn;

			r = vfs_lookup(root_vnode, &vn, star->filename,
			    kLookupMkdir);
			if (r < 0) {
				kprintf("failed to make dir: %d\n", -r);
			}
			break;
		}

		case kUStarNormal: {
			int	 r;
			vnode_t *vn;

			r = vfs_lookup(root_vnode, &vn, star->filename,
			    kLookupCreat);
			if (r < 0) {
				kprintf("failed to make file: %d\n", -r);
			}

			r = vfs_write(vn, initbin + i + 512, fsize, 0);
			break;
		}

		default:
			kprintf("unexpected type %c\n", star->type);
		}

	next:
		i += 512 + ROUNDUP(fsize, 512);
	}

	kprintf("starting init process...\n");
	start_init(initbin);

#if 0
	waitq_t wq;
	waitq_init(&wq);
	kprintf("waitq awaiting...\n");
	int x = waitq_await(&wq, 25, 1000);
	kprintf("waitq X: %d\n", x); /* will timeout */
#endif

	kprintf("Done!\n");

	for (;;)
		asm volatile("pause");
}
