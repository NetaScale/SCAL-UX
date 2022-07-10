/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include "libkern/klib.h"
#include "sys/param.h"
#include "sys/time.h"
#include "task.h"

#define NS_PER_MS 1000000

spinlock_t	  sched_lock;
struct task_queue alltasks = TAILQ_HEAD_INITIALIZER(alltasks);
task_t		  task0 = { .pid = 0, .name = "[kernel]" };
cpu_t	    *cpus = NULL;
size_t		  ncpus = 0;
size_t		  lastcpu = 0; /* cpu roundrobin */

/**
 * Signals/exit requests are implemented by adjusting the stack appropriately
 * either A) while readying a thread to run, if it is not currently within a
 * system call, or B) immediately before return from an interrupt/trap, if this
 * did not itself interrupt a system call.
 */

void	 arch_timer_set(uint64_t micros);
uint64_t arch_timer_get_remaining();

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
	callout->state = kCalloutPending;
next:
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
	TAILQ_INSERT_TAIL(&CURCPU()->dpcqueue, &co->dpc, dpcqueue);
	co->state = kCalloutPending;
	co = TAILQ_FIRST(queue);
	if (co != NULL)
		arch_timer_set(co->nanosecs / 1000);
	splx(spl);
}

void
dpcs_run()
{
	__auto_type queue = &CURCPU()->dpcqueue;

	while (true) {
		spl_t spl;
		void (*fun)(void *) = NULL;
		void  *arg;
		dpc_t *first;

		spl = splhigh();
		first = TAILQ_FIRST(queue);
		if (first != NULL) {
			first->bound = false;
			fun = first->fun;
			arg = first->arg;
			TAILQ_REMOVE(queue, first, dpcqueue);
		}
		splx(spl);

		if (!fun)
			break;

		fun(arg);
	}
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
	vm_activate(thr->proc->map);

	return 0;
}
