#ifndef DKDEVICE_H_
#define DKDEVICE_H_

#include <sys/queue.h>

#include <OFObject.h>

#define kAnsiYellow "\e[0;33m"
#define kAnsiReset "\e[0m"

#define DKLogAttach(dev) \
	kprintf(kAnsiYellow "%s" kAnsiReset " at " kAnsiYellow "%s" kAnsiReset);
#define DKLogAttachExtra(DEV, FMT, ...)                        \
	kprintf(kAnsiYellow "%s" kAnsiReset " at " kAnsiYellow \
			    "%s" kAnsiReset FMT,               \
	    [dev name], DEV -> parent -> name, ##__VA_ARGS__);
#define DKPrint(...) kprintf(__VA_ARGS__)
#define DKLog(SUB, ...)                                      \
	({                                                   \
		kprintf(kAnsiYellow "%s: " kAnsiReset, SUB); \
		kprintf(__VA_ARGS__);                        \
	})
#define DKDevLog(dev, ...) DKLog(dev->m_name, __VA_ARGS__)

@class DKDevice;

struct dk_device_pci_info;

/*! Represenst an offset in unit blocks of an underlying block device. */
typedef int64_t blkoff_t;

typedef _TAILQ_HEAD(, DKDevice, ) DKDevice_queue_t;
typedef _TAILQ_ENTRY(DKDevice, ) DKDevice_queue_entry_t;
typedef _SLIST_HEAD(, DKDevice, ) DKDevice_slist_t;
typedef _SLIST_ENTRY(DKDevice, ) DKDevice_slist_entry_t;

/**
 * Represents any sort of device.
 */
@interface DKDevice : OFObject {
	char		       m_name[32];
	DKDevice		 *parent;
	DKDevice_queue_t       subdevs;
	DKDevice_queue_entry_t subdev_entries;
}

/** The device's unique name. */
@property (readonly) const char *name;

/** Register the device in the system. */
- (void)registerDevice;

/** Register the device in the system (for PCI devices). */
- (void)registerDevicePCIInfo:(struct dk_device_pci_info *)pciInfo;

@end

#endif /* DKDEVICE_H_ */
