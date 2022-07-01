#include "PS2Keyboard.h"

@implementation PS2Keyboard

+ (BOOL)probeWithAcpiNode: (lai_nsnode_t *)node
{
	return YES;
}

@end
