
#include <amd64/boot.h>

#include <libkern/klib.h>

#include "dev/LimineFB.h"
#include "dev/PS2Keyboard.h"
#include "dev/acpi/AcpiPC.h"
//#include "dev/fbterm/FBTerm.h"

extern void (*init_array_start)(void);
extern void (*init_array_end)(void);

void
setup_objc()
{
	for (void (**func)(void) = &init_array_start; func != &init_array_end;
	     func++)
		(*func)();
}

int
autoconf()
{
	setup_objc();
	kprintf("DeviceKit version 0\n");
#if 0
	if (limfb != NULL)
		[LimineFB probeWithLimineFBResponse:limfb];

	[FBTerm probeWithFB:sysfb];
#endif
	[AcpiPC probeWithRSDP:rsdp_request.response->address];

	return 0;
}
