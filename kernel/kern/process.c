#include "amd64.h"
#include "kern/queue.h"
#include "liballoc.h"
#include "process.h"
#include "spl.h"

struct allprocs allprocs = TAILQ_HEAD_INITIALIZER(allprocs);
spinlock_t process_lock;
process_t proc0 = { .pid = 0, .name = "[kernel]" };
cpu_t *cpus;
size_t ncpus;
size_t lastcpu = 0; /* cpu roundrobin */

static cpu_t *
nextcpu()
{
	if (++lastcpu >= ncpus)
		lastcpu = 0;
	return &cpus[lastcpu];
}

void
callout_enqueue(callout_t *callout)
{
	callout_t *co;
	spl_t spl = splhigh();
	TAILQ_FOREACH (co, &CURCPU()->pendingcallouts, entries) {
		if (co->timeout > callout->timeout) {
			TAILQ_INSERT_BEFORE(co, callout, entries);
			goto next;
		}
		callout->timeout -= co->timeout;
	}
	TAILQ_INSERT_TAIL(&CURCPU()->pendingcallouts, callout, entries);
next:
	splx(spl);
}

void
callout_dequeue(callout_t *callout)
{
	callout_t *co;
	spl_t spl = splhigh();
	TAILQ_REMOVE(&CURCPU()->pendingcallouts, callout, entries);
	splx(spl);
}

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

	proc->map = vm_map_fork(parent->map);
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
		thread->kstack = kmalloc(16384) + 16384; /* TODO KSTACK_SIZE */
		thread->pcb.frame.cs = 0x38 | 0x3;
		thread->pcb.frame.ss = 0x40 | 0x3;
		thread->pcb.frame.rflags = 0x202;
		thread->stack = VADDR_MAX;
		vm_allocate(proc->map, NULL, &thread->stack, USER_STACK_SIZE,
		    false);
		thread->stack += USER_STACK_SIZE;
	}

	thread->cpu = nextcpu();

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
	thread->state = kRunnable;
	kprintf("Letting thread %p run on cpu %lu\n", thread, thread->cpu->num);
	lock(&thread->cpu->lock);
	TAILQ_INSERT_TAIL(&thread->cpu->runqueue, thread, runqueue);
	unlock(&thread->cpu->lock);
}

void
thread_new_kernel(void *entry, void *arg)
{
	thread_t *thread = thread_new(&proc0, true);
	thread->pcb.frame.rip = (uintptr_t)entry;
	thread->pcb.frame.rdi = (uintptr_t)arg;
}

/**
 * Called on every tick.
 *
 * \pre interrupts remain disabled
 */
void
tick()
{
	callout_t *co;
	cpu_t *cpu = CURCPU();

	cpu->counter++;
	if (cpu->timeslice > 0)
		cpu->timeslice--;

	co = TAILQ_FIRST(&cpu->pendingcallouts);
	if (co && (co->timeout == 0 || --co->timeout == 0)) {
		TAILQ_REMOVE(&cpu->pendingcallouts, co, entries);
		TAILQ_INSERT_TAIL(&cpu->elapsedcallouts, co, entries);
		if (!cpu->calloutdpc.bound) {
			TAILQ_INSERT_HEAD(&cpu->dpcqueue, &cpu->calloutdpc,
			    dpcqueue);
			cpu->calloutdpc.bound = true;
		}
	}
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
		first = TAILQ_FIRST(&CURCPU()->dpcqueue);
		if (first) {
			first->bound = false;
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(&CURCPU()->dpcqueue, first, dpcqueue);
		}
		splx(spl);

		if (!fun)
			break;

		fun(arg);
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
		first = TAILQ_FIRST(&CURCPU()->elapsedcallouts);
		if (first) {
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(&CURCPU()->elapsedcallouts, first,
			    entries);
		}
		splx(spl);

		if (!fun)
			break;

		fun(arg);
	}
}
