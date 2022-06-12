#include "process.h"
#include "liballoc.h"


struct allprocs allprocs = TAILQ_HEAD_INITIALIZER(allprocs);
spinlock_t process_lock;
process_t proc0 = {
	.pid = 0,
	.name = "[kernel]"
};
cpu_t *cpus;
size_t ncpus;

void setup_proc0()
{
	proc0.map = kmap;
	TAILQ_INSERT_HEAD(&allprocs, &proc0, allprocs);
}

void thread_new_kernel(void*entry, void*arg)
{
	thread_t * thread = kmalloc(sizeof *thread);

	thread->proc = &proc0;
	thread->kernel = true;
	thread->kstack = 0;
	thread->pcb.frame.cs = 0x28;
	thread->pcb.frame.ss = 0x30;
	thread->pcb.frame.rip = (uintptr_t)entry;
	thread->pcb.frame.rdi = (uintptr_t)arg;
	thread->pcb.frame.rsp = (uintptr_t)kmalloc(4096) + 4096;

	lock(&process_lock);
	LIST_INSERT_HEAD(&proc0.threads, thread, threads);
	unlock(&process_lock);
	lock(&cpus[1].lock);
	TAILQ_INSERT_TAIL(&cpus[1].runqueue, thread, runqueue);
	unlock(&cpus[1].lock);
 }