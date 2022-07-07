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

#include "liballoc.h"
#include "vm.h"
#include "vmem_impl.h"

/** Kernel's virtual address space. */
vmem_t vm_kernel_va;

/** Kernel wired memory. */
vmem_t vm_kernel_wired;

static int
internal_allocwired(vmem_t *vmem, vmem_size_t size, vmem_flag_t flags,
    vmem_addr_t *out)
{
	int r;

	assert(vmem == &vm_kernel_va);

	r = vmem_xalloc(vmem, size, 0, 0, 0, 0, 0, flags, out);
	if (r < 0)
		return r;

	for (int i = 0; i < size; i += PGSIZE) {
		vm_page_t *page = vm_allocpage(flags & kVMemSleep);
		pmap_enter(kmap.pmap, page->paddr, (vaddr_t)*out + i, kVMAll);
	}

	return 0;
}

static void
internal_freewired(vmem_t *vmem, vmem_addr_t addr)
{
}

void
vm_kernel_init()
{
	char *test;

	vmem_earlyinit();
	vmem_init(&vm_kernel_va, "kernel-va", KHEAP_BASE, KHEAP_SIZE, PGSIZE,
	    NULL, NULL, NULL, 0, kVMemBootstrap, kSPLVM);
	vmem_init(&vm_kernel_wired, "kernel-wired", 0, 0, PGSIZE,
	    internal_allocwired, internal_freewired, &vm_kernel_va, 0,
	    kVMemBootstrap, kSPLVM);

	test = kmalloc(64);
	strcpy(test, "hello\n");
	kprintf(test);
	kfree(test);
}

vaddr_t
vm_kalloc(size_t npages, bool wait)
{
	vmem_addr_t addr;
	int r = vmem_xalloc(&vm_kernel_wired, npages * PGSIZE, 0, 0, 0, 0, 0,
	    wait ? kVMemSleep : 0, &addr);
	kprintf("ADDR: 0x%lx, R: %d\n", addr, r);
	if (r == 0) {
		return (vaddr_t)addr;
	} else
		return NULL;
}
