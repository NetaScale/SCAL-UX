#ifndef IOAPIC_H_
#define IOAPIC_H_

#include "devicekit/DKDevice.h"
#include "kern/vm.h"

@interface IOApic : DKDevice
{
        uint32_t _id;
        vaddr_t _vaddr;
        uint32_t _gsi_base;
        uint32_t _n_redirs;

        _TAILQ_ENTRY(IOApic,) _ioapics_entries;
}

+ (void)handleGSI: (uint32_t) gsi;

- initWithID:(uint32_t)id address:(paddr_t *)address gsiBase:(uint32_t)gsiBase;

@end

#endif /* IOAPIC_H_ */
