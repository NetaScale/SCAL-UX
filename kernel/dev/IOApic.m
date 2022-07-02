
#include "dev/IOApic.h"
#include "kern/queue.h"

enum {
	kDeliveryModeFixed = 0x0,
	kDeliveryModeLowPriority = 0x1,
	kDeliveryModeSmi = 0x2,
	kDeliveryModeNmi = 0x4,
	kDeliveryModeInit = 0x5,
	kDeliveryModeExtInt = 0x7
};

enum {
	kDestinationModePhysical = 0x0,
	kDestinationModeLogical = 0x1,
};

enum {
	kRegisterId = 0x0,
	kRegisterVersion = 0x1,
	kRegisterArbitrationPriority = 0x2,
	kRegisterRedirectionEntriesBase = 0x10,
};

static inline uint32_t
redirection_register(uint32_t entry)
{
	return kRegisterRedirectionEntriesBase + entry * 2;
}

static uint32_t
ioapic_read(vaddr_t *vaddr, uint32_t reg)
{
	volatile uint32_t *base = (volatile uint32_t *)vaddr;
	base[0] = reg;
	return base[4];
}

static void
ioapic_write(vaddr_t *vaddr, uint32_t reg, uint32_t val)
{
	volatile uint32_t *base = (volatile uint32_t *)vaddr;
	base[0] = reg;
	base[4] = val;
}

static void
ioapic_route(vaddr_t *vaddr, uint8_t i, uint8_t vec)
{
	uint64_t ent = vec;
	// ent |= kDeliveryModeLowPriority << 8;
	// ent |= kDestinationModeLogical << 11;
	ent |= kDeliveryModeFixed << 8;
	ent |= kDestinationModePhysical << 11;
	ent |= 1ul << 56; /* lapic id 1 */
	ent |= 1 << 13;	  /* polarity low */
	ent |= 1 << 15;	  /* level triggered */
	ioapic_write(vaddr, redirection_register(i), ent);
	ioapic_write(vaddr, redirection_register(i) + 1, ent >> 32);
}

static _TAILQ_HEAD(, IOApic, ) ioapics = TAILQ_HEAD_INITIALIZER(ioapics);

@implementation IOApic

- initWithID:(uint32_t)id address:(paddr_t *)paddr gsiBase:(uint32_t)gsiBase
{
	self = [super init];

	_id = id;
	_vaddr = P2V(paddr);
	_gsi_base = gsiBase;
	_n_redirs = ((ioapic_read(_vaddr, kRegisterVersion) >> 16) & 0xff) + 1;

	TAILQ_INSERT_TAIL(&ioapics, self, _ioapics_entries);

	ksnprintf(name, sizeof name, "IOApic%d", id);
	[self registerDevice];

	return self;
}

+ (void)handleGSI:(uint32_t)gsi
{
	IOApic *ioapic;
	TAILQ_FOREACH (ioapic, &ioapics, _ioapics_entries) {
		if (ioapic->_gsi_base <= gsi &&
		    ioapic->_gsi_base + ioapic->_n_redirs > gsi) {
			ioapic_route(ioapic->_vaddr, gsi - ioapic->_gsi_base,
			    0x20 + gsi);
		}
	}
}

@end
