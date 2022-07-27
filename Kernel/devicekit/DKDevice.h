#ifndef DKDEVICE_H_
#define DKDEVICE_H_

#include <sys/queue.h>

#include <OFObject.h>

#define kAnsiYellow "\e[0;33m"
#define kAnsiReset "\e[0m"

#define DKLog(SUB, ...)                                      \
	({                                                   \
		kprintf(kAnsiYellow "%s: " kAnsiReset, SUB); \
		kprintf(__VA_ARGS__);                        \
	})
#define DKDevLog(dev, ...) DKLog(dev->name, __VA_ARGS__)

struct dk_device_pci_info;

@interface DKDevice : OFObject {
	char	  name[32];
	DKDevice *parent;
	_TAILQ_HEAD(, DKDevice, ) subdevs;
	_TAILQ_ENTRY(DKDevice, ) subdev_entries;
}

/** Get the device's name. */
- (const char *)name;

/** Register the device in the system. */
- (void)registerDevice;

/** Register the device in the system (for PCI devices). */
- (void)registerDevicePCIInfo: (struct dk_device_pci_info*)pciInfo;

@end

#endif /* DKDEVICE_H_ */
