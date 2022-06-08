#include <sys/amd64_misc.h>
#include <sys/limine.h>
#include <sys/vm.h>
#include <sys/vxkern.h>

#include <fs/vfs.h>
#include <stddef.h>
#include <stdint.h>

#include "intr.h"
#include "liballoc.h"

#define NANOPRINTF_IMPLEMENTATION
#include "sys/nanoprintf.h"

struct limine_terminal *terminal;
vm_pregion_t *g_1st_mem = NULL;
vm_pregion_t *g_last_mem = NULL;

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

static volatile struct limine_smp_request smp_request = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0
};

static volatile struct limine_terminal_request terminal_request = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0
};

static uint64_t bsp_lapic_id;
static int cpus_up = 0;
spinlock_t lock_msgbuf;

static void
done(void)
{
	kprintf("done\n");
	for (;;) {
		__asm__("hlt");
	}
}

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("out %0, %1" :: "a"(data), "Nd"(port));
}

void
limterm_putc(int ch, void *ctx)
{
	terminal_request.response->write(
		terminal_request.response->terminals[0], (const char *)&ch, 1);
}

void
common_init(struct limine_smp_info *smpi)
{
	cpu_t *cpu = (cpu_t *)smpi->extra_argument;

	kprintf("setup cpu %d\n", smpi->processor_id);

	wrmsr(kAMD64MSRKernelGSBase, smpi->extra_argument);
	cpu->num = smpi->processor_id;
	cpu->lapic_id = smpi->lapic_id;

	idt_load();
	lapic_enable(0xff);

	__atomic_add_fetch(&cpus_up, 1, __ATOMIC_RELAXED);
}

void
ap_init(struct limine_smp_info *smpi)
{
	common_init(smpi);

	done();
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

			used = ROUNDUP(sizeof(vm_pregion_t) +
				sizeof(vm_page_t) * bm->npages,
			    PGSIZE);

			kprintf("used %lu KiB for page map\n", used / 1024);

			kprintf("Usable memory area: 0x%lx "
			    "(%lu mb long, %lu pages)\n",
			    entries[i]->base,
			    entries[i]->length / (1024 * 1024),
			    entries[i]->length / PGSIZE);

			/* mark off the pages used */
			for (b = 0; b < used / PGSIZE; b++)
				bm->pages[b].type = kPageVMInternal;

			/* now zero the remainder */
			for (; b < bm->npages; b++)
				bm->pages[b].type = 0;

			if (g_1st_mem == NULL) {
				g_1st_mem = g_last_mem = bm;
				/* set 1st offs here maybe */
			} else
				g_last_mem = g_last_mem->next = bm;
		}
	}

	vm_init((paddr_t)kernel_address_request.response->physical_base);
	idt_init();
	kprintf("activating pmap\n");
	pmap_activate(kmap->pmap);
	kprintf("activated pmap\n");
	//done();

	tmpfs_mountroot();
	vnode_t * tvn = NULL;
	root_vnode->ops->create(root_vnode, &tvn, "tester");
	kprintf("got vnode %p\n", tvn);
	done();

	struct limine_smp_response *smpr = smp_request.response;
	cpu_t *cpus = kmalloc(sizeof *cpus * smpr->cpu_count);

	kprintf("%lu cpus\n", smpr->cpu_count);

	bsp_lapic_id = smpr->bsp_lapic_id;

	for (size_t i = 0; i < smpr->cpu_count; i++) {
		struct limine_smp_info *smpi = smpr->cpus[i];
		smpi->extra_argument = (uint64_t)&cpus[i];
		if (smpi->lapic_id == bsp_lapic_id) {
			common_init(smpi);
		} else {
			smpi->goto_address = ap_init;
		}
	}

	kprintf("waiting for all CPUs to come up\n");
	while (cpus_up != smpr->cpu_count)
		__asm__("pause");

	kprintf("all CPUs up\n");

	int doathing();
	doathing();

	if (module_request.response->module_count != 1) {
		kprintf("expected a module\n");
		done();
	}

	kmod_parsekern(kernel_file_request.response->kernel_file->address);

	struct limine_file * mod = module_request.response->modules[0];

	kprintf("mod %s: %p\n", mod->path, mod->address);

	kmod_load(mod->address);


	kprintf("test int\n");
	asm volatile("int $80");
	kprintf("test pgfault\n");
	uint64_t *ill = (uint64_t *)0x0000000200000000ull;
	*ill = 42;

	// We're done, just hang...
	done();
}
