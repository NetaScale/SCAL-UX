#include <amd64/amd64.h>
#include <amd64/kasan.h>

#include <kern/vm.h>
#include <libkern/klib.h>

#include "kern/task.h"
#include "machine/vm_machdep.h"

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
static spinlock_t     invlpg_global_lock;

static vm_page_t *
vm_page_from_paddr(paddr_t paddr)
{
	vm_pregion_t *preg;

	TAILQ_FOREACH (preg, &vm_pregion_queue, queue) {
		if (preg->paddr <= paddr &&
		    (preg->paddr + PGSIZE * preg->npages) > paddr) {
			return &preg->pages[(paddr - preg->paddr) / PGSIZE];
		}
	}

	return NULL;
}

vm_page_t *
vm_pagealloc(bool sleep)
{
	vm_page_t *page;
	spl_t	   spl;

	spl = splvm();
	VM_PAGE_QUEUES_LOCK();
	page = TAILQ_FIRST(&pg_freeq);
	if (!page) {
		fatal("vm_allocpage: oom not yet handled\n");
	}
	assert(page->free);
	TAILQ_REMOVE(&pg_freeq, page, queue);
	TAILQ_INSERT_TAIL(&pg_wireq, page, queue);
	vmstat.pgs_free--;
	vmstat.pgs_wired++;
	page->wirecnt = 1;
	page->free = 0;

	VM_PAGE_QUEUES_UNLOCK();
	splx(spl);

	return page;
}

void
vm_pagefree(vm_page_t *page)
{
	assert(!page->free);
	page->anon = NULL;
	page->free = 1;
	TAILQ_INSERT_HEAD(&pg_freeq, page, queue);
}

vm_page_t *
vm_pagealloc_zero(bool sleep)
{
	vm_page_t *page = vm_pagealloc(sleep);
	memset(P2V(page->paddr), 0x0, PGSIZE);
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

#if 0
	kheap_entry.start = (vaddr_t)KHEAP_BASE;
	kheap_entry.end = (vaddr_t)KHEAP_BASE + KHEAP_SIZE;
	kheap_entry.obj = &kheap_obj;
	kheap_obj.type = kKHeap;
	TAILQ_INSERT_TAIL(&kmap.entries, &kheap_entry, queue);
#endif

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
			pte_set(&pml4[i], vm_pagealloc_zero(0)->paddr,
			    kMMUDefaultProt);
			vmstat.pgs_pgtbl++;
		}
	}
}

/* table is a PHYSICAL address */
void
pmap_free_sub(uint64_t *table, int level)
{
	vm_page_t *page;

	if (table == NULL)
		return;

	table = P2V(table);

	/*
	 * we don't free the individual mappings (there shouldn't *be*
	 * any left, as they should've been removed by vm_deallocate).
	 * Only the page tables themselves are freed.
	 */
	if (level > 1)
		for (int i = 0; i < 512; i++) {
			pte_t *entry = &table[i];
			pmap_free_sub(pte_get_addr(*entry), level - 1);
		}

	page = vm_page_from_paddr(V2P(table));
	vmstat.pgs_pgtbl--;
	/* all pages go onto the wired queue by default right now, so remove */
	TAILQ_REMOVE(&pg_wireq, page, queue);
	vm_pagefree(page);
}

void
pmap_free(pmap_t *pmap)
{
	uint64_t *vpml4 = P2V(pmap->pml4);
	for (int i = 0; i < 255; i++) {
		pte_t *entry = &vpml4[i];
		pmap_free_sub(pte_get_addr(*entry), 3);
	}
}

pmap_t *
pmap_new()
{
	pmap_t *pmap = kcalloc(sizeof *pmap, 1);
	pmap->pml4 = vm_pagealloc_zero(1)->paddr;
	for (int i = 255; i < 512; i++) {
		uint64_t *pml4 = P2V(pmap->pml4);
		uint64_t *kpml4 = P2V(kmap.pmap->pml4);
		pte_set(&pml4[i], (void *)kpml4[i], kMMUDefaultProt);
	}
	return pmap;
}

void
invlpg(vaddr_t addr)
{
	asm volatile("invlpg %0" : : "m"(*((const char *)addr)) : "memory");
}

void	     arch_ipi_invlpg(cpu_t *cpu);
vaddr_t	     invlpg_addr;
volatile int invlpg_done_cnt;

void
global_invlpg(vaddr_t vaddr)
{
	spl_t spl = splhigh();
	lock(&invlpg_global_lock);
	invlpg_addr = vaddr;
	invlpg_done_cnt = 1;
	for (int i = 0; i < ncpus; i++) {
		if (&cpus[i] == CURCPU())
			continue;

		arch_ipi_invlpg(&cpus[i]);
	}
	invlpg(vaddr);
	while (invlpg_done_cnt != ncpus)
		__asm__("pause");
	unlock(&invlpg_global_lock);
	splx(spl);
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
		vm_page_t *page = vm_pagealloc_zero(true);
		if (!page)
			fatal("out of pages");
		vmstat.pgs_pgtbl++;
		addr = (uint64_t *)page->paddr;
		pte_set(entry, addr, mmuprot);
	}

	return addr;
}

/**
 * @returns physical address of pte, or NULL if none exists
 */
