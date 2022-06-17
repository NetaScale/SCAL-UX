/*
 * Entry point of the POSIX subsystem.
 */

#include <string.h>

#include "kern/liballoc.h"
#include "kern/process.h"
#include "posix_proc.h"
#include "spl.h"
#include "vfs.h"

static void
start_init(void *bin)
{
	/*
	 * .globl start
	 * start:
	 *   movq $2, %rax ; PXSYS_exec
	 *   movq $init, %rdi
	 *   int $0x80
	 * init:
	 *   .string "/init\0"
	 */
	static uint16_t initcode[] = { 0xc748, 0x02c0, 0x0000, 0x4800, 0xc7c7,
		0x0010, 0x0000, 0x80cd, 0x692f, 0x696e, 0x0074, 0x0000 };
	process_t *proc1 = process_new(&proc0);
	proc1->pxproc = kmalloc(sizeof(posix_proc_t));
	proc1->pxproc->proc = proc1;
	memset(proc1->pxproc->files, 0x0, sizeof proc1->pxproc->files);
	thread_t *thr1 = thread_new(proc1, false);
	vaddr_t vaddr = 0x0;

	assert(vm_allocate(kmap, NULL, &vaddr, 4096, 1) == 0);
	memcpy(0x0, initcode, sizeof(initcode));

	thr1->pcb.frame.rsp = kmalloc(4096) + 4096;
	thr1->pcb.frame.rip = 0x0;
	thr1->pcb.frame.rdi = 0x0;
	thr1->pcb.frame.rbp = 0x0;

	thread_run(thr1);
}

void
posix_main(void *initbin, size_t size, void *ldbin, size_t ldsize)
{
	timeslicing_start();

	/* reset system priority level, everything should now be ready to go */
	spl0();

	tmpfs_mountroot();
	vnode_t *tvn = NULL;
	root_vnode->ops->create(root_vnode, &tvn, "init");
	assert(vfs_write(tvn, initbin, size, 0x0) == 0);

	root_vnode->ops->create(root_vnode, &tvn, "ld.so");
	assert(vfs_write(tvn, ldbin, ldsize, 0x0) == 0);

	kprintf("starting init process...\n");
	start_init(initbin);

	pmap_stats();
	kmalloc(PGSIZE * 32);

	// for (;;)
	//	asm volatile("pause");
}