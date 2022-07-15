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

#define CURPSXPROC() CURCPU()->curthread->proc->pxproc

typedef struct proc {
	task_t *task; /* VXK process */

	spinlock_t fdlock;    /* locks files */
	file_t    *files[64]; /* FD table */
} proc_t;

int sys_exit(proc_t *proc, int code);

#endif /* PROC_H_ */
