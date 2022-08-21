/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/kmem.h>
#include <libkern/klib.h>
#include <vm/vm.h>

#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#define PGQ_INITIALIZER(PGQ)                                             \
	{                                                                \
		.queue = TAILQ_HEAD_INITIALIZER(PGQ.queue), .npages = 0, \
		.lock = MUTEX_INITIALISER(PGQ.lock)                     \
	}

vm_pagequeue_t vm_pgfreeq = PGQ_INITIALIZER(vm_pgfreeq),
	       vm_pgkmemq = PGQ_INITIALIZER(vm_pgkmemq),
	       vm_pgwiredq = PGQ_INITIALIZER(vm_pgwiredq),
	       vm_pgactiveq = PGQ_INITIALIZER(vm_pgactiveq),
	       vm_pginactiveq = PGQ_INITIALIZER(vm_pginactiveq),
	       vm_pgpmapq = PGQ_INITIALIZER(vm_pgpmapq);

vm_pregion_queue_t vm_pregion_queue = TAILQ_HEAD_INITIALIZER(vm_pregion_queue);

vm_page_t *
vm_page_from_paddr(paddr_t paddr)
{
	vm_pregion_t *preg;

	TAILQ_FOREACH (preg, &vm_pregion_queue, queue) {
		if (preg->base <= paddr &&
		    (preg->base + PGSIZE * preg->npages) > paddr) {
			return &preg->pages[(paddr - preg->base) / PGSIZE];
		}
	}

	return NULL;
}

vm_page_t *
vm_pagealloc(bool sleep, vm_pagequeue_t *queue)
{
	vm_page_t *page;

	mutex_lock(&vm_pgfreeq.lock);
	page = TAILQ_FIRST(&vm_pgfreeq.queue);
	if (!page) {
		fatal("vm_allocpage: oom not yet handled\n");
	}
	vm_page_changequeue(page, &vm_pgfreeq, queue);

	memset(P2V(page->paddr), 0x0, PGSIZE);

	return page;
}

vm_pagequeue_t *
vm_page_queue(vm_page_t *page)
{
	switch (page->queue) {
	case kVMPageFree:
		return &vm_pgfreeq;
	case kVMPageKMem:
		return &vm_pgkmemq;
	case kVMPageWired:
		return &vm_pgactiveq;
	case kVMPageActive:
		return &vm_pgactiveq;
	case kVMPageInactive:
		return &vm_pginactiveq;
	default:
		assert(!"unreached\n");
	}
}

void
vm_page_free(vm_page_t *page)
{
	assert(page != NULL);
	vm_page_changequeue(page, NULL, &vm_pgfreeq);
}

void
vm_page_changequeue(vm_page_t *page, NULLABLE vm_pagequeue_t *from,
    vm_pagequeue_t *to) LOCK_REQUIRES(from->lock)
{
	assert(page != NULL);
	assert(to != NULL);

	if (!from) {
		from = vm_page_queue(page);
		mutex_lock(&from->lock);
	}

	TAILQ_REMOVE(&from->queue, page, pagequeue);
	from->npages--;
	mutex_unlock(&from->lock);

	mutex_lock(&to->lock);
	TAILQ_INSERT_HEAD(&to->queue, page, pagequeue);
	to->npages++;
	mutex_unlock(&to->lock);
}

void
vm_pagedump(void)
{
	kprintf("\033[7m%-9s%-9s%-9s%-9s%-9s%-9s\033[m\n", "free", "kmem",
	    "wired", "active", "inactive", "pmap");

	kprintf("%-9zu%-9zu%-9zu%-9zu%-9zu%-9zu\n",
	    vm_pgfreeq.npages, vm_pgkmemq.npages, vm_pgwiredq.npages,
	    vm_pgactiveq.npages, vm_pginactiveq.npages, vm_pgpmapq.npages);
}

int
vm_mdl_expand(vm_mdl_t **mdl, size_t bytes)
{
	size_t	  nPages = PGROUNDUP(bytes) / PGSIZE;
	vm_mdl_t *newmdl;

	newmdl = kmem_alloc(sizeof (*newmdl) + sizeof(vm_page_t *) * nPages);
	if (!newmdl)
		return -ENOMEM;

	newmdl->offset = 0;
	newmdl->nBytes = bytes;
	newmdl->nPages = nPages;
	for (int i = 0; i < (*mdl)->nPages; i++)
		newmdl->pages[i] = (*mdl)->pages[i];

	for (int i = (*mdl)->nPages; i < nPages; i++) {
		newmdl->pages[i] = vm_pagealloc(1, &vm_pgwiredq);
		assert(newmdl->pages[i]);
	}

	kmem_free(*mdl, sizeof (**mdl) + sizeof(vm_page_t *) * (*mdl)->nPages);
	*mdl = newmdl;

	return 0;
}

int
vm_mdl_new_with_capacity(vm_mdl_t **out, size_t bytes)
{
	size_t	  nPages = PGROUNDUP(bytes) / PGSIZE;
	vm_mdl_t *mdl = kmem_alloc(sizeof (*mdl) + sizeof(vm_page_t *) * nPages);

	if (!mdl)
		return -ENOMEM;

	mdl->offset = 0;
	mdl->nBytes = bytes;
	mdl->nPages = nPages;
	for (int i = 0; i < nPages; i++) {
		mdl->pages[i] = vm_pagealloc(true, &vm_pgwiredq);
		assert(mdl->pages[i] != NULL);
	}

	*out = mdl;

	return 0;
}

size_t
vm_mdl_capacity(vm_mdl_t *mdl)
{
	return mdl->nPages * PGSIZE;
}

void
vm_mdl_copy(vm_mdl_t *mdl, void *buf, size_t nBytes, off_t off)
{
	off += mdl->offset;
	voff_t base = PGROUNDDOWN(off);
	voff_t pageoff = off - base;
	size_t firstpage = base / PGSIZE;
	size_t lastpage = firstpage + (pageoff + nBytes - 1) / PGSIZE + 1;

	for (size_t iPage = firstpage; iPage < lastpage; iPage++) {
		vm_page_t *page;
		size_t	   tocopy;

		if (nBytes > PGSIZE)
			tocopy = PGSIZE - pageoff;
		else
			tocopy = nBytes;

		page = mdl->pages[iPage];

		memcpy(buf + (iPage - firstpage) * PGSIZE,
		    P2V(page->paddr) + pageoff, tocopy);

		nBytes -= tocopy;
		pageoff = 0;
	}
}

void
vm_mdl_zero(vm_mdl_t *mdl)
{
	for (int i = 0; i < mdl->nBytes; i += PGSIZE) {
		memset(P2V(mdl->pages[i / PGSIZE]->paddr), 0x0, PGSIZE);
	}
}
