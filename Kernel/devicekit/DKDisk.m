#include <errno.h>

#include "DKDisk.h"

#define selfDelegate ((DKDisk<DKDiskMethods> *)self)

@implementation DKDisk

@synthesize blockSize = m_blockSize;
@synthesize nBlocks = m_nBlocks;

- (int)readBytes:(size_t)nBytes
	      at:(off_t)offset
      intoBuffer:(char *)buf
      completion:(struct dk_diskio_completion *)completion
{
	if (offset % m_blockSize == 0 && nBytes % m_blockSize == 0)
		return [selfDelegate readBlocks:nBytes / m_blockSize
					     at:offset / m_blockSize
				     intoBuffer:buf
				     completion:completion];

	DKDevLog(self, "Unaligned read request received - not yet handled.\n");
	return -EOPNOTSUPP;
}

- (int)writeBytes:(size_t)nBytes
	       at:(off_t)offset
       fromBuffer:(char *)buf
       completion:(struct dk_diskio_completion *)completion
{
	if (offset % m_blockSize == 0 && nBytes % m_blockSize == 0)
		return [selfDelegate writeBlocks:nBytes / m_blockSize
					      at:offset / m_blockSize
				      fromBuffer:buf
				      completion:completion];

	DKDevLog(self, "Unaligned read request received - not yet handled.\n");
	return -EOPNOTSUPP;
}

@end
