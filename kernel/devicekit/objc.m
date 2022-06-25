#include "kern/kern.h"

#import <OFObject.h>

extern void (*init_array_start)(void);
extern void (*init_array_end)(void);

void
setup_objc()
{
	kprintf("calling Objective-C module initialisers\n");
	for (void (**func)(void) = &init_array_start; func != &init_array_end;
	     func++)
		(*func)();
	kprintf("Class name: %s\n", [OFObject classNameCString]);
}
