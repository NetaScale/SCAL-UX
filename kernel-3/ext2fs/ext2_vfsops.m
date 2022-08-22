/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <vm/vm.h>

#include <errno.h>

#include "devicekit/DKDisk.h"
#include "ext2_fs.h"

int
mountit(DKLogicalDisk *disk)
{
	vm_mdl_t		 *mdl;
	int			r;
	struct ext2_super_block sb;

	r = vm_mdl_new_with_capacity(&mdl, PGSIZE);
	assert(r == 0);

	[disk readBytes:1024 at:1024 intoBuffer:mdl completion:NULL];
	vm_mdl_copy(mdl, &sb, 1024, 0);

	if (sb.s_magic != EXT2_SUPER_MAGIC) {
		kprintf("ext2fs: bad superblock magic number "
			"(expected 0x%x, got 0x%hx)\n",
		    EXT2_SUPER_MAGIC, sb.s_magic);
		return -EINVAL;
	}

        return 0;
}
