#ifndef NVMECONTROLLER_H_
#define NVMECONTROLLER_H_

#include "PCIBus.h"

struct nvme_queue;
struct nvm_identify_controller;

@interface NVMEController : DKDevice {
    @private
	volatile vaddr_t regs;

	size_t dstrd;

	struct nvm_identify_controller *cident; /* a dedicated page */
	struct nvme_queue		  *adminq;
	struct nvme_queue		  *ioqueue;
}

+ (BOOL)probeWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

- initWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

@end

#endif /* NVMECONTROLLER_H_ */
