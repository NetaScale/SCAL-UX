#include "amd64.h"
#include "liballoc.h"
#include "process.h"
#include "spl.h"

struct allprocs allprocs = TAILQ_HEAD_INITIALIZER(allprocs);
spinlock_t process_lock;
process_t proc0 = { .pid = 0, .name = "[kernel]" };
cpu_t *cpus;
size_t ncpus;

void
setup_proc0()
{
	proc0.map = kmap;
	TAILQ_INSERT_HEAD(&allprocs, &proc0, allprocs);
}

process_t *
process_new(process_t *parent)
{
	process_t *proc = kmalloc(sizeof *proc);
	spl_t spl;

	proc->map = parent->map;
	LIST_INIT(&proc->threads);

	spl = splhigh();
	lock(&process_lock);
	TAILQ_INSERT_HEAD(&allprocs, proc, allprocs);
	unlock(&process_lock);
	splx(spl);

	return proc;
}

thread_t *
thread_new(process_t *proc, bool iskernel)
{
	thread_t *thread = kmalloc(sizeof *thread);
	spl_t spl;

	thread->proc = proc;
	if (iskernel) {
		thread->kernel = true;
		thread->pcb.frame.cs = 0x28;
		thread->pcb.frame.ss = 0x38;
	} else {
		thread->kernel = false;
		thread->kstack = kmalloc(8192) + 8192;
		thread->pcb.frame.cs = 0x38 | 0x3;
		thread->pcb.frame.ss = 0x40 | 0x3;
		thread->pcb.frame.rflags = 0x202;
	}

	spl = splhigh();
	lock(&process_lock);
	LIST_INSERT_HEAD(&proc0.threads, thread, threads);
	unlock(&process_lock);
	splx(spl);

	return thread;
}

void
thread_run(thread_t *thread)
{
	lock(&cpus[0].lock);
	TAILQ_INSERT_TAIL(&cpus[0].runqueue, thread, runqueue);
	unlock(&cpus[0].lock);
}

void
thread_new_kernel(void *entry, void *arg)
{
	thread_t *thread = kmalloc(sizeof *thread);

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

void
dpcs_run()
{
	while (true) {
		spl_t spl;
		void (*fun)(void *) = NULL;
		void *arg;
		dpc_t *first;

		spl = splhigh();
		first = TAILQ_FIRST(&curcpu()->dpcqueue);
		if (first) {
			first->bound = false;
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(&curcpu()->dpcqueue, first, dpcqueue);
		}
		splx(spl);

		if (!fun)
			break;

		first->fun(first->arg);
	}
}

void
callouts_run(void *arg)
{
	while (true) {
		spl_t spl;
		void (*fun)(void *) = NULL;
		void *arg;
		callout_t *first;

		spl = splhigh();
		first = TAILQ_FIRST(&curcpu()->elapsedcallouts);
		if (first) {
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(&curcpu()->elapsedcallouts, first, queue);
		}
		splx(spl);

		if (!fun)
			break;

		first->fun(first->arg);
	}
}