/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <dev/fbterm/FBTerm.h>
#include <kern/kmem.h>
#include <kern/task.h>
#include <libkern/klib.h>
#include <vm/vm.h>
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

volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};

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

static int cpus_up = 0;

enum { kPortCOM1 = 0x3f8 };

static void
serial_init()
{
	outb(kPortCOM1 + 1, 0x00);
	outb(kPortCOM1 + 3, 0x80);
	outb(kPortCOM1 + 0, 0x03);
	outb(kPortCOM1 + 1, 0x00);
	outb(kPortCOM1 + 3, 0x03);
	outb(kPortCOM1 + 2, 0xC7);
	outb(kPortCOM1 + 4, 0x0B);
}

void
md_kputc(int ch, void *ctx)
{
	while (!(inb(kPortCOM1 + 5) & 0x20))
		;
	outb(kPortCOM1, ch);

	if (!syscon) {
		struct limine_terminal *terminal =
		    terminal_request.response->terminals[0];
		terminal_request.response->write(terminal, (char *)&ch, 1);
	} else {
		sysconputc(ch);
	}
}

static void
done(void)
{
	kprintf("Done!\n");
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

/* can't rely on mutexes until scheduling is up, so this must be used instead */
static spinlock_t early_lock = SPINLOCK_INITIALISER;

static void
common_init(struct limine_smp_info *smpi)
{
	cpu_t *cpu = (cpu_t *)smpi->extra_argument;

	spinlock_lock(&early_lock);
	SLIST_INSERT_HEAD(&task0.threads, cpu->curthread, taskthreads);
	spinlock_unlock(&early_lock);

	void	 lapic_enable();
	uint32_t lapic_timer_calibrate();
	idt_load();
	lapic_enable(0xff);

	void setup_cpu_gdt(cpu_t * cpu);
	setup_cpu_gdt(cpu);

	/* measure thrice and average it */
	cpu->md.lapic_tps = 0;
	cpu->md.lapic_id = smpi->lapic_id;
	for (int i = 0; i < 3; i++)
		cpu->md.lapic_tps += lapic_timer_calibrate() / 3;

	cpu->preempted = 0;
	cpu->timeslicer.arg = NULL;
	cpu->timeslicer.callback = sched_timeslice;
	cpu->timeslicer.state = kCalloutDisabled;
	TAILQ_INIT(&cpu->pendingcallouts);
	TAILQ_INIT(&cpu->runqueue);

	cpu->idlethread = cpu->curthread;
	cpu->idlethread->state = kThreadRunning;

	vm_activate(&kmap);
	asm("sti");
	__atomic_add_fetch(&cpus_up, 1, __ATOMIC_RELAXED);
}

static void
ap_init(struct limine_smp_info *smpi)
{
	cpu_t    *cpu = (cpu_t *)smpi->extra_argument;
	thread_t *thread;

	/* need this before any allocations */
	wrmsr(kAMD64MSRGSBase, (uintptr_t)&cpus[cpu->num]);

	spinlock_lock(&early_lock);
	thread = kmem_alloc(sizeof *thread);
	spinlock_unlock(&early_lock);

	spinlock_init(&thread->lock);
	cpu->curthread = thread;
	thread->task = &task0;

	common_init(smpi);
	/* this is now that CPU's idle thread loop */
	done();
}

static void
smp_init()
{
	struct limine_smp_response *smpr = smp_request.response;

	cpus = kmem_alloc(sizeof *cpus * smpr->cpu_count);

	kprintf("%lu cpus\n", smpr->cpu_count);
	ncpu = smpr->cpu_count;

	for (size_t i = 0; i < smpr->cpu_count; i++) {
		struct limine_smp_info *smpi = smpr->cpus[i];

		if (smpi->lapic_id == smpr->bsp_lapic_id) {
			smpi->extra_argument = (uint64_t)&cpu0;
			cpu0.num = i;
			cpus[i] = &cpu0;
			common_init(smpi);
		} else {
			cpu_t *cpu = kmem_alloc(sizeof *cpu);
			cpu->num = i;
			cpus[i] = cpu;
			smpi->extra_argument = (uint64_t)cpu;
			smpi->goto_address = ap_init;
		}
	}

	while (cpus_up != smpr->cpu_count)
		__asm__("pause");
}

static mutex_t mtx;

static void
fun2(void *arg)
{
	kprintf("thread2: hello\n");
	while (1) {
		mutex_lock(&mtx);
		for (int i = 0; i < UINT32_MAX / 1024; i++)
			asm("pause");
		//kprintf("B");
		mutex_unlock(&mtx);
	}
	done();
}

static void
kmain(void *arg)
{
	thread_t *test;

	mutex_init(&mtx);

	kprintf("thread1: hello\n");
	test = thread_new(&task0, fun2, (void *)100);
	kprintf("thread1: made thread2\n");
	thread_resume(test);
	kprintf("thread1: thread2 resumed\n");

#if 0
	while (1) {
		mutex_lock(&mtx);
		for (int i = 0; i < UINT32_MAX / 1024; i++)
			asm("pause");
		//kprintf("A");
		mutex_unlock(&mtx);
	}
#endif

	int posix_main(void);
	posix_main();

	done();
}

// The following will be our kernel's entry point.
void
_start(void)
{
	void *pcpu0 = &cpu0;
	serial_init();

	// Ensure we got a terminal
	if (terminal_request.response == NULL ||
	    terminal_request.response->terminal_count < 1) {
		done();
	}

	kprintf("The SCAL/UX Operating System\n");

	idt_init();
	idt_load();

	/*! make sure curcpu() can work; we only need it till smp_init() */
	wrmsr(kAMD64MSRGSBase, (uint64_t)&pcpu0);

	mem_init();
	vm_kernel_init();
	kmem_init();

	smp_init();

	// callout_t callout;
	// callout.nanosecs = NS_PER_S * 1;
	// callout_enqueue(&callout);

	thread_t *test = thread_new(&task0, kmain, 0);
	kprintf("thread0: made thread1\n");
	thread_resume(test);

	/* this thread is now the idle thread */

	kprintf("thread0: after resuming thread1\n");

	for (int i = 0; i < 512; i++)
		outb(0x80, 0x0);
	kmem_dump();
	vm_pagedump();

	vm_object_t *aobj1, *aobj2;

	vaddr_t anonaddr = VADDR_MAX, anon2addr = VADDR_MAX;
	vm_allocate(&kmap, &aobj1, &anonaddr, PGSIZE * 32);
	kprintf("Aobj at %p\n", anonaddr);

	strcpy(anonaddr, "Hello, world");

	aobj2 = vm_object_copy(aobj1);
	vm_map_object(&kmap, aobj2, &anon2addr, PGSIZE * 32, 0, false);
	kprintf("Aobj copy at %p\n", anon2addr);

	strcpy(anon2addr + 7, "copy-on-write world!");

	kprintf("Page from Aobj1:\t%s\nPage from Aobj2:\t%s\n",
	    (char *)anonaddr, (char *)anon2addr);

	//*(double *)11 = 48000000.12f;

	// We're done, just hang...
	done();
}
