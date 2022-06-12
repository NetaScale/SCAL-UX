/*
 * Entry point of the POSIX subsystem.
 */

#include "kern/liballoc.h"
#include "kern/process.h"
#include "spl.h"

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

	for (;;)
		asm volatile("pause");
}