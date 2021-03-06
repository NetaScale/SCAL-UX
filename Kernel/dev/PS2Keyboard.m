#include <amd64/amd64.h>

#include "PS2Keyboard.h"
#include "acpi/laiex.h"
#include "dev/IOApic.h"
#include "fbterm/FBTerm.h"
#include "lai/core.h"
#include "lai/error.h"
#include "lai/helpers/resource.h"

PS2Keyboard *ps2k = NULL;
void	     ps2_intr(intr_frame_t *frame, void *arg);

static const char codes[128] = { '\0', '\e', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u',
	'i', 'o', 'p', '[', ']', '\n', '\0', 'a', 's', 'd', 'f', 'g', 'h', 'j',
	'k', 'l', ';', '\'', '`', '\0', '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
	',', '.', '/', '\0', '\0', '\0', ' ', '\0' };

@implementation PS2Keyboard

+ (BOOL)probeWithAcpiNode:(lai_nsnode_t *)node
{
	LAI_CLEANUP_VAR lai_variable_t crs = LAI_VAR_INITIALIZER;
	struct lai_resource_view       res;
	LAI_CLEANUP_STATE lai_state_t  state;
	lai_api_error_t		       err;
	int			       ioa = -1, iob = -1, gsi = -1;

	lai_init_state(&state);
	if (laiex_view_resource(node, &crs, &res, &state) < 0)
		return NO;

	while ((err = lai_resource_iterate(&res)) == 0) {
		enum lai_resource_type type = lai_resource_get_type(&res);
		if (type == LAI_RESOURCE_IO) {
			if (ioa == -1)
				ioa = res.base;
			else
				iob = res.base;
		} else if (type == LAI_RESOURCE_IRQ) {
			while (lai_resource_next_irq(&res) !=
			    LAI_ERROR_END_REACHED) {
				if (gsi == -1)
					gsi = res.gsi;
				else {
					DKLog("PS2Keyboard",
					    "strange number of IRQs, gsi is %lu\n",
					    res.entry_idx);
					break;
				}
			}
		}
	}

	if (ioa == -1 || iob == -1 || gsi == -1) {
		DKLog("PS2Keyboard",
		    "failed to identify resources from ACPI\n");
		return NO;
	}

	ps2k = [[PS2Keyboard alloc] initWithPortA:ioa portB:iob GSI:gsi];

	return YES;
}

- initWithPortA:(uint8_t)portA portB:(uint8_t)portB GSI:(uint32_t)gsi
{
	self = [super init];

	ksnprintf(m_name, sizeof m_name, "PS2Keyboard0");
	[self registerDevice];

	DKDevLog(self, "I/O port A %d, port B %d, GSI %d\n", portA, portB, gsi);

	[IOApic handleGSI:gsi
	      withHandler:ps2_intr
		 argument:self
	      lowPolarity:false
	       atPriority:kSPL0];

	return self;
}

- (void)handleCode:(uint8_t)code
{
	// kprintf("got code %d\n", code);
	if (code & 0x80) {
	} else {
		extern FBTerm *syscon;
		[syscon input:codes[code]];
	}
}

@end

void
ps2_intr(intr_frame_t *frame, void *arg)
{
	uint8_t in = inb(0x60);
	[ps2k handleCode:in];
	md_eoi();
}
