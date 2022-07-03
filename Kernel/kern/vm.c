/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "vm.h"

struct vm_page_queue pg_freeq, pg_activeq, pg_inactiveq;
vm_map_t kmap;

int
pagedaemon()
{
	// sleep(1);
}

vaddr_t vm_kern_allocate(size_t npages, bool wait)
{
	
}
