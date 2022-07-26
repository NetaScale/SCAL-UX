#include "PCIBus.h"
#include "acpi/laiex.h"
#include "lai/core.h"
#include "lai/error.h"
#include "lai/host.h"

enum {
	kVendorID = 0x0,
	kDeviceID = 0x2,
	kSubclass = 0xa,
	kClass = 0xb,
	kHeaderType = 0xe, /* bit 7 = is multifunction */
	kBaseAddress0 = 0x10
};

static int
laiex_eval_one_int(lai_nsnode_t *node, const char *path, uint64_t *out,
    lai_state_t *state)
{
	LAI_CLEANUP_VAR lai_variable_t var = LAI_VAR_INITIALIZER;
	lai_nsnode_t		     *handle;
	int			       r;

	handle = lai_resolve_path(node, path);
	if (handle == NULL)
		return LAI_ERROR_NO_SUCH_NODE;

	r = lai_eval(&var, handle, state);
	if (r != LAI_ERROR_NONE)
		return r;

	return lai_obj_get_integer(&var, out);
}

#if 0
static void
iterate(lai_nsnode_t *obj, size_t depth)
{
	struct lai_ns_child_iterator iterator =
	    LAI_NS_CHILD_ITERATOR_INITIALIZER(obj);
	lai_nsnode_t *node;

	while ((node = lai_ns_child_iterate(&iterator))) {
		for (int i = 0; i < depth; i++)
			kprintf(" ");
		kprintf("NAME: %s\n", node->name);
		iterate(node, depth + 2);
	}
}
#endif

@implementation PCIBus

+ (BOOL)probeWithAcpiNode:(lai_nsnode_t *)node;
{
	uint64_t		      seg = -1, bbn = -1;
	LAI_CLEANUP_STATE lai_state_t state;
	int			      r;

	lai_init_state(&state);

	r = laiex_eval_one_int(node, "_SEG", &seg, &state);
	if (r != LAI_ERROR_NONE) {
		if (r == LAI_ERROR_NO_SUCH_NODE) {
			seg = 0;
		} else {
			DKLog("PCIBus: failed to evaluate _SEG: %d\n", r);
			return NO;
		}
	}

	r = laiex_eval_one_int(node, "_BBN", &bbn, &state);
	if (r != LAI_ERROR_NONE) {
		if (r == LAI_ERROR_NO_SUCH_NODE) {
			bbn = 0;
		} else {
			DKLog("PCIBus: failed to evaluate _BBN: %d\n", r);
		}
	}

	[[self alloc] initWithSeg:seg bus:bbn];

	return YES;
}

static void
doFunction(PCIBus *bus, uint16_t seg, uint8_t busNum, uint8_t slot, uint8_t fun)
{
	uint8_t class, subClass;
	uint16_t vendorId, deviceId;

#define CFG_READ(WIDTH, OFFSET) \
	laihost_pci_read##WIDTH(seg, busNum, slot, fun, OFFSET)

	vendorId = CFG_READ(w, kVendorID);
	if (vendorId == 0xffff)
		return;
	deviceId = CFG_READ(w, kDeviceID);
	class = CFG_READ(b, kClass);
	subClass = CFG_READ(b, kSubclass);

	DKLog("Function at %d:%d:%d:%d: Vendor %x, device %x, class %d/%d\n", seg, busNum,
	    slot, fun, vendorId, deviceId, class, subClass);


	if (class == 6 && subClass == 4) {
		DKLog("FIXME: PCI-PCI Bridge\n");
		return;
	}
}

- initWithSeg:(uint8_t)seg bus:(uint8_t)bus
{
	self = [super init];

	ksnprintf(name, sizeof name, "PCIBus%d@%d", seg, bus);
	[self registerDevice];

	for (int slot = 0; slot < 32; slot++) {
		size_t cntFun = 1;

		if (laihost_pci_readw(seg, bus, slot, 0, kVendorID) == 0xffff)
			continue;

		if (laihost_pci_readb(seg, bus, slot, 0, kHeaderType) &
		    (1 << 7))
			cntFun = 8;

		for (int fun = 0; fun < cntFun; fun++)
			doFunction(self, seg, bus, slot, fun);
	}

	return self;
}

@end
