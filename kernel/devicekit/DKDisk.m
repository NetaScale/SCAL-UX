/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

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

- (int)commonIO:(dk_strategy_t)strategy
	  bytes:(size_t)nBytes
	     at:(off_t)offset
	 buffer:(vm_mdl_t *)buf
     completion:(struct dk_diskio_completion *)completion
{
	if (nBytes > m_maxBlockTransfer * m_blockSize) {
		DKDevLog(self, "Excessive request received - not yet handled.");
		return -EOPNOTSUPP;
	}

	if (offset % m_blockSize != 0 || nBytes % m_blockSize != 0) {
		DKDevLog(self,
		    "Unaligned read request received - not yet handled.\n");
		return -EOPNOTSUPP;
	}

	return strategy == kDKRead ?
	    [selfDelegate readBlocks:nBytes / m_blockSize
				  at:offset / m_blockSize
			  intoBuffer:buf
			  completion:completion] :
	    [selfDelegate writeBlocks:nBytes / m_blockSize
				   at:offset / m_blockSize
			   fromBuffer:buf
			   completion:completion];
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
