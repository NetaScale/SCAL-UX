#include <stdint.h>
#include <string.h>

#include "amd64.h"
#include "kern/kern.h"
#include "kern/liballoc.h"
#include "kern/vm.h"

enum {
	kMMUPresent = 0x1,
	kMMUWrite = 0x2,
	kMMUUser = 0x4,
	kMMUWriteThrough = 0x8,
	kMMUCacheDisable = 0x10,
	kMMUAccessed = 0x40,
	kPageGlobal = 0x100,

	kMMUDefaultProt = kMMUPresent | kMMUWrite | kMMUUser,

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

spinlock_t g_1st_mem_lock;

static size_t pages_alloced = 0;

/* get the flags of a pte */
uint64_t *
pte_get_flags(uint64_t pte)
{
	return (uint64_t *)(pte & ~kMMUFrame);
}

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
pmap_makekpmap()
{
	pmap_t *pmap = kcalloc(sizeof *pmap, 1);
	pmap->pml4 = pmap_alloc_page(1);
	for (int i = 255; i < 511; i++) {
		pte_set(&pmap->pml4[i], pmap_alloc_page(1), kMMUDefaultProt);
	}
	return pmap;
}

pmap_t *
pmap_new()
{
	pmap_t *pmap = kcalloc(sizeof *pmap, 1);
	pmap->pml4 = pmap_alloc_page(1);
	for (int i = 255; i < 512; i++) {
		pte_set(P2V(&pmap->pml4[i]),
		    *(paddr_t *)P2V(&kmap->pmap->pml4[i]), kMMUDefaultProt);
	}
	return pmap;
}

void
pmap_free_sub(uint64_t *table, int level)
{
	if (table == NULL)
		return;
	if (level > 1)
		for (int i = 0; i < 255; i++) {
			pte_t entry = *(pte_t *)P2V(&table[i]);
			pmap_free_sub(pte_get_addr(entry), level - 1);
		}
	kprintf("anon page %p enqueued for reclamation\n", table);
}

void
pmap_free(pmap_t *pmap)
{
	pmap_free_sub(pmap->pml4, 4);
}

void
vm_activate(pmap_t *pmap)
{
	uint64_t val = (uint64_t)pmap->pml4;
	write_cr3(val);
}

static uint64_t
vm_prot_to_i386(vm_prot_t prot)
{
	return (prot & kVMRead ? kMMUPresent : 0) |
	    (prot & kVMWrite ? kMMUWrite : 0) | kMMUUser;
}

/*
 * TODO: probably belongs in vm.c not machdep
 * TODO: evict unneeded pages if exhausted
 */
vm_page_t *
vm_alloc_page()
{
	vm_page_t *page = NULL;
	int i = 0;

	lock(&g_1st_mem_lock);
	for (i = 0; i < g_1st_mem->npages; i++) {
		if (g_1st_mem->pages[i].type == kPageFree) {
			g_1st_mem->pages[i].paddr = g_1st_mem->paddr +
			    PGSIZE * i;
			g_1st_mem->pages[i].type = kPageObject;
			page = &g_1st_mem->pages[i];
			pages_alloced += 1;
			break;
		}
	}
	unlock(&g_1st_mem_lock);

	memset(P2V(page->paddr), 0, PGSIZE);

	if (!page)
		fatal("pages exhausted");

	return page;
}

/*
 * Allocate contiguous pages. This is strictly for use only during early init;
 * in normal operation the freelists may get disordered, and it carries out no
 * locking.
 */
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
				((char *)P2V(addr))[i2] = 0x0;

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
pmap_descend(uint64_t *table, size_t index, bool alloc, uint64_t mmuprot)
{
	uint64_t *entry = P2V((&table[index]));
	uint64_t *addr = NULL;

	if (*entry & kMMUPresent) {
		addr = pte_get_addr(*entry);
	} else if (alloc) {
		addr = (uint64_t *)pmap_alloc_page(1);
		if (!addr)
			fatal("out of pages");
		pte_set(entry, addr, mmuprot);
	}

	return addr;
}

paddr_t
pmap_trans(pmap_t *pmap, vaddr_t virt)
{
	uintptr_t virta = (uintptr_t)virt;
	pml4e_t *pml4 = pmap->pml4;
	int pml4i = ((virta >> 39) & 0x1FF);
	int pdpti = ((virta >> 30) & 0x1FF);
	int pdi = ((virta >> 21) & 0x1FF);
	int pti = ((virta >> 12) & 0x1FF);
	int pi = ((virta)&0xFFF);

	pdpte_t *pdpte;
	pde_t *pde;
	pte_t *pte;

	kprintf("%d %d %d %d\n", pml4i, pdpti, pdi, pti);

	pdpte = pmap_descend(pml4, pml4i, false, 0);
	if (!pdpte) {
		kprintf("no pml4 entry\n");
		return 0x0;
	}

	pde = pmap_descend(pdpte, pdpti, false, 0);
	if (!pde) {
		kprintf("no pdpt entry\n");
		return 0x0;
	}

	pte = pmap_descend(pde, pdi, false, 0);
	if (!pte) {
		kprintf("no pte entry\n");
		return 0x0;
	}

	pte = P2V(pte);

	if (!(pte[pti] & kMMUPresent))
		return 0x0;
	else
		return pte_get_addr(pte[pti]) + pi;
}

/* map a single given page at a virtual address. pml4 should be a phys addr */
void
pmap_enter(pml4e_t *pml4, paddr_t phys, vaddr_t virt, vm_prot_t prot)
{
	uintptr_t virta = (uintptr_t)virt;
	int pml4i = ((virta >> 39) & 0x1FF);
	int pdpti = ((virta >> 30) & 0x1FF);
	int pdi = ((virta >> 21) & 0x1FF);
	int pti = ((virta >> 12) & 0x1FF);
	uint64_t mmuprot = vm_prot_to_i386(prot);
	pdpte_t *pdpte;
	pde_t *pde;
	pte_t *pte;

	// kprintf("pmap_enter: phys 0x%lx at virt 0x%lx\n", phys, virt);
	pdpte = pmap_descend(pml4, pml4i, true, kMMUDefaultProt);
	pde = pmap_descend(pdpte, pdpti, true, kMMUDefaultProt);
	pte = pmap_descend(pde, pdi, true, kMMUDefaultProt);

	pte_set(P2V(&pte[pti]), phys, vm_prot_to_i386(prot));
}

/*
 * map range of physical addresses into an addres sspace. pml4 must be phys.
 * \size is in bytes, must be multiple of PGSIZE.
 */
void
pmap_map(pmap_t *pmap, paddr_t phys, vaddr_t virt, size_t size, vm_prot_t prot)
{
	size_t npages = size / PGSIZE;
#ifdef DEBUG_VM
	kprintf("pmap_map: mapping phys %p at virt %p (size 0x%lx)\n", phys,
	    virt, size);
#endif
	for (int i = 0; i < npages; i++, virt += PGSIZE, phys += PGSIZE) {
		pmap_enter(pmap->pml4, phys, virt, prot);
	}
}

void
pmap_stats()
{
	kprintf("%lu pages alloced\n", pages_alloced);
}

void
pmap_invlpg(vaddr_t addr)
{
	asm volatile("invlpg %0" ::"m"(addr) : "memory");
}
