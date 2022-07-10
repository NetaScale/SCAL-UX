
#include <amd64/amd64.h>

#include <kern/vm.h>
#include <libkern/klib.h>

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
	paddr_t pml4;
};

static uint64_t *pte_get_addr(uint64_t pte);
static void	 pte_set(uint64_t *pte, paddr_t addr, uint64_t flags);

struct vm_pregion_queue vm_pregion_queue = TAILQ_HEAD_INITIALIZER(
    vm_pregion_queue);
static vm_map_entry_t hhdm_entry, kernel_entry, kheap_entry;
static vm_object_t    hhdm_obj, kernel_obj, kheap_obj;
static pmap_t	      kpmap;

vm_page_t *
vm_allocpage(bool sleep)
{
	vm_page_t *page = TAILQ_FIRST(&pg_freeq);
	TAILQ_REMOVE(&pg_freeq, page, queue);
	return page;
}

void
arch_vm_init(paddr_t kphys)
{
	TAILQ_INIT(&kmap.entries);

	hhdm_entry.start = (vaddr_t)HHDM_BASE;
	hhdm_entry.end = (vaddr_t)HHDM_BASE + HHDM_SIZE;
	hhdm_entry.obj = &hhdm_obj;
	hhdm_obj.type = kDirectMap;
	hhdm_obj.dmap.base = 0x0;
	TAILQ_INSERT_TAIL(&kmap.entries, &hhdm_entry, queue);

	kheap_entry.start = (vaddr_t)KHEAP_BASE;
	kheap_entry.end = (vaddr_t)KHEAP_BASE + KHEAP_SIZE;
	kheap_entry.obj = &kheap_obj;
	kheap_obj.type = kKHeap;
	TAILQ_INSERT_TAIL(&kmap.entries, &kheap_entry, queue);

	kernel_entry.start = (vaddr_t)KERN_BASE;
	kernel_entry.end = (vaddr_t)KERN_BASE + KERN_SIZE;
	kernel_entry.obj = &kernel_obj;
	hhdm_obj.type = kDirectMap;
	hhdm_obj.dmap.base = kphys;
	TAILQ_INSERT_TAIL(&kmap.entries, &kernel_entry, queue);

	kmap.pmap = &kpmap;
	kpmap.pml4 = (paddr_t)read_cr3();

	/* pre-allocate the top 256. they are globally shared. */
	for (int i = 255; i < 511; i++) {
		uint64_t *pml4 = P2V(kpmap.pml4);
		if (pte_get_addr(pml4[i]) == NULL) {
			pte_set(&pml4[i], vm_allocpage(0),
			    0x0);
		}
	}
}

/* get the flags of a pte */
uint64_t *
pte_get_flags(uint64_t pte)
{
	return (uint64_t *)(pte & ~kMMUFrame);
}

/* get the physical address to which a pte points */
static uint64_t *
pte_get_addr(uint64_t pte)
{
	return (uint64_t *)(pte & kMMUFrame);
}

/* reset a pte to a given addy and flags. pte must be a virt addr. */
static void
pte_set(uint64_t *pte, paddr_t addr, uint64_t flags)
{
	uintptr_t a = (uintptr_t)addr;
	a &= kMMUFrame;
	*pte = 0x0;
	*pte = a | flags;
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
	kprintf("page %p of pagetable free\n", table);
}

void
pmap_free(pmap_t *pmap)
{
	pmap_free_sub(pmap->pml4, 4);
	for (int i = 0; i < 255; i++)
		((uint64_t *)P2V(pmap->pml4))[i] = 0x0;
}

void
vm_activate(vm_map_t *map)
{
	uint64_t val = (uint64_t)map->pmap->pml4;
	write_cr3(val);
}

static uint64_t
vm_prot_to_i386(vm_prot_t prot)
{
	return (prot & kVMRead ? kMMUPresent : 0) |
	    (prot & kVMWrite ? kMMUWrite : 0) | kMMUUser;
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
		vm_page_t *page = vm_allocpage(true);
		if (!page)
			fatal("out of pages");
		addr = (uint64_t *)page->paddr;
		pte_set(entry, addr, mmuprot);
	}

	return addr;
}

paddr_t
pmap_trans(pmap_t *pmap, vaddr_t virt)
{
	uintptr_t virta = (uintptr_t)virt;
	pml4e_t	*pml4 = pmap->pml4;
	int	  pml4i = ((virta >> 39) & 0x1FF);
	int	  pdpti = ((virta >> 30) & 0x1FF);
	int	  pdi = ((virta >> 21) & 0x1FF);
	int	  pti = ((virta >> 12) & 0x1FF);
	int	  pi = ((virta)&0xFFF);

	pdpte_t *pdpte;
	pde_t   *pde;
	pte_t   *pte;

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
pmap_enter(pmap_t *pmap, paddr_t phys, vaddr_t virt, vm_prot_t prot)
{
	uintptr_t virta = (uintptr_t)virt;
	int	  pml4i = ((virta >> 39) & 0x1FF);
	int	  pdpti = ((virta >> 30) & 0x1FF);
	int	  pdi = ((virta >> 21) & 0x1FF);
	int	  pti = ((virta >> 12) & 0x1FF);
	pml4e_t	*pml4 = pmap->pml4;
	pdpte_t	*pdpte;
	pde_t    *pde;
	pte_t    *pte;

	// kprintf("pmap_enter: phys 0x%lx at virt 0x%lx\n", phys, virt);
	pdpte = pmap_descend(pml4, pml4i, true, kMMUDefaultProt);
	pde = pmap_descend(pdpte, pdpti, true, kMMUDefaultProt);
	pte = pmap_descend(pde, pdi, true, kMMUDefaultProt);

	pte_set(P2V(&pte[pti]), phys, vm_prot_to_i386(prot));
}
