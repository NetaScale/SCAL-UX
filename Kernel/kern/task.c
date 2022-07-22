/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "liballoc.h"
#include "libkern/klib.h"
#include "machine/spl.h"
#include "sys/param.h"
#include "sys/time.h"
#include "task.h"

spinlock_t	  sched_lock;
struct task_queue alltasks = TAILQ_HEAD_INITIALIZER(alltasks);
task_t		  task0 = { .pid = 0, .name = "[kernel]", .map = &kmap };
cpu_t	    *cpus = NULL;
size_t		  ncpus = 0;
size_t		  lastcpu = 0; /* cpu roundrobin */
pid_t		  lastpid = 100;

/**
 * Signals/exit requests are implemented by adjusting the stack appropriately
 * either A) while readying a thread to run, if it is not currently within a
 * system call, or B) immediately before return from an interrupt/trap, if this
 * did not itself interrupt a system call.
 */

void	 arch_timer_set(uint64_t micros);
uint64_t arch_timer_get_remaining();

static cpu_t *
nextcpu()
{
#if 1
	if (++lastcpu >= ncpus)
		lastcpu = 0;
	return &cpus[lastcpu];
#else
	return &cpus[1];
#endif
}

void
dpc_enqueue(dpc_t *dpc)
{
	spl_t spl = splsched();
	if (!dpc->bound)
		TAILQ_INSERT_TAIL(&CURCPU()->dpcqueue, dpc, dpcqueue);
	dpc->bound = true;
	splx(spl);
}

void
callout_enqueue(callout_t *callout)
{
	__auto_type queue = &CURCPU()->pendingcallouts;
	callout_t *co;
	spl_t	   spl;

	assert(callout->nanosecs > 0);
	spl = splsched();
	co = TAILQ_FIRST(queue);

	if (!co) {
		TAILQ_INSERT_HEAD(queue, callout, entries);
		arch_timer_set(callout->nanosecs / 1000);
		goto next;
	} else {
		uint64_t remains = arch_timer_get_remaining() * 1000;
		/* XXX: at least on QEMU, often reading current count > initial
		   count! */
		// assert(remains < co->nanosecs);
		co->nanosecs = MIN(remains, co->nanosecs);
	}

	assert(co->nanosecs > 0);

	if (co->nanosecs > callout->nanosecs) {
		co->nanosecs -= callout->nanosecs;
		TAILQ_INSERT_HEAD(queue, callout, entries);
		arch_timer_set(callout->nanosecs / 1000);
		goto next;
	}

	while (co->nanosecs < callout->nanosecs) {
		callout_t *next;
		callout->nanosecs -= co->nanosecs;
		next = TAILQ_NEXT(co, entries);
		if (next == NULL)
			break;
		co = next;
	}

	TAILQ_INSERT_AFTER(queue, co, callout, entries);

next:
	callout->state = kCalloutPending;
	splx(spl);
}

void
callout_dequeue(callout_t *callout)
{
	__auto_type queue = &CURCPU()->pendingcallouts;
	callout_t *co;
	spl_t	   spl = splsched();

	co = TAILQ_FIRST(queue);

	if (co != callout) {
		TAILQ_REMOVE(queue, callout, entries);
	} else {
		uint64_t remains = arch_timer_get_remaining() * 1000;
		TAILQ_REMOVE(queue, callout, entries);
		/* XXX: at least on QEMU, often reading current count > initial
		   count! */
		// assert(remains < co->nanosecs);
		callout->state = kCalloutDisabled;
		co = TAILQ_FIRST(queue);
		if (co) {
			co->nanosecs += MIN(remains, co->nanosecs);
			arch_timer_set(co->nanosecs / 1000);
		} else
			arch_timer_set(0);
	}

	splx(spl);
}

void
callout_interrupt()
{
	__auto_type queue = &CURCPU()->pendingcallouts;
	callout_t *co;
	spl_t	   spl;

	spl = splsched();
	co = TAILQ_FIRST(queue);
	assert(co != NULL);
	TAILQ_REMOVE(queue, co, entries);
	co->state = kCalloutElapsed;
	TAILQ_INSERT_TAIL(&CURCPU()->dpcqueue, &co->dpc, dpcqueue);
	co = TAILQ_FIRST(queue);
	if (co != NULL)
		arch_timer_set(co->nanosecs / 1000);
	splx(spl);
}

void
dpcs_run()
{
	spl_t spl = splsoft();
	__auto_type queue = &CURCPU()->dpcqueue;

	while (true) {
		void (*fun)(void *) = NULL;
		void  *arg;
		dpc_t *first;
		spl_t  spl2;

		spl2 = splhigh();
		first = TAILQ_FIRST(queue);
		if (first != NULL) {
			first->bound = false;
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(queue, first, dpcqueue);
		}
		splx(spl2);

		if (!fun)
			break;

		fun(arg);
	}

	splx(spl);
}

task_t *
task_fork(task_t *parent)
{
	task_t *task = kmalloc(sizeof *task);
	spl_t	spl;

	task->pid = lastpid++;
	task->map = vm_map_fork(parent->map);
	LIST_INIT(&task->threads);

	spl = splhigh();
	lock(&sched_lock);
	TAILQ_INSERT_HEAD(&alltasks, task, alltasks);
	unlock(&sched_lock);
	splx(spl);

	return task;
}

/**
 * Switch to thread \p thr. Previously-running thread should have already been
 * placed on an appropriate queue or otherwise dealt with. sched_lock should be
 * held (it will be relinquished after switching.) Reschedule callout should be
 * set with nanoseconds to an appropriate value; it will be enqueued if not 0.
 */
