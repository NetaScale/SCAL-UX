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
#include "dev/GPTVolumeManager.h"
#include "libkern/klib.h"

@implementation DKLogicalDisk

@synthesize underlying = m_underlying;
@synthesize base = m_base;
@synthesize size = m_size;
@synthesize location = m_location;

- (blksize_t)blockSize
{
	return [m_underlying blockSize];
}

- (int)buildPosixDeviceName:(char *)buffer withMaxSize:(size_t)size
{
	int r;

	if ([m_underlying isKindOfClass:[DKDrive class]])
		return ksnprintf(buffer, size, "dk%d",
		    [(DKDrive *)m_underlying driveID]);

	r = [(id)m_underlying buildPosixDeviceName:buffer withMaxSize:size];
	buffer += r;
	size -= r;
	assert(size > 0);

	return ksnprintf(buffer, size, "s%lu", m_location);
}

- initWithUnderlyingDisk:(DKDevice<DKAbstractDiskMethods> *)underlying
		    base:(off_t)base
		    size:(size_t)size
		    name:(const char *)aname
		location:(size_t)location
		provider:(DKDevice *)provider
{
	self = [super init];
	if (self) {
		char nameBuf[64];

		ksnprintf(m_name, 32, "%s Disk", aname);
		m_underlying = underlying;
		m_base = base;
		m_size = size;
		m_location = location;
		parent = provider;
		[self registerDevice];

		[self buildPosixDeviceName:nameBuf withMaxSize:63];

		DKDevLog(self, "POSIX DevFS node: %s\n", nameBuf);

		if (location == 0) {
			[GPTVolumeManager probe:self];
		}
	}
	return self;
}

- (int)readBytes:(size_t)nBytes
	      at:(off_t)offset
      intoBuffer:(vm_mdl_t *)buf
      completion:(struct dk_diskio_completion *)completion
{
	if (offset + nBytes > m_size)
		return -EINVAL;

	return [m_underlying readBytes:nBytes
				    at:offset + m_base
			    intoBuffer:buf
			    completion:completion];
}

- (int)writeBytes:(size_t)nBytes
	       at:(off_t)offset
       fromBuffer:(vm_mdl_t *)buf
       completion:(struct dk_diskio_completion *)completion
{
	if (offset + nBytes > m_size)
		return -EINVAL;

	return [m_underlying writeBytes:nBytes
				     at:offset
			     fromBuffer:buf
			     completion:completion];
}

@end
