#include <stdint.h>

#include <sys/amd64_misc.h>
#include <sys/vm.h>
#include <sys/vxkern.h>

#include "liballoc.h"

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

enum {
	kPML4Shift = 0x39,
	kPDPTShift = 0x30,
	kPDIShift = 0x21,
	kPTShift = 0x12,
};

struct pmap {
	pml4e_t *pml4;
};

static size_t pages_alloced = 0;

/* get the physical address to which a pte points */
uint64_t *
pte_get_addr(uint64_t pte)
{
	return (uint64_t *)(pte & kMMUFrame);
}

/* reset a pte to a given addy and flags. pte must be a virt addr. */
void
pte_set(uint64_t *pte, paddr_t addr, uint64_t flags)
{
	uintptr_t a = (uintptr_t)addr;
	a &= kMMUFrame;
	*pte = 0x0;
	*pte = a | flags;
}

pmap_t *
pmap_new()
{
	pmap_t *pmap = kcalloc(sizeof *pmap, 1);
	pmap->pml4 = pmap_alloc_page(1);
	return pmap;
}

void pmap_activate(pmap_t *pmap)
{
	uint64_t val = (uint64_t)pmap->pml4;
	write_cr3(val);
}

paddr_t
pmap_alloc_page(size_t n)
{
	int i = 0;
	paddr_t addr;

	// kprintf("pmap_alloc_pages(%lu)\n", n);

retry:
	for (; i < g_1st_mem->npages; i++) {
		if (g_1st_mem->pages[i].type == kPageFree) {
			for (int i2 = 0; i2 < n; i2++) {
				if (g_1st_mem->pages[i + i2].type != kPageFree)
					goto retry; /* noncontiguous */
			}
			// kprintf("giving it page %p\n",
			//    g_1st_mem->paddr + PGSIZE * i);
			for (int i3 = 0; i3 < n; i3++) {
				//	kprintf("using page %d\n", i3);
				g_1st_mem->pages[i + i3].type = kPageVMInternal;
			}
			pages_alloced += n;
			// kprintf("returned n pages from %p\n", );

			addr = g_1st_mem->paddr + PGSIZE * i;
			/* zero it out here for now */
			for (int i2 = 0; i2 < PGSIZE * n; i2++)
				((char *)addr)[i2] = 0x0;

			return addr;
		}
	}

	fatal("pages exhausted\n");
}

/*
 * get the physical address at which a page table entry points. optionally
 * allocate a new entry, setting appropriate flags. table should be a pointer
 * to the physical location of the table.
 */
uint64_t *
pmap_descend(uint64_t *table, size_t index, bool alloc)
{
	uint64_t *entry = P2V((&table[index]));
	uint64_t *addr = NULL;

	if (*entry & kMMUPresent) {
		addr = pte_get_addr(*entry);
	} else if (alloc) {
		addr = (uint64_t *)pmap_alloc_page(1);
		if (!addr)
			fatal("out of pages");
		pte_set(entry, addr, kMMUPresent | kMMUWrite | kMMUUser);
	}

	return addr;
}

void
pmap_trans(pml4e_t *pml4, vaddr_t virt)
{
	uintptr_t virta = (uintptr_t)virt;
	int pml4i = ((virta >> 39) & 0x1FF);
	int pdpti = ((virta >> 30) & 0x1FF);
	int pdi = ((virta >> 21) & 0x1FF);
	int pti = ((virta >> 12) & 0x1FF);
	int pi = ((virta)&0xFFF);

	pdpte_t *pdpte;
	pde_t *pde;
	pte_t *pte;

	kprintf("%d %d %d %d\n", pml4i, pdpti, pdi, pti);

	pdpte = pmap_descend(pml4, pml4i, false);
	if (!pdpte) {
		kprintf("no pml4 entry");
		return;
	}

	pde = pmap_descend(pdpte, pdpti, false);
	if (!pde) {
		kprintf("no pdpt entry");
		return;
	}

	pte = pmap_descend(pde, pdi, false);
	if (!pte) {
		kprintf("no pte entry");
		return;
	}

	if (!(pte[pti] & kMMUPresent))
		kprintf("no pte entry\n");
	else
		kprintf("virt addr %p translates to phys addr 0x%lx\n", virt,
		    (uintptr_t)pte_get_addr(pte[pti]) + pi);
}

/* map a single given page at a virtual address. pml4 should be a phys addr */
void
pmap_enter(pml4e_t *pml4, paddr_t phys, vaddr_t virt)
{
	uintptr_t virta = (uintptr_t)virt;
	int pml4i = ((virta >> 39) & 0x1FF);
	int pdpti = ((virta >> 30) & 0x1FF);
	int pdi = ((virta >> 21) & 0x1FF);
	int pti = ((virta >> 12) & 0x1FF);
	pdpte_t *pdpte;
	pde_t *pde;
	pte_t *pte;

	// kprintf("pmap_enter: phys 0x%lx at virt 0x%lx\n", phys, virt);
	pdpte = pmap_descend(pml4, pml4i, true);
	pde = pmap_descend(pdpte, pdpti, true);
	pte = pmap_descend(pde, pdi, true);

	pte_set(P2V(&pte[pti]), phys, kMMUPresent | kMMUWrite | kMMUUser);
}

/*
 * map range of physical addresses into an addres sspace. pml4 must be phys.
 * \size is in bytes, must be multiple of PGSIZE.
 */
void
pmap_map(pmap_t *pmap, paddr_t phys, vaddr_t virt, size_t size)
{
	size_t npages = size / PGSIZE;
	kprintf("pmap_map: mapping phys %p at virt %p (size %lx)\n", phys, virt,
	    size);
	for (int i = 0; i < npages; i++, virt += PGSIZE, phys += PGSIZE) {
		pmap_enter(pmap->pml4, phys, virt);
	}
}

void
pmap_stats()
{
	kprintf("%lu pages alloced\n", pages_alloced);
}