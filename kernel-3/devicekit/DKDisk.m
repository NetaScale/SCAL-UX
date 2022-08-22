/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <kern/sync.h>
#include <kern/task.h>

#include <errno.h>

#include "DKDisk.h"

#define selfDelegate ((DKDrive<DKDriveMethods> *)self)

typedef enum dk_strategy {
	kDKRead,
	kDKWrite,
} dk_strategy_t;

static int driveIDCounter = 0;

@implementation DKDrive

@synthesize driveID = m_driveID;
@synthesize blockSize = m_blockSize;
@synthesize nBlocks = m_nBlocks;
@synthesize maxBlockTransfer = m_maxBlockTransfer;

- init
{
	self = [super init];
	if (self) {
		m_driveID = driveIDCounter++;
	}
	return self;
}

struct complete_sync_data {
	semaphore_t sem;
	ssize_t result;
};

static void
complete_sync(void *data, ssize_t result)
{
	struct complete_sync_data *sync = data;
	sync->result = result;
	semaphore_signal(&sync->sem);
}

- (int)commonIO:(dk_strategy_t)strategy
	  bytes:(size_t)nBytes
	     at:(off_t)offset
	 buffer:(vm_mdl_t *)buf
     completion:(struct dk_diskio_completion *)completion
{
	struct dk_diskio_completion comp;
	struct complete_sync_data   sync;
	int			    r;

	if (!completion) {
		completion = &comp;
		comp.callback = complete_sync;
		comp.data = &sync;
		sync.sem = (semaphore_t)SEMAPHORE_INITIALIZER(sync.sem);
	} else
		comp.data = NULL;

	if (nBytes > m_maxBlockTransfer * m_blockSize) {
		DKDevLog(self, "Excessive request received - not yet handled.");
		return -EOPNOTSUPP;
	}

	if (offset % m_blockSize != 0 || nBytes % m_blockSize != 0) {
		DKDevLog(self,
		    "Unaligned read request received - not yet handled.\n");
		return -EOPNOTSUPP;
	}

	r = strategy == kDKRead ? [selfDelegate readBlocks:nBytes / m_blockSize
							at:offset / m_blockSize
						intoBuffer:buf
						completion:completion] :
				  [selfDelegate writeBlocks:nBytes / m_blockSize
							 at:offset / m_blockSize
						 fromBuffer:buf
						 completion:completion];

	if (r != 0)
		return r;

	if (comp.data) {
		/* synchronous case */
		r = semaphore_wait(&sync.sem, -1);
		assert(r == kWQSuccess);
		return sync.result;
	} else
		return 0;
}

- (int)readBytes:(size_t)nBytes
	      at:(off_t)offset
      intoBuffer:(vm_mdl_t *)buf
      completion:(struct dk_diskio_completion *)completion
{
	return [self commonIO:kDKRead
			bytes:nBytes
			   at:offset
		       buffer:buf
		   completion:completion];
}

- (int)writeBytes:(size_t)nBytes
	       at:(off_t)offset
       fromBuffer:(vm_mdl_t *)buf
       completion:(struct dk_diskio_completion *)completion
{
	return [self commonIO:kDKWrite
			bytes:nBytes
			   at:offset
		       buffer:buf
		   completion:completion];
}

@end
