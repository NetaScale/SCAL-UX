#include <kern/kmem.h>
#include <kern/task.h>
#include <kern/vm.h>
#include <libkern/klib.h>
#include <x86_64/cpu.h>
#include <x86_64/limine.h>

#include <stddef.h>
#include <stdint.h>

void idt_init();
void idt_load();
void x64_vm_init(paddr_t kphys);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent.

static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

static volatile struct limine_kernel_address_request kernel_address_request = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0
};

static volatile struct limine_kernel_file_request kernel_file_request = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0
};

static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0
};

static volatile struct limine_module_request module_request = {
	.id = LIMINE_MODULE_REQUEST,
	.revision = 0
};

volatile struct limine_rsdp_request rsdp_request = { .id = LIMINE_RSDP_REQUEST,
	.revision = 0 };

static volatile struct limine_smp_request smp_request = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0
};

volatile struct limine_terminal_request terminal_request = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0
};

void
md_kputc(int ch, void *ctx)
{
	struct limine_terminal *terminal =
	    terminal_request.response->terminals[0];
	terminal_request.response->write(terminal, (const char *)&ch, 1);
}

static void
done(void)
{
	for (;;) {
		__asm__("hlt");
	}
}

static void
mem_init()
{
	if (hhdm_request.response->offset != 0xffff800000000000) {
		/* we expect HHDM begins there for now for simplicity */
		kprintf("Unexpected HHDM offset (assumes 0xffff800000000000, "
			"actual %lx",
		    hhdm_request.response->offset);
		done();
	}

	if (kernel_address_request.response->virtual_base !=
	    0xffffffff80000000) {
		kprintf("Unexpected kernel virtual base %lx",
		    kernel_address_request.response->virtual_base);
		done();
	}

	struct limine_memmap_entry **entries = memmap_request.response->entries;

	for (int i = 0; i < memmap_request.response->entry_count; i++) {

		vm_pregion_t *bm = P2V((void *)entries[i]->base);
		size_t	      used; /* n bytes used by bitmap struct */
		int	      b;

		if (entries[i]->type != 0 || entries[i]->base < 0x100000)
			continue;

		/* set up a pregion for this area */
		bm->base = (void *)entries[i]->base;
		bm->npages = entries[i]->length / PGSIZE;

		used = ROUNDUP(sizeof(vm_pregion_t) +
			sizeof(vm_page_t) * bm->npages,
		    PGSIZE);

		kprintf("used %lu KiB for resident pagetable\n", used / 1024);

		kprintf("Usable memory area: 0x%lx "
			"(%lu mb long, %lu pages)\n",
		    entries[i]->base, entries[i]->length / (1024 * 1024),
		    entries[i]->length / PGSIZE);

		/* initialise pages */
		for (b = 0; b < bm->npages; b++) {
			bm->pages[b].paddr = bm->base + PGSIZE * b;
			mutex_init(&bm->pages[b].lock);
			LIST_INIT(&bm->pages[b].pv_table);
			bm->pages[b].obj = NULL;
		}

		/* mark off the pages used */
		for (b = 0; b < used / PGSIZE; b++) {
			bm->pages[b].queue = kVMPagePMap;
			TAILQ_INSERT_TAIL(&vm_pgpmapq.queue, &bm->pages[b],
			    pagequeue);
			vm_pgpmapq.npages++;
		}

		/* now zero the remainder */
		for (; b < bm->npages; b++) {
			bm->pages[b].queue = kVMPageFree;
			TAILQ_INSERT_TAIL(&vm_pgfreeq.queue, &bm->pages[b],
			    pagequeue);
			vm_pgfreeq.npages++;
		}

		TAILQ_INSERT_TAIL(&vm_pregion_queue, bm, queue);
	}

	x64_vm_init((paddr_t)kernel_address_request.response->physical_base);
}

// The following will be our kernel's entry point.
void
_start(void)
{
	// Ensure we got a terminal
	if (terminal_request.response == NULL ||
	    terminal_request.response->terminal_count < 1) {
		done();
	}

	kprintf("The SCAL/UX Operating System\n");

	idt_init();
	idt_load();

	/*! make sure curcpu() can work */
	wrmsr(kAMD64MSRGSBase, (uint64_t)&cpu0);

	mem_init();
	vm_kernel_init();
	kmem_init();
	kmem_dump();

	*(double *)11 = 48000000.12f;

	// We're done, just hang...
	done();
}
