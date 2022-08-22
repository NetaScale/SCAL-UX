/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <kern/kmem.h>
#include <kern/task.h>
#include <libkern/klib.h>

#include <stdatomic.h>
#include <stdint.h>

#include "kern/sync.h"
#include "machine/machdep.h"

task_t task0 = {
        .name = "[kernel]",
        .map = &kmap
};

thread_t thread0 = {
        .task = &task0,
	.lock = SPINLOCK_INITIALISER,
	.wq = NULL,
	.in_pagefault = false,
};

cpu_t cpu0 = {
        .num = -1,
        .curthread = &thread0
};

spinlock_t sched_lock = SPINLOCK_INITIALISER;
cpu_t    **cpus;
int	   ncpu, lastcpu = 0;

/*! CPU round robin for assigning threads to; should do better some day ofc. */
static cpu_t *
nextcpu()
{
#if 1
	if (++lastcpu >= ncpu)
		lastcpu = 0;
	return cpus[lastcpu];
#else
	return cpus[0];
#endif
}

void
callout_enqueue(callout_t *callout)
{
	__auto_type queue = &curcpu()->pendingcallouts;
	callout_t *co;
	bool	   iff;

#if DEBUG_TIMERS == 1
	kprintf("Enqueuing callout %p\n", callout);
#endif

	assert(callout->nanosecs > 0);
	iff = md_intr_disable();
	co = TAILQ_FIRST(queue);

	if (!co) {
		TAILQ_INSERT_HEAD(queue, callout, queue);
		md_timer_set(callout->nanosecs);
		goto next;
	} else {
		uint64_t remains = md_timer_get_remaining();
		/* XXX: at least on QEMU, often reading current count > initial
		   count! */
		// assert(remains < co->nanosecs);
		co->nanosecs = MIN(remains, co->nanosecs);
	}

	assert(co->nanosecs > 0);

	if (co->nanosecs > callout->nanosecs) {
		co->nanosecs -= callout->nanosecs;
		TAILQ_INSERT_HEAD(queue, callout, queue);
		md_timer_set(callout->nanosecs);
		goto next;
	}

	while (co->nanosecs < callout->nanosecs) {
		callout_t *next;
		callout->nanosecs -= co->nanosecs;
		next = TAILQ_NEXT(co, queue);
		if (next == NULL)
			break;
		co = next;
	}

	TAILQ_INSERT_AFTER(queue, co, callout, queue);

next:
	callout->state = kCalloutPending;
	md_intr_x(iff);
}

void
callout_dequeue(callout_t *callout)
{
	__auto_type queue = &curcpu()->pendingcallouts;
	callout_t *co;
	bool	   iff = md_intr_disable();

	// TODO(med): can have false wakeups if an interrupt is pending?

	assert(callout->state == kCalloutPending);

	co = TAILQ_FIRST(queue);

	if (co != callout) {
		TAILQ_REMOVE(queue, callout, queue);
	} else {
		uint64_t remains = md_timer_get_remaining();
		TAILQ_REMOVE(queue, callout, queue);
		/* XXX: at least on QEMU, often reading current count > initial
		   count! */
		// assert(remains < co->nanosecs);
		callout->state = kCalloutDisabled;
		co = TAILQ_FIRST(queue);
		if (co) {
			co->nanosecs += MIN(remains, co->nanosecs);
			md_timer_set(co->nanosecs);
		} else
			/* nothing upcoming */
			md_timer_set(0);
	}

	md_intr_x(iff);
}

void
callout_interrupt(md_intr_frame_t *frame, void *unused)
{
	__auto_type queue = &curcpu()->pendingcallouts;
	callout_t *co;
	bool	   iff;

	iff = md_intr_disable();

	co = TAILQ_FIRST(queue);

	/* can have spurious ones */
	if (co == NULL)
		goto finish;

	TAILQ_REMOVE(queue, co, queue);
	co->state = kCalloutDisabled;
	co->callback(frame, co->arg);

	/* now set up the next in sequence */
	co = TAILQ_FIRST(queue);
	if (co != NULL)
		md_timer_set(co->nanosecs);

finish:
	md_intr_x(iff);
}

void
mutex_lock(mutex_t *mtx)
{
	struct thread *nul = NULL;

	if (atomic_fetch_add(&mtx->count, 1) >= 1) {
		switch (waitq_await(&mtx->wq, -1)) {
		case kWQSuccess:
			/* epsilon */
			break;

		default: {
			fatal("Failured to acquire a mutex.\n");
		}
		}
	}

	assert(atomic_compare_exchange_strong(&mtx->owner, &nul, curthread()));
}

void
mutex_unlock(mutex_t *mtx)
{
	struct thread *expected = curthread();

	assert(atomic_compare_exchange_strong(&mtx->owner, &expected, NULL));
	if (atomic_fetch_sub(&mtx->count, 1) > 1) {
		waitq_wake_one(&mtx->wq);
	}
}

/* to be called with interrupts disabled + wq locked */
static waitq_result_t
waitq_await_locked(waitq_t *wq, uint64_t nanosecs)
{
	thread_t	 *thread = curthread();
	waitq_result_t r;

	/* TODO(high): timeouts */

	spinlock_lock(&thread->lock);
	TAILQ_INSERT_TAIL(&wq->waiters, thread, queue);
	thread->state = kThreadWaiting;
	thread->wq = wq;
	spinlock_unlock(&wq->lock);
	sched_reschedule();
	r = thread->wqres;
	return r;
}

