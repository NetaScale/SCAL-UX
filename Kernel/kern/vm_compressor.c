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

#include "kern/lock.h"
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

static int
page_lock_owner(vm_page_t *page, bool spin)
{
	assert(page->anon || page->obj);

#if 0
	if (page->anon)
		return spinlock_trylock(&page->anon->lock, spin);
	else
		return spinlock_trylock(&page->obj->lock, spin);
#endif
}

static void
page_unlock_owner(vm_page_t *page)
{
	assert(page->anon || page->obj);

#if 0
	if (page->anon)
		unlock(&page->anon->lock);
	else
		unlock(&page->obj->lock);
#endif
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

	LIST_FOREACH_SAFE (pv, &page->pv_table, pv_entries, tmp_pv) {
		vaddr_t *vaddr = pv->vaddr;
		/** pmap unenter removes from the list and frees pv */
		pmap_unenter(pv->map, page, pv->vaddr, pv);
		kprintf("tlb shootdown: %p\n", vaddr);
	}

	return 1;
}

struct owner {
	bool is_anon;
	union {
		vm_object_t *obj;
		vm_anon_t	  *anon;
	};
};

#if 0
/**
 * try to lock a page's owner and then the page. If either step fails, return 0.
 * Otherwise returns 1.
 */
static inline int
vm_page_lock_and_owner(vm_page_t *page, bool spin)
{
	if (page_lock_owner(page, spin) != 0)
		return 0;
	if (spinlock_trylock(&page->lock, spin) != 0) {
		page_unlock_owner(page);
		return 0;
	}
	return 1;
}
#endif

void
swapper(void *unused)
{
	vm_page_t	  *page, *_next;
	waitq_result_t r;
	spl_t	       spl;

	/* todo: vmstats, ..., ? */

	waitq_init(&swq);

loop:
	/*
	 * the page out loop: every 3s:
	 *  - unconditionally at the moment) all pages on the inactive
	 * queue are checked for access; if so, they are moved to the
	 * active queue; otherwise they are put to backing store.
	 *  -  the active queue is scanned; mappings' accessed bits are
	 * checked (and reset to 0); if none are set, the page is moved
	 * to the inactive queue.
	 */
	r = waitq_await(&swq, 0x0, 3000000000);
	spl = splvm();

	(void)r;

	VM_PAGE_QUEUES_LOCK();

	TAILQ_FOREACH_SAFE (page, &pg_inactiveq, queue, _next) {
		struct owner owner;
		int	     r;

		if (page_lock_owner(page, false) == 0)
			continue;

		owner.is_anon = page->anon != NULL; /* FIXME */
		owner.anon = page->anon;

		TAILQ_REMOVE(&pg_inactiveq, page, queue);

		if (pmap_page_accessed_reset(page)) {
			TAILQ_INSERT_TAIL(&pg_activeq, page, queue);
			goto next;
		}

		if (spinlock_trylock(&page->lock, false) == 0) {
			/* page is busy for some reason */
			goto next;
		}

		TAILQ_REMOVE(&pg_inactiveq, page, queue);

		/* unlock for I/O */
		VM_PAGE_QUEUES_UNLOCK();
		r = page_swapout(page);
		VM_PAGE_QUEUES_LOCK();

		if (r == -1) {
			/* todo OOM-kill a process */
			fatal("swapout failed\n");
		} else if (r == 0) {
			kprintf("nonfatal swapout failure\n");
		} else {
			page->free = 1;
			TAILQ_INSERT_TAIL(&pg_freeq, page, queue);
		}

		unlock(&page->lock);

	next:
#if 0
		unlock(&owner.anon->lock); /* FIXME */
#endif
	}

	TAILQ_FOREACH_SAFE (page, &pg_activeq, queue, _next) {
		if (page_lock_owner(page, false) == 0)
			continue;
		if (!pmap_page_accessed_reset(page)) {
			TAILQ_REMOVE(&pg_activeq, page, queue);
			TAILQ_INSERT_TAIL(&pg_inactiveq, page, queue);
		}
		page_unlock_owner(page);
	}

	VM_PAGE_QUEUES_UNLOCK();

	splx(spl);

	goto loop;
}
