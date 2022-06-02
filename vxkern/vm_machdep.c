#include "amd64.h"
#include "vm.h"
#include "vxkern.h"

enum {
	kMMUPresent = 0x1,
	kMMUWrite = 0x2,
	kMMUUser = 0x4,
	kMMUWriteThrough = 0x8,
	kMMUCacheDisable = 0x10,
	kMMUAccessed = 0x40,
	kPageGlobal = 0x100,
	kMMUFrame = 0x000FFFFFFFFFF000
};

/* get the physical address to which a pte points */
uint64_t *
pte_get_addr(uint64_t pte)
{
	return (uint64_t *)(pte & kMMUFrame);
}

/* set a pte to a given addy and flags. pte must be a virt addr. */
void
pte_set(uint64_t *pte, paddr_t addr, uint64_t flags)
{
	uintptr_t a = (uintptr_t)addr;
	a &= kMMUFrame;
	*pte &= ~kMMUFrame;
	*pte |= a;
}

paddr_t pmap_alloc_page(size_t n) {
	retry:
	for (int i = 0; i < g_1st_mem->npages; i++) {
		if (g_1st_mem->pages[i].type == kPageFree) {
			for (int i2 = 0; i2 < n; i2++) {
				if (g_1st_mem->pages[i + i2].type != kPageFree)
					goto retry; /* noncontiguous */
			}
			kprintf("giving it page %p\n", g_1st_mem->paddr + PGSIZE * i);
			return g_1st_mem->paddr + PGSIZE * i;
		}
	}
	return NULL;
}

/*
 * get the physical address at which a page table entry points. optionally
 * allocate a new entry, setting appropriate flags. table should be a pointer
 * to the physical location of the table.
 */
uint64_t *
pmap_descend(uint64_t *table, size_t index)
{
	uint64_t *entry = &P2V(table)[index];
	uint64_t *addr;

	if (*entry & kMMUPresent)
		addr = pte_get_addr(*entry);
	else {
		addr = (uint64_t*)pmap_alloc_page(1);
		if (!addr)
			fatal("out of pages");
		pte_set(entry, addr, kMMUPresent | kMMUWrite | kMMUUser);
	}

	return addr;
}

/* map a single given page at a virtual address. pml4 should be a phys addr */
void
pmap_enter(pml4e_t *pml4, paddr_t phys, vaddr_t virt)
{
	uintptr_t physa = (uintptr_t)phys;
	int pml4i = ((physa >> 39) & 0x1FF);
	int pdpti = ((physa >> 30) & 0x1FF);
	int pdi = ((physa >> 21) & 0x1FF);
	int pti = ((physa >> 12) & 0x1FF);
	pdpte_t *pdpte;
	pde_t *pde;
	pte_t *pte;

	pdpte = pmap_descend(pml4, pml4i);
	pde = pmap_descend(pdpte, pdpti);
	pte = pmap_descend(pde, pdi);

	pte_set(P2V(&pte[pti]), phys, kMMUPresent | kMMUWrite | kMMUUser);
}
