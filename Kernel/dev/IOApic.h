#ifndef IOAPIC_H_
#define IOAPIC_H_

#include "devicekit/DKDevice.h"
#include "kern/vm.h"
#include "machine/intr.h"

@interface IOApic : DKDevice {
	uint32_t _id;
	vaddr_t	 _vaddr;
	uint32_t _gsi_base;
	uint32_t _n_redirs;

	_TAILQ_ENTRY(IOApic, ) _ioapics_entries;
}

/**
 * Install a handler for the given GSI (i.e. IRQ.)
 *
 * The I/O APIC handling that GSI is identified and routes the interrupt; the
 * handler is then installed into the IDT by the kernel.
 *
 * @returns 0 if handler installed successfully
 */
+ (int)handleGSI:(uint32_t)gsi
     withHandler:(intr_handler_fn_t)handler
     argument:(void*) arg
      atPriority:(spl_t)prio;

- initWithID:(uint32_t)id address:(paddr_t *)address gsiBase:(uint32_t)gsiBase;

@end

#endif /* IOAPIC_H_ */
