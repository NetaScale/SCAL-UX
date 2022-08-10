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

#include "machine/machdep.h"

task_t task0 = {
        .name = "[kernel]",
        .map = &kmap
};

thread_t thread0 = {
        .task = &task0
};

cpu_t cpu0 = {
        .num = -1,
        .curthread = &thread0
};

spinlock_t sched_lock = SPINLOCK_INITIALISER;
cpu_t    **cpus;

void
callout_enqueue(callout_t *callout)
{
	__auto_type queue = &curcpu()->pendingcallouts;
	callout_t *co;
	bool	   iff;

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

int
callout_interrupt(md_intr_frame_t *frame, void *unused)
{
	__auto_type queue = &curcpu()->pendingcallouts;
	callout_t *co;
	bool	   iff;
	int	   r = 0;

	iff = md_intr_disable();

	co = TAILQ_FIRST(queue);

	/* can have spurious ones */
	if (co == NULL)
		goto finish;

	TAILQ_REMOVE(queue, co, queue);
	co->state = kCalloutDisabled;
	r = co->callback(frame, co->arg);

	/* now set up the next in sequence */
	co = TAILQ_FIRST(queue);
	if (co != NULL)
		md_timer_set(co->nanosecs);

finish:
	md_intr_x(iff);
	return r;
}

void
task_init(void)
{
}

/*!
 * Select the most eligible thread to run next on this CPU.
 */
thread_t *
sched_next(cpu_t *cpu)
{
	thread_t *cand;

	ASSERT_SPINLOCK_HELD(&sched_lock);

	cand = TAILQ_FIRST(&cpu->runqueue);
	if (!cand)
		cand = cpu->idlethread;

	return cand;
}

/*!
 * Switch to a different thread. The thread should have marked its new state.
 * It should not have added itself to any queues (this is done here.)
 */
void
sched_switch(void)
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
		assert(oldthread->state == kThreadRunnable);
	} else if (oldthread->state == kThreadWaiting) {
		/* accounting will happen here some day */
	} else if (oldthread->state == kThreadExiting) {
		kprintf("thread %s:%p exits\n", oldthread->task->name,
		    oldthread);
	} else if (oldthread->state == kThreadRunnable) {
		TAILQ_INSERT_TAIL(&cpu->runqueue, oldthread, queue);
	}

	next = sched_next(cpu);

        if (next == oldthread) {
                spinlock_unlock(&sched_lock);
                md_intr_x(iff);
                return;
        }

        /* md_switch unlocks the sched_lock at the needful time */
        md_switch(oldthread, next);
        md_intr_x(iff);
}
