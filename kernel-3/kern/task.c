/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/queue.h>

#include <kern/kmem.h>
#include <kern/task.h>
#include <libkern/klib.h>

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
