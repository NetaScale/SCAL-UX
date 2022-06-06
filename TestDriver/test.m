#include "vxkern.h"

#if 0
__attribute__((__objc_root_class__)) @interface OFObject {
	Class _isa;
}

+ do;
+ (void)load;
+ (void)unload;
+ (void)initialize;
+ (Class)class;
@end

@interface TestObject : OFObject
+ (void)initialize;
@end

@implementation TestObject
+(void) initialize {
	kprintf("Hello World\n");
}
@end


@interface TestObject2 : OFObject
+ (void)initialize;
@end

@implementation TestObject2
+(void) initialize {
	kprintf("Hello World 2\n");
}
@end

extern void *__objc_exec_class;
#endif

void testfun();

void modinit2()
{
	testfun();
	kprintf("Hello\n");
}

void modinit()
{
	modinit2();
}