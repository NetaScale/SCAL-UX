#ifndef PS2KEYBOARD_H_
#define PS2KEYBOARD_H_

#include "devicekit/DKDevice.h"

@interface PS2Keyboard : DKDevice

+ (BOOL)probe;

@end

#endif /* PS2KEYBOARD_H_ */
