#include <libkern/klib.h>
#include <limine.h>
#include <stddef.h>

#include "amd64/amd64.h"
#include "kern/liballoc.h"
#include "kern/task.h"
#include "kern/vm.h"

void	 lapic_enable();
uint32_t lapic_timer_calibrate();

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

static volatile struct limine_terminal_request terminal_request = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0
};

static uint64_t	    bsp_lapic_id;
static volatile int cpus_up = 0;

void
limterm_putc(int ch, void *ctx)
{
	struct limine_terminal *terminal =
	    terminal_request.response->terminals[0];
	terminal_request.response->write(terminal, (char *)&ch, 1);
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
#ifdef PRINT_MAP
		kprintf("%lx - %lx: %lu\n", entries[i]->base,
		    entries[i]->base + entries[i]->length, entries[i]->type);
#endif

		if (entries[i]->type == 0 && entries[i]->base >= 0x100000) {
			vm_pregion_t *bm = P2V((void *)entries[i]->base);
			size_t	      used; /* n bytes used by bitmap struct */
			int	      b;

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

			/* attach physical address to pages */
			for (b = 0; b < bm->npages; b++)
				bm->pages[b].paddr = bm->paddr + PGSIZE * b;

			/* mark off the pages used */
			for (b = 0; b < used / PGSIZE; b++)
				bm->pages[b].free = false;

			/* now zero the remainder */
			for (; b < bm->npages; b++) {
				TAILQ_INSERT_TAIL(&pg_freeq, &bm->pages[b],
				    queue);
				bm->pages[b].free = 1;
			}

			TAILQ_INSERT_TAIL(&vm_pregion_queue, bm, queue);
		}
	}

	arch_vm_init((paddr_t)kernel_address_request.response->physical_base);
}

void arch_resched(void *arg);
void setup_cpu_gdt(cpu_t *cpu);

static spinlock_t cpulock;

void
common_init(struct limine_smp_info *smpi)
{
	cpu_t    *cpu = (cpu_t *)smpi->extra_argument;
	thread_t *thread = kmalloc(sizeof *thread);

	lock(&cpulock);

	write_cr4(read_cr4() | (1 << 9));

	wrmsr(kAMD64MSRGSBase, (uint64_t)&smpi->extra_argument);

	cpu->num = smpi->processor_id;
	cpu->arch_cpu.lapic_id = smpi->lapic_id;
	TAILQ_INIT(&cpu->runqueue);
	TAILQ_INIT(&cpu->dpcqueue);
	TAILQ_INIT(&cpu->pendingcallouts);
	TAILQ_INIT(&cpu->waitqueue);

	cpu->resched_dpc.bound = false;
	cpu->resched_dpc.fun = arch_resched;
	cpu->resched_dpc.arg = cpu;

	unlock(&cpulock);

	idt_load();
	lapic_enable(0xff);

	/* measure thrice and average it */
	for (int i = 0; i < 3; i++)
		cpu->arch_cpu.lapic_tps += lapic_timer_calibrate() / 3;

	setup_cpu_gdt(cpu);

	thread->cpu = cpu;
	thread->kernel = true;
	thread->kstack = 0x0;
	thread->proc = &task0;
	thread->state = kRunning;
	cpu->curthread = thread;
	cpu->idlethread = thread;
	TAILQ_INIT(&cpu->runqueue);
	lock(&sched_lock);
	LIST_INSERT_HEAD(&task0.threads, thread, threads);
	unlock(&sched_lock);

	asm("sti");
	spl0();

	vm_activate(&kmap);

	__atomic_add_fetch(&cpus_up, 1, __ATOMIC_RELAXED);
}

void
ap_init(struct limine_smp_info *smpi)
{
	common_init(smpi);
	done(); /* idle */
}

static void
setup_cpus()
{
	struct limine_smp_response *smpr = smp_request.response;

	cpus = kmalloc(sizeof *cpus * smpr->cpu_count);
	ncpus = smpr->cpu_count;

	kprintf("bringing up %lu cpus...", smpr->cpu_count);

	bsp_lapic_id = smpr->bsp_lapic_id;

	for (size_t i = 0; i < smpr->cpu_count; i++) {
		struct limine_smp_info *smpi = smpr->cpus[i];
		smpi->extra_argument = (uint64_t)&cpus[i];
		if (smpi->lapic_id == bsp_lapic_id)
			common_init(smpi);
		else
			smpi->goto_address = ap_init;
	}

	while (cpus_up != smpr->cpu_count)
		__asm__("pause");

	kprintf("done\n");
}

// The following will be our kernel's entry point.
void
_start(void)
{
	thread_t *swapthr;

	// Ensure we got a terminal
	if (terminal_request.response == NULL ||
	    terminal_request.response->terminal_count < 1) {
		done();
	}

	kprintf("The SCAL/UX Operating System\n");

	idt_init();
	mem_init();
	vm_kernel_init();
	setup_cpus();

	extern void autoconf();
	autoconf();

#if 0
	callout_t testco;
	testco.nanosecs = 2000000000;
	testco.dpc.fun = testfun;
	callout_enqueue(&testco);
#endif

	swapthr = thread_new(&task0, true);
	thread_goto(swapthr, swapper, NULL);
	thread_run(swapthr);

	// We're done, just hang...
	done();
}