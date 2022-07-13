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
#include "vm.h"

drumslot_t lastslot = 0;

typedef struct swappedpage_t {
	drumslot_t slot;
	size_t	   size;
	char	   data[0];
} swappedpage_t;

typedef struct vm_compressor {
	TAILQ_HEAD(, swappedpage) pages;
} vm_compressor_t;

drumslot_t
swapout(char *data)
{
	swappedpage_t *page = NULL;
	char	       buf[4096];
	size_t	       size;

	size = LZ4_compress_default(data, buf, 4096, 4096);
	if (size == 0)
		return kDrumSlotInvalid;

	// page = kmalloc(sizeof *page + size);
	assert(page != NULL);

	page->slot = lastslot++;

	return page->slot;
}

waitq_t swq;

/**
 * returns 0 if the page is not suitable for swapping out, -1 if any further
 * swapping at all is not permissible, 1 if page successfully swapped out.
 */
static int
page_swapout(vm_page_t *page)
{
	pv_entry_t *pv, *tmp_pv;

	if (page->anon) {
		drumslot_t slot;

		lock(&page->anon->lock);

		slot = swapout(P2V(page->paddr));
		if (slot == kDrumSlotInvalid)
			return 0;

		page->anon->resident = false;
		page->anon->drumslot = slot;
	} else {
		assert("not yet implemented\n");
	}

	LIST_FOREACH_SAFE (pv, &page->pv_table, pv_entries, tmp_pv) {
		pmap_unenter(pv->map, page, pv->vaddr, pv);
	}

	unlock(&page->anon->lock);
}

void
swapper(void *unused)
{
	waitq_init(&swq);
	while (1) {
		kprintf("We wait\n");
		__auto_type r = waitq_await(&swq, 0x0, 3000000000);
		kprintf("Our wait is over, we got %d\n", r);
	}
	for (;;)
		asm("hlt");
}
