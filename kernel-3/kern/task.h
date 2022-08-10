/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

/**
 * @file task.h
 * @brief Tasks and threads.
 */

#ifndef TASK_H_
#define TASK_H_

#include <kern/vm.h>
#include <machine/machdep.h>

typedef struct task {
	char	  name[31];
	vm_map_t *map;

	/*! linked by thread::taskthreads */
	SLIST_HEAD(, thread) threads;
} task_t;

enum thread_state {
	kThreadRunnable = 0,
	kThreadRunning,
	kThreadWaiting,
	kThreadExiting,
};

typedef struct thread {
	/*! linkage for task::threads */
	SLIST_ENTRY(thread) taskthreads;
	/*! linkage for cpu::runqueue or a wait queue or... */
	TAILQ_ENTRY(thread) queue;

	enum thread_state state;

	/*! task to which the thread belongs */
	task_t *task;

	/*! machine-dependent thread block */
	md_thread_t md;
} thread_t;

typedef struct cpu {
	int	  num;
	thread_t *curthread;

	thread_t *idlethread;

	/*! links thread::queue */
	TAILQ_HEAD(, thread) runqueue;

	/*! machine-dependent cpu block */
	md_cpu_t md;
} cpu_t;

static inline thread_t *
curthread()
{
	return curcpu()->curthread;
}

static inline task_t *
curtask()
{
	return curcpu()->curthread->task;
}

extern task_t	task0;
extern thread_t thread0;
extern cpu_t	cpu0;
extern cpu_t  **cpus;

#endif /* TASK_H_ */