pte_t *
pmap_fully_descend(pmap_t *pmap, vaddr_t virt)
{
	uintptr_t virta = (uintptr_t)virt;
	pml4e_t	*pml4 = pmap->pml4;
	int	  pml4i = ((virta >> 39) & 0x1FF);
	int	  pdpti = ((virta >> 30) & 0x1FF);
	int	  pdi = ((virta >> 21) & 0x1FF);
	int	  pti = ((virta >> 12) & 0x1FF);
	pdpte_t	*pdptes;
	pde_t    *pdes;
	pte_t    *ptes;

	pdptes = pmap_descend(pml4, pml4i, false, 0);
	if (!pdptes) {
		// kprintf("no pml4 entry\n");
		return 0x0;
	}

	pdes = pmap_descend(pdptes, pdpti, false, 0);
	if (!pdes) {
		// kprintf("no pdpt entry\n");
		return 0x0;
	}

	ptes = pmap_descend(pdes, pdi, false, 0);
	if (!ptes) {
		// kprintf("no pte entry\n");
		return 0x0;
	}

	return &ptes[pti];
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
		// kprintf("no pml4 entry\n");
		return 0x0;
	}

	pde = pmap_descend(pdpte, pdpti, false, 0);
	if (!pde) {
		// kprintf("no pdpt entry\n");
		return 0x0;
	}

	pte = pmap_descend(pde, pdi, false, 0);
	if (!pte) {
		// kprintf("no pte entry\n");
		return 0x0;
	}

	pte = P2V(pte);

	if (!(pte[pti] & kMMUPresent))
		return 0x0;
	else
		return pte_get_addr(pte[pti]) + pi;
}

void
pmap_enter(vm_map_t *map, vm_page_t *page, vaddr_t virt, vm_prot_t prot)
{
	pv_entry_t *ent = kmalloc(sizeof *ent);

	ent->map = map;
	ent->vaddr = virt;

	pmap_enter_kern(map->pmap, page->paddr, virt, prot);
	LIST_INSERT_HEAD(&page->pv_table, ent, pv_entries);
}

void
pmap_reenter(vm_map_t *map, vm_page_t *page, vaddr_t virt, vm_prot_t prot)
{
	pmap_enter_kern(map->pmap, page->paddr, virt, prot);
}

void
pmap_enter_kern(pmap_t *pmap, paddr_t phys, vaddr_t virt, vm_prot_t prot)
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
	pte_t    *pti_virt;

	// kprintf("pmap_enter: phys 0x%lx at virt 0x%lx\n", phys, virt);
	pdpte = pmap_descend(pml4, pml4i, true, kMMUDefaultProt);
	pde = pmap_descend(pdpte, pdpti, true, kMMUDefaultProt);
	pte = pmap_descend(pde, pdi, true, kMMUDefaultProt);

	pti_virt = P2V(&pte[pti]);

	if (pte_get_addr(*pti_virt) != NULL) {
		//fatal("pmap_enter_kern: address already has a mapping\n");
		/* TODO(med): do we care about this case? */
	}

	pte_set(pti_virt, phys, vm_prot_to_i386(prot));
}

void
pmap_reenter_all_readonly(vm_page_t *page)
{
	pv_entry_t *pv, *tmp;

	lock(&page->lock);
	LIST_FOREACH_SAFE(pv, &page->pv_table, pv_entries, tmp) {
		//lock(pv->map->lock);
		pmap_reenter(pv->map, page, pv->vaddr, kVMRead | kVMExecute);
		global_invlpg(pv->vaddr);
	}
	unlock(&page->lock);
}

void
pmap_unenter(vm_map_t *map, vm_page_t *page, vaddr_t vaddr, pv_entry_t *pv)
{
	/** \todo free no-longer-needed page tables */
	pte_t  *pte = pmap_fully_descend(map->pmap, vaddr);
	paddr_t paddr;

	/*
	 * XXX: maybe we should be more careful about this
	 * perhaps instead of bulk pmap_unenter'ing on deallocation, instead
	 * we should iterate through the object's page queue (amap for anon
	 * objects)
	 */
	if (pte == NULL)
		return;

	pte = P2V(pte);
	paddr = pte_get_addr(*pte);
	if (*pte == 0)
		return;
	*pte = 0x0;

	invlpg(vaddr);

	if (!page) {
		page = vm_page_from_paddr(paddr);
	}

	assert(page);

	if (!pv) {
		LIST_FOREACH (pv, &page->pv_table, pv_entries) {
			if (pv->map == map && pv->vaddr == vaddr) {
				goto next;
			}
		}

		fatal(
		    "pmap_unenter: no mapping of frame %p at vaddr %p in map %p\n",
		    page->paddr, vaddr, map);
	}

next:
	LIST_REMOVE(pv, pv_entries);
	kfree(pv);
}

vm_page_t *
pmap_unenter_kern(vm_map_t *map, vaddr_t vaddr)
{
	pte_t     *pte = pmap_fully_descend(map->pmap, vaddr);
	paddr_t	   paddr;
	vm_page_t *page;

	assert(pte);
	pte = P2V(pte);
	paddr = pte_get_addr(*pte);
	assert(*pte != 0x0);
	*pte = 0x0;

	invlpg(vaddr);

	page = vm_page_from_paddr(paddr);
	assert(page);

	return page;
}

bool
pmap_page_accessed_reset(vm_page_t *page)
{
	return false;
}

static void
kasan_map_enter(vaddr_t vaddr)
{
	void *paddr = pmap_trans(kmap.pmap, vaddr);
	if (paddr == NULL) {
		vm_page_t *page = vm_pagealloc(0);
		pmap_enter_kern(kmap.pmap, vaddr, page->paddr, kVMAll);
	}
}
