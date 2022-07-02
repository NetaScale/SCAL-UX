#ifndef PS2KEYBOARD_H_
#define PS2KEYBOARD_H_

#include "devicekit/DKDevice.h"
#include "lai/core.h"

@interface PS2Keyboard : DKDevice

+ (BOOL)probeWithAcpiNode:(lai_nsnode_t *)node;

- initWithPortA: (uint8_t) portA portB: (uint8_t) portB GSI: (uint32_t) gsi;

@end

#endif /* PS2KEYBOARD_H_ */