waitq_result_t
waitq_await(waitq_t *wq, uint64_t nanosecs)
{
	int	       iff = md_intr_disable();
	waitq_result_t r;

	spinlock_lock(&wq->lock);
	for (;;) ;
	r = waitq_await_locked(wq, nanosecs);
	md_intr_x(iff);
	return r;
}

int
waitq_wake_one(waitq_t *wq)
{
	thread_t *thrd;
	int	  iff = md_intr_disable();

	spinlock_lock(&wq->lock);
	thrd = TAILQ_FIRST(&wq->waiters);
	if (thrd)
		TAILQ_REMOVE(&wq->waiters, thrd, queue);
	spinlock_unlock(&wq->lock);

	if (!thrd) {
		kprintf("warning: waitq %p sent event with no waiters\n", wq);
		md_intr_x(iff);
		return 0;
	}

	thrd->wqres = kWQSuccess;
	thread_resume(thrd);

	md_intr_x(iff);
	return 1;
}

waitq_result_t
semaphore_wait(semaphore_t *sem, uint64_t nanosecs)
{
	int	       iff = md_intr_disable();
	waitq_result_t r;

	spinlock_lock(&sem->wq.lock);
	if (--sem->count < 0) {
		r = waitq_await_locked(&sem->wq, nanosecs);
		assert(r == kWQSuccess);
	} else {
		spinlock_unlock(&sem->wq.lock);
		r = kWQSuccess;
	}

	md_intr_x(iff);
	return r;
}

int
semaphore_signal(semaphore_t *sem)
{
	/* xxx TODO(high): can there be a race condition here? */
	if (++sem->count <= 0)
		return waitq_wake_one(&sem->wq);
	return 0;
}

thread_t *
thread_new(task_t *task, void (*fun)(void *arg), void *arg)
{
	thread_t *thread = kmem_alloc(sizeof *thread);
	bool	  iff;

	thread->state = kThreadRunnable;
	thread->task = task;
	thread->kstack = vm_kalloc(4, kVMKSleep) + 4 * PGSIZE;
	thread->ustack = NULL;

	iff = md_intr_disable();
	spinlock_lock(&sched_lock);
	SLIST_INSERT_HEAD(&task->threads, thread, taskthreads);
	thread->cpu = nextcpu();
	spinlock_unlock(&sched_lock);
	md_intr_x(iff);

	/* TODO(portability) */
	thread->md.frame.cs = 0x28;
	thread->md.frame.ss = 0x30;
	thread->md.frame.rflags = 0x202;
	thread->md.frame.rip = (uintptr_t)fun;
	thread->md.frame.rdi = (uintptr_t)arg;
	thread->md.frame.rbp = 0;
	thread->md.frame.rsp = (uintptr_t)thread->kstack;

	return thread;
}

void
thread_resume(thread_t *thread)
{
	bool iff;

	iff = md_intr_disable();
	spinlock_lock(&sched_lock);
	thread->state = kThreadRunnable;

	TAILQ_INSERT_HEAD(&thread->cpu->runqueue, thread, queue);

	spinlock_unlock(&sched_lock);
	if (thread->cpu == curcpu()) {
		if (thread->cpu->inInterrupt)
			thread->cpu->preempted = true;
		else
			sched_reschedule();
	} else {
		md_ipi_resched(thread->cpu);
	}
	md_intr_x(iff);
}

/*!
 * Select the most eligible thread to run next on this CPU, and remove it from
 * its runqueue.
 */
static thread_t *
sched_next(cpu_t *cpu) LOCK_REQUIRES(sched_lock)
{
	thread_t *cand;

	ASSERT_SPINLOCK_HELD(&sched_lock);

	cand = TAILQ_FIRST(&cpu->runqueue);
	if (!cand)
		cand = cpu->idlethread;
	else
		TAILQ_REMOVE(&cpu->runqueue, cand, queue);

	return cand;
}

void
sched_timeslice(md_intr_frame_t *frame, void *arg)
{
	curcpu()->preempted = 1;
}

void
sched_reschedule(void)
{
	cpu_t    *cpu;
	thread_t *oldthread, *next;
	bool iff;

	iff = md_intr_disable();
	spinlock_lock(&sched_lock);
	cpu = curcpu();
	oldthread = curthread();

	if (oldthread == cpu->idlethread) {
		/* idle thread shouldn't do any sleeping proper */
		assert(oldthread->state == kThreadRunning);
	} else if (oldthread->state == kThreadWaiting) {
		/* accounting will happen here some day */
		/*
		 * protocol indicates that if you are about to wait, you shall
		 * lock yourself and let Scheduler unlock you.
		 */
		spinlock_unlock(&oldthread->lock);
	} else if (oldthread->state == kThreadExiting) {
		kprintf("thread %s:%p exits\n", oldthread->task->name,
		    oldthread);
	} else if (oldthread->state == kThreadRunning) {
		TAILQ_INSERT_TAIL(&cpu->runqueue, oldthread, queue);
	}

	next = sched_next(cpu);
#if DEBUG_SCHED == 1
	kprintf("CPU %d: reschedule: old %p, next %p\n", cpu->num,
	    cpu->idlethread, oldthread, next);
#endif

	next->state = kThreadRunning;

	/* TODO(med): disable timeslicing if there are no threads except this
	 * eligible to run
	 */
	if (cpu->timeslicer.state == kCalloutDisabled) {
		cpu->timeslicer.nanosecs = NS_PER_S;
		callout_enqueue(&cpu->timeslicer);
	}

	if (next == oldthread) {
		spinlock_unlock(&sched_lock);
		md_intr_x(iff);
		return;
	}

	/* md_switch unlocks the sched_lock at the needful time */
	md_switch(oldthread, next);
	md_intr_x(iff);
}
