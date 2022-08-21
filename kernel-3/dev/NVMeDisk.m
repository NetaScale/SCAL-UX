/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "NVMeController.h"
#include "NVMeDisk.h"
#include "dev/GPTVolumeManager.h"
#include "devicekit/DKDisk.h"
#include "nvmereg.h"

@implementation NVMeDisk

- (NVMeController *)getController
{
	return (NVMeController *)m_provider;
}

- (id)initWithAttachmentInfo:(struct nvme_disk_attach *)info
{
	self = [super initWithProvider:info->controller];

	kmem_asprintf(&m_name,  "NS%hu@%lu",
	    info -> nsid, [(info->controller) controllerId]);
	nsid = info->nsid;
	m_nBlocks = info->nsident->nsze;
	m_blockSize = 1 << info->nsident
			       ->lbaf[NVME_ID_NS_FLBAS(info->nsident->flbas)]
			       .lbads;
	m_maxBlockTransfer = [info->controller maxBlockTransfer];
	[self registerDevice];

	DKLogAttachExtra(self, "NSID %d; %lu MiB (blocksize %ld, blocks %ld)\n",
	    info->nsid, m_nBlocks * m_blockSize / 1024 / 1024, m_blockSize,
	    m_nBlocks);

	[[DKLogicalDisk alloc]
	    initWithUnderlyingDisk:self
			      base:0
			      size:m_nBlocks * m_blockSize
			      name:[info->controller controllerName]
			  location:0
			  provider:self];

	return self;
}

- (int)readBlocks:(blksize_t)nBlocks
	       at:(blkoff_t)offset
       intoBuffer:(vm_mdl_t *)buf
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
	fromBuffer:(vm_mdl_t *)buf
	completion:(struct dk_diskio_completion *)completion
{
}

@end
