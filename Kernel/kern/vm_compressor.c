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

void
swapper(void *unused)
{
	waitq_init(&swq);
	while (1) {
		kprintf("We wait\n");
		__auto_type r = waitq_await(&swq, 0x0, 3000000000);
		kprintf("Our wait is over, we got %d\n",r);
	}
	for (;;)
		asm("hlt");
}
