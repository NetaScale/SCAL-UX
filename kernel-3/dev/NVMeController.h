#ifndef NVMECONTROLLER_H_
#define NVMECONTROLLER_H_

#include "PCIBus.h"

struct nvme_queue;
struct nvm_identify_controller;
struct dk_diskio_completion;

@interface NVMeController : DKDevice {
    @private
	size_t		 m_controllerId;
	volatile vaddr_t regs;

	blkcnt_t maxBlockTransfer;
	size_t	 dstrd;

	struct nvm_identify_controller *cident; /* a dedicated page */
	struct nvme_queue		  *adminq;
	struct nvme_queue		  *ioqueue;
}

@property (readonly) size_t	 controllerId;
@property (readonly) const char *controllerName;
@property (readonly) blkcnt_t	 maxBlockTransfer;

+ (BOOL)probeWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

- initWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

- (int)readBlocks:(blksize_t)nBlocks
	       at:(blkoff_t)offset
	     nsid:(uint16_t)nsid
       intoBuffer:(vm_mdl_t *)buf
       completion:(struct dk_diskio_completion *)completion;

@end

#endif /* NVMECONTROLLER_H_ */
