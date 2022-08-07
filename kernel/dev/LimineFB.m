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
	ksnprintf(m_name, sizeof m_name, "LimFB%d", fbNum++);
	[self registerDevice];
	width = fb->width;
	height = fb->height;
	pitch = fb->pitch;
	bpp = fb->bpp;
	base = fb->address;
	DKDevLog(self, " %lux%lux%d\n", width, height, bpp);
	return self;
}

@end
