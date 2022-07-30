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

#include <sys/types.h>

#include "devicekit/DKDevice.h"

/*!
 * Represents an I/O operation. The initiator of the operation allocates one of
 * these and passes it to a method; the initiator is responsible for freeing the
 * structure, but must ensure not to do so before the operation is completed.
 */
struct dk_diskio_completion {
	/*!
	 * Function to be called when the I/O completes.
	 * @param data the completion's data member
	 * @param result number of writes read/writen, or -errno for error
	 */
	void (*callback)(void *data, ssize_t result);
	/*! Opaque data passed to callback. */
	void *data;
};

/*!
 * Protocol common to physical and logical disks.
 */
@protocol DKAbstractDiskMethods

- (int)readBytes:(size_t)nBytes
	      at:(off_t)offset
      intoBuffer:(char *)buf
      completion:(struct dk_diskio_completion *)completion;
- (int)writeBytes:(size_t)nBytes
	       at:(off_t)offset
       fromBuffer:(char *)buf
       completion:(struct dk_diskio_completion *)completion;

@end

/*!
 * Physical disk methods. @see DKDisk
 */
@protocol DKDiskMethods

- (int)readBlocks:(blksize_t)nBlocks
	       at:(blkoff_t)offset
       intoBuffer:(char *)buf
       completion:(struct dk_diskio_completion *)completion;
- (int)writeBlocks:(blksize_t)nBlocks
		at:(blkoff_t)offset
	fromBuffer:(char *)buf
	completion:(struct dk_diskio_completion *)completion;

@end

/*!
 * The abstract superclass of physical disks proper which are present in a
 * drive. The class provides methods which handle the generic aspects of block
 * I/O, such as deblocking.
 *
 * Implementors must implement the DKDiskMethods protocol; its methods carry out
 * the actual I/O.
 */
@interface DKDisk : DKDevice <DKAbstractDiskMethods> {
	blksize_t m_blockSize;
	blkcnt_t  m_nBlocks;
}

/** block size in byets */
@property (readonly) blksize_t blockSize;

/** total size in unit blockSize */
@property (readonly) blkcnt_t nBlocks;

@end

#endif /* DKDISK_H_ */
