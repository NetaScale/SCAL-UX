//#include "dev/PCIBus.h"
#include "devicekit/DKDevice.h"

@implementation DKDevice

- (const char *)name
{
	return m_name;
}

- (void)addToTree
{
	TAILQ_INIT(&subdevs);
	if (parent)
		TAILQ_INSERT_TAIL(&parent->subdevs, self, subdev_entries);
}

- (void)registerDevice
{
	[self addToTree];
	if (parent)
		DKDevLog(self,
		    "Registered at " kAnsiYellow "%s" kAnsiReset "\n",
		    parent->m_name);
	else
		DKDevLog(self, "Registered\n");
}

#if 0
- (void)registerDevicePCIInfo:(struct dk_device_pci_info *)pciInfo
{
	parent = pciInfo->busObj;
	[self addToTree];
	DKDevLog(self, "Registered at %s (slot %d, func %d)\n",
	    [pciInfo->busObj name], pciInfo -> slot, pciInfo -> fun);
}
#endif

@end
