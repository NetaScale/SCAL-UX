/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <libkern/klib.h>
#include <libkern/lz4.h>

#include "kern/task.h"
#include "sys/queue.h"
#include "vm.h"

typedef struct swappedpage_t {
	size_t size;
	char   data[0];
} swappedpage_t;

typedef struct vm_compressor {
	TAILQ_HEAD(, swappedpage) pages;
} vm_compressor_t;

static void
page_lock_owner(vm_page_t *page)
{
	assert(page->anon || page->obj);

	if (page->anon)
		lock(&page->anon->lock);
	else
		lock(&page->obj->lock);
}

static void
page_unlock_owner(vm_page_t *page)
{
	assert(page->anon || page->obj);

	if (page->anon)
		unlock(&page->anon->lock);
	else
		unlock(&page->obj->lock);
}

drumslot_t
swapout(char *data)
{
	swappedpage_t *page = NULL;
	char	       buf[4096];
	size_t	       size;

	size = LZ4_compress_default(data, buf, 4096, 4096);
	if (size == 0) {
		kprintf("failed to compress page %p\n", data);
		return kDrumSlotInvalid;
	}

	kprintf("VM Compressor: page %p compressed into %lu bytes\n", data,
	    size);

	page = kmalloc(sizeof *page + size);
	assert(page != NULL);
	memcpy(page->data, buf, size);

	return (drumslot_t)page;
}

waitq_t swq;

/**
 * returns 0 if the page is not suitable for swapping out, -1 if any further
 * swapping at all is not permissible, 1 if page successfully swapped out.
 *
 * @param page LOCKED and OWNER LOCKED page to try to compress
 */
static int
page_swapout(vm_page_t *page)
{
	pv_entry_t *pv, *tmp_pv;

	if (page->anon) {
		drumslot_t slot;

		slot = swapout(P2V(page->paddr));
		if (slot == kDrumSlotInvalid)
			return 0;

		page->anon->resident = false;
		page->anon->drumslot = slot;
	} else {
		fatal("page_swapout: not yet implemented for non-anons\n");
	}

	page_unlock_owner(page);

	LIST_FOREACH_SAFE (pv, &page->pv_table, pv_entries, tmp_pv) {
		vaddr_t *vaddr = pv->vaddr;
		/** pmap unenter removes from the list and frees pv */
		pmap_unenter(pv->map, page, pv->vaddr, pv);
		kprintf("tlb shootdown: %p\n", vaddr);
	}

	unlock(&page->anon->lock);

	return 1;
}

void
swapper(void *unused)
{
	waitq_init(&swq);
	while (1) {
		__auto_type r = waitq_await(&swq, 0x0, 3000000000);
		spl_t spl = splvm();

		kprintf("VM Compressor: scanning for pages to swap out\n");
		(void)r;
		while (1) {
			vm_page_t *page;

			VM_PAGE_QUEUES_LOCK();
			page = TAILQ_LAST(&pg_activeq, vm_page_queue);
			if (page) {
				page_lock_owner(page);
				lock(&page->lock);
				TAILQ_REMOVE(&pg_activeq, page, queue);
			}
			VM_PAGE_QUEUES_UNLOCK();

			if (!page)
				break;

			assert(page_swapout(page) == 1);

			VM_PAGE_QUEUES_LOCK();
			TAILQ_INSERT_HEAD(&pg_freeq, page, queue);
			unlock(&page->lock);
			VM_PAGE_QUEUES_UNLOCK();
		}

		splx(spl);
	}
	for (;;)
		asm("hlt");
}
