/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <dev/acpi/AcpiPC.h>
#include <x86_64/boot.h>
#include <dev/LimineFB.h>
#include <dev/fbterm/FBTerm.h>

extern void (*init_array_start)(void);
extern void (*init_array_end)(void);

void
setup_objc(void)
{
	for (void (**func)(void) = &init_array_start; func != &init_array_end;
	     func++)
		(*func)();
}

int
autoconf(void)
{
	setup_objc();

	kprintf("DeviceKit version 0\n");

	[LimineFB probeWithLimineFBResponse:framebuffer_request.response];
	[FBTerm probeWithFB:sysfb];
	[AcpiPC probeWithRSDP:rsdp_request.response->address];

	return 0;
}
