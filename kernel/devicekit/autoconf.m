#include "dev/LimineFB.h"
#include "dev/fbterm/FBTerm.h"

int
autoconf(struct limine_framebuffer_response *limfb)
{
	kprintf("DeviceKit version 0\n");

	if (limfb != NULL)
		[LimineFB probeWithLimineFBResponse:limfb];

	[FBTerm probeWithFB:sysfb];

	return 0;
}
