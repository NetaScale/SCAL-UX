#include "DKLimineFB.h"

int
autoconf(struct limine_framebuffer_response *limfb)
{

	kprintf("DeviceKit version 0\n");

	if (limfb != NULL)
		[DKLimineFB probeWithLimineFBResponse:limfb];

	return 0;
}
