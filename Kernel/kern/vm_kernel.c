/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file vm_kernel.c
 * @brief Management of the kernel's virtual address space.
 */

#include <libkern/klib.h>

#include "kern/vmem.h"
#include "liballoc.h"
#include "machine/vm_machdep.h"
#include "vm.h"
#include "vmem_impl.h"

/** Kernel wired memory. */
vmem_t vm_kernel_wired;

static int
internal_allocwired(vmem_t *vmem, vmem_size_t size, vmem_flag_t flags,
    vmem_addr_t *out)
{
	int r;

	assert(vmem == &kmap.vmem);

	r = vmem_xalloc(vmem, size, 0, 0, 0, 0, 0, flags, out);
	if (r < 0) {
		fatal("vmem_xalloc returned %d\n", r);
		return r;
	}

	for (int i = 0; i < size - 1; i += PGSIZE) {
		vm_page_t *page = vm_pagealloc(flags & kVMemSleep);
		pmap_enter_kern(kmap.pmap, page->paddr, (vaddr_t)*out + i,
		    kVMAll);
		vmstat.pgs_kmem++;
	}

	return 0;
}

static void
internal_freewired(vmem_t *vmem, vmem_addr_t addr, vmem_size_t size)
{
	int r;

	assert(vmem == &kmap.vmem);

	r = vmem_xfree(vmem, addr, size);
	if (r < 0) {
		kprintf("internal_freewired: vmem returned %d\n", r);
		return;
	}
	r = size;

	for (int i = 0; i < r; i += PGSIZE) {
		spl_t	   spl = splvm();
		vm_page_t *page;
		page = pmap_unenter_kern(&kmap, (vaddr_t)addr + i);
		vmstat.pgs_kmem--;
		vmstat.pgs_wired--;
		VM_PAGE_QUEUES_LOCK();
		TAILQ_REMOVE(&pg_wireq, page, queue);
		vm_pagefree(page);
		VM_PAGE_QUEUES_UNLOCK();
		splx(spl);
	}
}

void
vm_kernel_dump()
{
	vmem_dump(&kmap.vmem);
	vmem_dump(&vm_kernel_wired);
}

void
vm_kernel_init()
{
	// char *test;

	vmem_earlyinit();
	vmem_init(&kmap.vmem, "kernel-va", KHEAP_BASE, KHEAP_SIZE, PGSIZE, NULL,
	    NULL, NULL, 0, kVMemBootstrap, kSPLVM);
	vmem_init(&vm_kernel_wired, "kernel-wired", 0, 0, PGSIZE,
	    internal_allocwired, internal_freewired, &kmap.vmem, 0,
	    kVMemBootstrap, kSPLVM);

	kmap.vmem.flags = 0;
	vm_kernel_wired.flags = 0;

#if 0
	test = kmalloc(64);
	strcpy(test, "hello\n");
	kprintf(test);
	kfree(test);
#endif
}

vaddr_t
vm_kalloc(size_t npages, int wait)
{
	vmem_addr_t addr;
	int	    flags;
	int	    r;

	flags = wait & 0x1 ? kVMemSleep : kVMemNoSleep;
	flags |= wait & 0x2 ? kVMemBootstrap : 0;
	r = vmem_xalloc(&vm_kernel_wired, npages * PGSIZE, 0, 0, 0, 0, 0, flags,
	    &addr);
	if (r == 0)
		return (vaddr_t)addr;
	else
		return NULL;
}

void
vm_kfree(vaddr_t addr, size_t npages)
{
	vmem_xfree(&vm_kernel_wired, (vmem_addr_t)addr, npages * PGSIZE);
}
