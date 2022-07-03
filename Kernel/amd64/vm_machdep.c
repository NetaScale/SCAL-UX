
#include <kern/vm.h>
#include <amd64/amd64.h>

struct pmap {
        paddr_t pml4;
};

struct vm_pregion_queue vm_pregion_queue;
static vm_map_entry_t hhdm_entry, kernel_entry, kheap_entry;
static vm_object_t    hhdm_obj, kernel_obj, kheap_obj;
static pmap_t kpmap;

static vm_page_t *steal_page()
{
        vm_page_t * page = TAILQ_FIRST(&pg_freeq);
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

        kpmap.pml4 = (paddr_t)read_cr3();
}
