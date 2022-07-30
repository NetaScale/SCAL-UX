/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "NVMEController.h"
#include "NVMEDisk.h"
#include "nvmereg.h"

@implementation NVMEDisk

- (NVMEController *)getController
{
	return (NVMEController *)parent;
}

- (id)initWithAttachmentInfo:(struct nvme_disk_attach *)info
{
	self = [super init];
	parent = info->controller;
	ksnprintf(name, sizeof name, "NVMEDisk0"); /* xxx  */
	nsid = info->nsid;
	m_nBlocks = info->nsident->nsze;
	m_blockSize = 1 << info->nsident
			       ->lbaf[NVME_ID_NS_FLBAS(info->nsident->flbas)]
			       .lbads;
	[self registerDevice];

	DKDevLog(self, "NSID %d; %lu MiB (blocksize %ld, blocks %ld)\n",
	    info->nsid, m_nBlocks * m_blockSize / 1024 / 1024, m_blockSize,
	    m_nBlocks);

	return self;
}

- (int)readBlocks:(blksize_t)nBlocks
	       at:(blkoff_t)offset
       intoBuffer:(char *)buf
       completion:(struct dk_diskio_completion *)completion
{
	return [[self getController] readBlocks:nBlocks
					     at:offset
					   nsid:nsid
				     intoBuffer:buf
				     completion:completion];
}

- (int)writeBlocks:(blksize_t)nBlocks
		at:(blkoff_t)offset
	fromBuffer:(char *)buf
	completion:(struct dk_diskio_completion *)completion
{
}

@end
