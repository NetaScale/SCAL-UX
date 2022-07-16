#include <amd64/boot.h>

#include "LimineFB.h"

@implementation LimineFB

@synthesize width, height, pitch, bpp, base;

static int fbNum = 0;
LimineFB	 *sysfb = NULL;

+ (BOOL)probeWithLimineFBResponse:(struct limine_framebuffer_response *)resp
{
	LimineFB *fbs[resp->framebuffer_count];
	assert(resp->framebuffer_count > 0);
	for (int i = 0; i < resp->framebuffer_count; i++)
		fbs[i] = [[self alloc] initWithLimineFB:resp->framebuffers[i]];
	sysfb = fbs[0];
	return YES;
}

- initWithLimineFB:(struct limine_framebuffer *)fb
{
	self = [super init];
	parent = nil;
	ksnprintf(name, sizeof name, "LimFB%d", fbNum++);
	width = fb->width;
	height = fb->height;
	pitch = fb->pitch;
	bpp = fb->bpp;
	base = fb->address;
	DKLog("Limine framebuffer: %lux%lux%d\n", width, height, bpp);
	[self registerDevice];
	return self;
}

@end
