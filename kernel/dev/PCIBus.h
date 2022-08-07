#ifndef PCIBUS_H_
#define PCIBUS_H_

#include "devicekit/DKDevice.h"
#include "lai/core.h"
#include "machine/intr.h"

@class PCIBus;

typedef struct dk_device_pci_info {
	PCIBus  *busObj; /* PCIBus * */
	uint16_t seg;
	uint8_t	 bus;
	uint8_t	 slot;
	uint8_t	 fun;

	uint8_t pin;
} dk_device_pci_info_t;

@interface PCIBus : DKDevice

/** handle INTx */
+ (int)handleInterruptOf:(dk_device_pci_info_t *)pciInfo
	     withHandler:(intr_handler_fn_t)handler
		argument:(void *)arg
	      atPriority:(spl_t)priority;
+ (void)enableMemorySpace:(dk_device_pci_info_t *)pciInfo;
+ (void)enableBusMastering:(dk_device_pci_info_t *)pciInfo;
+ (void)setInterruptsOf:(dk_device_pci_info_t *)pciInfo enabled:(BOOL)enabled;
+ (paddr_t)getBar:(uint8_t)num info:(dk_device_pci_info_t *)pciInfo;

+ (BOOL)probeWithAcpiNode:(lai_nsnode_t *)node;

- initWithSeg:(uint8_t)seg bus:(uint8_t)bus;

@end

#endif /* PCIBUS_H_ */
