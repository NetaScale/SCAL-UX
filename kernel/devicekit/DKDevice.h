#ifndef DKDEVICE_H_
#define DKDEVICE_H_

#include <OFObject.h>
#include "kern/queue.h"

#define DKLog(...) kprintf(__VA_ARGS__)

@interface DKDevice : OFObject
{
        char name[32];
        DKDevice *parent;
        _TAILQ_HEAD(, DKDevice,) subdevs;
        _TAILQ_ENTRY(DKDevice,) subdev_entries;
}


/** Register the device in the system. */
- (void)registerDevice;

@end


#endif /* DKDEVICE_H_ */
