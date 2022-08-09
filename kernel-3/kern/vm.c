/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/vm.h>
#include <libkern/klib.h>

#include <stdatomic.h>

#include "kern/sync.h"

#define PGQ_INITIALIZER(PGQ)                                             \
	{                                                                \
		.queue = TAILQ_HEAD_INITIALIZER(PGQ.queue), .npages = 0, \
		.lock = MUTEX_INITIALISER(&PGQ.lock)                     \
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
vm_pagefree(vm_page_t *page)
{
	assert(page != NULL);
	vm_page_changequeue(page, NULL, &vm_pgfreeq);
}

void
vm_page_changequeue(vm_page_t *page, nullable vm_pagequeue_t *from,
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
