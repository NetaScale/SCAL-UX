#include "DKLimineFB.h"

@implementation DKLimineFB

@synthesize width, height, pitch, bpp, base;

static int fbNum = 0;
DKLimineFB *sysfb = NULL;

+ (BOOL)probeWithLimineFBResponse:(struct limine_framebuffer_response *)resp
{
	DKLimineFB *fbs[resp->framebuffer_count];
        assert (resp->framebuffer_count > 0);
	for (int i = 0; i < resp->framebuffer_count; i++)
		fbs[i] = [[self alloc] initWithLimineFB:resp->framebuffers[i]];
        sysfb = fbs[0];
        return YES;
        
}

- initWithLimineFB:(struct limine_framebuffer *)fb
{
        self = [super init];
        parent = nil;
        ksnprintf(name, sizeof name,  "LimFB%d", fbNum++);
        width = fb->width;
        height = fb->height;
        pitch = fb->pitch;
        bpp = fb->bpp;
        base = P2V(fb->address);
        DKLog("Limine framebuffer: %lux%lux%d\n", width, height, bpp);
        [self registerDevice];
        return self;
}

@end
