/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */


//#include "amd64.h"
#include "proc.h"

int
sys_exit(proc_t *proc, int code)
{
	spl_t spl = splsoft();
	thread_t *curthread, *thread;

	curthread = CURCPU()->curthread;
#if 0

	LIST_FOREACH (thread, &proc->task->threads, threads) {
		lock(&thread->lock);
		if (thread == curthread)
			continue;
		thread->should_exit = true;
		if (thread->state == kWaiting) {
			waitq_lock(thread->wq);
			waitq_clear_locked(thread, kWaitQResultInterrupted);
			waitq_unlock(thread->wq);
		} else if (thread->state == kRunning && !thread->in_syscall) {
			/* do an IPI or something if it's nonlocal */
			thread->should_exit = true;
			thread->state = kExiting;
		}
		unlock(&thread->lock);
	}

	curthread->should_exit = true;
	unlock(&curthread->lock);
#endif

	splx(spl);
	return 0;
}
