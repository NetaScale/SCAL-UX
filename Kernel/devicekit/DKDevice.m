#include "devicekit/DKDevice.h"
@implementation DKDevice

- (void)registerDevice
{
        TAILQ_INIT(&subdevs);
        if(parent)
                TAILQ_INSERT_TAIL(&parent->subdevs, self, subdev_entries);

        if (parent)
                DKLog("Registered %s at %s\n", name, parent->name);
        else
                DKLog("Registered %s\n", name);

}

@end
