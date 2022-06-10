#include <sys/vxkern.h>

__attribute__((__objc_root_class__)) @interface OFObject {
	Class _isa;
}

+ do;
+ (void)load;
+ (void)unload;
+ (void)initialize;
+ (Class)class;
@end

@implementation OFObject

+ do
{
	kprintf("hello from objc\n");
}

+ (void)load
{
	kprintf("load\n");
}

+ (void)unload
{
	kprintf("unload\n");
}

+ (void)initialize
{
	kprintf("initialize\n");
}

+ (Class)class
{
	return self;
}

@end

extern void (*init_array_start)(void);
extern void (*init_array_end)(void);

int
doathing()
{
	kprintf("trying to do with OFObject");

	for (void (**func)(void) = &init_array_start; func != &init_array_end;
	     func++) {
		kprintf("calling init function %p\n", *func);
		(*func)();
	}

	[OFObject do];
	return 0;
}