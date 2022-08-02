#include <errno.h>

#include "DKDisk.h"

#define selfDelegate ((DKDisk<DKDiskMethods> *)self)

typedef enum dk_strategy {
	kDKRead,
	kDKWrite,
} dk_strategy_t;

@implementation DKDisk

@synthesize blockSize = m_blockSize;
@synthesize nBlocks = m_nBlocks;

- (int)strategy:(dk_strategy_t)strategy
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
	return [self strategy:kDKRead
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
	return [self strategy:kDKWrite
			bytes:nBytes
			   at:offset
		       buffer:buf
		   completion:completion];
}

@end
