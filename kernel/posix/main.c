/*
 * Entry point of the POSIX subsystem.
 */

#include "kern/liballoc.h"
#include "kern/process.h"
#include "spl.h"
#include "vfs.h"

static void
usermode(uint64_t val)
{
	for (;;)
		;
}

void
posix_main()
{
	process_t *proc1 = process_new(&proc0);
	thread_t *thr1 = thread_new(proc1, false);

	thr1->pcb.frame.rsp = kmalloc(8192) + 8192;
	thr1->pcb.frame.rip = usermode;
	thr1->pcb.frame.rdi = 43;

	thread_run(thr1);

	timeslicing_start();

	/* reset system priority level, everything should now be ready to go */
	spl0();

	tmpfs_mountroot();
	vnode_t * tvn = NULL;
	root_vnode->ops->create(root_vnode, &tvn, "tester");
	kprintf("got vnode %p\n", tvn);
	pmap_stats();

	kprintf("map vnode object\n");
	vaddr_t faddr = VADDR_MAX;
	vm_map_object(kmap, tvn->vmobj, &faddr, 8192, 0);
	kprintf("test writing to vnode object:\n");
	*((char*)faddr + 0x1000) = 'h';

	pmap_stats();
	kmalloc(PGSIZE * 32);

	for (;;)
		asm volatile("pause");
}