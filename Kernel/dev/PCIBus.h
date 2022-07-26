#ifndef PCIBUS_H_
#define PCIBUS_H_

#include "devicekit/DKDevice.h"
#include "lai/core.h"

@interface PCIBus : DKDevice

+ (BOOL)probeWithAcpiNode:(lai_nsnode_t *)node;
- initWithSeg:(uint8_t)seg bus:(uint8_t)bus;

@end

#endif /* PCIBUS_H_ */
