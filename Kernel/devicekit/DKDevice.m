#include "dev/PCIBus.h"
#include "devicekit/DKDevice.h"

@implementation DKDevice

- (void)addToTree
{
	TAILQ_INIT(&subdevs);
	if (parent)
		TAILQ_INSERT_TAIL(&parent->subdevs, self, subdev_entries);
}

- (const char *)name
{
	return name;
}

- (void)registerDevice
{
	[self addToTree];
	if (parent)
		DKDevLog(self, "Registered at %s\n", parent->name);
	else
		DKDevLog(self, "Registered\n");
}

- (void)registerDevicePCIInfo:(struct dk_device_pci_info *)pciInfo
{
	parent = pciInfo->busObj;
	[self addToTree];
	DKDevLog(self, "Registered at %s (slot %d, func %d)\n",
	    [pciInfo->busObj name], pciInfo -> slot, pciInfo -> fun);
}

@end
