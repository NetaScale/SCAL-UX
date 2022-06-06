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

@interface TestObject : OFObject
+ (void)initialize;
@end

@implementation TestObject
+(void) initialize {
	kprintf("Hello from TestObject's initializer\n");
}

+(void)doathing {
	kprintf("got the message\n");
}
@end


void modinit()
{
	kprintf("trying to message TestObject...\n");
	[TestObject doathing];
}