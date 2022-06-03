#include <limine.h>
#include <stddef.h>
#include <stdint.h>

#define NANOPRINTF_IMPLEMENTATION
#include "nanoprintf.h"
#include "vxkern.h"
#include "vm.h"
#include "liballoc.h"

struct limine_terminal *terminal;
vm_pregion_t *g_1st_mem = NULL;
vm_pregion_t *g_last_mem = NULL;

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent.

static volatile struct limine_kernel_address_request kernel_address_request = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0
};

static volatile struct limine_terminal_request terminal_request = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0
};

static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

static void
done(void)
{
	kprintf("done\n");
	for (;;) {
		__asm__("hlt");
	}
}

void
limterm_putc(int ch, void *ctx)
{
	terminal_request.response->write(
	    terminal_request.response->terminals[0], (const char *)&ch, 1);
}

/*
 * early allocator function - a watermark allocator - it robs pages from the 1st
 * pregion.
 */
int early_alloc(size_t size)
{
	static uintptr_t last_offs;

	/* find 1st useable page */
	if (last_offs == 0) {
		for (int i = 0; ; i++){
			if (g_1st_mem->pages[i].type != kPageVMInternal) {
				last_offs = i * PGSIZE;
				break;
			}
		}
	}

	kprintf("allocating at offset %lu\n", last_offs);

	return 0;
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

	kprintf("Valutron Executive for 64-bit PCs\n");

	if (hhdm_request.response->offset != 0xffff800000000000) {
		/* we expect HHDM begins there for now for simplicity */
		kprintf("Unexpected HHDM offset (assumes 0xffff800000000000, "
			"actual %lx", hhdm_request.response->offset);
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
#ifdef PRINT_MAP
		kprintf("%lx - %lx: %lu\n", entries[i]->base,
		    entries[i]->base + entries[i]->length, entries[i]->type);
#endif
		if (entries[i]->type == 0 && entries[i]->base >= 0x100000) {
			vm_pregion_t *bm = P2V((void *)entries[i]->base);
			size_t used; /* n bytes used by bitmap struct */
			int b;

			/* set up a pregion for this area */
			bm->paddr = (void *)entries[i]->base;
			bm->npages = entries[i]->length / PGSIZE;

			used = sizeof(vm_page_t) + ((bm->npages + 7) / 8);

			kprintf("Usable memory area: 0x%lx "
			    "(%lu mb long, %lu pages)\n",
			    entries[i]->base,
			    entries[i]->length / (1024 * 1024),
			    entries[i]->length / PGSIZE);

			/* mark off the pages used */
			for (b = 0; b < used; b++)
				bm->pages[b].type = kPageVMInternal;

			/* now zero the remainder */
			for (; b < bm->npages; b++)
				bm->pages[b].type = 0;

			if (g_1st_mem == NULL) {
				g_1st_mem = g_last_mem = bm;
				/* set 1st offs here maybe */
			}
			else
				g_last_mem = g_last_mem->next = bm;
		}
	}

	vm_init((paddr_t)kernel_address_request.response->physical_base);

	// We're done, just hang...
	done();
}
