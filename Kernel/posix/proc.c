/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#include <sys/wait.h>

#include <errno.h>

#include "libkern/klib.h"
#include "proc.h"

void arch_yield();

int
sys_exit(proc_t *proc, int code)
{
	spl_t spl = splsoft();

	thread_t *curthread, *thread;

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_EXIT(%d)\n", code);
#endif

	proc->status = kProcExiting;
	proc->wstat = W_EXITCODE(code, 0);
	curthread = CURCPU()->curthread;

	LIST_FOREACH (thread, &proc->task->threads, threads) {
		lock(&thread->lock);
		if (thread == curthread)
			continue;
		else
			fatal("can't exit multiple yet\n");
#if 0
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
#endif
		unlock(&thread->lock);
	}

	curthread->state = kExiting;
	curthread->should_exit = true;
	unlock(&curthread->lock);

	asm("cli");
	splx(spl);
	arch_yield();
	fatal("unreached\n");
	return 0;
}

void
proc_init(proc_t *proc, proc_t *super)
{
	proc->status = kProcNormal;
	LIST_INIT(&proc->subs);
	waitq_init(&proc->waitwq);
	spinlock_init(&proc->fdlock);

	proc->super = super;
	if (super) {
		LIST_INSERT_HEAD(&super->subs, proc, subentry);
	}
}

int
sys_fork(proc_t *proc, uintptr_t *errp)
{
	task_t   *newtask;
	thread_t *curthread = CURTHREAD(), *newthread;
	proc_t   *newproc;
	spl_t	  spl;

#if DEBUG_SYSCALLS == 1
	kprintf("SYS_FORK()\n");
#endif

	newtask = task_fork(proc->task);
	assert(newtask != NULL);

	newthread = thread_dup(curthread, newtask);
	assert(newthread != NULL);
	newthread->pcb.frame.rax = 0;
	newthread->pcb.frame.rdi = 0;

	newproc = kmalloc(sizeof *proc);
	proc_init(newproc, proc);
	newproc->task = newtask;
	for (int i = 0; i < ELEMENTSOF(newproc->files); i++) {
		newproc->files[i] = proc->files[i];
		if (proc->files[i])
			proc->files[i]->refcnt++;
	}

	newtask->pxproc = newproc;

	spl = spl0();
	thread_run(newthread);
	splx(spl);

	return newtask->pid;
}

int
sys_waitpid(proc_t *proc, pid_t pid, int *status, int flags, uintptr_t *errp)
{
	proc_t *subproc;

	if (pid != 0 && pid != -1) {
		fatal("sys_waitpid: unsupported pid %d\n", pid);
	}

	LIST_FOREACH (subproc, &proc->subs, subentry) {
		if (subproc->status == kProcCompleted) {
			*status = subproc->wstat;
			return subproc->task->pid;
		}
	}

	*errp = ENOSYS;

	return -1;
}
