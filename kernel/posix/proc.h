/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef PROC_H_
#define PROC_H_

#include "kern/task.h"
#include "vfs.h"

#define CURPSXPROC() CURCPU()->curthread->task->pxproc

typedef LIST_HEAD(, proc) proc_list_t;

typedef struct proc {
	LIST_ENTRY(proc) subentry; /** links subs */

	enum {
		kProcNormal,	/** has runnable threads */
		kProcExiting,	/** all threads are exiting */
		kProcCompleted, /** awaiting supervisor notice of exit */
	} status;
	int wstat; /** wait status, for exiting/completed procs */

	struct proc *super; /** its superprocess */
	proc_list_t  subs;  /** its subprocesses; field sublist */

	task_t *task; /** VXK process */

	spinlock_t fdlock;    /** locks files */
	file_t    *files[64]; /** FD table */

	waitq_t waitwq; /** waitqueue for wait() calls */
} proc_t;

int sys_exit(proc_t *proc, int code);
int sys_fork(proc_t *proc, uintptr_t *errp);
int sys_waitpid(proc_t *proc, pid_t pid, int *status, int flags,
    uintptr_t *errp);

#endif /* PROC_H_ */
