
#include "AcpiPC.h"
#include "amd64.h"
#include "devicekit/DKDevice.h"
#include "kern/kern.h"
#include "kern/vm.h"
#include "lai/core.h"
#include "lai/helpers/sci.h"

acpi_rsdt_t *rsdt = NULL;
acpi_xsdt_t *xsdt = NULL;

uint8_t
pci_readb(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	return inb(0xCFC + (offset & 3));
}

uint16_t
pci_readw(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	return inw(0xCFC + (offset & 3));
}

uint32_t
pci_readl(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	return inl(0xCFC + (offset & 3));
}

void
pci_writeb(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset,
    uint8_t value)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	outb(0xCFC + (offset & 3), value);
}

void
pci_writew(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset,
    uint16_t value)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	outw(0xCFC + (offset & 3), value);
}

void
pci_writel(uint32_t bus, uint32_t slot, uint32_t function, uint32_t offset,
    uint32_t value)
{
	uint32_t address = (bus << 16) | (slot << 11) | (function << 8) |
	    (offset & ~(uint32_t)(3)) | 0x80000000;
	outl(0xCF8, address);
	outl(0xCFC + (offset & 3), value);
}

void
laihost_log(int level, const char *msg)
{
	DKLog("AcpiPC: lai: %s\n", msg);
}

__attribute__((noreturn)) void
laihost_panic(const char *msg)
{
	fatal("AcpiPC: lai: %s\n", msg);
}

void *
laihost_malloc(size_t size)
{
	return kmalloc(size);
}

void *
laihost_realloc(void *ptr, size_t size, size_t oldsize)
{
	return krealloc(ptr, size);
}

void
laihost_free(void *ptr, size_t size)
{
	return kfree(ptr);
}

void *
laihost_map(size_t address, size_t count)
{
	return (void *)P2V(address);
}

void
laihost_unmap(void *pointer, size_t count)
{
}

void *
laihost_scan(const char *sig, size_t index)
{
	acpi_header_t *header;
	size_t cur = 0;

	if (memcmp(sig, "DSDT", 4) == 0) {
		acpi_fadt_t *fadt = laihost_scan("FACP", 0);
		assert(fadt != NULL);
		return xsdt == NULL ? P2V((uintptr_t)fadt->dsdt) :
				      P2V(fadt->x_dsdt);
	}

	if (xsdt) {
		size_t ntables = (xsdt->header.length - sizeof(acpi_header_t)) /
		    sizeof(uint64_t);
		for (size_t i = 0; i < ntables; i++) {
			header = (acpi_header_t *)P2V(xsdt->tables[i]);

			if (memcmp(header->signature, sig, 4) != 0)
				continue;

			if (cur++ == index)
				return header;
		}
	} else {
		size_t ntables = (rsdt->header.length - sizeof(acpi_header_t)) /
		    sizeof(uint32_t);
		for (size_t i = 0; i < ntables; i++) {
			header = (acpi_header_t *)P2V(
			    (uintptr_t)rsdt->tables[i]);

			if (memcmp(header->signature, sig, 4) != 0)
				continue;

			if (cur++ == index)
				return header;
		}
	}

	return NULL;
}

void
laihost_outb(uint16_t port, uint8_t val)
{
	asm volatile("outb %0, %1" : : "a"(val), "d"(port));
}
void
laihost_outw(uint16_t port, uint16_t val)
{
	asm volatile("outw %0, %1" : : "a"(val), "d"(port));
}
void
laihost_outd(uint16_t port, uint32_t val)
{
	asm volatile("outl %0, %1" : : "a"(val), "d"(port));
}

uint8_t
laihost_inb(uint16_t port)
{
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "d"(port));
	return val;
}
uint16_t
laihost_inw(uint16_t port)
{
	uint16_t val;
	asm volatile("inw %1, %0" : "=a"(val) : "d"(port));
	return val;
}
uint32_t
laihost_ind(uint16_t port)
{
	uint32_t val;
	asm volatile("inl %1, %0" : "=a"(val) : "d"(port));
	return val;
}

void
laihost_pci_writeb(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset, uint8_t v)
{
	assert(seg == 0);
	return pci_writeb(bus, slot, fn, offset, v);
}
void
laihost_pci_writew(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset, uint16_t v)
{
	assert(seg == 0);
	return pci_writew(bus, slot, fn, offset, v);
}
void
laihost_pci_writed(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset, uint32_t v)
{
	assert(seg == 0);
	return pci_writel(bus, slot, fn, offset, v);
}

uint8_t
laihost_pci_readb(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset)
{
	assert(seg == 0);
	return pci_readb(bus, slot, fn, offset);
}
uint16_t
laihost_pci_readw(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset)
{
	assert(seg == 0);
	return pci_readw(bus, slot, fn, offset);
}
uint32_t
laihost_pci_readd(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t fn,
    uint16_t offset)
{
	assert(seg == 0);
	return pci_readl(bus, slot, fn, offset);
}

void
laihost_sleep(uint64_t ms)
{
	for (size_t i = 0; i < 1000 * ms; i++) {
		asm("pause");
	}
}

@implementation AcpiPC : DKDevice

+ (BOOL)probeWithRSDP:(rsdp_desc_t *)rsdp
{
	if (rsdp->Revision > 0)
		xsdt = (acpi_xsdt_t *)P2V(((rsdp_desc2_t *)rsdp)->XsdtAddress);
	else
		rsdt = (acpi_rsdt_t *)P2V((uintptr_t)rsdp->RsdtAddress);

	lai_set_acpi_revision(rsdp->Revision);
	lai_create_namespace();
	lai_enable_acpi(1);
	return YES;
}

@end
