/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "sys/queue.h"
#include "vm.h"

struct vm_page_queue pg_freeq = TAILQ_HEAD_INITIALIZER(pg_freeq), pg_activeq = TAILQ_HEAD_INITIALIZER(pg_activeq), pg_inactiveq = TAILQ_HEAD_INITIALIZER(pg_inactiveq);
vm_map_t kmap;

int
pagedaemon()
{
	// sleep(1);
	return 0;
}