static int
thread_switchto(thread_t *thr)
{
	cpu_t *cpu = CURCPU();

	cpu->curthread = thr;
	vm_activate(thr->task->map);

	return 0;
}

/* TODO: remove amd64 specifics */
thread_t *
thread_new(task_t *task, bool iskernel)
{
	thread_t *thread = kcalloc(1, sizeof *thread);
	spl_t	  spl;

	thread->task = task;
	if (iskernel) {
		thread->kernel = true;
		thread->stack = kmalloc(USER_STACK_SIZE) + USER_STACK_SIZE;
		thread->pcb.frame.cs = 0x28;
		thread->pcb.frame.ss = 0x30;
		thread->pcb.frame.rsp = (uintptr_t)thread->stack;
		thread->pcb.frame.rflags = 0x202;
	} else {
		thread->kernel = false;
		thread->kstack = kmalloc(16384) + 16384; /* TODO KSTACK_SIZE */
		thread->pcb.frame.cs = 0x38 | 0x3;
		thread->pcb.frame.ss = 0x40 | 0x3;
		thread->pcb.frame.rflags = 0x202;
		thread->stack = VADDR_MAX;
		// vm_allocate(proc->map, NULL, &thread->stack,
		// USER_STACK_SIZE);
		thread->stack += USER_STACK_SIZE;
	}

	thread->cpu = nextcpu();

	spl = splsched();
	lock(&sched_lock);
	LIST_INSERT_HEAD(&task->threads, thread, threads);
	unlock(&sched_lock);
	splx(spl);

	return thread;
}

thread_t *
thread_dup(thread_t *thread, task_t *task)
{
	thread_t *newthr = kcalloc(1, sizeof *thread);
	spl_t	  spl;

	assert(thread->kernel == false);

	newthr->task = task;
	newthr->kernel = false;
	newthr->kstack = kmalloc(16384) + 16384; /* TODO KSTACK_SIZE */
	newthr->stack = thread->stack;
	memcpy(&newthr->pcb, &thread->pcb, sizeof thread->pcb);

	newthr->cpu = nextcpu();

	spl = splsched();
	lock(&sched_lock);
	LIST_INSERT_HEAD(&task->threads, newthr, threads);
	unlock(&sched_lock);
	splx(spl);

	return newthr;
}

void
thread_goto(thread_t *thr, void (*fun)(void *), void *arg)
{
	thr->pcb.frame.rip = (uintptr_t)fun;
	thr->pcb.frame.rdi = (uintptr_t)arg;
}

void arch_yield();
void arch_ipi_resched(cpu_t *cpu);

void
thread_run(thread_t *thread)
{
	spl_t spl = splsched();

	lock(&sched_lock);
	thread->state = kRunnable;
	TAILQ_INSERT_TAIL(&thread->cpu->runqueue, thread, runqueue);
	unlock(&sched_lock);
	splx(spl);

#if 0
	if (thread->cpu->curthread != thread->cpu->idlethread)
		return;
#endif

#ifdef DEBUG_SCHED
	kprintf("(curcpu %lu) let thread %p run on cpu %lu\n", CURCPU()->num,
	    thread, thread->cpu->num);
#endif

	/* thread should preempt the currently running thread */
	if (thread->cpu == CURCPU()) {
		arch_yield();
	} else
		arch_ipi_resched(thread->cpu);
}

/* callout callback for timeout of a waitqueue */
static void
waitq_timeout(void *arg)
{
	thread_t *thread = (thread_t *)arg;
	lock(&thread->wq->lock);
	thread_clearwait_locked(thread, 0x0, kWaitQResultTimeout);
	thread_run(thread);
}

void
waitq_init(waitq_t *wq)
{
	spinlock_init(&wq->lock);
	TAILQ_INIT(&wq->waiters);
}

/* XXX this probably should be refactored to let it make thread runnable etc. */
/**
 * \pre ~~thread locked,~~ thread->wq locked
 * \post thread->wq unlocked (and set to NULL)
 */
void
thread_clearwait_locked(struct thread *thread, waitq_event_t ev,
    waitq_result_t res)
{
	callout_dequeue(&thread->wqtimeout);
	TAILQ_REMOVE(&thread->wq->waiters, thread, waitqueue);
	unlock(&thread->wq->lock);
	thread->wq = NULL;
	thread->wqev = ev;
	thread->wqres = res;
}

waitq_result_t
waitq_await(waitq_t *wq, waitq_event_t ev, uint64_t nanosecs)
{
	thread_t	 *thread = CURCPU()->curthread;
	waitq_result_t res;

	splassertle(kSPL0);

	thread->wqtimeout.dpc.fun = waitq_timeout;
	thread->wqtimeout.dpc.arg = thread;
	thread->wqtimeout.nanosecs = nanosecs;

	asm("cli");
	lock(&wq->lock);
	TAILQ_INSERT_TAIL(&wq->waiters, thread, waitqueue);
	thread->wq = wq;
	thread->wqev = ev;
	thread->wqres = kWaitQResultWaiting;
	thread->state = kWaiting;
	unlock(&wq->lock);

	arch_yield();
	res = thread->wqres;
	asm("sti");

	return res;
}
void
waitq_wake_one(waitq_t *wq, waitq_event_t ev)
{
	thread_t *thrd;
	spl_t	  spl = splsoft();

	lock(&wq->lock);
	thrd = TAILQ_FIRST(&wq->waiters);
	TAILQ_REMOVE(&wq->waiters, thrd, waitqueue);
	unlock(&wq->lock);
	splx(spl);

	if (!thrd) {
		kprintf("warning: waitq %p sent event %lu with no waiters\n",
		    wq, ev);
		return;
	}

	thread_clearwait_locked(thrd, ev, kWaitQResultEvent);
	thread_run(thrd);
}
