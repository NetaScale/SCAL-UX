#ifndef NVMECONTROLLER_H_
#define NVMECONTROLLER_H_

#include "PCIBus.h"

struct nvme_queue;
struct nvm_identify_controller;

@interface NVMEController : DKDevice {
	volatile vaddr_t regs;
	
	struct nvm_identify_controller *cident; /* a dedicated page */
	struct nvme_queue *adminq;
}

+ (BOOL)probeWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

- initWithPCIInfo:(dk_device_pci_info_t *)pciInfo;

@end

#endif /* NVMECONTROLLER_H_ */
