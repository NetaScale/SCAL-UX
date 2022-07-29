/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef DKDISK_H_
#define DKDISK_H_

#include "devicekit/DKDevice.h"

/**
 * Protocol common to physical and logical disks.
 */
@protocol DKAbstractDiskMethods

- (int)readBytes:(size_t)nBytes at:(off_t)offset intoBuffer:(char *)buf;
- (int)writeBytes:(size_t)nBytes at:(off_t)offset fromBuffer:(char *)buf;

@end

/**
 * Physical disk methods. @see DKDisk
 */
@protocol DKDiskMethods

- (int)readBlocks:(size_t)nBlocks at:(off_t)offset intoBuffer:(char *)buf;
- (int)writeBlocks:(size_t)nBlocks at:(off_t)offset fromBuffer:(char *)buf;

@end

/**
 * The abstract superclass of physical disks proper which are present in a
 * drive. The class provides methods which handle the generic aspects of block
 * I/O, such as deblocking.
 *
 * Implementors must implement the DKDiskMethods protocol; its methods carry out
 * the actual I/O.
 */
@interface DKDisk : DKDevice <DKAbstractDiskMethods> {
	uint16_t blockSize; /** block size in byets */
	uint16_t nBlocks;   /** total number of blocks */
}

@end

#endif /* DKDISK_H_ */
