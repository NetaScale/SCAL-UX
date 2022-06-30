#include "amd64.h"
#include "dev/LimineFB.h"
#include "dev/PS2Keyboard.h"
#include "dev/acpi/AcpiPC.h"
#include "dev/fbterm/FBTerm.h"

int
autoconf(struct limine_framebuffer_response *limfb)
{
	kprintf("DeviceKit version 0\n");

	if (limfb != NULL)
		[LimineFB probeWithLimineFBResponse:limfb];

	[FBTerm probeWithFB:sysfb];
	[AcpiPC probeWithRSDP:rsdp_request.response->address];

	for (;;)
		;

	return 0;
}
